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

result<int> freeLB(EjfatURI &uri, const std::string &lbid = "")
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "Freeing a load balancer " << std::endl;
    std::cout << "   Contacting: " << uri.to_string(EjfatURI::TokenType::admin) << std::endl;
    std::cout << "   LB Name: " << (uri.get_lbName().empty() ? "not set"s : uri.get_lbId()) << std::endl;
    std::cout << "   LB ID: " << (lbid.empty() ? uri.get_lbId() : lbid) << std::endl;

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

result<int> registerWorker(EjfatURI &uri, const std::string &node_name, const std::string &node_ip, u_int16_t node_port, float weight, u_int16_t src_cnt)
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "Registering a worker " << std::endl;
    std::cout << "   Contacting: " << uri.to_string(EjfatURI::TokenType::instance) << std::endl;
    std::cout << "   LB Name: " << (uri.get_lbName().empty() ? "not set"s : uri.get_lbId()) << std::endl;
    std::cout << "   Worker details: " << node_name << " at "s << node_ip << ":"s << node_port << std::endl;
    std::cout << "   CP parameters: "
              << "w="s << weight << ",  source_count="s << src_cnt << std::endl;

    auto res = lbman.registerWorker(node_name, std::pair<ip::address, u_int16_t>(ip::make_address(node_ip), node_port), weight, src_cnt);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Sucess." << std::endl;
        std::cout << "Updated URI after register " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << std::endl;
        std::cout << "Session id is " << lbman.get_URI().get_sessionId() << std::endl;
        return 0;
    }
}

result<int> deregisterWorker(EjfatURI &uri)
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "De-Registering a worker " << std::endl;
    std::cout << "   Contacting: " << uri.to_string(EjfatURI::TokenType::session) << std::endl;
    std::cout << "   LB Name: " << (uri.get_lbName().empty() ? "not set"s : uri.get_lbId()) << std::endl;

    auto res = lbman.deregisterWorker();

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

result<int> getLBStatus(EjfatURI &uri, const std::string &lbid)
{
    // create LBManager
    auto lbman = LBManager(uri);

    std::cout << "Getting LB Status " << std::endl;
    std::cout << "   Contacting: " << uri.to_string(EjfatURI::TokenType::session) << std::endl;
    std::cout << "   LB Name: " << (uri.get_lbName().empty() ? "not set"s : uri.get_lbId()) << std::endl;
    std::cout << "   LB ID: " << (lbid.empty() ? uri.get_lbId() : lbid) << std::endl;

    auto res = lbman.getLBStatus(lbid);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Sucess." << std::endl;

        auto addresses = LBManager::get_SenderAddressVector(res.value());

        auto workers = LBManager::get_WorkerStatusVector(res.value());

        std::cout << "Registered sender addresses: ";
        for (auto a : addresses)
            std::cout << a << " "s;
        std::cout << std::endl;

        std::cout << "Registered workers: ";
        for (auto w : workers)
        {
            std::cout << "[ name="s << w.name() << ", controlsignal="s << w.controlsignal() << ", fillpercent="s << w.fillpercent() << ", slotsassigned="s << w.slotsassigned() << ", lastupdated=" << *w.mutable_lastupdated() << "] "s << std::endl;
        }
        std::cout << std::endl;

        std::cout << "LB details: expiresat=" << res.value()->expiresat() << ", currentepoch=" << res.value()->currentepoch() << ", predictedeventnum=" << res.value()->currentpredictedeventnumber() << std::endl;

        return 0;
    }
}

result<int> version(EjfatURI &uri)
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
        std::cout << "Reported version: " << res.value() << std::endl;
        return 0;
    }
}

int main(int argc, char **argv)
{

    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");

    // parameters
    opts("lbname,l", po::value<std::string>(), "specify name of the load balancer");
    opts("lbid,i", po::value<std::string>(), "specify id of the loadbalancer as issued by reserve call instead of using what is in EJFAT_URI");
    opts("address,a", po::value<std::vector<std::string>>()->multitoken(), "node IPv4/IPv6 address");
    opts("duration,d", po::value<std::string>(), "specify duration as 'hh:mm:ss'");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("name,n", po::value<std::string>(), "specify node name for registration");
    opts("port,p", po::value<u_int16_t>(), "node starting listening port number");
    opts("weight,w", po::value<float>(), "node weight");
    opts("count,c", po::value<u_int16_t>(), "node source count");
    opts("session,s", po::value<std::string>(), "session id");
    // commands
    opts("reserve", "reserve a load balancer (-l, -a, -d required)");
    opts("free", "free a load balancer");
    opts("version", "report the version of the LB");
    opts("register", "register a worker (-n, -a, -p, -w, -c required)");
    opts("deregister", "deregister worker (-s required)");
    opts("status", "get and print LB status");

    po::variables_map vm;

    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    // specify all options dependencies here
    try
    {
        option_dependency(vm, "reserve", "lbname");
        option_dependency(vm, "reserve", "duration");
        option_dependency(vm, "reserve", "address");
        option_dependency(vm, "register", "name");
        option_dependency(vm, "register", "address");
        option_dependency(vm, "register", "port");
        option_dependency(vm, "register", "weight");
        option_dependency(vm, "register", "count");
        option_dependency(vm, "deregister", "session");
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

    EjfatURI::TokenType tt{EjfatURI::TokenType::admin};
    if (vm.count("reserve") || vm.count("free") || vm.count("status") || vm.count("version")) 
    {
        tt = EjfatURI::TokenType::admin;
    } else if (vm.count("register")) 
    {
        tt = EjfatURI::TokenType::instance;
    } else if (vm.count("deregister"))
    {
        tt = EjfatURI::TokenType::session;
    }

    std::string ejfat_uri;
    auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>(), tt) : EjfatURI::getFromEnv("EJFAT_URI"s, tt));
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
                               vm["address"].as<std::vector<std::string>>(),
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
    }
    else if (vm.count("register"))
    {
        auto int_r = registerWorker(uri,
                                    vm["name"].as<std::string>(),
                                    vm["address"].as<std::vector<std::string>>()[0],
                                    vm["port"].as<u_int16_t>(),
                                    vm["weight"].as<float>(),
                                    vm["count"].as<u_int16_t>());

        if (int_r.has_error())
        {
            std::cerr << "There was an error registering worker: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("deregister"))
    {
        // remember to set session 
        uri.set_sessionId(vm["session"].as<std::string>());
        auto int_r = deregisterWorker(uri);
        if (int_r.has_error())
        {
            std::cerr << "There was an error deregistering worker: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("status"))
    {
        std::string lbid;
        if (vm.count("lbid"))
            lbid = vm["lbid"].as<std::string>();

        auto int_r = getLBStatus(uri, lbid);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting LB status: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
}