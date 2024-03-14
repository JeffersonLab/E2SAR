//
// Copyright 2022, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.
//
// EPSCI Group
// Thomas Jefferson National Accelerator Facility
// 12000, Jefferson Ave, Newport News, VA 23606
// (757)-269-7100


/**
 * @file
 * Contains routines to receive UDP packets that have been "packetized"
 * (broken up into smaller UDP packets by an EJFAT packetizer).
 * The receiving program handles sequentially numbered packets that may arrive out-of-order
 * coming from an FPGA-based between this and the sending program. Note that the routines
 * to reassemble buffers assume the new, version 2, RE headers. The code to reassemble the
 * older style RE header is still included but commented out.
 */
#ifndef EJFAT_ASSEMBLE_ERSAP_H
#define EJFAT_ASSEMBLE_ERSAP_H


#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include <cerrno>
#include <map>
#include <cmath>
#include <memory>
#include <getopt.h>
#include <climits>
#include <cinttypes>
#include <unordered_map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>


#ifdef __APPLE__
#include <cctype>
#endif

// Reassembly (RE) header size in bytes
#define HEADER_BYTES 20
#define HEADER_BYTES_OLD 18
// Max MTU that ejfat nodes' NICs can handle
#define MAX_EJFAT_MTU 9978

#define btoa(x) ((x)?"true":"false")


#ifdef __linux__
    // for recvmmsg
    #ifndef _GNU_SOURCE
        #define _GNU_SOURCE
    #endif

    #define htonll(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
    #define ntohll(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))
#endif


#ifdef __APPLE__

// Put this here so we can compile on MAC
struct mmsghdr {
    struct msghdr msg_hdr;  /* Message header */
    unsigned int  msg_len;  /* Number of received bytes for header */
};

extern int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    int flags, struct timespec *timeout);

#endif


#ifndef EJFAT_BYTESWAP_H
#define EJFAT_BYTESWAP_H

static inline uint16_t bswap_16(uint16_t x) {
    return (x>>8) | (x<<8);
}

static inline uint32_t bswap_32(uint32_t x) {
    return (bswap_16(x&0xffff)<<16) | (bswap_16(x>>16));
}

static inline uint64_t bswap_64(uint64_t x) {
    return (((uint64_t)bswap_32(x&0xffffffffull))<<32) |
           (bswap_32(x>>32));
}

