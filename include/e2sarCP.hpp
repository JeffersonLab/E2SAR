#ifndef E2SARCPHPP
#define E2SARCPHPP
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <google/protobuf/util/time_util.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include "grpc/loadbalancer.grpc.pb.h"

#include "e2sarUtil.hpp"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using loadbalancer::LoadBalancer;

using loadbalancer::LoadBalancerStatusReply;
using loadbalancer::WorkerStatus;
using loadbalancer::OverviewReply;

using google::protobuf::Timestamp;

// default reservation duration for a load balancer in hours
#define DEFAULT_LB_RESERVE_DURATION 24

/***
 * Control Plane definitions for E2SAR
 */

namespace e2sar
{
    /**
    The LBManager knows how to speak to load balancer control plane over gRPC.
    It can be run from Segmenter, Reassembler or a third party like the
    workflow manager.
    */

    /** This is used to specify for how long the loadbalancer is needed
     * use google::protobuf::util::FromString(string, TimeStamp*) to convert from
     * RFC 3339 string like "1972-01-01T10:00:20.021Z" or "1972-01-01T10:00:20.021-05:00"
     */
    using TimeUntil = google::protobuf::Timestamp;

    /**
     * Optional stats sent by individual workers in sendState and received in worker status from lbstatus about their performance
     * int64 totalEventsRecv - how many event ids the receiver has seen
     * int64 totalEventsReassembled - how many events has the receiver reassembled
     * int64 totalEventsReassemblyErr - how many events has the receiver dropped before reassembly
     * int64 totalEventsDequeued - how many events popped off the queue
     * int64 totalEventEnqueueErr - how many events has the receiver failed to put on the queue because it's full
     * int64 totalBytesRecv - total bytes received
     * int64 totalPacketsRecv - total packets received
     */
    struct WorkerStats {
        int64_t total_events_recv, total_events_reassembled, total_events_reassembly_err, total_events_dequeued,
            total_event_enqueue_err, total_bytes_recv, total_packets_recv;
        
        WorkerStats(): total_events_recv{0}, total_events_reassembled{0}, total_events_reassembly_err{0},
            total_events_dequeued{0}, total_event_enqueue_err{0}, total_bytes_recv{0}, total_packets_recv{0} {
            std::cout << "ZEROING OUT" << std::endl;
        }
    };

    /**
     * Status of LB (converted to this structure from loadbalancer::LoadBalancerStatusReply)
     */
    struct LBStatus
    {
        google::protobuf::Timestamp timestamp;
        u_int64_t currentEpoch;
        u_int64_t currentPredictedEventNumber;
        std::vector<WorkerStatus> workers;
        std::vector<std::string> senderAddresses;
        google::protobuf::Timestamp expiresAt;

        /**
         * Constructor for status struct that uses move semantics for workers and sendAddresses
         */
        LBStatus(google::protobuf::Timestamp ts, u_int64_t ce, u_int64_t penum, std::vector<WorkerStatus> &w, std::vector<std::string> &sa, google::protobuf::Timestamp exp):
        timestamp{ts}, currentEpoch{ce}, currentPredictedEventNumber{penum}, expiresAt{exp}
        {
            workers = std::move(w);
            senderAddresses = std::move(sa);
        }
        LBStatus() {}
    };

    /**
     * Overview - makes a list of these entries, they are simpler to traverse than the protobuf definitions
     */
    struct OverviewEntry
    {
        std::string name; // name passed in reserveLB
        std::string lbid; // load balancer id
        std::pair<ip::address, u_int16_t> syncIPv4AndPort;
        std::pair<ip::address, u_int16_t> syncIPv6AndPort;
        ip::address dataIPv4;
        ip::address dataIPv6; 
        u_int32_t fpgaLBId; 
        u_int32_t dataMinPort;
        u_int32_t dataMaxPort;
        LBStatus status; // same as in lbstatus call

        OverviewEntry() {}
    };
    using OverviewMessage = std::vector<OverviewEntry>;

