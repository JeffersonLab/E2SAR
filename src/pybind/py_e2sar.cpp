/*
  Python bindings for class e2sar.

  Created by xmei@jlab.org on Mar/19/2024
*/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::string conversion
#include <pybind11/functional.h>

#include "e2sarCP.hpp"
 
namespace py = pybind11;

using namespace e2sar;

// "e2sar_py" will be the python import module name
PYBIND11_MODULE(e2sar_py, m) {

    m.doc() = "Python bindings for the e2sar namesapce.";

    /**
     * Bind the E2SARErrorc enum class
     */
    py::enum_<E2SARErrorc>(m, "E2SARErrorc")
        .value("NoError", E2SARErrorc::NoError)
        .value("CaughtException", E2SARErrorc::CaughtException)
        .value("ParseError", E2SARErrorc::ParseError)
        .value("ParameterError", E2SARErrorc::ParameterError)
        .value("ParameterNotAvailable", E2SARErrorc::ParameterNotAvailable)
        .value("OutOfRange", E2SARErrorc::OutOfRange)
        .value("Undefined", E2SARErrorc::Undefined)
        .value("NotFound", E2SARErrorc::NotFound)
        .value("RPCError", E2SARErrorc::RPCError)
        .export_values();

    /**
     * Bind the E2SARErrorInfo class
     */
    py::class_<E2SARErrorInfo>(m, "E2SARErrorInfo")
        .def(py::init<E2SARErrorc, std::string>())
        .def("code", &E2SARErrorInfo::code)
        .def("message", &E2SARErrorInfo::message);

    /**
     * Bind "IPAddress" class for future usage
     */
    py::class_<boost::asio::ip::address>(m, "IPAddress")
        .def(py::init<>())
        .def_static("from_string", static_cast<boost::asio::ip::address(*)(const std::string&)>(
            &boost::asio::ip::address::from_string))
        .def("is_v4", &boost::asio::ip::address::is_v4)
        .def("is_v6", &boost::asio::ip::address::is_v6)
        .def("__str__", [](const boost::asio::ip::address &addr) {
            return addr.to_string();
        });

    /** 
     * Bind the result type containing std::string
     */
    py::class_<outcome::result<std::string, E2SARErrorInfo>>(m, "E2SARResultString")
        .def("value", [](const outcome::result<std::string, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<std::string, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });
  
    /**
     * Binding the result type containing std::pair<std::string, uint16_t>
     */
    py::class_<outcome::result<std::pair<std::string, uint16_t>, E2SARErrorInfo>>(m, "E2SARResultPairString")
        .def("value", [](const outcome::result<std::pair<std::string, uint16_t>, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<std::pair<std::string, uint16_t>, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });

    /**
     * Bind the result type containing std::pair<ip::address, uint16_t>
     */
    py::class_<outcome::result<std::pair<boost::asio::ip::address, uint16_t>, E2SARErrorInfo>>(m, "E2SARResultPairIP")
        .def("value", [](const outcome::result<std::pair<boost::asio::ip::address, uint16_t>, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<std::pair<boost::asio::ip::address, uint16_t>, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });

    /**
     * Bind the result type containing EjfatURI
     */
    py::class_<outcome::result<EjfatURI, E2SARErrorInfo>>(m, "E2SARResultEjfatURI")
        .def("value", [](const outcome::result<EjfatURI, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<EjfatURI, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });

    /**
     * Bind the result type containing int.
     */
    py::class_<outcome::result<int, E2SARErrorInfo>>(m, "E2SARResultInt")
        .def("value", [](const outcome::result<int, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<int, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });

    /**
     * Bind the result type containing grpc::SslCredentialsOptions
     */
    py::class_<outcome::result<grpc::SslCredentialsOptions, E2SARErrorInfo>>(
        m, "E2SARResultSslCredentialsOptions")
        .def("value", [](const outcome::result<grpc::SslCredentialsOptions, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<grpc::SslCredentialsOptions, E2SARErrorInfo> &res) {
            if (!res)
                return res.error();
            else
                throw std::runtime_error("No error present");
        });

    /**
     * Bind the e2sar::EjfatURI class.
     */

    /// NOTE: Do not use py::class_<EjfatURI> EjfatURI(m, "EjfatURI").
    ///       This will cause trouble for the classes depend on it!
    py::class_<EjfatURI> ejfat_uri(m, "EjfatURI");

    py::enum_<EjfatURI::TokenType>(ejfat_uri, "TokenType")
        .value("admin", EjfatURI::TokenType::admin)
        .value("instance", EjfatURI::TokenType::instance)
        .value("session", EjfatURI::TokenType::session)
        .export_values();

    // Constructor
    ejfat_uri.def(
        py::init<const std::string &, EjfatURI::TokenType>(),
        py::arg("uri"), py::arg("tt") = EjfatURI::TokenType::admin,
        "Set instance token from string"
        );
  
    ejfat_uri.def("get_use_tls", &EjfatURI::get_useTls);

    // Private variables and their get/set methods are dealt with class_::def_property()
    // Ref: https://pybind11.readthedocs.io/en/stable/classes.html#instance-and-static-fields
    ejfat_uri.def_property("lb_name", &EjfatURI::get_lbName, &EjfatURI::set_lbName);
    ejfat_uri.def_property("lb_id", &EjfatURI::get_lbId, &EjfatURI::set_lbId);
    ejfat_uri.def_property("session_id", &EjfatURI::get_sessionId, &EjfatURI::set_sessionId);

    ejfat_uri.def("set_instance_token", &EjfatURI::set_InstanceToken);
    ejfat_uri.def("set_session_token", &EjfatURI::set_SessionToken);

    ejfat_uri.def("set_sync_addr", &EjfatURI::set_syncAddr);
    ejfat_uri.def("set_data_addr", &EjfatURI::set_dataAddr);

    ejfat_uri.def("has_data_addr_v4", &EjfatURI::has_dataAddrv4);
    ejfat_uri.def("has_data_addr_v6", &EjfatURI::has_dataAddrv6);

    ejfat_uri.def("has_data_addr", &EjfatURI::has_dataAddr);
    ejfat_uri.def("has_sync_addr", &EjfatURI::has_syncAddr);

    // Return types of result<std::string>.
    ejfat_uri.def("get_instance_token", &EjfatURI::get_InstanceToken);
    ejfat_uri.def("get_session_token", &EjfatURI::get_SessionToken);
    ejfat_uri.def("get_admin_token", &EjfatURI::get_AdminToken);

    // Return types of result<std::pair<ip::address, u_int16_t>>.
    ejfat_uri.def("get_cp_addr", &EjfatURI::get_cpAddr);
    ejfat_uri.def("get_data_addr_v4", &EjfatURI::get_dataAddrv4);
    ejfat_uri.def("get_data_addr_v6", &EjfatURI::get_dataAddrv6);
    ejfat_uri.def("get_sync_addr", &EjfatURI::get_syncAddr);

    // Return type of result<std::pair<std::string, u_int16_t>>.
    ejfat_uri.def("get_cp_host", &EjfatURI::get_cpHost);

    ///TODO: not tested with Python.
    ejfat_uri.def(
        "to_string", &EjfatURI::to_string,
        py::arg("tt") = EjfatURI::TokenType::admin
        );
    // Return type of result<EjfatURI>.
    ejfat_uri.def_static("get_from_env", &EjfatURI::getFromEnv);
    ejfat_uri.def_static("get_from_string", &EjfatURI::getFromString);
    ejfat_uri.def_static("get_from_file", &EjfatURI::getFromFile);

    /**
     * Register grpc::SslCredentialsOptions.
     * The dependency chain of e2sar::LBManager.
     */
    py::class_<grpc::SslCredentialsOptions>(m, "SslCredentialsOptions")
        .def(py::init<>());

    /**
     * Bind the LBManager
     */
    py::class_<LBManager> lb_manager(m, "LBManager");
    
    // Constructor
    lb_manager.def(
        py::init<const EjfatURI &, bool &, grpc::SslCredentialsOptions>(),
        py::arg("cpuri"), py::arg("validate_server") = true, py::arg("opts") = grpc::SslCredentialsOptions()
        );

    lb_manager.def_property_readonly("is_reserved", &LBManager::isReserved);

    // Bind LBManager::reserveLB to use duration in seconds
    // It is specifically designed to interface with Python datetime.timedelta
    lb_manager.def(
        "reserve_lb_seconds",
        static_cast<result<int> (LBManager::*)(
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

    // Return an EjfatURI object.
    lb_manager.def("get_uri", &LBManager::get_URI, py::return_value_policy::reference);

    // lb_manager.def("get_lb_status", &LBManager::getLBStatus);
}
