#include "tsan_rtl.h"

typedef __m64 m64;

namespace __tsan {

static ALWAYS_INLINE void StoreVsm4(RawVsm *vp, RawVsm val) {
  u32 v = static_cast<u8>(val);
  v = (v << 24) | (v << 16) | (v << 8) | v;
  atomic_store((atomic_uint32_t *)vp, v, memory_order_relaxed);
}

static ALWAYS_INLINE void StoreVsm4(RawVsm *vp, u32 vsm4) {
  atomic_store((atomic_uint32_t *)vp, vsm4, memory_order_relaxed);
}

static ALWAYS_INLINE void StoreVsm8(RawVsm *vp, RawVsm val) {
  m64 s = _mm_set1_pi8(static_cast<u8>(val));
  atomic_store((atomic_uint64_t *)vp, _m_to_int64(s), memory_order_relaxed);
}

static ALWAYS_INLINE void StoreVsm8(RawVsm *vp, m64 vsm8) {
  atomic_store((atomic_uint64_t *)vp, _m_to_int64(vsm8), memory_order_relaxed);
}

static ALWAYS_INLINE u32 LoadVsm4(RawVsm *vp) {
  return atomic_load((atomic_uint32_t *)vp, memory_order_relaxed);
}

static ALWAYS_INLINE u64 LoadVsm8(RawVsm *vp) {
  return atomic_load((atomic_uint64_t *)vp, memory_order_relaxed);
}

static ALWAYS_INLINE void MapTo4(RawVsm *vp) {
  u32 origin = LoadVsm4(vp);
  u32 mask = VariableStateMachine::kDeviceMask4;
  u32 reset_dev_bits = origin & (~mask);
  u32 host_bits = (origin << 1) & mask;
  u32 new_vsm4 = reset_dev_bits | host_bits;
  StoreVsm4(vp, new_vsm4);
}

static ALWAYS_INLINE void MapTo8(RawVsm *vp) {
  m64 origin = _m_from_int64(LoadVsm8(vp));
  m64 mask = _m_from_int64(~VariableStateMachine::kDeviceMask8);
  m64 reset_dev_bits = _mm_and_si64(origin, mask);
  m64 mask2 = _m_from_int64(VariableStateMachine::kDeviceMask8);
  m64 host_bits = _mm_and_si64(_mm_slli_si64(origin, 1), mask2);
  m64 new_vsm8 = _mm_or_si64(reset_dev_bits, host_bits);
  StoreVsm8(vp, new_vsm8);
}

static ALWAYS_INLINE void MapFrom4(RawVsm *vp) {
  u32 origin = LoadVsm4(vp);
  u32 mask = VariableStateMachine::kHostMask4;
  u32 reset_host_bits = origin & (~mask);
  u32 dev_bits = (origin >> 1) & mask;
  u32 new_vsm4 = reset_host_bits | dev_bits;
  StoreVsm4(vp, new_vsm4);
}

static ALWAYS_INLINE void MapFrom8(RawVsm *vp) {
  m64 origin = _m_from_int64(LoadVsm8(vp));
  m64 mask = _m_from_int64(~VariableStateMachine::kHostMask8);
  m64 reset_host_bits = _mm_and_si64(origin, mask);
  m64 mask2 = _m_from_int64(VariableStateMachine::kHostMask8);
  m64 dev_bits = _mm_and_si64(_mm_srli_si64(origin, 1), mask2);
  m64 new_vsm8 = _mm_or_si64(reset_host_bits, dev_bits);
  StoreVsm8(vp, new_vsm8);
}

static ALWAYS_INLINE void VsmCellSet(uptr addr, uptr size, RawVsm val) {
  uptr cell_begin = RoundDown(addr, kVsmCell);
  RawVsm *begin = MemToVsm(addr) + (addr - cell_begin) * kMemToVsmRatio;
  if (size == 4) {
    StoreVsm4(begin, val);
  } else {
    RawVsm *end = begin + size * kMemToVsmRatio;
    for (RawVsm *p = begin; p < end; p++) {
      StoreVsm(p, val);
    }
  }
}

static ALWAYS_INLINE void VsmCellUpdateMapTo(uptr addr, uptr size) {
  uptr cell_begin = RoundDown(addr, kVsmCell);
  RawVsm *begin = MemToVsm(addr) + (addr - cell_begin) * kMemToVsmRatio;
  if (size == 4) {
    MapTo4(begin);
  } else {
    RawVsm *end = begin + size * kMemToVsmRatio;
    u8 mask = static_cast<u8>(VariableStateMachine::kDeviceMask);
    for (RawVsm *p = begin; p < end; p++) {
      u8 origin = static_cast<u8>(LoadVsm(p));
      u8 reset_dev_bits = origin & (~mask);
      u8 host_bits = (origin << 1) & mask;
      u8 new_vsm = reset_dev_bits | host_bits;
      StoreVsm(p, static_cast<RawVsm>(new_vsm));
    }
  }
}

static ALWAYS_INLINE void VsmCellUpdateMapFrom(uptr addr, uptr size) {
  uptr cell_begin = RoundDown(addr, kVsmCell);
  RawVsm *begin = MemToVsm(addr) + (addr - cell_begin) * kMemToVsmRatio;
  if (size == 4) {
    MapFrom4(begin);
  } else {
    RawVsm *end = begin + size * kMemToVsmRatio;
    u8 mask = static_cast<u8>(VariableStateMachine::kHostMask);
    for (RawVsm *p = begin; p < end; p++) {
      u8 origin = static_cast<u8>(LoadVsm(p));
      u8 reset_host_bits = origin & (~mask);
      u8 dev_bits = (origin >> 1) & mask;
      u8 new_vsm = reset_host_bits | dev_bits;
      StoreVsm(p, static_cast<RawVsm>(new_vsm));
    }
  }
}

// the following three functions assume 'p' and 'end' are
// aligned with kVsmCnt

void VsmSet(RawVsm* p, RawVsm* end, RawVsm val) {
  for (; p < end; p += kVsmCnt) {
    StoreVsm8(p, val);
  }
}

void VsmUpdateMapTo(RawVsm* p, RawVsm* end) {
  for (; p < end; p += kVsmCnt) {
    MapTo8(p);
  }
}

void VsmUpdateMapFrom(RawVsm* p, RawVsm* end) {
  for (; p < end; p += kVsmCnt) {
    MapFrom8(p);
  }
}

// addr and size should be aligned with kVsmCell
// The Mmap-based optimizaiton can only set VSMs to zeros
void VsmSetZero(uptr addr, uptr size) {
  RawVsm val = VariableStateMachine::kEmpty;
  RawVsm* begin = MemToVsm(addr);
  RawVsm* end = begin + size / kVsmCell * kVsmCnt;
  // Don't want to touch lots of shadow memory.
  // If a program maps 10MB stack, there is no need reset the whole range.
  // UnmapOrDie/MmapFixedNoReserve does not work on Windows.
  if (SANITIZER_WINDOWS ||
      size <= common_flags()->clear_shadow_mmap_threshold) {
    VsmSet(begin, end, val);
    return;
  }
  // The region is big, reset only beginning and end.
  const uptr kPageSize = GetPageSizeCached();
  // Set at least first kPageSize/2 to page boundary.
  RawVsm* mid1 =
      Min(end, reinterpret_cast<RawVsm*>(RoundUp(
                   reinterpret_cast<uptr>(begin) + kPageSize / 2, kPageSize)));
  VsmSet(begin, mid1, val);
  // Reset middle part.
  RawVsm* mid2 = RoundDown(end, kPageSize);
  if (mid2 > mid1) {
    if (!MmapFixedSuperNoReserve((uptr)mid1, (uptr)mid2 - (uptr)mid1))
      Die();
  }
  // Set the ending.
  VsmSet(mid2, end, val);
}

void VsmRangeSet(uptr addr, uptr size, RawVsm val) {
  if (size == 0)
    return;

  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;

  uptr first_aligned_cell = RoundUp(addr, kVsmCell);
  uptr end_aligned_cell = RoundDown(addr + size, kVsmCell);
  if (first_aligned_cell > addr) {
    VsmCellSet(addr, Min((first_aligned_cell - addr), size), val);
  }
  
  if (first_aligned_cell < end_aligned_cell) {
    if (static_cast<u8>(val)) {
      VsmSet(MemToVsm(first_aligned_cell), MemToVsm(end_aligned_cell), val);
    } else {
      VsmSetZero(first_aligned_cell, end_aligned_cell - first_aligned_cell);
    }
  }

  if (end_aligned_cell >= addr && end_aligned_cell < addr + size) {
    VsmCellSet(end_aligned_cell, addr + size - end_aligned_cell, val);
  }
}

void VsmRangeUpdateMapTo(uptr addr, uptr size) {
  if (size == 0)
    return;

  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;

  uptr first_aligned_cell = RoundUp(addr, kVsmCell);
  uptr end_aligned_cell = RoundDown(addr + size, kVsmCell);
  if (first_aligned_cell > addr) {
    VsmCellUpdateMapTo(addr, Min((first_aligned_cell - addr), size));
  }
  
  if (first_aligned_cell < end_aligned_cell) {
    VsmUpdateMapTo(MemToVsm(first_aligned_cell), MemToVsm(end_aligned_cell));
  }

  if (end_aligned_cell >= addr && end_aligned_cell < addr + size) {
    VsmCellUpdateMapTo(end_aligned_cell, addr + size - end_aligned_cell);
  }
}

void VsmRangeUpdateMapFrom(uptr addr, uptr size) {
  if (size == 0)
    return;

  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;

  uptr first_aligned_cell = RoundUp(addr, kVsmCell);
  uptr end_aligned_cell = RoundDown(addr + size, kVsmCell);
  if (first_aligned_cell > addr) {
    VsmCellUpdateMapFrom(addr, Min((first_aligned_cell - addr), size));
  }
  
  if (first_aligned_cell < end_aligned_cell) {
    VsmUpdateMapFrom(MemToVsm(first_aligned_cell), MemToVsm(end_aligned_cell));
  }

  if (end_aligned_cell >= addr && end_aligned_cell < addr + size) {
    VsmCellUpdateMapFrom(end_aligned_cell, addr + size - end_aligned_cell);
  }
}

ALWAYS_INLINE USED int CheckVsmUtil(uptr addr, uptr size, u64 vmask) {
  int range_mask = kVsmCellBitMap >> (kVsmCell - size);
  uptr cell_start = RoundDown(addr, kVsmCell);
  uptr offset =  addr - cell_start;
  range_mask <<= offset;
  m64 origin = _m_from_int64(LoadVsm8(MemToVsm(cell_start)));
  m64 mask = _m_from_int64(vmask);
  m64 result = _mm_cmpeq_pi8(_mm_and_si64(origin, mask), mask);
  int rewrite_mask = _mm_movemask_pi8(result);
  int error_bytes = range_mask ^ (range_mask & rewrite_mask);
  // xor is non-zero => at least one bit is not the same as the range_mask
  return error_bytes;
}

// [addr, addr + size) should fall into the same VSM
ALWAYS_INLINE USED bool CheckVsm(ThreadState *thr, uptr pc, uptr addr,
                                         uptr size) {
  if (thr->is_on_target) {
    Node *n = ctx->t_to_h.find({addr, addr + size});
    if (!n) {
      return false;
    }
    uptr corr_host_addr = n->info.start + (addr - n->interval.left_end);
    int error_bytes = CheckVsmUtil(corr_host_addr, size, VariableStateMachine::kDeviceMask8);
    if (UNLIKELY(error_bytes)) {
      uptr error_start_addr = (__builtin_ffs(error_bytes) - 1) + corr_host_addr;
      VariableStateMachine v{*MemToVsm(error_start_addr)};
      if (!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead)) {
        TraceSwitchPart(thr);
      }
      ReportDMI(thr, addr, size, kAccessRead, v.IsDeviceInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
      return true;
    } else {
      return false;
    }
  } else {
    if (!ctx->h_to_t.find({addr, addr + size})) {
      return false;
    }
    int error_bytes = CheckVsmUtil(addr, size, VariableStateMachine::kHostMask8);
    if (UNLIKELY(error_bytes)) {
      uptr error_start_addr = (__builtin_ffs(error_bytes) - 1) + addr;
      VariableStateMachine v{*MemToVsm(error_start_addr)};
      if (!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead)) {
        TraceSwitchPart(thr);
      }
      ReportDMI(thr, addr, size, kAccessRead, v.IsHostInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
      return true;
    } else {
      return false;
    }
  }
}

ALWAYS_INLINE USED void UnalignedCheckVsm(ThreadState *thr, uptr pc, uptr addr,
                                          uptr size) {
  uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  if (UNLIKELY(CheckVsm(thr, pc, addr, size1))) {
    return;
  }
  uptr size2 = size - size1;
  if (LIKELY(size2 == 0)) {
    return;
  }
  CheckVsm(thr, pc, addr + size1, size2);                      
}

ALWAYS_INLINE USED void UnalignedCheckVsm16(ThreadState *thr, uptr pc, uptr addr) {
  uptr size = 16;
  uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  uptr a = addr;
  if (UNLIKELY(CheckVsm(thr, pc, a, size1))) {
    return;
  }
  uptr size2 = Min<uptr>(size - size1, kVsmCell);
  a += size1;
  if (UNLIKELY(CheckVsm(thr, pc, a, size2))) {
    return;
  }
  uptr size3 = size - size1 - size2;
  if (LIKELY(size3 == 0)) {
    return;
  }
  a += size2;
  CheckVsm(thr, pc, a, size3);
}

ALWAYS_INLINE USED void CheckVsmForMemoryRange(ThreadState *thr, uptr pc,
                                               uptr addr, uptr size) {
  uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  uptr a = addr;
  size -= size1;
  if (UNLIKELY(CheckVsm(thr, pc, a, size1))) {
    return;
  }
  a += size1;
  for (; size >= kVsmCell; size -= kVsmCell, a += kVsmCell) {
    if (UNLIKELY(CheckVsm(thr, pc, a, kVsmCell))) {
      return;
    }
  }
  if (UNLIKELY(size)) {
    CheckVsm(thr, pc, a, size);
  }
}

ALWAYS_INLINE USED void UpdateVsmUtil(uptr addr, uptr size, u64 set_mask) {
  u64 range_mask = kVsmCellValueBitMap >> ((kVsmCell - size) * kMemToVsmRatioInBit);
  uptr cell_start = RoundDown(addr, kVsmCell);
  uptr offset =  addr - cell_start;
  range_mask <<= (offset * kMemToVsmRatioInBit);
  RawVsm *vp = MemToVsm(cell_start);
  u64 vsm_val = LoadVsm8(vp);
  vsm_val &= (~range_mask);
  vsm_val |= (range_mask & set_mask);
  StoreVsm8(vp, static_cast<RawVsm>(vsm_val));
}

ALWAYS_INLINE USED void UpdateVsm(ThreadState *thr, uptr addr, uptr size) {
  if (thr->is_on_target) {
    Node *n = ctx->t_to_h.find({addr, addr + size});
    if (!n) {
      return;
    }
    uptr corr_host_addr = n->info.start + (addr - n->interval.left_end);
    UpdateVsmUtil(corr_host_addr, size, VariableStateMachine::kDeviceMask8);
  } else {
    UpdateVsmUtil(addr, size, VariableStateMachine::kHostMask8);
  }                                   
}

ALWAYS_INLINE USED void UnalignedUpdateVsm(ThreadState *thr, uptr addr, uptr size) {
  uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  UpdateVsm(thr, addr, size1);
  uptr size2 = size - size1;
  if (size2) {
    UpdateVsm(thr, addr + size1, size2);
  }    
}

ALWAYS_INLINE USED void UnalignedUpdateVsm16(ThreadState *thr, uptr addr) {
  uptr size = 16;
  uptr a = addr;
  uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  UpdateVsm(thr, a, size1);
  uptr size2 = Min<uptr>(size - size1, kVsmCell);
  a += size1;
  UpdateVsm(thr, a, size2);
  uptr size3 = size - size1 - size2;
  if (LIKELY(size3 == 0)) {
    return;
  }
  a += size2;
  UpdateVsm(thr, a, size3);
}

ALWAYS_INLINE USED void UpdateVsmForMemoryRange(ThreadState *thr, uptr addr,
                                                uptr size) {
                                                    uptr first_cell_end = RoundUp(addr + 1, kVsmCell);
  uptr size1 = Min<uptr>(size, first_cell_end - addr);
  uptr a = addr;
  size -= size1;
  UpdateVsm(thr, a, size1);
  a += size1;
  for (; size >= kVsmCell; size -= kVsmCell, a += kVsmCell) {
    UpdateVsm(thr, a, kVsmCell);
  }
  if (UNLIKELY(size)) {
    UpdateVsm(thr, a, size);
  }

}

} // namespace __tsan

#if !SANITIZER_GO
// Must be included in this file to make sure everything is inlined.
#  include "tsan_arbalest_interface.inc"
#endif