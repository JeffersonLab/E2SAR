#ifndef E2SARUTILHPP
#define E2SARUTILHPP

#include <fstream>
#include <vector>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/shared_ptr.hpp>

#include "grpc/loadbalancer.grpc.pb.h"

#include "e2sarError.hpp"

using namespace boost::asio;
using namespace std::string_literals;
using namespace boost::log;

#define BOOST_LOG_FLUSH() sink->flush()
#define BOOST_MLL_START(PREF) { std::ostringstream PREF_ostr;
#define BOOST_MLL_LOG(PREF) PREF_ostr 
#define BOOST_MLL_STOP(PREF) BOOST_LOG_SEV(lg, trivial::info) << PREF_ostr.str(); } BOOST_LOG_FLUSH(); 

#define BOOST_LOG_INFO() BOOST_LOG_SEV(lg, trivial::info)
#define BOOST_LOG_WARN() BOOST_LOG_SEV(lg, trivial::warning)
#define BOOST_LOG_ERR() BOOST_LOG_SEV(lg, trivial::error)
typedef sinks::asynchronous_sink<sinks::text_ostream_backend> text_sink;
static boost::shared_ptr<text_sink> sink;
static sources::severity_logger_mt<trivial::severity_level> lg;

/***
 * Supporting classes for E2SAR
 */
namespace e2sar
{
    const u_int16_t DATAPLANE_PORT = 19522;

    /** Structure to hold info parsed from an ejfat URI (and a little extra). 
     * The URI is of the format:
     * ejfat[s]://[<token>@]<cp_host>:<cp_port>/lb/<lb_id>[?[data=<data_host>[:<data_port>]][&sync=<sync_host>:<sync_port>]][&sessionid=<string>].
     * More than one data= address can be specified (typically an IPv4 and IPv6). For data
     * the port is optional and defaults to 19522, however for testing/debugging can be overridden
    */
    class EjfatURI
    {
    public:
        enum class TokenType: u_int16_t {
            // contains overloaded names according to old and new token type hierarchy
            all=0, admin=1, load_balancer=admin, instance=2, reservation=instance, session=3, END
        };
        inline static constexpr size_t ttAsIdx(TokenType tt) 
        {
            return static_cast<size_t>(tt);
        }
        inline static const std::string toString(TokenType tt)
        {
            switch(tt) {
                case TokenType::all: return "ALL"s;
                case TokenType::admin: return "ADMIN/LOAD_BALANCER"s;
                case TokenType::instance: return "INSTANCE/RESERVATION"s;
                case TokenType::session: return "SESSION"s;
                default: "UNKNOWN"s;
            }
        }
        static const size_t tokenTypeCardinality = static_cast<size_t>(TokenType::END);


        enum class TokenPermission: u_int16_t {
            // 'register' is a keyword in C++, so adding '_' to names
            _read_only_, _register_, _reserve_, _update_, END
        };
        inline static const std::string toString(TokenPermission tt)
        {
            switch(tt) {
                case TokenPermission::_read_only_: return "READ"s;
                case TokenPermission::_register_: return "REGISTER"s;
                case TokenPermission::_reserve_: return "RESERVE"s;
                case TokenPermission::_update_: return "UPDATE"s;
                default: "UNKNOWN"s;
            }
        }
        static const size_t tokenPermissionCardinality = static_cast<size_t>(TokenPermission::END);

    private:
        std::string rawURI;
        /** Is there a valid data addr & port? */
        bool haveDatav4;
        bool haveDatav6;
        /** Is there a valid sync addr & port? */
        bool haveSync;
        /** Use TLS */
        bool useTls;
        /** Use IPv6 control plane IP address if available */
        bool preferV6;

        /** UDP port for event sender to send sync messages to. */
        u_int16_t syncPort;
        /** TCP port for grpc communications with CP. */
        u_int16_t cpPort;
        /** Dataplane port (normally defaults to DATAPLANE_PORT) */
        u_int16_t dataPort;

        /** String given by user, during registration, to label an LB instance. */
        std::string lbName;
        /** String identifier of an LB instance, set by the CP on an LB reservation. */
        std::string lbId;
        /** Instance token set by the CP on an LB reservation. */
        std::array<std::string, tokenTypeCardinality> tokensByType;
        /** Session ID issued via register call */
        std::string sessionId;

