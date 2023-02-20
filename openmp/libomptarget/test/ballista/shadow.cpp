// RUN: %clangxx %s  %S/../../src/ballista.cpp -I%S/../../include -o %t && %libomptarget-run-generic
// REQUIRES: nvptx64-nvidia-cuda

#include <cstdio>
#include <cassert>
#include <cstdint>
#include "ballista.h"

void test1() {
  AppStartVal =  0x7ffffffff000;
  ShdwStartVal = 0x2bfffffff000;
  void *InputAddr = reinterpret_cast<void *>(0x7ffffffffff8);
  void *ExpectResult = reinterpret_cast<void *>(0x2bfffffff7fc);
  assert(getShadowArrayAddr(InputAddr) == ExpectResult);
}

void test2() {
  AppStartVal =  0x7ffffffff001;
  ShdwStartVal = 0x2bfffffff001;
  void *InputAddr = reinterpret_cast<void *>(0x7ffffffffff9);
  void *ExpectResult = reinterpret_cast<void *>(0x2bfffffff7fd);
  assert(getShadowArrayAddr(InputAddr) == ExpectResult);
}

void test3() {
  AppStartVal =  0x7ffffffff001;
  ShdwStartVal = 0x2bfffffff001;
  void *InputAddr = reinterpret_cast<void *>(0x7ffffffffff3);
  void *ExpectResult = reinterpret_cast<void *>(0x2bfffffff7f9);
  assert(getShadowArrayAddr(InputAddr) == ExpectResult);
}

void test4() {
  assert(getShadowArrayNum(1) == 1);
  assert(getShadowArrayNum(2) == 1);
  assert(getShadowArrayNum(3) == 1);
  assert(getShadowArrayNum(4) == 1);
  assert(getShadowArrayNum(5) == 2);
  assert(getShadowArrayNum(6) == 2);
  assert(getShadowArrayNum(7) == 2);
  assert(getShadowArrayNum(8) == 2);
}

void test5() {
  int64_t Size[] = {1, 2, 4, 8, 17};
  bool NullPtr[] = {false, true, false, true, false};
  int Num = sizeof(Size) / sizeof(int64_t);
  void **Ptr1 = new void *[Num]{};
  void **Ptr2 = new void *[Num]{};
  for (int i = 0; i < Num; i++) {
    Ptr1[i] = Ptr2[i] = &Size[i];
    if (NullPtr[i]) {
      Ptr2[i] = nullptr;
    }
  }
  BallistaTargetInfo Glob{Ptr1, Ptr1, Size, Num}, Mapped{Ptr2, Ptr2, Size, Num};
  assert(getBufferSize(Mapped, Glob) == 17);
}

int main() {
  test1();
  test2();
  test3();
  test4();
  test5();
  printf("Pass all tests\n");
  return 0;  
}