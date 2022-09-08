#ifndef LIBOMPTARGET_OMPT_TARGET_H
#define LIBOMPTARGET_OMPT_TARGET_H

#define FROM_LIBOMPTARGET 1
#include "ompt-target-api.h"
#undef FROM_LIBOMPTARGET

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)

extern ompt_target_callbacks_active_t OmptTargetEnabled;

// RAII class for target event callbacks
class OmptTarget {
private:
  ompt_target_t Kind;
  int DeviceNum;
  void *CodePtr;
  bool Active;

public:
  OmptTarget(ompt_target_t Kind, int DeviceNum, void *CodePtr);
  ~OmptTarget();
};

#endif // LIBOMPTARGET_OMPT_TARGET_H