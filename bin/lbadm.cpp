#include <iostream>
#include <vector>
#include <boost/program_options.hpp>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

result<EjfatURI> reserveLB(const std::string &lbname, const std::vector<std::string> &senders)
{

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Unable to parse URI from environment, exiting"s};
    }
    auto uri = uri_r.value();

    std::cout << "Will attempt a connection to control plane on " << uri.get_cpAddr().value().first << (uri.get_useTls() ? " using TLS"s : " without using TLS"s) << std::endl;

    std::cout << "Sender list is " << std::endl;
    for (auto s: senders) {
        std::cout << " " << s << std::endl;
    }

    // create LBManager
    auto lbman = LBManager(uri);

    // attempt to reserve

    auto res = lbman.reserveLB(lbname, pt::duration_from_string("01"), senders);

    if (res.has_error()) {
        return E2SARErrorInfo{E2SARErrorc::RPCError, "Unable to connect to Load Balancer, error "s};
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
    opts("senders,s", po::value<std::vector<std::string>>()->multitoken(), "list of sender IPv4/6 addresses");

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

        if (vm.count("senders") < 1) {
            std::cerr << "You must specify a list of sender IP addresses" << std::endl;
            return -1;
        }
        auto uri_r = reserveLB(vm["lbname"].as<std::string>(), vm["senders"].as<std::vector<std::string>>());
        if (uri_r.has_error()) {
            std::cerr << "There was an error communicating to LB: " << uri_r.error().message() << std::endl;
            return -1;
        }

        std::cout << "Updated URI after reserve " << static_cast<std::string>(uri_r.value()) << std::endl;
    }
}