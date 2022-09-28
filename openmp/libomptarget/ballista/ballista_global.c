#include <stdint.h>

#pragma omp declare target
uintptr_t app_start_ = 0U;
uintptr_t shadow_start_ = 0U;
#pragma omp end declare target
