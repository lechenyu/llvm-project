#include "ballista.h"
#include "private.h"
#include <vector>

extern "C" void __ballista_register_glob_ptrs(void *AppStartInObj, void *ShdwStartInObj, void *ShdwEndInObj, int Argc, char **Argv) {
  DP("Ballista is enabled\n");
  //checkAndDisableASLR(Argc, Argv);
  BallistaEnabled = true;
  BallistaEnabledDeviceID = InvalidDeviceID;
  AppStartPtr = reinterpret_cast<uintptr_t *>(AppStartInObj);
  ShdwStartPtr = reinterpret_cast<uintptr_t *>(ShdwStartInObj);
  ShdwEndPtr = reinterpret_cast<uintptr_t *>(ShdwEndInObj);
}

extern "C" void __ballista_get_target_kernel_info(void **GlobPtrs, int64_t *GlobSizes, int GlobNum) {
  GlobTargetInfo = {GlobPtrs, nullptr, GlobSizes, GlobNum};
}

void preTargetKernel(void **HstPtrs, int64_t *Sizes, int64_t *MapTypes,
                     std::vector<void *> &TgtPtrs,
                     std::vector<int> &TgtPtrPositions, DeviceTy &Device,
                     AsyncInfoTy &AsyncInfo) {
  // Set up GlobTargetInfo and MappedVarTargetInfo
  if (GlobTargetInfo.Num) {
    void **GlobTgtPtrs = new void *[GlobTargetInfo.Num];
    for (int i = 0; i < GlobTargetInfo.Num; i++) {
      GlobTgtPtrs[i] = GlobMap.at(GlobTargetInfo.HstPtrs[i]);
    }
    GlobTargetInfo.TgtPtrs = GlobTgtPtrs;
  }
  int PtrNum = TgtPtrPositions.size();
  void **Ptrs = nullptr;
  if (PtrNum) {
    Ptrs = new void *[PtrNum]{};
  }
  for (int i = 0; i < PtrNum; i++) {
    if (!(MapTypes[i] & OMP_TGT_MAPTYPE_TARGET_PARAM) ||
        MapTypes[i] & OMP_TGT_MAPTYPE_LITERAL ||
        MapTypes[i] & OMP_TGT_MAPTYPE_PRIVATE) {
      continue;
    }
    Ptrs[i] = TgtPtrs[TgtPtrPositions[i]];
  }
  MappedVarTargetInfo = {HstPtrs, Ptrs, Sizes, PtrNum};

  // Initialize shadow memory on the device
  for (int i = 0; i < PtrNum; i++) {
    if (Ptrs[i]) {
      int Rc = Device.setData(getShadowArrayAddr(Ptrs[i]), 0, getShadowArrayNum(Sizes[i]) * ShadowSize, AsyncInfo);
      handleTargetOutcome(Rc == OFFLOAD_SUCCESS, nullptr);
    }
  }
  for (int i = 0; i < GlobTargetInfo.Num; i++) {
    int Rc = Device.setData(getShadowArrayAddr(GlobTargetInfo.TgtPtrs[i]), 0, getShadowArrayNum(GlobTargetInfo.Sizes[i]) * ShadowSize, AsyncInfo);
    handleTargetOutcome(Rc == OFFLOAD_SUCCESS, nullptr);
  }
}

int postTargetKernel(DeviceTy &Device, AsyncInfoTy &AsyncInfo) {
  int64_t BufferSize = getBufferSize(MappedVarTargetInfo, GlobTargetInfo);
  BallistaShadowTy *Buffer = nullptr;
  if (BufferSize) {
    Buffer = new BallistaShadowTy[BufferSize];
  }
  // Copy back shadow memory and analyze
  // Mapped variables first, then global variables
  BallistaShadowTy *Start = Buffer;
  std::vector<int> Index(MappedVarTargetInfo.Num + GlobTargetInfo.Num, -1);
  for (int i = 0; i < MappedVarTargetInfo.Num; i++) {
    if (MappedVarTargetInfo.TgtPtrs[i]) {
      uint64_t ShadowNum = getShadowArrayNum(MappedVarTargetInfo.Sizes[i]);
      int Rc = Device.retrieveData(Start, getShadowArrayAddr(MappedVarTargetInfo.TgtPtrs[i]), ShadowNum * ShadowSize, AsyncInfo);
      Index[i] = Start - Buffer;
      Start += ShadowNum;
      handleTargetOutcome(Rc == OFFLOAD_SUCCESS, nullptr);
    }
  }
  for (int i = 0; i < GlobTargetInfo.Num; i++) {
    uint64_t ShadowNum = getShadowArrayNum(GlobTargetInfo.Sizes[i]);
    int Rc = Device.retrieveData(Start, getShadowArrayAddr(GlobTargetInfo.TgtPtrs[i]), ShadowNum * ShadowSize, AsyncInfo);
    Index[MappedVarTargetInfo.Num + i] = Start - Buffer;
    Start += ShadowNum;
    handleTargetOutcome(Rc == OFFLOAD_SUCCESS, nullptr);
  }
  
  int Rc = AsyncInfo.synchronize();
  if (Rc == OFFLOAD_SUCCESS) {
    // check shadow memory
    for (int i = 0; i < MappedVarTargetInfo.Num; i++) {
      if (Index[i] != -1) {
        uint64_t ShadowNum = getShadowArrayNum(MappedVarTargetInfo.Sizes[i]);
        BallistaShadowTy *Start = &Buffer[Index[i]];
        bool IsSame = true;
        for (int j = 1; j < ShadowNum; j++) {
          if (Start[0] != Start[j]) {
            IsSame = false;
            break;
          }
        }
        if (IsSame) {
          printf("shadow for mapped var [%d] is [%d, %d, %d, %d]\n", i,
                 Start[0] >> 4, (Start[0] >> 2 & 0x1), (Start[0] >> 1 & 0x1), (Start[0] & 0x1));
        } else {
          printf("shadow for mapped var [%d] is inconsistent\n", i);
        }
      }
    }

    for (int i = 0; i < GlobTargetInfo.Num; i++) {
      uint64_t ShadowNum = getShadowArrayNum(GlobTargetInfo.Sizes[i]);
      BallistaShadowTy *Start = &Buffer[Index[MappedVarTargetInfo.Num + i]];
      bool IsSame = true;
      for (int j = 1; j < ShadowNum; j++) {
        if (Start[0] != Start[j]) {
          IsSame = false;
          break;
        }
      }
      if (IsSame) {
        printf("shadow for global var [%d] is [%d, %d, %d, %d]\n", i,
               Start[0] >> 4, (Start[0] >> 2 & 0x1), (Start[0] >> 1 & 0x1), (Start[0] & 0x1));
      } else {
        printf("shadow for global var [%d] is inconsistent\n", i);
      }
    }
  }
  
  if (Buffer) {
    delete[] Buffer;
  }

  if (MappedVarTargetInfo.TgtPtrs) {
    delete[] MappedVarTargetInfo.TgtPtrs;
  }

  if (GlobTargetInfo.TgtPtrs) {
    delete[] GlobTargetInfo.TgtPtrs;
  }
  MappedVarTargetInfo = {};
  GlobTargetInfo = {};
  return Rc;
}