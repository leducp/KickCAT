// SPDX-License-Identifier: GPL-2.0
// XDP program that redirects EtherCAT frames (ethertype 0x88A4) to AF_XDP sockets.
// All other traffic is passed through the normal kernel networking stack.

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 64);
} xsks_map SEC(".maps");

SEC("xdp")
int xdp_ethercat_filter(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
    {
        return XDP_PASS;
    }

    if (eth->h_proto == bpf_htons(0x88A4))
    {
        return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, XDP_PASS);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
