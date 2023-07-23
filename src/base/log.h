#ifndef _CBOX_LOG_H__
#define _CBOX_LOG_H__

#include <stdint.h>
#include <string.h>

#define __FILENAME__        strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__

typedef enum {
    CBOX_LOG_LEVEL_EMERG = 0,
    CBOX_LOG_LEVEL_ALERT,
    CBOX_LOG_LEVEL_CRIT,
    CBOX_LOG_LEVEL_ERROR,
    CBOX_LOG_LEVEL_WARNING,
    CBOX_LOG_LEVEL_NOTICE,
    CBOX_LOG_LEVEL_INFO,
    CBOX_LOG_LEVEL_DEBUG
} cbox_level_t;

typedef void (*cbox_log_function_t)(int, const char *, void *);
typedef struct cbox_log_s cbox_log_t;

extern cbox_log_t *cbox_log_instance;

#if defined (__cplusplus)
extern "C" {
#endif

cbox_log_t *cbox_log_init(cbox_level_t level, const char *name);
void cbox_log_destroy();
void cbox_log_redirect(cbox_log_t *log, cbox_log_function_t cb, void *obj, int copy);

void cbox_log_set_name(cbox_log_t *log, const char *name);
void cbox_log_set_level(cbox_log_t *log, cbox_level_t level);
void cbox_log_set_color(cbox_log_t *log, int color /*bool*/);

const char *cbox_log_get_category(cbox_log_t *log, int level, int color);
void cbox_log_set_syslog(cbox_log_t *log, int syslog);
void cbox_log_set_result(cbox_log_t *log, int result);
void cbox_log_set_format(cbox_log_t *log, int prefix, const char *fmt, ...);

void cbox_log_raw(cbox_log_t *log, int level, const char *fmt, ...);
void cbox_log_basic(cbox_log_t *log, int level, const char *fmt, ...);
void cbox_log_full(cbox_log_t *log, int level, const char *fmt, ...);
void cbox_log_dump(cbox_log_t *log, const void *buf, uint16_t len);
void cbox_log_dump_with_tag(cbox_log_t *log, const char *tag, const void *buf, uint16_t len);

#if defined (__cplusplus)
}
#endif

#define CBOX_LOG_INIT(l, n)                     do { cbox_log_instance = cbox_log_init(l, n); } while(0)
#define CBOX_LOG_DESTROY()                      do { cbox_log_destroy(); } while(0)
#define CBOX_LOG_REDIRECT(cb, obj, copy)        do { cbox_log_redirect(cbox_log_instance, cb, obj, copy); } while(0)
#define CBOX_LOG_SET_LEVEL(level)               do { cbox_log_set_level(cbox_log_instance, level); } while(0)
#define CBOX_LOG_SET_NAME(n)                    do { cbox_log_set_name(cbox_log_instance, n); } while(0)
#define CBOX_LOG_SET_COLOR(c)                   do { cbox_log_set_color(cbox_log_instance, c); } while(0)
#define CBOX_LOG_SET_REULT(result)              do { cbox_log_set_result(cbox_log_instance, result); } while(0)
#define CBOX_LOG_SET_SYSLOG(syslog)             do { cbox_log_set_syslog(cbox_log_instance, syslog); } while(0)
#define CBOX_LOG_GET_CATEGORY(level, color)     cbox_log_get_category(cbox_log_instance, level, color)

#define LOGM(fmt, ...)                  do { cbox_log_set_format(cbox_log_instance, 1, ""); cbox_log_set_format(cbox_log_instance, 0, ""); cbox_log_full(cbox_log_instance, CBOX_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__); } while(0)
#define LOGF(level, fmt, ...)           do { cbox_log_set_format(cbox_log_instance, 1, "%s: ", __FUNCTION__); cbox_log_set_format(cbox_log_instance, 0, " (%s:%d)", __FILENAME__, __LINE__); cbox_log_full(cbox_log_instance, level, fmt, ##__VA_ARGS__); } while(0)

#define LOGD(fmt, ...)                  LOGF(CBOX_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...)                  LOGF(CBOX_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOGN(fmt, ...)                  LOGF(CBOX_LOG_LEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...)                  LOGF(CBOX_LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...)                  LOGF(CBOX_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#define LOGM_WITH_RETURN(ret, fmt, ...)         do { LOGM(fmt, ##__VA_ARGS__); return ret; } while(0)

#define LOGD_WITH_RETURN(ret, fmt, ...)         do { LOGD(fmt, ##__VA_ARGS__); return ret; } while(0)
#define LOGI_WITH_RETURN(ret, fmt, ...)         do { LOGI(fmt, ##__VA_ARGS__); return ret; } while(0)
#define LOGN_WITH_RETURN(ret, fmt, ...)         do { LOGN(fmt, ##__VA_ARGS__); return ret; } while(0)
#define LOGW_WITH_RETURN(ret, fmt, ...)         do { LOGW(fmt, ##__VA_ARGS__); return ret; } while(0)
#define LOGE_WITH_RETURN(ret, fmt, ...)         do { LOGE(fmt, ##__VA_ARGS__); return ret; } while(0)

#define LOGD_WITH_GOTO(label, fmt, ...)         do { LOGD(fmt, ##__VA_ARGS__); goto label; } while(0)
#define LOGI_WITH_GOTO(label, fmt, ...)         do { LOGI(fmt, ##__VA_ARGS__); goto label; } while(0)
#define LOGN_WITH_GOTO(label, fmt, ...)         do { LOGN(fmt, ##__VA_ARGS__); goto label; } while(0)
#define LOGW_WITH_GOTO(label, fmt, ...)         do { LOGW(fmt, ##__VA_ARGS__); goto label; } while(0)
#define LOGE_WITH_GOTO(label, fmt, ...)         do { LOGE(fmt, ##__VA_ARGS__); goto label; } while(0)

#define LOG_HEX_DUMP(x, y)                      do { cbox_log_dump(cbox_log_instance, x, y); } while(0)

#define LOG_HEX_DUMP_WITH_TAG(x, y, z)          do { cbox_log_set_format(cbox_log_instance, 1, "%s: ", __FUNCTION__); cbox_log_set_format(cbox_log_instance, 0, " (%s:%d)", __FILENAME__, __LINE__); cbox_log_dump_with_tag(cbox_log_instance, x, y, z); } while(0)
#define LOG_EXP_CHECK(exp, ok, fmt, ...)        do { typeof(exp) ret; ret = (exp); if (ret != ok) { cbox_log_set_result(cbox_log_instance, ret); LOGW(fmt, ##__VA_ARGS__); } } while(0)
#define LOG_EXP_VERIFY(exp, ok)                 do { typeof(exp) ret; ret = (exp); if (ret != ok) { LOGW("%s failure [errno:%d]", #exp, ret); } } while(0)

#endif //_CBOX_LOG_H__

