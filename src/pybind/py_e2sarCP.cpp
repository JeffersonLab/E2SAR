#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <google/protobuf/timestamp.pb.h>

#include "e2sarUtil.hpp"
#include "e2sarCP.hpp"

namespace py = pybind11;
using namespace e2sar;

// Convert outcome::result to a Python object
template<typename T, typename E>
py::object outcome_result_to_pyobject(outcome::result<T, E>& res) {
    if (res) {
        return py::cast(res.value().release());
    } else {
        // Handle the error case (for simplicity, converting to a string here)
        return py::none();
    }
}

// Function to convert C++ Timestamp to Python Timestamp
py::object convert_timestamp_to_python(const google::protobuf::Timestamp& ts) {
    py::module_ protobuf = py::module_::import("google.protobuf.timestamp_pb2");
    py::object py_timestamp = protobuf.attr("Timestamp")();
    py_timestamp.attr("seconds") = ts.seconds();
    py_timestamp.attr("nanos") = ts.nanos();
    return py_timestamp;
}

// Function to convert Python Timestamp to C++ Timestamp
google::protobuf::Timestamp convert_timestamp_to_cpp(py::object py_timestamp) {
    google::protobuf::Timestamp ts;
    ts.set_seconds(py_timestamp.attr("seconds").cast<int64_t>());
    ts.set_nanos(py_timestamp.attr("nanos").cast<int32_t>());
    return ts;
}

void init_e2sarCP(py::module_ &m) {
    // Define the submodule "ControlPlane"
    py::module_ e2sarCP = m.def_submodule("ControlPlane", "E2SAR ControlPlane submodule");

    // Expose the LoadBalancerStatusReply class
    py::class_<LoadBalancerStatusReply>(e2sarCP, "LoadBalancerStatusReply")
        .def(py::init<>());

    py::class_<google::protobuf::Timestamp>(m, "Timestamp")
        .def(py::init<>())
        .def("get_seconds", &google::protobuf::Timestamp::seconds)
        .def("get_nanos", &google::protobuf::Timestamp::nanos)
        .def("set_seconds", &google::protobuf::Timestamp::set_seconds)
        .def("set_nanos", &google::protobuf::Timestamp::set_nanos);

    py::class_<LBWorkerStatus>(e2sarCP, "LBWorkerStatus")
        .def(py::init<const std::string&, float, float, int, const google::protobuf::Timestamp&>(),
            py::arg("name"),
            py::arg("fill_percent"),
            py::arg("control_signal"),
            py::arg("slots_assigned"),
            py::arg("last_updated"))
        .def_readwrite("name", &LBWorkerStatus::name)
        .def_readwrite("fill_percent", &LBWorkerStatus::fillPercent)
        .def_readwrite("control_signal", &LBWorkerStatus::controlSignal)
        .def_readwrite("slots_assigned", &LBWorkerStatus::slotsAssigned)
        .def_property("last_updated",
            [](const LBWorkerStatus &self) {
                return convert_timestamp_to_python(self.lastUpdated); // Access the member directly
            },
            [](LBWorkerStatus &self, py::object py_timestamp) {
                self.lastUpdated = convert_timestamp_to_cpp(py_timestamp); // Assign the converted value
            }
        );

    // Bind LBStatus
    /// TODO: check with Python tests; method with move()
    py::class_<LBStatus>(e2sarCP, "LBStatus")
        /// TODO: No binding for method with move()
        .def(py::init<>());

    /**
     * Binding the LBManager class as "LBManager" in the submodule "ControlPlane".
     * This has never been test in Python with a real or a mock LB.
     *
     * TODO: Python mock LB test
     */
    py::class_<LBManager> lb_manager(e2sarCP, "LBManager");

    // Constructor
    lb_manager.def(
        py::init<const EjfatURI &, bool, grpc::SslCredentialsOptions>(),
        py::arg("cpuri"),
        py::arg("validate_server") = true,
        py::arg("opts") = grpc::SslCredentialsOptions()
    );

    lb_manager.def("get_version", &LBManager::version);

    // Bind LBManager::reserveLB to use duration in seconds
    // It is specifically designed to interface with Python datetime.timedelta
    /// NOTE: std::vector<std::string> will map to the Python data type of: List[str]
    lb_manager.def(
        "reserve_lb_seconds",
        static_cast<result<u_int32_t> (LBManager::*)(
            const std::string &,
            const double &,
            const std::vector<std::string> &
            )>(&LBManager::reserveLB),
        py::arg("lb_id"), py::arg("seconds"), py::arg("senders")
    );

    /// NOTE: Bindings for overloaded methods.
    ///       Ref: https://pybind11.readthedocs.io/en/stable/classes.html#overloaded-methods
    lb_manager.def(
        "get_lb",
        static_cast<result<int> (LBManager::*)()>(&LBManager::getLB)
    );

    lb_manager.def(
        "get_lb_by_id",
        static_cast<result<int> (LBManager::*)(
            const std::string &
        )>(&LBManager::getLB),
        py::arg("lb_id")
    );

    lb_manager.def(
        "free_lb",
        static_cast<result<int> (LBManager::*)()>(&LBManager::freeLB)
    );

    lb_manager.def(
        "free_lb_by_id",
        static_cast<result<int> (LBManager::*)(
            const std::string &
        )>(&LBManager::freeLB),
        py::arg("lb_id")
    );

    lb_manager.def("register_worker", &LBManager::registerWorker);
    lb_manager.def("deregister_worker", &LBManager::deregisterWorker);

    lb_manager.def(
        "send_state",
        static_cast<result<int> (LBManager::*)(float, float, bool)>(&LBManager::sendState),
        "Send worker state update using session ID and session token from register call.",
        py::arg("fill_percent"), py::arg("control_signal"), py::arg("is_ready")
    );

    /// TODO: type cast between C++ google::protobuf::Timestamp between Python datetime package
    // lb_manager.def(
    //     "send_state_with_timestamp",
    //     static_cast<result<int> (LBManager::*)(float, float, bool, const Timestamp&)>(&LBManager::sendState),
    //     "Send worker state update using session ID and session token from register call with explicit timestamp.",
    //     py::arg("fill_percent"), py::arg("control_signal"), py::arg("is_ready"), py::arg("timestamp")
    // );

    /**
     * Return type containing std::unique_ptr<LoadBalancerStatusReply>
     */

    lb_manager.def(
        "get_lb_status",
        [](LBManager& self){
            auto result = self.getLBStatus();
            return outcome_result_to_pyobject(result);
        }
    );

    lb_manager.def(
        "get_lb_status_by_id",
        [](LBManager& self, const std::string & lbid){
            auto result = self.getLBStatus(lbid);
            return outcome_result_to_pyobject(result);
        },
        py::arg("lb_id")
    );

    lb_manager.def("make_ssl_options", &LBManager::makeSslOptions);

    lb_manager.def("get_port_range", &get_PortRange);

    // Return an EjfatURI object.
    lb_manager.def("get_uri", &LBManager::get_URI, py::return_value_policy::reference);

    /// NOTE: donot need to bind LBManager::makeSslOptionsFromFiles

    /// TODO: donot how to bind
    /// - LBManager::get_WorkerStatusVector
    /// - LBManager::get_SenderAddressVector
    /// Datatype too complicated
}
