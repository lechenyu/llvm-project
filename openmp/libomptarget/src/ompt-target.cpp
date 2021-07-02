#include "ompt-target.h"
#include "omptarget.h"

ompt_target_callbacks_active_t ompt_target_enabled;

int host_device_num;

OmptTargetDataOp::OmptTargetDataOp(ompt_target_data_op_t optype, void *src_addr, int src_device_num, void *dest_addr,
                                   int dest_device_num, size_t bytes, void *codeptr) :
        optype(optype), src_addr(src_addr), src_device_num(src_device_num), dest_addr(dest_addr),
        dest_device_num(dest_device_num), bytes(bytes), codeptr(codeptr) {
  this->active = ompt_target_enabled.enabled && ompt_target_enabled.ompt_callback_target_data_op_emi;
  if (active) {
    libomp_ompt_callback_target_data_op_emi(ompt_scope_begin, optype, src_addr, src_device_num, dest_addr,
                                            dest_device_num, bytes, codeptr);
  }
}

OmptTargetDataOp::~OmptTargetDataOp() {
  if (active) {
    libomp_ompt_callback_target_data_op_emi(ompt_scope_end, optype, src_addr, src_device_num, dest_addr,
                                            dest_device_num, bytes, codeptr);
  }
}

void OmptTargetDataOp::set_dest_addr(void *dest_addr) {
  this->dest_addr = dest_addr;
}

OmptTargetMapping::OmptTargetMapping(int capacity, void *codeptr) : capacity(capacity), size(0), codeptr(codeptr) {
  this->active = ompt_target_enabled.enabled && ompt_target_enabled.ompt_callback_target_map_emi;
  if (active && capacity) {
    host_addr = new void *[capacity];
    device_addr = new void *[capacity];
    bytes = new size_t[capacity];
    mapping_flags = new unsigned int[capacity];
  } else {
    host_addr = nullptr;
    device_addr = nullptr;
    bytes = nullptr;
    mapping_flags = nullptr;
  }
}

OmptTargetMapping::~OmptTargetMapping() {
  if (active && capacity) {
    delete[] host_addr;
    delete[] device_addr;
    delete[] bytes;
    delete[] mapping_flags;
  }
}

// This function should only be invoked when capacity is non-zero
void OmptTargetMapping::add_mapping(void *hst_addr, void *tgt_addr, size_t byte, int64_t arg_type, ConstructorType con_type) {
  if (active) {
    // Calculate mapping_flag using arg_type
    unsigned int flag = 0;
    if (con_type == TARGET) {
      if (arg_type & OMP_TGT_MAPTYPE_TO) {
        flag |= ompt_target_map_flag_to;
      }
      if (arg_type & OMP_TGT_MAPTYPE_FROM) {
        flag |= ompt_target_map_flag_from;
      }
      if (!flag) {
        flag |= ompt_target_map_flag_alloc;
      }
    } else if (con_type == TARGET_DATA_BEGIN) {
      if (arg_type & OMP_TGT_MAPTYPE_TO) {
        flag |= ompt_target_map_flag_to;
      } else {
        flag |= ompt_target_map_flag_alloc;
      }
    } else if (con_type == TARGET_DATA_END) {
      if (arg_type & OMP_TGT_MAPTYPE_FROM) {
        flag |= ompt_target_map_flag_from;
      } else if (arg_type & OMP_TGT_MAPTYPE_DELETE) {
        flag |= ompt_target_map_flag_delete;
      } else {
        flag |= ompt_target_map_flag_release;
      }
    }
    if (arg_type & OMP_TGT_MAPTYPE_IMPLICIT) {
      flag |= ompt_target_map_flag_implicit;
    }
    if (arg_type & OMP_TGT_MAPTYPE_ALWAYS) {
      flag |= ompt_target_map_flag_always;
    }
    if (arg_type & OMP_TGT_MAPTYPE_PRESENT) {
      flag |= ompt_target_map_flag_present;
    }
    if (arg_type & OMP_TGT_MAPTYPE_CLOSE) {
      flag |= ompt_target_map_flag_close;
    }

    host_addr[size] = hst_addr;
    device_addr[size] = tgt_addr;
    bytes[size] = byte;
    mapping_flags[size] = flag;
    size += 1;
  }
}

void OmptTargetMapping::invoke_callback() {
  if (active) {
    libomp_ompt_callback_target_map_emi(size, host_addr, device_addr, bytes, mapping_flags, codeptr);
  }
}

OmptTarget::OmptTarget(ompt_target_t kind, int device_num, void *codeptr) : kind(kind), device_num(device_num), codeptr(codeptr) {
  this->active = ompt_target_enabled.enabled && ompt_target_enabled.ompt_callback_target_emi;
  if (active) {
    libomp_ompt_callback_target_emi(kind, ompt_scope_begin, device_num, codeptr);
  }
}

OmptTarget::~OmptTarget() {
  if (active) {
    libomp_ompt_callback_target_emi(kind, ompt_scope_end, device_num, codeptr);
  }
}

OmptTargetSubmit::OmptTargetSubmit(unsigned int requested_num_teams) : requested_num_teams(requested_num_teams) {
  this->active = ompt_target_enabled.enabled && ompt_target_enabled.ompt_callback_target_submit_emi;
  if (active) {
    libomp_ompt_callback_target_submit_emi(ompt_scope_begin, requested_num_teams);
  }
}

OmptTargetSubmit::~OmptTargetSubmit() {
  if (active) {
    libomp_ompt_callback_target_submit_emi(ompt_scope_end, requested_num_teams);
  }
}

OmptDeviceMem::OmptDeviceMem(void *orig_base_addr, void *orig_addr, int orig_device_num, void *dest_addr,
                             int dest_device_num, size_t bytes, void *codeptr) :
        device_mem_flag(0), orig_base_addr(orig_base_addr), orig_addr(orig_addr), orig_device_num(orig_device_num),
        dest_addr(dest_addr), dest_device_num(dest_device_num), bytes(bytes), codeptr(codeptr) {
  this->active = ompt_target_enabled.enabled && ompt_target_enabled.ompt_callback_device_mem;
}

void OmptDeviceMem::add_target_data_op(unsigned int flag) {
  if (active) {
    device_mem_flag |= flag;
  }
}

OmptDeviceMem::~OmptDeviceMem() {
  if (active && device_mem_flag) {
    libomp_ompt_callback_device_mem(device_mem_flag, orig_base_addr, orig_addr, orig_device_num, dest_addr, dest_device_num, bytes, codeptr);
  }
}