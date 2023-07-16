# ifndef _C_LOOP_H_20230703_
#define _C_LOOP_H_20230703_

#include <stdint.h>
#include <stddef.h>

#if defined (__cplusplus)
extern "C" {
#endif

#define CBOX_RUN_MODE_ONCE (0)
#define CBOX_RUN_MODE_FOREVER (1)

typedef struct cbox_loop cbox_loop_t;
typedef struct cbox_basic_timer cbox_basic_timer_t;

typedef void (*cbox_run_in_loop_func_t)(void * /*user*/);
typedef void (*cbox_timeout_func_t)(void * /*user*/);

//loop
cbox_loop_t *cbox_loop_new();
void cbox_loop_delete(cbox_loop_t *);

void cbox_loop_dispatch(cbox_loop_t * /*cbox_loop*/, uint32_t /*mode*/);
void cbox_loop_delegate(cbox_loop_t * /*cbox_loop*/, cbox_run_in_loop_func_t /*cb*/, void * /*user*/);
void cbox_loop_exit(cbox_loop_t *);
void cbox_loop_exit_after(cbox_loop_t *, uint64_t /*miliseconds*/);

// basic timer
cbox_basic_timer_t *cbox_basic_timer_new(uint64_t /*miliseconds*/, int /*repeat*/, cbox_timeout_func_t /* timeout handler*/, void * /*user*/);
void cbox_basic_timer_delete(cbox_basic_timer_t * /*timer*/);
int cbox_basic_timer_enable(cbox_loop_t * /*loop*/, cbox_basic_timer_t * /*timer*/);
int cbox_basic_timer_disable(cbox_loop_t * /*loop*/, cbox_basic_timer_t * /*timer*/);

#if defined (__cplusplus)
}
#endif

#endif
