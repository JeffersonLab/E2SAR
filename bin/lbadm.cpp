#include <iostream>
#include <fstream>
#include <vector>
#include <boost/program_options.hpp>

#include <google/protobuf/util/time_util.h>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

// variadic template struct for handling variant lambdas
template<class... Ts> 
struct Overload : Ts... {
    using Ts::operator()...; 
};
// accompanying deduction guide
template<class... Ts> 
Overload(Ts...) -> Overload<Ts...>;

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
        if (vm.count(required_option) == 0)
            throw std::logic_error(std::string("Option '") + for_what + "' requires option '" + required_option + "'.");
}

/**
 * @param lbname is the name of the loadbalancer you give it
 * @param senders is the list of IP addresses of sender nodes
 * @param duration is a string indicating the duration you wish to reserve LB for format "hh:mm::ss"
 * @param ipfam 0 - dual stack, 1 - ipv4 only, 2 - ipv6 only
 */
result<int> reserveLB(LBManager &lbman,
                      const std::string &lbname,
                      const std::vector<std::string> &senders,
                      const std::string &duration, int ipfam, 
                      const bool suppress)
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
    if(!suppress)
    {
        std::cout << "Reserving a new load balancer " << std::endl;
        std::cout << "   Contacting: " << static_cast<std::string>(lbman.get_URI()) << " using address: " << 
            lbman.get_AddrString() << std::endl;
        std::cout << "   LB Name: " << lbname << std::endl;
        std::cout << "   Allowed senders: ";
        std::for_each(senders.begin(), senders.end(), [](const std::string& s) { std::cout << s << ' '; });
        std::cout << std::endl;
        std::cout << "   Duration: " << duration_v << std::endl;    
    }

    // attempt to reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders, ipfam);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        if(!suppress)
        {
            std::cout << "Success. FPGA ID is (for metrics): " << res.value() << std::endl;
            std::cout << "Updated URI after reserve with instance token: " << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;
        }
        else
        {
            std::cout << "export EJFAT_URI='" << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << "'\n";
        }
        return 0;
    }
}

result<int> freeLB(LBManager &lbman, const std::string &lbid = "")
{
    std::cout << "Freeing a load balancer " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin) << " using address: " << 
        lbman.get_AddrString() << std::endl;
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

result<int> registerWorker(LBManager &lbman, const std::string &node_name, 
                            const std::string &node_ip, u_int16_t node_port, 
                            float weight, u_int16_t src_cnt, float min_factor, 
                            float max_factor, bool keeplbhdr, const bool suppress)
{
    if(!suppress)
    {
        std::cout << "Registering a worker " << std::endl;
        std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << " using address: " << 
            lbman.get_AddrString() << std::endl;
        std::cout << "   Worker details: " << node_name << " at "s << node_ip << ":"s << node_port << std::endl;
        if (node_ip.length() == 0)
        {
            std::cout << "      Will attempt to determine node IP automatically" << std::endl;
        }
        std::cout << "   CP parameters: "
                << "w="s << weight << ",  source_count="s << src_cnt << std::endl;
    }
    
    auto handleError = [&suppress, &lbman](const result<int> &res) -> result<int>
    {
        if (res.has_error())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError,
                                "unable to connect to Load Balancer CP, error "s + res.error().message()};
        }
        else
        {
            if(!suppress)
            {
                std::cout << "Success." << std::endl;
                std::cout << "Updated URI after register with session token: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << std::endl;
                std::cout << "Session id is: " << lbman.get_URI().get_sessionId() << std::endl;
            }
            else
            {
                std::cout << "export EJFAT_URI='" << lbman.get_URI().to_string(EjfatURI::TokenType::instance) << "'\n";
            }
            return 0;
        }
    };

    if (node_ip.length() > 0)
    {
        return handleError(lbman.registerWorker(node_name, std::pair<ip::address, u_int16_t>(ip::make_address(node_ip), node_port), weight, src_cnt, min_factor, max_factor, keeplbhdr));
    } else
    {
        return handleError(lbman.registerWorkerSelf(node_name, node_port, weight, src_cnt, min_factor, max_factor, keeplbhdr));
    }
}

