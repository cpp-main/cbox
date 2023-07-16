#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include "fd_event.h"
#include "base/utils.h"

// Test fixture for fd_event tests
class FdEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        loop = cbox_loop_new();
    }

    void TearDown() override {
        cbox_loop_delete(loop);
    }


public:
    int trigger_cnt = 0;
    cbox_loop_t* loop;    
    cbox_fd_event_t *fd_event = nullptr;
};

TEST_F(FdEventTest, CreateAndDestroy) {
    cbox_fd_event_t *fd = cbox_fd_event_new(loop, 0, CBOX_EVENT_READ, NULL, CBOX_RUN_MODE_ONCE, NULL);
    ASSERT_TRUE(fd != NULL);
    cbox_fd_event_delete(fd);
}


static void on_once_event(int fd, uint32_t events, void *user)
{
    FdEventTest *self = static_cast<FdEventTest *>(user);
    EXPECT_EQ(events, CBOX_EVENT_WRITE);
    ++self->trigger_cnt;
    cbox_loop_exit_after(self->loop, 500);
    (void)fd;
}

TEST_F(FdEventTest, Once) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    ASSERT_TRUE(fd > 0);
    
    void *user = this;
    cbox_fd_event_t *event = cbox_fd_event_new(loop, fd, CBOX_EVENT_WRITE, on_once_event, CBOX_RUN_MODE_ONCE, user);
    ASSERT_TRUE(event != NULL);
    cbox_fd_event_enable(event);
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);
    EXPECT_EQ(trigger_cnt, 1);
    cbox_fd_event_delete(event);
}

static void on_forever_event(int fd, uint32_t events, void *user)
{
    FdEventTest *self = static_cast<FdEventTest *>(user);
    EXPECT_EQ(events, CBOX_EVENT_WRITE);
    ++self->trigger_cnt;
    if (self->trigger_cnt >= 10)
        cbox_loop_exit(self->loop);

    (void)fd;
}

TEST_F(FdEventTest, Forever) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    ASSERT_TRUE(fd > 0);
    
    uint32_t event_type = CBOX_EVENT_WRITE;
    void *user = this;
    trigger_cnt = 0;
    cbox_fd_event_t *event = cbox_fd_event_new(loop, fd, event_type, on_forever_event, CBOX_RUN_MODE_FOREVER, user);
    ASSERT_TRUE(event != NULL);
    cbox_fd_event_enable(event);
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);
    EXPECT_TRUE(trigger_cnt > 1);
    cbox_fd_event_delete(event);
}

static void on_disable_event(int fd, uint32_t events, void *user)
{
    FdEventTest *self = static_cast<FdEventTest *>(user);
    EXPECT_EQ(events, CBOX_EVENT_WRITE);
    ++self->trigger_cnt;
    cbox_fd_event_disable(self->fd_event);
    cbox_loop_exit_after(self->loop, 500);
    (void)fd;
}

TEST_F(FdEventTest, Disable) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    ASSERT_TRUE(fd > 0);
    
    uint32_t event_type = CBOX_EVENT_WRITE;
    void *user = this;
    trigger_cnt = 0;
    cbox_fd_event_t *event = cbox_fd_event_new(loop, fd, event_type, on_disable_event, CBOX_RUN_MODE_FOREVER, user);
    ASSERT_TRUE(event != NULL);
    this->fd_event = event;
    cbox_fd_event_enable(event);
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);
    EXPECT_EQ(trigger_cnt, 1);
    cbox_fd_event_delete(event);
}

static bool g_exception = false;

static void on_read_event(int fd, uint32_t events, void *user)
{
    FdEventTest *self = static_cast<FdEventTest *>(user);
    if (events & CBOX_EVENT_READ) {
        char data[100] = { 0};
        ssize_t len = read(fd, data, sizeof(data));
        EXPECT_EQ(len, sizeof(int));
    }

    if (events & CBOX_EVENT_EXCEPTION) {
        g_exception = true;
        cbox_loop_exit(self->loop);
    }

    cbox_loop_exit_after(self->loop, 500);
}

static void on_write_event(int fd, uint32_t events, void *user)
{
    if (events & CBOX_EVENT_WRITE) {
        int data = 0;
        ssize_t ret = write(fd, &data, sizeof(data));
        if (ret < 0) {
            printf("%s: write fd:%d, error:errno:%d, msg:%s\n", __func__, fd, errno, strerror(errno));
        }
        EXPECT_EQ(ret, sizeof(data));
        close(fd);
    }
    (void)user;
}

/// Test exception events
TEST_F(FdEventTest, Exception)
{
    int read_fd = -1, write_fd = -1;

    ASSERT_EQ(1, cbox_utils_create_pair_fd(&read_fd, &write_fd));

    ASSERT_TRUE(read_fd > 0);
    ASSERT_TRUE(write_fd > 0);

    auto read_fd_event = cbox_fd_event_new(this->loop, read_fd, CBOX_EVENT_READ | CBOX_EVENT_EXCEPTION, on_read_event, CBOX_RUN_MODE_FOREVER, this);
    auto write_fd_event = cbox_fd_event_new(this->loop, write_fd, CBOX_EVENT_WRITE | CBOX_EVENT_EXCEPTION, on_write_event, CBOX_RUN_MODE_FOREVER, this);
    ASSERT_TRUE(read_fd_event != nullptr);
    ASSERT_TRUE(write_fd_event != nullptr);

    EXPECT_EQ(0, cbox_fd_event_enable(read_fd_event));
    EXPECT_EQ(0, cbox_fd_event_enable(write_fd_event));

    cbox_loop_dispatch(this->loop, CBOX_RUN_MODE_FOREVER);

    cbox_fd_event_disable(read_fd_event);
    cbox_fd_event_disable(write_fd_event);

    cbox_fd_event_delete(read_fd_event);
    cbox_fd_event_delete(write_fd_event);
    ASSERT_TRUE(g_exception);
}

