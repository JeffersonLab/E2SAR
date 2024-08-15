/**
 * Python bindings for the E2SAR library.
 * Created by xmei@jlab.org on Mar/19/2024
 *
 * Pybind11 guides:
 * 1. Built-in type conversion:
 *  https://pybind11.readthedocs.io/en/stable/advanced/cast/overview.html#list-of-all-builtin-conversions
 * 2. Export C++ variables:
 *  https://pybind11.readthedocs.io/en/stable/basics.html#exporting-variables
*/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::string conversion
#include <pybind11/functional.h>

#include "e2sar.hpp"

namespace py = pybind11;

using namespace e2sar;

/// TODO: better bindings for the result<> type. May try to connect with Python try... except... @xmei
void initBindingResultType(pybind11::module_ &m);

void init_e2sarCP(pybind11::module_ &m);    // in a submodule "ControlPlane"
void init_e2sarUtil(pybind11::module_ &m);  // in the main module

// "e2sar_py" will be the python import module name
PYBIND11_MODULE(e2sar_py, m) {

    m.doc() = "Python bindings for the E2SAR library.";

    // Bind the get_Version method
    m.def("__version__", &get_Version);

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
     * Register grpc::SslCredentialsOptions.
     * The dependency chain of e2sar::LBManager.
     */
    py::class_<grpc::SslCredentialsOptions>(m, "SslCredentialsOptions")
        .def(py::init<>());

    // For all the returning datatype of result<...>
    initBindingResultType(m);

    /**
     * Bind the e2sar::EjfatURI class etc
     */
    init_e2sarUtil(m);

    /**
     * TODO: fix this statement.
     * Bind the e2sar::LBManager class to the Python e2sar_py.lbManager.lb_manager class.
     * e2sar_py.lbManager is a submodule.
     */
    init_e2sarCP(m);
}

void initBindingResultType(pybind11::module_ &m)
{
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
}
