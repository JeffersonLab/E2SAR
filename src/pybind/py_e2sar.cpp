#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "e2sar.hpp"

/**
 * Python bindings for the E2SAR library.
 * Created by xmei@jlab.org on Mar/19/2024
 *
 * Pybind11 built-in type conversion:
 *  https://pybind11.readthedocs.io/en/stable/advanced/cast/overview.html#list-of-all-builtin-conversions
*/


namespace py = pybind11;
using namespace e2sar;


// For binding the Result<> types
template <typename T>
void bind_result(py::module &m, const std::string &name) {
    py::class_<result<T>>(m, name.c_str())
        .def("value", [](const result<T> &res) { return res.value(); })
        .def("error", [](const result<T> &res) -> E2SARErrorInfo {
            if (!res) {
                return res.error();
            }
            throw std::runtime_error("No error present in ");
        })
        .def("has_error", [](const result<T> &res) {
            return !res.has_value();
        })
        .def("has_value", [](const result<T> &res) {
            return res.has_value();
        });
}


void init_e2sarResultTypes(py::module_ &m);

void init_e2sarCP(py::module_ &m);    // in a submodule "ControlPlane"
void init_e2sarUtil(py::module_ &m);  // in the main module
void init_e2sarAffinity(py::module_ &m);  // in the main module
void init_e2sarHeaders(py::module_ &m);  // in the main module
void init_e2sarDP(pybind11::module_ &m);    // in a submodule "DataPlane"

// "e2sar_py" will be the python module name using in "import xxx"
PYBIND11_MODULE(e2sar_py, m) {

    m.doc() = "Python bindings for the E2SAR library.";

    py::register_exception<E2SARException>(m, "E2SARException");

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
        .value("SystemError", E2SARErrorc::SystemError)
        .export_values();

    /**
     * Bind the E2SARErrorInfo class
     */
    py::class_<E2SARErrorInfo>(m, "E2SARErrorInfo")
        .def_property_readonly("code", &E2SARErrorInfo::code)
        .def_property_readonly("message", &E2SARErrorInfo::message)
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
    init_e2sarResultTypes(m);

    init_e2sarHeaders(m);
    init_e2sarUtil(m);
    init_e2sarCP(m);
    init_e2sarDP(m);
    init_e2sarAffinity(m);
}


/**
 * Bind specific Result<T> types (with no involvement of unique_ptr)
 */
void init_e2sarResultTypes(py::module_ &m)
{
    bind_result<int>(m, "E2SARResultInt");
    bind_result<std::string>(m, "E2SARResultString");
    bind_result<EjfatURI>(m, "E2SARResultEjfatURI");
    bind_result<grpc::SslCredentialsOptions>(m, "E2SARResultSslCredentialsOptions");
    bind_result<u_int32_t>(m, "E2SARResultUInt32");
    bind_result<std::pair<boost::asio::ip::address, uint16_t>>(m, "E2SARResultPairIP");
    bind_result<std::pair<std::string, uint16_t>>(m, "E2SARResultPairString");
    bind_result<std::list<std::pair<u_int16_t, size_t>>>(m, "E2SARResultListOfFDPairs");
    bind_result<std::pair<u_int64_t, uint16_t>>(m, "E2SARResultPairUInt64");
    bind_result<Reassembler::ReassemblerFlags>(m, "E2SARResultReassemblerFlags");
    bind_result<Segmenter::SegmenterFlags>(m, "E2SARResultSegmenterFlags");
}
