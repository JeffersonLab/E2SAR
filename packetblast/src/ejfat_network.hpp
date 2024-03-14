//
// Created by Carl Timmer on 11/1/22.
//

#ifndef EJFAT_EJFAT_NETWORK_H
#define EJFAT_EJFAT_NETWORK_H

#include <cstdio>
#include <cstring>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>   /* basic system data types */
#include <sys/socket.h>  /* basic socket definitions */
#include <sys/ioctl.h>   /* find broacast addr */
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>

namespace ejfat {

    //*****************************************************
    //  Networking (TCP client) code taken from ET system
    //*****************************************************

    /**
     * Routine that translates integer error into string.
     *
     * @param err integer error
     * @returns error string
     */
    static const char *netStrerror(int err) {
        if (err == 0)
            return ("no error");

        if (err == HOST_NOT_FOUND)
            return ("Unknown host");

        if (err == TRY_AGAIN)
            return ("Temporary error on name server - try again later");

        if (err == NO_RECOVERY)
            return ("Unrecoverable name server error");

        if (err == NO_DATA)
            return ("No address associated with name");

        return ("unknown error");
    }


    /**
     * This routine tells whether the given ip address is in dot-decimal notation or not.
     *
     * @param ipAddress ip address in string form
     * @param decimals  pointer to array of 4 ints which gets filled with ip address ints if not NULL
     *                  and address is in dotted decimal form (left most first)
     *
     * @returns 1    if address is in dot decimal format
     * @returns 0    if address is <b>NOT</b> in dot decimal format
     */
    static int isDottedDecimal(const char *ipAddress, int *decimals) {
        int i[4], j, err, isDottedDecimal = 0;

        if (ipAddress == NULL) return (0);

        err = sscanf(ipAddress, "%d.%d.%d.%d", &i[0], &i[1], &i[2], &i[3]);
        if (err == 4) {
            isDottedDecimal = 1;
            for (j = 0; j < 4; j++) {
                if (i[j] < 0 || i[j] > 255) {
                    isDottedDecimal = 0;
                    break;
                }
            }
        }

        if (isDottedDecimal && decimals != NULL) {
            for (j = 0; j < 4; j++) {
                *(decimals++) = i[j];
            }
        }

        return (isDottedDecimal);
    }


    /**
     * Function to take a string IP address, either an alphabetic host name such as
     * mycomputer.jlab.org or one in presentation format such as 129.57.120.113,
     * and convert it to binary numeric format and place it in a sockaddr_in
     * structure.
     *
     * @param ip_address string IP address of a host
     * @param addr pointer to struct holding the binary numeric value of the host
     *
     * @returns 0    if successful
     * @returns -1   if ip_address is null, out of memory, or
     *               the numeric address could not be obtained/resolved
     */
    static int stringToNumericIPaddr(const char *ip_address, struct sockaddr_in *addr) {
        int isDotDecimal;
        struct in_addr **pptr;
        struct hostent *hp;

        if (ip_address == NULL) {
            fprintf(stderr, "stringToNumericIPaddr: null argument\n");
            return (-7);
        }

        /*
         * Check to see if ip_address is in dotted-decimal form. If so, use different
         * routines to process that address than if it were a name.
         */
        isDotDecimal = isDottedDecimal(ip_address, NULL);

        if (isDotDecimal) {
            if (inet_pton(AF_INET, ip_address, &addr->sin_addr) < 1) {
                return (-1);
            }
            return (0);
        }

        if ((hp = gethostbyname(ip_address)) == NULL) {
            fprintf(stderr, "stringToNumericIPaddr: hostname error - %s\n", netStrerror(h_errno));
            return (-1);
        }

        pptr = (struct in_addr **) hp->h_addr_list;

        for (; *pptr != NULL; pptr++) {
            memcpy(&addr->sin_addr, *pptr, sizeof(struct in_addr));
            break;
        }

        return (0);
    }


