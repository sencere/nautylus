#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

#include "jsmn.h"
#include "nautylus/graph.h"
#include "nautylus/vector.h"

typedef struct nty_demo_item {
  char *name;
  nty_id_t id;
} nty_demo_item_t;

typedef struct nty_demo {
  nty_graph_t *graph;
  nty_vector_index_t *vectors;
  nty_demo_item_t *items;
  size_t count;
  size_t capacity;
  size_t dimension;
} nty_demo_t;

typedef struct nty_dataset_config {
  const char *nodes_path;
  const char *edges_path;
  const char *json_path;
  const char *db_path;
  size_t dimension;
} nty_dataset_config_t;

static void demo_destroy(nty_demo_t *demo) {
  if (!demo) {
    return;
  }
  if (demo->items) {
    for (size_t i = 0; i < demo->count; i++) {
      free(demo->items[i].name);
    }
  }
  free(demo->items);
  nty_vector_index_destroy(demo->vectors);
  nty_graph_destroy(demo->graph);
  demo->graph = NULL;
  demo->vectors = NULL;
  demo->items = NULL;
  demo->count = 0;
  demo->capacity = 0;
  demo->dimension = 0;
}

static bool demo_reserve(nty_demo_t *demo, size_t capacity) {
  if (!demo) {
    return false;
  }
  if (capacity <= demo->capacity) {
    return true;
  }
  size_t next = demo->capacity ? demo->capacity : 8;
  while (next < capacity) {
    next *= 2;
  }
  nty_demo_item_t *items =
      (nty_demo_item_t *)realloc(demo->items, next * sizeof(*items));
  if (!items) {
    return false;
  }
  demo->items = items;
  demo->capacity = next;
  return true;
}

static char *nty_strdup(const char *value) {
  size_t len = strlen(value);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1);
  return copy;
}

static bool demo_add_item(nty_demo_t *demo, const char *name, nty_id_t id) {
  if (!demo || !name || !id) {
    return false;
  }
  if (!demo_reserve(demo, demo->count + 1)) {
    return false;
  }
  char *copy = nty_strdup(name);
  if (!copy) {
    return false;
  }
  demo->items[demo->count].name = copy;
  demo->items[demo->count].id = id;
  demo->count++;
  return true;
}

static bool demo_build(nty_demo_t *demo) {
  if (!demo) {
    return false;
  }
  memset(demo, 0, sizeof(*demo));
  demo->dimension = 3;
  demo->graph = nty_graph_create(16, 32, 8);
  demo->vectors = nty_vector_index_create(demo->dimension, 16);
  if (!demo->graph || !demo->vectors) {
    demo_destroy(demo);
    return false;
  }

  const char *names[6] = {"alba", "boreal", "cetus",
                          "delta", "ember",  "fjord"};
  float vectors[6][3] = {
      {0.1f, 0.2f, 0.3f}, {0.2f, 0.1f, 0.4f}, {0.9f, 0.8f, 0.7f},
      {0.85f, 0.75f, 0.65f}, {0.3f, 0.4f, 0.2f}, {0.0f, 0.1f, 0.0f}};

  for (size_t i = 0; i < 6; i++) {
    nty_id_t id = nty_node_create(demo->graph);
    if (!id) {
      demo_destroy(demo);
      return false;
    }
    if (!demo_add_item(demo, names[i], id)) {
      demo_destroy(demo);
      return false;
    }
    if (!nty_vector_index_set(demo->vectors, id, vectors[i])) {
      demo_destroy(demo);
      return false;
    }
  }

  nty_edge_create(demo->graph, demo->items[0].id, demo->items[1].id, 1, 1.0,
                  0);
  nty_edge_create(demo->graph, demo->items[0].id, demo->items[4].id, 1, 0.7,
                  0);
  nty_edge_create(demo->graph, demo->items[2].id, demo->items[3].id, 1, 0.9,
                  0);
  nty_edge_create(demo->graph, demo->items[5].id, demo->items[0].id, 1, 0.4,
                  0);

  return true;
}

static const nty_demo_item_t *demo_find_by_name(const nty_demo_t *demo,
                                                const char *name) {
  if (!demo || !name) {
    return NULL;
  }
  for (size_t i = 0; i < demo->count; i++) {
    if (strcmp(demo->items[i].name, name) == 0) {
      return &demo->items[i];
    }
  }
  return NULL;
}

static const nty_demo_item_t *demo_find_by_id(const nty_demo_t *demo,
                                              nty_id_t id) {
  if (!demo) {
    return NULL;
  }
  for (size_t i = 0; i < demo->count; i++) {
    if (demo->items[i].id == id) {
      return &demo->items[i];
    }
  }
  return NULL;
}

static bool demo_has_edge(const nty_demo_t *demo,
                          nty_id_t from,
                          nty_id_t to) {
  nty_neighbor_iter_t iter = nty_node_out_edges(demo->graph, from);
  nty_id_t edge_id = 0;
  while (nty_neighbor_iter_next(demo->graph, &iter, &edge_id)) {
    nty_edge_t edge;
    if (nty_edge_get(demo->graph, edge_id, &edge) && edge.to == to) {
      return true;
    }
  }
  return false;
}

static char *read_file(const char *path, size_t *out_len) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }
  rewind(file);
  char *data = (char *)malloc((size_t)size + 1);
  if (!data) {
    fclose(file);
    return NULL;
  }
  size_t read = fread(data, 1, (size_t)size, file);
  fclose(file);
  data[read] = '\0';
  if (out_len) {
    *out_len = read;
  }
  return data;
}

