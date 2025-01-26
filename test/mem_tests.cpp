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

size_t eventSize = 1000000;
size_t maxPldLen = 8192;
size_t numBuffers = (eventSize + maxPldLen - 1)/ maxPldLen;

// pool of LB+RE headers for sending
boost::object_pool<LBREHdr> lbreHdrPool{32,0};
// pool of iovec items (we grab two at a time)
boost::pool<> iovecPool{sizeof(struct iovec)*2};

std::vector<LBREHdr*> hdrs;
std::vector<struct iovec*> iovecs;

void usePools()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs.push_back(lbreHdrPool.malloc());
        iovecs.push_back(static_cast<struct iovec*>(iovecPool.malloc()));
    }
    for(auto hdr: hdrs)
        lbreHdrPool.free(hdr);

    for(auto iov: iovecs)
        iovecPool.free(iov);

    hdrs.clear();
    iovecs.clear();
}

void useMallocs()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs.push_back(static_cast<LBREHdr*>(malloc(sizeof(LBREHdr))));
        iovecs.push_back(static_cast<struct iovec*>(calloc(2, sizeof(struct iovec))));
    }

    for(auto hdr: hdrs)
        free(hdr);

    for(auto iov: iovecs)
        free(iov);

    hdrs.clear();
    iovecs.clear();
}

void useNew()
{
    for(size_t i = 0; i < numBuffers; i++)
    {
        hdrs.push_back(new LBREHdr);
        iovecs.push_back(new struct iovec[2]);
    }
    for(auto hdr: hdrs)
        delete hdr;

    for(auto iov: iovecs)
        delete[] iov;

    hdrs.clear();
    iovecs.clear();
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
    doIters(usePools, numIters);
    end = getMicros();

    std::cout << "Pools took " << end - start << " microseconds" << std::endl;

    start = getMicros();
    doIters(useMallocs, numIters);
    end = getMicros();

    std::cout << "Mallocs took " << end - start << " microseconds" << std::endl;

    start = getMicros();
    doIters(useNew, numIters);
    end = getMicros();

    std::cout << "New took " << end - start << " microseconds" << std::endl;
}