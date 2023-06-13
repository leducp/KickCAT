#include <cstring>
#include <cerrno>

extern "C"
{
    #include <p4ext_vmem.h>
}

#include "OS/PikeOS/Socket.h"
#include "Time.h"

namespace kickcat
{
    Socket::Socket(nanoseconds polling_period)
        : AbstractSocket()
        , fd_{}
        , sbuf_{}
        , polling_period_(polling_period)
    {

    }

    void Socket::open(std::string const& interface)
    {
        // Check partition rights
        vm_partition_stat_t pinfo;
        P4_e_t rc = vm_part_pstat(VM_RESPART_MYSELF, &pinfo);
        if (rc != P4_E_OK)
        {
            THROW_PIKEOS_ERROR(rc, "vm_part_pstat()");
        }

        if ((pinfo.abilities & VM_AB_ULOCK_SHARED) == 0)
        {
            THROW_ERROR("SBUF mode only works with VM_AB_ULOCK_SHARED.");
        }

        // Open Ethernet interface
        rc = vm_open(interface.c_str(), VM_O_RD_WR | VM_O_MAP, &fd_);
        if (rc != P4_E_OK)
        {
            THROW_PIKEOS_ERROR(rc, "vm_open()");
        }

        // Switch driver to sbuf mode (efficent com mode)
        P4_size_t vsize;
        rc = vm_io_sbuf_init(&fd_, &vsize);
        if (rc != P4_E_OK)
        {
            THROW_PIKEOS_ERROR(rc, "vm_io_sbuf_init()");
        }

        // Allocate a virtual address space region to map Ethernet rings
        P4_address_t vaddr = p4ext_vmem_alloc(vsize);
        if (vaddr == 0)
        {
            THROW_ERROR("p4ext_vmem_alloc() failure.\n");
        }

        // Map Ethernet rings
        rc = vm_io_sbuf_map(&fd_, vaddr, &sbuf_);
        if (rc != P4_E_OK)
        {
            THROW_PIKEOS_ERROR(rc, "vm_io_sbuf_map()");
        }
    }

    void Socket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;

        flags_ = 1; // non block
        if (timeout_ < 0ns)
        {
            flags_ = 0;
        }
    }

    void Socket::close() noexcept
    {
        vm_close(&fd_);
    }

    int32_t Socket::read(uint8_t* frame, int32_t frame_size)
    {
        nanoseconds deadline = since_epoch() + timeout_;
        do
        {
            vm_io_buf_id_t rx = vm_io_sbuf_rx_get(&sbuf_, flags_); // 1 = non block
            if (rx == VM_IO_BUF_ID_INVALID)
            {
                sleep(polling_period_);
                continue;
            }

            void* rxbuf = (void*)vm_io_sbuf_rx_buf_addr(&sbuf_, rx);
            if (rxbuf == nullptr)
            {
                errno = ENOMEM;
                return -1;
            }

            int32_t len = static_cast<int32_t>(vm_io_sbuf_rx_buf_size(&sbuf_, rx));
            int32_t to_copy = std::min(frame_size, len);
            std::memcpy(frame, rxbuf, to_copy);
            vm_io_sbuf_rx_free(&sbuf_, rx);
            return to_copy;
        } while (since_epoch() < deadline);

        errno = EIO; // ETIMEDOUT or ETIME code unavailable on PikeOS
        return -1;
    }

    int32_t Socket::write(uint8_t const* frame, int32_t frame_size)
    {
        vm_io_buf_id_t tx = vm_io_sbuf_tx_alloc(&sbuf_, 1); // 1 = non block
        if (tx == VM_IO_BUF_ID_INVALID)
        {
            errno = EAGAIN;
            return -1;
        }

        void* txbuf = (void*)vm_io_sbuf_tx_buf_addr(&sbuf_, tx);
        if (txbuf == nullptr)
        {
            errno = ENOMEM;
            return -1;
        }

        // Write data and finalize operation
        std::memcpy(txbuf, frame, frame_size);
        vm_io_sbuf_tx_ready(&sbuf_, tx, frame_size);
        return frame_size;
    }
}
