/* **************************************************
 * wol.cpp - Simple Wake-On-LAN utility to wake a networked PC.
 * Usage: wol [-q] [-b <bcast>] [-p <port>] <dest>
 * Compile it with: g++ -Wall -Os -DNDEBUG -std=c++11 -o wol wol.cpp
 *
 * ************************************************** */

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    // RAII class to manage socket file descriptor lifecycle
    class socket_handle
    {
    public:
        explicit socket_handle(int descriptor) : _descriptor(descriptor) {
            if (_descriptor < 0)
                throw std::runtime_error("Failed to open socket");
        }
        socket_handle(const socket_handle&) = delete;
        socket_handle& operator=(const socket_handle&) = delete;

        int get() const {
            return _descriptor;
        }

        ~socket_handle() {
            close(_descriptor);
        }

    private:
        const int _descriptor;
    };

    // Function to print usage instructions
    void print_usage(const std::string& progname)
    {
        std::cerr << "Usage: " << progname << " [-q] [-b <bcast>] [-p <port>] <dest>\n";
    }

    // Function to convert hexadecimal ASCII string to an integer
    unsigned get_hex_from_string(const std::string& s)
    {
        unsigned hex{0};
        
        for (size_t i = 0; i < s.length(); ++i) {
            hex <<= 4;
            if (isdigit(s[i])) {
                hex |= s[i] - '0';
            }
            else if (s[i] >= 'a' && s[i] <= 'f') {
                hex |= s[i] - 'a' + 10;
            }
            else if (s[i] >= 'A' && s[i] <= 'F') {
                hex |= s[i] - 'A' + 10;
            }
            else {
                throw std::runtime_error("Failed to parse hexadecimal " + s);
            }
        }
        return hex;
    }

    // Function to convert a hardware address string to a binary Ethernet address
    std::string get_ether(const std::string& hardware_addr)
    {
        std::string ether_addr;

        for (size_t i = 0; i < hardware_addr.length();) {
            // Parse two characters at a time
            unsigned hex = get_hex_from_string(hardware_addr.substr(i, 2));
            i += 2;

            ether_addr += static_cast<char>(hex & 0xFF);

            // Skip colon if present
            if (hardware_addr[i] == ':') 
                ++i;
        }

        // Ensure the hardware address is 6 bytes long
        if (ether_addr.length() != 6)
            throw std::runtime_error(hardware_addr + " not a valid ether address");

        return ether_addr;
    }

    // Function to send a Wake-On-LAN packet
    void send_wol(const std::string& hardware_addr, unsigned port, unsigned long bcast)
    {
        // Convert the hardware address to binary format
        const std::string ether_addr{get_ether(hardware_addr)};

        // Create a UDP socket
        socket_handle packet{socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};

        // Build the WOL magic packet
        //   (6 * 0XFF followed by 16 * destination address) 
        std::string message(6, 0xFF);
        for (size_t i = 0; i < 16; ++i) {
            message += ether_addr;
        }

        // Set socket options to allow broadcast
        const int optval{1};
        if (setsockopt(packet.get(), SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) < 0) {
            throw std::runtime_error("Failed to set socket options");
        }

        // Set up the address structure
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = bcast;
        addr.sin_port = htons(port);

        // Send the magic packet
        if (sendto(packet.get(), message.c_str(), message.length(), 0, 
            reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("Failed to send packet");
        }
    }
}

int main(int argc, char * const argv[])
{
    try
    {
        int c{0};
        unsigned port{60000};
        bool quiet{false};
        unsigned long bcast{0xFFFFFFFF};

        // Parse command-line options
        while ((c = getopt(argc, argv, "hqb:p:d:")) != -1) {
            switch (c) {
                case 'h': // help
                    print_usage(argv[0]);
                    return 1;
                case 'q': // quiet
                    quiet = true;
                    break;
                case 'b': // broadcast address
                    bcast = inet_addr(optarg);
                    if (bcast == INADDR_NONE) {
                        throw std::runtime_error("Option -b requires address as argument");
                    }
                    break;
                case 'p': // port
                    port = strtol(optarg, NULL, 0);
                    if (port == 0 && errno != 0) {
                        throw std::runtime_error("Option -p requires integer as argument.");
                    }
                    break;
                case '?': // unrecognized option
                    if (optopt == 'b' || optopt == 'p' || optopt == 'd') {
                        throw std::runtime_error(std::string("Option -") + static_cast<char>(optopt) + " requires an argument");
                    } else {
                        throw std::runtime_error(std::string("Unknown option '-") + static_cast<char>(optopt) + "'");
                    }
                default:
                    throw std::runtime_error("Internal error");
            }
        }

        // Parse any remaining arguments (not options)
        if (optind == argc - 1) {
            // Send the Wake-On-LAN packet
            send_wol(argv[optind], port, bcast);
            
            // Print confirmation if not in quiet mode
            if (!quiet) {
                std::cout << "Packet sent to " << std::hex << std::uppercase << htonl(bcast) << '-' << argv[optind] 
                    << " on port " << std::dec << port << '\n';
            }
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }
    catch(const std::exception& e) {
        // Print error message and return error code
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
