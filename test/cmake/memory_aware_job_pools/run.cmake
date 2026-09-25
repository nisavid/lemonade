# Configures the fixture project in this directory once per case, against a
# fake /proc and cgroup v2 tree, and checks the job pools it ends up with.
#
#   cmake -DNINJA=<ninja> -DMODULE=<MemoryAwareJobPools.cmake> \
#         -DWORK_DIR=<scratch dir> -P run.cmake
cmake_minimum_required(VERSION 3.19)

foreach(var NINJA MODULE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run.cmake needs -D${var}=...")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK_DIR}")

# A LIMIT is "<cgroup path>|<memory.max>|<memory.current>|<file cache bytes>".
# "%3B" stands for ";" in cgroup paths, which CMake lists would split.
function(run_case name)
    cmake_parse_arguments(RC ""
        "POOLS;COMPILE;LINK;MEMAVAILABLE_KB;CGROUP;OUTPUT;UNREADABLE"
        "LIMIT;ARGS" ${ARGN})
    set(root "${WORK_DIR}/${name}")
    set(proc "${root}/proc")
    set(cgroup_root "${root}/cgroup")
    file(MAKE_DIRECTORY "${proc}/self" "${cgroup_root}")

    set(meminfo "MemTotal:       134217728 kB\n")
    if(DEFINED RC_MEMAVAILABLE_KB)
        string(APPEND meminfo "MemAvailable:   ${RC_MEMAVAILABLE_KB} kB\n")
    endif()
    file(WRITE "${proc}/meminfo" "${meminfo}")
    set(cgroup "/")
    if(DEFINED RC_CGROUP)
        string(REPLACE "%3B" ";" cgroup "${RC_CGROUP}")
    endif()
    file(WRITE "${proc}/self/cgroup" "0::${cgroup}\n")

    foreach(spec IN LISTS RC_LIMIT)
        string(REPLACE "|" ";" fields "${spec}")
        list(GET fields 0 rel)
        list(GET fields 1 max)
        list(GET fields 2 current)
        list(GET fields 3 cache)
        string(REPLACE "%3B" ";" rel "${rel}")
        set(dir "${cgroup_root}${rel}")
        file(MAKE_DIRECTORY "${dir}")
        file(WRITE "${dir}/memory.max" "${max}\n")
        file(WRITE "${dir}/memory.current" "${current}\n")
        file(WRITE "${dir}/memory.stat"
            "anon 0\nactive_file ${cache}\ninactive_file 0\n")
    endforeach()
    if(DEFINED RC_UNREADABLE)
        file(CHMOD "${cgroup_root}${RC_UNREADABLE}" PERMISSIONS OWNER_WRITE)
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA}"
            -S "${CMAKE_CURRENT_LIST_DIR}" -B "${root}/build"
            "-DMODULE=${MODULE}"
            "-D_MEMORY_AWARE_JOB_POOLS_PROC=${proc}"
            "-D_MEMORY_AWARE_JOB_POOLS_CGROUP_ROOT=${cgroup_root}"
            ${RC_ARGS}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(SEND_ERROR "${name}: configure failed (${rc}):\n${out}${err}")
        return()
    endif()

    file(READ "${root}/build/pools.txt" report)
    set(expected
        "pools=${RC_POOLS}\ncompile=${RC_COMPILE}\nlink=${RC_LINK}\n")
    if(NOT report STREQUAL expected)
        message(SEND_ERROR "${name}: expected\n${expected}got\n${report}")
    endif()
    if(DEFINED RC_OUTPUT)
        string(FIND "${out}" "${RC_OUTPUT}" at)
        if(at EQUAL -1)
            message(SEND_ERROR
                "${name}: output lacks \"${RC_OUTPUT}\":\n${out}")
        endif()
    endif()
endfunction()

set(ours "memory_aware_compile=11,memory_aware_link=3")
set(pooled COMPILE memory_aware_compile LINK memory_aware_link)

# 16 GiB available: (16384 - 2048) / 1024 = 14 jobs, link 14 / 4 = 3.
run_case(host_memory MEMAVAILABLE_KB 16777216
    POOLS "${ours}" ${pooled}
    OUTPUT "compile=11 (available 16.0 GiB, reserve 2 GiB, 1 GiB/job)")
run_case(project_budget MEMAVAILABLE_KB 16777216
    "ARGS" "-DFIXTURE_BEFORE_CODE=set(MEMORY_AWARE_JOB_MIB 1536)"
    POOLS "memory_aware_compile=7,memory_aware_link=2" ${pooled}
    OUTPUT "1.5 GiB/job")
