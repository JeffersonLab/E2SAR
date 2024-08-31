#include <pybind11/pybind11.h>
#include <pybind11/operators.h>

#include "e2sarUtil.hpp"

namespace py = pybind11;
using namespace e2sar;

void init_e2sarUtil(py::module_ &m) {

    // Bind the EjfatURI class in the main module
    py::class_<EjfatURI> ejfat_uri(m, "EjfatURI");

    py::enum_<EjfatURI::TokenType>(ejfat_uri, "TokenType")
        .value("admin", EjfatURI::TokenType::admin)
        .value("instance", EjfatURI::TokenType::instance)
        .value("session", EjfatURI::TokenType::session)
        .export_values();

    // Constructor
    ejfat_uri.def(
        py::init<const std::string &, EjfatURI::TokenType, bool>(),
        py::arg("uri"), py::arg("tt") = EjfatURI::TokenType::admin, py::arg("preferV6") = false,
        "Set instance token from string"
        );

    // Private variables and their get/set methods are dealt with class_::def_property()
    // Ref: https://pybind11.readthedocs.io/en/stable/classes.html#instance-and-static-fields
    ejfat_uri.def_property("lb_name", &EjfatURI::get_lbName, &EjfatURI::set_lbName);
    ejfat_uri.def_property("lb_id", &EjfatURI::get_lbId, &EjfatURI::set_lbId);
    ejfat_uri.def_property("session_id", &EjfatURI::get_sessionId, &EjfatURI::set_sessionId);

    ejfat_uri.def("get_useTls", &EjfatURI::get_useTls);

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

    // Bind the conversion operator std::string()
    ejfat_uri.def("__str__", [](const EjfatURI &self) {
        return static_cast<std::string>(self);
    });

    // Operator
    ejfat_uri.def(pybind11::self == pybind11::self);
    ejfat_uri.def(pybind11::self != pybind11::self);

    ejfat_uri.def(
        "to_string", &EjfatURI::to_string,
        py::arg("tt") = EjfatURI::TokenType::admin
        );

    // Return type of result<EjfatURI>.
    ejfat_uri.def_static(
        "get_from_env", &EjfatURI::getFromEnv,
        py::arg("env_var") = "EJFAT_URI",
        py::arg("tt") = EjfatURI::TokenType::admin,
        py::arg("preferV6") = false,
        "Create EjfatURI object from environment variable 'EJFAT_URI'."
    );
    ejfat_uri.def_static(
        "get_from_string", &EjfatURI::getFromString,
        py::arg("_str"),
        py::arg("tt") = EjfatURI::TokenType::admin,
        py::arg("preferV6") = false,
        "Create EjfatURI object from string."
    );
    ejfat_uri.def_static(
        "get_from_file", &EjfatURI::getFromFile,
        py::arg("_filename") = "/tmp/ejfat_uri",
        py::arg("tt") = EjfatURI::TokenType::admin,
        py::arg("preferV6") = false,
        "Create EjfatURI object from a path indicated by a string."
    );
}
