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

static ALWAYS_INLINE void DeviceReset4(RawVsm *vp) {
  u32 origin = LoadVsm4(vp);
  u32 mask = VariableStateMachine::kDeviceMask4;
  u32 reset_dev_bits = origin & (~mask);
  StoreVsm4(vp, reset_dev_bits);
}

static ALWAYS_INLINE void DeviceReset8(RawVsm *vp) {
  m64 origin = _m_from_int64(LoadVsm8(vp));
  m64 mask = _m_from_int64(~VariableStateMachine::kDeviceMask8);
  m64 reset_dev_bits = _mm_and_si64(origin, mask);
  StoreVsm8(vp, reset_dev_bits);
}

static ALWAYS_INLINE void VsmCellSet(uptr addr, uptr size, RawVsm val) {
  uptr cell_begin = RoundDown(addr, kVsmCell);
  RawVsm *begin = MemToVsm(cell_begin) + (addr - cell_begin) * kMemToVsmRatio;
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
  RawVsm *begin = MemToVsm(cell_begin) + (addr - cell_begin) * kMemToVsmRatio;
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
  RawVsm *begin = MemToVsm(cell_begin) + (addr - cell_begin) * kMemToVsmRatio;
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

static ALWAYS_INLINE void VsmCellDeviceReset(uptr addr, uptr size) {
  uptr cell_begin = RoundDown(addr, kVsmCell);
  RawVsm *begin = MemToVsm(cell_begin) + (addr - cell_begin) * kMemToVsmRatio;
  if (size == 4) {
    DeviceReset4(begin);
  } else {
    RawVsm *end = begin + size * kMemToVsmRatio;
    u8 mask = static_cast<u8>(VariableStateMachine::kDeviceMask);
    for (RawVsm *p = begin; p < end; p++) {
      u8 origin = static_cast<u8>(LoadVsm(p));
      u8 reset_dev_bits = origin & (~mask);
      StoreVsm(p, static_cast<RawVsm>(reset_dev_bits));
    }
  }
}

// the following three functions assume 'p' and 'end' are
// aligned with kVsmCnt

void VsmSet(RawVsm *p, RawVsm *end, RawVsm val) {
  for (; p < end; p += kVsmCnt) {
    StoreVsm8(p, val);
  }
}

void VsmUpdateMapTo(RawVsm *p, RawVsm *end) {
  for (; p < end; p += kVsmCnt) {
    MapTo8(p);
  }
}

void VsmUpdateMapFrom(RawVsm *p, RawVsm *end) {
  for (; p < end; p += kVsmCnt) {
    MapFrom8(p);
  }
}

void VsmDeviceReset(RawVsm *p, RawVsm *end) {
  for (; p < end; p += kVsmCnt) {
    DeviceReset8(p);
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

void VsmRangeDeviceReset(uptr addr, uptr size) {
  if (size == 0)
    return;

  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;

  uptr first_aligned_cell = RoundUp(addr, kVsmCell);
  uptr end_aligned_cell = RoundDown(addr + size, kVsmCell);
  if (first_aligned_cell > addr) {
    VsmCellDeviceReset(addr, Min((first_aligned_cell - addr), size));
  }
  
  if (first_aligned_cell < end_aligned_cell) {
    VsmDeviceReset(MemToVsm(first_aligned_cell), MemToVsm(end_aligned_cell));
  }

  if (end_aligned_cell >= addr && end_aligned_cell < addr + size) {
    VsmCellDeviceReset(end_aligned_cell, addr + size - end_aligned_cell);
  }
}

ALWAYS_INLINE USED RawVsm* CheckVsmUtil(uptr addr, uptr size, u64 vmask) {
  int range_mask = kVsmCellBitMap >> (kVsmCell - size);
  uptr cell_start = RoundDown(addr, kVsmCell);
  RawVsm *vp = MemToVsm(cell_start);
  uptr offset =  addr - cell_start;
  range_mask <<= offset;
  m64 origin = _m_from_int64(LoadVsm8(vp));
  m64 mask = _m_from_int64(vmask);
  m64 result = _mm_cmpeq_pi8(_mm_and_si64(origin, mask), mask);
  int rewrite_mask = _mm_movemask_pi8(result);
  int error_bytes = range_mask ^ (range_mask & rewrite_mask);
  // xor is non-zero => at least one bit is not the same as the range_mask
  if (UNLIKELY(error_bytes)) {
    uptr error_start_offset = __builtin_ffs(error_bytes) - 1;
    return vp + error_start_offset;
  } else {
    return nullptr;
  }
}

ALWAYS_INLINE USED RawVsm* CheckVsmUtil16(uptr addr, u8 vmask) {
  RawVsm *vp;
  if (LIKELY(!(addr & 0xf))) {
    vp = MemToVsm(addr);
  } else {
    uptr cell_start = RoundDown(addr, kVsmCell);
    vp = MemToVsm(cell_start);
    uptr offset =  (addr - cell_start) * kMemToVsmRatio;
    vp += offset;
  }
  int range_mask = 0xFFFF;
  m128 origin = _mm_loadu_si128(reinterpret_cast<m128*>(vp));
  m128 mask = _mm_set1_epi8(vmask);
  m128 result = _mm_cmpeq_epi8(_mm_and_si128(origin, mask), mask);
  int rewrite_mask = _mm_movemask_epi8(result);
  int error_bytes = range_mask ^ rewrite_mask;
  if (UNLIKELY(error_bytes)) {
    uptr error_start_offset = __builtin_ffs(error_bytes) - 1;
    return vp + error_start_offset;
  } else {
    return nullptr;
  }
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
    RawVsm *error_vsm_ptr = CheckVsmUtil(corr_host_addr, size, VariableStateMachine::kDeviceMask8);
    if (UNLIKELY(error_vsm_ptr)) {
      VariableStateMachine v{*error_vsm_ptr};
      if (UNLIKELY(!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead))) {
        TraceSwitchPart(thr);
        UNUSED bool res = TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead);
      }
      ReportDMI(thr, addr, size, n, kAccessRead, v.IsDeviceInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
      return true;
    } else {
      return false;
    }
  } else {
    Node *n = ctx->h_to_t.find({addr, addr + size});
    if (!n) {
      return false;
    }
    RawVsm *error_vsm_ptr = CheckVsmUtil(addr, size, VariableStateMachine::kHostMask8);
    if (UNLIKELY(error_vsm_ptr)) {
      VariableStateMachine v{*error_vsm_ptr};
      if (UNLIKELY(!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead))) {
        TraceSwitchPart(thr);
        UNUSED bool res = TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead);
      }
      ReportDMI(thr, addr, size, n, kAccessRead, v.IsHostInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
      return true;
    } else {
      return false;
    }
  }
}

