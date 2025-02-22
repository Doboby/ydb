// The code in this file is based on original ClickHouse source code
// which is licensed under Apache license v2.0
// See: https://github.com/ClickHouse/ClickHouse/

#include <Common/Allocator.h>

namespace CH
{

/** Keep definition of this constant in cpp file; otherwise its value
  * is inlined into allocator code making it impossible to override it
  * in third-party code.
  *
  * Note: extern may seem redundant, but is actually needed due to bug in GCC.
  * See also: https://gcc.gnu.org/legacy-ml/gcc-help/2017-12/msg00021.html
  */
#ifdef NDEBUG
    __attribute__((__weak__)) extern const size_t MMAP_THRESHOLD = 64 * (1ULL << 20);
#else
    /**
      * In debug build, use small mmap threshold to reproduce more memory
      * stomping bugs. Along with ASLR it will hopefully detect more issues than
      * ASan. The program may fail due to the limit on number of memory mappings.
      *
      * Not too small to avoid too quick exhaust of memory mappings.
      */
    __attribute__((__weak__)) extern const size_t MMAP_THRESHOLD = 16384;
#endif

template class Allocator<false, false>;
template class Allocator<true, false>;
template class Allocator<false, true>;
template class Allocator<true, true>;

}
