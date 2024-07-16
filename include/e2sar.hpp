#ifndef E2SARHPP
#define E2SARHPP

#include "e2sarUtil.hpp"
#include "e2sarCP.hpp"
#include "e2sarDP.hpp"

#ifndef E2SAR_VERSION
#define E2SAR_VERSION "Unknown"
#endif

namespace e2sar
{
    std::string get_Version() 
    {
        return std::string{E2SAR_VERSION};
    }
}
#endif
