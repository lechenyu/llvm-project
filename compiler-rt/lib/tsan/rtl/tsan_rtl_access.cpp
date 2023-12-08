//===-- tsan_rtl_access.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Definitions of memory access and function entry/exit entry points.
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"
namespace __tsan {
ConcurrencyVector* step_nodes;

extern "C" {
  void (*ompt_print)();
  void (*ompt_report_race_steps)(int,int);

  INTERFACE_ATTRIBUTE
  void __tsan_set_ompt_print_function(void (*f)()){
    ompt_print = f;
  }

  INTERFACE_ATTRIBUTE
  void __tsan_set_report_race_steps_function(void (*f)(int,int)){
    ompt_report_race_steps = f;
  }
}

// For DPST purpose, we assume shadow_mem[0] stores the last writer
// shadow_mem[1] stores the leftmost reader, shadow_mem[2] stores the rightmost reader
// constexpr u8 shadow_write_index = 0;
// constexpr u8 shadow_leftmost_read_index = 1;
// constexpr u8 shadow_rightmost_read_index = 2;

// /**
//  * @brief check if node a is to the left of node b in dpst
//  * @note 
//  * @param  a: node a != null
//  * @param  b: node b != null
//  * @retval true if a is to the left of b; false otherwise
//  */
// static bool a_to_the_left_of_b(TreeNode *a, TreeNode *b){
//   if (a->depth != b->depth) {
//     TreeNode *deep_node = (a->depth > b->depth) ? a : b;
//     TreeNode *shallow_node = (a->depth > b->depth) ? b : a;
//     int diff = deep_node->depth - shallow_node->depth;
//     for (int i = 0; i < diff; i++) {
//       deep_node = deep_node->parent;
//     }
//   }

//   while (a->parent != b->parent) {
//     a = a->parent;
//     b = b->parent;
//   }
//   return a->is_parent_nth_child < b->is_parent_nth_child;
// }


/**
 * @brief  Get the current step node, and return its corresponding_id (step id)
 * @param  thr: current ThreadState*
 * @retval the corresponding_id of current step node
 */
static int get_current_step_id(ThreadState* thr) {
  // TreeNode *current_task_node = thr->current_task_node;

  // if(current_task_node == nullptr){
  //   return kNullStepId;
  // }

  // if(current_task_node->current_finish_node == nullptr){
  //   return current_task_node->children_list_tail->corresponding_id;
  // }

  // return current_task_node->current_finish_node->children_list_tail->corresponding_id;
  return thr->step_id;
}


/**
 * @brief  check if prev precedes curr in dpst
 * @param  prev: previous step node != null
 * @param  curr: current step node != null
 * @param  lca:  output parameter to return the LCA
 * @parem  left: output parameter to return the left step node
 * @retval true if prev precdes curr by tree edges 
 */
static bool precede_dpst_new(TreeNode *prev, TreeNode *curr, TreeNode **lca, TreeNode **left) {

  if (prev->depth > curr->depth) {
    do {
      prev = prev->parent;
    } while (prev->depth > curr->depth);
  } else if (prev->depth < curr->depth) {
    do {
      curr = curr->parent;
    } while (curr->depth > prev->depth);
  }
  
  while (prev->parent != curr->parent) {
    prev = prev->parent;
    curr = curr->parent;
  }

  if (lca) {
    *lca = curr->parent;
  }

  if (left) {
    *left = (prev->is_parent_nth_child > curr->is_parent_nth_child) ? curr : prev;
  }

  if (prev->is_parent_nth_child > curr->is_parent_nth_child) {
    return false;
  }

  if ((prev->node_type == ASYNC_I || prev->node_type == ASYNC_E) &&
       prev->is_parent_nth_child > curr->preceeding_taskwait) {
    return false;
  } else {
    return true;
  }
}



ALWAYS_INLINE USED bool TryTraceMemoryAccess(ThreadState* thr, uptr pc,
                                             uptr addr, uptr size,
                                             AccessType typ) {
  DCHECK(size == 1 || size == 2 || size == 4 || size == 8);
  if (!kCollectHistory)
    return true;
  EventAccess* ev;
  if (UNLIKELY(!TraceAcquire(thr, &ev)))
    return false;
  u64 size_log = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
  uptr pc_delta = pc - thr->trace_prev_pc + (1 << (EventAccess::kPCBits - 1));
  thr->trace_prev_pc = pc;
  if (LIKELY(pc_delta < (1 << EventAccess::kPCBits))) {
    ev->is_access = 1;
    ev->is_read = !!(typ & kAccessRead);
    ev->is_atomic = !!(typ & kAccessAtomic);
    ev->size_log = size_log;
    ev->pc_delta = pc_delta;
    DCHECK_EQ(ev->pc_delta, pc_delta);
    ev->addr = CompressAddr(addr);
    TraceRelease(thr, ev);
    return true;
  }
  auto* evex = reinterpret_cast<EventAccessExt*>(ev);
  evex->is_access = 0;
  evex->is_func = 0;
  evex->type = EventType::kAccessExt;
  evex->is_read = !!(typ & kAccessRead);
  evex->is_atomic = !!(typ & kAccessAtomic);
  evex->size_log = size_log;
  // Note: this is important, see comment in EventAccessExt.
  evex->_ = 0;
  evex->addr = CompressAddr(addr);
  evex->pc = pc;
  TraceRelease(thr, evex);
  return true;
}

ALWAYS_INLINE
bool TryTraceMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr, uptr size,
                               AccessType typ) {
  if (!kCollectHistory)
    return true;
  EventAccessRange* ev;
  if (UNLIKELY(!TraceAcquire(thr, &ev)))
    return false;
  thr->trace_prev_pc = pc;
  ev->is_access = 0;
  ev->is_func = 0;
  ev->type = EventType::kAccessRange;
  ev->is_read = !!(typ & kAccessRead);
  ev->is_free = !!(typ & kAccessFree);
  ev->size_lo = size;
  ev->pc = CompressAddr(pc);
  ev->addr = CompressAddr(addr);
  ev->size_hi = size >> EventAccessRange::kSizeLoBits;
  TraceRelease(thr, ev);
  return true;
}

void TraceMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr, uptr size,
                            AccessType typ) {
  if (LIKELY(TryTraceMemoryAccessRange(thr, pc, addr, size, typ)))
    return;
  TraceSwitchPart(thr);
  UNUSED bool res = TryTraceMemoryAccessRange(thr, pc, addr, size, typ);
  DCHECK(res);
}

void TraceFunc(ThreadState* thr, uptr pc) {
  if (LIKELY(TryTraceFunc(thr, pc)))
    return;
  TraceSwitchPart(thr);
  UNUSED bool res = TryTraceFunc(thr, pc);
  DCHECK(res);
}

NOINLINE void TraceRestartFuncEntry(ThreadState* thr, uptr pc) {
  TraceSwitchPart(thr);
  FuncEntry(thr, pc);
}

NOINLINE void TraceRestartFuncExit(ThreadState* thr) {
  TraceSwitchPart(thr);
  FuncExit(thr);
}

void TraceMutexLock(ThreadState* thr, EventType type, uptr pc, uptr addr,
                    StackID stk) {
  DCHECK(type == EventType::kLock || type == EventType::kRLock);
  if (!kCollectHistory)
    return;
  EventLock ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = type;
  ev.pc = CompressAddr(pc);
  ev.stack_lo = stk;
  ev.stack_hi = stk >> EventLock::kStackIDLoBits;
  ev._ = 0;
  ev.addr = CompressAddr(addr);
  TraceEvent(thr, ev);
}

void TraceMutexUnlock(ThreadState* thr, uptr addr) {
  if (!kCollectHistory)
    return;
  EventUnlock ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = EventType::kUnlock;
  ev._ = 0;
  ev.addr = CompressAddr(addr);
  TraceEvent(thr, ev);
}

void TraceTime(ThreadState* thr) {
  if (!kCollectHistory)
    return;
  FastState fast_state = thr->fast_state;
  EventTime ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = EventType::kTime;
  ev.sid = static_cast<u64>(fast_state.sid());
  ev.epoch = static_cast<u64>(fast_state.epoch());
  ev._ = 0;
  TraceEvent(thr, ev);
}

// ALWAYS_INLINE RawShadow LoadShadow(RawShadow* p) {
//   return static_cast<RawShadow>(
//       atomic_load((atomic_uint32_t*)p, memory_order_relaxed));
// }

// ALWAYS_INLINE void StoreShadow(RawShadow* sp, RawShadow s) {
//   atomic_store((atomic_uint32_t*)sp, static_cast<u32>(s), memory_order_relaxed);
// }

// Shadow Layout for DPST Write|Empty|Read|Read
ALWAYS_INLINE u32 LoadWriteShadow(RawShadow *p) {
  return atomic_load((atomic_uint32_t *)p, memory_order_relaxed);
}

ALWAYS_INLINE u64 LoadReadShadow(RawShadow *p) {
  return atomic_load((atomic_uint64_t *)(p + 2), memory_order_relaxed);
}

ALWAYS_INLINE void StoreWriteShadow(RawShadow *sp, RawShadow s) {
  atomic_store((atomic_uint32_t *)sp, static_cast<u32>(s), memory_order_relaxed);
}

ALWAYS_INLINE void StoreReadShadow(RawShadow *sp, RawShadow sl, RawShadow sr) {
  u64 val = (static_cast<u64>(sl) << 32) | static_cast<u64>(sr);
  atomic_store((atomic_uint64_t *)(sp + 2), val, memory_order_relaxed);
}

// NOINLINE void DoReportRace(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
//                            Shadow old,
//                            AccessType typ) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
//   // For the free shadow markers the first element (that contains kFreeSid)
//   // triggers the race, but the second element contains info about the freeing
//   // thread, take it.


//   if (old.sid() == kFreeSid)
//     old = Shadow(LoadShadow(&shadow_mem[1]));
//   // This prevents trapping on this address in future.
//   for (uptr i = 0; i < kShadowCnt; i++)
//     StoreShadow(&shadow_mem[i], i == 0 ? Shadow::kRodata : Shadow::kEmpty);
//   // See the comment in MemoryRangeFreed as to why the slot is locked
//   // for free memory accesses. ReportRace must not be called with
//   // the slot locked because of the fork. But MemoryRangeFreed is not
//   // called during fork because fork sets ignore_reads_and_writes,
//   // so simply unlocking the slot should be fine.
//   if (typ & kAccessSlotLocked)
//     SlotUnlock(thr);
//   ReportRace(thr, shadow_mem, cur, Shadow(old), typ);
//   if (typ & kAccessSlotLocked)
//     SlotLock(thr);
// }

