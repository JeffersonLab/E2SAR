#include <iostream>
#include <system_error>
#include <ctime>
#include <sys/select.h>
#include <boost/asio.hpp>
#include <boost/outcome.hpp>
#include <boost/url.hpp>
#include <boost/heap/priority_queue.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <google/protobuf/util/time_util.h>

#include <boost/pool/object_pool.hpp>

#include "e2sarHeaders.hpp"

using namespace boost::asio;
namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;
using namespace std::string_literals;

void funcRef(const std::vector<int> &v)
{
    std::cout << "Ref" << std::endl;
    for(auto a: v)
        std::cout << a << " ";
    std::cout << std::endl;
}

void funcMv(std::vector<int> &&v)
{
    std::cout << "Move" << std::endl;
    for(auto a: v)
        std::cout << a << " ";
    std::cout << std::endl;
}

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


    // pool tests

    // object pool for queue items
    struct qItem {
        char preamble[2] {'L', 'B'};
        uint32_t bytes;
        uint64_t tick;
        char     *event;
        void     *cbArg;
        void* (*callback)(void *);

        qItem(): bytes{100}, tick{1000} {};
    };

    std::cout << "Item size " << sizeof(qItem) << std::endl;
    boost::object_pool<qItem> pool{sizeof(qItem) * 10, 0};
    std::cout << pool.get_next_size() << '\n';

    auto newItem = pool.construct();

    std::cout << pool.get_next_size() << '\n';

    newItem->tick = 1001;

    std::cout << newItem->tick << " " << newItem->bytes << std::endl;

    std::cout << newItem->preamble[0] << newItem->preamble[1] << std::endl;

    pool.free(newItem);

    std::cout << (1 << 4) << std::endl;

    std::cout << "Sync header size (expecting 28) = " << sizeof(e2sar::SyncHdr) << std::endl;

    {
        std::cout << "  Empty scope executes once" << std::endl;
    }

    std::cout << "  LB Hdr size (expecting 16) = " << sizeof(e2sar::LBHdr) << std::endl;
    std::cout << "  RE Hdr size (expecting 20) = " << sizeof(e2sar::REHdr) << std::endl;
    std::cout << "  LB+RE Hdr size (expecting 36) = " << sizeof(e2sar::LBREHdr) << std::endl;


    // test allocating N ports to M threads
    std::vector<int> ports{1,2,3,4};
    std::cout << "Assignable ports: " << std::endl;
    std::cout << "  ";
    for(auto i: ports)
        std::cout << i  << " ";
    std::cout << std::endl;

    int threads{3};

    std::vector<std::list<int>> ptt(threads); 

    for(int i=0;i<ports.size();)
    {
        for(int j=0;j<threads && i<ports.size();i++,j++)
        {
            ptt[j].push_back(ports[i]);
        }
    }

    std::cout << "Assigned ports to threads: " << std::endl;
    int t{0};
    for(auto l: ptt)
    {
        std::cout << "  Thread " << t++ << ": " << std::endl;
        for(auto e: l)
            std::cout << e << " ";
        std::cout << std::endl;
    }

    std::list<std::unique_ptr<int>> recvThreadState(5);
    std::cout << "Testing list: allocated size is (5) " << recvThreadState.size() << std::endl;

    // testing dealing with portRange
    std::cout << "Testing portRange:" << std::endl;
    int portRange{3};
    size_t numPorts{static_cast<size_t>(2 << (portRange - 1))};
    u_int16_t startPort{1850};
    size_t numThreads{3};

    std::cout << "  Allocating " << numPorts << " ports from portRange " << portRange << " to " << numThreads << " threads" << std::endl;
    for(int i=0; i<numPorts;)
    {
        for(int j=0; i<numPorts && j<numThreads; i++, j++)
        {
            std::cout << "  Assigning port " << startPort + i << " to thread " << j << std::endl;
        } 
    }

    // testing passing vectors
    std::cout << "Passing vectors " << std::endl;
    std::vector<int> va{1,2,3};
    std::vector<int> vb{2,3,4};

    funcRef(va);
    for(auto a: va)
        std::cout << "  " << a << " ";
    std::cout << std::endl;

    funcMv(std::move(vb));
    for(auto a: vb)
        std::cout << "  " << a << " ";
    std::cout << std::endl;

    // testing fd_set copy constructor
    std::cout << "Testing fdset" << std::endl;
    fd_set fdSet{};

    FD_SET(0, &fdSet);
    FD_SET(2, &fdSet);

    fd_set newSet{fdSet};

    std::cout << "  old set " << FD_ISSET(0, &fdSet) << " " << FD_ISSET(1, &fdSet) << " " << FD_ISSET(2, &fdSet) << std::endl;
    std::cout << "  new set " << FD_ISSET(0, &newSet) << " " << FD_ISSET(1, &newSet) << " " << FD_ISSET(2, &newSet) << std::endl;

    // test priority queue for custom events
    std::cout << "Priority queue" << std::endl;
    struct Event {
        int len;
        int offset;
    };

    struct EventComparator {
        bool operator() (const Event &l, const Event &r) const {
            return l.offset > r.offset;
        }
    };

    Event e1{5, 0};
    Event e2{3, 5};
    Event e3{2, 8};

    boost::heap::priority_queue<Event, boost::heap::compare<EventComparator>> pq;

    pq.push(e2);
    pq.push(e3);
    pq.push(e1);

    for(; !pq.empty(); pq.pop())
        std::cout << "  Event len " << pq.top().len << " offset " << pq.top().offset << std::endl;

    
    // test boost pool array allocation
    std::cout << "Test pool allocation" << std::endl;
    boost::pool<> bufpool(20);

    auto ar1 = static_cast<char*>(bufpool.malloc());
    auto ar2 = static_cast<char*>(bufpool.malloc());

    std::strcpy(ar1, "Hello world!");
    std::strcpy(ar2, "I have come to help!");

    std::cout << "  Ar1 " << ar1 << std::endl;
    std::cout << "  Ar2 " << ar2 << std::endl;

    bufpool.free(ar1);
    bufpool.free(ar2);

    std::cout << "Test array of lists" << std::endl;

    std::vector<std::list<int>> portsToThreads;

    portsToThreads.emplace(portsToThreads.end());

    portsToThreads[0].push_back(1);
}

