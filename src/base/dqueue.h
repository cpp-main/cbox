#ifndef _CBOX_DQUEUE_H_
#define _CBOX_DQUEUE_H_

#include "list.h"

#define DQUEUE_CREATE(head) INIT_LIST_HEAD(head)

#define DQUEUE_PUSH_FRONT(new_node, head) list_add(new_node, head)
#define DQUEUE_PUSH_BACK(new_node, head) list_add_tail(new_node, head)

#define DQUEUE_FRONT(head, type_of_struct, member_name) list_entry(((head)->next), type_of_struct, member_name)
#define DQUEUE_BACK(head, type_of_struct, member_name) list_entry(((head)->prev), type_of_struct, member_name)

#define DQUEUE_POP_FRONT(head, type_of_struct, member_name) ({\
        type_of_struct *temp = list_entry(((head)->next), type_of_struct, member_name); \
        struct list_head *list = (head)->next; \
        list_del((head)->next); \
        INIT_LIST_HEAD(list); \
        (temp); \
        })

#define DQUEUE_POP_BACK(head, type_of_struct, member_name) ({\
        type_of_struct *temp = list_entry(((head)->prev), type_of_struct, member_name); \
        struct list_head *list = (head)->prev; \
        list_del((head)->prev); \
        INIT_LIST_HEAD(list); \
        (temp); \
        })

#define DQUEUE_SIZE(head) ({ \
            struct list_head *pos; \
            int size = 0; \
            if ((head) == NULL) size = -1; \
            list_for_each(pos, (head)) { size ++; } \
            (size);\
        })

#define DQUEUE_EMPTY(head) list_empty(head)

#endif /* _CBOX_DQUEUE_H_ */
