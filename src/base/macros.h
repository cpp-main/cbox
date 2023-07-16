#ifndef _CBOX_MACROS_H_
#define _CBOX_MACROS_H_

#ifndef CBOX_SAFETY_FREE
#define CBOX_SAFETY_FREE(x) do { if (x) { free(x); x = NULL; } }while (0)
#endif

#ifndef CBOX_SAFETY_DELETE
#define CBOX_SAFETY_DELETE(x) do { if (x) { delete x; x = nullptr; } } while (0)
#endif

#ifndef CBOX_SAFETY_FUNC
#define CBOX_SAFETY_FUNC(f, a) do { if (a) { f(a); a = NULL; } } while (0)
#endif

#ifndef CBOX_ARRAY_SIZE
#define CBOX_ARRAY_SIZE(x) sizeof(x) / sizeof(x[0])
#endif


#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression)         \
(__extension__                                  \
({ long int __result;                           \
  do __result = (long int) (expression);        \
  while (__result == -1L && errno == EINTR);    \
  __result; }))
#endif

#define CBOX_CURRENT_CLOCK_MILLISECONDS() ({ struct timespec ts; (clock_gettime(CLOCK_MONOTONIC, &ts) == -1 ? 0 : (ts.tv_sec * 1000 + ts.tv_nsec / 1000000)); })

#endif
