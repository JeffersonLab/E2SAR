#ifndef E2SARUTILHPP
#define E2SARUTILHPP

#include <boost/asio.hpp>

using namespace boost::asio;

/***
 * Supporting classes for E2SAR
*/
namespace e2sar
{

    class ErrorCode {
        private:
            int error_code;
            std::string error_msg;
        public:
            enum ECode {
                NoError = 0,
                CaughtException = 1
            };
            ErrorCode(int c, const std::string &m): error_code{c}, error_msg{m} {}
            void setMsg(const std::string_view &m) {
                error_msg.
            }
    };

    ErrorCode NO_ERROR{ErrorCode::ECode::NoError, ""};
    ErrorCode UNEXPECTED_EXCEPTION{ErrorCode::ECode::CaughtException, 
                                   "Unexpected Exception Encountered."};

    /** Structure to hold info parsed from an ejfat URI (and a little extra). */
    class EjfatURI {

        private:
            std::string rawURI; 
            /** Is there a valid instance token? */
            bool haveInstanceToken;
            /** Is there a valid data addr & port? */
            bool haveData;
            /** Is there a valid sync addr & port? */
            bool haveSync;

            /** UDP port to send events (data) to. */
            uint16_t dataPort;
            /** UDP port for event sender to send sync messages to. */
            uint16_t syncPort;
            /** TCP port for grpc communications with CP. */
            uint16_t cpPort;

            /** String given by user, during registration, to label an LB instance. */
            std::string lbName;
            /** String identifier of an LB instance, set by the CP on an LB reservation. */
            std::string lbId;
            /** Admin token for the CP being used. */
            std::string adminToken;
            /** Instance token set by the CP on an LB reservation. */
            std::string instanceToken;
            /** address to send events (data) to. */
            ip::address dataAddr;
            /** address to send sync messages to. Not used, for future expansion. */
            ip::address syncAddr;

            /** IP address for grpc communication with CP. */
            ip::address cpAddr;
        
        public:
            /** base constructor */
            EjfatURI(const std::string_view& uri);
            /** rely on implicitly-declared copy constructor as needed */

            /** destructor */
            ~EjfatURI();

            /** implicit cast to string */
            operator std::string() const;

            /** factory method */
            static ErrorCode parseURI(const std::string_view &uri, 
                                      EjfatURI &uriInfo) {
                try {
                    uriInfo = EjfatURI(uri);
                    return NO_ERROR;
                } catch(...) {
                    return UNEXPECTED_EXCEPTION;
                }
            }

    };
};
#endif