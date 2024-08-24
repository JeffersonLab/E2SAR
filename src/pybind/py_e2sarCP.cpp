#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "e2sarUtil.hpp"
#include "e2sarCP.hpp"

/**
 * Create a Python submodule named "ControlPlane".
 * Created by xmei@jlab.org on Aug/15/24.
*/

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


void init_e2sarCP(py::module_ &m) {
    // Define the submodule "ControlPlane"
    py::module_ e2sarCP = m.def_submodule("ControlPlane", "E2SAR ControlPlane submodule");

    // Expose the LoadBalancerStatusReply class
    py::class_<LoadBalancerStatusReply>(e2sarCP, "LoadBalancerStatusReply")
        .def(py::init<>());

    // Bind LBWorkerStatus
    /// TODO: check with Python tests
    py::class_<LBWorkerStatus>(e2sarCP, "LBWorkerStatus")
        // Binding the constructor with parameters
        .def(
            py::init<const std::string&, float, float, uint32_t, google::protobuf::Timestamp>(),
            py::arg("name"),
            py::arg("fill_percentage"),
            py::arg("control_signal"),
            py::arg("slots_assigned"),
            py::arg("last_updated")
        );

    // Bind LBStatus
    /// TODO: check with Python tests; method with move()
    py::class_<LBStatus>(e2sarCP, "LBStatus")
        /// TODO: No binding for method with move()
        .def(py::init<>());

    /* Python code for LBWorkerStatus and LBStatus objects.
    // NOTE: delete in the future.
    import example
    from google.protobuf.timestamp_pb2 import Timestamp

    # Create a Timestamp object
    timestamp = Timestamp()
    timestamp.GetCurrentTime()

    # Create LBWorkerStatus objects
    worker1 = example.LBWorkerStatus("Worker1", 75.5, 0.9, 10, timestamp)
    worker2 = example.LBWorkerStatus("Worker2", 80.2, 1.1, 15, timestamp)

    workers = [worker1, worker2]
    sender_addresses = ["address1", "address2"]

    # Create an LBStatus object
    status = example.LBStatus(timestamp, 1234, 5678, workers, sender_addresses, timestamp)

    # Access members
    print(status.currentEpoch)
    print(status.senderAddresses)
    print([worker.name for worker in status.workers])
    */

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