    /**
     * Timeseries sample with timestamp and value for float data
     */
    struct FloatSample {
        int64_t timestamp_ms;  // milliseconds since epoch
        float value;

        FloatSample(int64_t ts, float v) : timestamp_ms{ts}, value{v} {}
        FloatSample() : timestamp_ms{0}, value{0.0f} {}
    };

    /**
     * Timeseries sample with timestamp and value for integer data
     */
    struct IntegerSample {
        int64_t timestamp_ms;  // milliseconds since epoch
        int64_t value;

        IntegerSample(int64_t ts, int64_t v) : timestamp_ms{ts}, value{v} {}
        IntegerSample() : timestamp_ms{0}, value{0} {}
    };

    /**
     * Timeseries data - either float or integer samples with path and units
     */
    struct TimeseriesData {
        std::string path;
        std::string unit;
        std::variant<std::vector<FloatSample>, std::vector<IntegerSample>> timeseries;

        TimeseriesData(const std::string& p, const std::string &u, std::vector<FloatSample>& fs):
            path{p}, unit{u}, timeseries{std::move(fs)} {}
        TimeseriesData(const std::string& p, const std::string &u, std::vector<IntegerSample>& is):
            path{p}, unit{u}, timeseries{std::move(is)} {}
    };

    // a collection of timeseries with a common since timestamp
    struct TimeseriesResult {
        int64_t since_ms;
        std::vector<TimeseriesData> td; // possibly multiple vectors of timeseries data returned by the query

        TimeseriesResult(int64_t _ts, std::vector<TimeseriesData> &_td): since_ms{_ts}, td{std::move(_td)} {}

        // be careful this is not a true copy constructor as it uses move to move the timeseries
        TimeseriesResult(TimeseriesResult&& _tsr): since_ms{_tsr.since_ms}, td{std::move(_tsr.td)} {}
    };

    /**
     * Token permission - defines access rights for a token
     * Mirrors loadbalancer::TokenPermission protobuf without exposing it
     */
    struct TokenPermission {
        EjfatURI::TokenType resourceType;
        std::string resourceId;  // optional, can be empty
        EjfatURI::TokenPermission permission;

        TokenPermission():
            resourceType{EjfatURI::TokenType::all},
            resourceId{""},
            permission{EjfatURI::TokenPermission::_read_only_} {}
        TokenPermission(EjfatURI::TokenType rt, const std::string& rid, EjfatURI::TokenPermission pt):
            resourceType{rt}, resourceId{rid}, permission{pt} {}
    };

    /**
     * Token details - information about a token including permissions
     * Mirrors loadbalancer::TokenDetails protobuf without exposing it
     */
    struct TokenDetails {
        std::string name;
        std::vector<TokenPermission> permissions;
        std::string created_at;
        uint32_t id;

        TokenDetails(): name{""}, created_at{""}, id{0} {}
    };

    /**
     * Token selector - select a token by ID or token string
     * Uses std::variant instead of loadbalancer::TokenSelector protobuf
     */
    using TokenSelector = std::variant<uint32_t, std::string>;

    class LBManager
    {
    private:
        EjfatURI _cpuri;
        std::string addr_string;
        std::unique_ptr<LoadBalancer::Stub> _stub;
        std::shared_ptr<grpc::Channel> _channel;

