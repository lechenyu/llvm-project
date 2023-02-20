// RUN: %clangxx -v %s -L%T/../../../../../../../../lib -lomptarget.rtl.cuda -Wl,-rpath, %T/../../../../../../../../lib -o %t && %libomptarget-run-generic
// REQUIRES: nvptx64-nvidia-cuda

#include <cstdio>
#include <cassert>
#include <cstdint>

enum TargetAllocTy : int32_t {
  TARGET_ALLOC_DEVICE = 0,
  TARGET_ALLOC_HOST,
  TARGET_ALLOC_SHARED,
  TARGET_ALLOC_DEFAULT
};

struct __tgt_async_info {
  // A pointer to a queue-like structure where offloading operations are issued.
  // We assume to use this structure to do synchronization. In CUDA backend, it
  // is CUstream.
  void *Queue = nullptr;
};

#define OFFLOAD_SUCCESS (0)

#define OFFLOAD_FAIL (~0)

#define OFFLOAD_DEVICE_DEFAULT -1

struct __tgt_async_info;

extern "C" {
int32_t __tgt_rtl_init_device(int32_t ID);

int32_t __tgt_rtl_deinit_device(int32_t ID);

void *__tgt_rtl_data_alloc(int32_t ID, int64_t Size, void *HostPtr,
                           int32_t Kind);

int32_t __tgt_rtl_data_delete(int32_t ID, void *TargetPtr);

int32_t __tgt_rtl_data_submit(int32_t ID, void *TargetPtr, void *HostPtr,
                              int64_t Size);

int32_t __tgt_rtl_data_submit_async(int32_t ID, void *TargetPtr, void *HostPtr,
                                    int64_t Size, __tgt_async_info *AsyncInfo);

int32_t __tgt_rtl_data_retrieve(int32_t ID, void *HostPtr, void *TargetPtr,
                                int64_t Size);

int32_t __tgt_rtl_data_retrieve_async(int32_t ID, void *HostPtr,
                                      void *TargetPtr, int64_t Size,
                                      __tgt_async_info *AsyncInfo);                                      

int32_t __tgt_rtl_data_set(int32_t DeviceId, void *TgtPtr, uint32_t Val,
                           int64_t Size);

int32_t __tgt_rtl_data_set_async(int32_t DeviceId, void *TgtPtr, uint32_t Val,
                                 int64_t Size, __tgt_async_info *AsyncInfo);

int32_t __tgt_rtl_synchronize(int32_t ID, __tgt_async_info *AsyncInfo);
}

const int DEVICE_ID = 0;

void test_sync_data_set(uint32_t Val, int64_t Size) {
  char *Host = new char[Size]{};
  void *Addr = __tgt_rtl_data_alloc(DEVICE_ID, Size, Host, TARGET_ALLOC_DEVICE);
  assert(Addr && "Fail to alloc device memory");
  assert(__tgt_rtl_data_set(DEVICE_ID, Addr, Val, Size) == OFFLOAD_SUCCESS);
  assert(__tgt_rtl_data_retrieve(DEVICE_ID, Host, Addr, Size) == OFFLOAD_SUCCESS);
  for (int i = 0; i < Size; i++) {
    assert(Host[i] == static_cast<char>(Val));
  }
  assert(__tgt_rtl_data_delete(DEVICE_ID, Addr) == OFFLOAD_SUCCESS);
  delete[] Host;
}

void test_async_data_set(uint32_t Val, int64_t Size) {
  char *Host = new char[Size]{};
  __tgt_async_info AsyncInfo;
  void *Addr = __tgt_rtl_data_alloc(DEVICE_ID, Size, Host, TARGET_ALLOC_DEVICE);
  assert(Addr && "Fail to alloc device memory");
  assert(__tgt_rtl_data_set_async(DEVICE_ID, Addr, Val, Size, &AsyncInfo) == OFFLOAD_SUCCESS);
  assert(__tgt_rtl_data_retrieve_async(DEVICE_ID, Host, Addr, Size, &AsyncInfo) == OFFLOAD_SUCCESS);
  assert(__tgt_rtl_synchronize(DEVICE_ID, &AsyncInfo) == OFFLOAD_SUCCESS);
  for (int i = 0; i < Size; i++) {
    assert(Host[i] == static_cast<char>(Val));
  }
  assert(__tgt_rtl_data_delete(DEVICE_ID, Addr) == OFFLOAD_SUCCESS);
  delete[] Host;
}

int main() {
  int Ret = __tgt_rtl_init_device(DEVICE_ID);
  if (Ret != OFFLOAD_SUCCESS) {
    printf("Fail to init device\n");
    return 1;
  }
  uint32_t Vals[] = {0, 1, 2, 3};
  int64_t Sizes[] = {10000, 10001, 10002, 10003};
  for (uint32_t v: Vals) {
    for (int64_t s: Sizes) {
      test_sync_data_set(v, s);
      test_async_data_set(v, s);
      printf("Testing data_set with val %d, size %lu bytes: Pass\n", v, s);
    }
  }
  __tgt_rtl_deinit_device(DEVICE_ID);
  return 0;
}