static char *trim(char *value) {
  while (*value && isspace((unsigned char)*value)) {
    value++;
  }
  if (*value == '\0') {
    return value;
  }
  char *end = value + strlen(value) - 1;
  while (end > value && isspace((unsigned char)*end)) {
    *end-- = '\0';
  }
  return value;
}

static size_t count_lines(const char *path) {
  FILE *file = fopen(path, "r");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    char *p = trim(line);
    if (*p == '\0' || *p == '#') {
      continue;
    }
    count++;
  }
  fclose(file);
  return count;
}

static bool demo_init(nty_demo_t *demo,
                      size_t node_cap,
                      size_t edge_cap,
                      size_t dimension) {
  if (!demo || dimension == 0) {
    return false;
  }
  memset(demo, 0, sizeof(*demo));
  demo->dimension = dimension;
  demo->graph = nty_graph_create(node_cap ? node_cap : 1,
                                 edge_cap ? edge_cap : 1, 0);
  demo->vectors = nty_vector_index_create(dimension,
                                          node_cap ? node_cap : 1);
  if (!demo->graph || !demo->vectors) {
    demo_destroy(demo);
    return false;
  }
  return true;
}

static bool parse_csv_vector(char *line,
                             size_t dimension,
                             const char **out_name,
                             float *out_vec) {
  char *save = NULL;
  char *token = strtok_r(line, ",", &save);
  if (!token) {
    return false;
  }
  token = trim(token);
  if (out_name) {
    *out_name = token;
  }
  for (size_t i = 0; i < dimension; i++) {
    token = strtok_r(NULL, ",", &save);
    if (!token) {
      return false;
    }
    out_vec[i] = strtof(trim(token), NULL);
  }
  return true;
}

static bool parse_csv_edge(char *line,
                           const char **out_from,
                           const char **out_to,
                           float *out_weight) {
  char *save = NULL;
  char *token = strtok_r(line, ",", &save);
  if (!token) {
    return false;
  }
  *out_from = trim(token);
  token = strtok_r(NULL, ",", &save);
  if (!token) {
    return false;
  }
  *out_to = trim(token);
  token = strtok_r(NULL, ",", &save);
  *out_weight = token ? strtof(trim(token), NULL) : 1.0f;
  return true;
}

