#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_link.h>

#include <cerrno>
#include <cstring>
#include <algorithm>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <xdp/xsk.h>

#include "OS/Linux/XdpSocket.h"
#include "OS/Time.h"
#include "debug.h"

#include "xdp_ethercat_bpf.h"

namespace kickcat
{
    XdpSocket::XdpSocket(nanoseconds polling_period, uint32_t queue_id)
        : AbstractSocket()
        , polling_period_(polling_period)
        , queue_id_(queue_id)
    {
    }


    void XdpSocket::loadXdpProgram(int ifindex)
    {
        bpf_obj_ = bpf_object__open_mem(xdp_ethercat_bpf, xdp_ethercat_bpf_len, nullptr);
        if (bpf_obj_ == nullptr)
        {
            THROW_ERROR("bpf_object__open_mem() failed");
        }

        int rc = bpf_object__load(bpf_obj_);
        if (rc != 0)
        {
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            THROW_SYSTEM_ERROR("bpf_object__load()");
        }

        struct bpf_program* prog = bpf_object__find_program_by_name(bpf_obj_, "xdp_ethercat_filter");
        if (prog == nullptr)
        {
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            THROW_ERROR("Cannot find xdp_ethercat_filter program in BPF object");
        }

        xdp_prog_fd_ = bpf_program__fd(prog);
        rc = bpf_xdp_attach(ifindex, xdp_prog_fd_, XDP_FLAGS_SKB_MODE, nullptr);
        if (rc != 0)
        {
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            xdp_prog_fd_ = -1;
            THROW_SYSTEM_ERROR("bpf_xdp_attach()");
        }

        struct bpf_map* xsks_map = bpf_object__find_map_by_name(bpf_obj_, "xsks_map");
        if (xsks_map == nullptr)
        {
            bpf_xdp_detach(ifindex, XDP_FLAGS_SKB_MODE, nullptr);
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            xdp_prog_fd_ = -1;
            THROW_ERROR("Cannot find xsks_map in BPF object");
        }

        int xsks_map_fd = bpf_map__fd(xsks_map);
        int xsk_fd = xsk_socket__fd(xsk_);
        rc = bpf_map_update_elem(xsks_map_fd, &queue_id_, &xsk_fd, 0);
        if (rc != 0)
        {
            bpf_xdp_detach(ifindex, XDP_FLAGS_SKB_MODE, nullptr);
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            xdp_prog_fd_ = -1;
            THROW_SYSTEM_ERROR("bpf_map_update_elem(xsks_map)");
        }
    }


    void XdpSocket::open(std::string const& interface)
    {
        ifindex_ = static_cast<int>(if_nametoindex(interface.c_str()));
        if (ifindex_ == 0)
        {
            THROW_SYSTEM_ERROR("if_nametoindex()");
        }

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            THROW_SYSTEM_ERROR("socket(AF_INET)");
        }

        struct ifreq ifr = {};
        std::strncpy(ifr.ifr_name, interface.c_str(), sizeof(ifr.ifr_name) - 1);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
        {
            ::close(sock);
            THROW_SYSTEM_ERROR("ioctl(SIOCGIFFLAGS)");
        }
        ifr.ifr_flags |= IFF_PROMISC | IFF_BROADCAST | IFF_UP;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
        {
            ::close(sock);
            THROW_SYSTEM_ERROR("ioctl(SIOCSIFFLAGS)");
        }
        ::close(sock);

