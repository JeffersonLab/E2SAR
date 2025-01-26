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
boost::object_pool<LBREHdr> lbreHdrPool{32,0};
// pool of iovec items (we grab two at a time)
boost::pool<> iovecPool{sizeof(struct iovec)*2};

LBREHdr* hdrs[numBuffers];
struct iovec* iovecs[numBuffers];

void usePools()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs[i] = lbreHdrPool.malloc();
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
        hdrs[i] = static_cast<LBREHdr*>(malloc(sizeof(LBREHdr)));
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
        hdrs[i] = new LBREHdr;
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
    size_t numIters = 1000;

    u_int64_t start, end; 
    
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