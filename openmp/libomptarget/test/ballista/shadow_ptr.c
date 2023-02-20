// RUN: %ballista-compile-and-run-nvptx64-nvidia-cuda 2>&1 | tee %%t.out | FileCheck %s
// REQUIRES: nvptx64-nvidia-cuda

#include <stdio.h>
#include <stdint.h>
#define N 10000

extern uintptr_t app_mem_start_;
extern uintptr_t app_shdw_start_;
extern uintptr_t app_shdw_end_;

int main() {
  int a[N];
  #pragma omp target teams distribute parallel for
  for (int i = 0; i < N; i++) {
    a[i] = i;
  }
  
  #pragma omp target update from(app_mem_start_, app_shdw_start_, app_shdw_end_)
  printf("app_mem_start_ = %p\n", (char *)app_mem_start_);
  printf("app_shdw_start_ = %p\n", (char *)app_shdw_start_);
  printf("app_shdw_end_ = %p\n", (char *)app_shdw_end_);
  int result = 0;
  for (int i = 0; i < N; i++) {
    result += a[i];
  }
  printf("%d\n", result);
  return 0;
}

// CHECK: {{.*}} application memory = [0x[[APP_START:[0-f]+]], 0x{{[0-f]+}}]
// CHECK: {{.*}} shadow memory = [0x[[SHADOW_START:[0-f]+]], 0x[[SHADOW_END:[0-f]+]]]
// CHECK: app_mem_start_ = 0x[[APP_START]]
// CHECK: app_shdw_start_ = 0x[[SHADOW_START]]
// CHECK: app_shdw_end_ =  0x[[SHADOW_END]]