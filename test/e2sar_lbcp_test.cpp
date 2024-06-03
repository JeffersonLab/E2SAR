#define BOOST_TEST_MODULE MyTestSuite
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/test/included/unit_test.hpp>

#include "e2sar.hpp"

using namespace e2sar;

std::string uri_string1{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};

BOOST_AUTO_TEST_SUITE(E2SARTestSuite)

BOOST_AUTO_TEST_CASE(LBMTest1) {
    // test generating ssl options

    std::string root{"root cert"}, priv{"priv key"}, cert{"cert chain"};

    std::cout<< root << "|" << priv << "|" << cert << std::endl;

    outcome::result<grpc::SslCredentialsOptions> opts{LBManager::makeSslOptions(root, priv, cert)};

    BOOST_TEST(!opts.has_error());

    std::cout<< root << "|" << priv << "|" << cert << std::endl;

    BOOST_TEST(opts.value().pem_root_certs == "root cert"s);
    BOOST_TEST(opts.value().pem_private_key == "priv key"s);
    BOOST_TEST(opts.value().pem_cert_chain == "cert chain"s);
}

BOOST_AUTO_TEST_CASE(LBMTest2) {

    std::string rootn{"/tmp/root.pem"};
    std::ofstream rootf{rootn};
    rootf << "root cert";
    rootf.close();

    std::string privn{"/tmp/priv.pem"};
    std::ofstream privf{privn};
    privf << "priv key";
    privf.close();

    std::string certn{"/tmp/cert.pem"};
    std::ofstream certf{certn};
    certf << "cert chain";
    certf.close();

    outcome::result<grpc::SslCredentialsOptions> opts{LBManager::makeSslOptionsFromFiles(rootn, privn, certn)};

    if (opts.has_error())
        std::cout << opts.error().message() << std::endl;
    BOOST_TEST(!opts.has_error());
    std::filesystem::remove(rootn);
    std::filesystem::remove(privn);
    std::filesystem::remove(certn);
}

BOOST_AUTO_TEST_CASE(LBMTest3) {

    EjfatURI uri(uri_string1);

    LBManager lbm(uri);
}
BOOST_AUTO_TEST_SUITE_END()