    /**
     * This routine chooses a particular network interface for a TCP socket
     * by having the caller provide a dotted-decimal ip address. Port is set
     * to an ephemeral port.
     *
     * @param fd file descriptor for TCP socket
     * @param ip_address IP address in dotted-decimal form
     *
     * @returns 0    if successful
     * @returns -1   if ip_address is null, out of memory, or
     *               the numeric address could not be obtained/resolved
     */
    static int setInterface(int fd, const char *ip_address) {
        int err;
        struct sockaddr_in netAddr;

        memset(&netAddr, 0, sizeof(struct sockaddr_in));

        err = stringToNumericIPaddr(ip_address, &netAddr);
        if (err != 0) {
            return err;
        }
        netAddr.sin_family = AF_INET; /* ipv4 */
        netAddr.sin_port = 0;         /* choose ephemeral port # */

        err = bind(fd, (struct sockaddr *) &netAddr, sizeof(netAddr));
        if (err != 0) perror("error in setInterface: ");
        return 0;
    }


    /**
     * This routine makes a TCP connection to a server.
     *
     * @param inetaddr    binary numeric address of host to connect to
     * @param interface   interface (dotted-decimal ip address) to connect through
     * @param port        port to connect to
     * @param sendBufSize size of socket's send buffer in bytes
     * @param rcvBufSize  size of socket's receive buffer in bytes
     * @param noDelay     false if socket TCP_NODELAY is off, else it's on
     * @param fd          pointer which gets filled in with file descriptor
     * @param localPort   pointer which gets filled in with local (ephemeral) port number
     *
     * @returns 0                          if successful
     * @returns -1   if out of memory,
     *               if socket could not be created or socket options could not be set, or
     *               if host name could not be resolved or could not connect
     *
     */
    static int tcpConnect2(uint32_t inetaddr, const char *interface, unsigned short port,
                           int sendBufSize, int rcvBufSize, bool noDelay, int *fd, int *localPort) {
        int sockfd, err;
        const int on = 1;
        struct sockaddr_in servaddr;


        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "tcpConnect2: socket error, %s\n", strerror(errno));
            return (-1);
        }

        // don't wait for messages to cue up, send any message immediately
        if (noDelay) {
            err = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect2: setsockopt error\n");
                return (-1);
            }
        }

        // set send buffer size unless default specified by a value <= 0
        if (sendBufSize > 0) {
            err = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *) &sendBufSize, sizeof(sendBufSize));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect2: setsockopt error\n");
                return (-1);
            }
        }

        // set receive buffer size unless default specified by a value <= 0
        if (rcvBufSize > 0) {
            err = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &rcvBufSize, sizeof(rcvBufSize));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect2: setsockopt error\n");
                return (-1);
            }
        }

        // set the outgoing network interface
        if (interface != NULL && strlen(interface) > 0) {
            err = setInterface(sockfd, interface);
            if (err != 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect2: error choosing network interface\n");
                return (-1);
            }
        }

        memset((void *) &servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        servaddr.sin_addr.s_addr = inetaddr;

        if ((err = connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) < 0) {
            fprintf(stderr, "tcpConnect2: error attempting to connect to server\n");
        }

        /* if there's no error, find & return the local port number of this socket */
        if (err != -1 && localPort != NULL) {
            socklen_t len;
            struct sockaddr_in ss;

            len = sizeof(ss);
            if (getsockname(sockfd, (struct sockaddr *) &ss, &len) == 0) {
                *localPort = (int) ntohs(ss.sin_port);
            }
            else {
                *localPort = 0;
            }
        }

        if (err == -1) {
            close(sockfd);
            fprintf(stderr, "tcpConnect2: socket connect error, %s\n", strerror(errno));
            return (-1);
        }

        if (fd != NULL) *fd = sockfd;
        return (0);
    }


    /**
     * This routine makes a TCP connection to a server.
     *
     * @param ip_address  name of host to connect to (may be dotted-decimal)
     * @param interface   interface (dotted-decimal ip address) to connect through
     * @param port        port to connect to
     * @param sendBufSize size of socket's send buffer in bytes
     * @param rcvBufSize  size of socket's receive buffer in bytes
     * @param noDelay     false if socket TCP_NODELAY is off, else it's on
     * @param fd          pointer which gets filled in with file descriptor
     * @param localPort   pointer which gets filled in with local (ephemeral) port number
     *
     * @returns 0    if successful
     * @returns -1   if ip_adress or fd args are NULL, out of memory,
     *               socket could not be created or socket options could not be set,
     *               host name could not be resolved or could not connect
     *
     */
    static int tcpConnect(const char *ip_address, const char *interface, unsigned short port,
                          int sendBufSize, int rcvBufSize, bool noDelay, int *fd, int *localPort) {
        int sockfd, err = 0, isDotDecimal = 0;
        const int on = 1;
        struct sockaddr_in servaddr;
        struct in_addr **pptr;
        struct hostent *hp;
        int h_errnop = 0;

        if (ip_address == NULL || fd == NULL) {
            fprintf(stderr, "tcpConnect: null argument(s)\n");
            return (-1);
        }

        /* Check to see if ip_address is in dotted-decimal form. If so, use different
         * routine to process that address than if it were a name. */
        isDotDecimal = isDottedDecimal(ip_address, NULL);
        if (isDotDecimal) {
            uint32_t inetaddr;
            if (inet_pton(AF_INET, ip_address, &inetaddr) < 1) {
                fprintf(stderr, "tcpConnect: unknown address for host %s\n", ip_address);
                return (-1);
            }
            return tcpConnect2(inetaddr, interface, port, sendBufSize, rcvBufSize, noDelay, fd, localPort);
        }


        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "tcpConnect: socket error, %s\n", strerror(errno));
            return (-1);
        }

        /* don't wait for messages to cue up, send any message immediately */
        if (noDelay) {
            err = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect: setsockopt error\n");
                return (-1);
            }
        }

        /* set send buffer size unless default specified by a value <= 0 */
        if (sendBufSize > 0) {
            err = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *) &sendBufSize, sizeof(sendBufSize));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect: setsockopt error\n");
                return (-1);
            }
        }

        /* set receive buffer size unless default specified by a value <= 0  */
        if (rcvBufSize > 0) {
            err = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &rcvBufSize, sizeof(rcvBufSize));
            if (err < 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect: setsockopt error\n");
                return (-1);
            }
        }

        /* set the outgoing network interface */
        if (interface != NULL && strlen(interface) > 0) {
            err = setInterface(sockfd, interface);
            if (err != 0) {
                close(sockfd);
                fprintf(stderr, "tcpConnect: error choosing network interface\n");
                return (-1);
            }
        }

        memset((void *) &servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);

        if ((hp = gethostbyname(ip_address)) == NULL) {
            close(sockfd);
            fprintf(stderr, "tcpConnect: hostname error - %s\n", netStrerror(h_errnop));
            return (-1);
        }
        pptr = (struct in_addr **) hp->h_addr_list;

        for (; *pptr != NULL; pptr++) {
            memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
            if ((err = connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) < 0) {
                fprintf(stderr, "tcpConnect: error attempting to connect to server, %s\n", strerror(errno));
            }
            else {
                fprintf(stderr, "tcpConnect: connected to server\n");
                break;
            }
        }

        /* if there's no error, find & return the local port number of this socket */
        if (err != -1 && localPort != NULL) {
            socklen_t len;
            struct sockaddr_in ss;

            len = sizeof(ss);
            if (getsockname(sockfd, (struct sockaddr *) &ss, &len) == 0) {
                *localPort = (int) ntohs(ss.sin_port);
            }
            else {
                *localPort = 0;
            }
        }

        if (err == -1) {
            close(sockfd);
            fprintf(stderr, "tcpConnect: socket connect error\n");
            return (-1);
        }

        if (fd != NULL) *fd = sockfd;
        return (0);
    }


    /**
     * This routine is a convenient wrapper for the write function
     * which writes a given number of bytes from a single buffer
     * over a single file descriptor. Will write all data unless
     * error occurs.
     *
     * @param fd      file descriptor
     * @param vptr    pointer to buffer to write
     * @param n       number of bytes to write
     *
     * @returns total number of bytes written, else -1 if error (errno is set)
     */
    static int tcpWrite(int fd, const void *vptr, int n) {
        int nleft;
        int nwritten;
        const char *ptr;

        ptr = (char *) vptr;
        nleft = n;

        while (nleft > 0) {
            if ((nwritten = write(fd, (char *) ptr, nleft)) <= 0) {
                if (errno == EINTR) {
                    nwritten = 0;        /* and call write() again */
                }
                else {
                    return (nwritten);    /* error */
                }
            }

            nleft -= nwritten;
            ptr += nwritten;
        }
        return (n);
    }

    //*****************************************************
    // End of networking code
    //*****************************************************

}

#endif //EJFAT_EJFAT_NETWORK_H
