#include "ballista.h"
#include "Debug.h"

uintptr_t *AppStartPtr;
uintptr_t *ShdwStartPtr;
bool BallistaEnabled = false;
std::atomic_int32_t BallistaEnabledDeviceID;

#define DEBUG_PREFIX "Ballista Runtime"
extern "C" void ballista_register_glob_ptrs(void *AppStartInObj, void *ShdwStartInObj) {
  DP("Ballista is enabled\n");
  BallistaEnabled = true;
  BallistaEnabledDeviceID = InvalidDeviceID;
  AppStartPtr = reinterpret_cast<uintptr_t *>(AppStartInObj);
  ShdwStartPtr = reinterpret_cast<uintptr_t *>(ShdwStartInObj);
}

