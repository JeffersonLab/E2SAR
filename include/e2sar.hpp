#ifndef E2SARHPP
#define E2SARHPP

#include "e2sarUtil.hpp"
#include "e2sarCP.hpp"
#include "e2sarDPSegmenter.hpp"
#include "e2sarDPReassembler.hpp"

namespace e2sar
{
    static const std::string E2SARVersion;

    const std::string& get_Version();
    const std::string get_Optimizations() noexcept;
    result<int> select_Optimizations(std::vector<std::string>& opt);
    const std::string get_SelectedOptimizations() noexcept;
    bool is_SelectedOptimization(std::string s) noexcept;
}
#endif
