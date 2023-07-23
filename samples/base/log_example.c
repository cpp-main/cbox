#include <stdio.h>
#include <libgen.h>
#include "cbox/base/log.h"


static void redirect_stdout(int level, const char *msg, void *user)
{
    fprintf(stdout, "level=%d, msg=%s\n", level, msg);
    (void)user;
}

int main(int argc, char **argv)
{
    CBOX_LOG_INIT(CBOX_LOG_LEVEL_DEBUG, basename(argv[0]));

    LOGD("This is a debug message");
    LOGI("This is an info message");
    LOGN("This is a notice message");
    LOGW("This is a warn message");
    LOGE("This is an error message");

    CBOX_LOG_REDIRECT(redirect_stdout, NULL, 0);

    LOGD("This is a debug message");
    LOGI("This is an info message");
    LOGN("This is a notice message");
    LOGW("This is a warn message");
    LOGE("This is an error message");

    CBOX_LOG_DESTROY();
    (void)argc;
    return 0;
}