#if !TSAN_VECTORIZE
ALWAYS_INLINE
bool ContainsSameAccess(RawShadow* s, Shadow cur, int unused0, int unused1,
                        AccessType typ) {
  for (uptr i = 0; i < kShadowCnt; i++) {
    auto old = LoadShadow(&s[i]);
    if (!(typ & kAccessRead)) {
      if (old == cur.raw())
        return true;
      continue;
    }
    auto masked = static_cast<RawShadow>(static_cast<u32>(old) |
                                         static_cast<u32>(Shadow::kRodata));
    if (masked == cur.raw())
      return true;
    if (!(typ & kAccessNoRodata) && !SANITIZER_GO) {
      if (old == Shadow::kRodata)
        return true;
    }
  }
  return false;
}

ALWAYS_INLINE
bool CheckRaces(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                int unused0, int unused1, AccessType typ) {
  bool stored = false;
  for (uptr idx = 0; idx < kShadowCnt; idx++) {
    RawShadow* sp = &shadow_mem[idx];
    Shadow old(LoadShadow(sp));
    if (LIKELY(old.raw() == Shadow::kEmpty)) {
      if (!(typ & kAccessCheckOnly) && !stored)
        StoreShadow(sp, cur.raw());
      return false;
    }
    if (LIKELY(!(cur.access() & old.access())))
      continue;
    if (LIKELY(cur.sid() == old.sid())) {
      if (!(typ & kAccessCheckOnly) &&
          LIKELY(cur.access() == old.access() && old.IsRWWeakerOrEqual(typ))) {
        StoreShadow(sp, cur.raw());
        stored = true;
      }
      continue;
    }
    if (LIKELY(old.IsBothReadsOrAtomic(typ)))
      continue;
    if (LIKELY(thr->clock.Get(old.sid()) >= old.epoch()))
      continue;
    DoReportRace(thr, shadow_mem, cur, old, typ);
    return true;
  }
  // We did not find any races and had already stored
  // the current access info, so we are done.
  if (LIKELY(stored))
    return false;
  // Choose a random candidate slot and replace it.
  uptr index =
      atomic_load_relaxed(&thr->trace_pos) / sizeof(Event) % kShadowCnt;
  StoreShadow(&shadow_mem[index], cur.raw());
  return false;
}

// #  define LOAD_CURRENT_SHADOW(cur, shadow_mem) UNUSED int access = 0, shadow = 0

#else /* !TSAN_VECTORIZE */

// ALWAYS_INLINE
// bool ContainsSameAccess(RawShadow* unused0, Shadow unused1, m128 shadow,
//                         m128 access, AccessType typ) {
//   // Note: we could check if there is a larger access of the same type,
//   // e.g. we just allocated/memset-ed a block (so it contains 8 byte writes)
//   // and now do smaller reads/writes, these can also be considered as "same
//   // access". However, it will make the check more expensive, so it's unclear
//   // if it's worth it. But this would conserve trace space, so it's useful
//   // besides potential speed up.
//   if (!(typ & kAccessRead)) {
//     const m128 same = _mm_cmpeq_epi32(shadow, access);
//     return _mm_movemask_epi8(same);
//   }
//   // For reads we need to reset read bit in the shadow,
//   // because we need to match read with both reads and writes.
//   // Shadow::kRodata has only read bit set, so it does what we want.
//   // We also abuse it for rodata check to save few cycles
//   // since we already loaded Shadow::kRodata into a register.
//   // Reads from rodata can't race.
//   // Measurements show that they can be 10-20% of all memory accesses.
//   // Shadow::kRodata has epoch 0 which cannot appear in shadow normally
//   // (thread epochs start from 1). So the same read bit mask
//   // serves as rodata indicator.
//   const m128 read_mask = _mm_set1_epi32(static_cast<u32>(Shadow::kRodata));
//   const m128 masked_shadow = _mm_or_si128(shadow, read_mask);
//   m128 same = _mm_cmpeq_epi32(masked_shadow, access);
//   // Range memory accesses check Shadow::kRodata before calling this,
//   // Shadow::kRodatas is not possible for free memory access
//   // and Go does not use Shadow::kRodata.
//   if (!(typ & kAccessNoRodata) && !SANITIZER_GO) {
//     const m128 ro = _mm_cmpeq_epi32(shadow, read_mask);
//     same = _mm_or_si128(ro, same);
//   }
//   return _mm_movemask_epi8(same);
// }

// NOINLINE void DoReportRaceV(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
//                             u32 race_mask, m128 shadow, AccessType typ) {
//   // race_mask points which of the shadow elements raced with the current
//   // access. Extract that element.
//   CHECK_NE(race_mask, 0);
//   u32 old;
//   // Note: _mm_extract_epi32 index must be a constant value.
//   switch (__builtin_ffs(race_mask) / 4) {
//     case 0:
//       old = _mm_extract_epi32(shadow, 0);
//       break;
//     case 1:
//       old = _mm_extract_epi32(shadow, 1);
//       break;
//     case 2:
//       old = _mm_extract_epi32(shadow, 2);
//       break;
//     case 3:
//       old = _mm_extract_epi32(shadow, 3);
//       break;
//   }
//   Shadow prev(static_cast<RawShadow>(old));
//   // For the free shadow markers the first element (that contains kFreeSid)
//   // triggers the race, but the second element contains info about the freeing
//   // thread, take it.
//   if (prev.sid() == kFreeSid)
//     prev = Shadow(static_cast<RawShadow>(_mm_extract_epi32(shadow, 1)));
//   DoReportRace(thr, shadow_mem, cur, prev, typ);
// }

