#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <syscall.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

#include "log.h"
#include "macros.h"

#define DEFAULT_CBOX_LOG_NAME "cbox_log"

#define COLOR_RED(s)       "\033[41m\033[37m" s "\033[0m"
#define COLOR_BLUE(s)      "\033[44m\033[37m" s "\033[0m"
#define COLOR_GREEN(s)     "\033[42m\033[37m" s "\033[0m"
#define COLOR_YELLOW(s)    "\033[43m\033[30m" s "\033[0m"
#define COLOR_WHITE(s)     "\033[47m\033[30m" s "\033[0m"
#define COLOR_PURPLE(s)    "\033[45m\033[37m" s "\033[0m"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct cbox_log_s
{
    cbox_log_function_t cb;
    int copy;

    int level;

    int result;

    int syslog;

    char *name;

    char prefix[128];
    char suffix[128];

    int color;  //!< default is 1

    void *user;
};

cbox_log_t *cbox_log_instance = NULL;

static int cbox_vasprintf(char **strp, const char* fmt, va_list args);
static int cbox_asprintf(char **strp, const char *fmt, ...);

cbox_log_t *cbox_log_init(cbox_level_t level, const char *name)
{
    pthread_mutex_lock(&g_lock);

    if (cbox_log_instance == NULL) {
        cbox_log_instance = (cbox_log_t *)malloc(sizeof(cbox_log_t));
        memset(cbox_log_instance, 0x00, sizeof(cbox_log_t));
        cbox_log_instance->level = level;
        if (name) cbox_log_instance->name = strdup(name);

        cbox_log_instance->prefix[0] = cbox_log_instance->suffix[0] = '\0';
        cbox_log_instance->color = 1;
        cbox_log_instance->copy = 0;
    }

    pthread_mutex_unlock(&g_lock);
    return cbox_log_instance;
}

void cbox_log_destroy()
{
    pthread_mutex_lock(&g_lock);

    if (cbox_log_instance != NULL) {
        CBOX_SAFETY_FREE(cbox_log_instance->name);
        if (cbox_log_instance->syslog)
            closelog();

        CBOX_SAFETY_FREE(cbox_log_instance);
    }

    pthread_mutex_unlock(&g_lock);
}

