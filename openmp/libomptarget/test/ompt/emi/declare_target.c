// RUN: %libomptarget-compile-generic && %libomptarget-run-generic 2>&1 | %fcheck-generic --check-prefixes CHECK
// REQUIRES: ompt

#include <stdio.h>
#include "callback-emi.h"

#pragma omp declare target
int a = 0;
int b = 0;
#pragma omp end declare target

int main() {
  int *a_addr = 0, *b_addr = 0;
  #pragma omp target map(tofrom: a_addr, b_addr)
  {
    a_addr = &a;
    b_addr = &b;
  }
  printf("%" PRIu64 ":" _TOOL_PREFIX " a: host_addr=%p, target_addr=%p\n", ompt_get_thread_data()->value, &a, a_addr);
  printf("%" PRIu64 ":" _TOOL_PREFIX " b: host_addr=%p, target_addr=%p\n", ompt_get_thread_data()->value, &b, b_addr);

  // CHECK: {{^}}[[MASTER_ID:[0-9]+]]: ompt_event_target_emi_begin
  // CHECK-SAME: task_id=[[INITIAL_TASK_ID:[0-9]+]], target_task_id=0, target_id=[[TARGET_ID_0:[0-9]+]], device_num=[[DEVICE_NUM:[0-9]+]]
  // CHECK-SAME: kind=ompt_target_enter_data, codeptr_ra=(nil)
  
  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_map_emi
  // CHECK-SAME: target_id=[[TARGET_ID_0]], nitems=2, codeptr_ra=(nil)
  // CHECK-NEXT: {{^}}[[MASTER_ID]]: map: host_addr=[[A_HOST_ADDR:0x[0-f]+]], device_addr=[[A_TARGET_ADDR:0x[0-f]+]], bytes=4, mapping_flag=ompt_target_map_flag_to
  // CHECK-NEXT: {{^}}[[MASTER_ID]]: map: host_addr=[[B_HOST_ADDR:0x[0-f]+]], device_addr=[[B_TARGET_ADDR:0x[0-f]+]], bytes=4, mapping_flag=ompt_target_map_flag_to

  // CHECK: {{^}}[[MASTER_ID]]: ompt_event_target_emi_end
  // CHECK-SAME: task_id=[[INITIAL_TASK_ID]], target_task_id=0, target_id=[[TARGET_ID_0]], device_num=[[DEVICE_NUM]]
  // CHECK-SAME: kind=ompt_target_enter_data, codeptr_ra=(nil)

  // CHECK-DAG: {{^}}[[MASTER_ID]]: a: host_addr=[[A_HOST_ADDR]], target_addr=[[A_TARGET_ADDR]]
  // CHECK-DAG: {{^}}[[MASTER_ID]]: b: host_addr=[[B_HOST_ADDR]], target_addr=[[B_TARGET_ADDR]]
  return 0;
}