    protected:
    public:
        /**
         * Initialize manager. Default is with TLS/SSL and default client options. To enable
         * custom SSL configuration with custom root certs, private key (for authN) and cert
         * use makeSslOptions[FromFiles]() methods and pass the output to this constructor.
         * @param cpuri an EjfatURI object parsed from configuration data
         * @param validateServer if false, skip server certificate validation (useful for self-signed testing)
         * @param useHostAddress even if hostname is provided, use host address as resolved by URI object (with preference for 
         * IPv4 by default or for IPv6 if explicitly requested)
         * @param opts grpc::SslCredentialsOptions containing some combination of server root certs, client key and client cert
         * use of SSL/TLS is governed by the URI scheme ('ejfat' vs 'ejfats')
         */
        LBManager(const EjfatURI &cpuri, bool validateServer = true, bool useHostAddress = false,
                  grpc::SslCredentialsOptions opts = grpc::SslCredentialsOptions()) : _cpuri(cpuri)
        {

            auto cp_host_r = cpuri.get_cpHost();

            // using host address automatically disables cert validation
            if (useHostAddress)
                validateServer = false;

            if (!useHostAddress && !cp_host_r.has_error())
            {
                // try hostname
                auto cp_host_v = cp_host_r.value();
                addr_string = cp_host_v.first + ":"s + std::to_string(cp_host_v.second);
            }
            else
            {
                // try address
                auto cp_addr_r = cpuri.get_cpAddr();
                if (cp_addr_r.has_error())
                    throw E2SARException("Unable to initialize LBManager due to missing CP address in URI");
                auto cp_addr_v = cp_addr_r.value();
                if (cp_addr_v.first.is_v4())
                    addr_string = "ipv4:///" + cp_addr_v.first.to_string() + ":" + std::to_string(cp_addr_v.second);
                else
                    addr_string = "ipv6:///[" + cp_addr_v.first.to_string() + "]:" + std::to_string(cp_addr_v.second);
            }

            if (cpuri.get_useTls())
            {
                grpc::experimental::TlsChannelCredentialsOptions topts;
                if (!validateServer)
                {
                    // disable most of server certificate validation
                    std::shared_ptr<grpc::experimental::NoOpCertificateVerifier> verifier = std::make_shared<grpc::experimental::NoOpCertificateVerifier>();
                    topts.set_verify_server_certs(false);
                    topts.set_check_call_host(false);
                    topts.set_certificate_verifier(verifier);
                    _channel = grpc::CreateChannel(addr_string, grpc::experimental::TlsCredentials(topts));
                }
                else
                {
                    // use provided SSL options
                    _channel = grpc::CreateChannel(addr_string, grpc::SslCredentials(opts));
                }
            }
            else
            {
                _channel = grpc::CreateChannel(addr_string, grpc::InsecureChannelCredentials());
            }
            _stub = LoadBalancer::NewStub(_channel);
        }

        /**
         * Reserve a new load balancer with this name until specified time
         *
         * @param lb_name LB name internal to you
         * @param until time until it's needed as google protobuf timestamp pointer.
         * @param senders list of sender IP addresses
         * @param ip_family whether the load balancer should be IPv4 only, IPv6 only or dual stack (default)
         *
         * @return - FPGA LB ID, for use in correlating logs/metrics
         */
        result<u_int32_t> reserveLB(const std::string &lb_name,
                              const TimeUntil &until,
                              const std::vector<std::string> &senders,
                              int ip_family=loadbalancer::IpFamily::DUAL_STACK) noexcept;

        /**
         * Reserve a new load balancer with this name until specified time. It sets the intstance
         * token on the internal URI object.
         *
         * @param lb_name LB name internal to you
         * @param duration for how long it is needed as boost::posix_time::time_duration. You can use
         * boost::posix_time::duration_from_string from string like "23:59:59.000"s
         * @param senders list of sender IP addresses
         * @param ip_family whether the load balancer should be IPv4 only, IPv6 only or dual stack (default)
         *
         * @return - FPGA LB ID, for use in correlating logs/metrics
         */
        result<u_int32_t> reserveLB(const std::string &lb_name,
                              const boost::posix_time::time_duration &duration,
                              const std::vector<std::string> &senders,
                              int ip_family=loadbalancer::IpFamily::DUAL_STACK) noexcept;

