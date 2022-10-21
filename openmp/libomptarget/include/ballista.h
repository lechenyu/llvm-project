#ifndef _OMPTARGET_BALLISTA_H
#define _OMPTARGET_BALLISTA_H

#include <cstdio>
#include <cstdint>
#include <atomic>

#include "ballista_defs.h"

extern uintptr_t *AppStartPtr;
extern uintptr_t *ShdwStartPtr;
extern bool BallistaEnabled;
extern std::atomic<int32_t> BallistaEnabledDeviceID;

#endif