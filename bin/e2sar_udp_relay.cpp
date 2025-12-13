/**
 * E2SAR UDP Relay - A simple UDP packet forwarder
 *
 * Receives UDP packets on a specified port and immediately forwards them
 * to a destination IP address and port. Supports both IPv4 and IPv6.
 */

#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "e2sarUtil.hpp"
#include "e2sarError.hpp"
#include "e2sar.hpp"

namespace po = boost::program_options;
using namespace e2sar;
using namespace boost::asio;
using namespace std::string_literals;

// Statistics (atomic for thread-safety)
std::atomic<uint64_t> packetsReceived{0};
std::atomic<uint64_t> bytesReceived{0};
std::atomic<uint64_t> packetsSent{0};
std::atomic<uint64_t> sendErrors{0};

// Control flags
std::atomic<bool> keepRunning{true};
std::atomic<bool> handlerTriggered{false};

// For cleanup in signal handler
int rxSocket{-1};
int txSocket{-1};
std::thread statsThread;

/**
 * Print relay statistics to stdout
 */
void printStats()
{
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "  Packets received: " << packetsReceived << std::endl;
    std::cout << "  Bytes received:   " << bytesReceived << std::endl;
    std::cout << "  Packets sent:     " << packetsSent << std::endl;
    std::cout << "  Send errors:      " << sendErrors << std::endl;
}

/**
 * Background thread function that prints statistics once per second
 */
void statsThreadFunc()
{
    while (keepRunning) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (keepRunning) {
            printStats();
        }
    }
}

/**
 * Signal handler for graceful shutdown (Ctrl-C)
 */
void signalHandler(int sig)
{
    if (handlerTriggered.exchange(true))
        return;

    std::cout << "\nShutting down..." << std::endl;
    keepRunning = false;

    // Wait for stats thread to finish
    if (statsThread.joinable()) {
        statsThread.join();
    }

    // Close sockets
    if (rxSocket >= 0)
        close(rxSocket);
    if (txSocket >= 0)
        close(txSocket);

    // Print final statistics
    printStats();

    exit(0);
}

/**
 * Function used to check that 'opt1' and 'opt2' are not specified
 * at the same time.
 */
void conflicting_options(const po::variables_map &vm,
                         const std::string &opt1, const std::string &opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted())
        throw std::logic_error(std::string("Conflicting options '") + opt1 + "' and '" + opt2 + "'.");
}

/**
 * Create and bind a UDP receive socket
 *
 * @param addr Address to bind to
 * @param port Port to bind to
 * @param bufSize Socket buffer size
 * @return Socket file descriptor or error
 */
result<int> createReceiveSocket(ip::address addr, u_int16_t port, int bufSize)
{
    int fd;

    // Create socket based on address type
    if (addr.is_v6()) {
        fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd < 0) {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to create IPv6 receive socket: "s + strerror(errno)};
        }

        // Set socket buffer size
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to set receive buffer size: "s + strerror(errno)};
        }

        // Bind to specified address
        sockaddr_in6 rxAddr{};
        rxAddr.sin6_family = AF_INET6;
        rxAddr.sin6_port = htons(port);
        inet_pton(AF_INET6, addr.to_string().c_str(), &rxAddr.sin6_addr);

        if (bind(fd, (sockaddr*)&rxAddr, sizeof(rxAddr)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Failed to bind IPv6 socket to "s + addr.to_string() + ":"s + std::to_string(port) + ": "s + strerror(errno)};
        }
    } else {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to create IPv4 receive socket: "s + strerror(errno)};
        }

        // Set socket buffer size
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to set receive buffer size: "s + strerror(errno)};
        }

        // Bind to specified address
        sockaddr_in rxAddr{};
        rxAddr.sin_family = AF_INET;
        rxAddr.sin_port = htons(port);
        inet_pton(AF_INET, addr.to_string().c_str(), &rxAddr.sin_addr);

        if (bind(fd, (sockaddr*)&rxAddr, sizeof(rxAddr)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Failed to bind IPv4 socket to "s + addr.to_string() + ":"s + std::to_string(port) + ": "s + strerror(errno)};
        }
    }

    return fd;
}

/**
 * Create and connect a UDP send socket
 *
 * @param dest Destination IP address
 * @param port Destination port
 * @param bufSize Socket buffer size
 * @return Socket file descriptor or error
 */
