#include <iostream>
#include <boost/program_options.hpp>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

outcome::result<EjfatURI> reserveLB(const std::string &lbname)
{

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
    {
        std::cerr << "Unable to parse URI from environment, exiting" << std::endl;
        return E2SARErrorc::ParseError;
    }
    auto uri = uri_r.value();

    std::cout << "Will attempt a connection to control plane on " << uri.get_cpAddr().value().first << (uri.get_useTls() ? " using TLS"s : " without using TLS"s) << std::endl;

    // create LBManager
    auto lbman = LBManager(uri);

    // attempt to reserve

    auto res = lbman.reserveLB(lbname, pt::duration_from_string("01"));

    if (res.has_error()) {
        std::cerr << "Unable to connect to Load Balancer, error " << res.error() << std::endl;
        return E2SARErrorc::RPCError;
    } else {
        return uri;
    }
}

int main(int argc, char **argv)
{

    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");

    opts("lbname,l", po::value<std::string>(), "specify name of the load balancer");
    opts("command,c", po::value<std::string>(), "specify command");

    po::variables_map vm;

    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        return 0;
    }

    if (vm.count("lbname"))
    {
        std::cout << "LB Name: " << vm["lbname"].as<std::string>() << std::endl;
    }

    if (vm.count("command"))
    {
        std::cout << "Command: " << vm["command"].as<std::string>() << std::endl;

        auto uri_r = reserveLB(vm["lbname"].as<std::string>());
        if (uri_r.has_error()) {
            return -1;
        }

        std::cout << "Updated URI adter reserve " << static_cast<std::string>(uri_r.value()) << std::endl;
    }
}