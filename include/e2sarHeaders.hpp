#ifndef E2SARHEADERSHPP
#define E2SARHEADERSHPP

#include <boost/tuple/tuple.hpp>
#include "portable_endian.h"

namespace e2sar
{
    using EventNum_t = u_int64_t;
    using UnixTimeNano_t = u_int64_t;
    using UnixTimeMicro_t = u_int64_t;
    using EventRate_t = u_int32_t;

    constexpr u_int8_t rehdrVersion = 1;
    constexpr u_int8_t rehdrVersionNibble = rehdrVersion << 4; // shifted up by 4 bits to be in the upper nibble
    /*
        The Reassembly Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct REHdr
    {
        u_int8_t preamble[2] {rehdrVersionNibble, 0}; // 4 bit version + reserved
        u_int16_t dataId{0};   // source identifier
        u_int32_t bufferOffset{0};
        u_int32_t bufferLength{0}; // this is event length, not the length of the buffer being sent
        EventNum_t eventNum{0};

        /**
         * Set all fields to network/big-endian byte order values
         */
        inline void set(u_int16_t data_id, u_int32_t buff_off, u_int32_t buff_len, EventNum_t event_num)
        {
            dataId = htobe16(data_id);
            bufferOffset = htobe32(buff_off);
            bufferLength = htobe32(buff_len);
            eventNum = htobe64(event_num);
        }

        /**
         * Get event number on host byte order
         */
        inline EventNum_t get_eventNum() const
        {
            return be64toh(eventNum);
        }

        /**
         * get buffer length in host byte order
         */
        inline u_int32_t get_bufferLength() const
        {
            return be32toh(bufferLength);
        }

        /**
         * get buffer offset in host byte order
         */
        inline u_int32_t get_bufferOffset() const
        {
            return be32toh(bufferOffset);
        }

        /**
         * get data id in host byte order
         */
        inline u_int16_t get_dataId() const
        {
            return be16toh(dataId);
        }

        /**
         * Convenience method to get all fields in host byte order. Best way to use it is 
         * u_int16_t myDataId;   
         * u_int32_t myBufferOffset;
         * u_int32_t myBufferLength;
         * EventNum_t myEventNum;
         * 
         * boost::tie(myDataId, myBufferOffset, myBufferLength, myEventNum) = rehdr.get_fields();
         */
        const inline boost::tuple<u_int16_t, u_int32_t, u_int32_t, EventNum_t> get_Fields() const 
        {
            return boost::make_tuple(get_dataId(), get_bufferOffset(), get_bufferLength(), get_eventNum());
        }

        /**
         * Get the version of the header
         */
        inline u_int8_t get_HeaderVersion() const 
        {
            return preamble[0] >> 4; 
        }