        /**
         * Reserve a new load balancer with this name of duration in seconds
         *
         * @param lb_name LB name internal to you
         * @param durationSeconds for how long it is needed in seconds
         * @param senders list of sender IP addresses
         * @param ip_family whether the load balancer should be IPv4 only, IPv6 only or dual stack (default)
         *
         * @return - 0 on success or error code with message on failure
         */
        result<u_int32_t> reserveLB(const std::string &lb_name,
                              const double &durationSeconds,
                              const std::vector<std::string> &senders,
                              int ip_family=loadbalancer::IpFamily::DUAL_STACK) noexcept;

        /**
         * Get load balancer info - it updates the info inside the EjfatURI object just like reserveLB.
         * Uses admin token of the internal URI object. Note that unlike reserve it does NOT set
         * the instance token - it is not available.
         *
         * @param lbid - externally provided lb id, in this case the URI only needs to contain
         * the cp address and admin token and it will be updated to contain dataplane and sync addresses.
         * @return - 0 on success or error code with message on failure
         */
        result<int> getLB(const std::string &lbid) noexcept;
        /**
         * Get load balancer info using lb id in the URI object
         * @return - 0 on success or error code with message on failure
         */
        result<int> getLB() noexcept;

        /**
         * Get load balancer status including list of workers, sender IP addresses.
         *
         * @param lbid - externally provided lbid, in this case the URI only needs to contain
         * cp address and admin token
         * @return - loadbalancer::LoadBalancerStatusReply protobuf object with getters for fields
         * like timestamp, currentepoch, currentpredictedeventnumber and iterators over senders
         * and workers. For each worker you get name, fill percent, controlsignal, slotsassigned and
         * timestamp last updated.
         */
        result<std::unique_ptr<LoadBalancerStatusReply>> getLBStatus(const std::string &lbid) noexcept;

        /**
         * Get load balancer status including list of workers, sender IP addresses etc
         * using lb id in the URI object
         *
         * @return - loadbalancer::LoadBalancerStatusReply protobuf object with getters for fields
         * like timestamp, currentepoch, currentpredictedeventnumber and iterators over senders
         * and workers. For each worker you get name, fill percent, controlsignal, slotsassigned and
         * timestamp last updated.
         */
        result<std::unique_ptr<LoadBalancerStatusReply>> getLBStatus() noexcept;

        /**
         * Get an 'overview' of reserved load balancer instances
         *
         * @return - loadbalancer::OverviewReply protobuf object which itself is an array of 
         * ReserveLoadBalancerReply and LoadBalancerStatusReply for each instance as well as the 
         * name given to each instance in reserveLB.
         */
        result<std::unique_ptr<OverviewReply>> overview() noexcept;

        /**
         * Add 'safe' sender addresses to CP to allow these sender to send data to the LB
         * 
         * @param sender_list - list of sender addresses
         */
        result<int> addSenders(const std::vector<std::string>& senders) noexcept;

        /**
         * Remove 'safe' sender addresses from CP to disallow these senders to send data
         * to the LB
         */
        result<int> removeSenders(const std::vector<std::string>& senders) noexcept;

        /**
         * Determine the caller's outgoing interface towards LB dataplane and register it as a sender
         * @param - use IPv6 dataplane (default false)
         */
        result<int> addSenderSelf(bool v6=false) noexcept;

        /**
         * Determine the caller's outgoing interface towards LB dataplane and unregister as a sender
         * @param - use IPv6 dataplane (default false)
         */
        result<int> removeSenderSelf(bool v6=false) noexcept;

        /**
         * Create a new delegated token with specific permissions
         *
         * @param name - Human-readable token name
         * @param permissions - Vector of TokenPermission specifying access rights
         *
         * @return The created token string
         */
        result<std::string> createToken(
            const std::string &name,
            const std::vector<TokenPermission> &permissions) noexcept;

        /**
         * List all permissions for a specific token
         *
         * @param target - Token selector (by ID or token string)
         *
         * @return TokenDetails with permissions and metadata
         */
        result<TokenDetails> listTokenPermissions(const TokenSelector &target) noexcept;

