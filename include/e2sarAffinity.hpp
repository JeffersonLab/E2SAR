#ifndef E2SARAFF
#define E2SARAFF

#include "e2sarError.hpp"

namespace e2sar {


    /**
     * Set the affinity of the entire process to the cores in the vector
     */
    result<int> setProcessAffinity(const std::vector<int> &cores) noexcept;

    /**
     * Set calling thread affinity to specified core
     */
    result<int> setThreadAffinity(int core) noexcept;

    /**
     * Set calling thread affinity to exclude named cores
     */
    result<int> setThreadAffinityXOR(const std::vector<int> &cores) noexcept;

    /**
     * Bind process memory allocation to specified NUMA node
     * Error out if node is invalid or NUMA not supported
     */
    result<int> setNUMABind(int node) noexcept;

};

#endif