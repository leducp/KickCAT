#include "kickcat/protocol.h"
#include "kickcat/CoE/mailbox/request.h"

#include <arpa/inet.h>
#include <cstring>
#include <argparse/argparse.hpp>


using namespace kickcat;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("emitter");

    std::string dest_ip;
    program.add_argument("-a", "--address")
        .help("destination IP address")
        .default_value(std::string{"127.0.0.1"})
        .store_into(dest_ip);

    uint16_t dest_port;
    program.add_argument("-p", "--port")
        .help("destination UDP port")
        .default_value(uint16_t{0x88A4})
        .scan<'i', uint16_t>()
        .store_into(dest_port);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    printf("Start emitting to %s:%u\n", dest_ip.c_str(), dest_port);

    int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
    {
        perror("socket()");
        return -1;
    }

    // Destination
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(addr);
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family      = AF_INET;         // IPv4
    addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
    addr.sin_port = hton<uint16_t>(dest_port); // Port is defined in ETG 8200


    // Serial number storage
    uint32_t sn;
    uint32_t sn_size = 4;

    // Local mailbox to generate and process messages
    mailbox::request::Mailbox mailbox;
    mailbox.recv_size = 128;

    // Frame to send/rec on the UDP socket
    uint8_t frame[ETH_MTU_SIZE];
    EthercatHeader* header = reinterpret_cast<EthercatHeader*>(frame);
    header->type = EthercatType::MAILBOX;

    for (int32_t i = 0; i < 256; ++i)
    {
        sleep(2s);

        // Target is an Elmo Gold device
        mailbox.createSDO(0x1018, 4, false, CoE::SDO::request::UPLOAD, &sn, &sn_size);
        auto msg = mailbox.send();
        msg->setAddress(1001);

        std::memcpy(frame + sizeof(EthercatHeader), msg->data(), msg->size());
        header->len = msg->size();

        int32_t sent = ::sendto(fd, &frame, header->len + sizeof(EthercatHeader), MSG_DONTWAIT, (struct sockaddr*)&addr, sizeof(addr));
        if (sent < 0)
        {
            perror("sendto()");
            continue;
        }

        int rec = ::recvfrom(fd, frame, ETH_MTU_SIZE, 0, (struct sockaddr*)&addr, &addr_size);
        if (rec < 0)
        {
            continue;
        }

        if (mailbox.receive(frame + sizeof(EthercatHeader)) == false)
        {
            printf("Mailbox didn't process this message\n");
            continue;
        }

        printf("Serial number %d\n", sn);
        sn = 0;
    }

    return 0;
}
