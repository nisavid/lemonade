# SPDX-License-Identifier: Apache-2.0
#
# MemoryAwareJobPools
# -------------------
#
# Sizes Ninja compile and link job pools from the memory that is available when
# CMake configures. Ninja's default parallelism follows the CPU count, so on a
# host with many cores and little free memory a full build can exhaust memory.
# The depths are a configure-time snapshot; re-run CMake to refresh them.
#
# Include it after project():
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemoryAwareJobPools.cmake)
#
# or inject it into an unmodified project (CMake >= 3.24):
#
#   cmake -G Ninja -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/abs/path/MemoryAwareJobPools.cmake ...
#
# It acts only for Ninja generators and defers to pools the builder defines: it
# does nothing when CMAKE_JOB_POOLS or the JOB_POOLS global property is set, and
# leaves CMAKE_JOB_POOL_COMPILE or CMAKE_JOB_POOL_LINK alone when either is set.
# Those checks see only what is set before it runs. With CMake >= 3.19 it also
# keeps pools the project defines later: at the end of the top-level directory
# it adds the CMAKE_JOB_POOLS entries that its own JOB_POOLS entries would
# hide, and restores its entries if the project replaced JOB_POOLS. Older CMake
# skips that step, so there include it after the project defines its pools.
#
# Available memory is MemAvailable from /proc/meminfo, lowered to the tightest
# cgroup v2 headroom of this process's cgroup and its ancestors: memory.max
# minus memory.current, not counting the file cache on the LRU lists
# (active_file + inactive_file in memory.stat). The kernel reclaims that cache
# before it lets the cgroup exceed memory.max, and MemAvailable counts it as
# available. Without MemAvailable it is CMake's AVAILABLE_PHYSICAL_MEMORY.
#
#   jobs    = max(1, (available - reserve) / job budget)
#   link    = max(1, jobs / 4)
#   compile = max(1, jobs - link)
#
# A link depth set through MEMORY_AWARE_LINK_JOBS is the link in that formula.
# When jobs is 1 and neither depth is set, compile and link share one pool of
# depth 1, since two pools of depth 1 would allow two jobs.
#
# Cache settings:
#
#   MEMORY_AWARE_JOB_POOLS     ON (default) to size pools, OFF to do nothing
#   MEMORY_AWARE_COMPILE_JOBS  0 (default) sizes from memory; N > 0 uses N
#   MEMORY_AWARE_LINK_JOBS     0 (default) sizes from memory; N > 0 uses N
#
# Budgets a project may set before including this file:
#
#   MEMORY_AWARE_JOB_MIB       memory per job in MiB (default 1024)
#   MEMORY_AWARE_RESERVE_MIB   memory held back in MiB (default 2048)

include_guard(GLOBAL)

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
    return()
endif()

option(MEMORY_AWARE_JOB_POOLS
    "Size Ninja compile and link job pools from available memory" ON)
set(MEMORY_AWARE_COMPILE_JOBS 0 CACHE STRING
    "Ninja compile job pool depth (0 = size from available memory)")
set(MEMORY_AWARE_LINK_JOBS 0 CACHE STRING
    "Ninja link job pool depth (0 = size from available memory)")

function(_memory_aware_job_pools_gib out mib decimals)
    math(EXPR whole "${mib} / 1024")
    math(EXPR rem "${mib} % 1024")
    if(rem EQUAL 0 AND NOT decimals)
        set(${out} "${whole}" PARENT_SCOPE)
        return()
    endif()
    math(EXPR tenths "(${mib} * 10 + 512) / 1024")
    math(EXPR whole "${tenths} / 10")
    math(EXPR frac "${tenths} % 10")
    set(${out} "${whole}.${frac}" PARENT_SCOPE)
endfunction()

