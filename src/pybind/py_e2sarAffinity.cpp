#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>

#include "e2sarAffinity.hpp"

namespace py = pybind11;
using namespace e2sar;


void init_e2sarAffinity(py::module_ &m) {
    py::class_<Affinity, std::unique_ptr<Affinity, py::nodelete>>(m, "Affinity")
        .def_static(
            "set_process", &Affinity::setProcess,
            py::arg("cores"),
            "Set the affinity of the entire process to the cores in the vector")
        .def_static(
            "set_thread", &Affinity::setThread,
            py::arg("core"),
            "Set calling thread affinity to the specified core.")
        .def_static(
            "set_thread_xor", &Affinity::setThreadXOR,
            py::arg("cores"),
            "Set calling thread affinity to exclude specified cores.")
        .def_static(
            "set_numa_bind", &Affinity::setNUMABind,
            py::arg("node"),
            "Bind process memory allocation to the specified NUMA node.");
}
