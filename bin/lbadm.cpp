#include <iostream>
#include <vector>
#include <boost/program_options.hpp>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

/* Function used to check that 'opt1' and 'opt2' are not specified
   at the same time. */
void conflicting_options(const po::variables_map &vm,
                         const std::string &opt1, const std::string &opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted())
        throw std::logic_error(std::string("Conflicting options '") + opt1 + "' and '" + opt2 + "'.");
}

/* Function used to check that of 'for_what' is specified, then
   'required_option' is specified too. */
void option_dependency(const po::variables_map &vm,
                       const std::string &for_what, const std::string &required_option)
{
    if (vm.count(for_what) && !vm[for_what].defaulted())
        if (vm.count(required_option) == 0 || vm[required_option].defaulted())
            throw std::logic_error(std::string("Option '") + for_what + "' requires option '" + required_option + "'.");
}

/**
 * @param lbname is the name of the loadbalancer you give it
 * @param senders is the list of IP addresses of sender nodes
 * @param duration is a string indicating the duration you wish to reserve LB for format "hh:mm::ss"
 */
result<int> reserveLB(EjfatURI &uri,
                      const std::string &lbname,
                      const std::vector<std::string> &senders,
                      const std::string &duration)
{
    // create LBManager
    auto lbman = LBManager(uri);

    boost::posix_time::time_duration duration_v;
    try
    {
        duration_v = pt::duration_from_string(duration);
    }
    catch (...)
    {
        return E2SARErrorInfo{E2SARErrorc::ParameterError,
                              "unable to convert duration string "s + duration};
    }

    std::cout << "Reserving a new load balancer " << std::endl;
    std::cout << "   Contacting: " << static_cast<std::string>(uri) << std::endl;
    std::cout << "   LB Name: " << lbname << std::endl;
    std::cout << "   Allowed senders: ";
    for (auto s : senders)
        std::cout << s << " ";
    std::cout << std::endl;
    std::cout << "   Duration: " << duration_v << std::endl;
    // attempt to reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Sucess." << std::endl;

        std::cout << "Updated URI after reserve " << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;
        return 0;
    }
}

result<int> freeLB(EjfatURI &uri, const std::string &lbid)
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "Freeing a load balancer " << std::endl;
    std::cout << "   Contacting: " << static_cast<std::string>(uri) << std::endl;
    std::cout << "   LB Name: " << uri.get_lbName() << std::endl;
    std::cout << "   LB ID: " << uri.get_lbId() << std::endl;

    result<int> res{0};

    // attempt to free
    if (lbid.empty())
        res = lbman.freeLB();
    else
        res = lbman.freeLB(lbid);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Sucess." << std::endl;
        return 0;
    }
}

result<std::string> version(EjfatURI &uri)
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "Getting load balancer version " << std::endl;
    std::cout << "   Contacting: " << static_cast<std::string>(uri) << std::endl;

    result<std::string> res{""};

    res = lbman.version();

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Sucess." << std::endl;
        return res.value();
    }
}

int main(int argc, char **argv)
{

    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");

    // parameters
    opts("lbname,l", po::value<std::string>(), "specify name of the load balancer");
    opts("lbid,i", po::value<std::string>(), "specify id of the loadbalancer as issued by reserve call instead of using what is in EJFAT_URI");
    opts("senders,s", po::value<std::vector<std::string>>()->multitoken(), "list of sender IPv4/6 addresses");
    opts("duration,d", po::value<std::string>(), "specify duration as 'hh:mm:ss'");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    // commands
    opts("reserve", "reserve a load balancer (-l, -s, -d required)");
    opts("free", "free a load balancer");
    opts("version", "report the version of the LB");

    po::variables_map vm;

    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    // specify all options dependencies here
    try
    {
        option_dependency(vm, "reserve", "lbname");
        option_dependency(vm, "reserve", "duration");
        option_dependency(vm, "reserve", "senders");
    }
    catch (const std::logic_error &le)
    {
        std::cerr << "Error processing command-line options: " << le.what() << std::endl;
        return -1;
    }

    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        return 0;
    }

    std::string ejfat_uri;
    auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>()) : EjfatURI::getFromEnv());
    if (uri_r.has_error())
    {
        std::cerr << "Error in parsing URI from command-line, error "s + uri_r.error().message();
    }
    auto uri = uri_r.value();

    // Reserve
    if (vm.count("reserve"))
    {
        // execute command
        auto uri_r = reserveLB(uri, vm["lbname"].as<std::string>(),
                               vm["senders"].as<std::vector<std::string>>(),
                               vm["duration"].as<std::string>());
        if (uri_r.has_error())
        {
            std::cerr << "There was an error reserving LB: " << uri_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("free"))
    {
        std::string lbid;
        if (vm.count("lbid"))
            lbid = vm["lbid"].as<std::string>();
        auto int_r = freeLB(uri, lbid);
        if (int_r.has_error())
        {
            std::cerr << "There was an error freeing LB: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("version"))
    {
        auto int_r = version(uri);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting LB version: " << int_r.error().message() << std::endl;
            return -1;
        }
        std::cout << "Reported version: " << int_r.value() << std::endl;
    }
}