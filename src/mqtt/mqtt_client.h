#ifndef _MQTT_H_20230129_
#define _MQTT_H_20230129_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "event/loop.h"

typedef struct cbox_mqtt_client_s cbox_mqtt_client_t;

typedef void (*cbox_mqtt_client_connected_func_t)(bool connected, void *user);
typedef void (*cbox_mqtt_client_message_func_t)(int mid, const char *topic, void *data, size_t len, int qos, bool retain, void *user);
typedef void (*cbox_mqtt_client_subscribed_func_t)(bool subscribed, void *user);

#ifdef __cplusplus
extern "C"
{
#endif

cbox_mqtt_client_t *cbox_mqtt_client_new(cbox_loop_t *loop,
                        const char *client_id,
                        const char *broker_host,
                        uint16_t broker_port,
                        int keepalive,
                        cbox_mqtt_client_connected_func_t connected,
                        cbox_mqtt_client_subscribed_func_t subscribed,
                        cbox_mqtt_client_message_func_t messaged,
                        void *user);

void cbox_mqtt_client_enable(cbox_mqtt_client_t *mqtt);
void cbox_mqtt_client_disable(cbox_mqtt_client_t *mqtt);

void cbox_mqtt_client_publish(cbox_mqtt_client_t *client, const char *topic, const void *payload, size_t payload_len, int qos, bool retain, int *mid);
void cbox_mqtt_client_subscribe(cbox_mqtt_client_t *client, const char *topic, int *mid, int qos);
void cbox_mqtt_client_unsubscribe(cbox_mqtt_client_t *client, const char *topic, int *mid);

bool cbox_mqtt_client_connected(cbox_mqtt_client_t *client);

void cbox_mqtt_client_delete(cbox_mqtt_client_t *client);
#ifdef __cplusplus
}
#endif

#endif
