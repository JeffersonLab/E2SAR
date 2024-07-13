#ifndef E2SARHEADERSHPP
#define E2SARHEADERSHPP

#include <boost/tuple/tuple.hpp>
#include "portable_endian.h"

namespace e2sar
{
    using EventNum_t = u_int64_t;
    using UnixTimeNano_t = u_int64_t;
    using EventRate_t = u_int32_t;

    constexpr u_int8_t rehdrVersion = 1 << 4; // shifted up by 4 bits to be in the upper nibble
    /*
        The Reassembly Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct REHdr
    {
        u_int8_t preamble[2] {rehdrVersion, 0}; // 4 bit version + reserved
        u_int16_t dataId{0};   // source identifier
        u_int32_t bufferOffset{0};
        u_int32_t bufferLength{0};
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
        inline const boost::tuple<u_int16_t, u_int32_t, u_int32_t, EventNum_t> get_Fields() const 
        {
            return boost::make_tuple(get_dataId(), get_bufferOffset(), get_bufferLength(), get_eventNum());
        }
    } __attribute__((__packed__));

    constexpr u_int8_t lbhdrVersion = 2;
    /**
        The Load Balancer Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct LBHdr
    {
        char preamble[2] {'L', 'B'};
        u_int8_t version{lbhdrVersion};
        u_int8_t nextProto{0};
        u_int16_t rsvd{0};
        u_int16_t entropy{0};
        EventNum_t eventNum{0L};

        /**
         * Set all fields to network/big-endian byte order values
         */
        inline void set(u_int8_t proto, u_int16_t ent, EventNum_t event_num) 
        {
            nextProto = proto;
            entropy = htobe16(ent);
            eventNum = htobe64(event_num);
        }

        /**
         * get version
         */
        u_int8_t get_version() const
        {
            return version;
        }

        /**
         * get next protocol field
         */
        u_int8_t get_nextProto() const 
        {
            return nextProto;
        }

        /**
         * get entropy in host byte order
         */
        u_int16_t get_entropy() const
        {
            return be16toh(entropy);
        }

        /**
         * get event number in host byte order
         */
        EventNum_t get_eventNum() const
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
        const boost::tuple<u_int8_t, u_int8_t, u_int16_t, EventNum_t> get_Fields() const 
        {
            return boost::make_tuple(version, nextProto, get_entropy(), get_eventNum());
        }
    } __attribute__((__packed__));

    // for memory allocation purposes we need them concatenated
    struct LBREHdr {
        struct LBHdr lb;
        struct REHdr re;
    } __attribute__((__packed__));

    constexpr u_int8_t synchdrVersion = 1;
    /**
        The Syncr Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    struct SyncHdr
    {
        char preamble[2] {'L', 'C'};
        u_int8_t version{synchdrVersion};
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
}

#endif