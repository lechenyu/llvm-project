#include <stdint.h>

#pragma omp declare target
uintptr_t app_mem_start_   = 0U;
uintptr_t app_shdw_start_  = 0U;
uintptr_t app_shdw_end_    = 0U;
// uintptr_t glob_mem_start_  = 0U;
// uintptr_t glob_mem_end_    = 0U;
// uintptr_t glob_shdw_start_ = 0U;
#pragma omp end declare target