void cbox_log_redirect(cbox_log_t *log, cbox_log_function_t cb, void *obj, int copy)
{
    pthread_mutex_lock(&g_lock);
    log->cb = cb;
    log->user = obj;
    log->copy = copy;
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_set_name(cbox_log_t *log, const char *name)
{
    pthread_mutex_lock(&g_lock);
    CBOX_SAFETY_FREE(log->name);
    log->name = strdup(name);
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_set_level(cbox_log_t *log, cbox_level_t level)
{
    pthread_mutex_lock(&g_lock);
    log->level = level;
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_set_color(cbox_log_t *log, int color)
{
    pthread_mutex_lock(&g_lock);
    log->color = color;
    pthread_mutex_unlock(&g_lock);
}

const char *cbox_log_get_category(cbox_log_t *log, int level, int color)
{
    static const char *blank_levels[] = { "E", "A", "C", "E", "W", "N", "I", "D" };
    static const char *color_levels[] = { COLOR_RED(" E "), COLOR_RED(" A "), COLOR_RED(" C "), COLOR_RED(" E "),
        COLOR_YELLOW(" W "), COLOR_PURPLE(" N "), COLOR_BLUE(" I "), COLOR_GREEN(" D ") };

    (void) log;
    return color ? color_levels[level] : blank_levels[level];
}

void cbox_log_set_syslog(cbox_log_t *log, int syslog)
{
    pthread_mutex_lock(&g_lock);

    log->syslog = syslog;

    if (log->syslog)
        openlog(log->name ? log->name : DEFAULT_CBOX_LOG_NAME, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);    
    else
        closelog();

    pthread_mutex_unlock(&g_lock);
}

void cbox_log_set_result(cbox_log_t *log, int result)
{
    pthread_mutex_lock(&g_lock);
    log->result = result;
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_set_format(cbox_log_t *log, int prefix, const char *fmt, ...)
{
    pthread_mutex_lock(&g_lock);
    va_list ap;
    va_start(ap, fmt);
    vsprintf(prefix ? log->prefix : log->suffix, fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_raw(cbox_log_t *log, int level, const char *fmt, ...)
{
    char *msg = NULL;
    if (level < log->level)
        return;

    pthread_mutex_lock(&g_lock);

    va_list ap;
    va_start(ap, fmt);
    cbox_vasprintf(&msg, fmt, ap);
    va_end(ap);

    if (log->cb) {
        (*log->cb)(level, msg, log->user);

        if (!log->copy) goto CLEANUP;
    }

    if (!log->syslog) fprintf(stdout, "%s\n", msg);
    else syslog(LOG_USER | LOG_INFO, "%s", msg);

CLEANUP:
    CBOX_SAFETY_FREE(msg);
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_full(cbox_log_t *log, int level, const char *fmt, ...)
{
    if (level > log->level) return;

    pthread_mutex_lock(&g_lock);

    char *msg = NULL;

    va_list ap;
    va_start(ap, fmt);
    cbox_vasprintf(&msg, fmt, ap);
    va_end(ap);

    char result[32] = { 0 };
    if (log->result != 0) sprintf(result, " [errno:%d]", log->result);

    struct timeval tv;
    struct tm tm;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    char fmt_str[32];
    strftime(fmt_str, sizeof(fmt_str), "%Y-%m-%d %H:%M:%S", &tm);

    if (log->cb) {
        char *log_str;
        cbox_asprintf(&log_str, "[%s:%03ld] %s %s [%ld]: %s%s%s%s", fmt_str, tv.tv_usec / 1000, cbox_log_get_category(log, level, 0), log->name ? log->name : DEFAULT_CBOX_LOG_NAME, syscall(SYS_gettid), log->prefix, msg, log->suffix, result);
        (*log->cb)(level, log_str, log->user);
        CBOX_SAFETY_FREE(log_str);

        if (!log->copy) goto CLEANUP;
    }

    if (!log->syslog)
        fprintf(stdout, "[%s:%03ld] %s %s [%ld]: %s%s%s%s\n", fmt_str, tv.tv_usec / 1000, cbox_log_get_category(log, level, log->color), log->name ? log->name : DEFAULT_CBOX_LOG_NAME, syscall(SYS_gettid), log->prefix, msg, log->suffix, result);
    else
        syslog(LOG_USER | LOG_INFO, "%s%s%s%s", log->prefix, msg, log->suffix, result);

CLEANUP:
    CBOX_SAFETY_FREE(msg);

    log->result = 0;
    pthread_mutex_unlock(&g_lock);
}

void cbox_log_basic(cbox_log_t *log, int level, const char *fmt, ...)
{
    if (level > log->level) return;
    char *msg = NULL;

    pthread_mutex_lock(&g_lock);
    va_list ap;
    va_start(ap, fmt);
    cbox_vasprintf(&msg, fmt, ap);
    va_end(ap);

    char result[32] = { 0 };
    if (log->result != 0) sprintf(result, " [errno:%d]", log->result);

    if (log->cb) {
        char *log_str;
        cbox_asprintf(&log_str, "%s %s [%ld] %s%s%s%s", cbox_log_get_category(log, level, 0), log->name ? log->name : DEFAULT_CBOX_LOG_NAME, syscall(SYS_gettid), log->prefix, msg, log->suffix, result);
        (*log->cb)(level, log_str, log->user);
        CBOX_SAFETY_FREE(log_str);

        if (!log->copy) goto CLEANUP;
    }

    if (!log->syslog)
        fprintf(stdout, "%s %s [%ld] %s%s%s%s\n", cbox_log_get_category(log, level, log->color), log->name ? log->name : DEFAULT_CBOX_LOG_NAME, syscall(SYS_gettid), log->prefix, msg, log->suffix, result);
    else
        syslog(LOG_USER | LOG_INFO, "%s%s%s%s", log->prefix, msg, log->suffix, result);

CLEANUP:
    if (msg) free(msg);

    log->result = 0;
    pthread_mutex_unlock(&g_lock);
}

static void bin_to_hex(char *hex, const uint8_t *bin, size_t bin_len, int space, int upper)
{
    size_t i = 0, j = 0;
    for (i = 0, j = 0; i < bin_len; i++) {
        sprintf(hex + i * 2 + j, upper ? "%02X" : "%02x", bin[i]);

        if (!space)  continue;

        strcat((char *)hex, " ");
        j++;
    }
}

void cbox_log_dump(cbox_log_t *log, const void *buf, uint16_t len)
{
    if (CBOX_LOG_LEVEL_DEBUG > log->level) return;

    pthread_mutex_lock(&g_lock);
    char prefix[64];
    sprintf(prefix, "%s [%ld]", log->name ? log->name : DEFAULT_CBOX_LOG_NAME, syscall(SYS_gettid));
    const size_t padding = 18 + strlen(prefix) + 1;

    char hex[128] = { 0 }, str[32] = { 0 };

    memset(hex, 0x20, sizeof(hex));

    size_t i = 0, index = 0;
    const size_t length = 16;

    while (index < len) {
        size_t line_len = len - index > length ? length : len - index;
        bin_to_hex(hex + padding, (uint8_t *)buf + index, line_len, 1, 0);
        if (line_len < length) for (i = 0; i < length - line_len; i++) strcat(hex, "   ");
        memcpy(str, (uint8_t *)buf + index, line_len);
        for (i = 0; i < length; i++) if ((uint8_t)str[i] < 0x20 || (uint8_t)str[i] > 0x7E) str[i] = 0x2E;
        str[line_len] = '\0';
        index += line_len;

        cbox_log_raw(log, CBOX_LOG_LEVEL_DEBUG, "%s\t\t%s", hex, str);
    }

    pthread_mutex_unlock(&g_lock);
}

void cbox_log_dump_with_tag(cbox_log_t *log, const char *tag, const void *buf, uint16_t len)
{
    cbox_log_full(log, CBOX_LOG_LEVEL_DEBUG, "%s: %d(bytes)", tag, len);
    cbox_log_dump(log, buf, len);
}

int cbox_vasprintf(char **strp, const char* fmt, va_list args)
{
    int r = -1;
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, fmt, args);

    *strp = malloc(len + 1);
    if(!*strp) {
        return -1; 
    }

    r = vsnprintf(*strp, len + 1, fmt, args_copy);

    va_end(args_copy);

    return r;  
}


static int cbox_asprintf(char **strp, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int r = cbox_vasprintf(strp, fmt, args);

    va_end(args);

    return r;
}

