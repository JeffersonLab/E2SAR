#ifndef E2SARCPHPP
#define E2SARCPHPP
#include <boost/asio.hpp>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "grpc/loadbalancer.grpc.pb.h"

#include "e2sarUtil.hpp"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using loadbalancer::LoadBalancer;
using loadbalancer::ReserveLoadBalancerRequest;
using loadbalancer::ReserveLoadBalancerReply;

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
    using TimeUntil = std::string;
    
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
             * @param tlsOn a boolean flag (default true)
             * @param opts a grpc::SslCredentialsOptions object (default object with no parameters)
            */
            LBManager(const EjfatURI &cpuri, 
                bool tlsOn = true, 
                grpc::SslCredentialsOptions opts = grpc::SslCredentialsOptions()): _cpuri(cpuri), _state_reserved(false) {

                auto cp_addr_r = cpuri.get_cpAddr();
                if (cp_addr_r.has_error())
                    throw E2SARException("Unable to initialize LBManager due to missing CP address in URI");
                auto cp_addr_v = cp_addr_r.value();
                auto addr_string{cp_addr_v.first.to_string() + ":" + std::to_string(cp_addr_v.second)};
                if (tlsOn) {
                    _channel = grpc::CreateChannel(addr_string, grpc::SslCredentials(opts));
                } else {
                    _channel = grpc::CreateChannel(addr_string, grpc::InsecureChannelCredentials());
                }
                _stub = LoadBalancer::NewStub(_channel);
            }

            /**
             * Is the load balancer reserved (set by reserveLB, unset when newly created or after freeLB)
            */
            inline bool isReserved() const {
                return _state_reserved;
            }
            /**
             * Reserve a new load balancer with this name until specified time
             * 
             * @param lb_name LB name internal to you
             * @param until time until it's needed
             * @param bearerToken use bearer token rather than putting admin token into the body (default true)
             * 
             * @return outcome which is either an error or a 0(==E2SARErrorc::NoError); 
             * RPCError - if unable to connect to the UDPLBd server; 
             * ParameterNotAvailable - if admin token or other parameters not available
            */
            outcome::result<int> reserveLB(const std::string &lb_name, const TimeUntil &until, bool bearerToken = true);
            // get load balancer info (same returns as reserverLB)
            int getLB();
            // get load balancer status
            int getLBStatus();
            // Free a Load Balancer
            int freeLB();
            // register worker
            int registerWorker();
            // send worker queue state
            int sendState();
            // deregister worker
            int deregisterWorker();
            // get updated statistics
            int probeStats();

            /**
             * Generate gRPC-compliant custom SSL Options object with the following parameters, 
             * where any parameter can be empty. Uses std::move to avoid copies.
             * @param pem_root_certs    The buffer containing the PEM encoding of the server root certificates.
             * @param pem_private_key   The buffer containing the PEM encoding of the client's private key.
             * @param pem_cert_chain    The buffer containing the PEM encoding of the client's certificate chain.
             * 
             * @return outcome for grpc::SslCredentialsOptions object with parameters filled in
            */
            static inline outcome::result<grpc::SslCredentialsOptions> makeSslOptions(const std::string &pem_root_certs,
                                            const std::string &pem_private_key,
                                            const std::string &pem_cert_chain) {
                return grpc::SslCredentialsOptions{std::move(pem_root_certs), std::move(pem_private_key), std::move(pem_cert_chain)};
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
            static outcome::result<grpc::SslCredentialsOptions> makeSslOptionsFromFiles(
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