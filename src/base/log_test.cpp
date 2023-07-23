#include <gtest/gtest.h>
#include "utils.h"
#include "log.h"

class LogTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        CBOX_LOG_INIT(CBOX_LOG_LEVEL_DEBUG, "test.log");
    }

    virtual void TearDown() {
        CBOX_LOG_DESTROY();
    }

public:
    static void redirect_cb(int level, const char *msg, void *user);
    static void log_level_cb(int level, const char *msg, void *user);
    static void log_name_cb(int level, const char *msg, void *user);

protected:
    int count = 0;
}; 


void LogTest::redirect_cb(int level, const char *msg, void *user)
{
    LogTest *self = (LogTest *)user;
    switch(self->count) {
        case 0:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_DEBUG);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_debug", msg);
            }
            break;
        case 1:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_INFO);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_info", msg);
            }
            break;
        case 2:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_NOTICE);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_notice", msg);
            }
            break;
        case 3:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_ERROR);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_error", msg);
            }
            break;
        case 4:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_WARNING);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_warning", msg);
            }
            break;
        default:
            ASSERT_TRUE(false);
            break;
    }
    self->count ++;
}

void LogTest::log_level_cb(int level, const char *msg, void *user)
{
    LogTest *self = (LogTest *)user;
    switch(self->count) {
        case 0:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_ERROR);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_error", msg);
            }
            break;
        case 1:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_WARNING);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_warning", msg);
            }
            break;
        case 2:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_ERROR);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "log_error", msg);
            }
            break;
        default:
            ASSERT_TRUE(false);
            break;
    }
    self->count ++;
}

void LogTest::log_name_cb(int level, const char *msg, void *user)
{
    LogTest *self = (LogTest *)user;
    switch(self->count) {
        case 0:
            {
                EXPECT_EQ(level, CBOX_LOG_LEVEL_DEBUG);
                EXPECT_PRED_FORMAT2(testing::IsSubstring, "test_name_log_test", msg);
            }
            break;
        default:
            ASSERT_TRUE(false);
            break;
    }
    self->count ++;
}



TEST_F(LogTest, Redirect)
{
    CBOX_LOG_REDIRECT(redirect_cb, this, 0);
    LOGD("log_debug");
    LOGI("log_info");
    LOGN("log_notice");
    LOGE("log_error");
    LOGW("log_warning");

    // unset
    CBOX_LOG_REDIRECT(NULL, this, 0);
    LOGD("log_debug");
    LOGI("log_info");
    LOGN("log_notice");
    LOGW("log_warning");
    LOGE("log_error");
    EXPECT_EQ(this->count, 5);
}

TEST_F(LogTest, Level)
{
    CBOX_LOG_REDIRECT(log_level_cb, this, 0);
    CBOX_LOG_SET_LEVEL(CBOX_LOG_LEVEL_ERROR);
    LOGD("log_debug");
    LOGW("log_warning");
    LOGE("log_error");
    CBOX_LOG_SET_LEVEL(CBOX_LOG_LEVEL_WARNING);
    LOGD("log_debug");
    LOGW("log_warning");
    LOGE("log_error");
    EXPECT_EQ(this->count, 3);
}

TEST_F(LogTest, Name)
{
    CBOX_LOG_REDIRECT(log_name_cb, this, 0);
    CBOX_LOG_SET_LEVEL(CBOX_LOG_LEVEL_DEBUG);
    CBOX_LOG_SET_NAME("test_name_log_test");
    LOGD("log_debug");
    EXPECT_EQ(this->count, 1);
}
