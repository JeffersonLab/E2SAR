#ifdef AFFINITY_AVAILABLE
#include <sched.h>
#include <unistd.h>
#endif

#ifdef THRD_AFFINITY_AVAILABLE
#include <pthread.h>
#endif

#ifdef NUMA_AVAILABLE
#include <numa.h>
#endif

#include "e2sarAffinity.hpp"

namespace e2sar {
    result<int> setProcessAffinity(const std::vector<int> &cores) noexcept
    {
#ifdef AFFINITY_AVAILABLE
        // set this process affinity to the indicated set of cores
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        for(auto core: cores)
        {
            if (core < 0)
                return E2SARErrorInfo{E2SARErrorc::OutOfRange, "Invalid core number"};
            CPU_SET(core, &cpuset);
        }
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(errno)};
        return 0;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting process affinity not available on this system"};
#endif
    }

    result<int> setThreadAffinity(int core) noexcept
    {
#ifdef THRD_AFFINITY_AVAILABLE
        cpu_set_t cpuset;
        if (core < 0)
            return E2SARErrorInfo{E2SARErrorc::OutOfRange, "Invalid core number"};
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        int err{0};
        if ((err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};
        return err;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting thread affinity not available on this system"};
#endif
    }

    result<int> setThreadAffinityXOR(const std::vector<int> &cores) noexcept
    {
#ifdef THRD_AFFINITY_AVAILABLE
        cpu_set_t cpuset, curset, xorset;

        // set the mast that we will XOR with
        CPU_ZERO(&cpuset);
        for(int core: cores) 
        {
            if (core < 0)
                return E2SARErrorInfo{E2SARErrorc::OutOfRange, "Invalid core number"};
            CPU_SET(core, &cpuset);
        }
        
        // get the current mask of the thread
        auto thread = pthread_self();
        CPU_ZERO(&curset);
        int err{0};
        if ((err = pthread_getaffinity_np(thread, sizeof(curset), &curset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};

        // do the XOR
        CPU_ZERO(&xorset);
        CPU_XOR(&xorset, &cpuset, &curset);
        // set thread to use the xorset which now excludes cores we don't want
        if ((err = pthread_setaffinity_np(thread, sizeof(xorset), &xorset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};
        return err;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting thread affinity not available on this system"};
#endif
    }

    result<int> setNUMABind(int node) noexcept
    {
#ifdef NUMA_AVAILABLE
        struct bitmask *numa_mask;

        int nr_nodes = numa_max_node() + 1;

        if (numa_available() < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, "NUMA management not supported on this system"};
        
        if ((node > nr_nodes - 1) or (node < 0))
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Requested NUMA node not valid"};

        numa_mask = numa_bitmask_alloc(numa_max_node() + 1);
        numa_bitmask_setbit(numa_mask, node);

        // makes sure all memory allocations come from the specified NUMA node
        numa_set_membind(numa_mask);

        numa_bitmask_free(numa_mask);
        return 0;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "NUMA management not available on this system"};
#endif
    }
}