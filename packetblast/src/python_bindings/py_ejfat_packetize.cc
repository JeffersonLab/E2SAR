/*
  A demo for creating Python bindings to C++.

  Created by xmei@jlab.org on Mar/19/2024
*/

#include <pybind11/pybind11.h>

#include "ejfat_packetize.hpp"
 
namespace py = pybind11;

// "packetblast" will be the python import module name
PYBIND11_MODULE(packetblast, m) {
    m.doc() = "Python bindings for the ejfat namesapce.";  // TODO: update the namespace in the future.

    m.def("getMTU", &ejfat::getMTU, "Get the MTU of the network interface.\
      Example usage: getMTU('eth0', True)");
}
