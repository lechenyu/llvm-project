// RUN: %libomptarget-compile-generic -DNOWAIT=0 && %libomptarget-run-generic 2>&1 | %fcheck-generic --check-prefixes CHECK,SYNC
// RUN: %libomptarget-compile-generic -DNOWAIT=1 && %libomptarget-run-generic 2>&1 | %fcheck-generic --check-prefixes CHECK,ASYNC
// REQUIRES: ompt

#include "callback-emi.h"
#include "omp.h"

#define N 2

int main() {
  printf("host_num = %" PRIu32 "\n", omp_get_initial_device());
  int a[N] = {0};

  // target 1 (target enter data)
  #pragma omp target data map(tofrom: a[0:N])
  {
    // target 2 (target update)
    #pragma omp target update to(a[0:N]) NOWAIT_CLAUSE
    print_fuzzy_address(1);
#if NOWAIT
    #pragma omp taskwait
#endif

    // target 3 (target update)
    #pragma omp target update from(a[0:N]) NOWAIT_CLAUSE
    print_fuzzy_address(2);
#if NOWAIT
    #pragma omp taskwait
#endif

  // target 4 (target exit data)
  }

  // CHECK-NOT: {{^}}0: Could not register callback
  // CHECK: 0: NULL_POINTER=[[NULL:.*$]]
  
  // CHECK: {{^}}[[MASTER_ID:[0-9]+]]: ompt_event_thread_begin: thread_type=ompt_thread_initial=1, thread_id=[[MASTER_ID]]
  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_initial_task_begin
  // CHECK-SAME: parallel_id=[[PARALLEL_ID:[0-9]+]], task_id=[[INITIAL_TASK_ID:[0-9]+]], actual_parallelism=1, index=1, flags=1
  // CHECK: host_num = [[HOST_NUM:[0-9]+]]


  /** target 1 (target enter data) **/

  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_begin
  // CHECK-SAME: target_task_id=0, target_id=[[TARGET_ID_1:[0-9]+]], host_op_id=[[HOST_OP_ID_1:[0-9]+]], optype=ompt_target_data_alloc, src_addr=[[SRC_ADDR:0x[0-f]+]]
  // CHECK-SAME: src_device_num=[[HOST_NUM]], dest_addr=(nil), dest_device_num=[[DEVICE_NUM:[0-9]+]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_1:0x[0-f]+]]
  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_end
  // CHECK-SAME: target_task_id=0, target_id=[[TARGET_ID_1]], host_op_id=[[HOST_OP_ID_1]], optype=ompt_target_data_alloc, src_addr=[[SRC_ADDR]]
  // CHECK-SAME: src_device_num=[[HOST_NUM]], dest_addr=[[DEST_ADDR:0x[0-f]+]], dest_device_num=[[DEVICE_NUM]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_1]]


  /** target 2 (target update) **/

  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_emi_begin
  // SYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=0, target_id=[[TARGET_ID_2:[0-9]+]], device_num=[[DEVICE_NUM]]
  // SYNC-SAME: kind=ompt_target_update, codeptr_ra=[[TARGET_RETURN_ADDRESS_2:0x[0-f]+]]{{[0-f][0-f]}}
  
  // ASYNC: {{^}}[[MASTER_ID]]: ompt_event_task_create
  // ASYNC-SAME: parent_task_id=[[INITIAL_TASK_ID]], parent_task_frame.exit=(nil), parent_task_frame.reenter=0x{{[0-f]+}}
  // ASYNC-SAME: new_task_id=[[TARGET_TASK_ID_2:[0-9]+]], codeptr_ra=0x{{[0-f]+}}
  // ASYNC-SAME: task_type=ompt_task_explicit|ompt_task_target
  // ASYNC: {{^}}[[THREAD_ID_2:[0-9]+]]: ompt_event_target_emi_begin
  // ASYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=[[TARGET_TASK_ID_2]], target_id=[[TARGET_ID_2:[0-9]+]], device_num=[[DEVICE_NUM]]
  // ASYNC-SAME: kind=ompt_target_update_nowait, codeptr_ra=(nil)

  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_begin
  // SYNC-SAME: target_task_id=0, target_id=[[TARGET_ID_2]], host_op_id=[[HOST_OP_ID_2:[0-9]+]], optype=ompt_target_data_transfer_to_device, src_addr=[[SRC_ADDR]]
  // SYNC-SAME: src_device_num=[[HOST_NUM]], dest_addr=[[DEST_ADDR]], dest_device_num=[[DEVICE_NUM]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_2]]{{[0-f][0-f]}}
  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_end
  // SYNC-SAME: target_task_id=0, target_id=[[TARGET_ID_2]], host_op_id=[[HOST_OP_ID_2]], optype=ompt_target_data_transfer_to_device, src_addr=[[SRC_ADDR]]
  // SYNC-SAME: src_device_num=[[HOST_NUM]], dest_addr=[[DEST_ADDR]], dest_device_num=[[DEVICE_NUM]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_2]]{{[0-f][0-f]}}
  
  // ASYNC: {{^}}[[THREAD_ID_2]]: ompt_event_target_data_op_emi_begin
  // ASYNC-SAME: target_task_id=[[TARGET_TASK_ID_2]], target_id=[[TARGET_ID_2]], host_op_id=[[HOST_OP_ID_2:[0-9]+]], optype=ompt_target_data_transfer_to_device, src_addr=[[SRC_ADDR]]
  // ASYNC-SAME: src_device_num=[[HOST_NUM]], dest_addr=[[DEST_ADDR]], dest_device_num=[[DEVICE_NUM]], bytes=8, codeptr_ra=(nil)
  // ASYNC: {{^}}[[THREAD_ID_2]]: ompt_event_target_data_op_emi_end
  // ASYNC-SAME: target_task_id=[[TARGET_TASK_ID_2]], target_id=[[TARGET_ID_2]], host_op_id=[[HOST_OP_ID_2]], optype=ompt_target_data_transfer_to_device, src_addr=[[SRC_ADDR]]
  // ASYNC-SAME: src_device_num=[[HOST_NUM]], dest_addr=[[DEST_ADDR]], dest_device_num=[[DEVICE_NUM]], bytes=8, codeptr_ra=(nil)

  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_emi_end
  // SYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=0, target_id=[[TARGET_ID_2]], device_num=[[DEVICE_NUM]]
  // SYNC-SAME: kind=ompt_target_update, codeptr_ra=[[TARGET_RETURN_ADDRESS_2]]{{[0-f][0-f]}}
  // SYNC: {{^}}[[MASTER_ID]]: fuzzy_address={{.*}}[[TARGET_RETURN_ADDRESS_2]]

  // ASYNC: {{^}}[[THREAD_ID_2]]: ompt_event_target_emi_end
  // ASYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=[[TARGET_TASK_ID_2]], target_id=[[TARGET_ID_2]], device_num=[[DEVICE_NUM]]
  // ASYNC-SAME: kind=ompt_target_update_nowait, codeptr_ra=(nil)

  
  /** target 3 (target update) **/
  
  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_emi_begin
  // SYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=0, target_id=[[TARGET_ID_3:[0-9]+]], device_num=[[DEVICE_NUM]]
  // SYNC-SAME: kind=ompt_target_update, codeptr_ra=[[TARGET_RETURN_ADDRESS_3:0x[0-f]+]]{{[0-f][0-f]}}
  
  // ASYNC: {{^}}[[MASTER_ID]]: ompt_event_task_create
  // ASYNC-SAME: parent_task_id=[[INITIAL_TASK_ID]], parent_task_frame.exit=(nil), parent_task_frame.reenter=0x{{[0-f]+}}
  // ASYNC-SAME: new_task_id=[[TARGET_TASK_ID_3:[0-9]+]], codeptr_ra=0x{{[0-f]+}}
  // ASYNC-SAME: task_type=ompt_task_explicit|ompt_task_target
  // ASYNC: {{^}}[[THREAD_ID_3:[0-9]+]]: ompt_event_target_emi_begin
  // ASYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=[[TARGET_TASK_ID_3]], target_id=[[TARGET_ID_3:[0-9]+]], device_num=[[DEVICE_NUM]]
  // ASYNC-SAME: kind=ompt_target_update_nowait, codeptr_ra=(nil)

  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_begin
  // SYNC-SAME: target_task_id=0, target_id=[[TARGET_ID_3]], host_op_id=[[HOST_OP_ID_3:[0-9]+]], optype=ompt_target_data_transfer_from_device, src_addr=[[DEST_ADDR]]
  // SYNC-SAME: src_device_num=[[DEVICE_NUM]], dest_addr=[[SRC_ADDR]], dest_device_num=[[HOST_NUM]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_3]]{{[0-f][0-f]}}
  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_data_op_emi_end
  // SYNC-SAME: target_task_id=0, target_id=[[TARGET_ID_3]], host_op_id=[[HOST_OP_ID_3]], optype=ompt_target_data_transfer_from_device, src_addr=[[DEST_ADDR]]
  // SYNC-SAME: src_device_num=[[DEVICE_NUM]], dest_addr=[[SRC_ADDR]], dest_device_num=[[HOST_NUM]], bytes=8, codeptr_ra=[[TARGET_RETURN_ADDRESS_3]]{{[0-f][0-f]}}
  
  // ASYNC: {{^}}[[THREAD_ID_3]]: ompt_event_target_data_op_emi_begin
  // ASYNC-SAME: target_task_id=[[TARGET_TASK_ID_3]], target_id=[[TARGET_ID_3]], host_op_id=[[HOST_OP_ID_3:[0-9]+]], optype=ompt_target_data_transfer_from_device, src_addr=[[DEST_ADDR]]
  // ASYNC-SAME: src_device_num=[[DEVICE_NUM]], dest_addr=[[SRC_ADDR]], dest_device_num=[[HOST_NUM]], bytes=8, codeptr_ra=(nil)
  // ASYNC: {{^}}[[THREAD_ID_3]]: ompt_event_target_data_op_emi_end
  // ASYNC-SAME: target_task_id=[[TARGET_TASK_ID_3]], target_id=[[TARGET_ID_3]], host_op_id=[[HOST_OP_ID_3]], optype=ompt_target_data_transfer_from_device, src_addr=[[DEST_ADDR]]
  // ASYNC-SAME: src_device_num=[[DEVICE_NUM]], dest_addr=[[SRC_ADDR]], dest_device_num=[[HOST_NUM]], bytes=8, codeptr_ra=(nil)

  // SYNC: {{^}}[[MASTER_ID]]: ompt_event_target_emi_end
  // SYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=0, target_id=[[TARGET_ID_3]], device_num=[[DEVICE_NUM]]
  // SYNC-SAME: kind=ompt_target_update, codeptr_ra=[[TARGET_RETURN_ADDRESS_3]]{{[0-f][0-f]}}
  // SYNC: {{^}}[[MASTER_ID]]: fuzzy_address={{.*}}[[TARGET_RETURN_ADDRESS_3]]

  // ASYNC: {{^}}[[THREAD_ID_3]]: ompt_event_target_emi_end
  // ASYNC-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=[[TARGET_TASK_ID_3]], target_id=[[TARGET_ID_3]], device_num=[[DEVICE_NUM]]
  // ASYNC-SAME: kind=ompt_target_update_nowait, codeptr_ra=(nil)
  return 0;
}