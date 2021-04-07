// RUN: %libomptarget-compile-and-run-x86_64-pc-linux-gnu | FileCheck %s
// REQUIRES: ompt

#include "callback-target.h"
#include <stdio.h>

int main() {
  #pragma omp target
  {
      //print_ids(0);
      printf("hello\n");
  }
  print_current_address();
  // CHECK-NOT: {{^}}0: Could not register callback

  // CHECK: 0: NULL_POINTER=[[NULL:.*$]]

  // CHECK: {{^}}[[MASTER_ID:[0-9]+]]: ompt_event_thread_begin: thread_type=ompt_thread_initial=1, thread_id=[[MASTER_ID]]
  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_initial_task_begin
  // CHECK-SAME: parallel_id=[[PARALLEL_ID:[0-9]+]], task_id=[[INITIAL_TASK_ID:[0-9]+]], actual_parallelism=1, index=1, flags=1 
  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_emi_begin
  // CHECK-SAME: target_id=[[TARGET_ID:[0-9]+]], target_task_id=0, task_id=[[INITIAL_TASK_ID]], device_num=[[DEVICE_NUM:[0-9]+]]
  // CHECK-SAME: kind=ompt_target, codeptr_ra=[[RETURN_ADDRESS:0x[0-f]+]]{{[0-f][0-f]}}

  // COM: {{^}}[[MASTER_ID]]: task level 0
  // COM: parallel_id=[[PARALLEL_ID]], task_id=[[INITIAL_TASK_ID]]

  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_emi_end
  // CHECK-SAME: target_id=[[TARGET_ID]], target_task_id=0, task_id=[[INITIAL_TASK_ID]], device_num=[[DEVICE_NUM]]
  // CHECK-SAME: kind=ompt_target, codeptr_ra=[[RETURN_ADDRESS]]
  // CHECK: {{^}}[[MASTER_ID]]: current_address={{.*}}[[RETURN_ADDRESS]]

  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_initial_task_end
  // CHECK-SAME: parallel_id=[[PARALLEL_ID]], task_id=[[INITIAL_TASK_ID]], actual_parallelism=0, index=1


  return 0;
}