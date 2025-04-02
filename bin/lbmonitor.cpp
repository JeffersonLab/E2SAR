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
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   LB ID: " << lbid << std::endl;

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

result<int> getLBOverview(LBManager &lbman)
{
    std::cout << "Getting Overview " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;

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

int main(int argc, char **argv)
{
    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "EJFAT LB Monitor"
    "This tool can be used to either check the status of a reserved LB with an instance token"
    "or to check the overview of the LB with an admin token"
    "If lbid is specified in EJFAT_URI/argument, it will default to status of LB"
    "EJFAT_URI must be specified in this format ejfat[s]://<token>@<cp name or ip>:<cp port>/lb/<lbid>");

    // parameters
    opts("lbid,i", po::value<std::string>(), "specify id of the loadbalancer as issued by reserve call instead of using what is in EJFAT_URI");
    opts("root,o", po::value<std::string>(), "root cert for SSL communications");
    opts("novalidate,v", "don't validate server certificate (conflicts with 'root')");
    opts("ipv6,6", "prefer IPv6 control plane address if URI specifies hostname (disables cert validation)");
    opts("ipv4,4", "prefer IPv4 control plane address if URI specifies hostname (disables cert validation)");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("time,t", po::value<uint64_t>(), "specify refresh time in ms (default is 5000ms)");
    
    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    } catch (const boost::program_options::unknown_option& e) {
            std::cout << "Unable to parse command line: " << e.what() << std::endl;
            return -1;
    }

    std::cout << "E2SAR Version: " << get_Version() << std::endl;
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

    // if ipv4 or ipv6 requested explicitly
    bool preferHostAddr = false;
    if (vm.count("ipv6") || vm.count("ipv4"))
        preferHostAddr = true;

    std::string ejfat_uri;
    auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>(), tt, preferV6) : 
        EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6));
    if (uri_r.has_error())
    {
        std::cerr << "Error in parsing URI from command-line, error "s + uri_r.error().message() << std::endl;
        return -1;
    }

    auto uri = uri_r.value();
    auto lbman = LBManager(uri, true, preferHostAddr);

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
            lbman = LBManager(uri, true, preferHostAddr, opts_res.value());
        }
        else
        {
            if (vm.count("novalidate"))
            {
                std::cerr << "Skipping server certificate validation" << std::endl;
                lbman = LBManager(uri, false, preferHostAddr);
            }
        }
    }

    uint64_t update_time = 5000;
    if (vm.count("time")){
        update_time = vm["time"].as<uint64_t>();
    }

    std::string lbid;
    if (vm.count("lbid"))
        lbid = vm["lbid"].as<std::string>();
    else
        lbid = lbman.get_URI().get_lbId();

    std::cout << "Use Ctrl-C to stop" << std::endl;

    while(true)
    {
        if(lbid.empty())
        {
            auto int_r = getLBOverview(lbman);
            if (int_r.has_error())
            {
                std::cerr << "There was an error getting LB overview: " << int_r.error().message() << std::endl;
                return -1;
            }
        }
        else
        {
            auto int_r = getLBStatus(lbman, lbid);
            if (int_r.has_error())
            {
                std::cerr << "There was an error getting LB status: " << int_r.error().message() << std::endl;
                return -1;
            }
        }
        boost::this_thread::sleep_for(boost::chrono::milliseconds(update_time));
    }
    return 0;
}