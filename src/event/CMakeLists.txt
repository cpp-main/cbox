cmake_minimum_required(VERSION 3.15)

set(CBOX_EVENT_VERSION_MAJOR 1)
set(CBOX_EVENT_VERSION_MINOR 0)
set(CBOX_EVENT_VERSION_PATCH 1)
set(CBOX_EVENT_VERSION ${CBOX_EVENT_VERSION_MAJOR}.${CBOX_EVENT_VERSION_MINOR}.${CBOX_EVENT_VERSION_PATCH})

project(cbox_event)

add_definitions(-DLOG_MODULE_ID="cbox.event")

set(CBOX_EVENT_HEADERS
    loop.h
    fd_event.h
    signal_event.h
    timer.h)

set(CBOX_EVENT_SOURCES
    loop.c
    fd_event.c
    signal_event.c
    timer.c
    delegator.c)

set(CBOX_EVENT_TEST_SOURCES
    loop_test.cpp
    fd_event_test.cpp
    signal_event_test.cpp
    timer_test.cpp)

add_library(${PROJECT_NAME} ${CBOX_BUILD_LIB_TYPE} ${CBOX_EVENT_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
                        VERSION ${CBOX_EVENT_VERSION}
                      SOVERSION ${CBOX_EVENT_VERSION_MAJOR})

if(${CBOX_ENABLE_TEST})
    add_executable(${PROJECT_NAME}_test ${CBOX_EVENT_TEST_SOURCES})

    target_link_libraries(${PROJECT_NAME}_test gmock_main gmock gtest pthread ${PROJECT_NAME} cbox_base rt dl)
    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)
endif()

install(FILES ${CBOX_EVENT_HEADERS} DESTINATION include/cbox/event)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
