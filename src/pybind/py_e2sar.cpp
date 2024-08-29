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
void initBindingResultType(py::module_ &m);

void init_e2sarCP(py::module_ &m);    // in a submodule "ControlPlane"
void init_e2sarUtil(py::module_ &m);  // in the main module
void init_e2sarHeaders(py::module_ &m);  // in the main module
void init_e2sarDP(pybind11::module_ &m);    // in a submodule "DataPlane"

// "e2sar_py" will be the python module name using in "import xxx"
PYBIND11_MODULE(e2sar_py, m) {

    m.doc() = "Python bindings for the E2SAR library.";

    // Constants belong to the top module
    m.attr("_dp_port") = py::int_(DATAPLANE_PORT);
    m.attr("_iphdr_len") = py::int_(IP_HDRLEN);
    m.attr("_udphdr_len") = py::int_(UDP_HDRLEN);
    m.attr("_total_hdr_len") = py::int_(TOTAL_HDR_LEN);
    m.attr("_rehdr_version") = py::int_(rehdrVersion);
    m.attr("_rehdr_version_nibble") = py::int_(rehdrVersionNibble);
    m.attr("_lbhdr_version") = py::int_(lbhdrVersion);
    m.attr("_synchdr_version") = py::int_(synchdrVersion);

    // Bind the get_Version method
    m.def("get_version", &get_Version);

    /// TODO: try bind the E2SARErrorc enum class to submodule "e2sarError" to make the top module looks nicer.
    // py::module_ e2sar_ec = m.def_submodule("e2sarError", "E2SAR e2sarError submodule");

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
        .value("SocketError", E2SARErrorc::SocketError)
        .value("MemoryError", E2SARErrorc::MemoryError)
        .value("LogicError", E2SARErrorc::LogicError)
        .export_values();

    /**
     * Bind the E2SARErrorInfo class
     */
    py::class_<E2SARErrorInfo>(m, "E2SARErrorInfo")
        .def_property_readonly("get_code", &E2SARErrorInfo::code)
        .def_property_readonly("get_message", &E2SARErrorInfo::message)
        .def(
            "__repr__", [](const E2SARErrorInfo &err) {
                return "<E2SARErrorInfo(code=" + std::to_string(static_cast<int>(err.code())) +
                ", message='" + err.message() + "')>";
            });

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

    init_e2sarHeaders(m);

    init_e2sarUtil(m);

    init_e2sarCP(m);
    init_e2sarDP(m);
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
        .def("value", [](const result<int> &res) { return res.value(); })
        .def("error", [](const result<int> &res) -> E2SARErrorInfo {
            if (!res) {
                return res.error();
            }
            throw std::runtime_error("No error present");
        })
        .def("has_error", [](const result<int> &res) {
            return !res.has_value();
        })
        .def("has_value", [](const result<int> &res) {
            return res.has_value();
        });

    /**
     * Bind the result type containing uint32_t.
     */
    py::class_<outcome::result<u_int32_t, E2SARErrorInfo>>(m, "E2SARResultUInit32")
        .def("value", [](const outcome::result<u_int32_t, E2SARErrorInfo> &res) {
            if (res)
                return res.value();
            else
                throw std::runtime_error(res.error().message());
        })
        .def("error", [](const outcome::result<u_int32_t, E2SARErrorInfo> &res) {
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

/// TODO: look into templated result<> bindings
// template <typename T>
// void bind_result(py::module &m, const std::string &name) {
//     py::class_<result<T>>(m, name.c_str())
//         .def("value", [](const result<T> &res) { return res.value(); })
//         .def("error", [](const result<T> &res) -> E2SARErrorInfo {
//             if (!res) {
//                 return res.error();
//             }
//             throw std::runtime_error("No error present in result");
//         })
//         .def("has_error", [](const result<T> &res) {
//             return !res.has_value();
//         })
//         .def("has_value", [](const result<T> &res) {
//             return res.has_value();
//         });
// }

// PYBIND11_MODULE(your_module, m) {
//     // Bind specific result types
//     bind_result<int>(m, "ResultInt");
//     bind_result<std::string>(m, "ResultString");
//     bind_result<EjfatURI>(m, "ResultEjfatURI");
// }