        /**
         * List all child tokens created by a parent token
         *
         * @param target - Token selector for parent token
         *
         * @return Vector of TokenDetails for all children
         */
        result<std::vector<TokenDetails>> listChildTokens(const TokenSelector &target) noexcept;

        /**
         * Revoke a token and all its children
         *
         * @param target - Token selector to revoke
         *
         * @return 0 on success or error code
         */
        result<int> revokeToken(const TokenSelector &target) noexcept;

        /**
         * Retrieve timeseries data for a specific metric path
         *
         * @param path - Timeseries path selector (e.g., "/lb/1/<asterisk>", "/lb/1/session/2/totalEventsReassembled")
         * @param since - Timestamp to retrieve data from
         *
         * @return TimeseriesResult containing the "since" timestamp and multiple vectors of samples (float or integer)
         */
        result<TimeseriesResult> timeseries(const std::string &path, const Timestamp &since) noexcept;

        /** Helper function copies worker records into a vector
         * It takes a unique_ptr from getLBStatus() call and helps parse it. Relies on move semantics.
         *
         * @param rep - the return of the getLBStatus() call
         *
         * @return - a vector of WorkerStatus objects with fields like name, fillpercent, controlsignal,
         * slotsassigned and a lastupdated timestamp
         */
        static inline std::vector<WorkerStatus> get_WorkerStatusVector(std::unique_ptr<LoadBalancerStatusReply> &rep) noexcept
        {
            std::vector<WorkerStatus> ret(rep->workers_size());

            size_t j{0};
            for (auto i = rep->workers().begin(); i != rep->workers().end(); ++i, j++)
            {
                ret[j].CopyFrom(*i);
            }
            return ret;
        }

        /** Helper function copies worker records into a vector
         * It takes a unique_ptr from getLBStatus() call and helps parse it. Relies on move semantics.
         *
         * @param rep - the return of the getLBStatus() call
         *
         * @return - a vector of WorkerStatus objects with fields like name, fillpercent, controlsignal,
         * slotsassigned and a lastupdated timestamp
         */
        static inline std::vector<WorkerStatus> get_WorkerStatusVector(const LoadBalancerStatusReply &rep) noexcept
        {
            std::vector<WorkerStatus> ret(rep.workers_size());

            size_t j{0};
            for (auto i = rep.workers().begin(); i != rep.workers().end(); ++i, j++)
            {
                ret[j].CopyFrom(*i);
            }
            return ret;
        }

        /** Helper function copies sender addresses into a vector. Relies on move semantics.
         *
         * @param rep - the referenced return of getLBStatus() call
         *
         * @return - a vector of strings with known sender addresses communicated in the reserve call
         */
        static inline std::vector<std::string> get_SenderAddressVector(std::unique_ptr<LoadBalancerStatusReply> &rep) noexcept
        {
            std::vector<std::string> ret(rep->senderaddresses_size());

            size_t j{0};
            for (auto i = rep->senderaddresses().begin(); i != rep->senderaddresses().end(); ++i, j++)
            {
                ret[j] = *i;
            }
            return ret;
        }

        /** Helper function copies sender addresses into a vector. Relies on move semantics.
         *
         * @param rep - the referenced return of getLBStatus() call
         *
         * @return - a vector of strings with known sender addresses communicated in the reserve call
         */
        static inline std::vector<std::string> get_SenderAddressVector(const LoadBalancerStatusReply &rep) noexcept
        {
            std::vector<std::string> ret(rep.senderaddresses_size());

            size_t j{0};
            for (auto i = rep.senderaddresses().begin(); i != rep.senderaddresses().end(); ++i, j++)
            {
                ret[j] = *i;
            }
            return ret;
        }