        /** data plane addresses - there can ever only be one v4 and one v6 */
        ip::address dataAddrv4;
        ip::address dataAddrv6;
        /** address to send sync messages to. Not used, for future expansion. (v4 or v6)*/
        ip::address syncAddr;
        /** IP address (and host if available) for grpc communication with CP. */
        ip::address cpAddr;
        std::string cpHost;

    public:
        /** base constructor, sets instance token from string
         * @param uri - the URI string
         * @param tt - convert to this token type (all, admin/load_balancer, instance/reservation, session)
         * @param preferV6 - when connecting to the control plane, prefer IPv6 address
         * if the name resolves to both (defaults to v4)
         */
        EjfatURI(const std::string &uri, TokenType tt=TokenType::admin, bool preferV6=false);

        /** destructor */
        ~EjfatURI() {}

        friend bool operator== (const EjfatURI &u1, const EjfatURI &u2);

        friend inline bool operator!= (const EjfatURI &u1, const EjfatURI &u2) 
        {
            return !(u1 == u2);
        }

        /** check if TLS should be used */
        inline bool get_useTls() const
        {
            return useTls;
        }

        /** set the token and token type */
        inline void set_Token(const std::string &t, TokenType tt)
        {
            tokensByType[ttAsIdx(tt)] = t;
        }

        /** set instance token based on gRPC return */
        inline void set_InstanceToken(const std::string &t)
        {
            tokensByType[ttAsIdx(TokenType::instance)] = t;
        }

        /** set session token based on gRPC return */
        inline void set_SessionToken(const std::string &t)
        {
            tokensByType[ttAsIdx(TokenType::session)] = t;
        }

        /** get instance token */
        inline const result<std::string> get_InstanceToken() const
        {
            auto idx = ttAsIdx(TokenType::instance);
            if (!tokensByType[idx].empty())
                return tokensByType[idx];
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available"s};
        }

        /** get session token */
        inline const result<std::string> get_SessionToken() const
        {
            auto idx = ttAsIdx(TokenType::session);
            if (!tokensByType[idx].empty())
                return tokensByType[idx];
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available"s};
        }

        /** return the admin token */
        inline const result<std::string> get_AdminToken() const
        {
            auto idx = ttAsIdx(TokenType::admin);
            if (!tokensByType[idx].empty())
                return tokensByType[idx];
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available"s};
        }

        /** set LB name */
        inline void set_lbName(const std::string &n)
        {
            lbName = n;
        }

        /** set LB Id */
        inline void set_lbId(const std::string &i)
        {
            lbId = i;
        }

        /** set session Id from gRPC return */
        inline void set_sessionId(const std::string &i)
        {
            sessionId = i;
        }

        /**
         * Set the sync address (v4 or v6)
        */
        inline void set_syncAddr(const std::pair<ip::address, u_int16_t> &a)
        {
            syncAddr = a.first;
            syncPort = a.second;
            haveSync = true;
        }

        /**
         * Set a dataplane address (v4 or v6)
        */
        inline void set_dataAddr(const std::pair<ip::address, u_int16_t> &a)
        {
            if (a.first.is_v4()) {
                dataAddrv4 = a.first;
                haveDatav4 = true;
            }
            else {
                dataAddrv6 = a.first;
                haveDatav6 = true;
            }
        }

        /** get LB name */
        inline const std::string get_lbName() const
        {
            return lbName;
        }

        /** get LB ID */
        inline const std::string get_lbId() const
        {
            return lbId;
        }

        /** get session Id  */
        inline const std::string get_sessionId() const 
        {
            return sessionId;
        }

        /** get control plane ip address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_cpAddr() const
        {
            return std::pair<ip::address, u_int16_t>(cpAddr, cpPort);
        }

        /** get control plane hostname and port */
        inline const result<std::pair<std::string, u_int16_t>> get_cpHost() const
        {
            if (!cpHost.empty())
                return std::pair<std::string, u_int16_t>(cpHost, cpPort);
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Control plane hostname not available"s};
        }

        /** does the URI contain a v4 dataplane address? */
        inline const bool has_dataAddrv4() const
        {
            return haveDatav4;
        }

        /** does the URI contain a v6 dataplane address? */
        inline const bool has_dataAddrv6() const
        {
            return haveDatav6;
        }

        /** does the URI contain any dataplane address? */
        inline const bool has_dataAddr() const
        {
            return haveDatav4 || haveDatav6;
        }

