# E2SAR Release Notes

API Details can always be found in the [wiki](https://github.com/JeffersonLab/E2SAR/wiki) and in the [Doxygen site](https://jeffersonlab.github.io/E2SAR-doc/annotated.html). 

## v0.2.0

Compared to 0.1.X this version introduces a number of enhancements, although it remains (largely) backward-compatible with 0.1.X releases. The list of enhancements/changes includes:

- Segmenter now has an additional constructor that takes a list of cores to run working threads on. It remains single-threaded so only one core needs to be specified on the list and the sending thread will be bound to this core. 
- A new class called Affinity is introduced to be the umbrella for a number of static methods that manipulate thread, process and NUMA affinity. See e2sar_perf for examples of how e.g. to turn on NUMA affinity. Thread affinity in Segmenter and Reassembler is automatic if a list of cores is provided to the constructors. Using NUMA affinity requires the installation of `libnuma-dev` package.
- Two kernel-level optimizations have been introduced into the Segmenter. In addition to the normal way of sending UDP packets using sendmsg() call it now can
    - Use sendmmsg() call to reduce the number of system calls to 1 per event, rather than N per event with traditional sendmsg() (generally compiles on any recent Linux kernel 5+).
    - Use io_uring with kernel thread polling to reduce the number of system calls to 0 per event (recommended to be used on kernel 5.15+ and requires the installation of `liburing-dev` package). 
- A new class called Optimizations is introduced which keeps track of which optimizations are compiled in and which are turned on. See e2sar_perf code for examples of how to use it.
- A number of automatic detection features have been added along with corresponding calls on various objects. They only work under Linux; See e2sar_perf for examples of how to use it:
    - Automatic sender MTU detection - if sFlags.mtu is set to 0 the code will attempt to automatically determine the MTU of the outgoing interface.
    - Automatic sender IP detection - the LBManager object has two new methods: addSenderSelf() and removeSenderSelf() which attempts to determine the IP of the outgoing interface (towards the Load Balancer) and use that IP address to register the sender with the Load Balancer. `lbadm` supports this by allowing user to omit the `-a` option on `--addsenders` and `--removesenders` commands. You can also see e2sar_perf for an example of how it can be done in conjunction with the Segmenter.
    - Automatic receiver IP detection - the LBManager object has a new method registerWorkerSelf() which does not need the receiving IP address and instead attempts to auto-detect it. The Reassembler object has two new constructors that do not require specifying the `<IP address:UDP port>` tuple and instead only need the UDP port. Reassembler object now also has get_dataIP() method to query the outgoing IP address that was auto-detected. You can see e2sar_perf on how it can be used in conjunction with the Reassembler.
- Reassembler stats tuple has been replaced with a struct. Now Reassembler::getStats() return a struct Reassembler::ReportedStats as follows:
    - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
    - EventNum_t reassemblyLoss; // number of events lost in reassembly due to missing segments
    - EventNum_t eventSuccess; // events successfully processed
    - int lastErrno; // last detected errno (use strerror to get string)
    - int grpcErrCnt; // number of grpcErrors 
    - int dataErrCnt; // number of dataplane socket errors
    - E2SARErrorc lastE2SARError;  // last encountered E2SAR internal error
    - WARNING! This is the point of incompatibility with 0.1.X 
- Reassembler has additional per-port stats of received fragments which can be obtained by calling Reassembler::get_FDStats() (only after threads have been stopped, otherwise error is returned). Returned is a list of pairs `<port number, number of frames/fragments received>`.
- Segmenter sync and send stat return tuple (Segmenter::getSyncStats() and Segmenter::getSendStats()) similarly has been replaced with a Segmenter::ReportedStats struct
    - WARNING! This is the point of incompatibility with 0.1.X
- Segmenter now has a 'warm-up' period of 1 second by default when it sends SYNC packets without allowing data to be sent. This is to help synchronize schedules with the control plane. The length of this period in milliseconds can be set using SegmenterFlags.warmUpMs (also reflected in the INI file format)
- Python bindings have been updated to match these changes; python test framework moved to pytest instead of notebooks

Note that IP auto-detection features work in simple scenarios where the outgoing interface has at most one address of each type (IPv4 or IPv6) associated with it. The code uses the first address it finds if multiple addresses are found. In the case of e.g. aliased interfaces the results of auto-detection may not be as intended and in this case sender and receiver IP addresses will need to be specified explicitly.

## v0.1.8

A fix for sendEvent handling of buffers. Addition of numpy support for sending and receiving numpy arrays. See tests/py_test for examples.

## v0.1.7
Added bash-helpers/ scripts that are installed in bin/ - they help test basic functionality by hiding much of the invocation complexity of tools like lbadm and e2sar_perf. The associated README.md explains the usage. 

## v0.1.5

Last release of the 0.1.X series mainly cleaning up and bug fixes.