static bool demo_build_from_csv(nty_demo_t *demo,
                                const char *nodes_path,
                                const char *edges_path,
                                size_t dimension) {
  if (!demo || !nodes_path || dimension == 0) {
    return false;
  }

  size_t node_count = count_lines(nodes_path);
  size_t edge_count = edges_path ? count_lines(edges_path) : 0;
  if (!demo_init(demo, node_count, edge_count, dimension)) {
    return false;
  }

  FILE *file = fopen(nodes_path, "r");
  if (!file) {
    demo_destroy(demo);
    return false;
  }
  char line[2048];
  float *vec = (float *)malloc(sizeof(float) * dimension);
  if (!vec) {
    fclose(file);
    demo_destroy(demo);
    return false;
  }
  while (fgets(line, sizeof(line), file)) {
    char *p = trim(line);
    if (*p == '\0' || *p == '#') {
      continue;
    }
    const char *name = NULL;
    if (!parse_csv_vector(p, dimension, &name, vec)) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    nty_id_t id = nty_node_create(demo->graph);
    if (!id || !demo_add_item(demo, name, id)) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    if (!nty_vector_index_set(demo->vectors, id, vec)) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
  }
  free(vec);
  fclose(file);

  if (!edges_path) {
    return true;
  }

  file = fopen(edges_path, "r");
  if (!file) {
    demo_destroy(demo);
    return false;
  }
  while (fgets(line, sizeof(line), file)) {
    char *p = trim(line);
    if (*p == '\0' || *p == '#') {
      continue;
    }
    const char *from_name = NULL;
    const char *to_name = NULL;
    float weight = 1.0f;
    if (!parse_csv_edge(p, &from_name, &to_name, &weight)) {
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    const nty_demo_item_t *from = demo_find_by_name(demo, from_name);
    const nty_demo_item_t *to = demo_find_by_name(demo, to_name);
    if (!from || !to) {
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    nty_edge_create(demo->graph, from->id, to->id, 1, weight, 0);
  }
  fclose(file);
  return true;
}

static bool json_token_eq(const char *json,
                          const jsmntok_t *tok,
                          const char *value) {
  int len = tok->end - tok->start;
  return (int)strlen(value) == len &&
         strncmp(json + tok->start, value, (size_t)len) == 0;
}

static int json_skip(const jsmntok_t *tokens, int index) {
  int i = index;
  int count;
  switch (tokens[i].type) {
    case JSMN_OBJECT:
      count = tokens[i].size;
      i++;
      for (int j = 0; j < count; j++) {
        i = json_skip(tokens, i);
        i = json_skip(tokens, i);
      }
      return i;
    case JSMN_ARRAY:
      count = tokens[i].size;
      i++;
      for (int j = 0; j < count; j++) {
        i = json_skip(tokens, i);
      }
      return i;
    default:
      return i + 1;
  }
}

static int json_object_get(const char *json,
                           const jsmntok_t *tokens,
                           int object_index,
                           const char *key) {
  int i = object_index + 1;
  for (int pair = 0; pair < tokens[object_index].size; pair++) {
    if (json_token_eq(json, &tokens[i], key)) {
      return i + 1;
    }
    i = json_skip(tokens, i + 1);
  }
  return -1;
}

static float json_parse_float(const char *json, const jsmntok_t *tok) {
  int len = tok->end - tok->start;
  char buf[64];
  int copy = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
  memcpy(buf, json + tok->start, (size_t)copy);
  buf[copy] = '\0';
  return strtof(buf, NULL);
}

static bool demo_build_from_json(nty_demo_t *demo, const char *path) {
  if (!demo || !path) {
    return false;
  }

  size_t len = 0;
  char *json = read_file(path, &len);
  if (!json) {
    return false;
  }

  jsmn_parser parser;
  jsmn_init(&parser);
  unsigned int cap = 256;
  jsmntok_t *tokens = NULL;
  int parsed = JSMN_ERROR_NOMEM;
  while (parsed == JSMN_ERROR_NOMEM) {
    cap *= 2;
    tokens = (jsmntok_t *)realloc(tokens, sizeof(jsmntok_t) * cap);
    if (!tokens) {
      free(json);
      return false;
    }
    jsmn_init(&parser);
    parsed = jsmn_parse(&parser, json, len, tokens, cap);
  }
  if (parsed < 0) {
    free(tokens);
    free(json);
    return false;
  }

  int nodes_index = -1;
  int edges_index = -1;
  if (tokens[0].type != JSMN_OBJECT) {
    free(tokens);
    free(json);
    return false;
  }
  int i = 1;
  for (int pair = 0; pair < tokens[0].size; pair++) {
    if (json_token_eq(json, &tokens[i], "nodes")) {
      nodes_index = i + 1;
    } else if (json_token_eq(json, &tokens[i], "edges")) {
      edges_index = i + 1;
    }
    i = json_skip(tokens, i + 1);
  }
  if (nodes_index < 0) {
    free(tokens);
    free(json);
    return false;
  }

  size_t node_count = (size_t)tokens[nodes_index].size;
  size_t edge_count = edges_index >= 0 ? (size_t)tokens[edges_index].size : 0;

  size_t dimension = 0;
  if (tokens[nodes_index].size > 0) {
    int node_obj = nodes_index + 1;
    int vec_index = json_object_get(json, tokens, node_obj, "vector");
    if (vec_index >= 0 && tokens[vec_index].type == JSMN_ARRAY) {
      dimension = (size_t)tokens[vec_index].size;
    }
  }
  if (dimension == 0) {
    free(tokens);
    free(json);
    return false;
  }

  if (!demo_init(demo, node_count, edge_count, dimension)) {
    free(tokens);
    free(json);
    return false;
  }

  int node_array_index = nodes_index + 1;
  for (int n = 0; n < tokens[nodes_index].size; n++) {
    int obj_index = node_array_index;
    char *name = NULL;
    float *vec = (float *)malloc(sizeof(float) * dimension);
    if (!vec) {
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    int name_index = json_object_get(json, tokens, obj_index, "name");
    int vec_index = json_object_get(json, tokens, obj_index, "vector");
    if (name_index < 0 || vec_index < 0) {
      free(vec);
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    int len_name = tokens[name_index].end - tokens[name_index].start;
    name = (char *)malloc((size_t)len_name + 1);
    if (!name) {
      free(vec);
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    memcpy(name, json + tokens[name_index].start, (size_t)len_name);
    name[len_name] = '\0';
    if (tokens[vec_index].type != JSMN_ARRAY ||
        (size_t)tokens[vec_index].size != dimension) {
      free(name);
      free(vec);
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    for (size_t d = 0; d < dimension; d++) {
      vec[d] = json_parse_float(json, &tokens[vec_index + 1 + (int)d]);
    }

    nty_id_t id = nty_node_create(demo->graph);
    if (!id || !demo_add_item(demo, name, id)) {
      free(name);
      free(vec);
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    free(name);
    if (!nty_vector_index_set(demo->vectors, id, vec)) {
      free(vec);
      demo_destroy(demo);
      free(tokens);
      free(json);
      return false;
    }
    free(vec);
    node_array_index = json_skip(tokens, node_array_index);
  }

  if (edges_index >= 0) {
    int edge_array_index = edges_index + 1;
    for (int e = 0; e < tokens[edges_index].size; e++) {
      int obj_index = edge_array_index;
      char *from = NULL;
      char *to = NULL;
      float weight = 1.0f;
      int from_index = json_object_get(json, tokens, obj_index, "from");
      int to_index = json_object_get(json, tokens, obj_index, "to");
      int weight_index = json_object_get(json, tokens, obj_index, "weight");
      if (from_index >= 0) {
        int len_from = tokens[from_index].end - tokens[from_index].start;
        from = (char *)malloc((size_t)len_from + 1);
        if (!from) {
          demo_destroy(demo);
          free(tokens);
          free(json);
          return false;
        }
        memcpy(from, json + tokens[from_index].start, (size_t)len_from);
        from[len_from] = '\0';
      }
      if (to_index >= 0) {
        int len_to = tokens[to_index].end - tokens[to_index].start;
        to = (char *)malloc((size_t)len_to + 1);
        if (!to) {
          free(from);
          demo_destroy(demo);
          free(tokens);
          free(json);
          return false;
        }
        memcpy(to, json + tokens[to_index].start, (size_t)len_to);
        to[len_to] = '\0';
      }
      if (weight_index >= 0) {
        weight = json_parse_float(json, &tokens[weight_index]);
      }
      if (!from || !to) {
        free(from);
        free(to);
        demo_destroy(demo);
        free(tokens);
        free(json);
        return false;
      }
      const nty_demo_item_t *from_item = demo_find_by_name(demo, from);
      const nty_demo_item_t *to_item = demo_find_by_name(demo, to);
      free(from);
      free(to);
      if (!from_item || !to_item) {
        demo_destroy(demo);
        free(tokens);
        free(json);
        return false;
      }
      nty_edge_create(demo->graph, from_item->id, to_item->id, 1, weight, 0);
      edge_array_index = json_skip(tokens, edge_array_index);
    }
  }

  free(tokens);
  free(json);
  return true;
}

static bool demo_save_to_store(const nty_demo_t *demo, const char *path) {
  if (!demo || !path) {
    return false;
  }
  FILE *file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  uint32_t version = 1;
  uint32_t dimension = (uint32_t)demo->dimension;
  uint32_t node_count = (uint32_t)demo->count;
  uint32_t edge_count = (uint32_t)nty_graph_edge_count(demo->graph);
  fwrite("NTY1", 1, 4, file);
  fwrite(&version, sizeof(version), 1, file);
  fwrite(&dimension, sizeof(dimension), 1, file);
  fwrite(&node_count, sizeof(node_count), 1, file);
  fwrite(&edge_count, sizeof(edge_count), 1, file);

  for (size_t i = 0; i < demo->count; i++) {
    uint32_t name_len = (uint32_t)strlen(demo->items[i].name);
    fwrite(&name_len, sizeof(name_len), 1, file);
    fwrite(demo->items[i].name, 1, name_len, file);
    float *vec = (float *)malloc(sizeof(float) * demo->dimension);
    if (!vec) {
      fclose(file);
      return false;
    }
    if (!nty_vector_index_get(demo->vectors, demo->items[i].id, vec)) {
      free(vec);
      fclose(file);
      return false;
    }
    fwrite(vec, sizeof(float), demo->dimension, file);
    free(vec);
  }

  nty_edge_iter_t iter = nty_graph_edges(demo->graph);
  nty_id_t edge_id = 0;
  while (nty_edge_iter_next(demo->graph, &iter, &edge_id)) {
    nty_edge_t edge;
    if (!nty_edge_get(demo->graph, edge_id, &edge)) {
      continue;
    }
    uint32_t from_index = UINT32_MAX;
    uint32_t to_index = UINT32_MAX;
    for (size_t i = 0; i < demo->count; i++) {
      if (demo->items[i].id == edge.from) {
        from_index = (uint32_t)i;
      }
      if (demo->items[i].id == edge.to) {
        to_index = (uint32_t)i;
      }
    }
    if (from_index == UINT32_MAX || to_index == UINT32_MAX) {
      continue;
    }
    fwrite(&from_index, sizeof(from_index), 1, file);
    fwrite(&to_index, sizeof(to_index), 1, file);
    float weight = (float)edge.weight;
    fwrite(&weight, sizeof(weight), 1, file);
  }

  fclose(file);
  return true;
}

static bool demo_build_from_store(nty_demo_t *demo, const char *path) {
  if (!demo || !path) {
    return false;
  }
  FILE *file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  char magic[4];
  uint32_t version = 0;
  uint32_t dimension = 0;
  uint32_t node_count = 0;
  uint32_t edge_count = 0;
  if (fread(magic, 1, 4, file) != 4 ||
      fread(&version, sizeof(version), 1, file) != 1 ||
      fread(&dimension, sizeof(dimension), 1, file) != 1 ||
      fread(&node_count, sizeof(node_count), 1, file) != 1 ||
      fread(&edge_count, sizeof(edge_count), 1, file) != 1) {
    fclose(file);
    return false;
  }
  if (memcmp(magic, "NTY1", 4) != 0 || version != 1 || dimension == 0) {
    fclose(file);
    return false;
  }

  if (!demo_init(demo, node_count, edge_count, dimension)) {
    fclose(file);
    return false;
  }

  float *vec = (float *)malloc(sizeof(float) * dimension);
  if (!vec) {
    fclose(file);
    demo_destroy(demo);
    return false;
  }
  for (uint32_t i = 0; i < node_count; i++) {
    uint32_t name_len = 0;
    if (fread(&name_len, sizeof(name_len), 1, file) != 1) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    char *name = (char *)malloc((size_t)name_len + 1);
    if (!name) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    if (fread(name, 1, name_len, file) != name_len) {
      free(name);
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    name[name_len] = '\0';
    if (fread(vec, sizeof(float), dimension, file) != dimension) {
      free(name);
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    nty_id_t id = nty_node_create(demo->graph);
    if (!id || !demo_add_item(demo, name, id)) {
      free(name);
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    free(name);
    if (!nty_vector_index_set(demo->vectors, id, vec)) {
      free(vec);
      fclose(file);
      demo_destroy(demo);
      return false;
    }
  }
  free(vec);

  for (uint32_t e = 0; e < edge_count; e++) {
    uint32_t from_index = 0;
    uint32_t to_index = 0;
    float weight = 1.0f;
    if (fread(&from_index, sizeof(from_index), 1, file) != 1 ||
        fread(&to_index, sizeof(to_index), 1, file) != 1 ||
        fread(&weight, sizeof(weight), 1, file) != 1) {
      fclose(file);
      demo_destroy(demo);
      return false;
    }
    if (from_index >= demo->count || to_index >= demo->count) {
      continue;
    }
    nty_edge_create(demo->graph,
                    demo->items[from_index].id,
                    demo->items[to_index].id,
                    1,
                    weight,
                    0);
  }

  fclose(file);
  return true;
}

static bool demo_build_from_config(nty_demo_t *demo,
                                   const nty_dataset_config_t *config) {
  if (!config) {
    return demo_build(demo);
  }
  if (config->db_path) {
    return demo_build_from_store(demo, config->db_path);
  }
  if (config->json_path) {
    return demo_build_from_json(demo, config->json_path);
  }
  if (config->nodes_path) {
    return demo_build_from_csv(demo,
                               config->nodes_path,
                               config->edges_path,
                               config->dimension);
  }
  return demo_build(demo);
}

typedef struct nty_result {
  nty_id_t id;
  float distance;
  bool connected;
} nty_result_t;

static size_t demo_query(const nty_demo_t *demo,
                         nty_id_t query_id,
                         size_t k,
                         nty_result_t *out) {
  if (!demo || !out || k == 0) {
    return 0;
  }

  float query_vec[3];
  if (!nty_vector_index_get(demo->vectors, query_id, query_vec)) {
    return 0;
  }

  size_t request = k + 1;
  if (request > demo->count) {
    request = demo->count;
  }
  nty_id_t *ids = (nty_id_t *)malloc(sizeof(nty_id_t) * request);
  float *dist = (float *)malloc(sizeof(float) * request);
  if (!ids || !dist) {
    free(ids);
    free(dist);
    return 0;
  }
  size_t count =
      nty_vector_index_knn(demo->vectors, query_vec, request, ids, dist);

  size_t out_count = 0;
  for (size_t i = 0; i < count && out_count < k; i++) {
    if (ids[i] == query_id) {
      continue;
    }
    out[out_count].id = ids[i];
    out[out_count].distance = dist[i];
    out[out_count].connected =
        demo_has_edge(demo, query_id, ids[i]) ||
        demo_has_edge(demo, ids[i], query_id);
    out_count++;
  }
  free(ids);
  free(dist);
  return out_count;
}

static void print_usage(void) {
  printf("nautylus demo\n");
  printf("nautylus query --node <name|id> [--k N] [--db FILE|--json FILE|--nodes FILE --dim N [--edges FILE]]\n");
  printf("nautylus serve [--port N] [--db FILE|--json FILE|--nodes FILE --dim N [--edges FILE]]\n");
  printf("nautylus load --json FILE [--out FILE]\n");
  printf("nautylus load --nodes FILE --dim N [--edges FILE] [--out FILE]\n");
}

static int handle_query(const nty_demo_t *demo,
                        const char *node_arg,
                        size_t k) {
  const nty_demo_item_t *item = demo_find_by_name(demo, node_arg);
  nty_id_t id = 0;
  if (item) {
    id = item->id;
  } else {
    char *end = NULL;
    unsigned long long value = strtoull(node_arg, &end, 10);
    if (end && *end == '\0') {
      id = (nty_id_t)value;
      item = demo_find_by_id(demo, id);
    }
  }

  if (!item) {
    fprintf(stderr, "unknown node: %s\n", node_arg);
    return 1;
  }

  nty_result_t *results =
      (nty_result_t *)calloc(k ? k : 1, sizeof(nty_result_t));
  if (!results) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  size_t count = demo_query(demo, item->id, k, results);
  printf("query: %s (id=%llu)\n", item->name,
         (unsigned long long)item->id);
  for (size_t i = 0; i < count; i++) {
    const nty_demo_item_t *candidate = demo_find_by_id(demo, results[i].id);
    const char *name = candidate ? candidate->name : "unknown";
    printf("  %zu) %s id=%llu dist=%.4f connected=%s\n",
           i + 1,
           name,
           (unsigned long long)results[i].id,
           results[i].distance,
           results[i].connected ? "yes" : "no");
  }
  free(results);
  return 0;
}

static void write_all(int fd, const char *data, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    ssize_t wrote = send(fd, data + offset, len - offset, 0);
    if (wrote <= 0) {
      break;
    }
    offset += (size_t)wrote;
  }
}

static void send_text(int fd, const char *body) {
  char header[256];
  size_t body_len = strlen(body);
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n\r\n",
                            body_len);
  if (header_len < 0) {
    return;
  }
  write_all(fd, header, (size_t)header_len);
  write_all(fd, body, body_len);
}

static void send_json(int fd, const char *body) {
  char header[256];
  size_t body_len = strlen(body);
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n\r\n",
                            body_len);
  if (header_len < 0) {
    return;
  }
  write_all(fd, header, (size_t)header_len);
  write_all(fd, body, body_len);
}

static void send_bytes(int fd,
                       const char *content_type,
                       const char *data,
                       size_t len) {
  char header[256];
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n\r\n",
                            content_type,
                            len);
  if (header_len < 0) {
    return;
  }
  write_all(fd, header, (size_t)header_len);
  write_all(fd, data, len);
}

static void send_not_found(int fd) {
  const char *body = "{\"error\":\"not found\"}";
  char header[256];
  size_t body_len = strlen(body);
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 404 Not Found\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n\r\n",
                            body_len);
  if (header_len < 0) {
    return;
  }
  write_all(fd, header, (size_t)header_len);
  write_all(fd, body, body_len);
}

static void send_file(int fd, const char *path, const char *content_type) {
  size_t len = 0;
  char *data = read_file(path, &len);
  if (!data) {
    send_not_found(fd);
    return;
  }
  send_bytes(fd, content_type, data, len);
  free(data);
}

static const char *find_query_param(const char *query,
                                    const char *key,
                                    char *out,
                                    size_t out_len) {
  size_t key_len = strlen(key);
  const char *p = query;
  while (p && *p) {
    if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
      p += key_len + 1;
      size_t i = 0;
      while (*p && *p != '&' && i + 1 < out_len) {
        out[i++] = *p++;
      }
      out[i] = '\0';
      return out;
    }
    p = strchr(p, '&');
    if (p) {
      p++;
    }
  }
  return NULL;
}

static void handle_api_query(int fd,
                             const nty_demo_t *demo,
                             const char *query) {
  char node_buf[64] = "";
  char k_buf[16] = "";
  const char *node_arg = find_query_param(query, "node", node_buf,
                                          sizeof(node_buf));
  find_query_param(query, "k", k_buf, sizeof(k_buf));

  size_t k = 3;
  if (k_buf[0]) {
    k = (size_t)strtoul(k_buf, NULL, 10);
    if (k == 0) {
      k = 3;
    }
  }
  if (k > 16) {
    k = 16;
  }

  if (!node_arg || !node_arg[0]) {
    send_json(fd, "{\"error\":\"missing node\"}");
    return;
  }

  const nty_demo_item_t *item = demo_find_by_name(demo, node_arg);
  if (!item) {
    send_json(fd, "{\"error\":\"unknown node\"}");
    return;
  }

  nty_result_t *results =
      (nty_result_t *)calloc(k ? k : 1, sizeof(nty_result_t));
  if (!results) {
    send_json(fd, "{\"error\":\"out of memory\"}");
    return;
  }
  size_t count = demo_query(demo, item->id, k, results);

  char body[2048];
  size_t offset = 0;
  offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                              "{\"query\":\"%s\",\"results\":[", item->name);
  for (size_t i = 0; i < count; i++) {
    const nty_demo_item_t *candidate = demo_find_by_id(demo, results[i].id);
    const char *name = candidate ? candidate->name : "unknown";
    offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                                "%s{\"name\":\"%s\",\"id\":%llu,"
                                "\"distance\":%.4f,\"connected\":%s}",
                                i == 0 ? "" : ",",
                                name,
                                (unsigned long long)results[i].id,
                                results[i].distance,
                                results[i].connected ? "true" : "false");
  }
  offset += (size_t)snprintf(body + offset, sizeof(body) - offset, "]}");
  send_json(fd, body);
  free(results);
}

static void handle_api_graph(int fd, const nty_demo_t *demo) {
  if (!demo) {
    send_json(fd, "{\"error\":\"no demo\"}");
    return;
  }

  char body[4096];
  size_t offset = 0;
  offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                             "{\"nodes\":[");
  for (size_t i = 0; i < demo->count; i++) {
    const nty_demo_item_t *item = &demo->items[i];
    offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                               "%s{\"id\":%llu,\"name\":\"%s\"}",
                               i == 0 ? "" : ",",
                               (unsigned long long)item->id,
                               item->name);
  }
  offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                             "],\"edges\":[");

  nty_edge_iter_t iter = nty_graph_edges(demo->graph);
  nty_id_t edge_id = 0;
  bool first = true;
  while (nty_edge_iter_next(demo->graph, &iter, &edge_id)) {
    nty_edge_t edge;
    if (!nty_edge_get(demo->graph, edge_id, &edge)) {
      continue;
    }
    offset += (size_t)snprintf(body + offset, sizeof(body) - offset,
                               "%s{\"source\":%llu,\"target\":%llu,"
                               "\"weight\":%.3f}",
                               first ? "" : ",",
                               (unsigned long long)edge.from,
                               (unsigned long long)edge.to,
                               edge.weight);
    first = false;
    if (offset >= sizeof(body)) {
      break;
    }
  }
  offset += (size_t)snprintf(body + offset, sizeof(body) - offset, "]}");
  send_json(fd, body);
}