run_case(cache_budget MEMAVAILABLE_KB 16777216
    ARGS -DMEMORY_AWARE_JOB_MIB=1536
    POOLS "memory_aware_compile=7,memory_aware_link=2" ${pooled}
    OUTPUT "1.5 GiB/job")
# 8 GiB limit, 2 GiB charged of which 1.5 GiB is file cache: 7.5 GiB headroom,
# 5 jobs. Counting the cache as used would leave 6 GiB and 4 jobs.
run_case(cgroup_headroom MEMAVAILABLE_KB 67108864 CGROUP /a.slice/b.scope
    LIMIT "/a.slice|8589934592|2147483648|1610612736"
    POOLS "memory_aware_compile=4,memory_aware_link=1" ${pooled}
    OUTPUT "in cgroup")
# The tightest of MemAvailable and every level's headroom wins, wherever it is.
run_case(tightest_child MEMAVAILABLE_KB 67108864 CGROUP /a/b
    LIMIT "/a|8589934592|0|0" "/a/b|4294967296|0|0"
    POOLS "memory_aware_compile=1,memory_aware_link=1" ${pooled})
run_case(tightest_ancestor MEMAVAILABLE_KB 67108864 CGROUP /a/b
    LIMIT "/a|4294967296|0|0" "/a/b|8589934592|0|0"
    POOLS "memory_aware_compile=1,memory_aware_link=1" ${pooled})
run_case(tightest_host MEMAVAILABLE_KB 4194304 CGROUP /a
    LIMIT "/a|8589934592|0|0"
    POOLS "memory_aware_compile=1,memory_aware_link=1" ${pooled})
run_case(unlimited_cgroup MEMAVAILABLE_KB 16777216 CGROUP /a
    LIMIT "/a|max|0|0"
    POOLS "${ours}" ${pooled})
run_case(semicolon_path MEMAVAILABLE_KB 67108864 CGROUP /x%3By
    LIMIT "/x%3By|3221225472|0|0"
    POOLS "memory_aware_jobs=1"
    COMPILE memory_aware_jobs LINK memory_aware_jobs
    OUTPUT "compile+link=1 (one pool")
run_case(no_memavailable
    POOLS "" COMPILE "" LINK ""
    OUTPUT "no MemAvailable")
run_case(forced_depths
    ARGS -DMEMORY_AWARE_COMPILE_JOBS=3 -DMEMORY_AWARE_LINK_JOBS=2
    POOLS "memory_aware_compile=3,memory_aware_link=2" ${pooled})
run_case(disabled MEMAVAILABLE_KB 16777216
    ARGS -DMEMORY_AWARE_JOB_POOLS=OFF
    POOLS "" COMPILE "" LINK "")
run_case(builder_pools MEMAVAILABLE_KB 16777216
    ARGS -DCMAKE_JOB_POOLS=custom=2
    POOLS "" COMPILE "" LINK "")
run_case(project_pools_after MEMAVAILABLE_KB 16777216
    "ARGS" "-DFIXTURE_AFTER_CODE=set(CMAKE_JOB_POOLS custom=2)"
    POOLS "${ours},custom=2" ${pooled})
run_case(project_replaces_pools MEMAVAILABLE_KB 16777216
    "ARGS" "-DFIXTURE_AFTER_CODE=set_property(GLOBAL PROPERTY JOB_POOLS other=1)"
    POOLS "other=1,${ours}" ${pooled})

# An unreadable limit is skipped instead of failing the configure. Root, and
# Windows, where the chmod only clears the read-only attribute, can still read
# a write-only file, so the case runs only where the probe below cannot.
set(probe "${WORK_DIR}/write-only-probe")
file(WRITE "${probe}" "")
file(CHMOD "${probe}" PERMISSIONS OWNER_WRITE)
set(readable EXISTS)
if(NOT CMAKE_VERSION VERSION_LESS 3.29)
    set(readable IS_READABLE)
endif()
if(${readable} "${probe}")
    message(STATUS "unreadable_limit: skipped; this process can read a "
        "write-only file")
else()
    run_case(unreadable_limit MEMAVAILABLE_KB 16777216 CGROUP /a
        LIMIT "/a|4294967296|0|0" UNREADABLE /a/memory.max
        POOLS "${ours}" ${pooled})
endif()
