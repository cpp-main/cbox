#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

#include "mqtt_client.h"
#include "mosquitto.h"
#include "base/utils.h"
#include "event/timer.h"
#include "event/fd_event.h"
#include "base/log.h"
#include "base/macros.h"

struct cbox_mqtt_client_s {
    cbox_loop_t *loop;

    struct mosquitto *mosquitto_instance;

    cbox_mqtt_client_connected_func_t connected_callback;
    cbox_mqtt_client_subscribed_func_t subscribed_callback;
    cbox_mqtt_client_message_func_t message_callback;

    void *user_data;
    bool enabled;

    char *client_id;
    char *broker_host;
    char *user_name;
    char *password;
    uint16_t broker_port;
    int keepalive;
    char *def_sub_topic;

    bool connected;
    bool keep_connect;

    cbox_fd_event_t *read_fd_event;
    cbox_fd_event_t *write_fd_event;
    cbox_timer_t *timer;
};

/* Local function declarations. */
static void on_cbox_mqtt_client_timeout(void *arg);

static void mqtt_cb_read_fd_event(int fd, uint32_t events, void *user);
static void mqtt_cb_write_fd_event(int fd, uint32_t events, void *user);

/*static void mqtt_cb_log(struct mosquitto *mosq, void *obj, int level, const char *str);*/
static void mqtt_cb_connect(struct mosquitto *mosq, void *obj, int rc);
static void mqtt_cb_disconnect(struct mosquitto *mosq, void *obj, int rc);

static void mqtt_cb_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos);
static void mqtt_cb_unsubscribe(struct mosquitto *mosq, void *obj, int mid);

