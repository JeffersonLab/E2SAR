#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // For binding STL containers like std::array
#include <array>

#include "e2sarHeaders.hpp"

namespace py = pybind11;
using namespace e2sar;

void init_e2sarHeaders(py::module_ &m) {

    // Bind REHdr struct into Python class
    py::class_<REHdr>(m, "REHdr")
        .def(py::init<>())
        .def("set", &REHdr::set,
            py::arg("data_id") = 0,
            py::arg("buff_off") = 0,
            py::arg("buff_len") = 0,
            py::arg("event_num") = 0
            )
        .def("get_eventNum", &REHdr::get_eventNum)
        .def("get_bufferLength", &REHdr::get_bufferLength)
        .def("get_bufferOffset", &REHdr::get_bufferOffset)
        .def("get_dataId", &REHdr::get_dataId)
        .def("get_headerVersion", &REHdr::get_HeaderVersion)
        .def("validate", &REHdr::validate)
        .def("get_fields", [](const REHdr &hdr) {
            auto fields = hdr.get_Fields();
            // Convert boost::tuple to std::tuple, which can be mapped to Python
            return std::tuple<std::uint16_t, std::uint32_t, std::uint32_t, EventNum_t>{
                fields.get<0>(), fields.get<1>(), fields.get<2>(), fields.get<3>()
            };
        });

    // Bind LBHdr struct into Python class
    py::class_<LBHdr>(m, "LBHdr")
        .def(py::init<>())
        .def("set", &LBHdr::set,
            py::arg("entropy") = 0,
            py::arg("event_num") = 0
            )
        .def("get_version", &LBHdr::get_version)
        .def("get_nextProto", &LBHdr::get_nextProto)
        .def("get_entropy", &LBHdr::get_entropy)
        .def("get_eventNum", &LBHdr::get_eventNum)
        .def("get_fields", [](const LBHdr &hdr) {
            auto fields = hdr.get_Fields();
            // Convert boost::tuple to std::tuple, which can be mapped to Python
            return std::tuple<std::uint8_t, std::uint8_t, std::uint16_t, EventNum_t>{
                fields.get<0>(), fields.get<1>(), fields.get<2>(), fields.get<3>()
            };
        });

    py::class_<LBREHdr>(m, "LBREHdr")
        .def(py::init<>());

    // Bind SyncHdr into Python class
    py::class_<SyncHdr>(m, "SyncHdr")
        .def(py::init<>())
        .def("set", &SyncHdr::set,
            py::arg("event_srcId") = 0,
            py::arg("event_num") = 0,
            py::arg("avg_rate") = 0,
            py::arg("unix_time_nano") = 0
            )
        .def("get_eventSrcId", &SyncHdr::get_eventSrcId)
        .def("get_eventNumber", &SyncHdr::get_eventNumber)
        .def("get_avgEventRateHz", &SyncHdr::get_avgEventRateHz)
        .def("get_unixTimeNano", &SyncHdr::get_unixTimeNano)
        .def("get_fields", [](const SyncHdr &hdr) {
            auto fields = hdr.get_Fields();
            // Convert boost::tuple to std::tuple, which can be mapped to Python
            return std::tuple<std::uint32_t, std::uint64_t, std::uint32_t, std::uint64_t>{
                fields.get<0>(), fields.get<1>(), fields.get<2>(), fields.get<3>()
            };
    });
}
