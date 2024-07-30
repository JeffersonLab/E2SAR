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
result<int> reserveLB(LBManager &lbman,
                      const std::string &lbname,
                      const std::vector<std::string> &senders,
                      const std::string &duration)
{
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
    std::cout << "   Contacting: " << static_cast<std::string>(lbman.get_URI()) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
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
        std::cout << "Success. FPGA ID is (for metrics): " << res.value() << std::endl;

        std::cout << "Updated URI after reserve with instance token: " << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;
        return 0;
    }
}

result<int> freeLB(LBManager &lbman, const std::string &lbid = "")
{

    std::cout << "Freeing a load balancer " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   LB ID: " << (lbid.empty() ? lbman.get_URI().get_lbId() : lbid) << std::endl;

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
        std::cout << "Success." << std::endl;
        return 0;
    }
}

result<int> registerWorker(LBManager &lbman, const std::string &node_name, const std::string &node_ip, u_int16_t node_port, float weight, u_int16_t src_cnt, float min_factor, float max_factor)
{
    std::cout << "Registering a worker " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   Worker details: " << node_name << " at "s << node_ip << ":"s << node_port << std::endl;
    std::cout << "   CP parameters: "
              << "w="s << weight << ",  source_count="s << src_cnt << std::endl;

    auto res = lbman.registerWorker(node_name, std::pair<ip::address, u_int16_t>(ip::make_address(node_ip), node_port), weight, src_cnt, min_factor, max_factor);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Success." << std::endl;
        std::cout << "Updated URI after register with session token: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << std::endl;
        std::cout << "Session id is: " << lbman.get_URI().get_sessionId() << std::endl;
        return 0;
    }
}

result<int> deregisterWorker(LBManager &lbman)
{
    std::cout << "De-Registering a worker " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;

    auto res = lbman.deregisterWorker();

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Success." << std::endl;
        return 0;
    }
}

result<int> getLBStatus(LBManager &lbman, const std::string &lbid)
{
    std::cout << "Getting LB Status " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   LB ID: " << (lbid.empty() ? lbman.get_URI().get_lbId() : lbid) << std::endl;

    auto res = lbman.getLBStatus(lbid);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        auto lbstatus = LBManager::asLBStatus(res.value());

        std::cout << "Registered sender addresses: ";
        for (auto a : lbstatus->senderAddresses)
            std::cout << a << " "s;
        std::cout << std::endl;

        std::cout << "Registered workers: ";
        for (auto w : lbstatus->workers)
        {
            std::cout << "[ name="s << w.name() << ", controlsignal="s << w.controlsignal() << ", fillpercent="s << w.fillpercent() << ", slotsassigned="s << w.slotsassigned() << ", lastupdated=" << *w.mutable_lastupdated() << "] "s << std::endl;
        }
        std::cout << std::endl;

        std::cout << "LB details: expiresat=" << lbstatus->expiresAt << ", currentepoch=" << lbstatus->currentEpoch << ", predictedeventnum=" << lbstatus->currentPredictedEventNumber << std::endl;

        return 0;
    }
}

result<int> overview(LBManager &lbman)
{
    std::cout << "Getting Overview " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;

    auto res = lbman.overview();

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        auto overview = LBManager::asOverviewMessage(res.value());

        for (auto r: overview) 
        {
            std::cout << "LB " << r.name << " ID: " << r.lbid << " FPGA LBID: " << r.fpgaLBId << std::endl;
            std::cout << "  Registered sender addresses: ";
            for (auto a : r.status.senderAddresses)
                std::cout << a << " "s;
            std::cout << std::endl;

            std::cout << "  Registered workers: " << std::endl;
            for (auto w : r.status.workers)
            {
                std::cout << "  [ name="s << w.name() << ", controlsignal="s << w.controlsignal() << 
                    ", fillpercent="s << w.fillpercent() << ", slotsassigned="s << w.slotsassigned() << 
                    ", lastupdated=" << *w.mutable_lastupdated() << "] "s << std::endl;
            }
            std::cout << std::endl;

            std::cout << "  LB details: expiresat=" << r.status.expiresAt << ", currentepoch=" << 
                r.status.currentEpoch << ", predictedeventnum=" << 
                r.status.currentPredictedEventNumber << std::endl;
        }
        return 0;
    }
}

result<int> sendState(LBManager &lbman, float fill_percent, float ctrl_signal, bool is_ready)
{
    std::cout << "Sending Worker State " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;

    auto res = lbman.sendState(fill_percent, ctrl_signal, is_ready);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Success." << std::endl;

        return 0;
    }
}

