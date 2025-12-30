#include <sys/types.h>
#include <sys/socket.h>

#include <iostream>

#include <boost/pool/pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/chrono.hpp>

#include "e2sarHeaders.hpp"

using namespace e2sar;

inline u_int64_t getMicros()
{
        auto nowT = boost::chrono::system_clock::now();
        auto nowUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
        return static_cast<u_int64_t>(nowUsec);
}

const size_t eventSize = 1000000;
const size_t maxPldLen = 8192;
const size_t numBuffers = (eventSize + maxPldLen - 1)/ maxPldLen;

// pool of LB+RE headers for sending
//boost::object_pool<LBREHdr> lbreHdrPool;
boost::pool<> lbreHdrPool{sizeof(LBREHdr)};
// pool of iovec items (we grab two at a time)
boost::pool<> iovecPool{sizeof(struct iovec)*2};

LBREHdr* hdrs[numBuffers];
struct iovec* iovecs[numBuffers];

void usePools()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs[i] = static_cast<LBREHdr*>(lbreHdrPool.malloc());
        iovecs[i] = static_cast<struct iovec*>(iovecPool.malloc());
    }
    for(size_t i = 0; i < numBuffers; i++)
    {
        lbreHdrPool.free(hdrs[i]);
        iovecPool.free(iovecs[i]);
    }
}

void useMallocs()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        void *space = malloc(sizeof(LBREHdr));

        hdrs[i] = new (space) LBREHdr();
        iovecs[i] = static_cast<struct iovec*>(calloc(2, sizeof(struct iovec)));
    }

    for(size_t i = 0; i < numBuffers; i++)
    {
        free(hdrs[i]);
        free(iovecs[i]);
    }
}

void useNew()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs[i] = new LBREHdr();
        iovecs[i] = new struct iovec[2];
    }

    for(size_t i = 0; i < numBuffers; i++)
    {
        delete hdrs[i];
        delete[] iovecs[i];
    }
}

void doIters(void (*func)(), size_t iters)
{
    for(size_t i = 0; i < iters; i++)
    {
        func();
    }
}

int main(int argc, char **argv)
{
    size_t numIters = 10000;

    u_int64_t start, end; 

    // allocation test
    void *hdrspace = malloc(sizeof(LBREHdr));
    memset(hdrspace, 0, sizeof(LBREHdr));
    
    // placement-new to construct the headers
    auto hdr = new (hdrspace) LBREHdr(lbhdrVersion3);
    u_int8_t* hdrbuf = reinterpret_cast<u_int8_t*>(hdr);

    std::cout << "LB Header Version2 Check: " << hdr->lbu.lb2.check_version() << std::endl;
    std::cout << "LB Header Version3 Check: " << hdr->lbu.lb3.check_version() << std::endl;
    std::cout << "LB Header Version: " << static_cast<int>(hdr->lbu.lb2.get_version()) << std::endl;
    std::cout << "RE Header Version: " << static_cast<int>(hdr->re.get_HeaderVersion()) << std::endl;
    hdr->lbu.lb3.set(1, 2, 3);
    hdr->re.set(4, 5, 6, 7);
    
    LBREHdr *newhdr{nullptr};
    newhdr = reinterpret_cast<LBREHdr*>(hdrbuf);
    std::cout << "Decoded LB Header Version: " << static_cast<int>(newhdr->lbu.lb2.get_version()) << std::endl;
    std::cout << "Decoded LB Fields: " << 
        static_cast<int>(newhdr->lbu.lb3.get_slotSelect()) << " " << 
        static_cast<int>(newhdr->lbu.lb3.get_portSelect()) << " " << 
        newhdr->lbu.lb3.get_tick() << std::endl; 

    REHdr *rehdr{nullptr};
    rehdr = reinterpret_cast<REHdr*>(hdrbuf + sizeof(LBHdrU));
    std::cout << "Decoded RE Header Version: " << static_cast<int>(rehdr->get_HeaderVersion()) << std::endl;
    std::cout << "Decoded RE Header Fields: " << 
        static_cast<int>(rehdr->get_dataId()) << " " <<
        static_cast<int>(rehdr->get_bufferOffset()) << " " << 
        static_cast<int>(rehdr->get_bufferLength()) << " " <<
        static_cast<int>(rehdr->get_eventNum()) << std::endl;

    start = getMicros();
    doIters(useNew, numIters);
    end = getMicros();

    std::cout << "New took " << end - start << " microseconds" << std::endl;

    start = getMicros();
    doIters(useMallocs, numIters);
    end = getMicros();

    std::cout << "Mallocs took " << end - start << " microseconds" << std::endl;

    start = getMicros();
    doIters(usePools, numIters);
    end = getMicros();

    std::cout << "Pools took " << end - start << " microseconds" << std::endl;
}