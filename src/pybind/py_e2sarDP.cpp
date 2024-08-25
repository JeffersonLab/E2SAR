
/**

 */

#include <pybind11/pybind11.h>

#include "e2sarDPReassembler.hpp"
#include "e2sarDPSegmenter.hpp"

namespace py = pybind11;
using namespace e2sar;

void init_e2sarDP_reassembler(py::module_ &m);

void init_e2sarDP(py::module_ &m) {

    // Define the submodule "DataPlane"
    py::module_ e2sarDP = m.def_submodule("DataPlane", "E2SAR DataPlane submodule");

    init_e2sarDP_reassembler(e2sarDP);

    // py::class_<Segmenter> seg = segmenter(e2sarDP, "Segmenter");
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

    /// TODO: constructor bindings can compile but donot work on the Python end
    // Constructor
    reas.def(py::init<const EjfatURI &, size_t, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with number of recv threads.",
        py::arg("uri"),  // must-have arg when init
        py::arg("num_recv_threads") = (size_t)1,
        py::arg("rflags") = Reassembler::ReassemblerFlags());

    // Custom method to create an instance using the second constructor
    reas.def_static("from_thread_num",
        [](const EjfatURI &uri, size_t numRecvThreads = 1,
            const Reassembler::ReassemblerFlags &rflags = e2sar::Reassembler::ReassemblerFlags()) {
            return e2sar::Reassembler(uri, numRecvThreads, rflags);
        },
        "Init the Reassembler object with number of recv threads.",
        py::arg("uri"),
        py::arg("thread_num") = 1,
        py::arg("rflags") = Reassembler::ReassemblerFlags());
}