result<int> deregisterWorker(LBManager &lbman)
{
    std::cout << "De-Registering a worker " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;

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
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
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

        std::cout << "LB details: expiresat=" << lbstatus->expiresAt << ", currentepoch=" << lbstatus->currentEpoch << ", predictedeventnum=" << 
            lbstatus->currentPredictedEventNumber << std::endl;

        std::cout << "Registered senders: ";
        for (auto a : lbstatus->senderAddresses)
            std::cout << a << " "s;
        std::cout << std::endl;

        std::cout << "Registered workers: " << std::endl;
        for (auto w : lbstatus->workers)
        {
            std::cout << "[ name="s << w.name() << ", controlsignal="s << w.controlsignal() << ", fillpercent="s << w.fillpercent() << 
                ", slotsassigned="s << w.slotsassigned() << ", lastupdated=" << *w.mutable_lastupdated() << 
                ", IP Address=" << w.ipaddress() << ", UDP Port=" << w.udpport() << ", minFactor=" << w.minfactor() <<
                ", maxFactor=" << w.maxfactor() << ", keepLBHeader=" << w.keeplbheader() << ", totalEventsRecv=" << w.totaleventsrecv() <<
                ", totalEventsReassembled=" << w.totaleventsreassembled() << ", " << w.totaleventsreassemblyerr() << 
                ", totalEventsDequeued=" << w.totaleventsdequeued() << ", totalEventEnqueueErr=" << w.totaleventenqueueerr() <<
                ", totalBytesRecv=" << w.totalbytesrecv() << ", totalPacketsRecv=" << w.totalpacketsrecv() <<
                "] "s << std::endl;
        }
        std::cout << std::endl;

        return 0;
    }
}

result<int> overview(LBManager &lbman)
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
            std::cout << "LB " << r.name << " ID: " << r.lbid << " FPGA LBID: " << r.fpgaLBId << 
                " Data Min Port: " << r.dataMinPort << " Data Max Port: " << r.dataMaxPort << 
                std::endl;
            std::cout << "  Sync on: " << r.syncIPv4AndPort.first << ":" << r.syncIPv4AndPort.second << " " << 
                r.syncIPv6AndPort.first << ":" << r.syncIPv6AndPort.second << std::endl;
            std::cout << "  Registered sender addresses: ";
            for (auto a : r.status.senderAddresses)
                std::cout << a << " "s;
            std::cout << std::endl;

            std::cout << "  Registered workers: " << std::endl;
            for (auto w : r.status.workers)
            {
                std::cout << "  [ name="s << w.name() << ", controlsignal="s << w.controlsignal() << 
                    ", fillpercent="s << w.fillpercent() << ", slotsassigned="s << w.slotsassigned() << 
                    ", lastupdated=" << *w.mutable_lastupdated() << 
                    ", IP Address=" << w.ipaddress() << ", UDP Port=" << w.udpport() << ", minFactor=" << w.minfactor() <<
                    ", maxFactor=" << w.maxfactor() << ", keepLBHeader=" << w.keeplbheader() << ", totalEventsRecv=" << w.totaleventsrecv() <<
                    ", totalEventsReassembled=" << w.totaleventsreassembled() << ", " << w.totaleventsreassemblyerr() << 
                    ", totalEventsDequeued=" << w.totaleventsdequeued() << ", totalEventEnqueueErr=" << w.totaleventenqueueerr() <<
                    ", totalBytesRecv=" << w.totalbytesrecv() << ", totalPacketsRecv=" << w.totalpacketsrecv() <<
                    "] "s << std::endl;
            }
            std::cout << std::endl;

            std::cout << "  LB details: expiresat=" << r.status.expiresAt << ", currentepoch=" << 
                r.status.currentEpoch << ", predictedeventnum=" << 
                r.status.currentPredictedEventNumber << std::endl;
        }
        return 0;
    }
}

