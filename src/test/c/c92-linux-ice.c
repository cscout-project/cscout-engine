// No macro should be allowed to be const int, because they're
// expanded in an integer constant expression context.

// Required supporting macros
#define BIT(nr)         (1UL << (nr))

#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)

#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)

#define __KERNEL_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define BITS_TO_LONGS(nr)       __KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(long))

#define BITS_PER_BYTE           8
#define BITS_PER_TYPE(type)     (sizeof(type) * BITS_PER_BYTE)

// From arch/x86/include/asm/vmx.h
#define VMX_EPT_READABLE_MASK                   0x1ull
#define VMX_EPT_WRITABLE_MASK                   0x2ull
#define VMX_EPT_EXECUTABLE_MASK                 0x4ull

#define EPT_VIOLATION_PROT_READ         BIT(3)
#define EPT_VIOLATION_PROT_WRITE        BIT(4)
#define EPT_VIOLATION_PROT_EXEC         BIT(5)

#define VMX_EPT_RWX_MASK (VMX_EPT_READABLE_MASK | \
  VMX_EPT_WRITABLE_MASK | \
  VMX_EPT_EXECUTABLE_MASK)

#define EPT_VIOLATION_RWX_TO_PROT(__epte) (((__epte) & VMX_EPT_RWX_MASK) << 3)

static_assert(EPT_VIOLATION_RWX_TO_PROT(VMX_EPT_RWX_MASK) ==
              (EPT_VIOLATION_PROT_READ | EPT_VIOLATION_PROT_WRITE | EPT_VIOLATION_PROT_EXEC));

// include/linux/bitops.h
#define DECLARE_BITMAP(name,bits) \
        unsigned long name[BITS_TO_LONGS(bits)]

#define NBITS 135
DECLARE_BITMAP(array, NBITS);

// mm/vmalloc.c
#define PAGE_SIZE 6666
#define VMAP_MAX_ALLOC          BITS_PER_LONG   /* 256K with 4K pages */
#define VMAP_BBMAP_BITS_MAX 1024  /* 4MB with 4K pages */
#define VMAP_BBMAP_BITS_MIN     (VMAP_MAX_ALLOC*2)
#define VMAP_MIN(x, y)          ((x) < (y) ? (x) : (y)) /* can't use min() */
#define VMAP_MAX(x, y)          ((x) > (y) ? (x) : (y)) /* can't use max() */

#define VMAP_BBMAP_BITS         \
                VMAP_MIN(VMAP_BBMAP_BITS_MAX,   \
                VMAP_MAX(VMAP_BBMAP_BITS_MIN,   \
                        VMALLOC_PAGES ))

#define BITS_PER_LONG 64
#define VMALLOC_PAGES              (VMALLOC_SPACE / PAGE_SIZE)
#define VMALLOC_SPACE          (128UL*1024*1024*1024)

struct vmap_block {
	unsigned long free, dirty;
	DECLARE_BITMAP(used_map, VMAP_BBMAP_BITS);
	unsigned long dirty_min, dirty_max; /*< dirty range */
};
