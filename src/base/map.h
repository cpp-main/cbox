#ifndef _CBOX_MAP_H_202307_
#define _CBOX_MAP_H_202307_
#include <stddef.h>

#define CBOX_MMAP_TYPE_RB_TREE  (1)
#define CBOX_MMAP_TYPE_AVL_TREE (1)
#define CBOX_MMAP_TYPE_HASH (2)

typedef struct cbox_map cbox_map_t;

cbox_map_t *cbox_map_new(int type);
void cbox_map_delete(cbox_map_t *map);

int cbox_map_insert(cbox_map_t *map, void *key, size_t key_len, void *value, size_t value_len);
void *cbox_map_erase(cbox_map_t *map, void *key, size_t key_len, size_t *value_len_ptr);
void *cbox_map_emplace(cbox_map_t *map, void *key, size_t key_len, void *value, size_t value_len, size_t *old_value_len);
void cbox_map_clear(cbox_map_t *map);

int cbox_map_empty(cbox_map_t *map);
size_t cbox_map_size(cbox_map_t *map);

#endif
