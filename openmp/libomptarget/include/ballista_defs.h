#ifndef _OMPTARGET_BALLISTA_DEFS_H
#define _OMPTARGET_BALLISTA_DEFS_H

#include <cstdint>

using BallistaShadowTy = uint16_t;
constexpr int32_t InvalidDeviceID = -1;

// user bytes on the device are mapped onto a single shadow cell
constexpr unsigned ShadowCell = 4;
// size of a shadow word on the device
constexpr unsigned ShadowSize = sizeof(BallistaShadowTy);

#endif