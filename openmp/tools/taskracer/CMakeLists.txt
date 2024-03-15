if(LIBOMP_OMPT_SUPPORT)
  include_directories(${LIBOMP_INCLUDE_DIR})

  add_library(taskracer SHARED ompt-tsan.cpp)
  add_library(taskracer_static STATIC ompt-tsan.cpp)

  target_link_libraries(taskracer PRIVATE dl)
  target_link_libraries(taskracer_static PRIVATE dl)
  target_include_directories(taskracer PRIVATE "/home/fjin/playground/json/include")
  target_include_directories(taskracer_static PRIVATE "/home/fjin/playground/json/include")
  
  install(TARGETS taskracer taskracer_static
    LIBRARY DESTINATION ${OPENMP_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${OPENMP_INSTALL_LIBDIR})

endif()