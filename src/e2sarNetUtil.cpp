#ifdef NETLINK_CAPABLE
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>

#include "e2sarNetUtil.hpp"

namespace e2sar
{
    /**
     * Get MTU of a given interface. Used in constructors, so doesn't
     * return error.
     */
    size_t NetUtil::getMTU(const std::string &interfaceName) noexcept 
    {
        // Default MTU
        size_t mtu = 1500;

        int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        struct ifreq ifr;
        strcpy(ifr.ifr_name, interfaceName.c_str());
        if (!ioctl(sock, SIOCGIFMTU, &ifr)) {
            mtu = ifr.ifr_mtu;
        } 
        close(sock);
        return mtu;
    }

    result<std::string> NetUtil::getHostName() noexcept 
    {
        char nameBuf[255];

        if (!gethostname(nameBuf, 255)) 
        {
            std::string ret{nameBuf};
            return ret;
        } else 
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to retrieve hostname"};
    }

    result<std::vector<ip::address>> NetUtil::getInterfaceIPs(const std::string &interfaceName, bool v6) noexcept 
    {
        struct ifaddrs *ifaddr;
        std::vector<ip::address> ips;

        if (getifaddrs(&ifaddr) == -1)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(errno)};
        
        // walk the list, multiple answers are possible
        for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
        {
            if (ifa->ifa_addr == NULL)
                continue;

            std::string ifa_name{ifa->ifa_name};
            if (ifa_name == interfaceName)
            {
                if ((not v6 and (ifa->ifa_addr->sa_family == AF_INET)) 
                    || (v6 and (ifa->ifa_addr->sa_family == AF_INET6)))
                {
                    auto ifa_addr = (struct sockaddr_in*) ifa->ifa_addr;
                    ips.push_back(ip::make_address(inet_ntoa(ifa_addr->sin_addr)));
                }
            }
        }
        freeifaddrs(ifaddr);
        return ips;
    }
#ifdef NETLINK_CAPABLE
    /**
     * Get the outgoing interface and its MTU for a given IPv4 or IPv6
     */
    result<boost::tuple<std::string, u_int16_t, ip::address>> NetUtil::getInterfaceAndMTU(const ip::address &addr) 
    {
        struct sockaddr_nl sa{};
        struct {
            struct nlmsghdr nlh;
            struct rtmsg rt;
            char buf[1024];
        } req;
        int sock;
        ssize_t len;

        sa.nl_family = AF_NETLINK;

        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.nlh.nlmsg_flags = NLM_F_REQUEST;
        req.nlh.nlmsg_type = RTM_GETROUTE;

        struct rtattr *rta = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.nlh.nlmsg_len));
        rta->rta_type = RTA_DST;
        if (addr.is_v6())
        {
            req.rt.rtm_family = AF_INET6;
            rta->rta_len = RTA_LENGTH(sizeof(struct in6_addr));
            inet_pton(AF_INET6, addr.to_string().c_str(), RTA_DATA(rta));
        }
        else
        {
            req.rt.rtm_family = AF_INET;
            rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
            inet_pton(AF_INET, addr.to_string().c_str(), RTA_DATA(rta));
        }

        req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + rta->rta_len;

        sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (sock < 0) 
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};

        if (sendto(sock, &req, req.nlh.nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) 
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};

        char buffer[4096];
        len = recv(sock, buffer, sizeof(buffer), 0);
        if (len < 0) 
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};

        struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;

        int ifindex = -1;
        char ifname[IFNAMSIZ];
        size_t mtu = 0;
        struct in_addr src_ip4 = {0};
        struct in6_addr src_ip6 = {0};
        char src_ip_str[INET6_ADDRSTRLEN]; // long enough for both IPv4 and IPv6
        ip::address srcAddr;

        bool found = false;
        for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE)
                break;

            if (nlh->nlmsg_type == NLMSG_ERROR) 
            {
                int err = *reinterpret_cast<int*>(NLMSG_DATA(nlh));
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(-err)};
            }

            struct rtmsg *rtm = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(nlh));
            struct rtattr *rta = RTM_RTA(rtm);
            int rta_len = RTM_PAYLOAD(nlh);

            // walk RTNETLINK attributes
            for (; RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
                if (rta->rta_type == RTA_OIF) {
                    ifindex = *(int *)RTA_DATA(rta);
                    if_indextoname(ifindex, ifname);
                    mtu = getMTU(ifname);
                    found = true;
                } else if (rta->rta_type == RTA_PREFSRC) {
                    if (addr.is_v6()) {
                        src_ip6 = *(reinterpret_cast<struct in6_addr*>RTA_DATA(rta));
                        inet_ntop(AF_INET6, &src_ip6, src_ip_str, sizeof(src_ip_str));
                    }
                    else {
                        src_ip4 = *(reinterpret_cast<struct in_addr*>RTA_DATA(rta));
                        inet_ntop(AF_INET, &src_ip4, src_ip_str, sizeof(src_ip_str));
                    }
                }
            }   
        }
        close(sock);
        if (found)
            return boost::make_tuple<std::string, u_int16_t, ip::address>(ifname, mtu, ip::make_address(src_ip_str));
        else
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to determine outgoing interface"};
    }
#endif
    result<int> NetUtil::getSocketOutstandingBytes(int sockfd) noexcept {
        int outstanding = 0;
        int res = 0;
    #if defined(SIOCOUTQ_AVAILABLE)
        res = ioctl(sockfd, TIOCOUTQ, &outstanding);
        if (res < 0)
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
    #elif defined(SO_NWRITE_AVAILABLE)
        socklen_t len = sizeof(outstanding);
        res = getsockopt(sockfd, SOL_SOCKET, SO_NWRITE, &outstanding, &len);
        if (res < 0)
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
    #else
        #warning "No send buffer query support on this platform"
        outstanding = -1;  // unsupported
    #endif
        return outstanding;
  }
}