static const char *ui_html(void) {
  return "<!doctype html>\n"
         "<html>\n"
         "<head>\n"
         "  <meta charset=\"utf-8\" />\n"
         "  <title>Nautylus Demo</title>\n"
         "  <script src=\"/d3.v7.min.js\"></script>\n"
         "  <style>\n"
         "    body { font-family: Georgia, serif; background: #f2efe6; margin: 0; }\n"
         "    header { padding: 24px; background: #2a3d45; color: #f9f4e8; }\n"
         "    main { padding: 24px; max-width: 980px; display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }\n"
         "    .card { background: #fff; padding: 16px; border-radius: 8px; box-shadow: 0 6px 16px rgba(0,0,0,0.08); }\n"
         "    label { display: block; margin-top: 12px; }\n"
         "    button { margin-top: 16px; padding: 8px 16px; background: #d07b45; color: #fff; border: none; border-radius: 4px; }\n"
         "    pre { background: #f6f4f0; padding: 12px; border-radius: 6px; }\n"
         "    svg { width: 100%; height: 420px; border-radius: 8px; background: #fdfcf8; }\n"
         "    .node { fill: #3a6ea5; stroke: #1e2f3a; stroke-width: 1px; }\n"
         "    .node.active { fill: #d07b45; }\n"
         "    .edge { stroke: #94a4a5; stroke-width: 2px; }\n"
         "    .edge.active { stroke: #d07b45; }\n"
         "    .label { font-size: 12px; fill: #1f2a30; }\n"
         "  </style>\n"
         "</head>\n"
         "<body>\n"
         "  <header><h1>Nautylus Demo</h1><p>Vector + graph scoring snapshot</p></header>\n"
         "  <main>\n"
         "    <div class=\"card\">\n"
         "      <label>Node\n"
         "        <select id=\"node\">\n"
         "          <option value=\"alba\">alba</option>\n"
         "          <option value=\"boreal\">boreal</option>\n"
         "          <option value=\"cetus\">cetus</option>\n"
         "          <option value=\"delta\">delta</option>\n"
         "          <option value=\"ember\">ember</option>\n"
         "          <option value=\"fjord\">fjord</option>\n"
         "        </select>\n"
         "      </label>\n"
         "      <label>k\n"
         "        <input id=\"k\" type=\"number\" min=\"1\" max=\"5\" value=\"3\" />\n"
         "      </label>\n"
         "      <button id=\"run\">Run</button>\n"
         "      <pre id=\"out\">Ready.</pre>\n"
         "    </div>\n"
         "    <div class=\"card\">\n"
         "      <svg id=\"graph\"></svg>\n"
         "    </div>\n"
         "  </main>\n"
         "  <script>\n"
         "    const out = document.getElementById('out');\n"
         "    const svg = d3.select('#graph');\n"
         "    const width = svg.node().getBoundingClientRect().width;\n"
         "    const height = svg.node().getBoundingClientRect().height;\n"
         "    let graphData = null;\n"
         "\n"
         "    function renderGraph(data) {\n"
         "      graphData = data;\n"
         "      svg.selectAll('*').remove();\n"
         "      const sim = d3.forceSimulation(data.nodes)\n"
         "        .force('link', d3.forceLink(data.edges).id(d => d.id).distance(140))\n"
         "        .force('charge', d3.forceManyBody().strength(-300))\n"
         "        .force('center', d3.forceCenter(width / 2, height / 2));\n"
         "\n"
         "      const link = svg.append('g')\n"
         "        .selectAll('line')\n"
         "        .data(data.edges)\n"
         "        .enter().append('line')\n"
         "        .attr('class', 'edge');\n"
         "\n"
         "      const node = svg.append('g')\n"
         "        .selectAll('circle')\n"
         "        .data(data.nodes)\n"
         "        .enter().append('circle')\n"
         "        .attr('class', 'node')\n"
         "        .attr('r', 12)\n"
         "        .call(d3.drag()\n"
         "          .on('start', (event, d) => {\n"
         "            if (!event.active) sim.alphaTarget(0.3).restart();\n"
         "            d.fx = d.x; d.fy = d.y;\n"
         "          })\n"
         "          .on('drag', (event, d) => {\n"
         "            d.fx = event.x; d.fy = event.y;\n"
         "          })\n"
         "          .on('end', (event, d) => {\n"
         "            if (!event.active) sim.alphaTarget(0);\n"
         "            d.fx = null; d.fy = null;\n"
         "          }));\n"
         "\n"
         "      const label = svg.append('g')\n"
         "        .selectAll('text')\n"
         "        .data(data.nodes)\n"
         "        .enter().append('text')\n"
         "        .attr('class', 'label')\n"
         "        .text(d => d.name);\n"
         "\n"
         "      sim.on('tick', () => {\n"
         "        link\n"
         "          .attr('x1', d => d.source.x)\n"
         "          .attr('y1', d => d.source.y)\n"
         "          .attr('x2', d => d.target.x)\n"
         "          .attr('y2', d => d.target.y);\n"
         "        node\n"
         "          .attr('cx', d => d.x)\n"
         "          .attr('cy', d => d.y);\n"
         "        label\n"
         "          .attr('x', d => d.x + 14)\n"
         "          .attr('y', d => d.y + 4);\n"
         "      });\n"
         "    }\n"
         "\n"
         "    function highlight(queryName, results) {\n"
         "      if (!graphData) return;\n"
         "      const ids = new Set(results.map(r => r.id));\n"
         "      const query = graphData.nodes.find(n => n.name === queryName);\n"
         "      svg.selectAll('circle')\n"
         "        .classed('active', d => query && (d.id === query.id || ids.has(d.id)));\n"
         "      svg.selectAll('line')\n"
         "        .classed('active', d => query &&\n"
         "          ((d.source.id === query.id && ids.has(d.target.id)) ||\n"
         "           (d.target.id === query.id && ids.has(d.source.id))));\n"
         "    }\n"
         "\n"
         "    fetch('/api/graph')\n"
         "      .then(res => res.json())\n"
         "      .then(renderGraph);\n"
         "\n"
         "    document.getElementById('run').onclick = async () => {\n"
         "      const node = document.getElementById('node').value;\n"
         "      const k = document.getElementById('k').value;\n"
         "      const res = await fetch(`/api/query?node=${node}&k=${k}`);\n"
         "      const data = await res.json();\n"
         "      out.textContent = JSON.stringify(data, null, 2);\n"
         "      if (data.results) {\n"
         "        highlight(data.query, data.results);\n"
         "      }\n"
         "    };\n"
         "  </script>\n"
         "</body>\n"
         "</html>\n";
}

