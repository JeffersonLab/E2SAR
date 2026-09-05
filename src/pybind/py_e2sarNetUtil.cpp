#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "e2sarNetUtil.hpp"

namespace py = pybind11;
using namespace e2sar;


void init_e2sarNetUtil(py::module_ &m) {
    py::class_<NetUtil>(m, "NetUtil")
        .def_static(
            "is_non_routable",
            py::overload_cast<const std::string &>(&NetUtil::isNonRoutable),
            py::arg("addr"),
            "Check if an IP address string is non-routable (private/loopback/link-local/ULA).")
        .def_static(
            "is_any_non_routable",
            py::overload_cast<const std::vector<std::string> &>(&NetUtil::isNonRoutable),
            py::arg("addrs"),
            "Check if any address in the list is non-routable.")
        .def_static(
            "get_mtu", &NetUtil::getMTU,
            py::arg("interface_name"),
            "Get MTU of a given interface (returns 1500 if lookup fails).")
        .def_static(
            "get_hostname", &NetUtil::getHostName,
            "Get the hostname of the host.");
}
