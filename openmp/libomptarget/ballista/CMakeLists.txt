libomptarget_say("Ballista enabled")
set(CLANG_TOOL $<TARGET_FILE:clang>)
set(BALLISTA_GLOBAL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/ballista_global.c")
set(BALLISTA_GLOBAL_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ballista_global.o")

add_custom_target(ballista-obj ALL DEPENDS ${BALLISTA_GLOBAL_OBJ})
add_custom_command(OUTPUT ${BALLISTA_GLOBAL_OBJ}
  COMMAND ${CLANG_TOOL}
  -c -fopenmp -fopenmp-targets=nvptx64-nvidia-cuda 
  -Xopenmp-target -march=sm_70
  --libomptarget-nvptx-bc-path=${LIBOMPTARGET_LIBRARY_DIR}/libomptarget-nvptx-sm_70.bc
  ${BALLISTA_GLOBAL_SRC} -o ${BALLISTA_GLOBAL_OBJ}
  COMMAND ${CMAKE_COMMAND} -E copy 
  ${BALLISTA_GLOBAL_OBJ}
  ${LLVM_BINARY_DIR}/lib/
  DEPENDS ${BALLISTA_GLOBAL_SRC}
  COMMENT "Building the object file needed by Ballista"
  VERBATIM
)
add_dependencies(ballista-obj omptarget.devicertl.nvptx)
install(FILES ${BALLISTA_GLOBAL_OBJ} DESTINATION "${OPENMP_INSTALL_LIBDIR}")