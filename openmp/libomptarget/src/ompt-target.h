#ifndef LIBOMPTARGET_OMPT_TARGET_H
#define LIBOMPTARGET_OMPT_TARGET_H

#define FROM_LIBOMPTARGET 1
#include "ompt-target-api.h"
#undef FROM_LIBOMPTARGET

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)

extern ompt_target_callbacks_active_t OmptTargetEnabled;

extern int HostDeviceNum;

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

class OmptTargetDataOp {
private:
  ompt_target_data_op_t OpType;
  void *SrcAddr;
  int SrcDeviceNum;
  void *DestAddr;
  int DestDeviceNum;
  size_t Bytes;
  bool OmpRoutine;
  void *CodePtr;
  bool Active;

public:
  OmptTargetDataOp(ompt_target_data_op_t OpType, void *SrcAddr,
                   int SrcDeviceNum, void *DestAddr, int DestDeviceNum,
                   size_t Bytes, bool OmpRoutine, void *CodePtr);
  ~OmptTargetDataOp();
  void setDestAddr(void *DestAddr);
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
  unsigned int Capacity;
  unsigned int Size;
  void **HostAddr;
  void **DeviceAddr;
  size_t *Bytes;
  unsigned int *MappingFlags;
  void *CodePtr;
  bool Active;

public:
  OmptTargetMapping(ConstructorType Ctor, unsigned int Capacity, void *CodePtr);
  ~OmptTargetMapping();
  void addMapping(void *HstAddr, void *TgtAddr, size_t Byte, int64_t ArgType);
  void invokeCallback();
};

class OmptDeviceMem {
private:
  unsigned int DeviceMemFlag;
  void *OrigBaseAddr;
  void *OrigAddr;
  int OrigDeviceNum;
  void *DestAddr;
  int DestDeviceNum;
  size_t Bytes;
  void *CodePtr;
  char *VarName;
  bool Active;

public:
  OmptDeviceMem(void *OrigBaseAddr, void *OrigAddr, int OrigDeviceNum,
                void *DestAddr, int DestDeviceNum, size_t Bytes, void *CodePtr,
                char *VarName);
  ~OmptDeviceMem();
  void addTargetDataOp(unsigned int Flag);
  void setDestAddr(void *DestAddr);
  void invokeCallback();
};
#endif // LIBOMPTARGET_OMPT_TARGET_H