result<int> createSendSocket(ip::address dest, u_int16_t port, int bufSize)
{
    int fd;

    // Create socket based on destination address type
    if (dest.is_v6()) {
        fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd < 0) {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to create IPv6 send socket: "s + strerror(errno)};
        }

        // Set socket buffer size
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to set send buffer size: "s + strerror(errno)};
        }

        // Build destination address structure
        sockaddr_in6 destAddr{};
        destAddr.sin6_family = AF_INET6;
        destAddr.sin6_port = htons(port);
        inet_pton(AF_INET6, dest.to_string().c_str(), &destAddr.sin6_addr);

        // Connect socket (allows using send() instead of sendto())
        if (connect(fd, (const sockaddr*)&destAddr, sizeof(destAddr)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Failed to connect IPv6 socket to "s + dest.to_string() + ":"s + std::to_string(port) + ": "s + strerror(errno)};
        }
    } else {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to create IPv4 send socket: "s + strerror(errno)};
        }

        // Set socket buffer size
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to set send buffer size: "s + strerror(errno)};
        }

        // Build destination address structure
        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(port);
        inet_pton(AF_INET, dest.to_string().c_str(), &destAddr.sin_addr);

        // Connect socket (allows using send() instead of sendto())
        if (connect(fd, (const sockaddr*)&destAddr, sizeof(destAddr)) < 0) {
            close(fd);
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Failed to connect IPv4 socket to "s + dest.to_string() + ":"s + std::to_string(port) + ": "s + strerror(errno)};
        }
    }

    return fd;
}

/**
 * Main relay loop - receives packets and forwards them
 *
 * @param rxSock Receive socket file descriptor
 * @param txSock Send socket file descriptor
 */
void relayLoop(int rxSock, int txSock)
{
    const size_t bufferSize = 65536; // Max UDP packet size
    uint8_t buffer[bufferSize];

    while (keepRunning) {
        // Receive packet
        ssize_t recvLen = recvfrom(rxSock, buffer, bufferSize, 0, nullptr, nullptr);

        if (recvLen < 0) {
            if (errno == EINTR) {
                // Signal interrupted, check keepRunning flag
                continue;
            }
            sendErrors++;
            continue; // Don't exit on single error
        }

        if (recvLen == 0) {
            continue; // Zero-length packet
        }

        // Update receive statistics
        packetsReceived++;
        bytesReceived += recvLen;

        // Forward packet
        ssize_t sentLen = send(txSock, buffer, recvLen, 0);

        if (sentLen < 0) {
            sendErrors++;
        } else if (sentLen == recvLen) {
            packetsSent++;
        } else {
            // Partial send
            sendErrors++;
        }
    }
}