// ALWAYS_INLINE
// bool CheckRaces_old(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
//                 m128 shadow, m128 access, AccessType typ) {
//   // Note: empty/zero slots don't intersect with any access.
//   const m128 zero = _mm_setzero_si128();
//   const m128 mask_access = _mm_set1_epi32(0x000000ff);
//   const m128 mask_sid = _mm_set1_epi32(0x0000ff00);
//   const m128 mask_read_atomic = _mm_set1_epi32(0xc0000000);
//   const m128 access_and = _mm_and_si128(access, shadow);
//   const m128 access_xor = _mm_xor_si128(access, shadow);
//   const m128 intersect = _mm_and_si128(access_and, mask_access);
//   const m128 not_intersect = _mm_cmpeq_epi32(intersect, zero);
//   const m128 not_same_sid = _mm_and_si128(access_xor, mask_sid);
//   const m128 same_sid = _mm_cmpeq_epi32(not_same_sid, zero);
//   const m128 both_read_or_atomic = _mm_and_si128(access_and, mask_read_atomic);
//   const m128 no_race =
//       _mm_or_si128(_mm_or_si128(not_intersect, same_sid), both_read_or_atomic);
//   const int race_mask = _mm_movemask_epi8(_mm_cmpeq_epi32(no_race, zero));
//   if (UNLIKELY(race_mask))
//     goto SHARED;

// STORE : {
//   if (typ & kAccessCheckOnly)
//     return false;
//   // We could also replace different sid's if access is the same,
//   // rw weaker and happens before. However, just checking access below
//   // is not enough because we also need to check that !both_read_or_atomic
//   // (reads from different sids can be concurrent).
//   // Theoretically we could replace smaller accesses with larger accesses,
//   // but it's unclear if it's worth doing.
//   const m128 mask_access_sid = _mm_set1_epi32(0x0000ffff);
//   const m128 not_same_sid_access = _mm_and_si128(access_xor, mask_access_sid);
//   const m128 same_sid_access = _mm_cmpeq_epi32(not_same_sid_access, zero);
//   const m128 access_read_atomic =
//       _mm_set1_epi32((typ & (kAccessRead | kAccessAtomic)) << 30);
//   const m128 rw_weaker =
//       _mm_cmpeq_epi32(_mm_max_epu32(shadow, access_read_atomic), shadow);
//   const m128 rewrite = _mm_and_si128(same_sid_access, rw_weaker);
//   const int rewrite_mask = _mm_movemask_epi8(rewrite);
//   int index = __builtin_ffs(rewrite_mask);
//   if (UNLIKELY(index == 0)) {
//     const m128 empty = _mm_cmpeq_epi32(shadow, zero);
//     const int empty_mask = _mm_movemask_epi8(empty);
//     index = __builtin_ffs(empty_mask);
//     if (UNLIKELY(index == 0))
//       index = (atomic_load_relaxed(&thr->trace_pos) / 2) % 16;
//   }
//   StoreShadow(&shadow_mem[index / 4], cur.raw());
//   // We could zero other slots determined by rewrite_mask.
//   // That would help other threads to evict better slots,
//   // but it's unclear if it's worth it.
//   return false;
// }

// SHARED:
//   m128 thread_epochs = _mm_set1_epi32(0x7fffffff);
//   // Need to unwind this because _mm_extract_epi8/_mm_insert_epi32
//   // indexes must be constants.
// #  define LOAD_EPOCH(idx)                                                     \
//     if (LIKELY(race_mask & (1 << (idx * 4)))) {                               \
//       u8 sid = _mm_extract_epi8(shadow, idx * 4 + 1);                         \
//       u16 epoch = static_cast<u16>(thr->clock.Get(static_cast<Sid>(sid)));    \
//       thread_epochs = _mm_insert_epi32(thread_epochs, u32(epoch) << 16, idx); \
//     }
//   LOAD_EPOCH(0);
//   LOAD_EPOCH(1);
//   LOAD_EPOCH(2);
//   LOAD_EPOCH(3);
// #  undef LOAD_EPOCH
//   const m128 mask_epoch = _mm_set1_epi32(0x3fff0000);
//   const m128 shadow_epochs = _mm_and_si128(shadow, mask_epoch);
//   const m128 concurrent = _mm_cmplt_epi32(thread_epochs, shadow_epochs);
//   const int concurrent_mask = _mm_movemask_epi8(concurrent);
//   if (LIKELY(concurrent_mask == 0))
//     goto STORE;

//   DoReportRaceV(thr, shadow_mem, cur, concurrent_mask, shadow, typ);
//   return true;
// }

#endif

// char* DumpShadow(char* buf, RawShadow raw) {
//   if (raw == Shadow::kEmpty) {
//     internal_snprintf(buf, 64, "0");
//     return buf;
//   }
//   Shadow s(raw);
//   AccessType typ;
//   s.GetAccess(nullptr, nullptr, &typ);
//   internal_snprintf(buf, 64, "{tid=%u@%u access=0x%x typ=%x}",
//                     static_cast<u32>(s.sid()), static_cast<u32>(s.epoch()),
//                     s.access(), static_cast<u32>(typ));
//   return buf;
// }

// TryTrace* and TraceRestart* functions allow to turn memory access and func
// entry/exit callbacks into leaf functions with all associated performance
// benefits. These hottest callbacks do only 2 slow path calls: report a race
// and trace part switching. Race reporting is easy to turn into a tail call, we
// just always return from the runtime after reporting a race. But trace part
// switching is harder because it needs to be in the middle of callbacks. To
// turn it into a tail call we immidiately return after TraceRestart* functions,
// but TraceRestart* functions themselves recurse into the callback after
// switching trace part. As the result the hottest callbacks contain only tail
// calls, which effectively makes them leaf functions (can use all registers,
// no frame setup, etc).
NOINLINE void TraceRestartMemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size, AccessType typ) {
  TraceSwitchPart(thr);
  MemoryAccess(thr, pc, addr, size, typ);
}

