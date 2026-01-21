#ifndef NAUTYLUS_VECTOR_H
#define NAUTYLUS_VECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nautylus/ids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nty_vector_index nty_vector_index_t;

nty_vector_index_t *nty_vector_index_create(size_t dimension, size_t capacity);
void nty_vector_index_destroy(nty_vector_index_t *index);

size_t nty_vector_index_dimension(const nty_vector_index_t *index);
size_t nty_vector_index_capacity(const nty_vector_index_t *index);
size_t nty_vector_index_count(const nty_vector_index_t *index);

bool nty_vector_index_set(nty_vector_index_t *index,
                          nty_id_t node_id,
                          const float *vector);
bool nty_vector_index_get(const nty_vector_index_t *index,
                          nty_id_t node_id,
                          float *out_vector);
bool nty_vector_index_remove(nty_vector_index_t *index, nty_id_t node_id);

/* Exact kNN over L2 squared distance; results sorted by distance then id. */
size_t nty_vector_index_knn(const nty_vector_index_t *index,
                            const float *query,
                            size_t k,
                            nty_id_t *out_ids,
                            float *out_distances);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NAUTYLUS_VECTOR_H