static inline int64_t ts_to_nano(struct timespec ts) {
    return ((int64_t)ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

#endif

    namespace ejfat {

        enum errorCodes {
            RECV_MSG = -1,
            TRUNCATED_MSG = -2,
            BUF_TOO_SMALL = -3,
            OUT_OF_ORDER = -4,
            BAD_FIRST_LAST_BIT = -5,
            OUT_OF_MEM = -6,
            BAD_ARG = -7,
            NO_REASSEMBLY = -8,
            NETWORK_ERROR = -9,
            INTERNAL_ERROR = -10
        };


        // Structure to hold reassembly header info
        typedef struct reHeader_t {
            uint8_t  version  = 2;
            int      reserved = 0; // use 8 of 12 bytes for testing for now
            uint16_t dataId;
            uint32_t offset;
            uint32_t length;
            uint64_t tick;
        } reHeader;


        /**
         * Structure able to hold stats of packet-related quantities for receiving.
         * The contained info relates to the reading/reassembly of a complete buffer.
         */
        typedef struct packetRecvStats_t {
            volatile int64_t  endTime;          /**< End time in nanosec from clock_gettime. */
            volatile int64_t  startTime;        /**< Start time in nanosec from clock_gettime. */
            volatile int64_t  readTime;         /**< Nanosec taken to read (all packets forming) one complete buffer. */

            volatile int64_t droppedPackets;   /**< Number of dropped packets. This cannot be known exactly, only estimate. */
            volatile int64_t acceptedPackets;  /**< Number of packets successfully read. */
            volatile int64_t discardedPackets; /**< Number of packets discarded because reassembly was impossible. */
            volatile int64_t badSrcIdPackets;  /**< Number of packets received with wrong source id. */

            volatile int64_t droppedBytes;     /**< Number of bytes dropped. */
            volatile int64_t acceptedBytes;    /**< Number of bytes successfully read, NOT including RE header. */
            volatile int64_t discardedBytes;   /**< Number of bytes dropped. */

            volatile int64_t droppedBuffers;    /**< Number of ticks/buffers for which no packets showed up.
                                                      Don't think it's possible to measure this in general. */
            volatile int64_t discardedBuffers;  /**< Number of ticks/buffers discarded. */
            volatile int64_t builtBuffers;      /**< Number of ticks/buffers fully reassembled. */

            volatile int cpuPkt;               /**< CPU that thread to read pkts is running on. */
            volatile int cpuBuf;               /**< CPU that thread to read build buffers is running on. */
        } packetRecvStats;


        /**
         * Clear packetRecvStats structure.
         * @param stats pointer to structure to be cleared.
         */
        static void clearStats(packetRecvStats *stats) {
            stats->endTime = 0;
            stats->startTime = 0;
            stats->readTime = 0;

            stats->droppedPackets = 0;
            stats->acceptedPackets = 0;
            stats->discardedPackets = 0;
            stats->badSrcIdPackets = 0;

            stats->droppedBytes = 0;
            stats->acceptedBytes = 0;
            stats->discardedBytes = 0;

            stats->droppedBuffers = 0;
            stats->discardedBuffers = 0;
            stats->builtBuffers = 0;

            stats->cpuPkt = -1;
            stats->cpuBuf = -1;
        }

        /**
         * Clear packetRecvStats structure.
         * @param stats shared pointer to structure to be cleared.
         */
        static void clearStats(std::shared_ptr<packetRecvStats> stats) {
            stats->endTime = 0;
            stats->startTime = 0;
            stats->readTime = 0;

            stats->droppedPackets = 0;
            stats->acceptedPackets = 0;
            stats->discardedPackets = 0;
            stats->badSrcIdPackets = 0;

            stats->droppedBytes = 0;
            stats->acceptedBytes = 0;
            stats->discardedBytes = 0;

            stats->droppedBuffers = 0;
            stats->discardedBuffers = 0;
            stats->builtBuffers = 0;

            stats->cpuPkt = -1;
            stats->cpuBuf = -1;
            stats->cpuBuf = -1;
        }


        static void clearHeader(reHeader *hdr) {
            // initialize mem to 0s
            *hdr = reHeader();
            hdr->version = 2;
        }


        /**
         * Print reHeader structure.
         * @param hdr reHeader structure to be printed.
         */
        static void printReHeader(reHeader *hdr) {
            if (hdr == nullptr) {
                fprintf(stderr, "null pointer\n");
                return;
            }
            fprintf(stderr,  "reHeader: ver %" PRIu8 ", id %" PRIu16 ", off %" PRIu32 ", len %" PRIu32 ", tick %" PRIu64 "\n",
                    hdr->version, hdr->dataId, hdr->offset, hdr->length, hdr->tick);
         }


        /**
         * Print some of the given packetRecvStats structure.
         * @param stats shared pointer to structure to be printed.
         */
        static void printStats(std::shared_ptr<packetRecvStats> const & stats, std::string const & prefix) {
            if (!prefix.empty()) {
                fprintf(stderr, "%s: ", prefix.c_str());
            }
            fprintf(stderr,  "bytes = %" PRId64 ", pkts = %" PRId64 ", dropped bytes = %" PRId64 ", dropped pkts = %" PRId64 ", dropped ticks = %" PRId64 "\n",
                    stats->acceptedBytes, stats->acceptedPackets, stats->droppedBytes,
                    stats->droppedPackets, stats->droppedBuffers);
        }


        /**
         * This routine takes a pointer and prints out (to stderr) the desired number of bytes
         * from the given position, in hex.
         *
         * @param data      data to print out
         * @param bytes     number of bytes to print in hex
         * @param label     a label to print as header
         */
        static void printBytes(const char *data, uint32_t bytes, const char *label) {

            if (label != nullptr) fprintf(stderr, "%s:\n", label);

            if (bytes < 1) {
                fprintf(stderr, "<no bytes to print ...>\n");
                return;
            }

            uint32_t i;
            for (i=0; i < bytes; i++) {
                if (i%8 == 0) {
                    fprintf(stderr, "\n  Buf(%3d - %3d) =  ", (i+1), (i + 8));
                }
                else if (i%4 == 0) {
                    fprintf(stderr, "  ");
                }

                // Accessing buf in this way does not change position or limit of buffer
                fprintf(stderr, "%02x ",( ((int)(*(data + i))) & 0xff)  );
            }

            fprintf(stderr, "\n\n");
        }


        /**
         * This routine takes a file pointer and prints out (to stderr) the desired number of bytes
         * from the given file, in hex.
         *
         * @param data      data to print out
         * @param bytes     number of bytes to print in hex
         * @param label     a label to print as header
         */
        static void printFileBytes(FILE *fp, uint32_t bytes, const char *label) {

            long currentPos = ftell(fp);
            rewind(fp);
            uint8_t byte;


            if (label != nullptr) fprintf(stderr, "%s:\n", label);

            if (bytes < 1) {
                fprintf(stderr, "<no bytes to print ...>\n");
                return;
            }

            uint32_t i;
            for (i=0; i < bytes; i++) {
                if (i%10 == 0) {
                    fprintf(stderr, "\n  Buf(%3d - %3d) =  ", (i+1), (i + 10));
                }
                else if (i%5 == 0) {
                    fprintf(stderr, "  ");
                }

                // Accessing buf in this way does not change position or limit of buffer
                fread(&byte, 1, 1, fp);
                fprintf(stderr, "  0x%02x ", byte);
            }

            fprintf(stderr, "\n\n");
            fseek(fp, currentPos, SEEK_SET);
        }


        /**
         * Parse the load balance header at the start of the given buffer.
         * This routine will, most likely, never be used as this header is
         * stripped off and parsed in the load balancer and the user never
         * sees it.
         *
         * <pre>
         *  protocol 'L:8,B:8,Version:8,Protocol:8,Reserved:16,Entropy:16,Tick:64'
         *
         *  0                   1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |       L       |       B       |    Version    |    Protocol   |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  3               4                   5                   6
         *  2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |              Rsvd             |            Entropy            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  6                                               12
         *  4 5       ...           ...         ...         0 1 2 3 4 5 6 7
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                                                               |
         *  +                              Tick                             +
         *  |                                                               |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * </pre>
         *
         * @param buffer   buffer to parse.
         * @param ll       return 1st byte as char.
         * @param bb       return 2nd byte as char.
         * @param version  return 3rd byte as integer version.
         * @param protocol return 4th byte as integer protocol.
         * @param entropy  return 2 bytes as 16 bit integer entropy.
         * @param tick     return last 8 bytes as 64 bit integer tick.
         */
        static void parseLbHeader(const char* buffer, char* ll, char* bb,
                                  uint32_t* version, uint32_t* protocol,
                                  uint32_t* entropy, uint64_t* tick)
        {
            *ll = buffer[0];
            *bb = buffer[1];
            if ((*ll != 'L') || (*bb != 'B')) {
                throw std::runtime_error("ersap pkt does not start with 'LB'");
            }

            *version  = ((uint32_t)buffer[2] & 0xff);
            *protocol = ((uint32_t)buffer[3] & 0xff);
            *entropy  = ntohs(*((uint16_t *)(&buffer[6]))) & 0xffff;
            *tick     = ntohll(*((uint64_t *)(&buffer[8])));
        }


       /**
        * Parse the load balance header at the start of the given buffer.
        * This is used only to debug the data coming over on the header.
        * Return first 2 bytes as ints, not chars in case there are issues.
        * Throw no exception.
        *
        * @param buffer   buffer to parse.
        * @param ll       return 1st byte as int.
        * @param bb       return 2nd byte as int.
        * @param version  return 3rd byte as integer version.
        * @param protocol return 4th byte as integer protocol.
        * @param entropy  return 2 bytes as 16 bit integer entropy.
        * @param tick     return last 8 bytes as 64 bit integer tick.
        */
        static void readLbHeader(const char* buffer, int* ll, int* bb,
                                          uint32_t* version, uint32_t* protocol,
                                          uint32_t* entropy, uint64_t* tick)
        {
            *ll = (int)buffer[0];
            *bb = (int)buffer[1];

            *version  = ((uint32_t)buffer[2] & 0xff);
            *protocol = ((uint32_t)buffer[3] & 0xff);
            *entropy  = ntohs(*((uint16_t *)(&buffer[6]))) & 0xffff;
            *tick     = ntohll(*((uint64_t *)(&buffer[8])));
        }


        /**
         * Parse the reassembly header at the start of the given buffer.
         * Return parsed values in pointer args.
         * The padding values are ignored and only there to circumvent
         * a bug in the ESNet Load Balancer which otherwise filters out
         * packets with data payload of 0 or 1 byte.
         * This is the old, version 1, RE header.
         *
         * <pre>
         *  protocol 'Version:4, Rsvd:10, First:1, Last:1, Data-ID:16, Offset:32 Padding1:8, Padding2:8'
         *
         *  0                   1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |Version|        Rsvd       |F|L|            Data-ID            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                  UDP Packet Offset                            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                                                               |
         *  +                              Tick                             +
         *  |                                                               |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  *   Padding 1   |   Padding 2   |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * </pre>
         *
         * @param buffer   buffer to parse.
         * @param version  returned version.
         * @param first    returned is-first-packet value.
         * @param last     returned is-last-packet value.
         * @param dataId   returned data source id.
         * @param sequence returned packet sequence number.
         * @param tick     returned tick value, also in LB meta data.
         */
        static void parseReHeaderOld(char* buffer, int* version,
                                  bool *first, bool *last,
                                  uint16_t* dataId, uint32_t* sequence,
                                  uint64_t *tick)
        {
            // Now pull out the component values
            *version = (buffer[0] >> 4) & 0xf;
            *first   = (buffer[1] & 0x02) >> 1;
            *last    =  buffer[1] & 0x01;

            *dataId   = ntohs(*((uint16_t *) (buffer + 2)));
            *sequence = ntohl(*((uint32_t *) (buffer + 4)));
            *tick     = ntohll(*((uint64_t *) (buffer + 8)));
        }


        /**
         * Parse the reassembly header at the start of the given buffer.
         * Return parsed values in pointer args.
         * This is the new, version 2, RE header.
         *
         * <pre>
         *  protocol 'Version:4, Rsvd:12, Data-ID:16, Offset:32, Length:32, Tick:64'
         *
         *  0                   1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |Version|        Rsvd           |            Data-ID            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Offset                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Length                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                                                               |
         *  +                             Tick                              +
         *  |                                                               |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * </pre>
         *
         * @param buffer   buffer to parse.
         * @param version  returned version.
         * @param dataId   returned data source id.
         * @param offset   returned byte offset into buffer of this data payload.
         * @param length   returned total buffer length in bytes of which this packet is a port.
         * @param tick     returned tick value, also in LB meta data.
         */
        static void parseReHeader(const char* buffer, int* version, uint16_t* dataId,
                                  uint32_t* offset, uint32_t* length, uint64_t *tick)
        {
            // Now pull out the component values
            *version = (buffer[0] >> 4) & 0xf;
            *dataId  = ntohs(*((uint16_t *)  (buffer + 2)));
            *offset  = ntohl(*((uint32_t *)  (buffer + 4)));
            *length  = ntohl(*((uint32_t *)  (buffer + 8)));
            *tick    = ntohll(*((uint64_t *) (buffer + 12)));
        }


        /**
         * Parse the reassembly header at the start of the given buffer.
         * Return parsed values in pointer arg.
         * This header limits transmission of a single buffer to less than
         * 2^32 = 4.29496e9 bytes in size.
         * This is the new, version 2, RE header.
         *
         * <pre>
         *  protocol 'Version:4, Rsvd:12, Data-ID:16, Offset:32, Length:32, Tick:64'
         *
         *  0                   1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |Version|        Rsvd           |            Data-ID            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Offset                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Length                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                                                               |
         *  +                             Tick                              +
         *  |                                                               |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * </pre>
         *
         * @param buffer   buffer to parse.
         * @param header   pointer to struct to be filled with RE header info.
         */
        static void parseReHeader(const char* buffer, reHeader* header)
        {
            // Now pull out the component values
            if (header != nullptr) {
                header->version  =                       (buffer[0] >> 4) & 0xf;
                header->reserved =                        buffer[1] & 0xff;
                header->dataId   = ntohs(*((uint16_t *)  (buffer + 2)));
                header->offset   = ntohl(*((uint32_t *)  (buffer + 4)));
                header->length   = ntohl(*((uint32_t *)  (buffer + 8)));
                header->tick     = ntohll(*((uint64_t *) (buffer + 12)));
            }
        }


        /**
        * Parse the reassembly header at the start of the given buffer.
        * Return parsed values in pointer args.
        * This is the new, version 2, RE header.
        *
        * <pre>
        *  protocol 'Version:4, Rsvd:12, Data-ID:16, Offset:32, Length:32, Tick:64'
        *
        *  0                   1                   2                   3
        *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        *  |Version|        Rsvd           |            Data-ID            |
        *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        *  |                         Buffer Offset                         |
        *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        *  |                         Buffer Length                         |
        *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        *  |                                                               |
        *  +                             Tick                              +
        *  |                                                               |
        *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        * </pre>
        *
        * @param buffer buffer to parse.
        * @param offset returned byte offset into buffer of this data payload.
        * @param length returned total buffer length in bytes of which this packet is a port.
        * @param tick   returned tick value, also in LB meta data.
        */
        static void parseReHeader(const char* buffer, uint32_t* offset, uint32_t *length, uint64_t *tick)
        {
            *offset = ntohl(*((uint32_t *)  (buffer + 4)));
            *length = ntohl(*((uint32_t *)  (buffer + 8)));
            *tick   = ntohll(*((uint64_t *) (buffer + 12)));
        }


        /**
         * Parse the reassembly header at the start of the given buffer.
         * Return parsed values in pointer arg and array.
         * This is the new, version 2, RE header.
         *
         * <pre>
         *  protocol 'Version:4, Rsvd:12, Data-ID:16, Offset:32, Length:32, Tick:64'
         *
         *  0                   1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |Version|        Rsvd           |            Data-ID            |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Offset                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                         Buffer Length                         |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |                                                               |
         *  +                             Tick                              +
         *  |                                                               |
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * </pre>
         *
         * @param buffer    buffer to parse.
         * @param intArray  array of ints in which version, dataId, offset,
         *                  and length are returned, in that order.
         * @param arraySize number of elements in intArray
         * @param tick      returned tick value, also in LB meta data.
         */
        static void parseReHeader(const char* buffer, uint32_t* intArray, int arraySize, uint64_t *tick)
        {
            if (intArray != nullptr && arraySize > 4) {
                intArray[0] = (buffer[0] >> 4) & 0xf;  // version
                intArray[1] = ntohs(*((uint16_t *) (buffer + 2))); // data ID
                intArray[2] = ntohl(*((uint32_t *) (buffer + 4))); // offset
                intArray[3] = ntohl(*((uint32_t *) (buffer + 8))); // length
            }
            *tick = ntohll(*((uint64_t *) (buffer + 12)));
        }

        /**
         * Parse the reassembly header at the start of the given buffer.
         * Return parsed values in array. Used in packetBlasteeFast to
         * return only needed data.
         * This is the new, version 2, RE header.
         *
         * @param buffer    buffer to parse.
         * @param intArray  array of ints in which offset, length, and tick are returned.
         * @param index     where in intArray to start writing.
         * @param tick      returned tick value.
         */
        static void parseReHeaderFast(const char* buffer, uint32_t* intArray, int index, uint64_t *tick)
        {
            intArray[index]     = ntohl(*((uint32_t *) (buffer + 4))); // offset
            intArray[index + 1] = ntohl(*((uint32_t *) (buffer + 8))); // length

            *tick = ntohll(*((uint64_t *) (buffer + 12)));
            // store tick for later
            *((uint64_t *) (&(intArray[index + 2]))) = *tick;
        }


        //-----------------------------------------------------------------------
        // Be sure to print to stderr as programs may pipe data to stdout!!!
        //-----------------------------------------------------------------------


        /**
         * <p>
         * Routine to read a single UDP packet into a single buffer.
         * The reassembly header will be parsed and its data retrieved.
         * This uses the new, version 2, RE header.
         * </p>
         *
         * It's the responsibility of the caller to have at least enough space in the
         * buffer for 1 MTU of data. Otherwise, the caller risks truncating the data
         * of a packet and having error code of TRUNCATED_MSG returned.
         *
         *
         * @param dataBuf   buffer in which to store actual data read (not any headers).
         * @param bufLen    available bytes in dataBuf in which to safely write.
         * @param udpSocket UDP socket to read.
         * @param tick      to be filled with tick from RE header.
         * @param length    to be filled with buffer length from RE header.
         * @param offset    to be filled with buffer offset from RE header.
         * @param dataId    to be filled with data id read RE header.
         * @param version   to be filled with version read RE header.
         * @param last      to be filled with "last" bit id from RE header,
         *                  indicating the last packet in a series used to send data.
         * @param debug     turn debug printout on & off.
         *
         * @return number of data (not headers!) bytes read from packet.
         *         If there's an error in recvfrom, it will return RECV_MSG.
         *         If there is not enough data to contain a header, it will return INTERNAL_ERROR.
         *         If there is not enough room in dataBuf to hold incoming data, it will return BUF_TOO_SMALL.
         */
        static ssize_t readPacketRecvFrom(char *dataBuf, size_t bufLen, int udpSocket,
                                      uint64_t *tick, uint32_t *length, uint32_t* offset,
                                      uint16_t* dataId, int* version, bool debug) {

            // Storage for packet
            char pkt[65536];

            ssize_t bytesRead = recvfrom(udpSocket, pkt, 65536, 0, nullptr, nullptr);
            if (bytesRead < 0) {
                if (debug) fprintf(stderr, "recvmsg() failed: %s\n", strerror(errno));
                return(RECV_MSG);
            }
            else if (bytesRead < HEADER_BYTES) {
                fprintf(stderr, "recvfrom(): not enough data to contain a header on read\n");
                return(INTERNAL_ERROR);
            }

            if (bufLen < bytesRead) {
                return(BUF_TOO_SMALL);
            }

            // Parse header
            parseReHeader(pkt, version, dataId, offset, length, tick);

            // Copy datq
            ssize_t dataBytes = bytesRead - HEADER_BYTES;
            memcpy(dataBuf, pkt + HEADER_BYTES, dataBytes);

            return dataBytes;
        }



        /**
         * <p>
         * Routine to read a single UDP packet into a single buffer.
         * The reassembly header will be parsed and its data retrieved.
         * This is for the old, version 1, RE header.
         * </p>
         *
         * It's the responsibility of the caller to have at least enough space in the
         * buffer for 1 MTU of data. Otherwise, the caller risks truncating the data
         * of a packet and having error code of TRUNCATED_MSG returned.
         *
         *
         * @param dataBuf   buffer in which to store actual data read (not any headers).
         * @param bufLen    available bytes in dataBuf in which to safely write.
         * @param udpSocket UDP socket to read.
         * @param tick      to be filled with tick from RE header.
         * @param sequence  to be filled with packet sequence from RE header.
         * @param dataId    to be filled with data id read RE header.
         * @param version   to be filled with version read RE header.
         * @param first     to be filled with "first" bit from RE header,
         *                  indicating the first packet in a series used to send data.
         * @param last      to be filled with "last" bit id from RE header,
         *                  indicating the last packet in a series used to send data.
         * @param debug     turn debug printout on & off.
         *
         * @return number of data (not headers!) bytes read from packet.
         *         If there's an error in recvfrom, it will return RECV_MSG.
         *         If there is not enough data to contain a header, it will return INTERNAL_ERROR.
         *         If there is not enough room in dataBuf to hold incoming data, it will return BUF_TOO_SMALL.
         */
        static ssize_t readPacketRecvFrom(char *dataBuf, size_t bufLen, int udpSocket,
                                      uint64_t *tick, uint32_t* sequence, uint16_t* dataId, int* version,
                                      bool *first, bool *last, bool debug) {

            // Storage for packet
            char pkt[65536];

            ssize_t bytesRead = recvfrom(udpSocket, pkt, 65536, 0, NULL, NULL);
            if (bytesRead < 0) {
                if (debug) fprintf(stderr, "recvmsg() failed: %s\n", strerror(errno));
                return(RECV_MSG);
            }
            else if (bytesRead < HEADER_BYTES_OLD) {
                fprintf(stderr, "recvfrom(): not enough data to contain a header on read\n");
                return(INTERNAL_ERROR);
            }

            if (bufLen < bytesRead) {
                return(BUF_TOO_SMALL);
            }

            // Parse header
            parseReHeaderOld(pkt, version, first, last, dataId, sequence, tick);

            // Copy datq
            ssize_t dataBytes = bytesRead - HEADER_BYTES_OLD;
            memcpy(dataBuf, pkt + HEADER_BYTES_OLD, dataBytes);

            return dataBytes;
        }


        /**
         * For the older code (version 1 RE header), a map is used to deal with
         * out-fo-order packets. This methods clears that map.
         * @param outOfOrderPackets map containing out-of-order UDP packets.
         */
        static void clearMap(std::map<uint32_t, std::tuple<char *, uint32_t, bool, bool>> & outOfOrderPackets) {
            if (outOfOrderPackets.empty()) return;

            for (const auto& n : outOfOrderPackets) {
                // Free allocated buffer holding packet
                free(std::get<0>(n.second));
            }
            outOfOrderPackets.clear();
        }


        /**
         * <p>
         * Assemble incoming packets into the given buffer.
         * It will read entire buffer or return an error.
         * Will work best on small / reasonably sized buffers.
         * This routine allows for out-of-order packets if they don't cross tick boundaries.
         * This assumes the new, version 2, RE header.
         * Data can only come from 1 source, which is returned in the dataId value-result arg.
         * Data from a source other than that of the first packet will be ignored.
         * This differs from getCompletePacketizedBuffer in that a boolean controls
         * whether to take time stats or not - which can be expensive.
         * </p>
         *
         * <p>
         * If the given tick value is <b>NOT</b> 0xffffffffffffffff, then it is the next expected tick.
         * And in this case, this method makes an attempt at figuring out how many buffers and packets
         * were dropped using tickPrescale.
         * </p>
         *
         * <p>
         * A note on statistics. The raw counts are <b>ADDED</b> to what's already
         * in the stats structure. It's up to the user to clear stats before calling
         * this method if desired.
         * </p>
         *
         * @param dataBuf           place to store assembled packets.
         * @param bufLen            byte length of dataBuf.
         * @param udpSocket         UDP socket to read.
         * @param debug             turn debug printout on & off.
         * @param tick              value-result parameter which gives the next expected tick
         *                          and returns the tick that was built. If it's passed in as
         *                          0xffff ffff ffff ffff, then ticks are coming in no particular order.
         * @param dataId            to be filled with data ID from RE header (can be nullptr).
         * @param stats             to be filled packet statistics.
         * @param tickPrescale      add to current tick to get next expected tick.
         * @param takeTimeStats     if true, store data of time when first packet arrived.
         *
         * @return total data bytes read (does not include RE header).
         *         If there error in recvfrom, return RECV_MSG.
         *         If buffer is too small to contain reassembled data, return BUF_TOO_SMALL.
         *         If a pkt contains too little data, return INTERNAL_ERROR.
         */
        static ssize_t getCompletePacketizedBufferTime(char* dataBuf, size_t bufLen, int udpSocket,
                                                       bool debug, uint64_t *tick, uint16_t *dataId,
                                                       std::shared_ptr<packetRecvStats> stats,
                                                       uint32_t tickPrescale, bool takeTimeStats) {

            uint64_t prevTick = UINT_MAX;
            uint64_t expectedTick = *tick;
            uint64_t packetTick;

            uint32_t offset, length = 0, prevLength, pktCount;
            uint32_t totalPkts = 0, prevTotalPkts;

            bool dumpTick = false;
            bool veryFirstRead = true;

            int  version;
            uint16_t packetDataId, srcId;
            ssize_t dataBytes, bytesRead, totalBytesRead = 0;

            // stats
            bool knowExpectedTick = expectedTick != 0xffffffffffffffffL;
            bool takeStats = stats != nullptr;
            takeTimeStats &= takeStats;

            int64_t discardedPackets = 0, discardedBytes = 0, discardedBufs = 0;

            // Storage for packet
            char pkt[65536];

            if (debug && takeStats) fprintf(stderr, "getCompletePacketizedBuffer: buf size = %lu, take stats = %d, %p\n",
                                            bufLen, takeStats, stats.get());

            while (true) {

                if (veryFirstRead) {
                    totalBytesRead = 0;
                    pktCount = 0;
                }

                // Read UDP packet
                bytesRead = recvfrom(udpSocket, pkt, 65536, 0, nullptr, nullptr);
                if (bytesRead < 0) {
                    if (debug) fprintf(stderr, "getCompletePacketizedBuffer: recvfrom failed: %s\n", strerror(errno));
                    return (RECV_MSG);
                }
                else if (bytesRead < HEADER_BYTES) {
                    if (debug) fprintf(stderr, "getCompletePacketizedBuffer: packet does not contain not enough data\n");
                    return (INTERNAL_ERROR);
                }

                if (veryFirstRead && takeTimeStats) {
                    struct timespec start;
                    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
                    stats->startTime = ts_to_nano(start);
                }

                dataBytes = bytesRead - HEADER_BYTES;

                // Parse header
                prevLength = length;
                prevTotalPkts = totalPkts;
                parseReHeader(pkt, &version, &packetDataId, &offset, &length, &packetTick);

                if (veryFirstRead) {
                    // record data id of first packet of buffer
                    srcId = packetDataId;
                    // guess at # of packets
                    totalPkts = (length + (dataBytes - 1))/dataBytes;
                }
                else if (packetDataId != srcId) {
                    // different data source, reject this packet
                    if (takeStats) {
                        stats->badSrcIdPackets++;
                    }
                    if (debug) fprintf(stderr, "getCompletePacketizedBuffer: reject pkt from src %hu\n", packetDataId);
                    continue;
                }

                // The following if-else is built on the idea that we start with a packet that has offset = 0.
                // While it's true that, if missing, it may be out-of-order and will show up eventually,
                // experience has shown that this almost never happens. Thus, for efficiency's sake,
                // we automatically dump any tick whose first packet does not show up FIRST.

                // Probably, where this most often gets us into trouble is if the first packet of the next
                // tick/event shows up just before the last pkt of the previous tick. In that case, this logic
                // just dumps all the previous info even if last pkt comes a little late.

                // Worst case scenario is if the pkts of 2 events are interleaved.
                // Then the number of dumped packets, bytes, and events will be grossly over-counted.

                // To do a complete job of trying to track out-of-order packets, we would need to
                // simultaneously keep track of packets from multiple ticks. This small routine
                // would need to keep state - greatly complicating things. So skip that here.
                // Such work is done in the packetBlasteeFull.cc program.

                // In general, tracking dropped pkts/events/data will always be guess work unless
                // we know exactly what we're supposed to be receiving.
                // Thus, normally we cannot know how many complete events were dropped.
                // When deciding to drop an event due to incomplete packets, we attempt to
                // guesstimate the # of packets.


                // If we get packet from new tick ...
                if (packetTick != prevTick) {
                    // If we're here, either we've just read the very first legitimate packet,
                    // or we've dropped some packets and advanced to another tick.

                    if (offset != 0) {
                        // Already have trouble, looks like we dropped the first packet of this new tick,
                        // and possibly others after it.
                        // So go ahead and dump the rest of the tick in an effort to keep any high data rate.
                        if (debug)
                            fprintf(stderr, "Skip pkt from id %hu, %" PRIu64 " - %u, expected seq 0\n",
                                    packetDataId, packetTick, offset);

                        // Go back to read beginning of buffer
                        veryFirstRead = true;
                        dumpTick = true;
                        prevTick = packetTick;

                        // Stats. Guess at # of packets.
                        discardedPackets += totalPkts;
                        discardedBytes += length;
                        discardedBufs++;

                        continue;
                    }

                    if (!veryFirstRead) {
                        // The last tick's buffer was not fully contructed
                        // before this new tick showed up!
                        if (debug) fprintf(stderr, "Discard tick %" PRIu64 "\n", prevTick);

                        pktCount = 0;
                        totalBytesRead = 0;
                        srcId = packetDataId;

                        // We discard previous tick/event
                        discardedPackets += prevTotalPkts;
                        discardedBytes += prevLength;
                        discardedBufs++;
                    }

                    // If here, new tick/buffer, offset = 0.
                    // There's a chance we can construct a full buffer.
                    // Overwrite everything we saved from previous tick.
                    dumpTick = false;
                }
                else if (dumpTick) {
                    // Same as last tick.
                    // If here, we missed beginning pkt(s) for this buf so we're dumping whole tick
                    veryFirstRead = true;

                    if (debug) fprintf(stderr, "Dump pkt from id %hu, %" PRIu64 " - %u, expected seq 0\n",
                                       packetDataId, packetTick, offset);
                    continue;
                }

                // Check to see if there's room to write data into provided buffer
                if (offset + dataBytes > bufLen) {
                    if (debug) fprintf(stderr, "getCompletePacketizedBuffer: buffer too small to hold data\n");
                    return (BUF_TOO_SMALL);
                }

                // Copy data into buf at correct location (provided by RE header)
                memcpy(dataBuf + offset, pkt + HEADER_BYTES, dataBytes);

                totalBytesRead += dataBytes;
                veryFirstRead = false;
                prevTick = packetTick;
                pktCount++;

                // If we've written all data to this buf ...
                if (totalBytesRead >= length) {
                    // Done
                    *tick = packetTick;
                    if (dataId != nullptr) *dataId = packetDataId;

                    // Keep some stats
                    if (takeStats) {
                        if (knowExpectedTick) {
                            int64_t diff = packetTick - expectedTick;
                            diff = (diff < 0) ? -diff : diff;
                            int64_t droppedTicks = diff / tickPrescale;

                            // In this case, it includes the discarded bufs (which it should not)
                            stats->droppedBuffers   += droppedTicks; // estimate

                            // This works if all the buffers coming in are exactly the same size.
                            // If they're not, then the # of packets of this buffer
                            // is used to guess at how many packets were dropped for the dropped tick(s).
                            // Again, this includes discarded packets which it should not.
                            stats->droppedPackets += droppedTicks * pktCount;
                        }

                        stats->acceptedBytes    += totalBytesRead;
                        stats->acceptedPackets  += pktCount;

                        stats->discardedBytes   += discardedBytes;
                        stats->discardedPackets += discardedPackets;
                        stats->discardedBuffers += discardedBufs;
                    }

                    break;
                }
            }

            return totalBytesRead;
        }



        /**
        * <p>
        * Assemble incoming packets into the given buffer.
        * It will read entire buffer or return an error.
        * Will work best on small / reasonably sized buffers.
        * This routine allows for out-of-order packets if they don't cross tick boundaries.
        * This assumes the new, version 2, RE header.
        * Data can only come from 1 source, which is returned in the dataId value-result arg.
        * Data from a source other than that of the first packet will be ignored.
        * </p>
        *
        * <p>
        * If the given tick value is <b>NOT</b> 0xffffffffffffffff, then it is the next expected tick.
        * And in this case, this method makes an attempt at figuring out how many buffers and packets
        * were dropped using tickPrescale.
        * </p>
        *
        * <p>
        * A note on statistics. The raw counts are <b>ADDED</b> to what's already
        * in the stats structure. It's up to the user to clear stats before calling
        * this method if desired.
        * </p>
        *
        * @param dataBuf           place to store assembled packets.
        * @param bufLen            byte length of dataBuf.
        * @param udpSocket         UDP socket to read.
        * @param debug             turn debug printout on & off.
        * @param tick              value-result parameter which gives the next expected tick
        *                          and returns the tick that was built. If it's passed in as
        *                          0xffff ffff ffff ffff, then ticks are coming in no particular order.
        * @param dataId            to be filled with data ID from RE header (can be nullptr).
        * @param stats             to be filled packet statistics.
        * @param tickPrescale      add to current tick to get next expected tick.
        *
        * @return total data bytes read (does not include RE header).
        *         If there error in recvfrom, return RECV_MSG.
        *         If buffer is too small to contain reassembled data, return BUF_TOO_SMALL.
        *         If a pkt contains too little data, return INTERNAL_ERROR.
        */
        static ssize_t getCompletePacketizedBuffer(char* dataBuf, size_t bufLen, int udpSocket,
                                                   bool debug, uint64_t *tick, uint16_t *dataId,
                                                   std::shared_ptr<packetRecvStats> stats,
                                                   uint32_t tickPrescale) {

            return getCompletePacketizedBufferTime(dataBuf, bufLen, udpSocket, debug,
                                                   tick, dataId, stats, tickPrescale, false);
        }



////////////////////////


        /**
         * <p>
         * Assemble incoming packets into a buffer that may be provided by the caller.
         * If it's null or if it ends up being too small,
         * the buffer will be created / reallocated and returned by this routine.
         * Will work best on small / reasonable sized buffers.
         * A internally allocated buffer is guaranteed to fit all reassembled data.
         * It's the responsibility of the user to free any buffer that is internally allocated.
         * If user gives nullptr for buffer and 0 for buffer length, buf defaults to internally
         * allocated 100kB. If the user provides a buffer < 9000 bytes, a larger one will be allocated.
         * This routine will read entire buffer or return an error.
         * </p>
         *
         * <p>
         * How does the caller determine if a buffer was (re)allocated in this routine?
         * If the returned buffer pointer is different than that supplied or if the supplied
         * buffer length is smaller than that returned, then the buffer was allocated
         * internally and must be freed by the caller.
         * </p>
         *
         * <p>
         * This routine allows for out-of-order packets if they don't cross tick boundaries.
         * This assumes the new, version 2, RE header.
         * Data can only come from 1 source, which is returned in the dataId value-result arg.
         * Data from a source other than that of the first packet will be ignored.
         * </p>
         *
         * <p>
         * If the given tick value is <b>NOT</b> 0xffffffffffffffff, then it is the next expected tick.
         * And in this case, this method makes an attempt at figuring out how many buffers and packets
         * were dropped using tickPrescale.
         * </p>
         *
         * <p>
         * A note on statistics. The raw counts are <b>ADDED</b> to what's already
         * in the stats structure. It's up to the user to clear stats before calling
         * this method if desired.
         * </p>
         *
         * @param dataBufAlloc      value-result pointer to data buffer.
         *                          User-given buffer to store assembled packets or buffer
         *                          (re)allocated by this routine. If (re)allocated internally,
         *                          CALLER MUST FREE THIS BUFFER!
         * @param pBufLen           value-result pointer to byte length of dataBuf.
         *                          Buffer length of supplied buffer. If no buffer supplied, or if buffer
         *                          is (re)allocated, the length of the new buffer is passed back to caller.
         *                          In all cases, the buffer length is returned.
         * @param udpSocket         UDP socket to read.
         * @param debug             turn debug printout on & off.
         * @param tick              value-result parameter which gives the next expected tick
         *                          and returns the tick that was built. If it's passed in as
         *                          0xffff ffff ffff ffff, then ticks are coming in no particular order.
         * @param dataId            to be filled with data ID from RE header (can be nullptr).
         * @param stats             to be filled packet statistics.
         * @param tickPrescale      add to current tick to get next expected tick.
         *
         * @return total data bytes read (does not include RE header).
         *         If dataBufAlloc or bufLenPtr are null, return BAD_ARG.
         *         If error in recvfrom, return RECV_MSG.
         *         If cannot allocate memory, return OUT_OF_MEM.
         *         If on a read &lt; HEADER_BYTES data returned, return INTERNAL_ERROR.
         */
        static ssize_t getCompleteAllocatedBuffer(char** dataBufAlloc, size_t *pBufLen, int udpSocket,
                                                  bool debug, uint64_t *tick, uint16_t *dataId,
                                                  std::shared_ptr<packetRecvStats> stats,
                                                  uint32_t tickPrescale) {

            if (pBufLen == nullptr || dataBufAlloc == nullptr) {
                fprintf(stderr, "getCompletePacketizedBufferNew: null arg(s)\n");
                return BAD_ARG;
            }

            // Length of buf passed in, or suggested length for this routine to allocate
            size_t bufLen = *pBufLen;
            bool allocateBuf = false;

            // If we need to allocate buffer
            if (*dataBufAlloc == nullptr) {
                if (bufLen == 0) {
                    // Use default len of 100kB
                    bufLen = 100000;
                }
                else if (bufLen < MAX_EJFAT_MTU) {
                    // Make sure we can at least read one JUMBO packet
                    bufLen = MAX_EJFAT_MTU;
                }
                allocateBuf = true;
            }
            else {
                if (bufLen < MAX_EJFAT_MTU) {
                    bufLen = MAX_EJFAT_MTU;
                    allocateBuf = true;
                }
            }

            char *dataBuf = *dataBufAlloc;
            if (allocateBuf) {
                dataBuf = (char *) malloc(bufLen);
                if (dataBuf == nullptr) {
                    return OUT_OF_MEM;
                }
            }


            uint64_t prevTick = UINT_MAX;
            uint64_t expectedTick = *tick;
            uint64_t packetTick;

            uint32_t offset, length = 0, prevLength, pktCount;
            uint32_t totalPkts = 0, prevTotalPkts;

            bool dumpTick = false;
            bool veryFirstRead = true;

            int  version;
            uint16_t packetDataId, srcId;
            ssize_t dataBytes, bytesRead, totalBytesRead = 0;

            // stats
            bool knowExpectedTick = expectedTick != 0xffffffffffffffffL;
            bool takeStats = stats != nullptr;
            int64_t discardedPackets = 0, discardedBytes = 0, discardedBufs = 0;

            // Storage for packet
            char pkt[65536];

            if (debug && takeStats) fprintf(stderr, "getCompleteAllocatedBuffer: buf size = %lu, take stats = %d, %p\n",
                                            bufLen, takeStats, stats.get());

            while (true) {

                if (veryFirstRead) {
                    totalBytesRead = 0;
                    pktCount = 0;
                }

                // Read UDP packet
                bytesRead = recvfrom(udpSocket, pkt, 65536, 0, nullptr, nullptr);
                if (bytesRead < 0) {
                    if (debug) fprintf(stderr, "getCompleteAllocatedBuffer: recvmsg failed: %s\n", strerror(errno));
                    if (allocateBuf) {
                        free(dataBuf);
                    }
                    return (RECV_MSG);
                }
                else if (bytesRead < HEADER_BYTES) {
                    if (debug) fprintf(stderr, "getCompleteAllocatedBuffer: packet does not contain not enough data\n");
                    if (allocateBuf) {
                        free(dataBuf);
                    }
                    return (INTERNAL_ERROR);
                }
                dataBytes = bytesRead - HEADER_BYTES;

                // Parse header
                prevLength = length;
                prevTotalPkts = totalPkts;
                parseReHeader(pkt, &version, &packetDataId, &offset, &length, &packetTick);
                if (veryFirstRead) {
                    // record data id of first packet of buffer
                    srcId = packetDataId;
                    // guess at # of packets
                    totalPkts = (length + (dataBytes - 1))/dataBytes;
                }
                else if (packetDataId != srcId) {
                    // different data source, reject this packet
                    if (takeStats) {
                        stats->badSrcIdPackets++;
                    }
                    if (debug) fprintf(stderr, "getCompleteAllocatedBuffer: reject pkt from src %hu\n", packetDataId);
                    continue;
                }

                // The following if-else is built on the idea that we start with a packet that has offset = 0.
                // While it's true that, if missing, it may be out-of-order and will show up eventually,
                // experience has shown that this almost never happens. Thus, for efficiency's sake,
                // we automatically dump any tick whose first packet does not show up FIRST.

                // Probably, where this most often gets us into trouble is if the first packet of the next
                // tick/event shows up just before the last pkt of the previous tick. In that case, this logic
                // just dumps all the previous info even if last pkt comes a little late.

                // Worst case scenario is if the pkts of 2 events are interleaved.
                // Then the number of dumped packets, bytes, and events will be grossly over-counted.

                // To do a complete job of trying to track out-of-order packets, we would need to
                // simultaneously keep track of packets from multiple ticks. This small routine
                // would need to keep state - greatly complicating things. So skip that here.
                // Such work is done in the packetBlasteeFull.cc program.

                // In general, tracking dropped pkts/events/data will always be guess work unless
                // we know exactly what we're supposed to be receiving.
                // Thus, normally we cannot know how many complete events were dropped.
                // When deciding to drop an event due to incomplete packets, we attempt to
                // guesstimate the # of packets.


                // If we get packet from new tick ...
                if (packetTick != prevTick) {
                    // If we're here, either we've just read the very first legitimate packet,
                    // or we've dropped some packets and advanced to another tick.

                    if (offset != 0) {
                        // Already have trouble, looks like we dropped the first packet of this new tick,
                        // and possibly others after it.
                        // So go ahead and dump the rest of the tick in an effort to keep any high data rate.
                        if (debug)
                            fprintf(stderr, "Skip pkt from id %hu, %" PRIu64 " - %u, expected seq 0\n",
                                    packetDataId, packetTick, offset);

                        // Go back to read beginning of buffer
                        veryFirstRead = true;
                        dumpTick = true;
                        prevTick = packetTick;

                        // Stats. Guess at # of packets.
                        discardedPackets += totalPkts;
                        discardedBytes += length;
                        discardedBufs++;

                        continue;
                    }

                    if (!veryFirstRead) {
                        // The last tick's buffer was not fully contructed
                        // before this new tick showed up!
                        if (debug) fprintf(stderr, "Discard tick %" PRIu64 "\n", prevTick);

                        pktCount = 0;
                        totalBytesRead = 0;
                        srcId = packetDataId;

                        // We discard previous tick/event
                        discardedPackets += prevTotalPkts;
                        discardedBytes += prevLength;
                        discardedBufs++;
                    }

                    // If here, new tick/buffer, offset = 0.
                    // There's a chance we can construct a full buffer.
                    // Overwrite everything we saved from previous tick.
                    dumpTick = false;
                }
                else if (dumpTick) {
                    // Same as last tick.
                    // If here, we missed beginning pkt(s) for this buf so we're dumping whole tick
                    veryFirstRead = true;

                    if (debug) fprintf(stderr, "Dump pkt from id %hu, %" PRIu64 " - %u, expected seq 0\n",
                                       packetDataId, packetTick, offset);
                    continue;
                }

                // Check to see if there's room to write data into provided buffer
                if (offset + dataBytes > bufLen) {
                    // Not enough room! Double buffer size here
                    bufLen *= 2;
                    // realloc copies data over if necessary
                    dataBuf = (char *)realloc(dataBuf, bufLen);
                    if (dataBuf == nullptr) {
                        if (allocateBuf) {
                            free(dataBuf);
                        }
                        return OUT_OF_MEM;
                    }
                    allocateBuf = true;
                    if (debug) fprintf(stderr, "getCompleteAllocatedBuffer: reallocated buffer to %zu bytes\n", bufLen);
                }

                // Copy data into buf at correct location (provided by RE header)
                memcpy(dataBuf + offset, pkt + HEADER_BYTES, dataBytes);

                totalBytesRead += dataBytes;
                veryFirstRead = false;
                prevTick = packetTick;
                pktCount++;

                // If we've written all data to this buf ...
                if (totalBytesRead >= length) {
                    // Done
                    *tick = packetTick;
                    if (dataId != nullptr) *dataId = packetDataId;
                    *pBufLen = bufLen;
                    *dataBufAlloc = dataBuf;

                    // Keep some stats
                    if (takeStats) {
                        if (knowExpectedTick) {
                            int64_t diff = packetTick - expectedTick;
                            diff = (diff < 0) ? -diff : diff;
                            int64_t droppedTicks = diff / tickPrescale;

                            // In this case, it includes the discarded bufs (which it should not)
                            stats->droppedBuffers += droppedTicks; // estimate

                            // This works if all the buffers coming in are exactly the same size.
                            // If they're not, then the # of packets of this buffer
                            // is used to guess at how many packets were dropped for the dropped tick(s).
                            // Again, this includes discarded packets which it should not.
                            stats->droppedPackets += droppedTicks * pktCount;
                        }

                        stats->acceptedBytes    += totalBytesRead;
                        stats->acceptedPackets  += pktCount;

                        stats->discardedBytes   += discardedBytes;
                        stats->discardedPackets += discardedPackets;
                        stats->discardedBuffers += discardedBufs;
                    }

                    break;
                }
            }

            return totalBytesRead;
        }


        /**
         * <p>
         * Assemble incoming packets into the given buffer - not necessarily the entirety of the data.
         * This routine is best for reading a very large buffer, or a file, for the purpose
         * of writing it on the receiving end - something too big to hold in RAM.
         * Transfer this packet by packet.
         * It will return when the buffer has &lt; 9000 bytes of free space left,
         * or when the last packet has been read.
         * It allows for multiple calls to read the buffer in stages.
         * This assumes the new, version 2, RE header.
         * </p>
         *
         * Note, this routine does not attempt any error recovery since it was designed to be called in a
         * loop.
         *
         * @param dataBuf           place to store assembled packets, must be &gt; 9000 bytes.
         * @param bufLen            byte length of dataBuf.
         * @param udpSocket         UDP socket to read.
         * @param debug             turn debug printout on & off.
         * @param veryFirstRead     this is the very first time data will be read for a sequence of same-tick packets.
         * @param last              to be filled with true if it's the last packet of fully reassembled buffer.
         * @param srcId             to be filled with the dataId from RE header of the very first packet.
         * @param tick              to be filled with tick from RE header.
         * @param offset            value-result parameter which gives the next expected offset to be
         *                          read from RE header and returns its updated value for the next
         *                          related call.
         * @param packetCount       pointer to int which get filled with the number of in-order packets read.
         *
         * @return total bytes read.
         *         If buffer cannot be reassembled (100 pkts from wrong tick/id), return NO_REASSEMBLY.
         *         If error in recvmsg/recvfrom, return RECV_MSG.
         *         If packet is out of order, return OUT_OF_ORDER.
         *         If packet &lt; header size, return INTERNAL_ERROR.
         */
        static ssize_t getPacketizedBuffer(char* dataBuf, size_t bufLen, int udpSocket,
                                           bool debug, bool veryFirstRead, bool *last,
                                           uint16_t *srcId, uint64_t *tick, uint32_t *offset,
                                           uint32_t *packetCount) {

            uint64_t packetTick;
            uint64_t firstTick = *tick;

            int  version, rejectedPkt = 0;
            uint16_t packetDataId;
            uint16_t firstSrcId = *srcId;

            uint32_t length, pktCount = 0;
            ssize_t dataBytes, bytesRead, totalBytesRead = 0;
            size_t remainingLen = bufLen;

            // The offset in the completely reassembled buffer is not useful
            // (which is what we get from the RE header).
            // We're working on a part so the local offset associated with offset must be 0.
            uint32_t offsetLocal = 0;
            uint32_t packetOffset, nextOffset, firstOffset;

            // Storage for packet
            char pkt[65536];

            // The offset arg is the next, expected offset
            firstOffset = nextOffset = *offset;

            if (debug) fprintf(stderr, "getPacketizedBuffer: remainingLen = %lu\n", remainingLen);

            while (true) {

                if (remainingLen < MAX_EJFAT_MTU) {
                    // Not enough room to read a full, jumbo packet so move on
                    break;
                }

                // Read in one packet
                bytesRead = recvfrom(udpSocket, pkt, 65536, 0, nullptr, nullptr);
                if (bytesRead < 0) {
                    if (debug) fprintf(stderr, "recvmsg() failed: %s\n", strerror(errno));
                    return (RECV_MSG);
                }
                else if (bytesRead < HEADER_BYTES) {
                    fprintf(stderr, "recvfrom(): not enough data to contain a header on read\n");
                    return (INTERNAL_ERROR);
                }
                dataBytes = bytesRead - HEADER_BYTES;

                // Parse header
                parseReHeader(pkt, &version, &packetDataId, &packetOffset, &length, &packetTick);

                if (veryFirstRead) {
                    // First check to see if we get expected offset (0 at very start)
                    if (packetOffset != 0) {
                        // Packet(s) was skipped, so return error since this method has no error recovery
                        return (OUT_OF_ORDER);
                    }

                    // Record data id of first packet so we can track between calls
                    firstSrcId = packetDataId;
                    // And pass that to caller
                    *srcId = firstSrcId;
                    // Record the tick we're working on so we can track between calls
                    firstTick = packetTick;
                }
                else {
                    // Check to see if we get expected offset
                    if (packetOffset != nextOffset) {
                        return (OUT_OF_ORDER);
                    }

                    // If different data source or tick, reject this packet, look at next one
                    if ((packetDataId != firstSrcId) || (packetTick != firstTick)) {
                        if (++rejectedPkt >= 100) {
                            // Return error if we've received at least 100 irrelevant packets
                            return (NO_REASSEMBLY);
                        }
                        continue;
                    }
                }

                // Take care of initial offset so we write into proper location
                offsetLocal = packetOffset - firstOffset;

                // There will be room to write this as we checked the offset to make sure it's sequential
                // and since there's room for another jumbo frame.

                // Copy data into buf at correct location
                memcpy(dataBuf + offsetLocal, pkt + HEADER_BYTES, dataBytes);

                totalBytesRead += dataBytes;
                veryFirstRead = false;
                remainingLen -= dataBytes;
                pktCount++;
                nextOffset = packetOffset + dataBytes;

                if (nextOffset >= length) {
                    // We're done reading pkts for full reassembly
                    *last = true;
                    break;
                }
            }

            if (debug) fprintf(stderr, "getPacketizedBuffer: passing next offset = %u\n\n", nextOffset);

            *tick = packetTick;
            *packetCount = pktCount;
            *offset = nextOffset;

            return totalBytesRead;
        }


        /**
         * Routine to process the data. In this case, write it to file pointer (file or stdout)
         *
         * @param dataBuf buffer filled with data.
         * @param nBytes  number of valid bytes.
         * @param fp      file pointer.
         * @return error code of 0 means OK. If there is an error, programs exits.
         */
        static int writeBuffer(const char* dataBuf, size_t nBytes, FILE* fp, bool debug) {

            size_t n, totalWritten = 0;

            while (true) {
                n = fwrite(dataBuf, 1, nBytes, fp);

                // Error
                if (n != nBytes) {
                    if (debug) fprintf(stderr, "\n ******* Last write had error, n = %lu, expected %ld\n\n", n, nBytes);
                    if (debug) fprintf(stderr, "write error: %s\n", strerror(errno));
                    exit(1);
                }

                totalWritten += n;
                if (totalWritten >= nBytes) {
                    break;
                }
            }

            if (debug) fprintf(stderr, "writeBuffer: wrote %lu bytes\n", totalWritten);

            return 0;
        }


        /**
         * Assemble incoming packets into the given buffer or into an internally allocated buffer.
         * Any internally allocated buffer is guaranteed to be big enough to hold the entire
         * incoming buffer.
         * It will return when the buffer has less space left than it read from the first packet
         * (for caller-given buffer) or when the "last" bit is set in a packet.
         * This routine allows for out-of-order packets.
         * This assumes the new, version 2, RE header.
         *
         * @param userBuf       address of pointer to data buffer if noCopy is true.
         *                      Otherwise, this must point to nullptr in order
         *                      to return a locally allocated data buffer.
         *                      Note that in the latter case, the returned buffer must be freed by caller!
         * @param userBufLen    pointer to byte length of given dataBuf if noCopy is true.
         *                      Otherwise it should pointer to a suggested buffer size (0 for default of 100kB)
         *                      and returns the size of the data buffer internally allocated.
         * @param port          UDP port to read on.
         * @param listeningAddr if specified, this is the IP address to listen on (dot-decimal form).
         * @param noCopy        If true, write data directly into userBuf. If there's not enough room, an error is thrown.
         *                      If false, an internal buffer is allocated and returned in the userBuf arg.
         * @param debug         turn debug printout on & off.
         *
         * @return 0 if success.
         *         If error in recvmsg, return RECV_MSG.
         *         If can't create socket, return NETWORK_ERROR.
         *         If on a read &lt; HEADER_BYTES data returned, return INTERNAL_ERROR.
         *         If userBuf, *userBuf, or userBufLen is null, return BAD_ARG.
         *         If noCopy and buffer is too small to contain reassembled data, return BUF_TOO_SMALL.
         *         If receiving &gt; 99 pkts from wrong data id and not noCopy, return NO_REASSEMBLY.
         *         If cannot allocate memory and not noCopy, return OUT_OF_MEM.
         */
        static ssize_t getBuffer(char** userBuf, size_t *userBufLen,
                                 uint16_t port, const char *listeningAddr,
                                 bool noCopy, bool debug, bool useIPv6) {


            if (userBuf == nullptr || userBufLen == nullptr) {
                return BAD_ARG;
            }

            port = port < 1024 ? 17750 : port;
            int err, udpSocket;

            if (useIPv6) {

                struct sockaddr_in6 serverAddr6{};

                // Create IPv6 UDP socket
                if ((udpSocket = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                    perror("creating IPv6 client socket");
                    return NETWORK_ERROR;
                }

                // Try to increase recv buf size to 25 MB
                socklen_t size = sizeof(int);
                int recvBufBytes = 25000000;
                setsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, &recvBufBytes, sizeof(recvBufBytes));
                recvBufBytes = 0; // clear it
                getsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, &recvBufBytes, &size);
                if (debug) fprintf(stderr, "UDP socket recv buffer = %d bytes\n", recvBufBytes);

                // Configure settings in address struct
                // Clear it out
                memset(&serverAddr6, 0, sizeof(serverAddr6));
                // it is an INET address
                serverAddr6.sin6_family = AF_INET6;
                // the port we are going to receiver from, in network byte order
                serverAddr6.sin6_port = htons(port);
                if (listeningAddr != nullptr && strlen(listeningAddr) > 0) {
                    inet_pton(AF_INET6, listeningAddr, &serverAddr6.sin6_addr);
                }
                else {
                    serverAddr6.sin6_addr = in6addr_any;
                }

                // Bind socket with address struct
                err = bind(udpSocket, (struct sockaddr *) &serverAddr6, sizeof(serverAddr6));
                if (err != 0) {
                    // TODO: handle error properly
                    if (debug) fprintf(stderr, "bind socket error\n");
                }

            } else {

                struct sockaddr_in serverAddr{};

                // Create UDP socket
                if ((udpSocket = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
                    perror("creating IPv4 client socket");
                    return NETWORK_ERROR;
                }

                // Try to increase recv buf size to 25 MB
                socklen_t size = sizeof(int);
                int recvBufBytes = 25000000;
                setsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, &recvBufBytes, sizeof(recvBufBytes));
                recvBufBytes = 0; // clear it
                getsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, &recvBufBytes, &size);
                if (debug) fprintf(stderr, "UDP socket recv buffer = %d bytes\n", recvBufBytes);

                // Configure settings in address struct
                memset(&serverAddr, 0, sizeof(serverAddr));
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_port = htons(port);
                if (listeningAddr != nullptr && strlen(listeningAddr) > 0) {
                    serverAddr.sin_addr.s_addr = inet_addr(listeningAddr);
                }
                else {
                    serverAddr.sin_addr.s_addr = INADDR_ANY;
                }
                memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);

                // Bind socket with address struct
                err = bind(udpSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
                if (err != 0) {
                    // TODO: handle error properly
                    if (debug) fprintf(stderr, "bind socket error\n");
                }
            }

            ssize_t nBytes;
            // Start with sequence 0 in very first packet to be read
            uint64_t tick = 0;


            if (noCopy) {
                // If it's no-copy, we give the reading routine the user's whole buffer ONCE and have it filled.
                // Write directly into user-specified buffer.
                // In this case, the user knows how much data is coming and provides
                // a buffer big enough to hold it all. If not, error.
                if (*userBuf == nullptr) {
                    return BAD_ARG;
                }

                nBytes = getCompletePacketizedBuffer(*userBuf, *userBufLen, udpSocket,
                                                     debug, &tick, nullptr,
                                                     nullptr, 1);
                if (nBytes < 0) {
                    if (debug) fprintf(stderr, "Error in getCompletePacketizedBufferNew, %ld\n", nBytes);
                    // Return the error (ssize_t)
                    return nBytes;
                }
            }
            else {
                nBytes = getCompleteAllocatedBuffer(userBuf, userBufLen, udpSocket,
                                                    debug, &tick, nullptr,
                                                    nullptr, 1);
                if (nBytes < 0) {
                    if (debug) fprintf(stderr, "Error in getCompleteAllocatedBufferNew, %ld\n", nBytes);
                    // Return the error
                    return nBytes;
                }
            }

            if (debug) fprintf(stderr, "Read %ld bytes from incoming reassembled packet\n", nBytes);
            return 0;
        }


    }


#endif // EJFAT_ASSEMBLE_ERSAP_H
