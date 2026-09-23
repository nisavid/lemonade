cmake_minimum_required(VERSION 3.19)
project(MemoryAwareJobPoolsFixture NONE)

include("${MODULE}")
if(DEFINED FIXTURE_AFTER_CODE)
    cmake_language(EVAL CODE "${FIXTURE_AFTER_CODE}")
endif()

# Deferred after the module's own reconcile call, so this records final pools.
function(fixture_report)
    get_property(pools GLOBAL PROPERTY JOB_POOLS)
    string(REPLACE ";" "," pools "${pools}")
    file(WRITE "${CMAKE_BINARY_DIR}/pools.txt"
        "pools=${pools}\n"
        "compile=${CMAKE_JOB_POOL_COMPILE}\n"
        "link=${CMAKE_JOB_POOL_LINK}\n")
endfunction()
cmake_language(DEFER CALL fixture_report)
