/*
  Python bindings for class e2sar.

  Created by xmei@jlab.org on Mar/19/2024
*/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::string conversion
#include <pybind11/functional.h>

#include "e2sarUtil.hpp"
 
namespace py = pybind11;

using namespace e2sar;

// "e2sar_py" will be the python import module name
PYBIND11_MODULE(e2sar_py, m) {

    m.doc() = "Python bindings for the e2sar namesapce.";

    /**
     * Bindings for the E2SARErrorc enum class.
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
     * Bindings for the E2SARErrorInfo class.
     */
    py::class_<E2SARErrorInfo>(m, "E2SARErrorInfo")
        .def(py::init<E2SARErrorc, std::string>())
        .def("code", &E2SARErrorInfo::code)
        .def("message", &E2SARErrorInfo::message);

    /**
     * Bind a "IPAddress" class for future usage.
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
     * Binding for the result type containing std::string.
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
     * Binding for the result type containing std::pair<std::string, uint16_t>.
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
     * Binding for the result type containing std::pair<ip::address, uint16_t>.
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
     * Binding for the result type containing EjfatURI.
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
     * Binding for the e2sar::EjfatURI class.
     */
    py::class_<EjfatURI> EjfatURI(m, "EjfatURI");
    
    EjfatURI.def(py::init<const std::string &>(), "Set instance token from string");
  
    EjfatURI.def("get_use_tls", &EjfatURI::get_useTls);

    // Private variables and their get/set methods are dealt with class_::def_property()
    // Ref: https://pybind11.readthedocs.io/en/stable/classes.html#instance-and-static-fields
    EjfatURI.def_property("lb_name", &EjfatURI::get_lbName, &EjfatURI::set_lbName);
    EjfatURI.def_property("lb_id", &EjfatURI::get_lbId, &EjfatURI::set_lbId);

    EjfatURI.def("set_instance_token", &EjfatURI::set_InstanceToken);

    EjfatURI.def("set_sync_addr", &EjfatURI::set_syncAddr);
    EjfatURI.def("set_data_addr", &EjfatURI::set_dataAddr);

    EjfatURI.def("has_data_addr_v4", &EjfatURI::has_dataAddrv4);
    EjfatURI.def("has_data_addr_v6", &EjfatURI::has_dataAddrv6);

    EjfatURI.def("has_data_addr", &EjfatURI::has_dataAddr);
    EjfatURI.def("has_sync_addr", &EjfatURI::has_syncAddr);

    // Return types of result<std::string>.
    EjfatURI.def("get_instance_token", &EjfatURI::get_InstanceToken);
    EjfatURI.def("get_admin_token", &EjfatURI::get_AdminToken);

    // Return types of result<std::pair<ip::address, u_int16_t>>.
    EjfatURI.def("get_cp_addr", &EjfatURI::get_cpAddr);
    EjfatURI.def("get_data_addr_v4", &EjfatURI::get_dataAddrv4);
    EjfatURI.def("get_data_addr_v6", &EjfatURI::get_dataAddrv6);
    EjfatURI.def("get_sync_addr", &EjfatURI::get_syncAddr);

    // Return type of result<std::pair<std::string, u_int16_t>>.
    EjfatURI.def("get_cp_host", &EjfatURI::get_cpHost);

    ///TODO: not tested with Python.
    // Return type of result<EjfatURI>.
    // EjfatURI.def_static("getFromEnv", &EjfatURI::getFromEnv);
    // EjfatURI.def_static("getFromFile", &EjfatURI::getFromFile);
}
