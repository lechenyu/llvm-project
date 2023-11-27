#include "tsan_interface.h"
#include "tsan_interface.h"

#define CALLERPC ((uptr)__builtin_return_address(0))

using namespace __tsan;

void __arbalest_read1(void *addr) {
  CheckVsm(cur_thread(), CALLERPC, (uptr)addr, 1);
}

void __arbalest_read2(void *addr) {
  CheckVsm(cur_thread(), CALLERPC, (uptr)addr, 2);
}

void __arbalest_read4(void *addr) {
  CheckVsm(cur_thread(), CALLERPC, (uptr)addr, 4);
}

void __arbalest_read8(void *addr) {
  CheckVsm(cur_thread(), CALLERPC, (uptr)addr, 8);
}

void __arbalest_read16(void *addr) {
  if (UNLIKELY(CheckVsm(cur_thread(), CALLERPC, (uptr)addr, 8))) {
    return;
  }
  CheckVsm(cur_thread(), CALLERPC, (uptr)addr + 8, 8);
}

void __arbalest_write1(void *addr) {
  UpdateVsm(cur_thread(), (uptr)addr, 1);
}

void __arbalest_write2(void *addr) {
  UpdateVsm(cur_thread(), (uptr)addr, 2);
}

void __arbalest_write4(void *addr){
  UpdateVsm(cur_thread(), (uptr)addr, 4);
}

void __arbalest_write8(void *addr) {
  UpdateVsm(cur_thread(), (uptr)addr, 8);
}

void __arbalest_write16(void *addr) {
  UpdateVsm(cur_thread(), (uptr)addr, 8);
  UpdateVsm(cur_thread(), (uptr)addr + 8, 8);
}

void __arbalest_unaligned_read2(const void *addr) {
  UnalignedCheckVsm(cur_thread(), CALLERPC, (uptr)addr, 2);
}

void __arbalest_unaligned_read4(const void *addr) {
  UnalignedCheckVsm(cur_thread(), CALLERPC, (uptr)addr, 4);
}

void __arbalest_unaligned_read8(const void *addr) {
  UnalignedCheckVsm(cur_thread(), CALLERPC, (uptr)addr, 8);
}

void __arbalest_unaligned_read16(const void *addr) {
  UnalignedCheckVsm16(cur_thread(), CALLERPC, (uptr)addr);
}

void __arbalest_unaligned_write2(void *addr) {
  UnalignedUpdateVsm(cur_thread(), (uptr)addr, 2);
}

void __arbalest_unaligned_write4(void *addr) {
  UnalignedUpdateVsm(cur_thread(), (uptr)addr, 4);
}

void __arbalest_unaligned_write8(void *addr) {
  UnalignedUpdateVsm(cur_thread(), (uptr)addr, 8);
}

void __arbalest_unaligned_write16(void *addr) {
  UnalignedUpdateVsm16(cur_thread(), (uptr)addr);
}