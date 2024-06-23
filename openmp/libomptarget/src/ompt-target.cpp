#include "ompt-target.h"
// #include "omptarget.h"

/// Data attributes for each data reference used in an OpenMP target region. Copied from omptarget.h

enum tgt_map_type {
  // No flags
  OMP_TGT_MAPTYPE_NONE            = 0x000,
  // copy data from host to device
  OMP_TGT_MAPTYPE_TO              = 0x001,
  // copy data from device to host
  OMP_TGT_MAPTYPE_FROM            = 0x002,
  // copy regardless of the reference count
  OMP_TGT_MAPTYPE_ALWAYS          = 0x004,
  // force unmapping of data
  OMP_TGT_MAPTYPE_DELETE          = 0x008,
  // map the pointer as well as the pointee
  OMP_TGT_MAPTYPE_PTR_AND_OBJ     = 0x010,
  // pass device base address to kernel
  OMP_TGT_MAPTYPE_TARGET_PARAM    = 0x020,
  // return base device address of mapped data
  OMP_TGT_MAPTYPE_RETURN_PARAM    = 0x040,
  // private variable - not mapped
  OMP_TGT_MAPTYPE_PRIVATE         = 0x080,
  // copy by value - not mapped
  OMP_TGT_MAPTYPE_LITERAL         = 0x100,
  // mapping is implicit
  OMP_TGT_MAPTYPE_IMPLICIT        = 0x200,
  // copy data to device
  OMP_TGT_MAPTYPE_CLOSE           = 0x400,
  // runtime error if not already allocated
  OMP_TGT_MAPTYPE_PRESENT         = 0x1000,
  // use a separate reference counter so that the data cannot be unmapped within
  // the structured region
  // This is an OpenMP extension for the sake of OpenACC support.
  OMP_TGT_MAPTYPE_OMPX_HOLD       = 0x2000,
  // descriptor for non-contiguous target-update
  OMP_TGT_MAPTYPE_NON_CONTIG      = 0x100000000000,
  // member of struct, member given by [16 MSBs] - 1
  OMP_TGT_MAPTYPE_MEMBER_OF       = 0xffff000000000000
};

ompt_target_callbacks_active_t OmptTargetEnabled;

int HostDeviceNum;

OmptTarget::OmptTarget(ompt_target_t Kind, int DeviceNum, void *CodePtr)
    : Kind(Kind), DeviceNum(DeviceNum), CodePtr(CodePtr) {
  this->Active =
      OmptTargetEnabled.enabled && OmptTargetEnabled.ompt_callback_target_emi;
  if (Active) {
    libomp_ompt_callback_target_emi(Kind, ompt_scope_begin, DeviceNum, CodePtr);
  }
}

OmptTarget::~OmptTarget() {
  if (Active) {
    libomp_ompt_callback_target_emi(Kind, ompt_scope_end, DeviceNum, CodePtr);
  }
}

OmptTargetSubmit::OmptTargetSubmit(unsigned int RequestedNumTeams)
    : RequestedNumTeams(RequestedNumTeams) {
  this->Active = OmptTargetEnabled.enabled &&
                 OmptTargetEnabled.ompt_callback_target_submit_emi;
  if (Active) {
    libomp_ompt_callback_target_submit_emi(ompt_scope_begin, RequestedNumTeams);
  }
}

OmptTargetSubmit::~OmptTargetSubmit() {
  if (Active) {
    libomp_ompt_callback_target_submit_emi(ompt_scope_end, RequestedNumTeams);
  }
}

OmptTargetDataOp::OmptTargetDataOp(ompt_target_data_op_t OpType, void *SrcAddr,
                                   int SrcDeviceNum, void *DestAddr,
                                   int DestDeviceNum, size_t Bytes,
                                   bool OmpRoutine, void *CodePtr)
    : OpType(OpType), SrcAddr(SrcAddr), SrcDeviceNum(SrcDeviceNum),
      DestAddr(DestAddr), DestDeviceNum(DestDeviceNum), Bytes(Bytes),
      OmpRoutine(OmpRoutine), CodePtr(CodePtr) {
  this->Active = OmptTargetEnabled.enabled &&
                 OmptTargetEnabled.ompt_callback_target_data_op_emi;
  if (Active) {
    libomp_ompt_callback_target_data_op_emi(
        ompt_scope_begin, OpType, SrcAddr, SrcDeviceNum, DestAddr,
        DestDeviceNum, Bytes, OmpRoutine, CodePtr);
  }
}

OmptTargetDataOp::~OmptTargetDataOp() {
  if (Active) {
    libomp_ompt_callback_target_data_op_emi(
        ompt_scope_end, OpType, SrcAddr, SrcDeviceNum, DestAddr, DestDeviceNum,
        Bytes, OmpRoutine, CodePtr);
  }
}

void OmptTargetDataOp::setDestAddr(void *DestAddr) {
  this->DestAddr = DestAddr;
}