        /** Helper function copies LoadBalancerStatusReply protobuf into a simpler struct */
        static inline const std::unique_ptr<LBStatus> asLBStatus(std::unique_ptr<LoadBalancerStatusReply> &rep) noexcept
        {
            std::vector<std::string> addresses{get_SenderAddressVector(rep)};
            std::vector<WorkerStatus> workers{get_WorkerStatusVector(rep)};
            std::unique_ptr<LBStatus> pret = std::make_unique<LBStatus>(rep->timestamp(), rep->currentepoch(), rep->currentpredictedeventnumber(),
            workers, addresses, rep->expiresat());
            return pret;
        }

        /** Helper function copies LoadBalancerStatusReply protobuf into a simpler struct */
        static inline const LBStatus asLBStatus(const LoadBalancerStatusReply &rep) noexcept
        {
            std::vector<std::string> addresses{get_SenderAddressVector(rep)};
            std::vector<WorkerStatus> workers{get_WorkerStatusVector(rep)};
            return LBStatus(rep.timestamp(), rep.currentepoch(), rep.currentpredictedeventnumber(),
                workers, addresses, rep.expiresat());
        }

        /** Helper function copies OverviewReply protobuf into a simpler struct */
        static inline const OverviewMessage asOverviewMessage(std::unique_ptr<OverviewReply> &rep) noexcept
        {
            size_t j{0};
            OverviewMessage om(rep.get()->loadbalancers_size());
            for (auto i = rep->loadbalancers().begin(); i != rep->loadbalancers().end(); ++i, j++)
            {
                om[j].name = i->name();
                om[j].lbid = i->reservation().lbid();
                om[j].syncIPv4AndPort.first = ip::make_address(i->reservation().syncipv4address());
                om[j].syncIPv4AndPort.second = i->reservation().syncudpport();
                om[j].syncIPv6AndPort.first = ip::make_address(i->reservation().syncipv6address());
                om[j].syncIPv6AndPort.second = i->reservation().syncudpport();
                om[j].dataIPv4 = ip::make_address(i->reservation().dataipv4address());
                om[j].dataIPv6 = ip::make_address(i->reservation().dataipv6address());
                om[j].fpgaLBId = i->reservation().fpgalbid();
                om[j].dataMinPort = i->reservation().dataminport();
                om[j].dataMaxPort = i->reservation().datamaxport();
                om[j].status = asLBStatus(i->status());
            }
            return om;
        }

        /** Helper function copies OverviewReply protobuf into a simpler struct */
        static inline const OverviewMessage asOverviewMessage(const OverviewReply &rep) noexcept
        {
            size_t j{0};
            OverviewMessage om(rep.loadbalancers_size());
            for (auto i = rep.loadbalancers().begin(); i != rep.loadbalancers().end(); ++i, j++)
            {
                om[j].name = i->name();
                om[j].lbid = i->reservation().lbid();
                om[j].syncIPv4AndPort.first = ip::make_address(i->reservation().syncipv4address());
                om[j].syncIPv4AndPort.second = i->reservation().syncudpport();
                om[j].syncIPv6AndPort.first = ip::make_address(i->reservation().syncipv6address());
                om[j].syncIPv6AndPort.second = i->reservation().syncudpport();
                om[j].dataIPv4 = ip::make_address(i->reservation().dataipv4address());
                om[j].dataIPv6 = ip::make_address(i->reservation().dataipv6address());
                om[j].fpgaLBId = i->reservation().fpgalbid();
                om[j].dataMinPort = i->reservation().dataminport();
                om[j].dataMaxPort = i->reservation().datamaxport();
                om[j].status = asLBStatus(i->status());
            }
            return om;
        }

        /**
         * Free previously reserved load balancer. Uses admin token.
         * @param - externally provided lbid, in this case the URI only needs to contain
         * cp address and admin token
         * @return - 0 on success or error code with message on failure
         */
        result<int> freeLB(const std::string &lbid) noexcept;
        /**
         * Free previously reserved load balancer. Uses admin token and uses LB ID obtained
         * from reserve call on the same LBManager object.
         * @return - 0 on success or error code with message on failure
         */
        result<int> freeLB() noexcept;

