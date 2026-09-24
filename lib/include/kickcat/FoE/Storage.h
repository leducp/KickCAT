#ifndef KICKCAT_FOE_STORAGE_H
#define KICKCAT_FOE_STORAGE_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>

namespace kickcat::FoE
{
    // Every call returns 0 on success or an FoE error code (FoE::result), sent to the master as an FoE error.

    /// \brief One FoE read: the file content, chunk by chunk. Destroyed when the transfer ends, whatever the outcome.
    class AbstractReader
    {
    public:
        virtual ~AbstractReader() = default;

        /// \param size Bytes put in buffer: less than capacity means end of file
        virtual uint32_t read(uint8_t* buffer, uint32_t capacity, uint32_t& size) = 0;
    };

    /// \brief One FoE write. Destroyed without a successful commit(), the transfer failed: what was written shall be
    ///        discarded.
    class AbstractWriter
    {
    public:
        virtual ~AbstractWriter() = default;

        virtual uint32_t write(uint8_t const* data, uint32_t size) = 0;

        /// \brief Last packet received. An error is sent to the master in place of the last ACK.
        virtual uint32_t commit() = 0;
    };

    /// \brief What a slave serves over FoE
    class AbstractStorage
    {
    public:
        virtual ~AbstractStorage() = default;

        /// \return 0 and the reader of the file, or an error code
        virtual std::tuple<uint32_t, std::unique_ptr<AbstractReader>> openRead(std::string_view name, uint32_t password) = 0;

        /// \return 0 and the writer of the file, or an error code
        virtual std::tuple<uint32_t, std::unique_ptr<AbstractWriter>> openWrite(std::string_view name, uint32_t password) = 0;
    };
}

#endif
