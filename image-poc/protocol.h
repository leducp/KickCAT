#ifndef IMAGE_POC_PROTOCOL_H
#define IMAGE_POC_PROTOCOL_H

#include <cstdint>

namespace kickcat::image_poc
{
    struct ChunkHeader
    {
        uint32_t frame_id;
        uint16_t chunk_idx;
        uint16_t total_chunks;
        uint32_t payload_len;
    };

    static constexpr size_t MAX_CHUNK_SIZE = 1400;
    static constexpr size_t PACKET_SIZE = sizeof(ChunkHeader) + MAX_CHUNK_SIZE;
}

#endif