result<int> sendState(LBManager &lbman, float fill_percent, float ctrl_signal, bool is_ready, const WorkerStats &stats)
{
    std::cout << "Sending Worker State " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;

    auto res = lbman.sendState(fill_percent, ctrl_signal, is_ready, stats);

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

result<int> removeSenders(LBManager &lbman, const std::vector<std::string>& senders)
{
    std::cout << "Removing senders to CP " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   Sender list: ";
    std::for_each(senders.begin(), senders.end(), [](const std::string& s) { std::cout << s << ' '; });

    auto handleError = [](const result<int> &res) -> result<int>
    {
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
    };
    auto res = lbman.removeSenders(senders);   

    if (senders.size() > 0)
    {
        std::cout << "   Sender list: ";
        std::for_each(senders.begin(), senders.end(), [](const std::string& s) { std::cout << s << ' '; });
        std::cout << std::endl;
        return handleError(lbman.removeSenders(senders));
    } else
    {
        std::cout << "   Will attempt to determine sender IP automatically" << std::endl;
        return handleError(lbman.removeSenderSelf());
    }
}

result<int> addSenders(LBManager &lbman, const std::vector<std::string>& senders)
{
    std::cout << "Adding senders to CP " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;

    auto handleError = [](const result<int> &res) -> result<int>
    {
        if (res.has_error())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError,
                                "Unable to add sender(s), error "s + res.error().message()};
        }
        else
        {
            std::cout << "Success." << std::endl;
            return 0;
        }
    };

    if (senders.size() > 0)
    {
        std::cout << "   Sender list: ";
        std::for_each(senders.begin(), senders.end(), [](const std::string& s) { std::cout << s << ' '; });
        std::cout << std::endl;
        return handleError(lbman.addSenders(senders));
    } else
    {
        std::cout << "   Will attempt to determine sender IP automatically" << std::endl;
        return handleError(lbman.addSenderSelf());
    }
}

result<int> version(LBManager &lbman)
{

    std::cout << "Getting load balancer version " << std::endl;
    std::cout << "   Contacting: " << static_cast<std::string>(lbman.get_URI()) << " using address: " << 
        lbman.get_AddrString() << std::endl;

    auto res = lbman.version();

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to Load Balancer CP, error "s + res.error().message()};
    }
    else
    {
        std::cout << "Success." << std::endl;
        std::cout << "Reported version: " << std::endl <<
            "\tCommit: " << res.value().get<0>() << std::endl << 
            "\tBuild: " << res.value().get<1>() << std::endl <<
            "\tCompatTag: " << res.value().get<2>() << std::endl;
        return 0;
    }
}

result<int> timeseries(LBManager &lbman, const std::string& lbpath, const std::string& since, const std::string& csvsaveto)
{
    std::cout << "Requesting timeseries " << std::endl;
    std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::session) << " using address: " << 
        lbman.get_AddrString() << std::endl;
    std::cout << "   LB Name: " << (lbman.get_URI().get_lbName().empty() ? "not set"s : lbman.get_URI().get_lbId()) << std::endl;
    std::cout << "   Query  path: " << lbpath << std::endl;
    std::cout << "   Since: " << since << std::endl;
    std::cout << "   Save to CSV: " << csvsaveto << std::endl;

    google::protobuf::Timestamp ts;
    if (not google::protobuf::util::TimeUtil::FromString(since, &ts))
        return E2SARErrorInfo{E2SARErrorc::ParameterError,
                            "unable to convert into timestamp: " + since};

    auto res = lbman.timeseries(lbpath, ts);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to connect to retrieve timeseries, error "s + res.error().message()};
    }
 
    std::cout << "Success. Saving timeseries to CSV." << std::endl;

    auto rsres(std::move(res.value()));

    //
    // we will assume timeseries can be of different lengths with different timestamps for values
    //
    // 1. Write out column headers
    // 2. Iterate over timeseries indices writing out rows, putting empty entries as needed (i.e. ',,')
    // 
    std::ofstream csvFile(csvsaveto);
    if (not csvFile)
    {
        return E2SARErrorInfo{E2SARErrorc::SystemError,
                              "unable to write timeseries file "s + csvsaveto};        
    }

    // write out two columns per series: /lb/path(unit),ts,
    for(size_t col=0; col<rsres.td.size(); col++)
    {
        csvFile << rsres.td[col].path;
        if (not rsres.td[col].unit.empty())
            csvFile << "(" << rsres.td[col].unit << "),";
        else
            csvFile << ",";
        csvFile << "Timestamp(ms),";
    }
    csvFile << std::endl;
    csvFile.flush();

    size_t tdIdx{0};
    while(true)
    {
        // timeseries can be of different lengths
        bool finished{true};
        for(size_t col=0; col<rsres.td.size(); col++)
        {
            std::visit(Overload {
                [&csvFile, &finished, tdIdx](const std::vector<FloatSample>& samples) {
                    if (tdIdx < samples.size()) {
                        csvFile << samples[tdIdx].value << "," << samples[tdIdx].timestamp_ms << ",";
                        finished = false;
                    } else
                        csvFile << ",,"; //skip this column
                },
                [&csvFile, &finished, tdIdx](const std::vector<IntegerSample>& samples) {
                    if (tdIdx < samples.size())
                    {
                        csvFile << samples[tdIdx].value << "," << samples[tdIdx].timestamp_ms << ",";
                        finished = false;
                    } else
                        csvFile << ",,"; // skip this column
                },
            },rsres.td[col].timeseries);
        }
        tdIdx++;
        csvFile << std::endl;
        if (finished)
            break;
    }

    csvFile.close();
    return 0;
}