function(_memory_aware_job_pools_available out_mib out_cgroup_limited)
    set(avail "")
    set(cgroup_limited FALSE)

    if(EXISTS "/proc/meminfo")
        file(STRINGS "/proc/meminfo" line REGEX "^MemAvailable:")
        if(line MATCHES "^MemAvailable:[ \t]*([0-9]+) kB")
            math(EXPR avail "${CMAKE_MATCH_1} / 1024")
        endif()
    endif()
    if(avail STREQUAL "")
        cmake_host_system_information(RESULT avail
            QUERY AVAILABLE_PHYSICAL_MEMORY)
    endif()
    if(NOT avail MATCHES "^[0-9]+$" OR avail EQUAL 0)
        set(${out_mib} "" PARENT_SCOPE)
        return()
    endif()

    set(rel "")
    if(EXISTS "/proc/self/cgroup")
        file(STRINGS "/proc/self/cgroup" line REGEX "^0::/")
        if(line MATCHES "^0::(/.*)$")
            # file(STRINGS) returns a list, which escapes a ";" in the path.
            string(REPLACE "\\;" ";" rel "${CMAKE_MATCH_1}")
        endif()
    endif()
    # Walk the path as a string: cgroup names can contain backslash escapes
    # (for example "\x2d"), which CMake's path commands treat as separators.
    while(NOT rel STREQUAL "")
        set(dir "/sys/fs/cgroup${rel}")
        if(EXISTS "${dir}/memory.max" AND EXISTS "${dir}/memory.current")
            file(STRINGS "${dir}/memory.max" max LIMIT_COUNT 1)
            file(STRINGS "${dir}/memory.current" current LIMIT_COUNT 1)
            # With the multi-generational LRU, cache that was used once can
            # still show as active_file, so inactive_file alone undercounts.
            set(file_cache 0)
            if(EXISTS "${dir}/memory.stat")
                file(STRINGS "${dir}/memory.stat" stat_lines
                    REGEX "^(in)?active_file [0-9]+$")
                foreach(stat_line IN LISTS stat_lines)
                    string(REGEX REPLACE "^[a-z_]+ " "" bytes "${stat_line}")
                    math(EXPR file_cache "${file_cache} + ${bytes}")
                endforeach()
            endif()
            if(max MATCHES "^[0-9]+$" AND current MATCHES "^[0-9]+$")
                math(EXPR used "${current} - ${file_cache}")
                if(used LESS 0)
                    set(used 0)
                endif()
                math(EXPR headroom "(${max} - ${used}) / 1048576")
                if(headroom LESS 0)
                    set(headroom 0)
                endif()
                if(headroom LESS avail)
                    set(avail ${headroom})
                    set(cgroup_limited TRUE)
                endif()
            endif()
        endif()
        if(rel STREQUAL "/")
            break()
        endif()
        string(FIND "${rel}" "/" slash REVERSE)
        if(slash EQUAL 0)
            set(rel "/")
        else()
            string(SUBSTRING "${rel}" 0 ${slash} rel)
        endif()
    endwhile()

    set(${out_mib} ${avail} PARENT_SCOPE)
    set(${out_cgroup_limited} ${cgroup_limited} PARENT_SCOPE)
endfunction()

# CMake reads CMAKE_JOB_POOLS only while JOB_POOLS is unset, and a project may
# replace JOB_POOLS outright. Either way a pool defined after this file ran
# would go missing from build.ninja, and Ninja rejects a missing pool.
function(_memory_aware_job_pools_reconcile)
    get_property(ours GLOBAL PROPERTY _MEMORY_AWARE_JOB_POOLS)
    get_property(pools GLOBAL PROPERTY JOB_POOLS)
    set(names "")
    set(foreign FALSE)
    foreach(entry IN LISTS pools)
        string(REGEX REPLACE "=.*" "" name "${entry}")
        list(APPEND names "${name}")
        list(FIND ours "${entry}" index)
        if(index EQUAL -1)
            set(foreign TRUE)
        endif()
    endforeach()
    # Without this file, entries of the project's own in JOB_POOLS would hide
    # CMAKE_JOB_POOLS, so add it only when JOB_POOLS holds none.
    set(wanted ${ours})
    if(NOT foreign)
        set(wanted ${CMAKE_JOB_POOLS} ${ours})
    endif()
    set(missing "")
    foreach(entry IN LISTS wanted)
        string(REGEX REPLACE "=.*" "" name "${entry}")
        list(FIND names "${name}" index)
        if(index EQUAL -1)
            list(APPEND missing "${entry}")
            list(APPEND names "${name}")
        endif()
    endforeach()
    if(NOT "${missing}" STREQUAL "")
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS ${missing})
    endif()
endfunction()

function(_memory_aware_job_pools_add name depth)
    get_property(ours GLOBAL PROPERTY _MEMORY_AWARE_JOB_POOLS)
    if("${ours}" STREQUAL "" AND NOT CMAKE_VERSION VERSION_LESS 3.19)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
            CALL _memory_aware_job_pools_reconcile)
    endif()
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS ${name}=${depth})
    set_property(GLOBAL APPEND PROPERTY _MEMORY_AWARE_JOB_POOLS
        ${name}=${depth})
endfunction()

