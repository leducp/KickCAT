#ifndef KICKCAT_FOE_MAILBOX_RESPONSE_H
#define KICKCAT_FOE_MAILBOX_RESPONSE_H

#include <map>
#include <string>
#include <vector>

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/protocol.h"

namespace kickcat::mailbox::response
{
    // Application-owned file storage backing the slave's FoE server. read()/write() return 0 on
    // success or an FoE::result error code (e.g. NOT_FOUND); 0 is not a valid FoE code, so it is a
    // safe success sentinel.
    class AbstractFileSystem
    {
    public:
        virtual ~AbstractFileSystem() = default;
        virtual uint32_t read(std::string const& name, uint32_t password, std::vector<uint8_t>& out) = 0;
        virtual uint32_t write(std::string const& name, uint32_t password, std::vector<uint8_t> const& data) = 0;
    };

    // Trivial in-memory file system, handy for the emulator and tests (password is ignored).
    class InMemoryFileSystem final : public AbstractFileSystem
    {
    public:
        uint32_t read(std::string const& name, uint32_t password, std::vector<uint8_t>& out) override;
        uint32_t write(std::string const& name, uint32_t password, std::vector<uint8_t> const& data) override;

        void setFile(std::string const& name, std::vector<uint8_t> data);
        bool hasFile(std::string const& name) const;
        std::vector<uint8_t> const& file(std::string const& name) const;

    private:
        std::map<std::string, std::vector<uint8_t>> files_;
    };

    std::shared_ptr<AbstractMessage> createFoEMessage(
            Mailbox* mbx,
            std::vector<uint8_t>&& raw_message);

    // Serves a single FoE transfer: a Read Request streams a file to the master, a Write Request
    // accumulates one from it. Subsequent Data/Ack packets arrive through process(raw_message).
    class FoEMessage final : public AbstractMessage
    {
    public:
        FoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~FoEMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        ProcessingResult startRead();
        ProcessingResult startWrite();
        ProcessingResult sendData();                              // serve the next Data packet (read)
        ProcessingResult onAck(uint32_t packet_number);           // master acked a Data packet (read)
        ProcessingResult onData(FoE::Header const* foe, uint16_t len); // master sent a Data packet (write)
        void sendError(uint32_t code);
        void sendAck(uint32_t packet_number);

        bool      started_{false};
        bool      writing_{false};
        std::string file_name_;
        uint32_t  password_{0};

        std::vector<uint8_t> buffer_;     // file content (read: source, write: accumulator)
        uint32_t  offset_{0};             // bytes served (read)
        uint32_t  packet_number_{0};
        uint32_t  expected_packet_{1};    // next Data packet number (write)
        uint32_t  max_data_;
        bool      last_data_{false};      // final Data packet has been sent (read)
    };
}

#endif
