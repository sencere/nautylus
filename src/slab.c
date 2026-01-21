#include "nautylus_slab.h"

#include <stdlib.h>
#include <string.h>

enum { NTY_SLAB_NO_FREE = 0xFFFFFFFFu };

bool nty_slab_init(nty_slab_t *slab, size_t item_size, uint32_t capacity) {
  if (!slab || item_size == 0 || capacity == 0) {
    return false;
  }

  slab->data = (uint8_t *)calloc(capacity, item_size);
  slab->next_free = (uint32_t *)calloc(capacity, sizeof(uint32_t));
  slab->generation = (uint32_t *)calloc(capacity, sizeof(uint32_t));
  slab->occupied = (uint8_t *)calloc(capacity, sizeof(uint8_t));
  if (!slab->data || !slab->next_free || !slab->generation || !slab->occupied) {
    nty_slab_destroy(slab);
    return false;
  }

  slab->item_size = item_size;
  slab->capacity = capacity;
  slab->next_unused = 0;
  slab->free_head = NTY_SLAB_NO_FREE;
  slab->count = 0;
  return true;
}

void nty_slab_destroy(nty_slab_t *slab) {
  if (!slab) {
    return;
  }
  free(slab->data);
  free(slab->next_free);
  free(slab->generation);
  free(slab->occupied);
  slab->data = NULL;
  slab->next_free = NULL;
  slab->generation = NULL;
  slab->occupied = NULL;
  slab->item_size = 0;
  slab->capacity = 0;
  slab->next_unused = 0;
  slab->free_head = NTY_SLAB_NO_FREE;
  slab->count = 0;
}

static uint32_t nty_slab_next_index(nty_slab_t *slab) {
  if (slab->free_head != NTY_SLAB_NO_FREE) {
    uint32_t index = slab->free_head;
    slab->free_head = slab->next_free[index];
    return index;
  }
  if (slab->next_unused < slab->capacity) {
    return slab->next_unused++;
  }
  return NTY_SLAB_NO_FREE;
}

nty_id_t nty_slab_alloc(nty_slab_t *slab, void **out_ptr) {
  if (!slab || !out_ptr) {
    return 0;
  }

  uint32_t index = nty_slab_next_index(slab);
  if (index == NTY_SLAB_NO_FREE) {
    return 0;
  }

  uint32_t generation = slab->generation[index];
  generation = generation == 0 ? 1 : generation + 1;
  slab->generation[index] = generation;
  slab->occupied[index] = 1;
  slab->count++;

  void *ptr = slab->data + ((size_t)index * slab->item_size);
  memset(ptr, 0, slab->item_size);
  *out_ptr = ptr;

  nty_id_t id = ((nty_id_t)generation << NTY_ID_INDEX_BITS) |
                (nty_id_t)(index + 1);
  return id;
}

bool nty_slab_free(nty_slab_t *slab, nty_id_t id) {
  if (!slab) {
    return false;
  }
  uint32_t index = nty_id_index(id);
  if (index == UINT32_MAX || index >= slab->capacity) {
    return false;
  }
  if (!slab->occupied[index]) {
    return false;
  }
  if (slab->generation[index] != nty_id_generation(id)) {
    return false;
  }

  void *ptr = slab->data + ((size_t)index * slab->item_size);
  memset(ptr, 0, slab->item_size);
  slab->occupied[index] = 0;
  slab->next_free[index] = slab->free_head;
  slab->free_head = index;
  slab->count--;
  return true;
}

void *nty_slab_get(nty_slab_t *slab, nty_id_t id) {
  if (!slab) {
    return NULL;
  }
  if (!nty_slab_exists(slab, id)) {
    return NULL;
  }
  uint32_t index = nty_id_index(id);
  return slab->data + ((size_t)index * slab->item_size);
}

const void *nty_slab_get_const(const nty_slab_t *slab, nty_id_t id) {
  if (!slab) {
    return NULL;
  }
  if (!nty_slab_exists(slab, id)) {
    return NULL;
  }
  uint32_t index = nty_id_index(id);
  return slab->data + ((size_t)index * slab->item_size);
}

bool nty_slab_exists(const nty_slab_t *slab, nty_id_t id) {
  if (!slab) {
    return false;
  }
  uint32_t index = nty_id_index(id);
  if (index == UINT32_MAX || index >= slab->capacity) {
    return false;
  }
  if (!slab->occupied[index]) {
    return false;
  }
  return slab->generation[index] == nty_id_generation(id);
}
