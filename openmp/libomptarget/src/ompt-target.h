#ifndef LIBOMPTARGET_OMPT_TARGET_H
#define LIBOMPTARGET_OMPT_TARGET_H

#if OMPT_SUPPORT
#include "omp-tools.h"

#define _OMP_EXTERN extern "C"

#define OMPT_WEAK_ATTRIBUTE __attribute__((weak))

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)

#define OMPT_ARG(...) , __VA_ARGS__

#define OMPT_FIRST_ARG(...) __VA_ARGS__

// The following structs are used to pass target-related OMPT callbacks to libomptarget. The structs' definitions
// should be in sync with the definitions in libomptarget/src/ompt_internal.h

/* Bitmap to mark OpenMP 5.1 target events as registered*/
typedef struct ompt_target_callbacks_active_s {
    unsigned int enabled : 1;
#define ompt_event_macro(event, callback, eventid) unsigned int event : 1;

    FOREACH_OMPT_51_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_active_t;

extern ompt_target_callbacks_active_t ompt_target_enabled;

extern int host_device_num;

_OMP_EXTERN OMPT_WEAK_ATTRIBUTE bool libomp_start_tool(
        ompt_target_callbacks_active_t *libomptarget_ompt_enabled);

_OMP_EXTERN OMPT_WEAK_ATTRIBUTE void libomp_ompt_callback_target_emi(ompt_target_t kind,
                                                                     ompt_scope_endpoint_t endpoint,
                                                                     int device_num, void* codeptr);

_OMP_EXTERN OMPT_WEAK_ATTRIBUTE void libomp_ompt_callback_target_data_op_emi(ompt_scope_endpoint_t endpoint,
                                                                             ompt_target_data_op_t optype,
                                                                             void *src_addr,
                                                                             int src_device_num,
                                                                             void *dest_addr,
                                                                             int dest_device_num,
                                                                             size_t bytes,
                                                                             void *codeptr);

_OMP_EXTERN OMPT_WEAK_ATTRIBUTE void libomp_ompt_callback_target_map_emi(unsigned int nitems,
                                                                         void **host_addr,
                                                                         void **device_addr,
                                                                         size_t *bytes,
                                                                         unsigned int *mapping_flags,
                                                                         void *codeptr);

_OMP_EXTERN OMPT_WEAK_ATTRIBUTE void libomp_ompt_callback_target_submit_emi(ompt_scope_endpoint_t endpoint,
                                                                            unsigned int requested_num_teams);
//TODO (lechenyu): Will move following classes into openmp runtime
class OmptTargetDataOp {
private:
    ompt_target_data_op_t optype;
    void *src_addr;
    int src_device_num;
    void *dest_addr;
    int dest_device_num;
    size_t bytes;
    void * codeptr;
    bool active;
public:
    OmptTargetDataOp(ompt_target_data_op_t optype, void *src_addr, int src_device_num, void *dest_addr, int dest_device_num, size_t bytes, void *codeptr);
    ~OmptTargetDataOp();
    void set_dest_addr(void *dest_addr);
};

class OmptTargetMapping {
private:
    int32_t capacity;
    int32_t size;
    void **host_addr;
    void **device_addr;
    size_t *bytes;
    unsigned int *mapping_flags;
    void *codeptr;
    bool active;
public:
    typedef enum ConstructorType {
        TARGET              = 1,
        TARGET_DATA_BEGIN   = 2,
        TARGET_DATA_END     = 3
    } ConstructorType;
    OmptTargetMapping(int capacity, void *codeptr);
    ~OmptTargetMapping();
    void add_mapping(void *hst_addr, void *tgt_addr, size_t byte, int64_t arg_type, ConstructorType con_type);
    void invoke_callback();
};

class OmptTarget {
private:
    ompt_target_t kind;
    int device_num;
    void *codeptr;
    bool active;
public:
    OmptTarget(ompt_target_t kind, int device_num, void *codeptr);
    ~OmptTarget();
};

class OmptTargetSubmit {
private:
    unsigned int requested_num_teams;
    bool active;
public:
    OmptTargetSubmit(unsigned int requested_num_teams);
    ~OmptTargetSubmit();
};
#else

#define OMPT_ARG(...)
#define OMPT_FIRST_ARG(...)

#endif // OMPT_SUPPORT

#endif // LIBOMPTARGET_OMPT_TARGET_H
