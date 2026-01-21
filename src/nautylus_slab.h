#ifndef NAUTYLUS_SLAB_H
#define NAUTYLUS_SLAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nautylus/ids.h"

typedef struct nty_slab {
  uint8_t *data;
  uint32_t *next_free;
  uint32_t *generation;
  uint8_t *occupied;
  size_t item_size;
  uint32_t capacity;
  uint32_t next_unused;
  uint32_t free_head;
  uint32_t count;
} nty_slab_t;

bool nty_slab_init(nty_slab_t *slab, size_t item_size, uint32_t capacity);
void nty_slab_destroy(nty_slab_t *slab);

nty_id_t nty_slab_alloc(nty_slab_t *slab, void **out_ptr);
bool nty_slab_free(nty_slab_t *slab, nty_id_t id);
void *nty_slab_get(nty_slab_t *slab, nty_id_t id);
const void *nty_slab_get_const(const nty_slab_t *slab, nty_id_t id);
bool nty_slab_exists(const nty_slab_t *slab, nty_id_t id);

#endif  // NAUTYLUS_SLAB_H