function(_memory_aware_job_pools)
    if(NOT MEMORY_AWARE_JOB_POOLS)
        return()
    endif()
    # Setting the JOB_POOLS property would silently replace CMAKE_JOB_POOLS.
    if(NOT "${CMAKE_JOB_POOLS}" STREQUAL "")
        return()
    endif()
    get_property(pools GLOBAL PROPERTY JOB_POOLS)
    if(NOT "${pools}" STREQUAL "")
        return()
    endif()

    set(job_mib 1024)
    if(DEFINED MEMORY_AWARE_JOB_MIB)
        set(job_mib "${MEMORY_AWARE_JOB_MIB}")
    endif()
    set(reserve_mib 2048)
    if(DEFINED MEMORY_AWARE_RESERVE_MIB)
        set(reserve_mib "${MEMORY_AWARE_RESERVE_MIB}")
    endif()
    if(NOT job_mib MATCHES "^[0-9]+$" OR job_mib EQUAL 0)
        message(FATAL_ERROR "MemoryAwareJobPools: MEMORY_AWARE_JOB_MIB must "
            "be a positive integer, not \"${job_mib}\"")
    endif()
    if(NOT reserve_mib MATCHES "^[0-9]+$")
        message(FATAL_ERROR "MemoryAwareJobPools: MEMORY_AWARE_RESERVE_MIB "
            "must be a non-negative integer, not \"${reserve_mib}\"")
    endif()
    foreach(var MEMORY_AWARE_COMPILE_JOBS MEMORY_AWARE_LINK_JOBS)
        if(NOT "${${var}}" MATCHES "^[0-9]+$")
            message(FATAL_ERROR "MemoryAwareJobPools: ${var} must be 0 "
                "(size from memory) or a positive integer, not \"${${var}}\"")
        endif()
    endforeach()

    set(want_compile FALSE)
    if("${CMAKE_JOB_POOL_COMPILE}" STREQUAL "")
        set(want_compile TRUE)
    endif()
    set(want_link FALSE)
    if("${CMAKE_JOB_POOL_LINK}" STREQUAL "")
        set(want_link TRUE)
    endif()

    set(avail "")
    if((want_compile AND MEMORY_AWARE_COMPILE_JOBS EQUAL 0) OR
       (want_link AND MEMORY_AWARE_LINK_JOBS EQUAL 0))
        _memory_aware_job_pools_available(avail cgroup_limited)
    endif()

    set(jobs 0)
    if(NOT avail STREQUAL "")
        if(avail GREATER reserve_mib)
            math(EXPR jobs "(${avail} - ${reserve_mib}) / ${job_mib}")
        endif()
        # A Ninja pool depth of 0 means unlimited, so no depth may reach 0.
        if(jobs LESS 1)
            set(jobs 1)
        endif()
        math(EXPR auto_link "${jobs} / 4")
        if(auto_link LESS 1)
            set(auto_link 1)
        endif()

        _memory_aware_job_pools_gib(avail_gib ${avail} TRUE)
        _memory_aware_job_pools_gib(reserve_gib ${reserve_mib} FALSE)
        _memory_aware_job_pools_gib(job_gib ${job_mib} FALSE)
        set(inputs "available ${avail_gib} GiB")
        if(cgroup_limited)
            string(APPEND inputs " in cgroup")
        endif()
        string(APPEND inputs ", reserve ${reserve_gib} GiB, ${job_gib} GiB/job")
    endif()

    if(jobs EQUAL 1 AND want_compile AND want_link
       AND MEMORY_AWARE_COMPILE_JOBS EQUAL 0
       AND MEMORY_AWARE_LINK_JOBS EQUAL 0)
        _memory_aware_job_pools_add(memory_aware_jobs 1)
        set(CMAKE_JOB_POOL_COMPILE memory_aware_jobs PARENT_SCOPE)
        set(CMAKE_JOB_POOL_LINK memory_aware_jobs PARENT_SCOPE)
        message(STATUS "MemoryAwareJobPools: compile+link=1 (one pool; "
            "${inputs}); set MEMORY_AWARE_COMPILE_JOBS or "
            "MEMORY_AWARE_LINK_JOBS to override")
        return()
    endif()

    set(link "")
    if(want_link)
        if(MEMORY_AWARE_LINK_JOBS GREATER 0)
            set(link ${MEMORY_AWARE_LINK_JOBS})
            set(link_why "MEMORY_AWARE_LINK_JOBS=${link}")
            set(link_how "set it to 0 to size from memory")
        elseif(NOT avail STREQUAL "")
            set(link ${auto_link})
            set(link_why "${inputs}")
            set(link_how "set MEMORY_AWARE_LINK_JOBS to override")
        endif()
    endif()

    set(compile "")
    if(want_compile)
        if(MEMORY_AWARE_COMPILE_JOBS GREATER 0)
            set(compile ${MEMORY_AWARE_COMPILE_JOBS})
            set(compile_why "MEMORY_AWARE_COMPILE_JOBS=${compile}")
            set(compile_how "set it to 0 to size from memory")
        elseif(NOT avail STREQUAL "")
            set(link_share ${auto_link})
            if(NOT link STREQUAL "")
                set(link_share ${link})
            endif()
            math(EXPR compile "${jobs} - ${link_share}")
            if(compile LESS 1)
                set(compile 1)
            endif()
            set(compile_why "${inputs}")
            set(compile_how "set MEMORY_AWARE_COMPILE_JOBS to override")
        endif()
    endif()

    if(NOT compile STREQUAL "")
        _memory_aware_job_pools_add(memory_aware_compile ${compile})
        set(CMAKE_JOB_POOL_COMPILE memory_aware_compile PARENT_SCOPE)
        message(STATUS "MemoryAwareJobPools: compile=${compile} "
            "(${compile_why}); ${compile_how}")
    endif()
    if(NOT link STREQUAL "")
        _memory_aware_job_pools_add(memory_aware_link ${link})
        set(CMAKE_JOB_POOL_LINK memory_aware_link PARENT_SCOPE)
        message(STATUS "MemoryAwareJobPools: link=${link} "
            "(${link_why}); ${link_how}")
    endif()
endfunction()

_memory_aware_job_pools()
