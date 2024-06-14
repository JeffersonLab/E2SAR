#ifndef E2SARCPHPP
#define E2SARCPHPP
#include <vector>
#include <memory>
#include <boost/asio.hpp>

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

using google::protobuf::Timestamp;

// default reservation duration for a load balancer in hours
#define DEFAULT_LB_RESERVE_DURATION 24

// change to '1' to test older versions of UDPLBd where token
// was sent as a parameter in the body. The new way is to send
// it in the authorization header as bearer token
#ifndef TOKEN_IN_BODY
#define TOKEN_IN_BODY 0
#endif

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

    class LBManager
    {
    private:
        EjfatURI _cpuri;
        bool _state_reserved;
        std::unique_ptr<LoadBalancer::Stub> _stub;
        std::shared_ptr<grpc::Channel> _channel;

    protected:
    public:
        /**
         * Initialize manager. Default is with TLS/SSL and default client options. To enable
         * custom SSL configuration with custom root certs, private key (for authN) and cert
         * use makeSslOptions[FromFiles]() methods and pass the output to this constructor.
         * @param cpuri an EjfatURI object parsed from configuration data
         * @param opts a grpc::SslCredentialsOptions object (default object with no parameters). The
         * use of SSL/TLS is governed by the URI scheme ('ejfat' vs 'ejfats')
         */
        LBManager(const EjfatURI &cpuri,
                  grpc::SslCredentialsOptions opts = grpc::SslCredentialsOptions()) : _cpuri(cpuri), _state_reserved(false)
        {

            auto cp_addr_r = cpuri.get_cpAddr();
            if (cp_addr_r.has_error())
                throw E2SARException("Unable to initialize LBManager due to missing CP address in URI");
            auto cp_addr_v = cp_addr_r.value();
            auto addr_string{cp_addr_v.first.to_string() + ":" + std::to_string(cp_addr_v.second)};
            if (cpuri.get_useTls())
            {
                _channel = grpc::CreateChannel(addr_string, grpc::SslCredentials(opts));
            }
            else
            {
                _channel = grpc::CreateChannel(addr_string, grpc::InsecureChannelCredentials());
            }
            _stub = LoadBalancer::NewStub(_channel);
        }

        /**
         * Is the load balancer reserved (set by reserveLB, unset when newly created or after freeLB)
         */
        inline bool isReserved() const
        {
            return _state_reserved;
        }
        /**
         * Reserve a new load balancer with this name until specified time
         *
         * @param lb_name LB name internal to you
         * @param until time until it's needed as google protobuf timestamp pointer.
         *
         * @return - 0 on success or error code with message on failure
         */
        result<int> reserveLB(const std::string &lb_name,
                              const TimeUntil &until,
                              const std::vector<std::string> &senders);

        /**
         * Reserve a new load balancer with this name until specified time. It sets the intstance
         * token on the internal URI object.
         *
         * @param lb_name LB name internal to you
         * @param duration for how long it is needed as boost::posix_time::time_duration. You can use
         * boost::posix_time::duration_from_string from string like "23:59:59.000"s
         * @param senders list of sender IP addresses
         *
         * @return - 0 on success or error code with message on failure
         */
        result<int> reserveLB(const std::string &lb_name,
                              const boost::posix_time::time_duration &duration,
                              const std::vector<std::string> &senders);
        /**
         * Get load balancer info - it updates the info inside the EjfatURI object just like reserveLB.
         * Uses admin token and sets instance token on the internal URI object.
         * 
         * @param lbid - externally provided lb id, in this case the URI only needs to contain
         * the cp address and admin token and it will be updated to contain dataplane and sync addresses.
         * @return - 0 on success or error code with message on failure
         */
        result<int> getLB(const std::string &lbid);
        /**
         * Get load balancer info using lb id in the URI object
         * @return - 0 on success or error code with message on failure
         */
        result<int> getLB();

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
        result<std::unique_ptr<LoadBalancerStatusReply>> getLBStatus(const std::string &lbid);

        /**
         * Get load balancer status including list of workers, sender IP addresses etc
         * using lb id in the URI object
         * 
         * @return - loadbalancer::LoadBalancerStatusReply protobuf object with getters for fields
         * like timestamp, currentepoch, currentpredictedeventnumber and iterators over senders
         * and workers. For each worker you get name, fill percent, controlsignal, slotsassigned and
         * timestamp last updated.
         */
        result<std::unique_ptr<LoadBalancerStatusReply>> getLBStatus();

        /** Helper function copies worker records into a vector
         * It takes a unique_ptr from getLBStatus() call and helps parse it.
         * 
         * @param rep - the return of the getLBStatus() call
         *
         * @return - a vector of WorkerStatus objects with fields like name, fillpercent, controlsignal,
         * slotsassigned and a lastupdated timestamp
         */
        static inline const std::vector<WorkerStatus> get_WorkerStatusVector(std::unique_ptr<LoadBalancerStatusReply> &rep)
        {
            std::vector<WorkerStatus> ret(rep->workers_size());

            for (auto i = rep->workers().begin(); i != rep->workers().end(); ++i)
            {
                ret.push_back(*i);
            }
            return ret;
        }

        /** Helper function copies sender addresses into a vector
         * 
         * @param rep - the return of getLBStatus() call
         *
         * @return - a vector of strings with known sender addresses communicated in the reserve call
         */
        static inline const std::vector<std::string> get_SenderAddressVector(std::unique_ptr<LoadBalancerStatusReply> &rep)
        {
            std::vector<std::string> ret(rep->senderaddresses_size());

            for (auto i = rep->senderaddresses().begin(); i != rep->senderaddresses().end(); ++i)
            {
                ret.push_back(*i);
            }
            return ret;
        }

        /**
         * Free previously reserved load balancer. Uses admin token.
         * @param - externally provided lbid, in this case the URI only needs to contain
         * cp address and admin token
         * @return - 0 on success or error code with message on failure
         */
        result<int> freeLB(const std::string &lbid);
        /**
         * Free previously reserved load balancer. Uses admin token.
         * @return - 0 on success or error code with message on failure
         */
        result<int> freeLB();

        /**
         * Register a workernode/backend with an allocated loadbalancer. Note that this call uses
         * instance token. It sets session token and session id on the internal
         * URI object.
         * @param node_name - name of the node (can be FQDN)
         * @param node_ip_port - a pair of ip::address and u_int16_t starting UDP port on which it listens
         * @param weight - weight given to this node in terms of processing power
         * @param source_count - how many sources we can listen to (gets converted to port range [0,14])
         *
         * @return - 0 on success or an error condition
         */
        result<int> registerWorker(const std::string &node_name, std::pair<ip::address, u_int16_t> node_ip_port, float weight, u_int16_t source_count);

        /**
         * Deregister worker using session ID and session token from the register call
         * 
         * @return - 0 on success or an error condition
         */
        result<int> deregisterWorker();

        /**
         * Send worker state update using session ID and session token from register call. Automatically
         * uses localtime to set the timestamp.
         * @param fill_percent - [0:1] percentage filled of the queue
         * @param control_signal - change to data rate
         * @param is_ready - if true, worker ready to accept more data, else not ready
         */
        result<int> sendState(float fill_percent, float control_signal, bool is_ready);

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
        result<int> sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts);

        /**
         * Get the version of the load balancer (the commit string)
         * 
         * @return the commit string
         */
        result<std::string> version();

        // get updated statistics
        result<int> probeStats();

        /**
         * Get the internal URI object.
         */
        inline const EjfatURI &get_URI() const { return _cpuri; }

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
                                                                         const std::string &pem_cert_chain)
        {
            return grpc::SslCredentialsOptions{std::move(pem_root_certs),
                                               std::move(pem_private_key),
                                               std::move(pem_cert_chain)};
        }

        /**
         * Generate gRPC-compliant custom SSL Options object with the following parameters,
         * where any parameter can be empty
         * @param pem_root_certs    The file name containing the PEM encoding of the server root certificates.
         * @param pem_private_key   The file name containing the PEM encoding of the client's private key.
         * @param pem_cert_chain    The file name containing the PEM encoding of the client's certificate chain.
         *
         * @return outcome for grpc::SslCredentialsOptions object with parameters filled in
         */
        static result<grpc::SslCredentialsOptions> makeSslOptionsFromFiles(
            std::string_view pem_root_certs,
            std::string_view pem_private_key,
            std::string_view pem_cert_chain);

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
                maxCount >>= 1;
            }

            return iteration;
        }
    };

    /*
    Sync packet sent by segmenter to LB periodically.
    */
    struct LBSyncPkt
    {
    };
}
#endif