result<int> version(LBManager &lbman)
{

    std::cout << "Getting load balancer version " << std::endl;
    std::cout << "   Contacting: " << static_cast<std::string>(lbman.get_URI()) << " on IP " << lbman.get_URI().get_cpAddr().value().first << std::endl;

    result<std::string> res{""};

    res = lbman.version();

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Success." << std::endl;
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
    opts("address,a", po::value<std::vector<std::string>>()->multitoken(), "node IPv4/IPv6 address, can be used multiple times for 'reserve' call");
    opts("duration,d", po::value<std::string>(), "specify duration as '[hh[:mm[:ss]]]'");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("name,n", po::value<std::string>(), "specify node name for registration");
    opts("port,p", po::value<u_int16_t>(), "node starting listening port number");
    opts("weight,w", po::value<float>(), "node weight");
    opts("count,c", po::value<u_int16_t>(), "node source count");
    opts("session,s", po::value<std::string>(), "session id from 'register' call");
    opts("queue,q", po::value<float>(), "queue fill");
    opts("ctrl,c", po::value<float>(), "control signal value");
    opts("ready,r", po::value<bool>(), "worker ready state");
    opts("root,o", po::value<std::string>(), "root cert for SSL communications");
    opts("novalidate,v", "don't validate server certificate (conflicts with 'root')");
    opts("minfactor", po::value<float>(), "node min factor, multiplied with the number of slots that would be assigned evenly to determine min number of slots for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots");
    opts("maxfactor", po::value<float>(), "multiplied with the number of slots that would be assigned evenly to determine max number of slots for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum");
    opts("ipv6,6", "prefer IPv6 control plane address if URI specifies hostname");
    // commands
    opts("reserve", "reserve a load balancer (-l, -a, -d required). Uses admin token.");
    opts("free", "free a load balancer. Uses instance or admin token.");
    opts("version", "report the version of the LB. Uses admin token.");
    opts("register", "register a worker (-n, -a, -p, -w, -c required), note you must use 'state' within 10 seconds or worker is deregistered. Uses instance or admin token.");
    opts("deregister", "deregister worker (-s required). Uses instance or session token.");
    opts("status", "get and print LB status. Uses admin token.");
    opts("state", "send worker state update (must be done within 10 sec of registration) (-q, -c, -r required). Uses admin token.");
    opts("overview","return metadata and status information on all registered load balancers. Uses admin token.");

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
        option_dependency(vm, "register", "minfactor");
        option_dependency(vm, "register", "maxfactor");
        option_dependency(vm, "deregister", "session");
        option_dependency(vm, "state", "queue");
        option_dependency(vm, "state", "ctrl");   
        option_dependency(vm, "state", "ready");
        option_dependency(vm, "state", "session");
        conflicting_options(vm, "root", "novalidate");
    }
    catch (const std::logic_error &le)
    {
        std::cerr << "Error processing command-line options: " << le.what() << std::endl;
        return -1;
    }

    std::cout << "E2SAR Version: " << get_Version() << std::endl;
    if (vm.count("help") || vm.empty())
    {
        std::cout << od << std::endl;
        return 0;
    }

    // make sure the token is interpreted as the correct type, depending on the call
    EjfatURI::TokenType tt{EjfatURI::TokenType::admin};
    if (vm.count("reserve") || vm.count("free") || vm.count("status") || vm.count("version")) 
    {
        tt = EjfatURI::TokenType::admin;
    } else if (vm.count("register")) 
    {
        tt = EjfatURI::TokenType::instance;
    } else if (vm.count("deregister") || vm.count("state"))
    {
        tt = EjfatURI::TokenType::session;
    }

    bool preferV6 = false;
    if (vm.count("ipv6"))
    {
        preferV6 = true;
    }

    std::string ejfat_uri;
    auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>(), tt, preferV6) : 
        EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6));
    if (uri_r.has_error())
    {
        std::cerr << "Error in parsing URI from command-line, error "s + uri_r.error().message() << std::endl;
        return -1;
    }
    auto uri = uri_r.value();
    auto lbman = LBManager(uri);

    if (vm.count("root") && !uri.get_useTls())
    {
        std::cerr << "Root certificate passed in, but URL doesn't require TLS/SSL, ignoring "s;
    }
    else 
    {
        if (vm.count("root")) 
        {
            result<grpc::SslCredentialsOptions> opts_res = LBManager::makeSslOptionsFromFiles(vm["root"].as<std::string>());
            if (opts_res.has_error()) 
            {
                std::cerr << "Unable to read server root certificate file "s;
                return -1;
            }
            lbman = LBManager(uri, true, opts_res.value());
        } 
        else
        {
            if (vm.count("novalidate"))
            {
                std::cerr << "Skipping server certificate validation" << std::endl;
                lbman = LBManager(uri, false);
            }
        }
    }

    // Reserve
    if (vm.count("reserve"))
    {
        // execute command
        auto uri_r = reserveLB(lbman, vm["lbname"].as<std::string>(),
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
        auto int_r = freeLB(lbman, lbid);
        if (int_r.has_error())
        {
            std::cerr << "There was an error freeing LB: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("version"))
    {
        auto int_r = version(lbman);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting LB version: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("register"))
    {
        auto int_r = registerWorker(lbman,
                                    vm["name"].as<std::string>(),
                                    vm["address"].as<std::vector<std::string>>()[0],
                                    vm["port"].as<u_int16_t>(),
                                    vm["weight"].as<float>(),
                                    vm["count"].as<u_int16_t>(),
                                    vm["minfactor"].as<float>(),
                                    vm["maxfactor"].as<float>()
                                    );

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
        auto int_r = deregisterWorker(lbman);
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

        auto int_r = getLBStatus(lbman, lbid);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting LB status: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("state"))
    {
        // remember to set session 
        uri.set_sessionId(vm["session"].as<std::string>());
        auto int_r = sendState(lbman, vm["queue"].as<float>(), vm["ctrl"].as<float>(), vm["ready"].as<bool>());
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting sending worker state update: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("overview"))
    {

    }
    else
        // print help
        std::cout << od << std::endl;
}