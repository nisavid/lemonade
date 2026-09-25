# The fixture configures with Ninja, which only a Ninja build is sure to have,
# and uses cmake_language(DEFER) and file(CHMOD) from CMake 3.19.
if(BUILD_TESTING AND CMAKE_GENERATOR MATCHES "^Ninja"
   AND NOT CMAKE_VERSION VERSION_LESS 3.19)
    add_custom_target(memory_aware_job_pools_tests
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt
            ${CMAKE_CURRENT_LIST_DIR}/run.cmake)
    add_cpp_ci_test(MemoryAwareJobPools CI ON
        COMMAND ${CMAKE_COMMAND}
            -DNINJA=${CMAKE_MAKE_PROGRAM}
            -DMODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemoryAwareJobPools.cmake
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_memory_aware_job_pools
            -P ${CMAKE_CURRENT_LIST_DIR}/run.cmake
        DEPENDS memory_aware_job_pools_tests)
endif()
