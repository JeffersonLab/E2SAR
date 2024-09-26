#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

#include "e2sarUtil.hpp"
#include "e2sarCP.hpp"

namespace py = pybind11;
using namespace e2sar;

// Convert outcome::result to a Python object. It's for unique_ptr situations.
template<typename T, typename E>
py::object outcome_result_to_pyobject(outcome::result<T, E>& res) {
    if (res) {
        return py::cast(res.value().release());
    } else {
        // Handle the error case (for simplicity, printing the error message here).
        std::cerr << "Error: " << res.error().message() << std::endl;
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

    // Basic return types
    e2sarCP.def("get_port_range", &get_PortRange);

    // Expose the grpc classes
    py::class_<WorkerStatus>(e2sarCP, "WorkerStatus")
        .def(py::init<>())
        .def("get_name", &WorkerStatus::name)
        .def("get_fill_percent", &WorkerStatus::fillpercent)
        .def("get_control_signal", &WorkerStatus::controlsignal)
        .def("get_slots_assigned", &WorkerStatus::slotsassigned)
        .def("get_last_updated",
            [](const WorkerStatus &self) {
                return google::protobuf::util::TimeUtil::ToString(self.lastupdated());
            }
        );
    py::class_<LoadBalancerStatusReply>(e2sarCP, "LoadBalancerStatusReply")
        .def(py::init<>());
    py::class_<OverviewReply>(e2sarCP, "OverviewReply")
        .def(py::init<>());

    py::class_<google::protobuf::Timestamp>(e2sarCP, "Timestamp")
        .def(py::init<>())
        .def("get_seconds", &google::protobuf::Timestamp::seconds)
        .def("get_nanos", &google::protobuf::Timestamp::nanos)
        .def("set_seconds", &google::protobuf::Timestamp::set_seconds)
        .def("set_nanos", &google::protobuf::Timestamp::set_nanos)
        // pretty print in Python: use TimeUtil::ToString to get the default string representation
        .def("__str__", [](const google::protobuf::Timestamp& self) {
            return google::protobuf::util::TimeUtil::ToString(self);
    });

    /**
     * Bindings for struct "LBWorkerStatus"
     */
    py::class_<LBWorkerStatus>(e2sarCP, "LBWorkerStatus")
        .def(py::init<const std::string&, float, float, int, const google::protobuf::Timestamp&>(),
            py::arg("name"),
            py::arg("fill_percent"),
            py::arg("control_signal"),
            py::arg("slots_assigned"),
            py::arg("last_updated")
        )
        // For params return from the LB, bind them as _readonly instead of _readwrite 
        .def_readonly("name", &LBWorkerStatus::name)
        .def_readonly("fill_percent", &LBWorkerStatus::fillPercent)
        .def_readonly("control_signal", &LBWorkerStatus::controlSignal)
        .def_readonly("slots_assigned", &LBWorkerStatus::slotsAssigned)
        .def_property_readonly("last_updated",
            [](const LBWorkerStatus &self) {
                return google::protobuf::util::TimeUtil::ToString(self.lastUpdated); // Access the member directly
            }
        );

    /**
     * Bindings for struct "LBStatus"
     */
    py::class_<LBStatus>(e2sarCP, "LBStatus")
        // Default constructor
        .def(py::init<>())

        // Constructor with arguments using move semantics for workers and senderAddresses
        .def(py::init<
            google::protobuf::Timestamp,
            /* Basic types */ u_int64_t, u_int64_t,
            std::vector<WorkerStatus>&,
            std::vector<std::string>&,
            google::protobuf::Timestamp>(),
            py::arg("timestamp"),
            py::arg("current_epoch"),
            py::arg("current_predicted_event_number"),
            py::arg("worker_status_list"),
            py::arg("sender_addresses"),
            py::arg("expires_at"))

        // Expose the struct members
        .def_property_readonly(/* a Python string */"timestamp",
            [](const LBStatus &self) {
                return google::protobuf::util::TimeUtil::ToString(self.timestamp);
            })
        .def_readonly("currentEpoch", &LBStatus::currentEpoch)
        .def_readonly("currentPredictedEventNumber", &LBStatus::currentPredictedEventNumber)
        .def_readonly("workerStatusList", &LBStatus::workers)
        .def_readonly("senderAddressList", &LBStatus::senderAddresses)
        .def_property_readonly(/* a Python string */"expiresAt",
            [](const LBStatus &self) {
                return google::protobuf::util::TimeUtil::ToString(self.expiresAt); // Access the member directly
            });
    
    /**
     * Bindings for struct "OverviewEntry"
     */
    py::class_<OverviewEntry>(e2sarCP, "OverviewEntry")
        .def(py::init<>())
        
        .def_readonly("name", &OverviewEntry::name)
        .def_readonly("lb_id", &OverviewEntry::lbid)
        .def_readonly("syncAddressAndPort", &OverviewEntry::syncAddressAndPort)
        .def_readonly("data_ip_v4", &OverviewEntry::dataIPv4)
        .def_readonly("data_ip_v6", &OverviewEntry::dataIPv6)
        .def_readonly("fpga_lb_id", &OverviewEntry::fpgaLBId)
        .def_readonly("lb_status", &OverviewEntry::status);

    /**
     * Bind the `e2sar::LBManager` class as "LBManager" in the submodule "ControlPlane".
     */
    py::class_<LBManager> lb_manager(e2sarCP, "LBManager");

    // Constructor
    lb_manager.def(
        py::init<const EjfatURI &, bool, bool, grpc::SslCredentialsOptions>(),
        py::arg("cpuri"),
        py::arg("validate_server") = true,
        py::arg("use_host_address") = false,
        py::arg("opts") = grpc::SslCredentialsOptions()
    );

    // Return type contains result<boost::tuple<...>>: 
    // - transform boost::tuple to std::tuple
    // - return std::tuple
    lb_manager.def("get_version", [](LBManager &self) {
        auto result = self.version();

        // Check if the result contains an error
        if (result.has_error()) {
            // You can define a Python exception or use a standard one
            throw std::runtime_error(result.error().message());
        }

        // Extract the tuple and return it
        auto val = result.value();
        return py::make_tuple(val.get<0>(), val.get<1>(), val.get<2>());
    });

    // Bind LBManager::reserveLB to use duration in seconds
    // It is specifically designed to interface with Python datetime.timedelta
    /// NOTE: std::vector<std::string> will map to the Python data type of: List[str]
    lb_manager.def(
        "reserve_lb_in_seconds",
        static_cast<result<u_int32_t> (LBManager::*)(
            const std::string &,
            const double &,
            const std::vector<std::string> &
            )>(&LBManager::reserveLB),
        py::arg("lb_id"), py::arg("seconds"), py::arg("senders")
    );

    /// NOTE: Bindings for overloaded methods.
    /// https://pybind11.readthedocs.io/en/stable/classes.html#overloaded-methods
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

    // C++14 above usage. No py::arg() allowed. Must define as def_static
    lb_manager.def_static(
        "get_sender_addresses",
        py::overload_cast<const LoadBalancerStatusReply &>(&LBManager::get_SenderAddressVector),
        "Retrieve sender addresses as a list of strings."
    );
    lb_manager.def_static(
        "get_worker_statuses",
        py::overload_cast<const LoadBalancerStatusReply &>(&LBManager::get_WorkerStatusVector),
        "Retrieve worker status as a list of ControlPlane.WorkerStatus objects."
    );
    lb_manager.def_static(
        "as_lb_status",
        py::overload_cast<const LoadBalancerStatusReply &>(&LBManager::asLBStatus),
        "Helper function copies LoadBalancerStatusReply protobuf into a simpler object."
    );
    lb_manager.def_static(
        "as_overview_message",
        py::overload_cast<const OverviewReply &>(&LBManager::asOverviewMessage),
        "Helper function copies OverviewReply protobuf into a simpler object."
    );

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

    // Simple return types of result<int>
    lb_manager.def("add_senders",  &LBManager::addSenders);
    lb_manager.def("remove_senders",  &LBManager::removeSenders);
    lb_manager.def("register_worker", &LBManager::registerWorker);
    lb_manager.def("deregister_worker", &LBManager::deregisterWorker);

    /**
     * Return type containing result<std::unique_ptr<LoadBalancerStatusReply>>
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

    /**
     * Return type containing result<std::unique_ptr<OverviewReply>>
     */
    lb_manager.def(
        "get_lb_overview",
        [](LBManager& self){
            auto result = self.overview();
            return outcome_result_to_pyobject(result);
        }
    );

    lb_manager.def("make_ssl_options", &LBManager::makeSslOptions);

    // Return an EjfatURI object.
    lb_manager.def("get_uri", &LBManager::get_URI, py::return_value_policy::reference);

    // return connect string
    lb_manager.def("get_addrstring", &LBManager::get_AddrString, py::return_value_policy::reference);

    /// NOTE: donot need to bind LBManager::makeSslOptionsFromFiles
}
