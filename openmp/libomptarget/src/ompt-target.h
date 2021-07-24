#ifndef LIBOMPTARGET_OMPT_TARGET_H
#define LIBOMPTARGET_OMPT_TARGET_H

#include "kmp_config.h"
#if OMPT_SUPPORT
#include "omp-tools.h"
#define FROM_LIBOMPTARGET 1
#include "ompt-target-api.h"
#undef FROM_LIBOMPTARGET

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)

#define OMPT_ARG(...) , __VA_ARGS__

#define OMPT_FIRST_ARG(...) __VA_ARGS__

extern ompt_target_callbacks_active_t OmptTargetEnabled;

extern int HostDeviceNum;

//TODO (lechenyu): Will move following classes into openmp runtime
//TODO (lechenyu): Need to add additional parameters to initialize and clean target_data for omp_target_* routines
class OmptTargetDataOp {
private:
    ompt_target_data_op_t OpType;
    void *SrcAddr;
    int SrcDeviceNum;
    void *DestAddr;
    int DestDeviceNum;
    size_t Bytes;
    bool OmpRoutine;
    void * CodePtr;
    bool Active;
public:
    OmptTargetDataOp(ompt_target_data_op_t OpType, void *SrcAddr, int SrcDeviceNum, void *DestAddr, int DestDeviceNum, size_t Bytes, bool OmpRoutine, void *CodePtr);
    ~OmptTargetDataOp();
    void setDestAddr(void *DestAddr);
};

class OmptTargetMapping {
private:
    int32_t Capacity;
    int32_t Size;
    void **HostAddr;
    void **DeviceAddr;
    size_t *Bytes;
    unsigned int *MappingFlags;
    void *CodePtr;
    bool Active;
public:
    typedef enum ConstructorType {
        TARGET              = 1,
        TARGET_DATA_BEGIN   = 2,
        TARGET_DATA_END     = 3
    } ConstructorType;

    OmptTargetMapping(int Capacity, void *CodePtr);
    ~OmptTargetMapping();
    void addMapping(void *HstAddr, void *TgtAddr, size_t Byte, int64_t ArgType, ConstructorType ConType);
    void invokeCallback();
};

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
#else

#define OMPT_ARG(...)
#define OMPT_FIRST_ARG(...)

#endif // OMPT_SUPPORT

#endif // LIBOMPTARGET_OMPT_TARGET_H
