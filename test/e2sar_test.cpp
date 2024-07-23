#include <iostream>

#include "e2sar.hpp"

using namespace e2sar;

int main()
{

    std::cout << "E2SAR VERSION: " << get_Version() << std::endl;
        
    std::string uri_string{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};

    try
    {
        EjfatURI euri(uri_string);
        std::cout << static_cast<std::string>(euri) << std::endl;
    }
    catch (const E2SARException &e)
    {
        std::cout << "Exception" << std::endl;
        std::cout << "Exception occurred: "s << std::string(e) << std::endl;
    }
}