enum RaceType {
  WriteWriteRace = 0,
  WriteReadRace  = 1,
  ReadWriteRace  = 2,
  AccessFreedMem = 4
};

// static const char* kRaceTypeName[] = {"write-write-race", "write-read-race",
//                                       "read-write-race", "", "use-after-free"};

NOINLINE void DoReportRaceDPST(ThreadState *thr, RawShadow* shadow_mem, Shadow cur, Shadow prev, RaceType race_type, AccessType typ, uptr addr, uptr pc) {
  if ((prev.is_atomic() && cur.is_atomic()) || !(prev.access_mask() & cur.access_mask())) {
    return;
  }
  
  ompt_report_race_steps(cur.step_id(), prev.step_id());

  if (!flags()->report_bugs || thr->suppress_reports) {
    return;
  }

  u32 curr_step_id = cur.step_id();
  u32 prev_step_id = prev.step_id();
  const TreeNode &curr_step = (*step_nodes)[curr_step_id];
  const TreeNode &prev_step = (*step_nodes)[prev_step_id];
  u8 rt_val = static_cast<u8>(race_type);
  
  //u8 access, Sid sid, u16 epoch, bool is_read, bool is_atomic
  Shadow prev_tsan(prev.ConvertMaskToAccess(), prev_step.sid, prev_step.ev, rt_val & 0x2, prev.is_atomic());
  Shadow cur_tsan(cur.ConvertMaskToAccess(), curr_step.sid, curr_step.ev, typ & kAccessRead, typ & kAccessAtomic);
  //Printf("Error type: %s at %016lx, Prev step: %u, Curr step: %u\n", kRaceTypeName[race_type], addr, prev_step_id, curr_step_id);
  ReportRace(thr, shadow_mem, cur_tsan, prev_tsan, typ, curr_step_id, prev_step_id);

  // TODO: if we use lock in ompt-tsan.cpp, PrintPC still works for the current access
  // The problem is that we cannot restore stack for the previous access
  PrintPC(pc);
}

// #  define LOAD_CURRENT_SHADOW(cur, shadow_mem)                         \
//     const m128 access = _mm_set1_epi32(static_cast<u32>((cur).raw())); \
//     const m128 shadow = _mm_load_si128(reinterpret_cast<m128*>(shadow_mem))


ALWAYS_INLINE
bool CheckWrite(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                AccessType typ, uptr addr, uptr pc) {
  u32 curr_step_id = cur.step_id();
  u32 prev_write = LoadWriteShadow(shadow_mem);
  Shadow w(static_cast<RawShadow>(prev_write));
  
  if (w.is_freed()) {
    DoReportRaceDPST(thr, shadow_mem, cur, w, AccessFreedMem, typ, addr, pc);
    return true;
  }
  //Printf("%u Write %p\n", curr_step_id, addr);
  if (curr_step_id == w.step_id()) {
    return false;
  }
  
  TreeNode *curr_step = &( (*step_nodes)[curr_step_id] );
  if (w.raw() != Shadow::kEmpty &&
      !precede_dpst_new(& ((*step_nodes)[w.step_id()]),
                        curr_step, nullptr, nullptr)) {
    DoReportRaceDPST(thr, shadow_mem, cur, w, WriteWriteRace, typ, addr, pc);
    return true;
  }

  u64 prev_reads = LoadReadShadow(shadow_mem);
  Shadow r1(static_cast<RawShadow>(static_cast<u32>(prev_reads >> 32)));
  Shadow r2(static_cast<RawShadow>(static_cast<u32>(prev_reads)));
  if (r1.raw() != Shadow::kEmpty &&
      !precede_dpst_new(& ((*step_nodes)[r1.step_id()]),
                        curr_step, nullptr, nullptr)) {
    DoReportRaceDPST(thr, shadow_mem, cur, r1, ReadWriteRace, typ, addr, pc);
    return true;
  }

  if (r2.raw() != Shadow::kEmpty &&
      !precede_dpst_new(&((*step_nodes)[r2.step_id()]),
                        curr_step, nullptr, nullptr)) {
    DoReportRaceDPST(thr, shadow_mem, cur, r2, ReadWriteRace, typ, addr, pc);
    return true;
  }

  if (!(typ & kAccessCheckOnly)){
    StoreWriteShadow(shadow_mem, cur.raw());
    StoreReadShadow(shadow_mem, Shadow::kEmpty, Shadow::kEmpty);
  }
  return false;
}

