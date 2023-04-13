#include "kickcat/protocol.h"
#include "kickcat/Mailbox.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif


#include <arpa/inet.h>
#include <cstring>


using namespace kickcat;

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;
    printf("Start\n");

    int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
    {
        perror("socket()");
        return -1;
    }

    // Destination
    struct sockaddr_in addr;
    socklen_t addr_size;
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family      = AF_INET;         // IPv4
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = hton<uint16_t>(0x88A4); // Port is defined in ETG 8200


    // Serial number storage
//    uint32_t sn = 0;
//    uint32_t sn_size = 4;

    // Local mailbox to generate and process messages
    Mailbox mailbox;
    mailbox.recv_size = 128;

    // Frame to send/rec on the UDP socket
    uint8_t frame[ETH_MTU_SIZE];
    EthercatHeader* header = reinterpret_cast<EthercatHeader*>(frame);
    header->type = EthercatType::MAILBOX;

//    for (int32_t i = 0; i < 256; ++i)
//    {
//        sleep(2s);
//
//        // Target is an Elmo Gold device
//        mailbox.createSDO(0x1018, 4, false, CoE::SDO::request::UPLOAD, &sn, &sn_size);
//        auto msg = mailbox.send();
//        msg->setAddress(1001);
//
//        std::memcpy(frame + sizeof(EthercatHeader), msg->data(), msg->size());
//        header->len = msg->size();
//
//        int32_t sent = ::sendto(fd, &frame, header->len + sizeof(EthercatHeader), MSG_DONTWAIT, (struct sockaddr*)&addr, sizeof(addr));
//        if (sent < 0)
//        {
//            perror("sendto()");
//            continue;
//        }
//
//        int rec = ::recvfrom(fd, frame, ETH_MTU_SIZE, 0, (struct sockaddr*)&addr, &addr_size);
//        if (rec < 0)
//        {
//            continue;
//        }
//
//        if (mailbox.receive(frame + sizeof(EthercatHeader)) == false)
//        {
//            printf("Mailbox didn't process this message\n");
//            continue;
//        }
//
//        printf("Serial number %d\n", sn);
//        sn = 0;
//    }

    // Try to access to master mailbox data

    //Random SDO, (Target is an Elmo Gold device)
    //mailbox.createSDO(0x1018, 4, false, CoE::SDO::request::UPLOAD, &sn, &sn_size);









//    uint32_t device_type = 5;
//    uint32_t device_type_size = 4;
//    mailbox.createSDO(0x1000, 0, false, CoE::SDO::request::UPLOAD, &device_type, &device_type_size);

//    CoE::IdentityObject identity{1,2,3,4,5};
//    uint32_t identity_size = sizeof(identity);
//    printf("client identity size %i \n", identity_size);
//    mailbox.createSDO(0x1018, 1, true, CoE::SDO::request::UPLOAD, &identity.vendor_id, &identity_size);


    std::string device_name;
    device_name.resize(50);
    uint32_t device_name_size = device_name.size();
    mailbox.createSDO(0x1008, 0, false, CoE::SDO::request::UPLOAD, device_name.data(), &device_name_size);

    auto msg = mailbox.send();
    msg->setAddress(0); // target master

    std::memcpy(frame + sizeof(EthercatHeader), msg->data(), msg->size());
    header->len = msg->size();

    int32_t sent = ::sendto(fd, &frame, header->len + sizeof(EthercatHeader), MSG_DONTWAIT, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0)
    {
        perror("sendto()");
    }

    int rec = ::recvfrom(fd, frame, ETH_MTU_SIZE, 0, (struct sockaddr*)&addr, &addr_size);
    if (rec < 0)
    {
        printf("Nothing to read \n");
    }

    if (mailbox.receive(frame + sizeof(EthercatHeader)) == false)
    {
        printf("Mailbox didn't process this message\n");
    }


    mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(frame + sizeof(EthercatHeader) + sizeof(mailbox::Header));

    if (coe->service == CoE::Service::SDO_RESPONSE)
    {
//        printf("Device type received %i \n", device_type);

//        printf("Identity number_of_entries %i \n", identity.number_of_entries);
//        printf("Identity vendor_id %i \n", identity.vendor_id);
//        printf("Identity serial_number %i \n", identity.serial_number);

        printf("Device name received %s \n", device_name.c_str());
    }

    return 0;
}
