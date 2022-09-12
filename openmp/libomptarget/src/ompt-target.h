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

class OmptTargetSubmit {
private:
  unsigned int RequestedNumTeams;
  bool Active;

public:
  OmptTargetSubmit(unsigned int RequestedNumTeams);
  ~OmptTargetSubmit();
};

class OmptTargetMapping {
public:
  typedef enum ConstructorType {
    TARGET = 1,
    TARGET_DATA_BEGIN = 2,
    TARGET_DATA_END = 3
  } ConstructorType;

private:
  ConstructorType Ctor;
  int32_t Capacity;
  int32_t Size;
  void **HostAddr;
  void **DeviceAddr;
  size_t *Bytes;
  unsigned int *MappingFlags;
  void *CodePtr;
  bool Active;

public:
  OmptTargetMapping(ConstructorType Ctor, int32_t Capacity, void *CodePtr);
  ~OmptTargetMapping();
  void addMapping(void *HstAddr, void *TgtAddr, size_t Byte, int64_t ArgType);
  void invokeCallback();
};
#endif // LIBOMPTARGET_OMPT_TARGET_H