/**
 * Parse permission strings into e2sar::TokenPermission objects
 * Format: RESOURCE_TYPE:RESOURCE_ID:PERMISSION_TYPE
 * Example: "ALL::READ_ONLY" or "LOAD_BALANCER:lb1:UPDATE"
 */
result<std::vector<e2sar::TokenPermission>>
parsePermissions(const std::vector<std::string>& permStrings)
{
    using namespace e2sar;
    std::vector<TokenPermission> perms;

    // Maps for string to enum conversion
    std::map<std::string, EjfatURI::TokenType> resourceTypeMap = {
        {"ALL", EjfatURI::TokenType::all},
        {"LOAD_BALANCER", EjfatURI::TokenType::load_balancer},
        {"RESERVATION", EjfatURI::TokenType::reservation},
        {"SESSION", EjfatURI::TokenType::session}
    };

    std::map<std::string, EjfatURI::TokenPermission> permissionTypeMap = {
        {"READ_ONLY", EjfatURI::TokenPermission::_read_only_},
        {"REGISTER", EjfatURI::TokenPermission::_register_},
        {"RESERVE", EjfatURI::TokenPermission::_reserve_},
        {"UPDATE", EjfatURI::TokenPermission::_update_}
    };

    for (const auto& perm_str : permStrings)
    {
        // Split by ':' delimiter
        size_t first_colon = perm_str.find(':');
        size_t second_colon = perm_str.find(':', first_colon + 1);

        if (first_colon == std::string::npos || second_colon == std::string::npos)
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError,
                "Invalid permission format: '"s + perm_str +
                "'. Expected format: RESOURCE_TYPE:RESOURCE_ID:PERMISSION_TYPE"s};
        }

        std::string res_type_str = perm_str.substr(0, first_colon);
        std::string res_id = perm_str.substr(first_colon + 1, second_colon - first_colon - 1);
        std::string perm_type_str = perm_str.substr(second_colon + 1);

        // Validate resource type
        if (resourceTypeMap.find(res_type_str) == resourceTypeMap.end())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError,
                "Invalid resource type: '"s + res_type_str +
                "'. Valid types: ALL, LOAD_BALANCER, RESERVATION, SESSION"s};
        }

        // Validate permission type
        if (permissionTypeMap.find(perm_type_str) == permissionTypeMap.end())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError,
                "Invalid permission type: '"s + perm_type_str +
                "'. Valid types: READ_ONLY, REGISTER, RESERVE, UPDATE"s};
        }

        // Create TokenPermission object
        TokenPermission perm;
        perm.resourceType = resourceTypeMap[res_type_str];
        perm.resourceId = res_id;  // Can be empty string
        perm.permission = permissionTypeMap[perm_type_str];

        perms.push_back(perm);
    }

    return perms;
}

/**
 * Create a TokenSelector variant from a string (ID or token string)
 */
e2sar::TokenSelector createTokenSelector(const std::string& tokenid_str)
{
    // Try to parse as uint32_t first
    if (!tokenid_str.empty() &&
        std::all_of(tokenid_str.begin(), tokenid_str.end(), ::isdigit))
    {
        try
        {
            uint32_t id = std::stoul(tokenid_str);
            return id;  // Return uint32_t variant
        }
        catch (...)
        {
            // Parse failed, use as token string
        }
    }
    // Return string variant
    return tokenid_str;
}

