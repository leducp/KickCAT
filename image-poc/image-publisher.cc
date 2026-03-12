#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <argparse/argparse.hpp>
#include <kickcat/TapSocket.h>
#include "protocol.h"

using namespace kickcat;
using namespace kickcat::image_poc;

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("image-publisher");

    program.add_argument("-i", "--interface")
        .help("Tap interface (shared memory name)")
        .default_value(std::string("/tmp/kickcat_video_poc"));

    program.add_argument("-c", "--camera")
        .help("Camera index")
        .default_value(0)
        .scan<'i', int>();

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
    int camera_index = program.get<int>("--camera");

    // Open camera
    cv::VideoCapture cap(camera_index);
    if (!cap.isOpened())
    {
        std::cerr << "Error: Could not open camera." << std::endl;
        return -1;
    }

    // Initialize TapSocket (publisher initializes the shared memory)
    TapSocket socket(true);
    try
    {
        socket.open(interface);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to open socket: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Publisher started on " << interface << " using camera " << camera_index << std::endl;

    uint32_t frame_id = 0;
    cv::Mat frame;
    std::vector<uint8_t> encoded_frame;
    std::vector<int> compression_params = {cv::IMWRITE_JPEG_QUALITY, 80};

    uint8_t packet[PACKET_SIZE];

    while (true)
    {
        cap >> frame;
        if (frame.empty())
            break;

        // Display locally for debugging
        cv::imshow("Publisher (Local)", frame);

        // Encode frame to JPEG
        cv::imencode(".jpg", frame, encoded_frame, compression_params);

        // Calculate chunks
        uint16_t total_chunks = static_cast<uint16_t>((encoded_frame.size() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE);

        for (uint16_t chunk_idx = 0; chunk_idx < total_chunks; ++chunk_idx)
        {
            ChunkHeader *header = reinterpret_cast<ChunkHeader *>(packet);
            header->frame_id = frame_id;
            header->chunk_idx = chunk_idx;
            header->total_chunks = total_chunks;

            size_t offset = chunk_idx * MAX_CHUNK_SIZE;
            size_t payload_len = std::min(MAX_CHUNK_SIZE, encoded_frame.size() - offset);
            header->payload_len = static_cast<uint32_t>(payload_len);

            std::memcpy(packet + sizeof(ChunkHeader), encoded_frame.data() + offset, payload_len);

            socket.write(packet, static_cast<int32_t>(sizeof(ChunkHeader) + payload_len));
        }

        frame_id++;

        if (cv::waitKey(33) == 'q') // ~30 FPS
            break;
    }

    cap.release();
    cv::destroyAllWindows();
    return 0;
}
