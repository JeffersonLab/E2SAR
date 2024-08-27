
/**

 */
#define PYBIND11_DETAILED_ERROR_MESSAGES
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "e2sarDPReassembler.hpp"
#include "e2sarDPSegmenter.hpp"


#include <iostream>
#include <typeinfo>
#include <cxxabi.h>
#include <memory>

#include <boost/tuple/tuple.hpp>
#include <boost/any.hpp>

// Function to print demangled type names
template <typename T>
void print_type(const T& param) {
    int status;
    std::unique_ptr<char[], decltype(&std::free)> demangled_name(
        abi::__cxa_demangle(typeid(param).name(), nullptr, nullptr, &status), std::free);
    std::cout << "Type of parameter: "
              << (status == 0 ? demangled_name.get() : typeid(param).name())
              << std::endl;
}

namespace py = pybind11;
using namespace e2sar;

result<int> addToSendQueueWrapper(e2sar::Segmenter& seg, uint8_t *event, size_t bytes, 
                                  int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
                                  std::function<void(boost::any)> callback,
                                  boost::any cbArg) noexcept {
    // If callback is provided, wrap it in a lambda
    void (*c_callback)(boost::any) = nullptr;
    if (callback) {
        // Use a static function to store the lambda
        static std::function<void(boost::any)> static_callback;
        static_callback = callback;
        c_callback = [](boost::any arg) {
            static_callback(arg);
        };
    }

    // Call the actual addToSendQueue method
    return seg.addToSendQueue(event, bytes, _eventNum, _dataId, entropy, c_callback, cbArg);
}

void init_e2sarDP_reassembler(py::module_ &m);
void init_e2sarDP_segmenter(py::module_ &m);

void init_e2sarDP(py::module_ &m) {

    // Define the submodule "DataPlane"
    py::module_ e2sarDP = m.def_submodule("DataPlane", "E2SAR DataPlane submodule");

    init_e2sarDP_segmenter(e2sarDP);
    init_e2sarDP_reassembler(e2sarDP);
}

void init_e2sarDP_segmenter(py::module_ &m)
{
    py::class_<Segmenter> seg(m, "Segmenter");

    // Bind "SegmenterFlags" struct as a nested class of Segmenter
    py::class_<Segmenter::SegmenterFlags>(m, "SegmenterFlags")
        .def(py::init<>())  // The default values will be the same in Python after binding.
        .def_readwrite("dpV6", &Segmenter::SegmenterFlags::dpV6)
        .def_readwrite("zeroCopy", &Segmenter::SegmenterFlags::zeroCopy)
        .def_readwrite("connectedSocket", &Segmenter::SegmenterFlags::connectedSocket)
        .def_readwrite("useCP", &Segmenter::SegmenterFlags::useCP)
        .def_readwrite("syncPeriodMs", &Segmenter::SegmenterFlags::syncPeriodMs)
        .def_readwrite("syncPeriods", &Segmenter::SegmenterFlags::syncPeriods)
        .def_readwrite("mtu", &Segmenter::SegmenterFlags::mtu)
        .def_readwrite("numSendSockets", &Segmenter::SegmenterFlags::numSendSockets);

    // Constructor
    seg.def(
        py::init<const EjfatURI &, u_int16_t, u_int32_t, const Segmenter::SegmenterFlags &>(),
        "Init the Segmenter object.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_id"),
        py::arg("eventSrc_id"),
        py::arg("sflags") = Segmenter::SegmenterFlags());

    // Return type of result<int>
    seg.def("OpenAndStart", &Segmenter::openAndStart);

    // Invoke type contains py::bytes
    seg.def("addToSendQueue",
        [](e2sar::Segmenter& seg, py::bytes event, size_t bytes,
        int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
        py::object callback = py::none(), py::object cbArg = py::none()) -> result<int> {

            // Convert py::bytes to uint8_t*
            std::string event_str = event;
            uint8_t* event_ptr = reinterpret_cast<uint8_t*>(event_str.data());

            // Convert Python callback to std::function (if provided)
            std::function<void(boost::any)> c_callback = nullptr;
            if (!callback.is_none()) {
                c_callback = [callback](boost::any arg) {
                    // Invoke the Python callback with the provided argument
                    callback(arg);
                };
            }

            // Wrap the Python cbArg in boost::any (if provided)
            boost::any c_cbArg = nullptr;
            if (!cbArg.is_none()) {
                c_cbArg = cbArg;
            }

            // Call the wrapper function
            return addToSendQueueWrapper(seg, event_ptr, bytes, _eventNum, _dataId, entropy, c_callback, c_cbArg);
        },
        "Call Segmenter::addToSendQueue.",
        py::arg("event"),
        py::arg("bytes"),
        py::arg("_eventNum") = 0LL,
        py::arg("_dataId") = 0,
        py::arg("entropy") = 0,
        py::arg("callback") = py::none(),
        py::arg("cbArg") = py::none()
    );

    // Return type of boost::tuple<>
    seg.def("getSendStats", [](const Segmenter& segObj) {
            auto stats = segObj.getSendStats();
            return std::make_tuple(boost::get<0>(stats), boost::get<1>(stats), boost::get<2>(stats));
        });
}

void init_e2sarDP_reassembler(py::module_ &m)
{
    py::class_<Reassembler> reas(m, "Reassembler");

    // Bind the ReassemblerFlags struct as a nested class of Reassembler
    py::class_<Reassembler::ReassemblerFlags>(m, "ReassemblerFlags")
        .def(py::init<>())  // The default values will be the same in Python after binding.
        .def_readwrite("dpV6", &Reassembler::ReassemblerFlags::dpV6)
        .def_readwrite("cpV6", &Reassembler::ReassemblerFlags::cpV6)
        .def_readwrite("useCP", &Reassembler::ReassemblerFlags::useCP)
        .def_readwrite("period_ms", &Reassembler::ReassemblerFlags::period_ms)
        .def_readwrite("validateCert", &Reassembler::ReassemblerFlags::validateCert)
        .def_readwrite("Ki", &Reassembler::ReassemblerFlags::Ki)
        .def_readwrite("Kp", &Reassembler::ReassemblerFlags::Kp)
        .def_readwrite("Kd", &Reassembler::ReassemblerFlags::Kd)
        .def_readwrite("setPoint", &Reassembler::ReassemblerFlags::setPoint)
        .def_readwrite("epoch_ms", &Reassembler::ReassemblerFlags::epoch_ms)
        .def_readwrite("portRange", &Reassembler::ReassemblerFlags::portRange)
        .def_readwrite("withLBHeader", &Reassembler::ReassemblerFlags::withLBHeader)
        .def_readwrite("eventTimeout_ms", &Reassembler::ReassemblerFlags::eventTimeout_ms);

    // Constructor
    reas.def(
        py::init<const EjfatURI &, size_t, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with number of recv threads.",
        py::arg("uri"),  // must-have arg when init
        py::arg("num_recv_threads") = (size_t)1,
        py::arg("rflags") = Reassembler::ReassemblerFlags());
}
