#ifndef _OMPTARGET_BALLISTA_H
#define _OMPTARGET_BALLISTA_H

#include <atomic>
#include <unordered_map>
#include <vector>
#include "ballista_defs.h"

struct BallistaMemData {
  void *AppStart;
  void *ShdwStart;
  void *ShdwEnd;
};

struct BallistaTargetInfo {
  void **HstPtrs;
  void **TgtPtrs;
  int64_t *Sizes;
  int Num;
};

struct __tgt_kernel_arguments;
struct DeviceTy;
class AsyncInfoTy;

extern uintptr_t *AppStartPtr;
extern uintptr_t *ShdwStartPtr;
extern uintptr_t *ShdwEndPtr;
extern uintptr_t AppStartVal;
extern uintptr_t ShdwStartVal;
extern uintptr_t ShdwEndVal;
extern bool BallistaEnabled;
extern std::atomic<int32_t> BallistaEnabledDeviceID;
extern std::unordered_map<void *, void *> GlobMap;
extern thread_local BallistaTargetInfo GlobTargetInfo;
extern thread_local BallistaTargetInfo MappedVarTargetInfo;

void checkAndDisableASLR(int Argc, char **Argv);

int64_t getShadowArrayNum(int64_t AppMemSize);

void *getShadowArrayAddr(void *AppMemAddr);

int64_t getBufferSize(BallistaTargetInfo &MappedVariables, BallistaTargetInfo &Globals);

void preTargetKernel(void **HstPtrs, int64_t *Sizes, int64_t *MapTypes,
                     std::vector<void *> &TgtPtrs,
                     std::vector<int> &TgtPtrPositions, DeviceTy &Device,
                     AsyncInfoTy &AsyncInfo);

int postTargetKernel(DeviceTy &Device, AsyncInfoTy &AsyncInfo);

#endif