static void mqtt_cb_publish(struct mosquitto *mosq, void *obj, int mid);
static void mqtt_cb_msg(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

static void mqtt_trigger_reconnect(cbox_mqtt_client_t *mqtt, bool firsttime);

static int mqtt_proc_reconnect(cbox_mqtt_client_t *mqtt);
static int mqtt_assign_event_loop(cbox_mqtt_client_t *mqtt);

static void mqtt_handle_rc(cbox_mqtt_client_t *mqtt, int rc, const char *function, const char *file, unsigned long line);

#define mqtt_handle_return(client, retval)                                                                          \
    do {                                                                                                            \
        mqtt_handle_rc((client), (rc), __FUNCTION__, __FILE__, __LINE__);                                           \
    } while (0)


/* Global vaiable declarations. */

static int g_mosquitto_instance_count = 0;

static void mqtt_prepare_write(cbox_mqtt_client_t *mqtt)
{
    int rc = -1;
    bool want_write = mosquitto_want_write(mqtt->mosquitto_instance);
    if (want_write && !cbox_fd_event_enabled(mqtt->write_fd_event)) {
        rc = cbox_fd_event_enable(mqtt->write_fd_event);
        if (rc != 0)
            LOGE("(cbox_fd_event_enable() fd:%d, type:write) failed.\n", mosquitto_socket(mqtt->mosquitto_instance));
    }
}

void cbox_mqtt_client_publish(cbox_mqtt_client_t *client, const char *topic, const void *payload, size_t payload_len, int qos, bool retain, int *mid)
{
    assert(client != NULL && topic != NULL && payload != NULL);
    int tmp_mid = 0;
    int rc = mosquitto_publish(client->mosquitto_instance, &tmp_mid, topic, payload_len, payload, qos, retain);
    if (mid != NULL) *mid = tmp_mid;
    mqtt_handle_return(client, rc);
    if (MOSQ_ERR_SUCCESS == rc) mqtt_prepare_write(client);
}

void cbox_mqtt_client_subscribe(cbox_mqtt_client_t *client, const char *topic, int *mid, int qos)
{
    assert(client != NULL && topic != NULL);
    int tmp_mid = 0;
    int rc = mosquitto_subscribe(client->mosquitto_instance, &tmp_mid, topic, qos);
    if (mid != NULL) *mid = tmp_mid;
    mqtt_handle_return(client, rc);
    if (MOSQ_ERR_SUCCESS == rc) {
        mqtt_prepare_write(client);
        CBOX_SAFETY_FREE(client->def_sub_topic);
        client->def_sub_topic = strdup(topic);
    }
}

void mqtt_unsubscribe(cbox_mqtt_client_t *client, const char *topic, int *mid)
{
    assert(client != NULL && topic != NULL);
    int tmp_mid = 0;
    int rc = mosquitto_unsubscribe(client->mosquitto_instance, &tmp_mid, topic);
    if (mid != NULL) *mid = tmp_mid;
    mqtt_handle_return(client, rc);
    if (MOSQ_ERR_SUCCESS == rc) {
        mqtt_prepare_write(client);
        CBOX_SAFETY_FREE(client->def_sub_topic);
    }
}

cbox_mqtt_client_t *cbox_mqtt_client_new(cbox_loop_t *loop,
                                         const char *client_id,
                                         const char *broker_host,
                                         uint16_t broker_port,
                                         int keepalive,
                                         cbox_mqtt_client_connected_func_t connected,
                                         cbox_mqtt_client_subscribed_func_t subscribed,
                                         cbox_mqtt_client_message_func_t message,
                                         void *user)
{
    assert(loop != NULL && client_id != NULL && broker_host != NULL);

    if (g_mosquitto_instance_count == 0)
        mosquitto_lib_init();

    ++ g_mosquitto_instance_count;

    cbox_mqtt_client_t *client = (cbox_mqtt_client_t*)malloc(sizeof(cbox_mqtt_client_t));
    assert(client != NULL);

    client->mosquitto_instance = mosquitto_new(NULL, true, client);
    assert(client->mosquitto_instance != NULL);

    client->connected = false;
    client->keep_connect = false;
    client->enabled = false;

    client->client_id = client_id ? strdup(client_id) : NULL;
    client->broker_host = strdup(broker_host);
    client->broker_port = broker_port;
    client->keepalive = keepalive;
    client->def_sub_topic = NULL;
    client->user_data = user;
    client->loop = loop;
    client->connected_callback = connected;
    client->subscribed_callback = subscribed;
    client->message_callback = message;

    client->read_fd_event = NULL;
    client->write_fd_event = NULL;

    /*mosquitto_log_callback_set(client->mosquitto_instance, mqtt_cb_log);*/
    mosquitto_connect_callback_set(client->mosquitto_instance, mqtt_cb_connect);
    mosquitto_disconnect_callback_set(client->mosquitto_instance, mqtt_cb_disconnect);
    mosquitto_subscribe_callback_set(client->mosquitto_instance, mqtt_cb_subscribe);
    mosquitto_unsubscribe_callback_set(client->mosquitto_instance, mqtt_cb_unsubscribe);
    mosquitto_publish_callback_set(client->mosquitto_instance, mqtt_cb_publish);
    mosquitto_message_callback_set(client->mosquitto_instance, mqtt_cb_msg);
    struct timespec ts;
    ts.tv_sec = 5;
    ts.tv_nsec = 0;
    client->timer = cbox_timer_new(loop, &ts, 0, on_cbox_mqtt_client_timeout, client);
    assert(client->timer != NULL);

    return client;
}

void cbox_mqtt_client_enable(cbox_mqtt_client_t *client)
{
    assert(client != NULL);
    if (client->enabled)
        return;

    client->keep_connect = true;
    mqtt_trigger_reconnect(client, true);
}

void cbox_mqtt_client_disable(cbox_mqtt_client_t *client)
{
    assert(client != NULL);
    if (client->enabled  == false)
        return;

    client->keep_connect = false;

    mosquitto_disconnect(client->mosquitto_instance);

    client->enabled = false;
}

void cbox_mqtt_client_delete(cbox_mqtt_client_t *client)
{
    if (client == NULL)
        return;

    if (client->enabled)
        cbox_mqtt_client_disable(client);

    CBOX_SAFETY_FUNC(cbox_timer_delete, client->timer);

    CBOX_SAFETY_FUNC(mosquitto_destroy, client->mosquitto_instance);

    CBOX_SAFETY_FREE(client->client_id);
    CBOX_SAFETY_FREE(client->broker_host);
    CBOX_SAFETY_FREE(client->def_sub_topic);


    if ((-- g_mosquitto_instance_count) == 0)
        mosquitto_lib_cleanup();

    CBOX_SAFETY_FUNC(cbox_fd_event_delete, client->read_fd_event);
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, client->write_fd_event);

    CBOX_SAFETY_FREE(client);
}