OmptTargetMapping::OmptTargetMapping(ConstructorType Ctor,
                                     unsigned int Capacity, void *CodePtr)
    : Ctor(Ctor), Capacity(Capacity), Size(0), CodePtr(CodePtr) {
  this->Active = OmptTargetEnabled.enabled &&
                 OmptTargetEnabled.ompt_callback_target_map_emi;
  if (Active && Capacity) {
    HostAddr = new void *[Capacity];
    DeviceAddr = new void *[Capacity];
    Bytes = new size_t[Capacity];
    MappingFlags = new unsigned int[Capacity];
  } else {
    HostAddr = nullptr;
    DeviceAddr = nullptr;
    Bytes = nullptr;
    MappingFlags = nullptr;
  }
}

OmptTargetMapping::~OmptTargetMapping() {
  if (Active && Capacity) {
    delete[] HostAddr;
    delete[] DeviceAddr;
    delete[] Bytes;
    delete[] MappingFlags;
  }
}

// This function should only be invoked when capacity is non-zero
void OmptTargetMapping::addMapping(void *HstAddr, void *TgtAddr, size_t Byte,
                                   int64_t ArgType) {
  if (Active) {
    // Calculate mapping_flag using arg_type
    unsigned int flag = 0;
    if (Ctor == TARGET) {
      if (ArgType & OMP_TGT_MAPTYPE_TO) {
        flag |= ompt_target_map_flag_to;
      }
      if (ArgType & OMP_TGT_MAPTYPE_FROM) {
        flag |= ompt_target_map_flag_from;
      }
      if (!flag) {
        flag |= ompt_target_map_flag_alloc;
      }
    } else if (Ctor == TARGET_DATA_BEGIN) {
      if (ArgType & OMP_TGT_MAPTYPE_TO) {
        flag |= ompt_target_map_flag_to;
      } else {
        flag |= ompt_target_map_flag_alloc;
      }
    } else if (Ctor == TARGET_DATA_END) {
      if (ArgType & OMP_TGT_MAPTYPE_FROM) {
        flag |= ompt_target_map_flag_from;
      } else if (ArgType & OMP_TGT_MAPTYPE_DELETE) {
        flag |= ompt_target_map_flag_delete;
      } else {
        flag |= ompt_target_map_flag_release;
      }
    }
    if (ArgType & OMP_TGT_MAPTYPE_IMPLICIT) {
      flag |= ompt_target_map_flag_implicit;
    }
    if (ArgType & OMP_TGT_MAPTYPE_ALWAYS) {
      flag |= ompt_target_map_flag_always;
    }
    if (ArgType & OMP_TGT_MAPTYPE_PRESENT) {
      flag |= ompt_target_map_flag_present;
    }
    if (ArgType & OMP_TGT_MAPTYPE_CLOSE) {
      flag |= ompt_target_map_flag_close;
    }

    HostAddr[Size] = HstAddr;
    DeviceAddr[Size] = TgtAddr;
    Bytes[Size] = Byte;
    MappingFlags[Size] = flag;
    Size += 1;
  }
}

void OmptTargetMapping::invokeCallback() {
  if (Active && Size) {
    libomp_ompt_callback_target_map_emi(Size, HostAddr, DeviceAddr, Bytes,
                                        MappingFlags, CodePtr);
  }
}

OmptDeviceMem::OmptDeviceMem(void *OrigBaseAddr, void *OrigAddr,
                             int OrigDeviceNum, void *DestAddr,
                             int DestDeviceNum, size_t Bytes, void *CodePtr,
                             char *VarName)
    : DeviceMemFlag(0), OrigBaseAddr(OrigBaseAddr), OrigAddr(OrigAddr),
      OrigDeviceNum(OrigDeviceNum), DestAddr(DestAddr),
      DestDeviceNum(DestDeviceNum), Bytes(Bytes), CodePtr(CodePtr),
      VarName(VarName) {
  this->Active =
      OmptTargetEnabled.enabled && OmptTargetEnabled.ompt_callback_device_mem;
}

void OmptDeviceMem::addTargetDataOp(unsigned int Flag) {
  if (Active) {
    DeviceMemFlag |= Flag;
  }
}

void OmptDeviceMem::setDestAddr(void *DestAddr) {
  this->DestAddr = DestAddr;
}

void OmptDeviceMem::invokeCallback() {
  if (Active && DeviceMemFlag) {
    libomp_ompt_callback_device_mem(DeviceMemFlag, OrigBaseAddr, OrigAddr,
                                    OrigDeviceNum, DestAddr, DestDeviceNum,
                                    Bytes, CodePtr, VarName);
    DeviceMemFlag = 0;
  }
}

OmptDeviceMem::~OmptDeviceMem() {
  if (Active && DeviceMemFlag) {
    libomp_ompt_callback_device_mem(DeviceMemFlag, OrigBaseAddr, OrigAddr,
                                    OrigDeviceNum, DestAddr, DestDeviceNum,
                                    Bytes, CodePtr, VarName);
  }
}
