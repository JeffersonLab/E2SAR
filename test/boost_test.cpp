#include <iostream>
#include <system_error>
#include <ctime>
#include <boost/asio.hpp>
#include <boost/outcome.hpp>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <google/protobuf/util/time_util.h>

using namespace boost::asio;
namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;
using namespace std::string_literals;

int main()
{

    // Test boost IP addresses
    ip::address ipv4 = ip::make_address("192.168.1.1");
    ip::address ipv6 = ip::make_address("2001:db8:0000:1:1:1:1:1");
    try
    {
        ip::make_address("blaaaa");
    }
    catch (boost::system::system_error &e)
    {
        std::cout << "Unable to convert string to address" << std::endl;
    }

    std::cout << "IPv4 " << ipv4 << ' ' << ipv4.is_v4() << '\n';
    std::cout << "IPv6 " << ipv6 << ' ' << ipv6.is_v6() << '\n';

    // Test name resolution
    boost::asio::io_service io_service;

    ip::udp::resolver resolver(io_service);
    ip::udp::resolver::query query("www.renci.org", "445");
    ip::udp::resolver::iterator iter = resolver.resolve(query);
    ip::udp::resolver::iterator end; // End marker.
    while (iter != end)
    {
        ip::udp::endpoint endpoint = *iter++;
        ip::address address = endpoint.address();
        std::cout << address << ' ' << address.is_v4() << ' ' << std::endl;
    }

    // Test URL parsing
    std::string uri_string{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};
    boost::system::result<boost::url_view> r = boost::urls::parse_uri(uri_string);

    if (!r)
    {
        if (r.error())
            std::cout << "Unable to convert! "s << r.error() << std::endl;
    }
    else
    {

        boost::url_view u = r.value();

        std::cout << u.scheme() << std::endl;
        std::cout << u.userinfo() << " " << u.userinfo().length() << std::endl;
        std::cout << u.host() << std::endl;
        std::cout << u.port() << std::endl;
        std::cout << u.path() << std::endl;
        std::cout << u.query() << std::endl;

        for (auto param : u.params())
            std::cout << param.key << ": " << param.value << std::endl;

        std::vector<std::string> lb_path;
        boost::split(lb_path, u.path(), boost::is_any_of("/"));
        for (auto g : lb_path)
        {
            std::cout << ": " << g << std::endl;
        }
    }

    // Timestamp conversions
    // from std::time_t
    std::time_t now = std::time(nullptr);
    google::protobuf::Timestamp ts = google::protobuf::util::TimeUtil::TimeTToTimestamp(now);

    // from boost::ptime with time difference
    boost::posix_time::ptime pt = boost::posix_time::second_clock::local_time();
    boost::posix_time::time_duration td = boost::posix_time::hours(24);
    boost::posix_time::ptime pt1 = pt + td;
    std::cout << "Now + 1 day is " << pt1 << std::endl;
    google::protobuf::Timestamp ts1 = google::protobuf::util::TimeUtil::TimeTToTimestamp(to_time_t(pt1));
    std::cout << ts1 << std::endl;
}
