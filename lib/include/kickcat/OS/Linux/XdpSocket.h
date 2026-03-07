#ifndef KICKCAT_LINUX_XDP_SOCKET_H
#define KICKCAT_LINUX_XDP_SOCKET_H

#include <vector>
#include <cstdint>

#include <xdp/xsk.h>

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    class XdpSocket final : public AbstractSocket
    {
    public:
        static constexpr uint32_t NUM_FRAMES      = 4096;
        static constexpr uint32_t FRAME_SIZE       = 2048;
        static constexpr uint32_t RX_RING_SIZE     = 2048;
        static constexpr uint32_t TX_RING_SIZE     = 2048;
        static constexpr uint32_t FILL_RING_SIZE   = 2048;
        static constexpr uint32_t COMP_RING_SIZE   = 2048;

        XdpSocket(nanoseconds polling_period = 20us, uint32_t queue_id = 0);
        virtual ~XdpSocket()
        {
            close();
        }

        void open(std::string const& interface) override;
        void setTimeout(nanoseconds timeout) override;
        void close() noexcept override;
        int32_t read(void* frame, int32_t frame_size) override;
        int32_t write(void const* frame, int32_t frame_size) override;

    private:
        void reclaimCompletedTx();
        void refillFillRing();
        uint64_t allocFrame();
        void freeFrame(uint64_t addr);
        void loadXdpProgram(int ifindex);

        struct xsk_umem*       umem_{nullptr};
        struct xsk_socket*     xsk_{nullptr};
        struct xsk_ring_prod   fill_ring_{};
        struct xsk_ring_cons   comp_ring_{};
        struct xsk_ring_prod   tx_ring_{};
        struct xsk_ring_cons   rx_ring_{};
        struct bpf_object*     bpf_obj_{nullptr};
        int                    xdp_prog_fd_{-1};

        uint8_t*               umem_area_{nullptr};
        std::vector<uint64_t>  free_frames_;

        nanoseconds            timeout_{};
        nanoseconds            polling_period_;
        uint32_t               queue_id_;
        int                    ifindex_{0};
    };
}

#endif
