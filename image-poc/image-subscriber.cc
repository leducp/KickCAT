#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <argparse/argparse.hpp>
#include <kickcat/TapSocket.h>
#include "protocol.h"

using namespace kickcat;
using namespace kickcat::image_poc;

struct FrameAssembler
{
    uint32_t frame_id;
    uint16_t total_chunks;
    uint16_t chunks_received;
    std::map<uint16_t, std::vector<uint8_t>> chunks;

    FrameAssembler(uint32_t id, uint16_t total)
        : frame_id(id), total_chunks(total), chunks_received(0) {}

    bool isComplete() const { return chunks_received == total_chunks; }

    std::vector<uint8_t> reassemble()
    {
        std::vector<uint8_t> result;
        for (uint16_t i = 0; i < total_chunks; ++i)
        {
            auto &chunk = chunks[i];
            result.insert(result.end(), chunk.begin(), chunk.end());
        }
        return result;
    }
};

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("image-subscriber");

    program.add_argument("-i", "--interface")
        .help("Tap interface (shared memory name)")
        .default_value(std::string("/tmp/kickcat_video_poc"));

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::string interface = program.get<std::string>("--interface");

    TapSocket socket(false); // init = false for the subscriber
    try
    {
        socket.open(interface);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to open socket: " << e.what() << std::endl;
        return 1;
    }

    // Set a short timeout to not block too long
    socket.setTimeout(1ms);

    std::cout << "Subscriber started on " << interface << ". Press 'q' to quit." << std::endl;

    std::map<uint32_t, FrameAssembler> assemblers;
    uint32_t last_displayed_frame = 0;

    uint8_t packet[PACKET_SIZE];

    while (true)
    {
        int32_t bytes_read = socket.read(packet, PACKET_SIZE);

        if (bytes_read > static_cast<int32_t>(sizeof(ChunkHeader)))
        {
            const ChunkHeader *header = reinterpret_cast<const ChunkHeader *>(packet);

            // If it's a new frame, create an assembler
            if (assemblers.find(header->frame_id) == assemblers.end())
            {
                // If we're getting a frame that's too old, ignore it
                if (header->frame_id >= last_displayed_frame)
                {
                    assemblers.emplace(header->frame_id, FrameAssembler(header->frame_id, header->total_chunks));
                }
            }

            if (assemblers.count(header->frame_id))
            {
                auto &assembler = assemblers.at(header->frame_id);
                if (assembler.chunks.find(header->chunk_idx) == assembler.chunks.end())
                {
                    std::vector<uint8_t> payload(packet + sizeof(ChunkHeader),
                                                 packet + sizeof(ChunkHeader) + header->payload_len);
                    assembler.chunks[header->chunk_idx] = std::move(payload);
                    assembler.chunks_received++;

                    if (assembler.isComplete())
                    {
                        std::vector<uint8_t> full_frame = assembler.reassemble();
                        cv::Mat img = cv::imdecode(full_frame, cv::IMREAD_COLOR);

                        if (!img.empty())
                        {
                            cv::imshow("Subscriber (Reassembled)", img);
                            last_displayed_frame = header->frame_id;

                            // Cleanup old assemblers
                            for (auto it = assemblers.begin(); it != assemblers.end();)
                            {
                                if (it->first <= last_displayed_frame)
                                {
                                    it = assemblers.erase(it);
                                }
                                else
                                {
                                    ++it;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (cv::waitKey(1) == 'q')
            break;
    }

    cv::destroyAllWindows();
    return 0;
}
