
#include <gtest/gtest.h>
#include <unistd.h>
#include "base/utils.h"
#include "base/macros.h"
#include "loop.h"
#include "worker.h"

// Test fixture for SignalEvent tests
class WorkerTest : public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = cbox_loop_new();
        worker_ = cbox_worker_new(loop_, 3);
    }

    void TearDown() override {
        cbox_loop_delete(loop_);
        cbox_worker_delete(worker_);
    }


public:
    cbox_loop_t* loop_ = nullptr;
    cbox_worker_t *worker_ = nullptr;
    int count_ = 0;
};

static void task(void *arg)
{
    WorkerTest *self = (WorkerTest *)arg;
    self->count_ ++;
    EXPECT_EQ(cbox_utils_running_in_main(), 0);
}

static void task_done(void *arg)
{
    WorkerTest *self = (WorkerTest *)arg;
    self->count_ ++;
    EXPECT_EQ(cbox_utils_running_in_main(), 1);
    cbox_loop_exit(self->loop_);
}

TEST_F(WorkerTest, Worker) {
    ASSERT_TRUE(worker_ != nullptr);
    ASSERT_TRUE(loop_ != nullptr);
    cbox_worker_enqueue_task(worker_, task, task_done, this);
    cbox_loop_dispatch(loop_, CBOX_RUN_MODE_FOREVER);
    EXPECT_EQ(this->count_, 2);
}

static void perform_task(void *arg)
{
    WorkerTest *self = (WorkerTest *)arg;
    EXPECT_EQ(cbox_utils_running_in_main(), 0);
    self->count_ ++;
}

TEST_F(WorkerTest, Perform) {
    ASSERT_TRUE(worker_ != nullptr);
    ASSERT_TRUE(loop_ != nullptr);
    cbox_perform_task_t *task = cbox_worker_perform_task(perform_task, this);
    ASSERT_TRUE(task != nullptr);
    cbox_worker_perform_wait_task_done(task);
    EXPECT_EQ(this->count_, 1);
}

