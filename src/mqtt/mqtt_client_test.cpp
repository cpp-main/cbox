#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mqtt/mqtt_client.h"
#include "base/utils.h"
#include "base/log.h"

using ::testing::Exactly;

static void connected_cb(bool connected, void* user);
    

// Test fixture for mqtt_client tests
class MqttClient : public ::testing::Test
{
public:
    void SetUp() override {
        CBOX_LOG_INIT(CBOX_LOG_LEVEL_DEBUG, LOG_MODULE_ID);
        loop_ = cbox_loop_new();
        mqtt_ = cbox_mqtt_client_new(loop_, "test", "localhost", 1883, 1, connected_cb, NULL, NULL,this);
    }

    void TearDown() override {
        cbox_mqtt_client_delete(mqtt_);
        cbox_loop_delete(loop_);
        CBOX_LOG_DESTROY();
    }

    MOCK_METHOD2(ConnectedCallback, void(bool, void *));

public:
    cbox_loop_t* loop_;
    cbox_mqtt_client_t *mqtt_;
};

void connected_cb(bool connected, void* user)
{
    auto test_instance = static_cast<MqttClient*>(user);
    test_instance->ConnectedCallback(connected, user);
}


TEST_F(MqttClient, Connected) {
    bool expected_connected = false;
    EXPECT_CALL(*this, ConnectedCallback(expected_connected, this)).Times(Exactly(1));
    EXPECT_FALSE(cbox_mqtt_client_connected(mqtt_));
    cbox_mqtt_client_enable(mqtt_);
}
