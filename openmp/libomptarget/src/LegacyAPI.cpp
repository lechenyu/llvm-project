//===-------- LegacyAPI.cpp - Target independent OpenMP target RTL --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Legacy interfaces for libomptarget used to maintain backwards-compatibility.
//
//===----------------------------------------------------------------------===//

#include "omptarget.h"
#include "private.h"

#if OMPTARGET_OMPT_SUPPORT
#include "ompt-target.h"
#endif

EXTERN void __tgt_target_data_begin(int64_t DeviceId, int32_t ArgNum,
                                    void **ArgsBase, void **Args,
                                    int64_t *ArgSizes, int64_t *ArgTypes) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_begin_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                   ArgSizes, ArgTypes, nullptr, nullptr, false,
                                   OMPT_GET_RETURN_ADDRESS(0));
#else
  __tgt_target_data_begin_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                   ArgSizes, ArgTypes, nullptr, nullptr, false);
#endif
}

EXTERN void __tgt_target_data_begin_nowait(int64_t DeviceId, int32_t ArgNum,
                                           void **ArgsBase, void **Args,
                                           int64_t *ArgSizes, int64_t *ArgTypes,
                                           int32_t DepNum, void *DepList,
                                           int32_t NoAliasDepNum,
                                           void *NoAliasDepList) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_begin_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                   ArgSizes, ArgTypes, nullptr, nullptr, true,
                                   OMPT_GET_RETURN_ADDRESS(0));
#else
  __tgt_target_data_begin_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                   ArgSizes, ArgTypes, nullptr, nullptr, true);
#endif
}

EXTERN void __tgt_target_data_end(int64_t DeviceId, int32_t ArgNum,
                                  void **ArgsBase, void **Args,
                                  int64_t *ArgSizes, int64_t *ArgTypes) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_end_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                 ArgSizes, ArgTypes, nullptr, nullptr, false,
                                 OMPT_GET_RETURN_ADDRESS(0));

#else
  __tgt_target_data_end_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                 ArgSizes, ArgTypes, nullptr, nullptr, false);
#endif
}

EXTERN void __tgt_target_data_update(int64_t DeviceId, int32_t ArgNum,
                                     void **ArgsBase, void **Args,
                                     int64_t *ArgSizes, int64_t *ArgTypes) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_update_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                    ArgSizes, ArgTypes, nullptr, nullptr, false,
                                    OMPT_GET_RETURN_ADDRESS(0));
#else
  __tgt_target_data_update_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                    ArgSizes, ArgTypes, nullptr, nullptr,
                                    false);
#endif
}

EXTERN void __tgt_target_data_update_nowait(
    int64_t DeviceId, int32_t ArgNum, void **ArgsBase, void **Args,
    int64_t *ArgSizes, int64_t *ArgTypes, int32_t DepNum, void *DepList,
    int32_t NoAliasDepNum, void *NoAliasDepList) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_update_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                    ArgSizes, ArgTypes, nullptr, nullptr, true,
                                    OMPT_GET_RETURN_ADDRESS(0));
#else
  __tgt_target_data_update_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                    ArgSizes, ArgTypes, nullptr, nullptr, true);
#endif
}

EXTERN void __tgt_target_data_end_nowait(int64_t DeviceId, int32_t ArgNum,
                                         void **ArgsBase, void **Args,
                                         int64_t *ArgSizes, int64_t *ArgTypes,
                                         int32_t DepNum, void *DepList,
                                         int32_t NoAliasDepNum,
                                         void *NoAliasDepList) {
  TIMESCOPE();

#if OMPTARGET_OMPT_SUPPORT
  __tgt_target_data_end_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                 ArgSizes, ArgTypes, nullptr, nullptr, true,
                                 OMPT_GET_RETURN_ADDRESS(0));
#else
  __tgt_target_data_end_internal(nullptr, DeviceId, ArgNum, ArgsBase, Args,
                                 ArgSizes, ArgTypes, nullptr, nullptr, true);
#endif
}

