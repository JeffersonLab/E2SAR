#ifndef E2SARCPHPP
#define E2SARCPHPP
#include <vector>
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
         * Reserve a new load balancer with this name until specified time
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
         * Uses admin token.
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
        // get load balancer status
        result<int> getLBStatus();

        /**
         * Free previously reserved load balancer. Uses admin token.
         * @return - 0 on success or error code with message on failure
         */
        result<int> freeLB();
        // register worker
        int registerWorker();
        // send worker queue state
        int sendState();
        // deregister worker
        int deregisterWorker();
        // get updated statistics
        int probeStats();

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
    };

    /*
    Sync packet sent by segmenter to LB periodically.
    */
    struct LBSyncPkt
    {
    };
}
#endif