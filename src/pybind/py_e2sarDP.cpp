
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

// Thread-safe callback wrapper that avoids storing Python objects
struct PythonCallbackWrapper {
    PyObject* callback_ptr;
    PyObject* cbArg_ptr;
    
    PythonCallbackWrapper(py::object cb, py::object arg) {
        // Must hold GIL when manipulating Python objects
        pybind11::gil_scoped_acquire acquire;
        // Increment reference count and store raw pointers
        callback_ptr = cb.is_none() ? nullptr : cb.inc_ref().ptr();
        cbArg_ptr = arg.is_none() ? nullptr : arg.inc_ref().ptr();
    }
    
    ~PythonCallbackWrapper() {
        // Must acquire GIL before decrementing reference counts
        if (callback_ptr || cbArg_ptr) {
            pybind11::gil_scoped_acquire acquire;
            if (callback_ptr) {
                Py_DECREF(callback_ptr);
            }
            if (cbArg_ptr) {
                Py_DECREF(cbArg_ptr);
            }
        }
    }
    
    static void execute(boost::any wrapper_any) {
        PythonCallbackWrapper* wrapper = nullptr;
        try {
            wrapper = boost::any_cast<PythonCallbackWrapper*>(wrapper_any);
            if (wrapper && wrapper->callback_ptr) {
                pybind11::gil_scoped_acquire acquire;
                py::object callback = py::reinterpret_borrow<py::object>(wrapper->callback_ptr);
                py::object cbArg = wrapper->cbArg_ptr ? 
                    py::reinterpret_borrow<py::object>(wrapper->cbArg_ptr) : py::none();
                callback(cbArg);
            }
        } catch (...) {
            // Ignore callback execution errors
        }
        
        // Always clean up wrapper
        delete wrapper;
    }
};

void init_e2sarDP_reassembler(py::module_ &m);
void init_e2sarDP_segmenter(py::module_ &m);

void init_e2sarDP(py::module_ &m) {
    // Define the submodule "DataPlane"
    py::module_ e2sarDP = m.def_submodule("DataPlane", "E2SAR DataPlane submodule");

    init_e2sarDP_segmenter(e2sarDP);
    init_e2sarDP_reassembler(e2sarDP);
}

