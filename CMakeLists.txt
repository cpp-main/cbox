#
# cbox
#
cmake_minimum_required(VERSION 3.15)

#
# Building in-tree is not allowed (we take care of your craziness).
#
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there. Thank you.")
endif()

#
# Project configuration
#
set(CBOX_VERSION_MAJOR 0)
set(CBOX_VERSION_MINOR 1)
set(CBOX_VERSION_PATCH 0)
set(CBOX_VERSION ${CBOX_VERSION_MAJOR}.${CBOX_VERSION_MINOR}.${CBOX_VERSION_PATCH})


#
# Set install prefix
#
#set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "")


project(
    cbox
    VERSION ${CBOX_VERSION}
    DESCRIPTION "Easy to use, convenient and treasure box!"
    HOMEPAGE_URL "https://gitee.com/cpp-master/cbox"
    LANGUAGES C
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CBOX_BUILD_LIB_TYPE STATIC)

message("*")
message("* ${PROJECT_NAME} v${PROJECT_VERSION} (${CMAKE_BUILD_TYPE})")
message("* Copyright (c) 2020-2025 Hevake,Lucas")
message("*")

option(CBOX_ENABLE_BASE "build base" ON)
option(CBOX_ENABLE_EVENT "build event" ON)

#
# TESTS
#
option(CMAKE_ENABLE_TEST "Whether to enable unit tests" OFF)

if(CMAKE_ENABLE_TEST)
    message(STATUS "Unit tests enabled")
    find_package(GTest REQUIRED)
    set(CBOX_ENABLE_TEST ON)
    enable_testing()
endif()


#
# Compiler args
#
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra -Werror -Wno-unused-result -Wno-missing-field-initializers)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_definitions(-DCBOX_VERSION_MAJOR=${CBOX_VERSION_MAJOR}
                -DCBOX_VERSION_MINOR=${CBOX_VERSION_MINOR}
                -DCBOX_VERSION_REVISION=${CBOX_VERSION_REVISION})

if(CBOX_ENABLE_BASE)
    message(STATUS "base module enabled")
    add_subdirectory(src/base)
endif()

if(CBOX_ENABLE_EVENT)
    message(STATUS "event module enabled")
    add_subdirectory(src/event)
endif()
