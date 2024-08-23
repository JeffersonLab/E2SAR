#include "e2sarNetUtil.hpp"

namespace e2sar
{
    /**
     * Get MTU of a given interface
     */
    u_int16_t NetUtil::getMTU(const std::string &interfaceName) {
        // Default MTU
        u_int16_t mtu = 1500;

        int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        struct ifreq ifr;
        strcpy(ifr.ifr_name, interfaceName.c_str());
        if (!ioctl(sock, SIOCGIFMTU, &ifr)) {
            mtu = ifr.ifr_mtu;
        }
        close(sock);
        return mtu;
    }
#ifdef NETLINK_CAPABLE
    /**
     * Get the outgoing interface and its MTU for a given IPv4 or IPv6
     */
    result<boost::tuple<std::string, u_int16_t>> NetUtil::getInterfaceAndMTU(const ip::address &addr) 
    {
        // Untested code from ChatGPT 07/12/24
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
        req.rt.rtm_family = AF_INET;

        struct rtattr *rta = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.nlh.nlmsg_len));
        rta->rta_type = RTA_DST;
        rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
        inet_pton(AF_INET, addr.to_string().c_str(), RTA_DATA(rta));
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
        for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE)
                break;

            if (nlh->nlmsg_type == NLMSG_ERROR) 
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};

            struct rtmsg *rtm = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(nlh));
            struct rtattr *rta = RTM_RTA(rtm);
            int rta_len = RTM_PAYLOAD(nlh);

            for (; RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
                if (rta->rta_type == RTA_OIF) {
                    int ifindex = *(int *)RTA_DATA(rta);
                    char ifname[IFNAMSIZ];
                    if_indextoname(ifindex, ifname);

                    struct ifreq ifr;
                    memset(&ifr, 0, sizeof(ifr));
                    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

                    if (ioctl(sock, SIOCGIFMTU, &ifr) < 0) 
                        return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};

                    //printf("MTU of interface %s (index %d) to %s is %d\n", ifname, ifindex, dest_ip, ifr.ifr_mtu);
                    close(sock);
                    return boost::make_tuple<std::string, u_int16_t>(ifname, ifr.ifr_mtu);
                }
            }   
        }
        close(sock);
        return E2SARErrorInfo{E2SARErrorc::SocketError, "Unrecoverable NETLINK error"};
    }
#endif
}