void init_e2sarDP_segmenter(py::module_ &m) {
    py::class_<Segmenter> seg(m, "Segmenter");

    // Bind "SegmenterFlags" struct as a nested class of Segmenter
    py::class_<Segmenter::SegmenterFlags>(seg, "SegmenterFlags")
        .def(py::init<>())  // The default values will be the same in Python after binding.
        .def_readwrite("dpV6", &Segmenter::SegmenterFlags::dpV6)
        .def_readwrite("connectedSocket", &Segmenter::SegmenterFlags::connectedSocket)
        .def_readwrite("useCP", &Segmenter::SegmenterFlags::useCP)
        .def_readwrite("syncPeriodMs", &Segmenter::SegmenterFlags::syncPeriodMs)
        .def_readwrite("syncPeriods", &Segmenter::SegmenterFlags::syncPeriods)
        .def_readwrite("mtu", &Segmenter::SegmenterFlags::mtu)
        .def_readwrite("numSendSockets", &Segmenter::SegmenterFlags::numSendSockets)
        .def_readwrite("sndSocketBufSize", &Segmenter::SegmenterFlags::sndSocketBufSize)
        .def_readwrite("rateGbps", &Segmenter::SegmenterFlags::rateGbps)
        .def("getFromINI", &Segmenter::SegmenterFlags::getFromINI);

    // Constructor-simple
    seg.def(
        py::init<const EjfatURI &, u_int16_t, u_int32_t, const Segmenter::SegmenterFlags &>(),
        "Init the Segmenter object.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_id"),
        py::arg("eventSrc_id"),
        py::arg("sflags") = Segmenter::SegmenterFlags());

    // Constructor-corelist
    seg.def(
        py::init<const EjfatURI &, u_int16_t, u_int32_t,
                    std::vector<int>, const Segmenter::SegmenterFlags &>(),
        "Init the Segmenter object with CPU core list (with Python list)",
        py::arg("uri"),
        py::arg("data_id"),
        py::arg("eventSrc_id"),
        py::arg("cpu_core_list"),
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

    // Send method related to callback with thread-safe implementation
    seg.def("addNumpyArrayToSendQueue",
        [](e2sar::Segmenter& seg, py::array numpy_array, size_t bytes,
        int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
        py::object callback = py::none(), py::object cbArg = py::none()) -> result<int> {
            
            py::buffer_info buf_info = numpy_array.request();
            uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

            void (*c_callback)(boost::any) = nullptr;
            boost::any c_cbArg = boost::any();
            
            if (!callback.is_none()) {
                // Create heap-allocated wrapper for thread-safe callback
                auto* wrapper = new PythonCallbackWrapper(callback, cbArg);
                c_callback = PythonCallbackWrapper::execute;
                c_cbArg = wrapper;
            }

            // Call the C++ method directly - no wrapper needed
            return seg.addToSendQueue(data, bytes, _eventNum, _dataId, entropy, c_callback, c_cbArg);
        },
        "Call Segmenter::addToSendQueue with numpy array interface",
        py::arg("numpy_array"),
        py::arg("nbytes"),
        py::arg("_eventNum") = 0LL,
        py::arg("_dataId") = 0,
        py::arg("entropy") = 0,
        py::arg("callback") = py::none(),
        py::arg("cbArg") = py::none());

    // Send method related to callback with thread-safe implementation
    seg.def("addToSendQueue",
        [](e2sar::Segmenter& seg, py::buffer py_buf, size_t bytes,
        int64_t _eventNum, uint16_t _dataId, uint16_t entropy,
        py::object callback = py::none(), py::object cbArg = py::none()) -> result<int> {
            
            py::buffer_info buf_info = py_buf.request();
            uint8_t* data = static_cast<uint8_t*>(buf_info.ptr);

            void (*c_callback)(boost::any) = nullptr;
            boost::any c_cbArg = boost::any();
            
            if (!callback.is_none()) {
                // Create heap-allocated wrapper for thread-safe callback
                auto* wrapper = new PythonCallbackWrapper(callback, cbArg);
                c_callback = PythonCallbackWrapper::execute;
                c_cbArg = wrapper;
            }

            // Call the C++ method directly - no wrapper needed
            return seg.addToSendQueue(data, bytes, _eventNum, _dataId, entropy, c_callback, c_cbArg);
        },
        "Call Segmenter::addToSendQueue with buffer interface",
        py::arg("send_buf"),
        py::arg("buf_len"),
        py::arg("_eventNum") = 0LL,
        py::arg("_dataId") = 0,
        py::arg("entropy") = 0,
        py::arg("callback") = py::none(),
        py::arg("cbArg") = py::none());

    // Return type of ReportedStats: bind ReportedStats as a subclass
    py::class_<Segmenter::ReportedStats,
        std::unique_ptr<Segmenter::ReportedStats, py::nodelete>>(seg, "ReportedStats")
            .def_readonly("msgCnt", &Segmenter::ReportedStats::msgCnt)
            .def_readonly("errCnt", &Segmenter::ReportedStats::errCnt)
            .def_readonly("lastErrno", &Segmenter::ReportedStats::lastErrno)
            .def_readonly("lastE2SARError", &Segmenter::ReportedStats::lastE2SARError);

    seg.def("getSendStats", &Segmenter::getSendStats);
    seg.def("getSyncStats", &Segmenter::getSyncStats);

    // Simple return types
    seg.def("getMTU", &Segmenter::getMTU);
    seg.def("getMaxPldLen", &Segmenter::getMaxPldLen);
    seg.def("stopThreads", &Segmenter::stopThreads);
}