int main(int argc, char **argv)
{
    po::options_description od("Command-line options");

    auto opts = od.add_options()(
        "help,h", "Show this help message"
    );

    std::string rxAddrStr;
    std::string txAddrStr;
    int rxBufSize, txBufSize;

    // Define command-line options
    opts("rx-addr,r", po::value<std::string>(&rxAddrStr)->default_value("127.0.0.1:0"), "Address and port to receive on (e.g., \"192.168.1.1:5000\" or \"[::1]:5000\"). Default: 127.0.0.1:0");
    opts("tx-addr,t", po::value<std::string>(&txAddrStr), "Destination address and port (e.g., \"192.168.1.1:5000\" or \"[::1]:5000\") [required]");
    opts("rx-bufsize", po::value<int>(&rxBufSize)->default_value(1048576), "Receive socket buffer size in bytes (default 1MB)");
    opts("tx-bufsize", po::value<int>(&txBufSize)->default_value(1048576), "Send socket buffer size in bytes (default 1MB)");

    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    } catch (const boost::program_options::error &e) {
        std::cerr << "Unable to parse command line: " << e.what() << std::endl;
        return -1;
    }

    // Show help if requested
    if (vm.count("help")) {
        std::cout << "E2SAR UDP Relay" << std::endl;
        std::cout << "Version: " << get_Version() << std::endl;
        std::cout << std::endl;
        std::cout << "A simple UDP packet forwarder that receives packets on one address/port" << std::endl;
        std::cout << "and forwards them to another. Supports both IPv4 and IPv6, including" << std::endl;
        std::cout << "mixed protocol relaying (IPv4 to IPv6 and vice versa)." << std::endl;
        std::cout << std::endl;
        std::cout << od << std::endl;
        std::cout << std::endl;
        std::cout << "Example usage:" << std::endl;
        std::cout << "  IPv4 loopback:  e2sar_udp_relay -r 127.0.0.1:10000 -t 127.0.0.1:10001" << std::endl;
        std::cout << "  IPv4 specific:  e2sar_udp_relay -r 192.168.1.1:10000 -t 192.168.1.100:10001" << std::endl;
        std::cout << "  IPv6 loopback:  e2sar_udp_relay -r [::1]:10000 -t [::1]:10001" << std::endl;
        std::cout << "  Mixed protocol: e2sar_udp_relay -r 127.0.0.1:10000 -t [::1]:10001" << std::endl;
        std::cout << std::endl;
        std::cout << "Security note:" << std::endl;
        std::cout << "  Default receive address is 127.0.0.1 (localhost only) for security." << std::endl;
        std::cout << "  Binding to 0.0.0.0 or :: exposes the relay to all network interfaces" << std::endl;
        std::cout << "  and should only be used in trusted environments." << std::endl;
        return 0;
    }

    // Validate command-line arguments
    try {
        if (!vm.count("tx-addr")) {
            throw std::logic_error("--tx-addr is required");
        }
    } catch (const std::logic_error &le) {
        std::cerr << "Error: " << le.what() << std::endl;
        std::cerr << "Use --help for usage information" << std::endl;
        return -1;
    }

    // Parse receive address and port
    auto rxRes = string_tuple_to_ip_and_port(rxAddrStr);
    if (rxRes.has_error()) {
        std::cerr << "Invalid receive address: " << rxRes.error().message() << std::endl;
        return -1;
    }
    auto [rxAddr, rxPort] = rxRes.value();

    if (rxPort == 0) {
        std::cerr << "Error: Receive port must be specified in format IP:PORT" << std::endl;
        return -1;
    }

    // Parse destination address and port
    auto destRes = string_tuple_to_ip_and_port(txAddrStr);
    if (destRes.has_error()) {
        std::cerr << "Invalid destination address: " << destRes.error().message() << std::endl;
        return -1;
    }
    auto [destAddr, destPort] = destRes.value();

    if (destPort == 0) {
        std::cerr << "Error: Destination port must be specified in format IP:PORT" << std::endl;
        return -1;
    }

    // Warn if binding to all interfaces
    bool bindingToAll = false;
    if (rxAddr.is_v6()) {
        if (rxAddr.to_string() == "::" || rxAddr.to_string() == "0:0:0:0:0:0:0:0") {
            bindingToAll = true;
        }
    } else {
        if (rxAddr.to_string() == "0.0.0.0") {
            bindingToAll = true;
        }
    }

    if (bindingToAll) {
        std::cerr << "WARNING: Binding to all interfaces (" << rxAddr.to_string() << ") is unsafe for production use!" << std::endl;
        std::cerr << "         Consider binding to a specific interface address instead." << std::endl;
        std::cerr << std::endl;
    }

    // Register signal handler
    signal(SIGINT, signalHandler);

    // Print configuration banner
    std::cout << "E2SAR UDP Relay" << std::endl;
    std::cout << "Version:      " << get_Version() << std::endl;
    std::cout << "Receive:      " << rxAddr.to_string() << ":" << rxPort << " (IPv" << (rxAddr.is_v6() ? "6" : "4") << ")" << std::endl;
    std::cout << "Destination:  " << destAddr.to_string() << ":" << destPort << " (IPv" << (destAddr.is_v6() ? "6" : "4") << ")" << std::endl;
    std::cout << "RX buffer:    " << rxBufSize << " bytes" << std::endl;
    std::cout << "TX buffer:    " << txBufSize << " bytes" << std::endl;
    std::cout << std::endl;

    // Create receive socket
    auto rxSocketRes = createReceiveSocket(rxAddr, rxPort, rxBufSize);
    if (rxSocketRes.has_error()) {
        std::cerr << "Failed to create receive socket: " << rxSocketRes.error().message() << std::endl;
        return -1;
    }
    rxSocket = rxSocketRes.value();
    std::cout << "Receive socket created and bound to " << rxAddr.to_string() << ":" << rxPort << std::endl;

    // Create send socket
    auto txRes = createSendSocket(destAddr, destPort, txBufSize);
    if (txRes.has_error()) {
        std::cerr << "Failed to create send socket: " << txRes.error().message() << std::endl;
        close(rxSocket);
        return -1;
    }
    txSocket = txRes.value();
    std::cout << "Send socket created and connected to " << destAddr.to_string() << ":" << destPort << std::endl;

    // Start relay
    std::cout << "\nRelay active... (Press Ctrl-C to stop)" << std::endl;

    // Start statistics thread
    statsThread = std::thread(statsThreadFunc);

    relayLoop(rxSocket, txSocket);

    // Cleanup handled by signal handler
    return 0;
}
