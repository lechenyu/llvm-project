#include "ompt-target.h"

ompt_target_callbacks_active_t OmptTargetEnabled;

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