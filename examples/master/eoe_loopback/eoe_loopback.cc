// In-process EoE demonstration: a master request mailbox and a slave response mailbox exchange
// EoE parameter services and tunneled Ethernet frames without any OS networking or hardware.
// It shows the protocol layer end to end - fragmentation, reassembly, Set IP and the
// bidirectional, buffer-free frame callbacks.

#include <cstdio>
#include <cstring>
#include <vector>

#include "kickcat/EoE/mailbox/request.h"
#include "kickcat/EoE/mailbox/response.h"
#include "kickcat/Mailbox.h"

using namespace kickcat;

namespace
{
    constexpr uint16_t MBX_SIZE = 128;

    // A toy slave-side configuration: it just stores the IP parameters and accepts filters.
    struct DemoConfig : EoE::SlaveConfig
    {
        EoE::IpParameters params{};

        uint16_t setIpParameter(EoE::IpParameters const& p) override
        {
            params = p;
            printf("[slave] Set IP -> %u.%u.%u.%u\n", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);
            return EoE::result::SUCCESS;
        }
        uint16_t getIpParameter(EoE::IpParameters& p) override
        {
            p = params;
            return EoE::result::SUCCESS;
        }
        uint16_t setAddressFilter(uint8_t const*, size_t len) override
        {
            printf("[slave] Set address filter (%zu payload bytes)\n", len);
            return EoE::result::SUCCESS;
        }
    };

    std::vector<uint8_t> makeFrame(size_t len)
    {
        std::vector<uint8_t> frame(len);
        for (size_t i = 0; i < len; ++i)
        {
            frame[i] = static_cast<uint8_t>(i & 0xFF);
        }
        return frame;
    }
}

int main()
{
    mailbox::request::Mailbox master;
    master.recv_size = MBX_SIZE;
    master.send_size = MBX_SIZE;

    mailbox::response::Mailbox slave{MBX_SIZE, 8};
    DemoConfig config;
    slave.enableEoE(config, [](uint8_t const*, size_t len, uint8_t port)
    {
        printf("[slave] received Ethernet frame: %zu bytes on port %u\n", len, port);
    });

    // --- Set IP parameter (master -> slave, request/response) ---
    EoE::IpParameters ip{};
    ip.ip_included = true;
    ip.ip[0] = 192; ip.ip[1] = 168; ip.ip[2] = 1; ip.ip[3] = 50;
    auto set_ip = master.createEoESetIP(ip, 1s);
    {
        auto sent = master.send();
        auto reply = slave.processRequest(std::vector<uint8_t>(sent->data(), sent->data() + sent->size()));
        master.receive(reply.data());
    }
    printf("[master] Set IP status: 0x%x\n", set_ip->status());

    // --- Tunnel a frame master -> slave (fragmented over the 128-byte mailbox) ---
    auto frame = makeFrame(1500);
    printf("[master] sending %zu-byte frame to slave\n", frame.size());
    master.sendEoEFrame(frame.data(), frame.size(), 1);
    while (not master.to_send.empty())
    {
        auto sent = master.send();
        slave.handleMessage(std::vector<uint8_t>(sent->data(), sent->data() + sent->size()));
        slave.process();
    }

    // --- Tunnel a frame slave -> master (unsolicited) ---
    std::vector<uint8_t> received;
    auto receiver = std::make_shared<mailbox::request::EoEReceiveMessage>(master.recv_size);
    receiver->setFrameSink([&](uint8_t const* f, size_t len, uint8_t port)
    {
        received.assign(f, f + len);
        printf("[master] received Ethernet frame: %zu bytes on port %u\n", len, port);
    });
    master.to_process.push_back(receiver);

    auto reply_frame = makeFrame(800);
    printf("[slave] pushing %zu-byte frame to master\n", reply_frame.size());
    slave.sendEoEFrame(reply_frame.data(), reply_frame.size(), 2);
    while (true)
    {
        auto raw = slave.popReply();
        if (raw.empty())
        {
            break;
        }
        master.receive(raw.data());
    }

    if (received == reply_frame)
    {
        printf("OK: round-trip frame matched\n");
        return 0;
    }
    printf("ERROR: round-trip frame mismatch\n");
    return 1;
}
