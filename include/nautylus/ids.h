#ifndef NAUTYLUS_IDS_H
#define NAUTYLUS_IDS_H

#include <stdint.h>

typedef uint64_t nty_id_t;

enum {
  NTY_ID_INDEX_BITS = 32,
  NTY_ID_INDEX_MASK = 0xFFFFFFFFu
};

static inline uint32_t nty_id_index(nty_id_t id) {
  uint32_t raw = (uint32_t)(id & NTY_ID_INDEX_MASK);
  return raw == 0 ? UINT32_MAX : (raw - 1);
}

static inline uint32_t nty_id_generation(nty_id_t id) {
  return (uint32_t)(id >> NTY_ID_INDEX_BITS);
}

#endif  // NAUTYLUS_IDS_H
