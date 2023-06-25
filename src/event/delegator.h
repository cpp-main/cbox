#ifndef _CBOX_DELEGATOR_H_
#define _CBOX_DELEGATOR_H_

#include "loop.h"

#if defined (__cplusplus)
extern "C" {
#endif
typedef struct cbox_delegator cbox_delegator_t;

cbox_delegator_t *cbox_delegator_new(cbox_loop_t * /*loop*/);
void cbox_delegator_delete(cbox_delegator_t * /*delegator*/);

void cbox_delegator_delegate(cbox_delegator_t *, cbox_run_in_loop_func_t, void *user);

#if defined (__cplusplus)
}
#endif

#endif
