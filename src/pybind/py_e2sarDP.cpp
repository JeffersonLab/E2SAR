
/**

 */

#include <pybind11/pybind11.h>

#include "e2sarDPReassembler.hpp"
#include "e2sarDPSegmenter.hpp"

namespace py = pybind11;
using namespace e2sar;


void init_e2sarDP(py::module_ &m) {

    // Define the submodule "DataPlane"
    py::module_ e2sarDP = m.def_submodule("DataPlane", "E2SAR DataPlane submodule");


    py::class_<Reassembler> reassembler(e2sarDP, "Reassembler");

    py::class_<Segmenter> segmenter(e2sarDP, "Segmenter");
}