        /** does the URI contain a sync address */
        inline const bool has_syncAddr() const
        {
            return haveSync;
        }

        /** get data plane v4 address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_dataAddrv4() const noexcept
        {
            if (haveDatav4)
                return std::pair<ip::address, u_int16_t>(dataAddrv4, dataPort);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Data plane address not available"s};
        }

        /** get data plane v6 address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_dataAddrv6() const noexcept
        {
            if (haveDatav6)
                return std::pair<ip::address, u_int16_t>(dataAddrv6, dataPort);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Data plane address not available"s};
        }

        /** get sync address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_syncAddr() const noexcept
        {
            if (haveSync)
                return std::pair<ip::address, u_int16_t>(syncAddr, syncPort);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Sync address not available"s};
        }
        /** implicit cast to string which prints session token if available, otherwise insstance
         * token if available, otherwise admin token if available, otherwise no token.
         * To get the URI string with specific token use the to_string(TokenType) method.
        */
        operator std::string() const;

        const std::string to_string(TokenType tt = TokenType::admin) const;
 
        /** from environment variable
         * @param envVar - name of environment variable containing the URI (defaults to EJFAT_URI)
         * @param tt - token type - one of EjfatURI::TokenType::[admin, instance, session], defaults to admin
         * @param preferV6 - if control plane host specified by name use IPv6 address, defaults to false
        */
        static inline result<EjfatURI> getFromEnv(const std::string &envVar = "EJFAT_URI"s, 
            TokenType tt=TokenType::admin, bool preferV6=false) noexcept
        {
            const char *envStr = std::getenv(envVar.c_str());
            if (envStr != nullptr)
            {
                try
                {
                    return EjfatURI(envStr, tt, preferV6);
                }
                catch (const E2SARException &e)
                {
                    return E2SARErrorInfo{E2SARErrorc::CaughtException, "Unable to parse EJFAT_URI from environment variable: "s + static_cast<std::string>(e)};
                }
            }
            return E2SARErrorInfo{E2SARErrorc::Undefined, "Environment variable "s + envVar + " not defined."s};
        }

        /** from string 
         * @param uriStr - URI string
         * @param tt - token type - one of EjfatURI::TokenType::[admin, instance, session], defaults to admin
         * @param preferV6 - if control plane host specified by name use IPv6 address, defaults to false
        */
        static inline result<EjfatURI> getFromString(const std::string &uriStr, 
            TokenType tt=TokenType::admin, bool preferV6=false) noexcept
        {
            try
            {
                return EjfatURI(uriStr, tt, preferV6);
            }
            catch (const E2SARException &e)
            {
                return E2SARErrorInfo{E2SARErrorc::CaughtException, "Unable to parse URI from string: "s + static_cast<std::string>(e)};
            }
        }

        /** from a file
         * @param filename - file containing URI string
         * @param tt - token type - one of EjfatURI::TokenType::[admin, instance, session], defaults to admin
         * @param preferV6 - if control plane host specified by name use IPv6 address, defaults to false
        */
        static inline result<EjfatURI> getFromFile(const std::string &fileName = "/tmp/ejfat_uri"s, 
            TokenType tt=TokenType::admin, bool preferV6=false) noexcept
        {
            if (!fileName.empty())
            {
                std::ifstream file(fileName);
                if (file.is_open())
                {
                    std::string uriLine;
                    if (std::getline(file, uriLine))
                    {
                        file.close();
                        try
                        {
                            return EjfatURI(uriLine, tt, preferV6);
                        }
                        catch (const E2SARException &e)
                        {
                            return E2SARErrorInfo{E2SARErrorc::CaughtException, "Unable to parse URI: "s + static_cast<std::string>(e)};
                        }
                    }
                    file.close();
                    return E2SARErrorInfo{E2SARErrorc::Undefined, "Unable to parse URI."s};
                }
            }
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to find file "s + fileName};
        }

        /**
         * Figure out the local outgoing dataplane addresses based on data= entries
         * in the URI. This only works where NETLINK is available (Linux)
         * @param v6 - if true look for v6 (default false)
         */
        result<std::vector<ip::address>> getDataplaneLocalAddresses(bool v6=false) noexcept;
    };

