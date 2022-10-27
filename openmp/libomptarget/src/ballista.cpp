#include <unistd.h>
#include <sys/personality.h>
#include <sys/auxv.h>
#include "ballista.h"
#include "Debug.h"

uintptr_t *AppStartPtr;
uintptr_t *ShdwStartPtr;
bool BallistaEnabled = false;
std::atomic_int32_t BallistaEnabledDeviceID;

#define DEBUG_PREFIX "Ballista Runtime"

namespace {

void initializeShadowMemoryForHost() {

}

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

} // end anonymous namespace

extern "C" void ballista_register_glob_ptrs(void *AppStartInObj, void *ShdwStartInObj, int Argc, char **Argv) {
  DP("Ballista is enabled\n");
  checkAndDisableASLR(Argc, Argv);
  initializeShadowMemoryForHost();
  BallistaEnabled = true;
  BallistaEnabledDeviceID = InvalidDeviceID;
  AppStartPtr = reinterpret_cast<uintptr_t *>(AppStartInObj);
  ShdwStartPtr = reinterpret_cast<uintptr_t *>(ShdwStartInObj);
}

