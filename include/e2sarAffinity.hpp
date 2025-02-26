#ifndef E2SARAFF
#define E2SARAFF

#include "e2sarError.hpp"

namespace e2sar {

    /**
     * Static methods to manipuate affinity of memory and CPUs
     */
    class Affinity {
        public: 
            /**
             * Set the affinity of the entire process to the cores in the vector
             */
            static result<int> setProcess(const std::vector<int> &cores) noexcept;

            /**
             * Set calling thread affinity to specified core
             */
            static result<int> setThread(int core) noexcept;

            /**
             * Set calling thread affinity to exclude named cores
             */
            static result<int> setThreadXOR(const std::vector<int> &cores) noexcept;

            /**
             * Bind process memory allocation to specified NUMA node
             * Error out if node is invalid or NUMA not supported
             */
            static result<int> setNUMABind(int node) noexcept;

        private:
            Affinity() = delete;
            ~Affinity() = delete;
    };
};

#endif