bool cbox_mqtt_client_connected(cbox_mqtt_client_t *client)
{
    assert(client != NULL);
    return client->connected;
}

static void on_cbox_mqtt_client_timeout(void *arg)
{
    static uint32_t count = 0;
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)arg;

    count ++;
    if (count % 5 == 0 && mqtt->keep_connect && !mqtt->connected)
        mqtt_proc_reconnect(mqtt);

    mosquitto_loop_misc(mqtt->mosquitto_instance);
}

static int mqtt_assign_event_loop(cbox_mqtt_client_t *mqtt)
{
    int mosq_sock = mosquitto_socket(mqtt->mosquitto_instance);
    if (mosq_sock <= 0)
        LOGW_WITH_GOTO(CLEANUP, "mosquitto_socket(), ret: %d", mosq_sock);

    unsigned int flags = fcntl(mosq_sock, F_GETFL, 0);
    fcntl(mosq_sock, F_SETFL, flags | O_NONBLOCK);


    CBOX_SAFETY_FUNC(cbox_fd_event_delete, mqtt->read_fd_event);
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, mqtt->write_fd_event);

    mqtt->read_fd_event = cbox_fd_event_new(mqtt->loop, mosq_sock,
                                       CBOX_EVENT_READ, mqtt_cb_read_fd_event, CBOX_RUN_MODE_FOREVER, mqtt);

    if (mqtt->read_fd_event == NULL)
        goto CLEANUP;

    mqtt->write_fd_event = cbox_fd_event_new(mqtt->loop, mosq_sock,
                                       CBOX_EVENT_WRITE, mqtt_cb_write_fd_event, CBOX_RUN_MODE_FOREVER, mqtt);

    if (mqtt->write_fd_event == NULL)
        goto CLEANUP;

    cbox_fd_event_enable(mqtt->read_fd_event);
    cbox_fd_event_enable(mqtt->write_fd_event);

    mqtt_prepare_write(mqtt);

    return 0;

CLEANUP:
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, mqtt->read_fd_event);
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, mqtt->write_fd_event);
    return -1;
}

static int mqtt_proc_reconnect(cbox_mqtt_client_t *mqtt)
{
    LOGI("try to reconnect...");
    int rc = 0, ret = -1;
    rc = mosquitto_reconnect_async(mqtt->mosquitto_instance);
    if (MOSQ_ERR_INVAL == rc) {
        /* Not initialed and try connect again*/
        rc = mosquitto_connect_async(mqtt->mosquitto_instance, mqtt->broker_host, mqtt->broker_port, mqtt->keepalive);
    }

    mqtt_handle_return(mqtt, rc);

    if (rc <= MOSQ_ERR_SUCCESS) {
        ret = mqtt_assign_event_loop(mqtt);
        if (ret != 0) {
            LOGE("mqtt_assign_event_loop(), failed: %d\n", ret);
            mqtt_trigger_reconnect(mqtt, false);
        }
    }

    return 0;
}

static void mqtt_trigger_reconnect(cbox_mqtt_client_t *mqtt, bool firsttime)
{
    mqtt->connected = false;
    if (firsttime)
        mqtt_proc_reconnect(mqtt);
}

static void mqtt_handle_rc(cbox_mqtt_client_t *mqtt, int rc, const char *function, const char *file, unsigned long line)
{
    int mosq_sock = mosquitto_socket(mqtt->mosquitto_instance);
    if ((rc == MOSQ_ERR_SUCCESS && mosq_sock >= 0))
        return;

    if (rc < MOSQ_ERR_SUCCESS)
        LOGI("MQTT[%s] handler rc:[%d][%s] errno:[%d], sock:%d", mqtt->client_id, rc, mosquitto_strerror(rc), errno, mosq_sock);
    else
        LOGE("MQTT[%s] handler rc:[%d][%s] errno:[%d], sock:%d", mqtt->client_id, rc, mosquitto_strerror(rc), errno, mosq_sock);

    mqtt->connected = false;
    if (mqtt->keep_connect) {
        if (mqtt->connected_callback)
            mqtt->connected_callback(false, mqtt->user_data);

        mqtt_trigger_reconnect(mqtt, false);
    } else {
        cbox_fd_event_disable(mqtt->read_fd_event);
        cbox_fd_event_disable(mqtt->write_fd_event);
    }

    (void)function;
    (void)file;
    (void)line;
}