ALWAYS_INLINE USED bool CheckVsm16(ThreadState *thr, uptr pc, uptr addr) {
  constexpr uptr size = 16;
  if (thr->is_on_target) {
    Node *n = ctx->t_to_h.find({addr, addr + size});
    if (!n) {
      return false;
    }
    uptr corr_host_addr = n->info.start + (addr - n->interval.left_end);
    RawVsm *error_vsm_ptr = CheckVsmUtil16(corr_host_addr, static_cast<u8>(VariableStateMachine::kDeviceMask));
    if (UNLIKELY(error_vsm_ptr)) {
      VariableStateMachine v{*error_vsm_ptr};
      if (UNLIKELY(!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead))) {
        TraceSwitchPart(thr);
        UNUSED bool res = TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead);
      }
      ReportDMI(thr, addr, size, n, kAccessRead, v.IsDeviceInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
      return true;
    } else {
      return false;
    }
  } else {
    Node *n = ctx->h_to_t.find({addr, addr + size});
    if (!n) {
      return false;
    }
    RawVsm *error_vsm_ptr = CheckVsmUtil16(addr, static_cast<u8>(VariableStateMachine::kHostMask));
    if (UNLIKELY(error_vsm_ptr)) {
      VariableStateMachine v{*error_vsm_ptr};
      if (UNLIKELY(!TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead))) {
        TraceSwitchPart(thr);
        UNUSED bool res = TryTraceMemoryAccess(thr, pc, addr, size, kAccessRead);
      }
      ReportDMI(thr, addr, size, n, kAccessRead, v.IsHostInit() ? USE_OF_STALE_DATA : USE_OF_UNINITIALIZED_MEMORY);
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
  CheckVsm16(thr, pc, addr);
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

ALWAYS_INLINE USED void UpdateVsmUtil(uptr addr, uptr size, u64 value_bitmap, u64 set_mask) {
  m64 range_mask = _mm_srli_si64(_m_from_int64(value_bitmap), ((kVsmCell - size) * kMemToVsmRatioInBit));
  uptr cell_start = RoundDown(addr, kVsmCell);
  uptr offset =  addr - cell_start;
  range_mask = _mm_slli_si64(range_mask, (offset * kMemToVsmRatioInBit));
  m64 vmask = _m_from_int64(set_mask);
  RawVsm *vp = MemToVsm(cell_start);
  u64 vsm_val = LoadVsm8(vp);
  u64 curr;
  do {
    curr = vsm_val;
    u64 new_val = _m_to_int64(_mm_or_si64(_mm_andnot_si64(range_mask, _m_from_int64(curr)), _mm_and_si64(range_mask, vmask)));
    vsm_val = __sync_val_compare_and_swap(reinterpret_cast<u64 *>(vp), curr, new_val);
  } while (vsm_val != curr);
  // curr = vsm_val;
  // m64 new_val = _mm_or_si64(_mm_andnot_si64(range_mask, _m_from_int64(curr)), _mm_and_si64(range_mask, vmask));
  // StoreVsm8(vp, new_val);
}

// 'addr' must start at the 16-byte boundary to avoid the general-protection exception
// enforced by the SSE2 ISA 
ALWAYS_INLINE USED void UpdateVsmUtil16(uptr addr, u8 value_bitmap, u8 set_mask) {
  RawVsm *vp;
  if (LIKELY(!(addr & 0xf))) {
    vp = MemToVsm(addr);
  } else {
    uptr cell_start = RoundDown(addr, kVsmCell);
    vp = MemToVsm(cell_start);
    uptr offset =  (addr - cell_start) * kMemToVsmRatio;
    vp += offset;
  }
  m128 origin = _mm_loadu_si128(reinterpret_cast<m128*>(vp));
  m128 bitmap = _mm_set1_epi8(value_bitmap);
  m128 mask = _mm_set1_epi8(set_mask);
  m128 cleaned_bit = _mm_andnot_si128(bitmap, origin);
  m128 vsm_val = _mm_or_si128(cleaned_bit, mask);
  _mm_storeu_si128(reinterpret_cast<m128*>(vp), vsm_val);
}

ALWAYS_INLINE USED void UpdateVsm(ThreadState *thr, uptr addr, uptr size) {
  if (thr->is_on_target) {
    Node *n = ctx->t_to_h.find({addr, addr + size});
    if (!n) {
      return;
    }
    uptr corr_host_addr = n->info.start + (addr - n->interval.left_end);
    UpdateVsmUtil(corr_host_addr, size, VariableStateMachine::kDeviceValueBitMap8, VariableStateMachine::kDeviceMask8);
  } else {
    UpdateVsmUtil(addr, size, VariableStateMachine::kHostValueBitMap8, VariableStateMachine::kHostMask8);
  }                         
}

ALWAYS_INLINE USED void UpdateVsm16(ThreadState *thr, uptr addr) {
  constexpr uptr size = 16;
  if (thr->is_on_target) {
    Node *n = ctx->t_to_h.find({addr, addr + size});
    if (!n) {
      return;
    }
    uptr corr_host_addr = n->info.start + (addr - n->interval.left_end);
    UpdateVsmUtil16(corr_host_addr, static_cast<u8>(VariableStateMachine::kDeviceValueBitMap), static_cast<u8>(VariableStateMachine::kDeviceMask));
  } else {
    UpdateVsmUtil16(addr, static_cast<u8>(VariableStateMachine::kHostValueBitMap), static_cast<u8>(VariableStateMachine::kHostMask));
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
  UpdateVsm16(thr, addr);
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

ALWAYS_INLINE USED void CheckBound(ThreadState *thr, uptr pc, uptr base, uptr start, uptr size) {
  if (ctx->t_to_h.isOverflow(base, start)) {
    if (UNLIKELY(!TryTraceMemoryAccess(thr, pc, start, size, kAccessRead))) {
      TraceSwitchPart(thr);
      UNUSED bool res = TryTraceMemoryAccess(thr, pc, start, size, kAccessRead);
    }
    Node *n = ctx->t_to_h.find({base, base + size});
    ReportDMI(thr, start, size, n, kAccessRead, BUFFER_OVERFLOW);
  }
}

} // namespace __tsan

#if !SANITIZER_GO
// Must be included in this file to make sure everything is inlined.
#  include "tsan_arbalest_interface.inc"
#endif