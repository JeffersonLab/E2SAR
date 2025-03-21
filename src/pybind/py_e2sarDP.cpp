
/**

 */
#define PYBIND11_DETAILED_ERROR_MESSAGES
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "e2sarDPReassembler.hpp"
#include "e2sarDPSegmenter.hpp"

#include <iostream>
#include <typeinfo>
#include <cxxabi.h>
#include <memory>

#include <boost/tuple/tuple.hpp>
#include <boost/any.hpp>

// Helper function to print demangled type names
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

// Must have a wrapper because of the callback function.
result<int> addToSendQueueWrapper(Segmenter& seg, uint8_t *event, size_t bytes,
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
        .def_readwrite("zeroRate", &Segmenter::SegmenterFlags::zeroRate)
        .def_readwrite("usecAsEventNum", &Segmenter::SegmenterFlags::usecAsEventNum)
        .def_readwrite("syncPeriodMs", &Segmenter::SegmenterFlags::syncPeriodMs)
        .def_readwrite("syncPeriods", &Segmenter::SegmenterFlags::syncPeriods)
        .def_readwrite("mtu", &Segmenter::SegmenterFlags::mtu)
        .def_readwrite("numSendSockets", &Segmenter::SegmenterFlags::numSendSockets)
        .def_readwrite("sndSocketBufSize", &Segmenter::SegmenterFlags::sndSocketBufSize)
        .def("getFromINI", &Segmenter::SegmenterFlags::getFromINI);

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

    // Send event with py::buffer, which accepts python bytes input
    seg.def("sendEvent",
        [](Segmenter& self, py::buffer py_buf, size_t buf_len,
            EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy) -> result<int> {
                // Convert py::bytes to uint8_t*
                py::buffer_info buf_info = py_buf.request();
                uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

                return self.sendEvent(data, buf_len, _eventNum, _dataId, entropy);
        },
        "Send immediately overriding event number",
        py::arg("send_buf"),
        py::arg("buf_len"),
        py::arg("_eventNum") = 0LL,
        py::arg("_dataId") = 0,
        py::arg("entropy") = 0);

    // Send events part with numpy array.
    seg.def("sendNumpyArray",
        [](Segmenter& self, py::array numpy_array, size_t nbytes,
            EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy) -> result<int> {
            // Request buffer info from the numpy array
            py::buffer_info buf_info = numpy_array.request();
            uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

            return self.sendEvent(data, nbytes, _eventNum, _dataId, entropy);
        },
        "Send an event as a numpy array",
        py::arg("numpy_array"),
        py::arg("nbytes"),
        py::arg("event_num") = 0LL,
        py::arg("data_id") = 0,
        py::arg("entropy") = 0);

        // Send method related to callback. Have to define corresponding wrapper function.
        seg.def("addNumpyArrayToSendQueue",
            [](e2sar::Segmenter& seg, py::array numpy_array, size_t bytes,
            int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
            py::object callback = py::none(), py::object cbArg = py::none()) -> result<int> {
                // Convert py::bytes to uint8_t*
                py::buffer_info buf_info = numpy_array.request();
                uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

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
                return addToSendQueueWrapper(seg, data, bytes, _eventNum, _dataId, entropy, c_callback, c_cbArg);
            },
            "Call Segmenter::addToSendQueue with numpy array interface",
            py::arg("numpy_array"),
            py::arg("nbytes"),
            py::arg("_eventNum") = 0LL,
            py::arg("_dataId") = 0,
            py::arg("entropy") = 0,
            py::arg("callback") = py::none(),
            py::arg("cbArg") = py::none());

    // Send method related to callback. Have to define corresponding wrapper function.
    seg.def("addToSendQueue",
        [](e2sar::Segmenter& seg, py::buffer py_buf, size_t bytes,
        int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
        py::object callback = py::none(), py::object cbArg = py::none()) -> result<int> {
            // Convert py::bytes to uint8_t*
            py::buffer_info buf_info = py_buf.request();
            uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

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
            return addToSendQueueWrapper(seg, data, bytes, _eventNum, _dataId, entropy, c_callback, c_cbArg);
        },
        "Call Segmenter::addToSendQueue.",
        py::arg("send_buf"),
        py::arg("buf_len"),
        py::arg("_eventNum") = 0LL,
        py::arg("_dataId") = 0,
        py::arg("entropy") = 0,
        py::arg("callback") = py::none(),
        py::arg("cbArg") = py::none());

    // Return type of boost::tuple<>
    seg.def("getSendStats", [](const Segmenter& segObj) {
            auto stats = segObj.getSendStats();
            return std::make_tuple(boost::get<0>(stats), boost::get<1>(stats), boost::get<2>(stats));
        });
    seg.def("getSyncStats", [](const Segmenter& segObj) {
            auto stats = segObj.getSyncStats();
            return std::make_tuple(boost::get<0>(stats), boost::get<1>(stats), boost::get<2>(stats));
        });

    // Simple return types
    seg.def("getMTU", &Segmenter::getMTU);
    seg.def("getMaxPldLen", &Segmenter::getMaxPldLen);
    seg.def("stopThreads", &Segmenter::stopThreads);
}