        /**
         * Register a workernode/backend with an allocated loadbalancer. Note that this call uses
         * instance token. It sets session token and session id on the internal
         * URI object. Note that a new worker must send state immediately (within 10s)
         * or be automatically deregistered.
         *
         * @param node_name - name of the node (can be FQDN)
         * @param node_ip_port - a pair of ip::address and u_int16_t starting UDP port on which it listens
         * @param weight - weight given to this node in terms of processing power
         * @param source_count - how many sources we can listen to (gets converted to port range [0,14])
         * @param min_factor - multiplied with the number of slots that would be assigned evenly to determine min number of slots
         * for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots
         * @param max_factor - multiplied with the number of slots that would be assigned evenly to determine max number of slots
         * for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum
         * @param keep_lb_header - don't strip the LB header when processing for this worker (to support hierarchical LBs; defaults to false)
         * @return - 0 on success or an error condition
         */
        result<int> registerWorker(const std::string &node_name, std::pair<ip::address, u_int16_t> node_ip_port, float weight, u_int16_t source_count, 
            float min_factor, float max_factor, bool keep_lb_header=false) noexcept;

        /**
         * Register the calling worker workernode/backend with an allocated loadbalancer. 
         * The node tries to determine its outgoing IP address towards LB dataplane.
         * Note that this call uses instance token. It sets session token and session id on the internal
         * URI object. Note that a new worker must send state immediately (within 10s)
         * or be automatically deregistered.
         *
         * @param node_name - name of the node (can be FQDN)
         * @param node_port - a u_int16_t starting UDP port on which it listens
         * @param weight - weight given to this node in terms of processing power
         * @param source_count - how many sources we can listen to (gets converted to port range [0,14])
         * @param min_factor - multiplied with the number of slots that would be assigned evenly to determine min number of slots
         * for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots
         * @param max_factor - multiplied with the number of slots that would be assigned evenly to determine max number of slots
         * for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum
         * @param v6 - use IPv6 dataplane (defaults to false)
         * @param keep_lb_header - don't strip the LB header when processing for this worker (to support hierarchical LBs; defaults to false)
         * @return - 0 on success or an error condition
         */
        result<int> registerWorkerSelf(const std::string &node_name, u_int16_t node_port, float weight, u_int16_t source_count, float min_factor, float max_factor, 
                bool v6=false, bool keep_lb_header=false) noexcept;

        /**
         * Deregister worker using session ID and session token from the register call
         *
         * @return - 0 on success or an error condition
         */
        result<int> deregisterWorker() noexcept;

        /**
         * Send worker state update using session ID and session token from register call. Automatically
         * uses localtime to set the timestamp. Workers are expected to send state every 100ms or so. Allows
         * to set worker stats.
         *
         * @param fill_percent - [0:1] percentage filled of the queue
         * @param control_signal - change to data rate
         * @param is_ready - if true, worker ready to accept more data, else not ready
         * @param stats - a struct of additional optional worker stats (WorkerStats)
         */
        result<int> sendState(float fill_percent, float control_signal, bool is_ready,
                       const WorkerStats &stats) noexcept;

        /**
         * Send worker state update using session ID and session token from register call. Allows to explicitly
         * set the timestamp and set worker stats
         *
         * @param fill_percent - [0:1] percentage filled of the queue
         * @param control_signal - change to data rate
         * @param is_ready - if true, worker ready to accept more data, else not ready
         * @param ts - google::protobuf::Timestamp timestamp for this message (if you want to explicitly not
         * use localtime)
         * @param stats - a struct of additional optional worker stats (WorkerStats)
         */
        result<int> sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts,
                       const WorkerStats &stats) noexcept;

        /**
         * Send worker state update using session ID and session token from register call. Automatically
         * uses localtime to set the timestamp. Workers are expected to send state every 100ms or so.
         *
         * @param fill_percent - [0:1] percentage filled of the queue
         * @param control_signal - change to data rate
         * @param is_ready - if true, worker ready to accept more data, else not ready
         */
        result<int> sendState(float fill_percent, float control_signal, bool is_ready) noexcept;

