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
     * Bindings for optional worker stats
     */
    py::class_<WorkerStats>(e2sarCP, "WorkerStats")
        .def(py::init<>())
        .def_readwrite("total_events_recv", &WorkerStats::total_events_recv)
        .def_readwrite("total_events_reassembled", &WorkerStats::total_events_reassembled)
        .def_readwrite("total_events_reassembly_err", &WorkerStats::total_events_reassembly_err)
        .def_readwrite("total_events_dequeued", &WorkerStats::total_events_dequeued)
        .def_readwrite("total_event_enqueue_err", &WorkerStats::total_event_enqueue_err)
        .def_readwrite("total_bytes_recv", &WorkerStats::total_bytes_recv)
        .def_readwrite("total_packets_recv", &WorkerStats::total_packets_recv);

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
        .def_readonly("syncIPv4AndPort", &OverviewEntry::syncIPv4AndPort)
        .def_readonly("syncIPv6AndPort", &OverviewEntry::syncIPv6AndPort)        
        .def_readonly("data_ip_v4", &OverviewEntry::dataIPv4)
        .def_readonly("data_ip_v6", &OverviewEntry::dataIPv6)
        .def_readonly("fpga_lb_id", &OverviewEntry::fpgaLBId)
        .def_readonly("data_min_port", &OverviewEntry::dataMinPort)
        .def_readonly("data_max_port", &OverviewEntry::dataMaxPort)
        .def_readonly("lb_status", &OverviewEntry::status);

    /**
     * Bindings for timeseries structs
     */
    py::class_<FloatSample>(e2sarCP, "FloatSample")
        .def(py::init<>(),
            "Default constructor")
        .def(py::init<int64_t, float>(),
            py::arg("timestamp_ms"), py::arg("value"),
            "Create a float sample with timestamp and value")
        .def_readwrite("timestamp_ms", &FloatSample::timestamp_ms,
            "Timestamp in milliseconds since epoch")
        .def_readwrite("value", &FloatSample::value,
            "Float sample value");

    py::class_<IntegerSample>(e2sarCP, "IntegerSample")
        .def(py::init<>(),
            "Default constructor")
        .def(py::init<int64_t, int64_t>(),
            py::arg("timestamp_ms"), py::arg("value"),
            "Create an integer sample with timestamp and value")
        .def_readwrite("timestamp_ms", &IntegerSample::timestamp_ms,
            "Timestamp in milliseconds since epoch")
        .def_readwrite("value", &IntegerSample::value,
            "Integer sample value");

    py::class_<TimeseriesData>(e2sarCP, "TimeseriesData")
        .def_readonly("path", &TimeseriesData::path,
            "Timeseries path identifier")
        .def_readonly("unit", &TimeseriesData::unit,
            "Unit of measurement for samples")

        // Helper method to check which type is held
        .def("is_float", [](const TimeseriesData &self) {
            return std::holds_alternative<std::vector<FloatSample>>(self.timeseries);
        }, "Check if timeseries contains float samples")

        .def("is_integer", [](const TimeseriesData &self) {
            return std::holds_alternative<std::vector<IntegerSample>>(self.timeseries);
        }, "Check if timeseries contains integer samples")

        // Methods to get the samples
        .def("get_float_samples", [](const TimeseriesData &self) -> py::list {
            if (!std::holds_alternative<std::vector<FloatSample>>(self.timeseries)) {
                throw std::runtime_error("Timeseries does not contain float samples");
            }
            py::list result;
            for (const auto& sample : std::get<std::vector<FloatSample>>(self.timeseries)) {
                result.append(sample);
            }
            return result;
        }, "Get list of FloatSample objects (raises error if not float type)")

        .def("get_integer_samples", [](const TimeseriesData &self) -> py::list {
            if (!std::holds_alternative<std::vector<IntegerSample>>(self.timeseries)) {
                throw std::runtime_error("Timeseries does not contain integer samples");
            }
            py::list result;
            for (const auto& sample : std::get<std::vector<IntegerSample>>(self.timeseries)) {
                result.append(sample);
            }
            return result;
        }, "Get list of IntegerSample objects (raises error if not integer type)");

    py::class_<TimeseriesResult>(e2sarCP, "TimeseriesResult")
        .def_readonly("since_ms", &TimeseriesResult::since_ms,
            "Timestamp in milliseconds since epoch for start of timeseries")
        .def_readonly("td", &TimeseriesResult::td,
            "List of TimeseriesData objects");

    /**
     * Bindings for token management structs
     */
    py::class_<e2sar::TokenPermission>(e2sarCP, "TokenPermission")
        .def(py::init<>(),
            "Default constructor")
        .def(py::init<EjfatURI::TokenType, const std::string&, EjfatURI::TokenPermission>(),
            py::arg("resource_type"), py::arg("resource_id"), py::arg("permission"),
            "Create token permission with resource type, ID, and permission level")
        .def_readwrite("resourceType", &e2sar::TokenPermission::resourceType,
            "Type of resource (admin, instance, session, all)")
        .def_readwrite("resourceId", &e2sar::TokenPermission::resourceId,
            "Resource identifier (can be empty string for wildcards)")
        .def_readwrite("permission", &e2sar::TokenPermission::permission,
            "Permission level (read_only, register, reserve, update)");

    py::class_<e2sar::TokenDetails>(e2sarCP, "TokenDetails")
        .def(py::init<>(),
            "Default constructor")
        .def_readonly("name", &e2sar::TokenDetails::name,
            "Human-readable token name")
        .def_readonly("permissions", &e2sar::TokenDetails::permissions,
            "List of TokenPermission objects")
        .def_readonly("created_at", &e2sar::TokenDetails::created_at,
            "Creation timestamp as string")
        .def_readonly("id", &e2sar::TokenDetails::id,
            "Numeric token ID");

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
            const std::vector<std::string> &,
            int ip_family
            )>(&LBManager::reserveLB),
        py::arg("lb_id"), py::arg("seconds"), py::arg("senders"), py::arg("ip_family")
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
        static_cast<result<int> (LBManager::*)(float, float, bool, const WorkerStats&)>(&LBManager::sendState),
        "Send worker state update using session ID and session token from register call.",
        py::arg("fill_percent"), py::arg("control_signal"), py::arg("is_ready"),
        py::arg("stats")
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
    lb_manager.def("add_sender_self",  &LBManager::addSenderSelf);
    lb_manager.def("remove_sender_self",  &LBManager::removeSenderSelf);
    lb_manager.def("register_worker_self", &LBManager::registerWorkerSelf);

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

    // Return connect string.
    lb_manager.def("get_addr_string", &LBManager::get_AddrString);

    /**
     * Token management methods
     */
    lb_manager.def(
        "create_token",
        &LBManager::createToken,
        py::arg("name"), py::arg("permissions"),
        "Create a new delegated token with specific permissions.\n\n"
        "Args:\n"
        "    name (str): Human-readable token name\n"
        "    permissions (List[TokenPermission]): List of token permissions\n\n"
        "Returns:\n"
        "    result<str>: The created token string on success"
    );

    lb_manager.def(
        "list_token_permissions_by_id",
        [](LBManager& self, uint32_t id) {
            return self.listTokenPermissions(id);
        },
        py::arg("token_id"),
        "List all permissions for a token by numeric ID.\n\n"
        "Args:\n"
        "    token_id (int): Numeric token ID\n\n"
        "Returns:\n"
        "    result<TokenDetails>: Token details with permissions"
    );

    lb_manager.def(
        "list_token_permissions_by_string",
        [](LBManager& self, const std::string& token) {
            return self.listTokenPermissions(token);
        },
        py::arg("token"),
        "List all permissions for a token by token string.\n\n"
        "Args:\n"
        "    token (str): Token string\n\n"
        "Returns:\n"
        "    result<TokenDetails>: Token details with permissions"
    );

    lb_manager.def(
        "list_child_tokens_by_id",
        [](LBManager& self, uint32_t id) {
            return self.listChildTokens(id);
        },
        py::arg("parent_token_id"),
        "List all child tokens created by a parent token (by ID).\n\n"
        "Args:\n"
        "    parent_token_id (int): Numeric parent token ID\n\n"
        "Returns:\n"
        "    result<List[TokenDetails]>: List of child token details"
    );

    lb_manager.def(
        "list_child_tokens_by_string",
        [](LBManager& self, const std::string& token) {
            return self.listChildTokens(token);
        },
        py::arg("parent_token"),
        "List all child tokens created by a parent token (by string).\n\n"
        "Args:\n"
        "    parent_token (str): Parent token string\n\n"
        "Returns:\n"
        "    result<List[TokenDetails]>: List of child token details"
    );

    lb_manager.def(
        "revoke_token_by_id",
        [](LBManager& self, uint32_t id) {
            return self.revokeToken(id);
        },
        py::arg("token_id"),
        "Revoke a token and all its children by numeric ID.\n\n"
        "Args:\n"
        "    token_id (int): Numeric token ID\n\n"
        "Returns:\n"
        "    result<int>: 0 on success"
    );

    lb_manager.def(
        "revoke_token_by_string",
        [](LBManager& self, const std::string& token) {
            return self.revokeToken(token);
        },
        py::arg("token"),
        "Revoke a token and all its children by token string.\n\n"
        "Args:\n"
        "    token (str): Token string\n\n"
        "Returns:\n"
        "    result<int>: 0 on success"
    );

    /**
     * Timeseries method
     */
    lb_manager.def(
        "timeseries",
        &LBManager::timeseries,
        py::arg("path"), py::arg("since"),
        "Retrieve timeseries data for a specific metric path.\n\n"
        "Args:\n"
        "    path (str): Timeseries path selector (e.g., '/lb/1/*', '/lb/1/session/2/totalEventsReassembled')\n"
        "    since (Timestamp): Timestamp to retrieve data from\n\n"
        "Returns:\n"
        "    result<TimeseriesResult>: TimeseriesResult containing samples"
    );

    /// NOTE: donot need to bind LBManager::makeSslOptionsFromFiles
}
