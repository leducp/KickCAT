#ifndef KICKCAT_MOCK_LINK_H
#define KICKCAT_MOCK_LINK_H

#include <queue>
#include <vector>
#include <cstring>
#include <gtest/gtest.h>

#include "kickcat/AbstractLink.h"

namespace kickcat
{
    class MockLink : public AbstractLink
    {
    public:
        struct PendingDatagram
        {
            Command command{};
            uint32_t address{};
            std::vector<uint8_t> data;
            uint16_t data_size{};
            std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> process;
            std::function<void(DatagramState const& state)> error;
        };

        void handleWriteThenRead(Command expected_command, uint16_t wkc = 1)
        {
            wtr_responses_.push({expected_command, wkc});
        }

        void writeThenRead(Frame& frame) override
        {
            ASSERT_FALSE(wtr_responses_.empty()) << "MockLink: no writeThenRead response queued";
            auto [expected_command, wkc] = wtr_responses_.front();
            wtr_responses_.pop();

            frame.finalize();

            while (true)
            {
                auto [header, data, wkc_ptr] = frame.peekDatagram();
                if (header == nullptr)
                {
                    break;
                }
                ASSERT_EQ(header->command, expected_command)
                    << "MockLink: unexpected command in writeThenRead";
                *wkc_ptr = wkc;
            }

            frame.resetContext();
            frame.setIsDatagramAvailable();
        }

        void addDatagram(enum Command command, uint32_t address, void const* data, uint16_t data_size,
                         std::function<DatagramState(DatagramHeader const*, uint8_t const* data, uint16_t wkc)> const& process,
                         std::function<void(DatagramState const& state)> const& error) override
        {
            PendingDatagram dg;
            dg.command = command;
            dg.address = address;
            dg.data_size = data_size;
            dg.process = process;
            dg.error = error;
            if (data != nullptr && data_size > 0)
            {
                dg.data.resize(data_size);
                std::memcpy(dg.data.data(), data, data_size);
            }
            pending_datagrams_.push_back(std::move(dg));
        }

        template<typename T>
        void handleProcess(Command expected_command, T const& reply_data, uint16_t wkc = 1)
        {
            QueuedResponse response;
            response.expected_command = expected_command;
            response.data.resize(sizeof(T));
            std::memcpy(response.data.data(), &reply_data, sizeof(T));
            response.wkc = wkc;
            queued_responses_.push(std::move(response));
        }

        void processDatagrams() override
        {
            std::exception_ptr client_exception;

            for (auto& dg : pending_datagrams_)
            {
                ASSERT_FALSE(queued_responses_.empty())
                    << "MockLink: no response queued for datagram (command="
                    << static_cast<int>(dg.command) << ")";

                auto response = std::move(queued_responses_.front());
                queued_responses_.pop();

                ASSERT_EQ(dg.command, response.expected_command)
                    << "MockLink: unexpected command in processDatagrams";

                uint16_t buffer_size = std::max(dg.data_size, static_cast<uint16_t>(response.data.size()));
                std::vector<uint8_t> buffer(buffer_size, 0);
                std::memcpy(buffer.data(), response.data.data(), response.data.size());

                DatagramHeader header{};
                header.command = dg.command;
                header.len = buffer_size;

                DatagramState status = dg.process(&header, buffer.data(), response.wkc);
                if (status != DatagramState::OK)
                {
                    try
                    {
                        dg.error(status);
                    }
                    catch (...)
                    {
                        client_exception = std::current_exception();
                    }
                }
            }
            pending_datagrams_.clear();

            if (client_exception)
            {
                std::rethrow_exception(client_exception);
            }
        }

        void finalizeDatagrams() override {}

        void setTimeout(nanoseconds const&) override {}

        void checkRedundancyNeeded() override {}

        void attachEcatEventCallback(enum EcatEvent, std::function<void()>) override {}

        std::vector<PendingDatagram> const& pendingDatagrams() const { return pending_datagrams_; }

    private:
        struct QueuedResponse
        {
            Command expected_command{};
            std::vector<uint8_t> data;
            uint16_t wkc{};
        };

        std::vector<PendingDatagram> pending_datagrams_;
        std::queue<QueuedResponse> queued_responses_;
        struct WtrResponse // wtr: writeThenRead
        {
            Command expected_command{};
            uint16_t wkc{};
        };
        std::queue<WtrResponse> wtr_responses_;
    };
}

#endif