result<int> createToken(LBManager &lbman,
                       const std::string &name,
                       const std::vector<e2sar::TokenPermission> &permissions,
                       const bool suppress)
{
    using namespace e2sar;
    if(!suppress)
    {
        std::cout << "Creating a new token " << std::endl;
        std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin)
                  << " using address: " << lbman.get_AddrString() << std::endl;
        std::cout << "   Token name: " << name << std::endl;
        std::cout << "   Permissions (" << permissions.size() << "):" << std::endl;

        for (const auto& perm : permissions)
        {
            std::cout << "      ResourceType=" << static_cast<u_int16_t>(perm.resourceType)
                      << ", ResourceId=" << (perm.resourceId.empty() ? "(none)"s : perm.resourceId)
                      << ", Permission=" << static_cast<u_int16_t>(perm.permission) << std::endl;
        }
    }

    auto res = lbman.createToken(name, permissions);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to create token, error "s + res.error().message()};
    }
    else
    {
        if(!suppress)
        {
            std::cout << "Success. Token created." << std::endl;
            std::cout << "Token: " << res.value() << std::endl;
        }
        else
        {
            std::cout << res.value() << "\n";
        }
        return 0;
    }
}

result<int> listTokenPermissions(LBManager &lbman,
                                 const std::string &tokenid_str,
                                 const bool suppress)
{
    using namespace e2sar;
    if(!suppress)
    {
        std::cout << "Listing token permissions " << std::endl;
        std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin)
                  << " using address: " << lbman.get_AddrString() << std::endl;
        std::cout << "   Token ID/String: " << tokenid_str << std::endl;
    }

    auto selector = createTokenSelector(tokenid_str);
    auto res = lbman.listTokenPermissions(selector);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to list token permissions, error "s + res.error().message()};
    }
    else
    {
        const auto& details = res.value();

        if(!suppress)
        {
            std::cout << "Success." << std::endl;
            std::cout << "Token Details:" << std::endl;
            std::cout << "  Name: " << details.name << std::endl;
            std::cout << "  ID: " << details.id << std::endl;
            std::cout << "  Created: " << details.created_at << std::endl;
            std::cout << "  Permissions (" << details.permissions.size() << "):" << std::endl;

            for (const auto& perm : details.permissions)
            {
                std::cout << "    [ resourceType=" << EjfatURI::toString(perm.resourceType)
                          << ", resourceId=" << (perm.resourceId.empty() ? "(none)"s : perm.resourceId)
                          << ", permission=" << EjfatURI::toString(perm.permission) << " ]" << std::endl;
            }
        }
        else
        {
            // Export mode: output token ID
            std::cout << details.id << "\n";
        }
        return 0;
    }
}

result<int> listChildTokens(LBManager &lbman,
                           const std::string &tokenid_str,
                           const bool suppress)
{
    using namespace e2sar;
    if(!suppress)
    {
        std::cout << "Listing child tokens " << std::endl;
        std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin)
                  << " using address: " << lbman.get_AddrString() << std::endl;
        std::cout << "   Parent Token ID/String: " << tokenid_str << std::endl;
    }

    auto selector = createTokenSelector(tokenid_str);
    auto res = lbman.listChildTokens(selector);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to list child tokens, error "s + res.error().message()};
    }
    else
    {
        const auto& child_tokens = res.value();

        if(!suppress)
        {
            std::cout << "Success." << std::endl;
            std::cout << "Child tokens (" << child_tokens.size() << "):" << std::endl;

            if (child_tokens.empty())
            {
                std::cout << "  (no child tokens)" << std::endl;
            }
            else
            {
                for (const auto& token : child_tokens)
                {
                    std::cout << "  [ name=" << token.name
                              << ", id=" << token.id
                              << ", created=" << token.created_at
                              << ", permissions=" << token.permissions.size() << " ]" << std::endl;
                }
            }
        }
        else
        {
            // Export mode: output count
            std::cout << child_tokens.size() << "\n";
        }
        return 0;
    }
}