EXTERN int __tgt_target_mapper(ident_t *Loc, int64_t DeviceId, void *HostPtr,
                               int32_t ArgNum, void **ArgsBase, void **Args,
                               int64_t *ArgSizes, int64_t *ArgTypes,
                               map_var_info_t *ArgNames, void **ArgMappers) {
  TIMESCOPE_WITH_IDENT(Loc);
  __tgt_kernel_arguments KernelArgs{
      1, ArgNum, ArgsBase, Args, ArgSizes, ArgTypes, ArgNames, ArgMappers, -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(Loc, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, false,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(Loc, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, false);
#endif
}

EXTERN int __tgt_target(int64_t DeviceId, void *HostPtr, int32_t ArgNum,
                        void **ArgsBase, void **Args, int64_t *ArgSizes,
                        int64_t *ArgTypes) {
  TIMESCOPE();
  __tgt_kernel_arguments KernelArgs{1,        ArgNum,  ArgsBase, Args, ArgSizes,
                                    ArgTypes, nullptr, nullptr,  -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(nullptr, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, false,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(nullptr, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, false);
#endif
}

EXTERN int __tgt_target_nowait(int64_t DeviceId, void *HostPtr, int32_t ArgNum,
                               void **ArgsBase, void **Args, int64_t *ArgSizes,
                               int64_t *ArgTypes, int32_t DepNum, void *DepList,
                               int32_t NoAliasDepNum, void *NoAliasDepList) {
  TIMESCOPE();
  __tgt_kernel_arguments KernelArgs{1,        ArgNum,  ArgsBase, Args, ArgSizes,
                                    ArgTypes, nullptr, nullptr,  -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(nullptr, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, true,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(nullptr, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, true);
#endif
}

EXTERN int __tgt_target_nowait_mapper(
    ident_t *Loc, int64_t DeviceId, void *HostPtr, int32_t ArgNum,
    void **ArgsBase, void **Args, int64_t *ArgSizes, int64_t *ArgTypes,
    map_var_info_t *ArgNames, void **ArgMappers, int32_t DepNum, void *DepList,
    int32_t NoAliasDepNum, void *NoAliasDepList) {
  TIMESCOPE_WITH_IDENT(Loc);
  __tgt_kernel_arguments KernelArgs{
      1, ArgNum, ArgsBase, Args, ArgSizes, ArgTypes, ArgNames, ArgMappers, -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(Loc, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, true,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(Loc, DeviceId, -1, -1, HostPtr,
                                      &KernelArgs, true);
#endif
}

EXTERN int __tgt_target_teams_mapper(ident_t *Loc, int64_t DeviceId,
                                     void *HostPtr, int32_t ArgNum,
                                     void **ArgsBase, void **Args,
                                     int64_t *ArgSizes, int64_t *ArgTypes,
                                     map_var_info_t *ArgNames,
                                     void **ArgMappers, int32_t NumTeams,
                                     int32_t ThreadLimit) {
  TIMESCOPE_WITH_IDENT(Loc);
  __tgt_kernel_arguments KernelArgs{
      1, ArgNum, ArgsBase, Args, ArgSizes, ArgTypes, ArgNames, ArgMappers, -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(Loc, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, false,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(Loc, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, false);
#endif
}

EXTERN int __tgt_target_teams(int64_t DeviceId, void *HostPtr, int32_t ArgNum,
                              void **ArgsBase, void **Args, int64_t *ArgSizes,
                              int64_t *ArgTypes, int32_t NumTeams,
                              int32_t ThreadLimit) {
  TIMESCOPE();
  __tgt_kernel_arguments KernelArgs{1,        ArgNum,  ArgsBase, Args, ArgSizes,
                                    ArgTypes, nullptr, nullptr,  -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(nullptr, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, false,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(nullptr, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, false);
#endif
}

EXTERN int __tgt_target_teams_nowait(int64_t DeviceId, void *HostPtr,
                                     int32_t ArgNum, void **ArgsBase,
                                     void **Args, int64_t *ArgSizes,
                                     int64_t *ArgTypes, int32_t NumTeams,
                                     int32_t ThreadLimit, int32_t DepNum,
                                     void *DepList, int32_t NoAliasDepNum,
                                     void *NoAliasDepList) {
  TIMESCOPE();
  __tgt_kernel_arguments KernelArgs{1,        ArgNum,  ArgsBase, Args, ArgSizes,
                                    ArgTypes, nullptr, nullptr,  -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(nullptr, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, true,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(nullptr, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, true);
#endif
}

EXTERN int __tgt_target_teams_nowait_mapper(
    ident_t *Loc, int64_t DeviceId, void *HostPtr, int32_t ArgNum,
    void **ArgsBase, void **Args, int64_t *ArgSizes, int64_t *ArgTypes,
    map_var_info_t *ArgNames, void **ArgMappers, int32_t NumTeams,
    int32_t ThreadLimit, int32_t DepNum, void *DepList, int32_t NoAliasDepNum,
    void *NoAliasDepList) {
  TIMESCOPE_WITH_IDENT(Loc);
  __tgt_kernel_arguments KernelArgs{
      1, ArgNum, ArgsBase, Args, ArgSizes, ArgTypes, ArgNames, ArgMappers, -1};

#if OMPTARGET_OMPT_SUPPORT
  return __tgt_target_kernel_internal(Loc, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, true,
                                      OMPT_GET_RETURN_ADDRESS(0));
#else
  return __tgt_target_kernel_internal(Loc, DeviceId, NumTeams, ThreadLimit,
                                      HostPtr, &KernelArgs, true);
#endif
}

EXTERN void __kmpc_push_target_tripcount_mapper(ident_t *Loc, int64_t DeviceId,
                                                uint64_t LoopTripcount) {
  TIMESCOPE_WITH_IDENT(Loc);
  if (checkDeviceAndCtors(DeviceId, Loc)) {
    DP("Not offloading to device %" PRId64 "\n", DeviceId);
    return;
  }

  DP("__kmpc_push_target_tripcount(%" PRId64 ", %" PRIu64 ")\n", DeviceId,
     LoopTripcount);
  PM->TblMapMtx.lock();
  PM->Devices[DeviceId]->LoopTripCnt.emplace(__kmpc_global_thread_num(NULL),
                                             LoopTripcount);
  PM->TblMapMtx.unlock();
}

EXTERN void __kmpc_push_target_tripcount(int64_t DeviceId,
                                         uint64_t LoopTripcount) {
  __kmpc_push_target_tripcount_mapper(nullptr, DeviceId, LoopTripcount);
}
