cmake_minimum_required(VERSION 3.15)

set(CBOX_BASE_VERSION_MAJOR 1)
set(CBOX_BASE_VERSION_MINOR 0)
set(CBOX_BASE_VERSION_PATCH 1)
set(CBOX_BASE_VERSION ${CBOX_BASE_VERSION_MAJOR}.${CBOX_BASE_VERSION_MINOR}.${CBOX_BASE_VERSION_PATCH})

project(cbox_base)

add_definitions(-DLOG_MODULE_ID="cbox.base")

set(CBOX_BASE_HEADERS
    macros.h
    utils.h
    list.h
    rbtree.h
    log.h
    pbl.h)

set(CBOX_BASE_SOURCES
    utils.c
    rbtree.c
    log.c
    pbl/src/pblCgi.c
    pbl/src/pblStringBuilder.c
    pbl/src/pblPriorityQueue.c
    pbl/src/pblHeap.c
    pbl/src/pblMap.c
    pbl/src/pblSet.c
    pbl/src/pblList.c
    pbl/src/pblCollection.c
    pbl/src/pblIterator.c
    pbl/src/pblisam.c
    pbl/src/pblkf.c
    pbl/src/pblhash.c
    pbl/src/pbl.c)

set(CBOX_BASE_TEST_SOURCES
    log_test.cpp
    utils_test.cpp)

add_library(${PROJECT_NAME} ${CBOX_BUILD_LIB_TYPE} ${CBOX_BASE_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
                        VERSION ${CBOX_BASE_VERSION}
                      SOVERSION ${CBOX_BASE_VERSION_MAJOR})

if(${CBOX_ENABLE_TEST})
    add_executable(${PROJECT_NAME}_test ${CBOX_BASE_TEST_SOURCES})

    target_link_libraries(${PROJECT_NAME}_test gmock_main gmock gtest pthread ${PROJECT_NAME} rt dl)
    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)
endif()

install(FILES ${CBOX_BASE_HEADERS} DESTINATION include/cbox/base)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
