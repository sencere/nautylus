#include "nautylus/vector.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct nty_vector_index {
  size_t dimension;
  size_t capacity;
  size_t count;
  nty_id_t *ids;
  float *data;
};

nty_vector_index_t *nty_vector_index_create(size_t dimension,
                                            size_t capacity) {
  if (dimension == 0 || capacity == 0) {
    return NULL;
  }
  nty_vector_index_t *index =
      (nty_vector_index_t *)calloc(1, sizeof(nty_vector_index_t));
  if (!index) {
    return NULL;
  }
  index->ids = (nty_id_t *)calloc(capacity, sizeof(nty_id_t));
  index->data = (float *)calloc(capacity * dimension, sizeof(float));
  if (!index->ids || !index->data) {
    nty_vector_index_destroy(index);
    return NULL;
  }
  index->dimension = dimension;
  index->capacity = capacity;
  index->count = 0;
  return index;
}

void nty_vector_index_destroy(nty_vector_index_t *index) {
  if (!index) {
    return;
  }
  free(index->ids);
  free(index->data);
  free(index);
}

size_t nty_vector_index_dimension(const nty_vector_index_t *index) {
  return index ? index->dimension : 0;
}

size_t nty_vector_index_capacity(const nty_vector_index_t *index) {
  return index ? index->capacity : 0;
}

size_t nty_vector_index_count(const nty_vector_index_t *index) {
  return index ? index->count : 0;
}

static size_t nty_vector_index_find(const nty_vector_index_t *index,
                                    nty_id_t node_id) {
  for (size_t i = 0; i < index->capacity; i++) {
    if (index->ids[i] == node_id) {
      return i;
    }
  }
  return index->capacity;
}

bool nty_vector_index_set(nty_vector_index_t *index,
                          nty_id_t node_id,
                          const float *vector) {
  if (!index || !vector || node_id == 0) {
    return false;
  }
  size_t slot = nty_vector_index_find(index, node_id);
  if (slot == index->capacity) {
    for (size_t i = 0; i < index->capacity; i++) {
      if (index->ids[i] == 0) {
        slot = i;
        index->ids[i] = node_id;
        index->count++;
        break;
      }
    }
  }
  if (slot == index->capacity) {
    return false;
  }
  memcpy(index->data + slot * index->dimension,
         vector,
         index->dimension * sizeof(float));
  return true;
}

bool nty_vector_index_get(const nty_vector_index_t *index,
                          nty_id_t node_id,
                          float *out_vector) {
  if (!index || !out_vector || node_id == 0) {
    return false;
  }
  size_t slot = nty_vector_index_find(index, node_id);
  if (slot == index->capacity) {
    return false;
  }
  memcpy(out_vector,
         index->data + slot * index->dimension,
         index->dimension * sizeof(float));
  return true;
}

bool nty_vector_index_remove(nty_vector_index_t *index, nty_id_t node_id) {
  if (!index || node_id == 0) {
    return false;
  }
  size_t slot = nty_vector_index_find(index, node_id);
  if (slot == index->capacity) {
    return false;
  }
  index->ids[slot] = 0;
  memset(index->data + slot * index->dimension,
         0,
         index->dimension * sizeof(float));
  index->count--;
  return true;
}

typedef struct nty_candidate {
  nty_id_t id;
  float distance;
} nty_candidate_t;

static int nty_candidate_cmp(const void *a, const void *b) {
  const nty_candidate_t *left = (const nty_candidate_t *)a;
  const nty_candidate_t *right = (const nty_candidate_t *)b;
  if (left->distance < right->distance) {
    return -1;
  }
  if (left->distance > right->distance) {
    return 1;
  }
  if (left->id < right->id) {
    return -1;
  }
  if (left->id > right->id) {
    return 1;
  }
  return 0;
}

size_t nty_vector_index_knn(const nty_vector_index_t *index,
                            const float *query,
                            size_t k,
                            nty_id_t *out_ids,
                            float *out_distances) {
  if (!index || !query || (!out_ids && !out_distances) || k == 0) {
    return 0;
  }

  nty_candidate_t *candidates =
      (nty_candidate_t *)calloc(index->count, sizeof(nty_candidate_t));
  if (!candidates && index->count > 0) {
    return 0;
  }

  size_t used = 0;
  for (size_t i = 0; i < index->capacity; i++) {
    nty_id_t id = index->ids[i];
    if (id == 0) {
      continue;
    }
    float dist = 0.0f;
    const float *vec = index->data + i * index->dimension;
    for (size_t d = 0; d < index->dimension; d++) {
      float diff = vec[d] - query[d];
      dist += diff * diff;
    }
    if (isnan(dist)) {
      dist = INFINITY;
    }
    candidates[used].id = id;
    candidates[used].distance = dist;
    used++;
  }

  qsort(candidates, used, sizeof(nty_candidate_t), nty_candidate_cmp);

  size_t out_count = k < used ? k : used;
  for (size_t i = 0; i < out_count; i++) {
    if (out_ids) {
      out_ids[i] = candidates[i].id;
    }
    if (out_distances) {
      out_distances[i] = candidates[i].distance;
    }
  }

  free(candidates);
  return out_count;
}