        /**
         * Send worker state update using session ID and session token from register call. Allows to explicitly
         * set the timestamp.
         *
         * @param fill_percent - [0:1] percentage filled of the queue
         * @param control_signal - change to data rate
         * @param is_ready - if true, worker ready to accept more data, else not ready
         * @param ts - google::protobuf::Timestamp timestamp for this message (if you want to explicitly not
         * use localtime)
         */
        result<int> sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts) noexcept;

        /**
         * Get the version of the load balancer (the commit string)
         *
         * @return the result with commit string, build tag and compatTag in
         */
        result<boost::tuple<std::string, std::string, std::string>> version() noexcept;

        /**
         * Get the internal URI object.
         */
        inline const EjfatURI &get_URI() const noexcept { return _cpuri; }

        /**
         * Generate gRPC-compliant custom SSL Options object with the following parameters,
         * where any parameter can be empty. Uses std::move to avoid copies.
         * @param pem_root_certs    The buffer containing the PEM encoding of the server root certificates.
         * @param pem_private_key   The buffer containing the PEM encoding of the client's private key.
         * @param pem_cert_chain    The buffer containing the PEM encoding of the client's certificate chain.
         *
         * @return outcome for grpc::SslCredentialsOptions object with parameters filled in
         */
        static inline result<grpc::SslCredentialsOptions> makeSslOptions(const std::string &pem_root_certs,
                                                                         const std::string &pem_private_key,
                                                                         const std::string &pem_cert_chain) noexcept
        {
            return grpc::SslCredentialsOptions{std::move(pem_root_certs),
                                               std::move(pem_private_key),
                                               std::move(pem_cert_chain)};
        }

        /**
         * Generate gRPC-compliant custom SSL Options object with the following parameters,
         * where any parameter can be empty
         * @param pem_root_certs  - The file name containing the PEM encoding of the server root certificate.
         * @param pem_private_key - The file name containing the PEM encoding of the client's private key.
         * @param pem_cert_chain  - The file name containing the PEM encoding of the client's certificate chain.
         *
         * @return outcome for grpc::SslCredentialsOptions object with parameters filled in
         */
        static result<grpc::SslCredentialsOptions> makeSslOptionsFromFiles(
            std::string_view pem_root_certs,
            std::string_view pem_private_key,
            std::string_view pem_cert_chain) noexcept;

        /**
         * Generate gRPC-compliant custom SSL Options object with just the server root cert 
         * @param pem_root_certs - The file name containing the PEM encoding of the server root certificate.
         */
        static result<grpc::SslCredentialsOptions> makeSslOptionsFromFiles(
            std::string_view pem_root_certs) noexcept;

        /**
         * Return the address string used by gRPC to connect to control plane. Can be
         * in the format of hostname:port or ipv4:///W.X.Y.Z:port or ipv6:///[XXXX::XX:XXXX]:port
         * 
         * @return the string containing the address
         */
        inline std::string get_AddrString() {
            return addr_string;
        }

    };

    /**
     * Method to map max # of data sources a backend will see to
     * the corressponding PortRange (enum) value in loadbalancer.proto.
     *
     * @param sourceCount max # of data sources backend will see.
     * @return corressponding PortRange.
     */
    static inline int get_PortRange(int source_count) noexcept
    {
        // Based on the proto file enum for the load balancer, seen below,
        // map the max # of sources a backend will see to the PortRange value.
        // This is necessay to provide the control plane when registering.

        // Handle edge cases
        if (source_count < 2)
        {
            return 0;
        }
        else if (source_count > 16384)
        {
            return 14;
        }

        int maxCount = 2;
        int iteration = 1;

        while (source_count > maxCount)
        {
            iteration++;
            maxCount <<= 1;
        }

        return iteration;
    }
}
#endif