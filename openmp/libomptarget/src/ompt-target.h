#ifndef LIBOMPTARGET_OMPT_TARGET_H
#define LIBOMPTARGET_OMPT_TARGET_H

#define FROM_LIBOMPTARGET 1
#include "ompt-target-api.h"
#undef FROM_LIBOMPTARGET

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)

extern ompt_target_callbacks_active_t OmptTargetEnabled;

#endif // LIBOMPTARGET_OMPT_TARGET_H