static int serve_ui(const nty_demo_t *demo, int port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 8) < 0) {
    perror("listen");
    close(server_fd);
    return 1;
  }

  printf("Nautylus UI running on http://127.0.0.1:%d\n", port);
  for (;;) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      break;
    }

    char buffer[2048];
    ssize_t len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (len > 0) {
      buffer[len] = '\0';
      char method[8] = "";
      char path[256] = "";
      if (sscanf(buffer, "%7s %255s", method, path) == 2) {
        if (strncmp(path, "/api/query", 10) == 0) {
          const char *query = strchr(path, '?');
          handle_api_query(client_fd, demo, query ? query + 1 : "");
        } else if (strncmp(path, "/api/graph", 10) == 0) {
          handle_api_graph(client_fd, demo);
        } else if (strcmp(path, "/d3.v7.min.js") == 0) {
          send_file(client_fd, "resources/d3/d3.v7.min.js",
                    "application/javascript");
        } else {
          send_text(client_fd, ui_html());
        }
      }
    }
    close(client_fd);
  }

  close(server_fd);
  return 0;
}

static bool validate_config(const nty_dataset_config_t *config) {
  int count = 0;
  if (config->db_path) {
    count++;
  }
  if (config->json_path) {
    count++;
  }
  if (config->nodes_path) {
    count++;
  }
  if (count > 1) {
    return false;
  }
  if (config->edges_path && !config->nodes_path) {
    return false;
  }
  if (config->nodes_path && config->dimension == 0) {
    return false;
  }
  return true;
}

