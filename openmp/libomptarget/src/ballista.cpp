#include <unistd.h>
#include <sys/personality.h>
#include <sys/auxv.h>
#include "ballista.h"
#include "private.h"
#include "omptarget.h"
#include "Debug.h"

uintptr_t *AppStartPtr;
uintptr_t *ShdwStartPtr;
uintptr_t *ShdwEndPtr;
uintptr_t AppStartVal;
uintptr_t ShdwStartVal;
uintptr_t ShdwEndVal;
bool BallistaEnabled = false;
std::atomic_int32_t BallistaEnabledDeviceID;
std::unordered_map<void *, void *> GlobMap;
thread_local BallistaTargetInfo GlobTargetInfo;
thread_local BallistaTargetInfo MappedVarTargetInfo;

#undef DEBUG_PREFIX
#define DEBUG_PREFIX "Ballista Runtime"

void checkAndDisableASLR(int Argc, char **Argv) {
  int OldPersonality = personality(0xffffffff);
  if (OldPersonality != -1 && (OldPersonality & ADDR_NO_RANDOMIZE) == 0) {
    DP("Set personality to disable ASLR\n");
    if (personality(OldPersonality | ADDR_NO_RANDOMIZE) == -1) {
      DP("Fail to set personality\n");
    }
    if (!Argc) {
      Argv = {nullptr};
    }
    const char *Pathname = reinterpret_cast<const char *>(getauxval(AT_EXECFN));
    execvp(Pathname, Argv);
  }
  DP("Execute the program without ASLR\n");
}

// return the number of shadow cell in a shadow array
int64_t getShadowArrayNum(int64_t AppMemSize) {
  return (AppMemSize +  ShadowCell - 1) / ShadowCell;
}

void *getShadowArrayAddr(void *AppMemAddr) {
  uintptr_t Addr = reinterpret_cast<uintptr_t>(AppMemAddr);
  return reinterpret_cast<BallistaShadowTy *>(ShdwStartVal) + ((Addr - AppStartVal) / ShadowCell);
}

int64_t getBufferSize(BallistaTargetInfo &MappedVariables, BallistaTargetInfo &Globals) {
  int64_t TotalShdwSize = 0;
  for (int i = 0; i < MappedVariables.Num; i++) {
    if (MappedVariables.TgtPtrs[i]) {
      TotalShdwSize += getShadowArrayNum(MappedVariables.Sizes[i]);
    }
  }
  for (int i = 0; i < Globals.Num; i++) {
    TotalShdwSize += getShadowArrayNum(Globals.Sizes[i]);
  }
  return TotalShdwSize;
}



