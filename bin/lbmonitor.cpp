#include <iostream>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp> 

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

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

int main(int argc, char **argv)
{
    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");

    // parameters
    opts("lbname,l", po::value<std::string>(), "specify name of the load balancer");
    opts("lbid,i", po::value<std::string>(), "specify id of the loadbalancer as issued by reserve call instead of using what is in EJFAT_URI");
    opts("address,a", po::value<std::string>(), "node IPv4/IPv6 address");
    opts("name,n", po::value<std::string>(), "specify node name for registration");
    opts("port,p", po::value<u_int16_t>(), "node starting listening port number");
    opts("root,o", po::value<std::string>(), "root cert for SSL communications");
    opts("novalidate,v", "don't validate server certificate (conflicts with 'root')");
    opts("ipv6,6", "prefer IPv6 control plane address if URI specifies hostname");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("time,t", po::value<std::string>(), "specify update time in ms");
    
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    if (vm.count("help") || vm.empty())
    {
        std::cout << od << std::endl;
        return 0;
    }

    //admin needed for status
    EjfatURI::TokenType tt{EjfatURI::TokenType::admin};

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

    uint64_t update_time = 5000;
    if (vm.count("time")){
        update_time = vm["count"].as<uint64_t>();
    }

    while(true)
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

        boost::this_thread::sleep_for(boost::chrono::milliseconds(update_time));
    }
    return 0;
}