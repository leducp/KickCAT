#include <cstring>

#include "OS/Windows/Socket.h"
#include "OS/Time.h"
#include "protocol.h"
#include "debug.h"

#include <pcap.h>

namespace kickcat
{
    std::string NetworkInterface::format() const
    {
        return name + " (" + description + ")";
    }

    std::vector<NetworkInterface> listInterfaces()
    {
        std::vector<NetworkInterface> interfaces;
        pcap_if_t* ifs;

        char error[PCAP_ERRBUF_SIZE];
        if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, nullptr, &ifs, error) == -1)
        {
            fprintf(stderr,"Error in pcap_findalldevs_ex: %s\n", error);
            exit(1);
        }

        // Print the list
        for(pcap_if_t* d = ifs; d != nullptr; d = d->next)
        {
            NetworkInterface netif;
            netif.name = d->name;
            if (d->description)
            {
                netif.description = d->description;
            }
            else
            {
                netif.description = "nil";
            }
            interfaces.push_back(std::move(netif));
        }

        pcap_freealldevs(ifs);
        return interfaces;
    }

    Socket::Socket(nanoseconds polling_period)
        : AbstractSocket()
        , fd_{nullptr}
        , polling_period_(polling_period)
    {
        error_.resize(PCAP_ERRBUF_SIZE);
    }

    void Socket::open(std::string const& ifname)
    {
        fd_ = pcap_open(ifname.c_str(), 65536,
                            PCAP_OPENFLAG_PROMISCUOUS |
                            PCAP_OPENFLAG_MAX_RESPONSIVENESS |
                            PCAP_OPENFLAG_NOCAPTURE_LOCAL,
                            -1, //-1 -> 0 ?
                            nullptr , error_.data());

        if (fd_ == nullptr)
        {
            throw std::runtime_error{error_.data()};
        }

        // Filter to retrieve only our packets
        auto mac2str = [](MAC const& mac)
        {
            char buffer[18];
            snprintf(buffer, sizeof(buffer),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return std::string(buffer);
        };

        std::string primary_mac   = mac2str(PRIMARY_IF_MAC);
        std::string secondary_mac = mac2str(SECONDARY_IF_MAC);
        std::string filter = "ether host " + primary_mac + " or ether host " + secondary_mac;

        struct bpf_program fp;
        if (pcap_compile(static_cast<pcap_t*>(fd_), &fp, filter.c_str(), 0, PCAP_NETMASK_UNKNOWN ) == -1)
        {
            throw std::runtime_error{pcap_geterr(static_cast<pcap_t*>(fd_))};
        }

        if (pcap_setfilter(static_cast<pcap_t*>(fd_), &fp) == -1)
        {
            throw std::runtime_error{"set filter error"};
        }
    }

    void Socket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }

    void Socket::close() noexcept
    {
        if (fd_ == nullptr)
        {
            return;
        }

        pcap_close(static_cast<pcap_t*>(fd_));
    }

    int32_t Socket::read(void* frame, int32_t frame_size)
    {
        struct pcap_pkthdr* header;
        unsigned char const* data;

        nanoseconds deadline = since_epoch() + timeout_;
        do
        {
            int r = pcap_next_ex(static_cast<pcap_t*>(fd_), &header, &data);
            if (r != 1)
            {
                sleep(polling_period_);
                continue;
            }

            int32_t to_copy = std::min(static_cast<int32_t>(header->len), frame_size);
            std::memcpy(frame, data, to_copy);
            return to_copy;

        } while (since_epoch() < deadline);

        errno = ETIMEDOUT;
        return -1;
    }

    int32_t Socket::write(void const* frame, int32_t frame_size)
    {
        int r = pcap_sendpacket(static_cast<pcap_t*>(fd_), static_cast<unsigned char const*>(frame), frame_size);
        if (r == 0)
        {
            return frame_size;
        }
        return -1;
    }
}