void init_e2sarDP_reassembler(py::module_ &m) {
    py::class_<Reassembler> reas(m, "Reassembler");

    // reas.def(py::init<boost::asio::ip::address>());

    // Bind the ReassemblerFlags struct as a nested class of Reassembler
    py::class_<Reassembler::ReassemblerFlags>(reas, "ReassemblerFlags")
        .def(py::init<>())  // The default values will be the same in Python after binding.
        .def_readwrite("useCP", &Reassembler::ReassemblerFlags::useCP)
        .def_readwrite("useHostAddress", &Reassembler::ReassemblerFlags::useHostAddress)
        .def_readwrite("period_ms", &Reassembler::ReassemblerFlags::period_ms)
        .def_readwrite("validateCert", &Reassembler::ReassemblerFlags::validateCert)
        .def_readwrite("Ki", &Reassembler::ReassemblerFlags::Ki)
        .def_readwrite("Kp", &Reassembler::ReassemblerFlags::Kp)
        .def_readwrite("Kd", &Reassembler::ReassemblerFlags::Kd)
        .def_readwrite("setPoint", &Reassembler::ReassemblerFlags::setPoint)
        .def_readwrite("epoch_ms", &Reassembler::ReassemblerFlags::epoch_ms)
        .def_readwrite("portRange", &Reassembler::ReassemblerFlags::portRange)
        .def_readwrite("withLBHeader", &Reassembler::ReassemblerFlags::withLBHeader)
        .def_readwrite("eventTimeout_ms", &Reassembler::ReassemblerFlags::eventTimeout_ms)
        .def_readwrite("rcvSocketBufSize", &Reassembler::ReassemblerFlags::rcvSocketBufSize)
        .def_readwrite("weight", &Reassembler::ReassemblerFlags::weight)
        .def_readwrite("min_factor", &Reassembler::ReassemblerFlags::min_factor)
        .def_readwrite("max_factor", &Reassembler::ReassemblerFlags::max_factor)
        .def("getFromINI", &Reassembler::ReassemblerFlags::getFromINI);

    // Constructor-simple
    reas.def(
        py::init<const EjfatURI &, ip::address, u_int16_t, size_t, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with number of recv threads.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_ip"),
        py::arg("starting_port"),
        py::arg("num_recv_threads") = static_cast<size_t>(1),
        py::arg("rflags") = Reassembler::ReassemblerFlags());

    // Constructor-simple without data_ip and with v6
    reas.def(
        py::init<const EjfatURI &, u_int16_t, size_t,
                const Reassembler::ReassemblerFlags &, bool>(),
        "Init the Reassembler object with number of recv threads, and auto-detect the outgoing IP address.",
        py::arg("uri"),  // must-have args when init
        py::arg("starting_port"),
        py::arg("num_recv_threads") = static_cast<size_t>(1),
        py::arg("rflags") = Reassembler::ReassemblerFlags(),
        py::arg("v6") = false);

    // Constructor with CPU core list.
    reas.def(
        py::init<const EjfatURI &, ip::address, u_int16_t, std::vector<int>, const Reassembler::ReassemblerFlags &>(),
        "Init the Reassembler object with a list of CPU cores.",
        py::arg("uri"),  // must-have args when init
        py::arg("data_ip"),
        py::arg("starting_port"),
        py::arg("cpu_core_list"),
        py::arg("rflags") = Reassembler::ReassemblerFlags());

    // Constructor with CPU core list and auto IP detection
    reas.def(
        py::init<const EjfatURI &, u_int16_t, std::vector<int>,
            const Reassembler::ReassemblerFlags &, bool>(),
        "Init the Reassembler object with a list of CPU cores.",
        py::arg("uri"),  // must-have args when init
        py::arg("starting_port"),
        py::arg("cpu_core_list"),
        py::arg("rflags") = Reassembler::ReassemblerFlags(),
        py::arg("v6") = false);

    // Recv events part. Return py::tuple.
    reas.def("getEventBytes",
        [](Reassembler& self) -> py::tuple {
            u_int8_t *eventBuf = nullptr;
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);

            if (recvres.has_error()) {
                //std::cout << "Error encountered receiving event frames: "
                //    << recvres.error().message() << std::endl;
                // Return an empty buffer
                return py::make_tuple(static_cast<int>(-2), py::bytes(), eventNum, recDataId);
            }

            if (recvres.value() == -1 || eventBuf == nullptr || eventLen == 0) {
                //std::cout << "No message received, continuing" << std::endl;
                return py::make_tuple(static_cast<int>(-1), py::bytes(), eventNum, recDataId);
            }

            // Received event
            // std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen
            //     << " with event number " << eventNum << " and data id " << recDataId << std::endl;
            py::bytes recv_bytes(reinterpret_cast<const char*>(eventBuf), eventLen);
            delete[] eventBuf;  // Clean up the buffer
            return py::make_tuple(eventLen, recv_bytes, eventNum, recDataId);
    },
    "Get an event from the Reassembler EventQueue. Use py.bytes to accept the data.");

    // Receive event as 1D numpy array with the provided numpy data type.
    reas.def("get1DNumpyArray",
        [](Reassembler& self, py::dtype data_type) -> py::tuple {
            u_int8_t *eventBuf{nullptr};
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);

            if (recvres.has_error()) {
                //std::cout << "Error encountered receiving event frames: "
                //    << recvres.error().message() << std::endl;
                return py::make_tuple(static_cast<int>(-2), py::array(), eventNum, recDataId);
            }

            if (recvres.value() == -1) {
                //std::cout << "No message received, continuing" << std::endl;
                return py::make_tuple(static_cast<int>(-1), py::array(), eventNum, recDataId);
            }

            // Create a numpy array from the event buffer with the specified dtype
            py::ssize_t num_elements = static_cast<py::ssize_t>(eventLen) / data_type.itemsize();
            // Use capsule to manage the lifetime of eventBuf
            py::capsule cleanup(eventBuf, [](void *p) { delete[] static_cast<uint8_t*>(p); });
            py::array numpy_array(data_type, {num_elements}, eventBuf, cleanup);

            return py::make_tuple(eventLen, numpy_array, eventNum, recDataId);
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
                    //std::cout << "Error encountered receiving event frames: "
                    //        << recvres.error().message() << std::endl;
                    return py::make_tuple(static_cast<int>(-2), py::array(), eventNum, recDataId);
                }

                if (recvres.value() == -1) {
                    //std::cout << "No message received, continuing" << std::endl;
                    return py::make_tuple(static_cast<int>(-1), py::array(), eventNum, recDataId);
                }

                // Calculate the number of elements based on the item size of the specified data type
                py::ssize_t num_elements = static_cast<py::ssize_t>(eventLen) / data_type.itemsize();

                // Create a capsule to manage eventBuf lifetime
                py::capsule cleanup(eventBuf, [](void *p) { delete[] static_cast<uint8_t*>(p); });
                // Create a numpy array from the event buffer with the specified dtype, using the capsule for cleanup
                py::array numpy_array(data_type, {num_elements}, eventBuf, cleanup);

                return py::make_tuple(eventLen, numpy_array, eventNum, recDataId);
            },
            "Receive an event as a 1D numpy array in blocking mode.",
            py::arg("data_type"),
            py::arg("wait_ms") = 0);


    reas.def("recvEventBytes",
        [](Reassembler& self, u_int64_t wait_ms) -> py::tuple {
            u_int8_t *eventBuf{nullptr};
            size_t eventLen = 0;
            EventNum_t eventNum = 0;
            u_int16_t recDataId = 0;

            auto recvres = self.recvEvent(&eventBuf, &eventLen, &eventNum, &recDataId, wait_ms);

            // Return empty bytes object of return code is not 0
            if (recvres.has_error()) {
                //std::cout << "Error encountered receiving event frames: "
                //    << recvres.error().message() << std::endl;
                return py::make_tuple(static_cast<int>(-2), py::bytes(), eventNum, recDataId);
            }

            if (recvres.value() == -1) {
                //std::cout << "No message received, continuing" << std::endl;
                return py::make_tuple(static_cast<int>(-1), py::bytes(), eventNum, recDataId);
            }

            // Received event
            // std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen
            //     << " with event number " << eventNum << " and data id " << recDataId << std::endl;
            py::bytes recv_bytes(reinterpret_cast<const char*>(eventBuf), eventLen);
            delete [] eventBuf;  // Clean up the buffer
            return py::make_tuple(eventLen, recv_bytes, eventNum, recDataId);
    },
    "Get an event in the blocking mode. Use py.bytes to accept the data.",
    py::arg("wait_ms") = 0);

    // Return type of result<int>
    reas.def("OpenAndStart", &Reassembler::openAndStart);
    reas.def("registerWorker", &Reassembler::registerWorker);
    reas.def("deregisterWorker", &Reassembler::deregisterWorker);

    // Return type of result<std::list<std::pair<u_int16_t, size_t>>>
    reas.def("get_FDStats", &Reassembler::get_FDStats);

    // Return type of result<boost::tuple<EventNum_t, u_int16_t, size_t>>
    // TODO: check the underlying C++ result<T> convention and pybind
    reas.def("get_LostEvent", [](Reassembler& reasObj) -> py::tuple {
        
        auto res = reasObj.get_LostEvent();
        if (res.has_error()) {
            return py::make_tuple();
        } else {
            auto ret = res.value();
            return py::make_tuple(boost::get<0>(ret), boost::get<1>(ret), boost::get<2>(ret));
        }
    });

    // Return type of ReportedStats: bind ReportedStats as a subclass of Reassembler
    py::class_<Reassembler::ReportedStats,
                std::unique_ptr<Reassembler::ReportedStats, py::nodelete>>(reas, "ReportedStats")
        .def_readonly("enqueueLoss", &Reassembler::ReportedStats::enqueueLoss)
        .def_readonly("reassemblyLoss", &Reassembler::ReportedStats::reassemblyLoss)
        .def_readonly("eventSuccess", &Reassembler::ReportedStats::eventSuccess)
        .def_readonly("lastErrno", &Reassembler::ReportedStats::lastErrno)
        .def_readonly("grpcErrCnt", &Reassembler::ReportedStats::grpcErrCnt)
        .def_readonly("dataErrCnt", &Reassembler::ReportedStats::dataErrCnt)
        .def_readonly("lastE2SARError", &Reassembler::ReportedStats::lastE2SARError);
    reas.def("getStats", &Reassembler::getStats);

    // Return type: ip::address - convert to string for Python
    reas.def("get_dataIP", [](const Reassembler &reasObj) {
        return reasObj.get_dataIP().to_string();
    });

    // Simple return types
    reas.def("get_numRecvThreads", &Reassembler::get_numRecvThreads);
    reas.def("get_recvPorts", &Reassembler::get_recvPorts);
    reas.def("get_portRange", &Reassembler::get_portRange);
    reas.def("stopThreads", &Reassembler::stopThreads);
}