static void demo_print_summary(const nty_demo_t *demo) {
  if (!demo) {
    return;
  }
  printf("nodes: %zu edges: %zu dim: %zu\n",
         demo->count,
         nty_graph_edge_count(demo->graph),
         demo->dimension);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  int rc = 0;
  if (strcmp(argv[1], "demo") == 0) {
    nty_demo_t demo;
    if (!demo_build(&demo)) {
      fprintf(stderr, "failed to initialize demo dataset\n");
      return 1;
    }
    rc = handle_query(&demo, "alba", 3);
    demo_destroy(&demo);
  } else if (strcmp(argv[1], "query") == 0) {
    const char *node = NULL;
    size_t k = 3;
    nty_dataset_config_t config;
    memset(&config, 0, sizeof(config));
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--node") == 0 && i + 1 < argc) {
        node = argv[++i];
      } else if (strcmp(argv[i], "--k") == 0 && i + 1 < argc) {
        k = (size_t)strtoul(argv[++i], NULL, 10);
      } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
        config.db_path = argv[++i];
      } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
        config.json_path = argv[++i];
      } else if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc) {
        config.nodes_path = argv[++i];
      } else if (strcmp(argv[i], "--edges") == 0 && i + 1 < argc) {
        config.edges_path = argv[++i];
      } else if (strcmp(argv[i], "--dim") == 0 && i + 1 < argc) {
        config.dimension = (size_t)strtoul(argv[++i], NULL, 10);
      }
    }
    if (!node) {
      fprintf(stderr, "--node is required\n");
      rc = 1;
    } else if (!validate_config(&config)) {
      fprintf(stderr, "invalid dataset options\n");
      rc = 1;
    } else {
      nty_demo_t demo;
      if (!demo_build_from_config(&demo, &config)) {
        fprintf(stderr, "failed to load dataset\n");
        return 1;
      }
      rc = handle_query(&demo, node, k);
      demo_destroy(&demo);
    }
  } else if (strcmp(argv[1], "serve") == 0) {
    int port = 6180;
    nty_dataset_config_t config;
    memset(&config, 0, sizeof(config));
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
        port = atoi(argv[++i]);
      } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
        config.db_path = argv[++i];
      } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
        config.json_path = argv[++i];
      } else if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc) {
        config.nodes_path = argv[++i];
      } else if (strcmp(argv[i], "--edges") == 0 && i + 1 < argc) {
        config.edges_path = argv[++i];
      } else if (strcmp(argv[i], "--dim") == 0 && i + 1 < argc) {
        config.dimension = (size_t)strtoul(argv[++i], NULL, 10);
      }
    }
    if (!validate_config(&config)) {
      fprintf(stderr, "invalid dataset options\n");
      rc = 1;
    } else {
      nty_demo_t demo;
      if (!demo_build_from_config(&demo, &config)) {
        fprintf(stderr, "failed to load dataset\n");
        return 1;
      }
      rc = serve_ui(&demo, port);
      demo_destroy(&demo);
    }
  } else if (strcmp(argv[1], "load") == 0) {
    nty_dataset_config_t config;
    memset(&config, 0, sizeof(config));
    const char *out_path = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
        config.json_path = argv[++i];
      } else if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc) {
        config.nodes_path = argv[++i];
      } else if (strcmp(argv[i], "--edges") == 0 && i + 1 < argc) {
        config.edges_path = argv[++i];
      } else if (strcmp(argv[i], "--dim") == 0 && i + 1 < argc) {
        config.dimension = (size_t)strtoul(argv[++i], NULL, 10);
      } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
        out_path = argv[++i];
      }
    }
    if (!validate_config(&config) || (!config.json_path && !config.nodes_path)) {
      fprintf(stderr, "load requires --json or --nodes/--dim\n");
      rc = 1;
    } else {
      nty_demo_t demo;
      if (!demo_build_from_config(&demo, &config)) {
        fprintf(stderr, "failed to load dataset\n");
        return 1;
      }
      demo_print_summary(&demo);
      if (out_path) {
        if (!demo_save_to_store(&demo, out_path)) {
          fprintf(stderr, "failed to write store\n");
          rc = 1;
        } else {
          printf("saved: %s\n", out_path);
        }
      }
      demo_destroy(&demo);
    }
  } else {
    print_usage();
    rc = 1;
  }

  return rc;
}
