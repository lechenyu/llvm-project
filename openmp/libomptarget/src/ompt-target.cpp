#include "ompt-target.h"
#include "omptarget.h"

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

OmptTargetMapping::OmptTargetMapping(ConstructorType Ctor, int32_t Capacity,
                                     void *CodePtr)
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
  if (Active) {
    libomp_ompt_callback_target_map_emi(Size, HostAddr, DeviceAddr, Bytes,
                                        MappingFlags, CodePtr);
  }
}
