/* PCE SoftFloat build: portable integer primitives, no host FP. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLEENDIAN 1
#elif !defined(__BYTE_ORDER__)
#error "SoftFloat requires a known host byte order"
#endif
#define INLINE inline
#define SOFTFLOAT_BUILTIN_CLZ 1
#define SOFTFLOAT_INTRINSIC_INT128 1
#include "opts-GCC.h"