ALWAYS_INLINE
bool CheckRead(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
               AccessType typ, uptr addr, uptr pc) {
  u32 curr_step_id = cur.step_id();
  u32 prev_write = LoadWriteShadow(shadow_mem);
  Shadow w(static_cast<RawShadow>(prev_write));

  if (w.is_freed()) {
    DoReportRaceDPST(thr, shadow_mem, cur, w, AccessFreedMem, typ, addr, pc);
    return true;
  }
  //Printf("%u Read %p\n", curr_step_id, addr);
  u64 prev_reads = LoadReadShadow(shadow_mem);
  Shadow r1(static_cast<RawShadow>(static_cast<u32>(prev_reads >> 32)));
  Shadow r2(static_cast<RawShadow>(static_cast<u32>(prev_reads)));
  if (curr_step_id == r1.step_id() ||
      curr_step_id == r2.step_id()) {
    return false;
  }

  TreeNode *curr_step = &((*step_nodes)[curr_step_id]);

  if (w.raw() != Shadow::kEmpty &&
      !precede_dpst_new(&((*step_nodes)[w.step_id()]),
                        curr_step, nullptr, nullptr)) {
    DoReportRaceDPST(thr, shadow_mem, cur, w, WriteReadRace, typ, addr, pc);
    return true;
  }
  
  if (!(typ & kAccessCheckOnly)) {
    u8 not_empty_reads =
        (r1.raw() != Shadow::kEmpty) + (r2.raw() != Shadow::kEmpty);
    if (not_empty_reads == 0) {
      StoreReadShadow(shadow_mem, cur.raw(), Shadow::kEmpty);
    } else if (not_empty_reads == 1) {
      if (precede_dpst_new(&((*step_nodes)[r1.step_id()]), curr_step, nullptr,
                           nullptr)) {
        StoreReadShadow(shadow_mem, cur.raw(), Shadow::kEmpty);
      } else {
        StoreReadShadow(shadow_mem, r1.raw(), cur.raw());
      }
    } else {
      TreeNode* r1_step = &((*step_nodes)[r1.step_id()]);
      TreeNode* r2_step = &((*step_nodes)[r2.step_id()]);
      TreeNode *lca1, *lca2, *left1, *left2;
      bool hb1 = precede_dpst_new(r1_step, curr_step, &lca1, &left1);
      bool hb2 = precede_dpst_new(r2_step, curr_step, &lca2, &left2);
      if (hb1 && hb2) {
        StoreReadShadow(shadow_mem, cur.raw(), Shadow::kEmpty);
      }
      if (!(hb1 || hb2) && (left1 == left2)) {
        StoreReadShadow(shadow_mem, cur.raw(), r2.raw());
      }
    }
  }

  return false;
}

ALWAYS_INLINE USED void MemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                     uptr size, AccessType typ) {
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  RawShadow* shadow_mem = MemToShadow(addr);
  FastState fast_state = thr->fast_state;

  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;

  Shadow cur(typ & kAccessAtomic, typ & kAccessFree, addr, size, curr_step_id);

  if (UNLIKELY(static_cast<RawShadow>(LoadWriteShadow(shadow_mem)) == Shadow::kRodata)) {
    return;
  }

  // TODO: decide if we want to use the following if statement
  if (!TryTraceMemoryAccess(thr, pc, addr, size, typ))
    return TraceRestartMemoryAccess(thr, pc, addr, size, typ);
  // Printf("addr = %p, shadow = %p, size = %lu\n", (char *)addr, shadow_mem, size);
  if (typ & kAccessRead) {
    CheckRead(thr, shadow_mem, cur, typ, addr, pc);
  } else {
    CheckWrite(thr, shadow_mem, cur, typ, addr, pc);
  }
  return;
}

// ALWAYS_INLINE USED void MemoryAccess_old(ThreadState* thr, uptr pc, uptr addr,
//                                      uptr size, AccessType typ) {
//   RawShadow* shadow_mem = MemToShadow(addr);
//   UNUSED char memBuf[4][64];
//   DPrintf2("#%d: Access: %d@%d %p/%zd typ=0x%x {%s, %s, %s, %s}\n", thr->tid,
//            static_cast<int>(thr->fast_state.sid()),
//            static_cast<int>(thr->fast_state.epoch()), (void*)addr, size,
//            static_cast<int>(typ), DumpShadow(memBuf[0], shadow_mem[0]),
//            DumpShadow(memBuf[1], shadow_mem[1]),
//            DumpShadow(memBuf[2], shadow_mem[2]),
//            DumpShadow(memBuf[3], shadow_mem[3]));

//   FastState fast_state = thr->fast_state;
//   // Shadow cur(fast_state, addr, size, typ);
//   Shadow cur(fast_state, get_current_step_id(thr), addr, size, typ);

//   LOAD_CURRENT_SHADOW(cur, shadow_mem);
//   if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
//     return;
//   if (UNLIKELY(fast_state.GetIgnoreBit()))
//     return;
//   if (!TryTraceMemoryAccess(thr, pc, addr, size, typ))
//     return TraceRestartMemoryAccess(thr, pc, addr, size, typ);
//   CheckRaces_old(thr, shadow_mem, cur, shadow, access, typ);
// }

void MemoryAccess16(ThreadState* thr, uptr pc, uptr addr, AccessType typ);

NOINLINE
void RestartMemoryAccess16(ThreadState* thr, uptr pc, uptr addr,
                           AccessType typ) {
  TraceSwitchPart(thr);
  MemoryAccess16(thr, pc, addr, typ);
}

ALWAYS_INLINE USED void MemoryAccess16(ThreadState* thr, uptr pc, uptr addr,
                                       AccessType typ) {
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;
  Shadow cur(typ & kAccessAtomic, false, 0, 8, curr_step_id);
  RawShadow* shadow_mem = MemToShadow(addr);
  
  if (UNLIKELY(static_cast<RawShadow>(LoadWriteShadow(shadow_mem)) == Shadow::kRodata)) {
    return;
  }

  if (typ & kAccessRead) {
    if (UNLIKELY(CheckRead(thr, shadow_mem, cur, typ, addr, pc))) {
      return;
    }
    shadow_mem += kShadowCnt;
    CheckRead(thr, shadow_mem, cur, typ, addr + kShadowCell,  pc);
  } else {
    if (UNLIKELY(CheckWrite(thr, shadow_mem, cur, typ, addr, pc))) {
      return;
    }
    shadow_mem += kShadowCnt;
    CheckWrite(thr, shadow_mem, cur, typ, addr + kShadowCell, pc);
  }
}