        uint64_t umem_size = static_cast<uint64_t>(NUM_FRAMES) * FRAME_SIZE;
        umem_area_ = static_cast<uint8_t*>(
            mmap(nullptr, umem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (umem_area_ == MAP_FAILED)
        {
            umem_area_ = nullptr;
            THROW_SYSTEM_ERROR("mmap(UMEM)");
        }

        struct xsk_umem_config umem_cfg = {};
        umem_cfg.fill_size      = FILL_RING_SIZE;
        umem_cfg.comp_size      = COMP_RING_SIZE;
        umem_cfg.frame_size     = FRAME_SIZE;
        umem_cfg.frame_headroom = 0;
        umem_cfg.flags          = 0;

        int rc = xsk_umem__create(&umem_, umem_area_, umem_size, &fill_ring_, &comp_ring_, &umem_cfg);
        if (rc != 0)
        {
            munmap(umem_area_, umem_size);
            umem_area_ = nullptr;
            THROW_SYSTEM_ERROR("xsk_umem__create()");
        }

        struct xsk_socket_config xsk_cfg = {};
        xsk_cfg.rx_size      = RX_RING_SIZE;
        xsk_cfg.tx_size      = TX_RING_SIZE;
        xsk_cfg.bind_flags   = XDP_USE_NEED_WAKEUP;
        xsk_cfg.xdp_flags    = 0;
        xsk_cfg.libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;

        rc = xsk_socket__create(&xsk_, interface.c_str(), queue_id_, umem_, &rx_ring_, &tx_ring_, &xsk_cfg);
        if (rc != 0)
        {
            xsk_umem__delete(umem_);
            umem_ = nullptr;
            munmap(umem_area_, umem_size);
            umem_area_ = nullptr;
            THROW_SYSTEM_ERROR("xsk_socket__create()");
        }

        free_frames_.reserve(NUM_FRAMES);
        for (uint32_t i = 0; i < NUM_FRAMES; ++i)
        {
            free_frames_.push_back(static_cast<uint64_t>(i) * FRAME_SIZE);
        }

        refillFillRing();
        loadXdpProgram(ifindex_);

        socket_info("AF_XDP socket opened on %s (queue %u)\n", interface.c_str(), queue_id_);
    }


    void XdpSocket::refillFillRing()
    {
        uint32_t to_fill = xsk_prod_nb_free(&fill_ring_, free_frames_.size());
        if (to_fill == 0 or free_frames_.empty())
        {
            return;
        }

        to_fill = std::min(to_fill, static_cast<uint32_t>(free_frames_.size()));

        uint32_t idx = 0;
        if (xsk_ring_prod__reserve(&fill_ring_, to_fill, &idx) != to_fill)
        {
            return;
        }

        for (uint32_t i = 0; i < to_fill; ++i)
        {
            *xsk_ring_prod__fill_addr(&fill_ring_, idx + i) = free_frames_.back();
            free_frames_.pop_back();
        }

        xsk_ring_prod__submit(&fill_ring_, to_fill);

        if (xsk_ring_prod__needs_wakeup(&fill_ring_))
        {
            recvfrom(xsk_socket__fd(xsk_), nullptr, 0, MSG_DONTWAIT, nullptr, nullptr);
        }
    }


    void XdpSocket::reclaimCompletedTx()
    {
        uint32_t idx = 0;
        uint32_t completed = xsk_ring_cons__peek(&comp_ring_, COMP_RING_SIZE, &idx);
        if (completed == 0)
        {
            return;
        }

        for (uint32_t i = 0; i < completed; ++i)
        {
            freeFrame(*xsk_ring_cons__comp_addr(&comp_ring_, idx + i));
        }

        xsk_ring_cons__release(&comp_ring_, completed);
        refillFillRing();
    }


    uint64_t XdpSocket::allocFrame()
    {
        if (free_frames_.empty())
        {
            reclaimCompletedTx();
        }

        if (free_frames_.empty())
        {
            return UINT64_MAX;
        }

        uint64_t addr = free_frames_.back();
        free_frames_.pop_back();
        return addr;
    }


    void XdpSocket::freeFrame(uint64_t addr)
    {
        free_frames_.push_back(addr);
    }


    void XdpSocket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }


    void XdpSocket::close() noexcept
    {
        if (xsk_ != nullptr)
        {
            xsk_socket__delete(xsk_);
            xsk_ = nullptr;
        }

        if (bpf_obj_ != nullptr)
        {
            bpf_xdp_detach(ifindex_, XDP_FLAGS_SKB_MODE, nullptr);
            bpf_object__close(bpf_obj_);
            bpf_obj_ = nullptr;
            xdp_prog_fd_ = -1;
        }

        if (umem_ != nullptr)
        {
            xsk_umem__delete(umem_);
            umem_ = nullptr;
        }

        if (umem_area_ != nullptr)
        {
            munmap(umem_area_, static_cast<uint64_t>(NUM_FRAMES) * FRAME_SIZE);
            umem_area_ = nullptr;
        }

        free_frames_.clear();
    }


    int32_t XdpSocket::read(void* frame, int32_t frame_size)
    {
        nanoseconds deadline = since_epoch() + timeout_;

        do
        {
            uint32_t idx = 0;
            uint32_t received = xsk_ring_cons__peek(&rx_ring_, 1, &idx);
            if (received > 0)
            {
                uint64_t addr = xsk_ring_cons__rx_desc(&rx_ring_, idx)->addr;
                uint32_t len  = xsk_ring_cons__rx_desc(&rx_ring_, idx)->len;

                uint32_t copy_len = std::min(static_cast<uint32_t>(frame_size), len);
                std::memcpy(frame, xsk_umem__get_data(umem_area_, addr), copy_len);

                xsk_ring_cons__release(&rx_ring_, 1);
                freeFrame(addr);
                refillFillRing();

                return static_cast<int32_t>(copy_len);
            }

            if (timeout_ < 0ns)
            {
                struct pollfd pfd = {};
                pfd.fd     = xsk_socket__fd(xsk_);
                pfd.events = POLLIN;
                poll(&pfd, 1, -1);
                continue;
            }

            sleep(polling_period_);
        } while (since_epoch() < deadline);

        return -ETIMEDOUT;
    }


    int32_t XdpSocket::write(void const* frame, int32_t frame_size)
    {
        reclaimCompletedTx();

        uint64_t addr = allocFrame();
        if (addr == UINT64_MAX)
        {
            return -ENOMEM;
        }

        std::memcpy(xsk_umem__get_data(umem_area_, addr), frame, frame_size);

        uint32_t idx = 0;
        if (xsk_ring_prod__reserve(&tx_ring_, 1, &idx) != 1)
        {
            freeFrame(addr);
            return -EAGAIN;
        }

        struct xdp_desc* desc = xsk_ring_prod__tx_desc(&tx_ring_, idx);
        desc->addr = addr;
        desc->len  = static_cast<uint32_t>(frame_size);

        xsk_ring_prod__submit(&tx_ring_, 1);

        if (xsk_ring_prod__needs_wakeup(&tx_ring_))
        {
            sendto(xsk_socket__fd(xsk_), nullptr, 0, MSG_DONTWAIT, nullptr, 0);
        }

        return frame_size;
    }
}