static void mqtt_cb_read_fd_event(int fd, uint32_t events, void *user)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)user;
    int rc = 0;
    if (events & CBOX_EVENT_READ) {
        rc = mosquitto_loop_misc(mqtt->mosquitto_instance);
        mqtt_handle_return(mqtt, rc);

        if (MOSQ_ERR_SUCCESS != rc)
            return;

        rc = mosquitto_loop_read(mqtt->mosquitto_instance, 1);
        mqtt_handle_return(mqtt, rc);
        if (MOSQ_ERR_SUCCESS != rc)
            return;
    }

    mqtt_prepare_write(mqtt);
    (void)fd;
}

static void mqtt_cb_write_fd_event(int fd, uint32_t events, void *user)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)user;
    int rc = 0;
    if (events & CBOX_EVENT_WRITE) {
        rc = mosquitto_loop_write(mqtt->mosquitto_instance, 1);
        mqtt_handle_return(mqtt, rc);
        if (MOSQ_ERR_SUCCESS != rc)
            return;

        /// clear write falgs after event
        cbox_fd_event_disable(mqtt->write_fd_event);
    }

    (void)fd;
}

/*static void mqtt_cb_log(struct mosquitto *mosq, void *obj, int level, const char *str)
{
    LOGD("%s", str);
}*/

static void mqtt_cb_connect(struct mosquitto *mosq, void *obj, int rc)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)obj;

    LOGD("MQTT[%s] Connected rc:[%d][%s].\n", mqtt->client_id, rc, mosquitto_strerror(rc));

    if (rc == 0) {
        if (mqtt->def_sub_topic != NULL) {
            rc = mosquitto_subscribe(mosq, NULL, mqtt->def_sub_topic, 0);
            mqtt_handle_return(mqtt, rc);
            if (MOSQ_ERR_SUCCESS != rc)
                return;

            mqtt_prepare_write(mqtt);
        }

        mqtt->connected = true;
        if (mqtt->connected_callback)
            mqtt->connected_callback(true, mqtt->user_data);
    } else {
        mqtt_trigger_reconnect(mqtt, false);
    }
}

static void mqtt_cb_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)obj;
    LOGD("MQTT[%s] Disconnected rc:[%d][%s].\n", mqtt->client_id, rc, mosquitto_strerror(rc));
    mqtt->connected = false;

    /* Free read and write event to avoid epoll issue(continue report EPOLLHUP on closed fd) */
    cbox_fd_event_disable(mqtt->read_fd_event);
    cbox_fd_event_disable(mqtt->write_fd_event);

    mqtt_trigger_reconnect(mqtt, false);
    (void)mosq;
}

static void mqtt_cb_msg(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)obj;
    if (message->payloadlen)
        LOGI("%s:%s", message->topic, message->payload);

    if (mqtt->message_callback)
        mqtt->message_callback(message->mid, message->topic, message->payload, message->payloadlen, message->qos, message->retain, mqtt->user_data);

    (void)mqtt;
    (void)mosq;
}

static void mqtt_cb_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)obj;
    LOGD("MQTT[%s] Subscribe Message MID[%d] QoS Count[%d] Done.\n", mqtt->client_id, mid, qos_count);
    if (mqtt->subscribed_callback) {
        mqtt->subscribed_callback(true, mqtt->user_data);
    }

    (void)mosq;
    (void)granted_qos;
}

static void mqtt_cb_unsubscribe(struct mosquitto *mosq, void *obj, int mid)
{
    cbox_mqtt_client_t *mqtt = (cbox_mqtt_client_t *)obj;
    LOGD("MQTT[%s] Unsubscribe Message MID[%d] Done.\n", mqtt->client_id, mid);
    if (mqtt->subscribed_callback) {
        mqtt->subscribed_callback(false, mqtt->user_data);
    }

    (void)mosq;
}

static void mqtt_cb_publish(struct mosquitto *mosq, void *obj, int mid)
{
    (void)mosq;
    (void)obj;
    (void)mid;
}
