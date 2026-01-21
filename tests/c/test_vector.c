#include <stdio.h>
#include <stdlib.h>

#include "nautylus/vector.h"

#define ASSERT_OK(cond)                                                  \
  do {                                                                   \
    if (!(cond)) {                                                       \
      fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__,        \
              __LINE__, #cond);                                          \
      return 1;                                                          \
    }                                                                    \
  } while (0)

static int test_set_get_update(void) {
  nty_vector_index_t *index = nty_vector_index_create(3, 4);
  ASSERT_OK(index != NULL);

  float v1[3] = {1.0f, 2.0f, 3.0f};
  float v2[3] = {4.0f, 5.0f, 6.0f};
  ASSERT_OK(nty_vector_index_set(index, 10, v1));
  ASSERT_OK(nty_vector_index_set(index, 10, v2));

  float out[3] = {0.0f, 0.0f, 0.0f};
  ASSERT_OK(nty_vector_index_get(index, 10, out));
  ASSERT_OK(out[0] == 4.0f && out[1] == 5.0f && out[2] == 6.0f);

  nty_vector_index_destroy(index);
  return 0;
}

static int test_knn_tiebreak(void) {
  nty_vector_index_t *index = nty_vector_index_create(2, 4);
  ASSERT_OK(index != NULL);

  float v0[2] = {0.0f, 0.0f};
  float v1[2] = {1.0f, 0.0f};
  float v2[2] = {-1.0f, 0.0f};
  ASSERT_OK(nty_vector_index_set(index, 1, v0));
  ASSERT_OK(nty_vector_index_set(index, 3, v1));
  ASSERT_OK(nty_vector_index_set(index, 2, v2));

  nty_id_t ids[3] = {0, 0, 0};
  float dist[3] = {0.0f, 0.0f, 0.0f};
  float query[2] = {0.0f, 0.0f};
  size_t count = nty_vector_index_knn(index, query, 3, ids, dist);
  ASSERT_OK(count == 3);
  ASSERT_OK(ids[0] == 1);
  ASSERT_OK(ids[1] == 2);
  ASSERT_OK(ids[2] == 3);
  ASSERT_OK(dist[0] == 0.0f);
  ASSERT_OK(dist[1] == 1.0f);
  ASSERT_OK(dist[2] == 1.0f);

  nty_vector_index_destroy(index);
  return 0;
}

static int test_remove(void) {
  nty_vector_index_t *index = nty_vector_index_create(2, 4);
  ASSERT_OK(index != NULL);

  float v0[2] = {1.0f, 2.0f};
  ASSERT_OK(nty_vector_index_set(index, 42, v0));
  ASSERT_OK(nty_vector_index_count(index) == 1);
  ASSERT_OK(nty_vector_index_remove(index, 42));
  ASSERT_OK(nty_vector_index_count(index) == 0);

  float out[2] = {0.0f, 0.0f};
  ASSERT_OK(!nty_vector_index_get(index, 42, out));

  nty_vector_index_destroy(index);
  return 0;
}

int main(void) {
  ASSERT_OK(test_set_get_update() == 0);
  ASSERT_OK(test_knn_tiebreak() == 0);
  ASSERT_OK(test_remove() == 0);
  printf("vector tests passed\n");
  return 0;
}
