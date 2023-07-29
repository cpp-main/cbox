cmake_minimum_required(VERSION 3.15)

set(CBOX_MQTT_VERSION_MAJOR 0)
set(CBOX_MQTT_VERSION_MINOR 0)
set(CBOX_MQTT_VERSION_PATCH 1)
set(CBOX_MQTT_VERSION ${CBOX_MQTT_VERSION_MAJOR}.${CBOX_MQTT_VERSION_MINOR}.${CBOX_MQTT_VERSION_PATCH})

project(cbox_mqtt)

add_definitions(-DLOG_MODULE_ID="cbox.mqtt")

set(CBOX_MQTT_HEADERS mqtt_client.h)

set(CBOX_MQTT_SOURCES mqtt_client.c)

set(CBOX_MQTT_TEST_SOURCES mqtt_client_test.cpp)

add_library(${PROJECT_NAME} ${CBOX_BUILD_LIB_TYPE} ${CBOX_MQTT_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
                        VERSION ${CBOX_MQTT_VERSION}
                      SOVERSION ${CBOX_MQTT_VERSION_MAJOR})

if(${CBOX_ENABLE_TEST})
    add_executable(${PROJECT_NAME}_test ${CBOX_MQTT_TEST_SOURCES})

    target_link_libraries(${PROJECT_NAME}_test gmock_main gmock gtest ${PROJECT_NAME} cbox_event cbox_base mosquitto pthread)
    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)
endif()

install(FILES ${CBOX_MQTT_HEADERS} DESTINATION include/cbox/mqtt)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