void init_e2sarDP_reassembler(py::module_ &m)
{
    py::class_<Reassembler> reas(m, "Reassembler");

    // Bind the ReassemblerFlags struct as a nested class of Reassembler
    py::class_<Reassembler::ReassemblerFlags>(m, "ReassemblerFlags")
        .def(py::init<>())  // The default values will be the same in Python after binding.
        .def_readwrite("useCP", &Reassembler::ReassemblerFlags::useCP)
        .def_readwrite("useHostAddress", &Reassembler::ReassemblerFlags::useHostAddress)
        .def_readwrite("period_ms", &Reassembler::ReassemblerFlags::period_ms)
        .def_readwrite("validateCert", &Reassembler::ReassemblerFlags::validateCert)
        .def_readwrite("Ki", &Reassembler::ReassemblerFlags::Ki)
        .def_readwrite("Kp", &Reassembler::ReassemblerFlags::Kp)
        .def_readwrite("Kd", &Reassembler::ReassemblerFlags::Kd)
        .def_readwrite("weight", &Reassembler::ReassemblerFlags::weight)
        .def_readwrite("min_factor", &Reassembler::ReassemblerFlags::min_factor)
        .def_readwrite("max_factor", &Reassembler::ReassemblerFlags::max_factor)
        .def_readwrite("setPoint", &Reassembler::ReassemblerFlags::setPoint)
        .def_readwrite("epoch_ms", &Reassembler::ReassemblerFlags::epoch_ms)
        .def_readwrite("portRange", &Reassembler::ReassemblerFlags::portRange)
        .def_readwrite("withLBHeader", &Reassembler::ReassemblerFlags::withLBHeader)
        .def_readwrite("eventTimeout_ms", &Reassembler::ReassemblerFlags::eventTimeout_ms)
        .def_readwrite("rcvSocketBufSize", &Reassembler::ReassemblerFlags::rcvSocketBufSize)
        .def("getFromINI", &Reassembler::ReassemblerFlags::getFromINI);

    // Constructor
    reas.def(
        py::init<const EjfatURI &, ip::address, u_int16_t, size_t, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with number of recv threads.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_ip"),
        py::arg("starting_port"),
        py::arg("num_recv_threads") = (size_t)1,
        py::arg("rflags") = Reassembler::ReassemblerFlags());

    // Constructor with CPU core list.
    reas.def(
        py::init<const EjfatURI &, ip::address, u_int16_t, std::vector<int>, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with a list of CPU cores.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_ip"),
        py::arg("starting_port"),
        py::arg("cpu_core_list"),
        py::arg("rflags") = Reassembler::ReassemblerFlags());


    // Recv events part. Return py::tuple.
    reas.def("getEvent",
        [](Reassembler& self, /* py::list is mutable */ py::list& recv_bytes) -> py::tuple {
            u_int8_t *eventBuf{nullptr};
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);

            if (recvres.has_error()) {
                std::cout << "Error encountered receiving event frames: "
                    << recvres.error().message() << std::endl;
                return py::make_tuple((int)-2, eventLen, eventNum, recDataId);
            }
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else {
                // // Received event
                // std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen
                //     << " with event number " << eventNum << " and data id " << recDataId << std::endl;

                // Convert eventBuf to a Python bytes object and update the Python list with it
                recv_bytes[0] = py::bytes(reinterpret_cast<char*>(eventBuf), eventLen);
            }

            return py::make_tuple(recvres.value(), eventLen, eventNum, recDataId);
    },
    "Get an event from the Reassembler EventQueue. Use py::list[None] to accept the data.",
    py::arg("recv_bytes_list"));

    // Receive event as 1D numpy array with the provided numpy data type.
    reas.def("get1DNumpyArray",
        [](Reassembler& self, py::dtype data_type) -> py::tuple {
            u_int8_t *eventBuf{nullptr};
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);

            if (recvres.has_error()) {
                std::cout << "Error encountered receiving event frames: "
                    << recvres.error().message() << std::endl;
                return py::make_tuple(static_cast<int>(-2), py::array(), eventNum, recDataId);
            }

            if (recvres.value() == -1) {
                std::cout << "No message received, continuing" << std::endl;
                return py::make_tuple(static_cast<int>(-1), py::array(), eventNum, recDataId);
            }

            // Create a numpy array from the event buffer with the specified dtype
            py::ssize_t num_elements = static_cast<py::ssize_t>(eventLen) / data_type.itemsize();
            py::array numpy_array(data_type, num_elements, eventBuf);

            return py::make_tuple(recvres.value(), numpy_array, eventNum, recDataId);
        },
        "Get an event from the Reassembler EventQueue as 1D numpy array.",
        py::arg("data_type"));

        reas.def("recv1DNumpyArray",
            [](Reassembler& self, py::dtype data_type, u_int64_t wait_ms) -> py::tuple {
                u_int8_t *eventBuf{nullptr};
                size_t eventLen = 0;
                EventNum_t eventNum = 0;
                u_int16_t recDataId = 0;

                // Call the underlying recvEvent function
                auto recvres = self.recvEvent(&eventBuf, &eventLen, &eventNum, &recDataId, wait_ms);

                if (recvres.has_error()) {
                    std::cout << "Error encountered receiving event frames: "
                            << recvres.error().message() << std::endl;
                    return py::make_tuple(static_cast<int>(-2), py::array(), eventNum, recDataId);
                }

                if (recvres.value() == -1) {
                    std::cout << "No message received, continuing" << std::endl;
                    return py::make_tuple(static_cast<int>(-1), py::array(), eventNum, recDataId);
                }

                // Calculate the number of elements based on the item size of the specified data type
                py::ssize_t num_elements = static_cast<py::ssize_t>(eventLen) / data_type.itemsize();

                // Create a numpy array from the event buffer with the specified dtype
                py::array numpy_array(data_type, {num_elements}, eventBuf);

                return py::make_tuple(recvres.value(), numpy_array, eventNum, recDataId);
            },
            "Receive an event as a 1D numpy array in blocking mode.",
            py::arg("data_type"),
            py::arg("wait_ms") = 0);


    reas.def("recvEvent",
        [](Reassembler& self, /* py::list is mutable */ py::list& recv_bytes,
        u_int64_t wait_ms) -> py::tuple {
            u_int8_t *eventBuf{nullptr};
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.recvEvent(&eventBuf, &eventLen, &eventNum, &recDataId, wait_ms);

            if (recvres.has_error()) {
                std::cout << "Error encountered receiving event frames: "
                    << recvres.error().message() << std::endl;
                return py::make_tuple((int)-2, eventLen, eventNum, recDataId);
            }
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else {
                // // Received event
                // std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen
                //     << " with event number " << eventNum << " and data id " << recDataId << std::endl;

                // Convert eventBuf to a Python bytes object and update the Python list with it
                recv_bytes[0] = py::bytes(reinterpret_cast<char*>(eventBuf), eventLen);
            }

            return py::make_tuple(recvres.value(), eventLen, eventNum, recDataId);
    },
    "Get an event in the blocking mode. Use py::list[None] to accept the data.",
    py::arg("recv_bytes_list"),
    py::arg("wait_ms") = 0);

    // Return type of result<int>
    reas.def("OpenAndStart", &Reassembler::openAndStart);
    reas.def("registerWorker", &Reassembler::registerWorker);
    reas.def("deregisterWorker", &Reassembler::deregisterWorker);

    // Return type of resultresult<std::pair<EventNum_t, u_int16_t>>
    reas.def("get_LostEvent", &Reassembler::get_LostEvent);

    // Return type of boost::tuple<>: convert to std::tuple
    reas.def("getStats", [](const Reassembler& reasObj) {
            auto stats = reasObj.getStats();
            return std::make_tuple(boost::get<0>(stats), boost::get<1>(stats), boost::get<2>(stats),
                                    boost::get<3>(stats), boost::get<4>(stats), boost::get<5>(stats));
        });

    // Simple return types
    reas.def("get_numRecvThreads", &Reassembler::get_numRecvThreads);
    reas.def("get_recvPorts", &Reassembler::get_recvPorts);
    reas.def("get_portRange", &Reassembler::get_portRange);
    reas.def("stopThreads", &Reassembler::stopThreads);
}