    /**
     * Convert a string into an IPv4 or v6 address throwing E2SARException if a problem is encountered
     */
    static inline const result<ip::address> string_to_ip(const std::string &addr) noexcept
    {
        try
        {
            if (addr[0] == '[')
            {
                // strip '[]' from IPv6
                try
                {
                    ip::make_address(addr.substr(1, addr.length() - 2));
                }
                catch (...)
                {
                    ;
                }
                return ip::make_address(addr.substr(1, addr.length() - 2));
            }
            else
                return ip::make_address(addr);
        }
        catch (boost::system::system_error &e)
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert IP address from "s + addr};
        }
    }

    /**
     * Convert a string to a port number, checking range
     */
    static inline const result<u_int16_t> string_to_port(const std::string &port_string) noexcept
    {
        try
        {
            u_int16_t port = std::stoi(port_string);
            if (port < 1024 || port > 65535)
            {
                // port is out of range
                return E2SARErrorInfo{E2SARErrorc::OutOfRange, "Port value "s + port_string + " is out of range"s};
            }
            return port;
        }
        catch (const std::exception &e)
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + port_string + " to integer"s};
        }
    }

    /**
     * Convert a colon-separated tuple into ip address and port. Note that IPv6 in [] can contain colons.
     */
    static inline const result<std::pair<ip::address, u_int16_t>> string_tuple_to_ip_and_port(const std::string &t) noexcept
    {
        // search for last ":" (ip:port) or "]" (ipv6 by itself) whichever comes last
        auto const pos = t.find_last_of("]:");

        // IPv4 or IPv6 by itself
        if ((pos == std::string::npos) || (t[pos] == ']'))
        {
            auto r1 = string_to_ip(t);
            if (r1)
                return std::pair<ip::address, u_int16_t>(r1.value(), 0);
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + t + " to ip address and port"s};
        }

        // port with either IPv4 or IPv6 address x.x.x.x:num or [x:x:x:x::y]:num
        auto r1 = string_to_ip(t.substr(0, pos));
        auto r2 = string_to_port(t.substr(pos + 1));
        if (r1 && r2)
            return std::pair<ip::address, int>(r1.value(), r2.value());
        return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + t + " to ip address and port"s};
    }

    /**
     * Function to take a host name and turn it into IP addresses, IPv4 and IPv6.
     *
     * @param host_name name of host to examine.
     * @return outcome variable with either has_error() set or value() returning a list of ip::address
    */
    static inline result<std::vector<ip::address>> resolveHost(const std::string &host_name) noexcept
    {

        std::vector<ip::address> addresses;
        boost::asio::io_context io_context;

        try
        {
            ip::udp::resolver resolver(io_context);
            ip::udp::resolver::results_type results = resolver.resolve(host_name, "443");

            for(auto i = results.begin(); i != results.end(); ++i)
            {
                ip::udp::endpoint endpoint = *i;
                addresses.push_back(endpoint.address());
            }
            return addresses;
        }
        catch (...)
        {
            // anything happens - we can't find the host
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to convert "s + host_name + " to ip address"s};
        }
    }

    // to support unordered maps of pairs
    struct pair_hash {
        std::size_t operator()(const std::pair<u_int64_t, u_int16_t>& p) const {
            u_int64_t hash1 = p.first;
            u_int64_t tmp = p.second;
            u_int64_t hash2 = tmp | tmp << 16 | tmp << 32 | tmp << 48;
            return hash1 ^ hash2;  // Combine the two hashes
        }
    };

    struct pair_equal {
        bool operator()(const std::pair<u_int64_t, u_int16_t>& lhs, const std::pair<u_int64_t, u_int16_t>& rhs) const {
            return lhs.first == rhs.first && lhs.second == rhs.second;
        }
    };

    /**  
     * clock entropy test to validate that system clock produces sufficient randomness
     * in the least 8 bits of the microsecond timestamp (required by LB). Normally runs
     * for 1 second to collect 1k samples
     * @param totalTests - number of samples to collect
     * @param sleepMs - number of milliseconds to sleep between each sample
     * @return entropy in bits
     */
    static inline float clockEntropyTest(int totalTests = 1000, int sleepMs = 1) 
    {
        std::vector<int_least64_t> points;
        std::vector<int> bins(256, 0);

        for (int i = 0; i < totalTests; i++)
        {
            auto now = boost::chrono::system_clock::now();
            auto nowUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(now.time_since_epoch()).count();
            bins[nowUsec & 0xff]++;
            auto until = now + boost::chrono::milliseconds(sleepMs);
            boost::this_thread::sleep_until(until);
        }

        // compute the probabilities and entropy
        float entropy{0.0};
        for (size_t i = 0; i < bins.size(); i++)
        {
            float prob = static_cast<float>(bins[i])/(totalTests*1.0);
            entropy += prob * std::log(prob);
        }

        // normalize into bits
        entropy *= -1.0/std::log(2);
        return entropy;
    }

    template<typename Container>
    std::string concatWithSeparator(const Container& c, const std::string& sep=","s)
    {
        typename Container::const_iterator it = c.begin();
        std::string rets{};
        while(it != c.end())
        {
            rets += *it;
            if (++it != c.end())
                rets += sep;
        }
        return rets;
    }

    // busy wait for a given number of microseconds using high_resolution_clock timepoints
    inline void busyWaitUsecs(const boost::chrono::steady_clock::time_point &tp, int64_t usecs)
    {
        while(true)
        {
            // busy wait checking the clock
            if (boost::chrono::duration_cast<boost::chrono::microseconds>(boost::chrono::high_resolution_clock::now() 
                - tp).count() > usecs)
                break;
        }   
    }

    using OptimizationsWord = u_int16_t;
    /**
     * This class encompasses the definition, encoding and selection of optimizations
     * There are two types of optimizations - available (compiled in) and selected (chosen)
     */
    class Optimizations {
        public:
            // go as powers of 2 to make calculations easier
            enum class Code {
                none = 0,
                sendmmsg = 1,
                liburing_send = 2,
                liburing_recv = 3,
                // always last
                unknown = 15
            };
        public:
            inline static OptimizationsWord toWord(Code o)
            {
                return 1 << static_cast<int>(o);
            }
            inline static std::string toString(Code o)
            {
                switch(o)
                {
                    case Code::none: return "none"s; 
                    case Code::sendmmsg: return "sendmmsg";
                    case Code::liburing_recv: return "liburing_recv";
                    case Code::liburing_send: return "liburing_send";
                    default: "unknown"s;
                }
                return "unknown"s;
            }
            inline static Code fromString(const std::string& opt)
            {
                if (opt == "none"s)
                    return Code::none;
                else if (opt == "sendmmsg"s)
                    return Code::sendmmsg;
                else if (opt == "liburing_recv"s)
                    return Code::liburing_recv;
                else if (opt == "liburing_send"s)
                    return Code::liburing_send;
                return Code::unknown;
            }
            /**
             * List of strings of available optimizations that are compiled in
             */
            const static std::vector<std::string> availableAsStrings() noexcept;

            /**
             * Get a single 16-bit word with OR of all compiled-in optimizations
             */
            const static OptimizationsWord availableAsWord() noexcept;

            /**
             * Select optimizations based on names and add them to internal
             * state
             */
            static result<int> select(std::vector<std::string>& opt) noexcept;

            /**
             * Select optimizations based on enum value names and add them to internal
             * state
             */
            static result <int> select(std::vector<Code> &opt) noexcept;

            /**
             * List of strings of selected optimizations
             */
            const static std::vector<std::string> selectedAsStrings() noexcept;

            /**
             * List of selected optimizations as a word
             */
            const static OptimizationsWord selectedAsWord() noexcept;

            /**
             * List of selected optimizations as a vector
             */
            const static std::vector<Code> selectedAsList() noexcept;

            /**
             * Is this optimization selected?
             */
            const static bool isSelected(Code o) noexcept;
        private:
            // all available compiled in optimizations
            static const std::vector<Optimizations::Code> available;

            // this is where we store selected optimizations;
            OptimizationsWord selected_optimizations;

            // c-tor is private
            Optimizations():selected_optimizations{toWord(Code::none)}
            {}
            // d-tor doesn't exist
            ~Optimizations() = delete;

            static Optimizations* instance;
            static inline Optimizations* _get()
            {
                if (not instance)
                    instance = new Optimizations();
                return instance;
            }
    };

    /**
     * Expand tilde (~) in file paths to home directory.
     * Only supports ~/path format (not ~username/path).
     * Falls back to original path if HOME environment variable is not set.
     */
    std::string expandTilde(const std::string& path);

    /**
     * Define a std::clog-based logger
     */
    void defineClogLogger();
};
#endif