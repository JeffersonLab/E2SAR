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
             * Initialize manager without SSL
            */
            LBManager(const EjfatURI &cpuri): _cpuri(cpuri), _state_reserved(false) {
                auto cp_addr_r = cpuri.get_cpAddr();
                if (cp_addr_r.has_error())
                    throw E2SARException("Unable to initialize LBManager due to missing CP address in URI");
                auto cp_addr_v = cp_addr_r.value();
                std::string addr_string{cp_addr_v.first.to_string() + ":" + std::to_string(cp_addr_v.second)};
                _channel = grpc::CreateChannel(addr_string, grpc::InsecureChannelCredentials());
                _stub = LoadBalancer::NewStub(_channel);
            }

            /**
             * Initialize manager with SSL configuration (obtained via makeSslOptions().value() helper method)
            */
            LBManager(const EjfatURI &cpuri, grpc::SslCredentialsOptions opts): _cpuri(cpuri), _state_reserved(false) {
                auto cp_addr_r = cpuri.get_cpAddr();
                if (cp_addr_r.has_error())
                    throw E2SARException("Unable to initialize LBManager due to missing CP address in URI");
                auto cp_addr_v = cp_addr_r.value();
                std::string addr_string{cp_addr_v.first.to_string() + ":" + std::to_string(cp_addr_v.second)};
                _channel = grpc::CreateChannel(addr_string, grpc::SslCredentials(opts));
                _stub = LoadBalancer::NewStub(_channel);
            }

            /**
             * Is the load balancer reserved (set by reserveLB, unset when newly created or after freeLB)
            */
            inline bool isReserved() const {
                return _state_reserved;
            }
            // Reserve a new Load Balancer
            outcome::result<int> reserveLB(const std::string &lb_name, const TimeUntil &until);
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
             * Generate gRPC-compliant SSL Options object with the following parameters, 
             * where any parameter can be empty. Uses std::move to avoid copies.
             * @param pem_root_certs    The buffer containing the PEM encoding of the server root certificates.
             * @param pem_private_key   The buffer containing the PEM encoding of the client's private key.
             * @param pem_cert_chain    The buffer containing the PEM encoding of the client's certificate chain.
             * 
             * @return grpc::SslCredentialsOptions object with parameters filled in
            */
            static inline outcome::result<grpc::SslCredentialsOptions> makeSslOptions(const std::string &pem_root_certs,
                                            const std::string &pem_private_key,
                                            const std::string &pem_cert_chain) {
                return grpc::SslCredentialsOptions{std::move(pem_root_certs), std::move(pem_private_key), std::move(pem_cert_chain)};
            }

            /**
             * Generate gRPC-compliant SSL Options object with the following parameters, 
             * where any parameter can be empty
             * @param pem_root_certs    The file name containing the PEM encoding of the server root certificates.
             * @param pem_private_key   The file name containing the PEM encoding of the client's private key.
             * @param pem_cert_chain    The file name containing the PEM encoding of the client's certificate chain.
             * 
             * @return grpc::SslCredentialsOptions object with parameters filled in
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