result<int> revokeToken(LBManager &lbman,
                       const std::string &tokenid_str,
                       const bool suppress)
{
    using namespace e2sar;
    if(!suppress)
    {
        std::cout << "Revoking token (including all children) " << std::endl;
        std::cout << "   Contacting: " << lbman.get_URI().to_string(EjfatURI::TokenType::admin)
                  << " using address: " << lbman.get_AddrString() << std::endl;
        std::cout << "   Token ID/String: " << tokenid_str << std::endl;
    }

    auto selector = createTokenSelector(tokenid_str);
    auto res = lbman.revokeToken(selector);

    if (res.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError,
                              "unable to revoke token, error "s + res.error().message()};
    }
    else
    {
        if(!suppress)
        {
            std::cout << "Success. Token revoked (including all child tokens)." << std::endl;
        }
        else
        {
            std::cout << "0\n";
        }
        return 0;
    }
}

int main(int argc, char **argv)
{

    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");
    std::string duration, lbpath, since, csvsaveto;
    float weight;
    u_int16_t count;
    float queue, ctrl, minfactor, maxfactor;
    bool ready, keeplbhdr;
    int ipfam;
    // all the sendState stats
    WorkerStats stats;

    // parameters
    opts("lbname,l", po::value<std::string>(), "specify name of the load balancer");
    opts("lbid,i", po::value<std::string>(), "override/provide id of the loadbalancer");
    opts("address,a", po::value<std::vector<std::string>>()->multitoken(), "node IPv4/IPv6 address, can be used multiple times for 'reserve' call");
    opts("duration,d", po::value<std::string>(&duration)->default_value("02:00:00"), "specify duration as '[hh[:mm[:ss]]]'");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("name,n", po::value<std::string>(), "specify node name for registration");
    opts("port,p", po::value<u_int16_t>(), "node starting listening port number");
    opts("weight,w", po::value<float>(&weight)->default_value(1.0), "node weight");
    opts("count,c", po::value<u_int16_t>(&count)->default_value(1), "node source count");
    opts("session,s", po::value<std::string>(), "override/provide session id");
    opts("queue,q", po::value<float>(&queue)->default_value(0.0), "queue fill");
    opts("ctrl,t", po::value<float>(&ctrl)->default_value(0.0), "control signal value");
    opts("ready,r", po::value<bool>(&ready)->default_value(true), "worker ready state (1 or 0)");
    opts("root,o", po::value<std::string>(), "root cert for SSL communications");
    opts("novalidate,v", "don't validate server certificate (conflicts with 'root')");
    opts("minfactor", po::value<float>(&minfactor)->default_value(0.5), "node min factor, multiplied with the number of slots that would be assigned evenly to determine min number of slots for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots");
    opts("maxfactor", po::value<float>(&maxfactor)->default_value(2.0), "multiplied with the number of slots that would be assigned evenly to determine max number of slots for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum");
    opts("ipv6,6", "force using IPv6 control plane address if URI specifies hostname (disables cert validation)");
    opts("ipv4,4", "force using IPv4 control plane address if URI specifies hostname (disables cert validation)");
    opts("export,e", "suppresses other messages and prints out 'export EJFAT_URI=<the new uri>' returned by the LB");
    opts("keeplbhdr", po::value<bool>(&keeplbhdr)->default_value(false), "do not remove LB header (in 'register' call; defaults to false)");
    opts("ipfam", po::value<int>(&ipfam)->default_value(0), "specify whether the LB should be dual stacked [0], ipv4 only [1] or ipv6 only (in 'reserve' call; defaults to 0)");
    opts("lbpath", po::value<std::string>(&lbpath)->default_value("/lb/1/*"), "LB path (used for timeseries(e.g., '/lb/1/*', '/lb/1/session/2/totalEventsReassembled')");
    opts("since", po::value<std::string>(&since)->default_value("1972-01-01T10:00:20.021Z"), "time stamp in the form of 1972-01-01T10:00:20.021Z (starting point for timeseries)");
    opts("csv", po::value<std::string>(&csvsaveto)->default_value("timeseries.csv"), "name of the file to save timeseries in CSV format (comma-separated)");

    // send state (or 'state' command) optional stats
    opts("total_events_recv", po::value<int64_t>(&stats.total_events_recv)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_events_reassembled", po::value<int64_t>(&stats.total_events_reassembled)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_events_reassembly_err", po::value<int64_t>(&stats.total_events_reassembly_err)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_events_dequeued", po::value<int64_t>(&stats.total_events_dequeued)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_event_enqueue_err", po::value<int64_t>(&stats.total_event_enqueue_err)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_bytes_recv", po::value<int64_t>(&stats.total_bytes_recv)->default_value(0), "optional stats for 'state' command, defaults to 0");
    opts("total_packets_recv", po::value<int64_t>(&stats.total_packets_recv)->default_value(0), "optional stats for 'state' command, defaults to 0");

    // Token management options
    opts("tokenname", po::value<std::string>(), "name for new token (used with 'createtoken')");
    opts("permission", po::value<std::vector<std::string>>()->multitoken(),
         "permission spec: RESOURCE_TYPE:RESOURCE_ID:PERMISSION_TYPE (e.g., 'ALL::READ_ONLY' or 'LOAD_BALANCER:lb1:UPDATE'), can be specified multiple times");
    opts("tokenid", po::value<std::string>(), "token ID (numeric) or token string to target");

    // commands
    opts("reserve", "reserve a load balancer (-l, -a, -d required). Uses admin token.");
    opts("free", "free a load balancer. Uses instance or admin token.");
    opts("version", "report the version of the LB. Uses admin or instance token.");
    opts("register", "register a worker (-n, -p, -w, -c required; either use -a to specify receive address, or auto-detection will register incoming interface address), note you must use 'state' within 10 seconds or worker is deregistered. Uses instance or admin token.");
    opts("deregister", "deregister worker. Uses instance or session token.");
    opts("status", "get and print LB status. Uses admin or instance token.");
    opts("state", "send worker state update (must be done within 10 sec of registration) (-q, -c, -r required). Uses session token.");
    opts("overview","return metadata and status information on all registered load balancers. Uses admin token.");
    opts("addsenders","add 'safe' sender IP addresses to CP (use one or more -a to specify addresses, if none are specified auto-detection is used to determine outgoing interface address). Uses instance token.");
    opts("removesenders","remove 'safe' sender IP addresses from CP (use one or more -a to specify addresses, if none are specified auto-detection is used to determine outgoing interface address). Uses instance token.");
    opts("timeseries", "return requested timeseries based on a path (e.g., '/lb/1/*', '/lb/1/session/2/totalEventsReassembled')");
    opts("createtoken", "create a new delegated token (--tokenname, --permission required). Uses admin token.");
    opts("listtokenpermissions", "list all permissions for a token (--tokenid required). Uses admin token.");
    opts("listchildtokens", "list all child tokens of a parent (--tokenid required). Uses admin token.");
    opts("revoketoken", "revoke a token and all its children (--tokenid required). Uses admin token.");

    std::vector<std::string> commands{"reserve", "free", "version", "register",
        "deregister", "status", "state", "overview", "addsenders", "removesenders",
        "timeseries", "createtoken", "listtokenpermissions", "listchildtokens", "revoketoken"};

    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    } catch (const boost::program_options::error& e) {
            std::cout << "Unable to parse command line: " << e.what() << std::endl;
            return -1;
    }

    // specify all options dependencies here
    try
    {
        option_dependency(vm, "reserve", "lbname");
        option_dependency(vm, "reserve", "duration");
        option_dependency(vm, "register", "name");
        option_dependency(vm, "register", "port");
        option_dependency(vm, "register", "weight");
        option_dependency(vm, "register", "count");
        option_dependency(vm, "register", "minfactor");
        option_dependency(vm, "register", "maxfactor");
        option_dependency(vm, "state", "queue");
        option_dependency(vm, "state", "ctrl");   
        option_dependency(vm, "state", "ready");
        option_dependency(vm, "timeseries", "lbpath");
        option_dependency(vm, "createtoken", "tokenname");
        option_dependency(vm, "createtoken", "permission");
        option_dependency(vm, "listtokenpermissions", "tokenid");
        option_dependency(vm, "listchildtokens", "tokenid");
        option_dependency(vm, "revoketoken", "tokenid");
        conflicting_options(vm, "root", "novalidate");
        conflicting_options(vm, "ipv4", "ipv6");

        for (auto c1: commands)
        {
            for (auto c2: commands)
            {
                if (c1.compare(c2))
                    conflicting_options(vm, c1, c2);
            }
        }
    }
    catch (const std::logic_error &le)
    {
        std::cerr << "Error processing command-line options: " << le.what() << std::endl;
        return -1;
    }

    bool suppress = false;
    if(vm.count("export")){
        suppress = true;
    }

    if(!suppress)
        std::cout << "E2SAR Version: " << get_Version() << std::endl;
    if (vm.count("help") || vm.empty())
    {
        std::cout << od << std::endl;
        return 0;
    }

    
    // make sure the token is interpreted as the correct type, depending on the call
    EjfatURI::TokenType tt{EjfatURI::TokenType::admin};
    if (vm.count("reserve") || vm.count("free") || vm.count("status") || vm.count("version") ||
        vm.count("overview") || vm.count("timeseries") ||
        vm.count("createtoken") || vm.count("listtokenpermissions") ||
        vm.count("listchildtokens") || vm.count("revoketoken"))
    {
        tt = EjfatURI::TokenType::admin;
    } else if (vm.count("register") || vm.count("addsenders") || vm.count("removesenders"))
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

    // remember to override session if provided
    if (vm.count("session")) 
        uri.set_sessionId(vm["session"].as<std::string>());

    // remember to override lbid if provided
    if (vm.count("lbid")) 
        uri.set_lbId(vm["lbid"].as<std::string>());

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

    // Reserve
    if (vm.count("reserve"))
    {
        // execute command
        auto uri_r = reserveLB(lbman, vm["lbname"].as<std::string>(),
                               (vm.count("address") > 0 ? vm["address"].as<std::vector<std::string>>(): std::vector<std::string>()),
                               duration, ipfam, suppress);
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
                                    (vm.count("address") > 0 ? vm["address"].as<std::vector<std::string>>()[0]: ""s),
                                    vm["port"].as<u_int16_t>(),
                                    weight,
                                    count,
                                    minfactor,
                                    maxfactor,
                                    keeplbhdr,
                                    suppress
                                    );

        if (int_r.has_error())
        {
            std::cerr << "There was an error registering worker: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("deregister"))
    {
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
        auto int_r = sendState(lbman, queue, ctrl, ready, stats);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting sending worker state update: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("overview"))
    {
        auto int_r = overview(lbman);
        if (int_r.has_error())
        {
            std::cerr << "There was an error getting overview: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("addsenders"))
    {
        auto int_r = addSenders(lbman, 
            (vm.count("address") > 0 ? vm["address"].as<std::vector<std::string>>(): std::vector<std::string>()));
        if (int_r.has_error())
        {
            std::cerr << "There was an error adding senders: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("removesenders"))
    {
        auto int_r = removeSenders(lbman, 
            (vm.count("address") > 0 ? vm["address"].as<std::vector<std::string>>(): std::vector<std::string>()));
        if (int_r.has_error())
        {
            std::cerr << "There was an error removing senders: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("timeseries"))
    {
        auto int_r = timeseries(lbman, lbpath, since, csvsaveto);
        if (int_r.has_error())
        {
            std::cerr << "There was an error querying for timeseries: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("createtoken"))
    {
        // Parse permissions from command line strings
        auto perms_r = parsePermissions(vm["permission"].as<std::vector<std::string>>());
        if (perms_r.has_error())
        {
            std::cerr << "Error parsing permissions: " << perms_r.error().message() << std::endl;
            return -1;
        }

        auto int_r = createToken(lbman, vm["tokenname"].as<std::string>(),
                                perms_r.value(), suppress);
        if (int_r.has_error())
        {
            std::cerr << "There was an error creating token: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("listtokenpermissions"))
    {
        auto int_r = listTokenPermissions(lbman, vm["tokenid"].as<std::string>(), suppress);
        if (int_r.has_error())
        {
            std::cerr << "There was an error listing token permissions: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("listchildtokens"))
    {
        auto int_r = listChildTokens(lbman, vm["tokenid"].as<std::string>(), suppress);
        if (int_r.has_error())
        {
            std::cerr << "There was an error listing child tokens: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else if (vm.count("revoketoken"))
    {
        auto int_r = revokeToken(lbman, vm["tokenid"].as<std::string>(), suppress);
        if (int_r.has_error())
        {
            std::cerr << "There was an error revoking token: " << int_r.error().message() << std::endl;
            return -1;
        }
    }
    else
        // print help
        std::cout << od << std::endl;
}