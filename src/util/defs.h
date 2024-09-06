#pragma once

#include <tomatodotnet/host.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PACKED __attribute__((packed))

#define STATIC_ASSERT(x) _Static_assert(x, #x)

#define CACHE_PADDED __attribute__((aligned(64)))

#define ALIGN_UP(x, align) ({ \
__typeof(x) _x = x; \
__typeof(align) _align = align; \
__typeof(_x) _result = (__typeof(_x))((_x + _align - 1) & ~(_align - 1)); \
_result; \
})

#define ALIGN_DOWN(x, align) ({ \
__typeof(x) _x = x; \
__typeof(align) _align = align; \
__typeof(_x) _result = (__typeof(_x))(((uintptr_t)_x) & ~(_align - 1)); \
_result; \
})

#define ARRAY_LENGTH(array) (sizeof((array))/sizeof((array)[0]))

#define SIGNATURE_16(A, B)  ((A) | (B << 8))
#define SIGNATURE_32(A, B, C, D)  (SIGNATURE_16 (A, B) | (SIGNATURE_16 (C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
    (SIGNATURE_32 (A, B, C, D) | ((UINT64) (SIGNATURE_32 (E, F, G, H)) << 32))

#define MIN(a, b) \
    ({ \
        typeof(a) __a = a; \
        typeof(b) __b = b; \
        __a > __b ? __b : __a; \
    })

#define MAX(a, b) \
    ({ \
        typeof(a) __a = a; \
        typeof(b) __b = b; \
        __a < __b ? __b : __a; \
    })

#define UNUSED(x) ((void)x)

#define  BIT0   0x00000001ULL
#define  BIT1   0x00000002ULL
#define  BIT2   0x00000004ULL
#define  BIT3   0x00000008ULL
#define  BIT4   0x00000010ULL
#define  BIT5   0x00000020ULL
#define  BIT6   0x00000040ULL
#define  BIT7   0x00000080ULL
#define  BIT8   0x00000100ULL
#define  BIT9   0x00000200ULL
#define  BIT10  0x00000400ULL
#define  BIT11  0x00000800ULL
#define  BIT12  0x00001000ULL
#define  BIT13  0x00002000ULL
#define  BIT14  0x00004000ULL
#define  BIT15  0x00008000ULL
#define  BIT16  0x00010000ULL
#define  BIT17  0x00020000ULL
#define  BIT18  0x00040000ULL
#define  BIT19  0x00080000ULL
#define  BIT20  0x00100000ULL
#define  BIT21  0x00200000ULL
#define  BIT22  0x00400000ULL
#define  BIT23  0x00800000ULL
#define  BIT24  0x01000000ULL
#define  BIT25  0x02000000ULL
#define  BIT26  0x04000000ULL
#define  BIT27  0x08000000ULL
#define  BIT28  0x10000000ULL
#define  BIT29  0x20000000ULL
#define  BIT30  0x40000000ULL
#define  BIT31  0x80000000ULL
#define  BIT32  0x0000000100000000ULL
#define  BIT33  0x0000000200000000ULL
#define  BIT34  0x0000000400000000ULL
#define  BIT35  0x0000000800000000ULL
#define  BIT36  0x0000001000000000ULL
#define  BIT37  0x0000002000000000ULL
#define  BIT38  0x0000004000000000ULL
#define  BIT39  0x0000008000000000ULL
#define  BIT40  0x0000010000000000ULL
#define  BIT41  0x0000020000000000ULL
#define  BIT42  0x0000040000000000ULL
#define  BIT43  0x0000080000000000ULL
#define  BIT44  0x0000100000000000ULL
#define  BIT45  0x0000200000000000ULL
#define  BIT46  0x0000400000000000ULL
#define  BIT47  0x0000800000000000ULL
#define  BIT48  0x0001000000000000ULL
#define  BIT49  0x0002000000000000ULL
#define  BIT50  0x0004000000000000ULL
#define  BIT51  0x0008000000000000ULL
#define  BIT52  0x0010000000000000ULL
#define  BIT53  0x0020000000000000ULL
#define  BIT54  0x0040000000000000ULL
#define  BIT55  0x0080000000000000ULL
#define  BIT56  0x0100000000000000ULL
#define  BIT57  0x0200000000000000ULL
#define  BIT58  0x0400000000000000ULL
#define  BIT59  0x0800000000000000ULL
#define  BIT60  0x1000000000000000ULL
#define  BIT61  0x2000000000000000ULL
#define  BIT62  0x4000000000000000ULL
#define  BIT63  0x8000000000000000ULL