NOINLINE
void RestartUnalignedMemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                  uptr size, AccessType typ) {
  TraceSwitchPart(thr);
  UnalignedMemoryAccess(thr, pc, addr, size, typ);
}

ALWAYS_INLINE USED void UnalignedMemoryAccess(ThreadState* thr, uptr pc,
                                              uptr addr, uptr size,
                                              AccessType typ) {
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  DCHECK_LE(size, 8);
  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;
  RawShadow* shadow_mem = MemToShadow(addr);

  if (UNLIKELY(static_cast<RawShadow>(LoadWriteShadow(shadow_mem)) == Shadow::kRodata)) {
    return;
  }

  uptr size1 = Min<uptr>(size, RoundUp(addr + 1, kShadowCell) - addr);
  Shadow cur(typ & kAccessAtomic, false, addr, size1, curr_step_id);
  if (!TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
    return RestartUnalignedMemoryAccess(thr, pc, addr, size, typ);
  if (typ & kAccessRead) {
    if (UNLIKELY(CheckRead(thr, shadow_mem, cur, typ, addr, pc))) {
      return;
    }
    uptr size2 = size - size1;
    if (LIKELY(size2 == 0)) {
      return;
    }
    addr = RoundUp(addr + 1, kShadowCell);
    shadow_mem += kShadowCnt;
    Shadow cur_next(typ & kAccessAtomic, false, 0, size2, curr_step_id);
    CheckRead(thr, shadow_mem, cur_next, typ, addr, pc);
  } else {
    if (UNLIKELY(CheckWrite(thr, shadow_mem, cur, typ, addr, pc))) {
      return;
    }
    uptr size2 = size - size1;
    if (LIKELY(size2 == 0)) {
      return;
    }
    addr = RoundUp(addr + 1, kShadowCell);
    shadow_mem += kShadowCnt;
    Shadow cur_next(typ & kAccessAtomic, false, 0, size2, curr_step_id);
    CheckWrite(thr, shadow_mem, cur_next, typ, addr, pc);
  }
}

void ShadowSet(RawShadow* p, RawShadow* end, RawShadow v) {
  DCHECK_LE(p, end);
  DCHECK(IsShadowMem(p));
  DCHECK(IsShadowMem(end));
  UNUSED const uptr kAlign = kShadowCnt * kShadowSize;
  DCHECK_EQ(reinterpret_cast<uptr>(p) % kAlign, 0);
  DCHECK_EQ(reinterpret_cast<uptr>(end) % kAlign, 0);
  for (; p < end; p += kShadowCnt) {
    StoreWriteShadow(p, v);
    StoreReadShadow(p, Shadow::kEmpty, Shadow::kEmpty);
  }
}

static void MemoryRangeSet(uptr addr, uptr size, RawShadow val) {
  if (size == 0)
    return;
  DCHECK_EQ(addr % kShadowCell, 0);
  DCHECK_EQ(size % kShadowCell, 0);
  // If a user passes some insane arguments (memset(0)),
  // let it just crash as usual.
  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;
  RawShadow* begin = MemToShadow(addr);
  RawShadow* end = begin + size / kShadowCell * kShadowCnt;
  // Don't want to touch lots of shadow memory.
  // If a program maps 10MB stack, there is no need reset the whole range.
  // UnmapOrDie/MmapFixedNoReserve does not work on Windows.
  if (SANITIZER_WINDOWS ||
      size <= common_flags()->clear_shadow_mmap_threshold) {
    ShadowSet(begin, end, val);
    return;
  }
  // The region is big, reset only beginning and end.
  const uptr kPageSize = GetPageSizeCached();
  // Set at least first kPageSize/2 to page boundary.
  RawShadow* mid1 =
      Min(end, reinterpret_cast<RawShadow*>(RoundUp(
                   reinterpret_cast<uptr>(begin) + kPageSize / 2, kPageSize)));
  ShadowSet(begin, mid1, val);
  // Reset middle part.
  RawShadow* mid2 = RoundDown(end, kPageSize);
  if (mid2 > mid1) {
    if (!MmapFixedSuperNoReserve((uptr)mid1, (uptr)mid2 - (uptr)mid1))
      Die();
  }
  // Set the ending.
  ShadowSet(mid2, end, val);
}

void MemoryResetRange(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  uptr addr1 = RoundDown(addr, kShadowCell);
  uptr size1 = RoundUp(size + addr - addr1, kShadowCell);
  MemoryRangeSet(addr1, size1, Shadow::kEmpty);
}

void MemoryRangeFreed(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  // Callers must lock the slot to ensure synchronization with the reset.
  // The problem with "freed" memory is that it's not "monotonic"
  // with respect to bug detection: freed memory is bad to access,
  // but then if the heap block is reallocated later, it's good to access.
  // As the result a garbage "freed" shadow can lead to a false positive
  // if it happens to match a real free in the thread trace,
  // but the heap block was reallocated before the current memory access,
  // so it's still good to access. It's not the case with data races.
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  DCHECK(thr->slot_locked);
  DCHECK_EQ(addr % kShadowCell, 0);
  size = RoundUp(size, kShadowCell);
  // Processing more than 1k (2k of shadow) is expensive,
  // can cause excessive memory consumption (user does not necessary touch
  // the whole range) and most likely unnecessary.
  size = Min<uptr>(size, 1024);
  const AccessType typ = kAccessWrite | kAccessFree | kAccessSlotLocked | kAccessNoRodata;
  TraceMemoryAccessRange(thr, pc, addr, size, typ);
  RawShadow* shadow_mem = MemToShadow(addr);
  uptr address = addr;
  Shadow cur(false, true, 0, kShadowCell, curr_step_id);
  for (; size; size -= kShadowCell, shadow_mem += kShadowCnt, address += kShadowCell) {
    if (UNLIKELY(CheckWrite(thr, shadow_mem, cur, typ, address, pc)))
      return;
  }
}

void MemoryRangeImitateWrite(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  DCHECK_EQ(addr % kShadowCell, 0);
  size = RoundUp(size, kShadowCell);
  TraceMemoryAccessRange(thr, pc, addr, size, kAccessWrite);
  Shadow cur(false, false, 0, kShadowCell, curr_step_id);
  MemoryRangeSet(addr, size, cur.raw());
}

void MemoryRangeImitateWriteOrResetRange(ThreadState* thr, uptr pc, uptr addr,
                                         uptr size) {
  if (thr->ignore_reads_and_writes == 0)
    MemoryRangeImitateWrite(thr, pc, addr, size);
  else
    MemoryResetRange(thr, pc, addr, size);
}

ALWAYS_INLINE
bool MemoryAccessRangeOne(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                          AccessType typ, uptr pc, uptr addr) {

  if (UNLIKELY(static_cast<RawShadow>(LoadWriteShadow(shadow_mem)) == Shadow::kRodata)) {
    return false;
  }
  if (typ & kAccessRead) {
    return CheckRead(thr, shadow_mem, cur, typ, addr, pc);
  } else {
    return CheckWrite(thr, shadow_mem, cur, typ, addr, pc);
  }
}

template <bool is_read>
NOINLINE void RestartMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size) {
  TraceSwitchPart(thr);
  MemoryAccessRangeT<is_read>(thr, pc, addr, size);
}

