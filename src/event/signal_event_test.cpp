#include <gtest/gtest.h>
#include <time.h>
#include "signal_event.h"
#include "timer.h"
#include "base/utils.h"
#include "base/macros.h"

// Test fixture for SignalEvent tests
class SignalEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = cbox_loop_new();
    }

    void TearDown() override {
        cbox_loop_delete(loop_);
    }


public:
    cbox_loop_t* loop_ = nullptr;
    cbox_timer_t *timer_ = nullptr;
    cbox_signal_event_t *se_ = nullptr;
    int sigint_cnt_ = 0;
    int sigquit_cnt_ = 0;
    int siguser1_cnt_ = 0;
    int siguser2_cnt_ = 0;
    int sigalarm_cnt_ = 0;
};

static void on_signal(int signo, void *user)
{
    SignalEventTest *self = (SignalEventTest *)user;
    if (signo == SIGINT) {
        self->sigint_cnt_ ++;
    } else if (signo == SIGQUIT) {
        self->sigquit_cnt_ ++;
    } else if (signo == SIGUSR1) {
        self->siguser1_cnt_ ++;
    } else if (signo == SIGUSR2) {
        self->siguser2_cnt_ ++;
    } else if (signo == SIGALRM) {
        self->sigalarm_cnt_ ++;
    }

    if (self->sigalarm_cnt_ >= 5)
        cbox_loop_exit(self->loop_);
}

static void on_signal_timeout(void *user)
{
    int signo[] = { SIGINT, SIGQUIT, SIGUSR1, SIGUSR2, SIGALRM };
    for (size_t i = 0; i < CBOX_ARRAY_SIZE(signo); ++i) {
        int pid = getpid();
        int sig = signo[i];
        kill(pid, sig);
    }

    (void)user;
}

TEST_F(SignalEventTest, Signal) {
    int signo[] = { SIGINT, SIGQUIT, SIGUSR1, SIGUSR2, SIGALRM };
    se_  = cbox_signal_event_new(loop_, signo, CBOX_ARRAY_SIZE(signo), on_signal, CBOX_RUN_MODE_FOREVER, this);
    ASSERT_TRUE(se_ != NULL);
    struct timespec ts  = { .tv_sec = 0, .tv_nsec = (50 * 1000000) };
    timer_ = cbox_timer_new(loop_, &ts, 5, on_signal_timeout, this);
    ASSERT_TRUE(timer_ != NULL);

    cbox_timer_enable(timer_);
    cbox_loop_exit_after(loop_, 5000);
    cbox_loop_dispatch(loop_, CBOX_RUN_MODE_FOREVER);
    cbox_timer_disable(timer_);
    CBOX_SAFETY_FUNC(cbox_signal_event_delete, se_);
    CBOX_SAFETY_FUNC(cbox_timer_delete, timer_);
    EXPECT_EQ(this->sigint_cnt_, 5);
    EXPECT_EQ(this->sigquit_cnt_, 5);
    EXPECT_EQ(this->sigalarm_cnt_, 5);
    EXPECT_EQ(this->siguser1_cnt_, 5);
    EXPECT_EQ(this->siguser2_cnt_, 5);
}