#define  SIZE_1KB    0x00000400ULL
#define  SIZE_2KB    0x00000800ULL
#define  SIZE_4KB    0x00001000ULL
#define  SIZE_8KB    0x00002000ULL
#define  SIZE_16KB   0x00004000ULL
#define  SIZE_32KB   0x00008000ULL
#define  SIZE_64KB   0x00010000ULL
#define  SIZE_128KB  0x00020000ULL
#define  SIZE_256KB  0x00040000ULL
#define  SIZE_512KB  0x00080000ULL
#define  SIZE_1MB    0x00100000ULL
#define  SIZE_2MB    0x00200000ULL
#define  SIZE_4MB    0x00400000ULL
#define  SIZE_8MB    0x00800000ULL
#define  SIZE_16MB   0x01000000ULL
#define  SIZE_32MB   0x02000000ULL
#define  SIZE_64MB   0x04000000ULL
#define  SIZE_128MB  0x08000000ULL
#define  SIZE_256MB  0x10000000ULL
#define  SIZE_512MB  0x20000000ULL
#define  SIZE_1GB    0x40000000ULL
#define  SIZE_2GB    0x80000000ULL
#define  SIZE_4GB    0x0000000100000000ULL
#define  SIZE_8GB    0x0000000200000000ULL
#define  SIZE_16GB   0x0000000400000000ULL
#define  SIZE_32GB   0x0000000800000000ULL
#define  SIZE_64GB   0x0000001000000000ULL
#define  SIZE_128GB  0x0000002000000000ULL
#define  SIZE_256GB  0x0000004000000000ULL
#define  SIZE_512GB  0x0000008000000000ULL
#define  SIZE_1TB    0x0000010000000000ULL
#define  SIZE_2TB    0x0000020000000000ULL
#define  SIZE_4TB    0x0000040000000000ULL
#define  SIZE_8TB    0x0000080000000000ULL
#define  SIZE_16TB   0x0000100000000000ULL
#define  SIZE_32TB   0x0000200000000000ULL
#define  SIZE_64TB   0x0000400000000000ULL
#define  SIZE_128TB  0x0000800000000000ULL
#define  SIZE_256TB  0x0001000000000000ULL
#define  SIZE_512TB  0x0002000000000000ULL
#define  SIZE_1PB    0x0004000000000000ULL
#define  SIZE_2PB    0x0008000000000000ULL
#define  SIZE_4PB    0x0010000000000000ULL
#define  SIZE_8PB    0x0020000000000000ULL
#define  SIZE_16PB   0x0040000000000000ULL
#define  SIZE_32PB   0x0080000000000000ULL
#define  SIZE_64PB   0x0100000000000000ULL
#define  SIZE_128PB  0x0200000000000000ULL
#define  SIZE_256PB  0x0400000000000000ULL
#define  SIZE_512PB  0x0800000000000000ULL
#define  SIZE_1EB    0x1000000000000000ULL
#define  SIZE_2EB    0x2000000000000000ULL
#define  SIZE_4EB    0x4000000000000000ULL
#define  SIZE_8EB    0x8000000000000000ULL

#define  BASE_1KB    0x00000400ULL
#define  BASE_2KB    0x00000800ULL
#define  BASE_4KB    0x00001000ULL
#define  BASE_8KB    0x00002000ULL
#define  BASE_16KB   0x00004000ULL
#define  BASE_32KB   0x00008000ULL
#define  BASE_64KB   0x00010000ULL
#define  BASE_128KB  0x00020000ULL
#define  BASE_256KB  0x00040000ULL
#define  BASE_512KB  0x00080000ULL
#define  BASE_1MB    0x00100000ULL
#define  BASE_2MB    0x00200000ULL
#define  BASE_4MB    0x00400000ULL
#define  BASE_8MB    0x00800000ULL
#define  BASE_16MB   0x01000000ULL
#define  BASE_32MB   0x02000000ULL
#define  BASE_64MB   0x04000000ULL
#define  BASE_128MB  0x08000000ULL
#define  BASE_256MB  0x10000000ULL
#define  BASE_512MB  0x20000000ULL
#define  BASE_1GB    0x40000000ULL
#define  BASE_2GB    0x80000000ULL
#define  BASE_4GB    0x0000000100000000ULL
#define  BASE_8GB    0x0000000200000000ULL
#define  BASE_16GB   0x0000000400000000ULL
#define  BASE_32GB   0x0000000800000000ULL
#define  BASE_64GB   0x0000001000000000ULL
#define  BASE_128GB  0x0000002000000000ULL
#define  BASE_256GB  0x0000004000000000ULL
#define  BASE_512GB  0x0000008000000000ULL
#define  BASE_1TB    0x0000010000000000ULL
#define  BASE_2TB    0x0000020000000000ULL
#define  BASE_4TB    0x0000040000000000ULL
#define  BASE_8TB    0x0000080000000000ULL
#define  BASE_16TB   0x0000100000000000ULL
#define  BASE_32TB   0x0000200000000000ULL
#define  BASE_64TB   0x0000400000000000ULL
#define  BASE_128TB  0x0000800000000000ULL
#define  BASE_256TB  0x0001000000000000ULL
#define  BASE_512TB  0x0002000000000000ULL
#define  BASE_1PB    0x0004000000000000ULL
#define  BASE_2PB    0x0008000000000000ULL
#define  BASE_4PB    0x0010000000000000ULL
#define  BASE_8PB    0x0020000000000000ULL
#define  BASE_16PB   0x0040000000000000ULL
#define  BASE_32PB   0x0080000000000000ULL
#define  BASE_64PB   0x0100000000000000ULL
#define  BASE_128PB  0x0200000000000000ULL
#define  BASE_256PB  0x0400000000000000ULL
#define  BASE_512PB  0x0800000000000000ULL
#define  BASE_1EB    0x1000000000000000ULL
#define  BASE_2EB    0x2000000000000000ULL
#define  BASE_4EB    0x4000000000000000ULL
#define  BASE_8EB    0x8000000000000000ULL
