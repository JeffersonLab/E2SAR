## v0.2.0

Compared to 0.1.X this version introduces a number of enhancements, although it remains backward-compatible with 0.1.X releases. The list of enhancements/changes includes:

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

Note that IP auto-detection features work in simple scenarios where the outgoing interface has at most one address of each type (IPv4 or IPv6) associated with it. The code uses the first address it finds if multiple addresses are found. In the case of e.g. aliased interfaces the results of auto-detection may not be as intended and in this case sender and receiver IP addresses will need to be specified explicitly.

## v0.1.5

Last release of the 0.1.X series mainly cleaning up and bug fixes.



