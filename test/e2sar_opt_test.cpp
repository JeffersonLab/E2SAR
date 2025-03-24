#define BOOST_TEST_MODULE DPOptTests
#include <boost/test/included/unit_test.hpp>
#include <vector>
#include "e2sarUtil.hpp"

using namespace e2sar;


BOOST_AUTO_TEST_SUITE(DPOptTests)

// these tests test the way Optimizations object works
BOOST_AUTO_TEST_CASE(DPOptTest1)
{
    // basic conversions
    BOOST_CHECK(Optimizations::toString(Optimizations::Code::sendmmsg) == "sendmmsg"s);
    BOOST_CHECK(Optimizations::toString(Optimizations::Code::unknown) == "unknown"s);

    BOOST_CHECK(Optimizations::fromString("sendmmsg") == Optimizations::Code::sendmmsg);
    BOOST_CHECK(Optimizations::fromString("liburing_send") == Optimizations::Code::liburing_send);

    auto avail = Optimizations::availableAsStrings();
    bool nonePresent = false;
    for(auto a: avail)
    {
        if (a == "none")
            nonePresent = true;
    }
    BOOST_CHECK(nonePresent == true);
}

BOOST_AUTO_TEST_CASE(DPOptTest2)
{
    std::vector<std::string> opts = {"sendmmsg"};
    auto res = Optimizations::select(opts);
#ifdef SENDMMSG_AVAILABLE
    BOOST_CHECK(not res.has_error());
#else
    BOOST_CHECK(res.has_error());
#endif
}
BOOST_AUTO_TEST_SUITE_END()