        /**
         * Validate this header header
         * Check that version matches and reserved field is 00s
         */
        inline bool validate() const 
        {
            return (preamble[0] == rehdrVersionNibble) && (preamble[1] == 0);
        }
    } __attribute__((__packed__));

    constexpr u_int8_t lbhdrVersion2 = 2;
    constexpr u_int8_t lbhdrVersion3 = 3;
    /**
        The Load Balancer Header v2. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct LBHdrV2
    {
        char preamble[2] {'L', 'B'};
        u_int8_t version{lbhdrVersion2};
        u_int8_t nextProto{rehdrVersion};
        u_int16_t rsvd{0};
        u_int16_t entropy{0};
        EventNum_t eventNum{0L};

        /**
         * Set all fields to network/big-endian byte order values
         */
        inline void set(u_int16_t ent, EventNum_t event_num) 
        {
            entropy = htobe16(ent);
            eventNum = htobe64(event_num);
        }

        /**
         * get version
         */
        inline u_int8_t get_version() const
        {
            return version;
        }

        /**
         * check version 2
         */
        inline bool check_version() const
        {
            return version == lbhdrVersion2;
        }

        /**
         * get next protocol field
         */
        inline u_int8_t get_nextProto() const 
        {
            return nextProto;
        }

        /**
         * get entropy in host byte order
         */
        inline u_int16_t get_entropy() const
        {
            return be16toh(entropy);
        }

        /**
         * get event number in host byte order
         */
        inline EventNum_t get_eventNum() const
        {
            return be64toh(eventNum);
        }

        /**
         * Convenience method to get all fields (in host byte order where appropriate). 
         * Best way to use it is 
         * 
         * u_int8_t version;
         * u_int8_t nextProto;
         * u_int16_t entropy;
         * EventNum_t eventNum;
         * 
         * boost::tie(version, nextProto, entropy, eventNum) = lbhdr.get_Fields();
         */
        const inline boost::tuple<u_int8_t, u_int8_t, u_int16_t, EventNum_t> get_Fields() const 
        {
            return boost::make_tuple(version, nextProto, get_entropy(), get_eventNum());
        }
    } __attribute__((__packed__));

    /**
        The Load Balancer Header v2. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct LBHdrV3
    {
        char preamble[2] {'L', 'B'};
        u_int8_t version{lbhdrVersion3};
        u_int8_t nextProto{rehdrVersion};
        u_int16_t slotSelect{0};
        u_int16_t portSelect{0};
        EventNum_t tick{0L};

        /**
         * Set all fields to network/big-endian byte order values
         */
        inline void set(u_int16_t slt, u_int16_t prt, EventNum_t _tick) 
        {
            slotSelect = htobe16(slt);
            portSelect = htobe16(prt);
            tick = htobe64(tick);
        }

        /**
         * get version
         */
        inline u_int8_t get_version() const
        {
            return version;
        }

        /**
         * check version 2
         */
        inline bool check_version() const
        {
            return version == lbhdrVersion2;
        }

        /**
         * get next protocol field
         */
        inline u_int8_t get_nextProto() const 
        {
            return nextProto;
        }

        /**
         * get slot select in host byte order
         */
        inline u_int16_t get_slotSelect() const
        {
            return be16toh(slotSelect);
        }

        /**
         * get port select in host byte order
         */
        inline u_int16_t get_portSelect() const
        {
            return be16toh(portSelect);
        }

        /**
         * get tick in host byte order
         */
        inline EventNum_t get_tick() const
        {
            return be64toh(tick);
        }

        /**
         * Convenience method to get all fields (in host byte order where appropriate). 
         * Best way to use it is 
         * 
         * u_int8_t version;
         * u_int8_t nextProto;
         * u_int16_t slotSelect;
         * u_int16_t portSelect;
         * EventNum_t tick;
         * 
         * boost::tie(version, nextProto, entropy, eventNum) = lbhdr.get_Fields();
         */
        const inline boost::tuple<u_int8_t, u_int8_t, u_int16_t, u_int16_t, EventNum_t> get_Fields() const 
        {
            return boost::make_tuple(version, nextProto, get_slotSelect(), get_portSelect(), get_tick());
        }
    } __attribute__((__packed__));

    
    // union of LB headers (they are all the same lengths)
    union LBHdrU {
        struct LBHdrV2 lb2;
        struct LBHdrV3 lb3;
        // explicit c-tor because it's a union
        LBHdrU()
        {
            memset(this, 0, sizeof(LBHdrU));
        }
    };

    // for memory allocation purposes we need them concatenated
    struct LBREHdr {
        union LBHdrU lbu;
        struct REHdr re;
    } __attribute__((__packed__));

    constexpr u_int8_t synchdrVersion2 = 2;
    /**
        The Syncr Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct SyncHdr
    {
        char preamble[2] {'L', 'C'};
        u_int8_t version{synchdrVersion2};
        u_int8_t rsvd{0};
        u_int32_t eventSrcId{0};
        EventNum_t eventNumber{0LL};
        EventRate_t avgEventRateHz{0};
        UnixTimeNano_t unixTimeNano{0LL};

        /**
         * Set all fields to network/big-endian byte order values
         */
        inline void set(u_int32_t esid, EventNum_t event_num, EventRate_t avg_rate, UnixTimeNano_t ut)
        {
            eventSrcId = htobe32(esid);
            eventNumber = htobe64(event_num);
            avgEventRateHz = htobe32(avg_rate);
            unixTimeNano = htobe64(ut);
        }

        /**
         * get version of SYNC
         */
        u_int8_t get_version() const 
        {
            return version;
        }

        /**
         * check version
         */
        bool check_version() const 
        {
            return version == synchdrVersion2;
        }

        /**
         * get event src id in host byte order
         */
        inline u_int32_t get_eventSrcId() const
        {
            return be32toh(eventSrcId);
        }
        /**
         * get event number in host byte order
         */
        inline EventNum_t get_eventNumber() const
        {
            return be64toh(eventNumber);
        }
        /**
         * get avg event rate in Hz in host byte order
         */
        inline u_int32_t get_avgEventRateHz() const 
        {
            return be32toh(avgEventRateHz);
        }
        /**
         * get unix time nanoseconds in host byte order
         */
        inline UnixTimeNano_t get_unixTimeNano() const 
        {
            return be64toh(unixTimeNano);
        }
        /**
         * Convenience method to get all fields (in host byte order where appropriate). 
         * Best way to use it is 
         * 
         * u_int32_t eventSrcId;
         * EventNum_t eventNumber;
         * u_int32_t avgEventRateHz;
         * UnixTimeNano_t unixTimeNano;
         * 
         * boost::tie(eventSrcId, eventNumber, avgEventRateHz, unixTimeNano) = synchdr.get_Fields();
         */
        const boost::tuple<u_int32_t, EventNum_t, u_int32_t, UnixTimeNano_t> get_Fields() const 
        {
            return boost::make_tuple(get_eventSrcId(), get_eventNumber(), get_avgEventRateHz(), get_unixTimeNano());
        }
    } __attribute__((__packed__));

    // various useful header lengths
    constexpr size_t IPV4_HDRLEN = 20;
    constexpr size_t IPV6_HDRLEN = 40;
    constexpr size_t UDP_HDRLEN = 8;
    
    // Legacy constant for backward compatibility (IPv4 only)
    constexpr size_t IP_HDRLEN = IPV4_HDRLEN;
    constexpr size_t TOTAL_HDR_LEN{IP_HDRLEN + UDP_HDRLEN + sizeof(LBHdrV2) + sizeof(REHdr)};

    // Protocol-aware header length functions
    inline constexpr size_t getIPHeaderLength(bool useIPv6) {
        return useIPv6 ? IPV6_HDRLEN : IPV4_HDRLEN;
    }

    inline constexpr size_t getTotalHeaderLength(bool useIPv6) {
        return getIPHeaderLength(useIPv6) + UDP_HDRLEN + sizeof(LBHdrV2) + sizeof(REHdr);
    }
}

#endif