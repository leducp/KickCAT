#include <p4ext_config.h>
#include <p4ext_tls.h>

#define P4EXT_VM_STACK      0x800000
#define P4EXT_VMEM_START    0x50000000
#define P4EXT_VMEM_SIZE     0x20000000
#define P4EXT_STACK_START   0x70000000
#define P4EXT_STACK_SIZE    0x0FFF0000
#define P4EXT_TRACE_SIZE    0x02000000
#define P4EXT_HEAP_POOL     "_RAM_"
#define P4EXT_HEAP_SIZE     0x0
#define P4EXT_HEAP_EXEC     0
#define P4EXT_TLS_SIZE      sizeof(p4ext_tls_area_t)

P4_DECLARE_STACK(P4EXT_VM_STACK);

p4ext_config_t p4ext_config =
{
    .vmem_start  = (void *)P4EXT_VMEM_START,
    .vmem_size   = P4EXT_VMEM_SIZE,
    .stack_start = (void *)P4EXT_STACK_START,
    .stack_size  = P4EXT_STACK_SIZE,
    .heap_pool   = P4EXT_HEAP_POOL,
    .heap_size   = P4EXT_HEAP_SIZE,
    .heap_exec   = P4EXT_HEAP_EXEC,
    .trace_size  = P4EXT_TRACE_SIZE,
    .tls_size    = P4EXT_TLS_SIZE
};