template <bool is_read>
void MemoryAccessRangeT(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  int curr_step_id = get_current_step_id(thr);
  if (curr_step_id == kNullStepId) {
    return;
  }
  const AccessType typ =
      (is_read ? kAccessRead : kAccessWrite) | kAccessNoRodata;
  RawShadow* shadow_mem = MemToShadow(addr);
  DPrintf2("#%d: MemoryAccessRange: @%p %p size=%d is_read=%d\n", thr->tid,
           (void*)pc, (void*)addr, (int)size, is_read);

#if SANITIZER_DEBUG
  if (!IsAppMem(addr)) {
    Printf("Access to non app mem %zx\n", addr);
    DCHECK(IsAppMem(addr));
  }
  if (!IsAppMem(addr + size - 1)) {
    Printf("Access to non app mem %zx\n", addr + size - 1);
    DCHECK(IsAppMem(addr + size - 1));
  }
  if (!IsShadowMem(shadow_mem)) {
    Printf("Bad shadow addr %p (%zx)\n", static_cast<void*>(shadow_mem), addr);
    DCHECK(IsShadowMem(shadow_mem));
  }
  if (!IsShadowMem(shadow_mem + size * kShadowCnt - 1)) {
    Printf("Bad shadow addr %p (%zx)\n",
           static_cast<void*>(shadow_mem + size * kShadowCnt - 1),
           addr + size - 1);
    DCHECK(IsShadowMem(shadow_mem + size * kShadowCnt - 1));
  }
#endif

  // Access to .rodata section, no races here.
  // Measurements show that it can be 10-20% of all memory accesses.
  // Check here once to not check for every access separately.
  // Note: we could (and should) do this only for the is_read case
  // (writes shouldn't go to .rodata). But it happens in Chromium tests:
  // https://bugs.chromium.org/p/chromium/issues/detail?id=1275581#c19
  // Details are unknown since it happens only on CI machines.
  if (*shadow_mem == Shadow::kRodata)
    return;

  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;

  if (!TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
    return RestartMemoryAccessRange<is_read>(thr, pc, addr, size);

  uptr address = addr;
  if (UNLIKELY(addr % kShadowCell)) {
    // Handle unaligned beginning, if any.
    uptr size1 = Min(size, RoundUp(addr, kShadowCell) - addr);
    Shadow cur(false, false, addr, size1, curr_step_id);
    size -= size1;
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ, addr, pc)))
      return;
    shadow_mem += kShadowCnt;
    address += size1;
  }
  // Handle middle part, if any.
  Shadow cur(false, false, 0, kShadowCell, curr_step_id);
  for (; size >= kShadowCell; size -= kShadowCell, shadow_mem += kShadowCnt, address += kShadowCell) {
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ, address, pc)))
      return;
  }
  // Handle ending, if any.
  if (UNLIKELY(size)) {
    Shadow cur(false, false, 0, size, curr_step_id);
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ, address, pc)))
      return;
  }
}

template void MemoryAccessRangeT<true>(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size);
template void MemoryAccessRangeT<false>(ThreadState* thr, uptr pc, uptr addr,
                                        uptr size);

}  // namespace __tsan

#if !SANITIZER_GO
// Must be included in this file to make sure everything is inlined.
#  include "tsan_interface.inc"
#endif
