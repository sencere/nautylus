#include "nautylus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#define NG_VALUE_PARAM ((ng_value_type)255)
static size_t ng_test_fail_after_count;
static int ng_test_failure_enabled;
enum ng_test_import_stage {
    NG_TEST_IMPORT_NONE = 0,
    NG_TEST_IMPORT_SNAPSHOT,
    NG_TEST_IMPORT_PARSE_NODES,
    NG_TEST_IMPORT_SYMBOL,
    NG_TEST_IMPORT_NODE,
    NG_TEST_IMPORT_LABEL,
    NG_TEST_IMPORT_NODE_PROPERTY,
    NG_TEST_IMPORT_PARSE_RELATIONSHIPS,
    NG_TEST_IMPORT_RELATIONSHIP,
    NG_TEST_IMPORT_RELATIONSHIP_PROPERTY,
    NG_TEST_IMPORT_ADJACENCY,
    NG_TEST_IMPORT_COMPLETE
};
static enum ng_test_import_stage ng_test_import_stage;
enum ng_test_import_stage ng_test_import_last_stage(void) {
    return ng_test_import_stage;
}
void ng_test_fail_after(size_t count) {
    ng_test_import_stage = NG_TEST_IMPORT_NONE;
    ng_test_fail_after_count = count;
    ng_test_failure_enabled = 1;
}
void ng_test_fail_reset(void) {
    ng_test_fail_after_count = 0;
    ng_test_failure_enabled = 0;
}
static ng_status ng_test_maybe_fail(void) {
    if (!ng_test_failure_enabled)
        return NG_OK;
    if (ng_test_fail_after_count == 0) {
        ng_test_failure_enabled = 0;
        return NG_OOM;
    }
    ng_test_fail_after_count--;
    return NG_OK;
}
static uint64_t ng_test_import_stage_mask_value;
static void ng_test_set_import_stage(enum ng_test_import_stage stage) {
    ng_test_import_stage = stage;
    ng_test_import_stage_mask_value |= ((uint64_t)1u << ((unsigned)stage));
}
enum ng_test_import_stage ng_test_import_current_stage(void) {
    return ng_test_import_stage;
}
uint64_t ng_test_import_stage_mask(void) {
    return ng_test_import_stage_mask_value;
}
static const ng_parameter* ng_query_parameters;
static size_t ng_query_parameter_count;
static int ng_query_parameter_error;

typedef struct {
    ng_symbol_id id;
    char* s;
} sym;
typedef struct {
    ng_symbol_id key;
    ng_value v;
} prop;
typedef struct {
    ng_id id;
    ng_symbol_id* labels;
    size_t nl, np, cap;
    prop* p;
} node_i;
typedef struct {
    ng_id id, src, dst;
    ng_symbol_id type;
    size_t np, cap;
    prop* p;
} rel_i;
typedef struct {
    ng_node_constraint_kind kind;
    ng_symbol_id label, key;
} constraint_i;
typedef struct {
    ng_symbol_id label, key;
} index_i;
typedef struct {
    char* name;
    ng_procedure_handler handler;
    void* context;
} procedure_i;
struct ng_graph {
    char* path;
    uint64_t next_node, next_rel, next_sym;
    sym* sy;
    size_t ns, cs;
    node_i* no;
    size_t nn, cn;
    rel_i* re;
    size_t nr, cr;
    constraint_i* co;
    size_t nc, cc;
    index_i* ix;
    size_t nix, cix;
    size_t* ao;
    size_t* ai;
    size_t an;
    procedure_i* procedures;
    size_t procedure_count, procedure_capacity;
};
struct ng_transaction {
    ng_graph* target;
    ng_graph working;
    int active;
};
struct ng_graphsage_model {
    uint32_t layers;
    size_t input_dimensions;
    size_t output_dimensions;
    size_t neighborhood_sample;
    int normalize_features;
    uint64_t seed;
    double** weights;
    double** biases;
};
typedef struct {
    size_t dimensions;
    double* aggregate;
    double* pre_activation;
    double* activation;
    size_t* neighbor_offsets;
    size_t* neighbors;
    size_t neighbor_count;
} ng_graphsage_layer_cache;
typedef struct {
    size_t layer_count;
    ng_graphsage_layer_cache* layers;
    double* input_activation;
    size_t input_dimensions;
} ng_graphsage_forward_cache;
typedef struct {
    double* activation_gradient;
    double* aggregation_gradient;
    double* weight_gradient;
    double* bias_gradient;
} ng_graphsage_gradient_layer;
typedef struct {
    size_t layer_count;
    ng_graphsage_gradient_layer* layers;
    double* input_gradient;
} ng_graphsage_gradient_cache;
static void ng_graphsage_gradient_cache_free(ng_graphsage_gradient_cache* gradients) {
    size_t i;
    if (!gradients)
        return;
    for (i = 0; i < gradients->layer_count; i++) {
        free(gradients->layers[i].activation_gradient);
        free(gradients->layers[i].aggregation_gradient);
        free(gradients->layers[i].weight_gradient);
        free(gradients->layers[i].bias_gradient);
    }
    free(gradients->layers);
    free(gradients->input_gradient);
    memset(gradients, 0, sizeof(*gradients));
}
static void ng_graphsage_forward_cache_free(ng_graphsage_forward_cache* cache) {
    size_t i;
    if (!cache)
        return;
    free(cache->input_activation);
    for (i = 0; i < cache->layer_count; i++) {
        free(cache->layers[i].aggregate);
        free(cache->layers[i].pre_activation);
        free(cache->layers[i].activation);
        free(cache->layers[i].neighbor_offsets);
        free(cache->layers[i].neighbors);
    }
    free(cache->layers);
    memset(cache, 0, sizeof(*cache));
}
typedef struct {
    ng_node_id id;
    ng_value value;
} ng_node_index_entry;
struct ng_node_index {
    ng_symbol_id label, key;
    ng_node_index_entry* entries;
    size_t count, cap;
};
static ng_status ng_validate_constraints_all(const ng_graph* g);
static int ng_value_equal(const ng_value* a, const ng_value* b);
static void valfree(ng_value* v);
static int ng_ident_char(int c);
static int ng_node_matches_label(const node_i* n, ng_symbol_id label);
static size_t ng_node_position(const ng_graph* g, ng_node_id id);
static int grow(void** p, size_t* cap, size_t n, size_t z) {
    size_t c = *cap ? *cap : 8;
    if (ng_test_maybe_fail() != NG_OK)
        return 0;
    if (n > SIZE_MAX / z)
        return 0;
    while (c < n) {
        if (c > SIZE_MAX / 2)
            return 0;
        c *= 2;
    }
    void* q = realloc(*p, c * z);
    if (!q)
        return 0;
    *p = q;
    *cap = c;
    return 1;
}
static char* dupstr(const char* s) {
    size_t n;
    if (ng_test_maybe_fail() != NG_OK)
        return NULL;
    n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}
static ng_status init(ng_graph** out, const char* path) {
    ng_graph* g = (ng_graph*)calloc(1, sizeof(*g));
    if (!g)
        return NG_OOM;
    g->path = dupstr(path);
    if (!g->path) {
        free(g);
        return NG_OOM;
    }
    g->next_node = g->next_rel = g->next_sym = 1;
    *out = g;
    return NG_OK;
}
ng_status ng_create(ng_graph** o, const char* p) {
    if (!o || !p)
        return NG_INVALID_ARGUMENT;
    return init(o, p);
}
static void put64(unsigned char* p, uint64_t v) {
    size_t i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v & 255u);
        v >>= 8;
    }
}
static uint64_t get64(const unsigned char* p) {
    uint64_t v = 0;
    size_t i;
    for (i = 0; i < 8; i++)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}
static uint32_t hash32(const unsigned char* p, size_t n) {
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
typedef struct {
    unsigned char* p;
    size_t n, c;
} blob;
static int add(blob* b, const void* p, size_t n) {
    if (n > SIZE_MAX - b->n)
        return 0;
    if (b->n + n > b->c) {
        size_t c = b->c ? b->c : 256;
        void* q;
        while (c < b->n + n) {
            if (c > SIZE_MAX / 2)
                return 0;
            c *= 2;
        }
        q = realloc(b->p, c);
        if (!q)
            return 0;
        b->p = (unsigned char*)q;
        b->c = c;
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 1;
}
static int a64(blob* b, uint64_t v) {
    unsigned char x[8];
    put64(x, v);
    return add(b, x, 8);
}
static int astr(blob* b, const char* s, size_t n) {
    return a64(b, n) && add(b, s, n);
}
static int avalue(blob* b, const ng_value* v) {
    if (!a64(b, (uint64_t)v->type) || !a64(b, v->length))
        return 0;
    if (v->type == NG_VALUE_STRING)
        return add(b, v->as.string, v->length);
    if (v->type == NG_VALUE_BYTES)
        return add(b, v->as.bytes, v->length);
    if (v->type == NG_VALUE_BOOL)
        return a64(b, (uint64_t)v->as.boolean);
    if (v->type == NG_VALUE_INT64)
        return a64(b, (uint64_t)v->as.integer);
    if (v->type == NG_VALUE_DOUBLE) {
        uint64_t u;
        memcpy(&u, &v->as.real, 8);
        return a64(b, u);
    }
    if (v->type == NG_VALUE_LIST) {
        size_t i;
        if (!v->as.list)
            return 1;
        for (i = 0; i < v->as.list->count; i++)
            if (!avalue(b, &v->as.list->items[i]))
                return 0;
        return 1;
    }
    if (v->type == NG_VALUE_MAP) {
        size_t i;
        if (!v->as.map)
            return 1;
        for (i = 0; i < v->as.map->count; i++)
            if (!astr(b, v->as.map->entries[i].key, strlen(v->as.map->entries[i].key)) ||
                !avalue(b, &v->as.map->entries[i].value))
                return 0;
        return 1;
    }
    return 1;
}
static int aprop(blob* b, const prop* p) {
    return a64(b, p->key) && avalue(b, &p->v);
}
ng_status ng_save(ng_graph* g) {
    blob b = {0, 0, 0};
    FILE* f;
    char tmp[4096];
    size_t i, j;
    unsigned char h[32];
    ng_status vs;
    if (!g)
        return NG_INVALID_ARGUMENT;
    vs = ng_validate(g);
    if (vs != NG_OK)
        return vs;
    if (strlen(g->path) > sizeof(tmp) - 6)
        return NG_INVALID_ARGUMENT;
    if (!a64(&b, g->ns) || !a64(&b, g->nn) || !a64(&b, g->nr) || !a64(&b, g->nc) ||
        !a64(&b, g->nix) || !a64(&b, g->next_sym) || !a64(&b, g->next_node) ||
        !a64(&b, g->next_rel))
        goto oom;
    for (i = 0; i < g->ns; i++)
        if (!a64(&b, g->sy[i].id) || !astr(&b, g->sy[i].s, strlen(g->sy[i].s)))
            goto oom;
    for (i = 0; i < g->nn; i++) {
        node_i* n = &g->no[i];
        if (!a64(&b, n->id) || !a64(&b, n->nl))
            goto oom;
        for (j = 0; j < n->nl; j++)
            if (!a64(&b, n->labels[j]))
                goto oom;
        if (!a64(&b, n->np))
            goto oom;
        for (j = 0; j < n->np; j++)
            if (!aprop(&b, &n->p[j]))
                goto oom;
    }
    for (i = 0; i < g->nr; i++) {
        rel_i* r = &g->re[i];
        if (!a64(&b, r->id) || !a64(&b, r->src) || !a64(&b, r->dst) || !a64(&b, r->type) ||
            !a64(&b, r->np))
            goto oom;
        for (j = 0; j < r->np; j++)
            if (!aprop(&b, &r->p[j]))
                goto oom;
    }
    for (i = 0; i < g->nc; i++)
        if (!a64(&b, (uint64_t)g->co[i].kind) || !a64(&b, g->co[i].label) || !a64(&b, g->co[i].key))
            goto oom;
    for (i = 0; i < g->nix; i++)
        if (!a64(&b, g->ix[i].label) || !a64(&b, g->ix[i].key))
            goto oom;
    memcpy(h, "NAUTY", 5);
    h[5] = 3;
    put64(h + 8, (uint64_t)b.n);
    put64(h + 16, g->next_node ^ g->next_rel ^ g->next_sym);
    put64(h + 24, hash32(b.p, b.n));
    (void)snprintf(tmp, sizeof(tmp), "%s.tmp", g->path);
    f = fopen(tmp, "wb");
    if (!f || fwrite(h, 1, 32, f) != 32 || fwrite(b.p, 1, b.n, f) != b.n || fclose(f) != 0) {
        if (f)
            fclose(f);
        remove(tmp);
        free(b.p);
        return NG_IO_ERROR;
    }
    if (rename(tmp, g->path) != 0) {
        remove(tmp);
        free(b.p);
        return NG_IO_ERROR;
    }
    free(b.p);
    return NG_OK;
oom:
    free(b.p);
    return NG_OOM;
}
typedef struct {
    const unsigned char* p;
    size_t n, o;
} cursor;
static int take64(cursor* c, uint64_t* v) {
    if (c->o > c->n || c->n - c->o < 8)
        return 0;
    *v = get64(c->p + c->o);
    c->o += 8;
    return 1;
}
static int take_bytes(cursor* c, const unsigned char** p, size_t n) {
    if (n > c->n - c->o)
        return 0;
    *p = c->p + c->o;
    c->o += n;
    return 1;
}
static int load_value(cursor* c, ng_value* v) {
    uint64_t t, n, u;
    const unsigned char* p;
    if (!take64(c, &t) || !take64(c, &n) || t > NG_VALUE_MAP || n > SIZE_MAX)
        return 0;
    memset(v, 0, sizeof(*v));
    v->type = (ng_value_type)t;
    v->length = (size_t)n;
    if (t == NG_VALUE_STRING) {
        if (!take_bytes(c, &p, v->length))
            return 0;
        v->as.string = (char*)malloc(v->length + 1);
        if (!v->as.string)
            return 0;
        memcpy((char*)v->as.string, p, v->length);
        ((char*)v->as.string)[v->length] = 0;
    } else if (t == NG_VALUE_BYTES) {
        if (!take_bytes(c, &p, v->length))
            return 0;
        v->as.bytes = (unsigned char*)malloc(v->length);
        if (v->length && !v->as.bytes)
            return 0;
        memcpy((unsigned char*)v->as.bytes, p, v->length);
    } else if (t == NG_VALUE_BOOL || t == NG_VALUE_INT64 || t == NG_VALUE_DOUBLE) {
        if (!take64(c, &u))
            return 0;
        if (t == NG_VALUE_BOOL)
            v->as.boolean = (int)u;
        else if (t == NG_VALUE_INT64)
            v->as.integer = (int64_t)u;
        else
            memcpy(&v->as.real, &u, 8);
    } else if (t == NG_VALUE_LIST) {
        size_t i;
        ng_value_list* list = (ng_value_list*)calloc(1, sizeof(*list));
        if (!list)
            return 0;
        list->count = (size_t)n;
        if (list->count) {
            list->items = (ng_value*)calloc(list->count, sizeof(*list->items));
            if (!list->items) {
                free(list);
                return 0;
            }
            for (i = 0; i < list->count; i++) {
                if (!load_value(c, &list->items[i])) {
                    v->type = NG_VALUE_LIST;
                    v->as.list = list;
                    valfree(v);
                    return 0;
                }
            }
        }
        v->as.list = list;
    } else if (t == NG_VALUE_MAP) {
        size_t i;
        ng_value_map* map = (ng_value_map*)calloc(1, sizeof(*map));
        if (!map)
            return 0;
        map->count = (size_t)n;
        if (map->count) {
            map->entries = (ng_value_map_entry*)calloc(map->count, sizeof(*map->entries));
            if (!map->entries) {
                free(map);
                return 0;
            }
            for (i = 0; i < map->count; i++) {
                uint64_t key_length;
                const unsigned char* key_data;
                if (!take64(c, &key_length) || key_length > SIZE_MAX ||
                    !take_bytes(c, &key_data, (size_t)key_length)) {
                    v->type = NG_VALUE_MAP;
                    v->as.map = map;
                    valfree(v);
                    return 0;
                }
                map->entries[i].key = (char*)malloc((size_t)key_length + 1);
                if (!map->entries[i].key) {
                    v->type = NG_VALUE_MAP;
                    v->as.map = map;
                    valfree(v);
                    return 0;
                }
                memcpy((char*)map->entries[i].key, key_data, (size_t)key_length);
                ((char*)map->entries[i].key)[key_length] = 0;
                if (!load_value(c, &map->entries[i].value)) {
                    v->type = NG_VALUE_MAP;
                    v->as.map = map;
                    valfree(v);
                    return 0;
                }
            }
        }
        v->as.map = map;
    }
    return 1;
}
ng_status ng_open(ng_graph** o, const char* p) {
    FILE* f;
    unsigned char h[32], *d;
    uint64_t z, ns, nn, nr, nc = 0, nix = 0;
    size_t i, j;
    cursor c;
    ng_status s;
    if (!o || !p)
        return NG_INVALID_ARGUMENT;
    s = init(o, p);
    if (s != NG_OK)
        return s;
    f = fopen(p, "rb");
    if (!f)
        return NG_OK;
    if (fread(h, 1, 32, f) != 32 || memcmp(h, "NAUTY", 5) != 0 ||
        (h[5] != 1 && h[5] != 2 && h[5] != 3)) {
        fclose(f);
        ng_close(*o);
        return NG_CORRUPT;
    }
    z = get64(h + 8);
    if (z > SIZE_MAX) {
        fclose(f);
        ng_close(*o);
        return NG_CORRUPT;
    }
    d = (unsigned char*)malloc((size_t)z);
    if (z && !d) {
        fclose(f);
        ng_close(*o);
        return NG_OOM;
    }
    if (fread(d, 1, (size_t)z, f) != (size_t)z) {
        free(d);
        fclose(f);
        ng_close(*o);
        return NG_CORRUPT;
    }
    if (fgetc(f) != EOF || ferror(f)) {
        free(d);
        fclose(f);
        ng_close(*o);
        return NG_CORRUPT;
    }
    fclose(f);
    if (hash32(d, (size_t)z) != (uint32_t)get64(h + 24)) {
        free(d);
        ng_close(*o);
        return NG_CORRUPT;
    }
    c.p = d;
    c.n = (size_t)z;
    c.o = 0;
    if (!take64(&c, &ns) || !take64(&c, &nn) || !take64(&c, &nr) ||
        (h[5] >= 2 && !take64(&c, &nc)) || (h[5] >= 3 && !take64(&c, &nix)) ||
        !take64(&c, &(*o)->next_sym) || !take64(&c, &(*o)->next_node) ||
        !take64(&c, &(*o)->next_rel) || ns > SIZE_MAX || nn > SIZE_MAX || nr > SIZE_MAX ||
        nc > SIZE_MAX || nix > SIZE_MAX) {
        free(d);
        ng_close(*o);
        return NG_CORRUPT;
    }
    for (i = 0; i < (size_t)ns; i++) {
        uint64_t id, len;
        const unsigned char* q;
        if (!take64(&c, &id) || !take64(&c, &len) || len > SIZE_MAX ||
            !take_bytes(&c, &q, (size_t)len) || memchr(q, 0, (size_t)len)) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        if (!grow((void**)&(*o)->sy, &(*o)->cs, (*o)->ns + 1, sizeof(*(*o)->sy))) {
            free(d);
            ng_close(*o);
            return NG_OOM;
        }
        (*o)->sy[(*o)->ns].id = id;
        (*o)->sy[(*o)->ns].s = (char*)malloc((size_t)len + 1);
        if (!(*o)->sy[(*o)->ns].s) {
            free(d);
            ng_close(*o);
            return NG_OOM;
        }
        memcpy((*o)->sy[(*o)->ns].s, q, (size_t)len);
        (*o)->sy[(*o)->ns].s[len] = 0;
        (*o)->ns++;
    }
    for (i = 0; i < (size_t)nn; i++) {
        node_i* x;
        uint64_t id, nl, np;
        if (!take64(&c, &id) || !take64(&c, &nl) || nl > SIZE_MAX ||
            !grow((void**)&(*o)->no, &(*o)->cn, (*o)->nn + 1, sizeof(*(*o)->no))) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        x = &(*o)->no[(*o)->nn++];
        memset(x, 0, sizeof(*x));
        x->id = id;
        x->nl = (size_t)nl;
        if (nl) {
            x->labels = malloc((size_t)nl * sizeof(*x->labels));
            if (!x->labels) {
                free(d);
                ng_close(*o);
                return NG_OOM;
            }
            for (j = 0; j < (size_t)nl; j++)
                if (!take64(&c, (uint64_t*)&x->labels[j])) {
                    free(d);
                    ng_close(*o);
                    return NG_CORRUPT;
                }
        }
        if (!take64(&c, &np) || np > SIZE_MAX) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        for (j = 0; j < (size_t)np; j++) {
            uint64_t key;
            if (!take64(&c, &key) || !grow((void**)&x->p, &x->cap, x->np + 1, sizeof(*x->p)) ||
                !load_value(&c, &x->p[x->np].v)) {
                free(d);
                ng_close(*o);
                return NG_CORRUPT;
            }
            x->p[x->np++].key = key;
        }
    }
    for (i = 0; i < (size_t)nr; i++) {
        rel_i* r;
        uint64_t np;
        if (!grow((void**)&(*o)->re, &(*o)->cr, (*o)->nr + 1, sizeof(*(*o)->re))) {
            free(d);
            ng_close(*o);
            return NG_OOM;
        }
        r = &(*o)->re[(*o)->nr++];
        memset(r, 0, sizeof(*r));
        if (!take64(&c, &r->id) || !take64(&c, &r->src) || !take64(&c, &r->dst) ||
            !take64(&c, &r->type) || !take64(&c, &np) || np > SIZE_MAX) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        for (j = 0; j < (size_t)np; j++) {
            uint64_t key;
            if (!take64(&c, &key) || !grow((void**)&r->p, &r->cap, r->np + 1, sizeof(*r->p)) ||
                !load_value(&c, &r->p[r->np].v)) {
                free(d);
                ng_close(*o);
                return NG_CORRUPT;
            }
            r->p[r->np++].key = key;
        }
    }
    for (i = 0; i < (size_t)nc; i++) {
        uint64_t kind, label, key;
        if (!take64(&c, &kind) || !take64(&c, &label) || !take64(&c, &key) ||
            !grow((void**)&(*o)->co, &(*o)->cc, (*o)->nc + 1, sizeof(*(*o)->co))) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        (*o)->co[(*o)->nc].kind = (ng_node_constraint_kind)kind;
        (*o)->co[(*o)->nc].label = label;
        (*o)->co[(*o)->nc].key = key;
        (*o)->nc++;
    }
    for (i = 0; i < (size_t)nix; i++) {
        uint64_t label, key;
        if (!take64(&c, &label) || !take64(&c, &key) ||
            !grow((void**)&(*o)->ix, &(*o)->cix, (*o)->nix + 1, sizeof(*(*o)->ix))) {
            free(d);
            ng_close(*o);
            return NG_CORRUPT;
        }
        (*o)->ix[(*o)->nix].label = label;
        (*o)->ix[(*o)->nix].key = key;
        (*o)->nix++;
    }
    if (c.o != c.n) {
        free(d);
        ng_close(*o);
        return NG_CORRUPT;
    }
    free(d);
    return ng_validate(*o);
}
static void valfree(ng_value* v) {
    if (v->type == NG_VALUE_STRING || v->type == NG_VALUE_BYTES)
        free((void*)(v->type == NG_VALUE_STRING ? (void*)v->as.string : (void*)v->as.bytes));
    else if (v->type == NG_VALUE_LIST && v->as.list) {
        size_t i;
        ng_value_list* l = (ng_value_list*)v->as.list;
        for (i = 0; i < l->count; i++)
            valfree(&l->items[i]);
        free(l->items);
        free(l);
    } else if (v->type == NG_VALUE_MAP && v->as.map) {
        size_t i;
        ng_value_map* map = (ng_value_map*)v->as.map;
        for (i = 0; i < map->count; i++) {
            free((void*)map->entries[i].key);
            valfree(&map->entries[i].value);
        }
        free(map->entries);
        free(map);
    }
}
static ng_status valcopy(ng_value* dst, const ng_value* src) {
    *dst = *src;
    if (src->type == NG_VALUE_STRING) {
        dst->as.string = dupstr(src->as.string);
        if (!dst->as.string)
            return NG_OOM;
    } else if (src->type == NG_VALUE_BYTES) {
        dst->as.bytes = (unsigned char*)malloc(src->length);
        if (src->length && !dst->as.bytes)
            return NG_OOM;
        memcpy((void*)dst->as.bytes, src->as.bytes, src->length);
    } else if (src->type == NG_VALUE_LIST) {
        size_t i;
        ng_value_list* l;
        if (!src->as.list) {
            dst->as.list = NULL;
            return NG_OK;
        }
        l = (ng_value_list*)calloc(1, sizeof(*l));
        if (!l)
            return NG_OOM;
        l->count = src->as.list->count;
        if (l->count) {
            l->items = (ng_value*)calloc(l->count, sizeof(*l->items));
            if (!l->items) {
                free(l);
                return NG_OOM;
            }
            for (i = 0; i < l->count; i++) {
                ng_status s = valcopy(&l->items[i], &src->as.list->items[i]);
                if (s != NG_OK) {
                    ng_value tmp;
                    tmp.type = NG_VALUE_LIST;
                    tmp.as.list = l;
                    valfree(&tmp);
                    return s;
                }
            }
        }
        dst->as.list = l;
        dst->length = l->count;
    } else if (src->type == NG_VALUE_MAP) {
        size_t i;
        ng_value_map* map;
        if (!src->as.map) {
            dst->as.map = NULL;
            return NG_OK;
        }
        map = (ng_value_map*)calloc(1, sizeof(*map));
        if (!map)
            return NG_OOM;
        map->count = src->as.map->count;
        if (map->count) {
            map->entries = (ng_value_map_entry*)calloc(map->count, sizeof(*map->entries));
            if (!map->entries) {
                free(map);
                return NG_OOM;
            }
            for (i = 0; i < map->count; i++) {
                map->entries[i].key = dupstr(src->as.map->entries[i].key);
                if (!map->entries[i].key ||
                    valcopy(&map->entries[i].value, &src->as.map->entries[i].value) != NG_OK) {
                    ng_value tmp = {.type = NG_VALUE_MAP, .as.map = map};
                    valfree(&tmp);
                    return NG_OOM;
                }
            }
        }
        dst->as.map = map;
        dst->length = map->count;
    }
    return NG_OK;
}
static int ng_valid_value(const ng_value* v) {
    size_t i;
    if (!v || v->type > NG_VALUE_MAP)
        return 0;
    if (v->type == NG_VALUE_BOOL && (v->as.boolean != 0 && v->as.boolean != 1))
        return 0;
    if (v->type == NG_VALUE_STRING && v->length && !v->as.string)
        return 0;
    if (v->type == NG_VALUE_BYTES && v->length && !v->as.bytes)
        return 0;
    if (v->type == NG_VALUE_LIST) {
        if (v->length && !v->as.list)
            return 0;
        if (!v->as.list)
            return 1;
        if (v->length != v->as.list->count)
            return 0;
        for (i = 0; i < v->as.list->count; i++)
            if (!ng_valid_value(&v->as.list->items[i]))
                return 0;
    }
    if (v->type == NG_VALUE_MAP) {
        if (v->length && !v->as.map)
            return 0;
        if (!v->as.map)
            return 1;
        if (v->length != v->as.map->count)
            return 0;
        for (i = 0; i < v->as.map->count; i++)
            if (!v->as.map->entries[i].key || !ng_valid_value(&v->as.map->entries[i].value))
                return 0;
    }
    return 1;
}
void ng_close(ng_graph* g) {
    size_t i, j;
    if (!g)
        return;
    for (i = 0; i < g->ns; i++) {
        free(g->sy[i].s);
    }
    for (i = 0; i < g->nn; i++) {
        free(g->no[i].labels);
        for (j = 0; j < g->no[i].np; j++)
            valfree(&g->no[i].p[j].v);
        free(g->no[i].p);
    }
    for (i = 0; i < g->nr; i++) {
        for (j = 0; j < g->re[i].np; j++)
            valfree(&g->re[i].p[j].v);
        free(g->re[i].p);
    }
    free(g->sy);
    free(g->no);
    free(g->re);
    free(g->co);
    free(g->ix);
    free(g->ao);
    free(g->ai);
    for (i = 0; i < g->procedure_count; i++)
        free(g->procedures[i].name);
    free(g->procedures);
    free(g->path);
    free(g);
}
ng_status ng_symbol(ng_graph* g, const char* s, ng_symbol_id* o) {
    size_t i;
    char* copy;
    if (!g || !s || !o)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->ns; i++)
        if (!strcmp(g->sy[i].s, s)) {
            *o = g->sy[i].id;
            return NG_OK;
        }
    copy = dupstr(s);
    if (!copy)
        return NG_OOM;
    if (!grow((void**)&g->sy, &g->cs, g->ns + 1, sizeof(*g->sy))) {
        free(copy);
        return NG_OOM;
    }
    g->sy[g->ns].id = g->next_sym++;
    g->sy[g->ns].s = copy;
    *o = g->sy[g->ns++].id;
    return NG_OK;
}
static node_i* node(ng_graph* g, ng_id id) {
    size_t i;
    for (i = 0; i < g->nn; i++)
        if (g->no[i].id == id)
            return &g->no[i];
    return NULL;
}
static const prop* findprop(const prop* p, size_t n, ng_symbol_id k) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i].key == k)
            return &p[i];
    return NULL;
}
ng_status ng_node_create(ng_graph* g, const ng_symbol_id* l, size_t n, ng_id* o) {
    node_i* x;
    ng_symbol_id* labels = NULL;
    size_t i;
    if (!g || !o || (n && !l))
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < n; i++)
        if (!l[i] || !ng_symbol_name(g, l[i]))
            return NG_NOT_FOUND;
    if (n) {
        labels = (ng_symbol_id*)malloc(n * sizeof(*l));
        if (!labels)
            return NG_OOM;
        memcpy(labels, l, n * sizeof(*l));
    }
    if (!grow((void**)&g->no, &g->cn, g->nn + 1, sizeof(*g->no))) {
        free(labels);
        return NG_OOM;
    }
    x = &g->no[g->nn++];
    memset(x, 0, sizeof(*x));
    x->id = g->next_node++;
    x->labels = labels;
    x->nl = n;
    *o = x->id;
    return NG_OK;
}
ng_status ng_relationship_create(ng_graph* g, ng_id a, ng_symbol_id t, ng_id b, ng_id* o) {
    rel_i* r;
    if (!g || !o || !node(g, a) || !node(g, b) || !t)
        return NG_INVALID_ARGUMENT;
    if (!ng_symbol_name(g, t))
        return NG_NOT_FOUND;
    if (!grow((void**)&g->re, &g->cr, g->nr + 1, sizeof(*g->re)))
        return NG_OOM;
    r = &g->re[g->nr++];
    memset(r, 0, sizeof(*r));
    r->id = g->next_rel++;
    r->src = a;
    r->dst = b;
    r->type = t;
    *o = r->id;
    return NG_OK;
}
ng_status ng_relationship_delete(ng_graph* g, ng_relationship_id id) {
    size_t i, j;
    if (!g)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++)
        if (g->re[i].id == id) {
            for (j = 0; j < g->re[i].np; j++)
                valfree(&g->re[i].p[j].v);
            free(g->re[i].p);
            if (i + 1 < g->nr)
                memmove(&g->re[i], &g->re[i + 1], (g->nr - i - 1) * sizeof(*g->re));
            g->nr--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
ng_status ng_node_delete(ng_graph* g, ng_node_id id) {
    size_t i, j;
    if (!g)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nn; i++)
        if (g->no[i].id == id) {
            for (j = g->nr; j > 0; j--)
                if (g->re[j - 1].src == id || g->re[j - 1].dst == id)
                    ng_relationship_delete(g, g->re[j - 1].id);
            free(g->no[i].labels);
            for (j = 0; j < g->no[i].np; j++)
                valfree(&g->no[i].p[j].v);
            free(g->no[i].p);
            if (i + 1 < g->nn)
                memmove(&g->no[i], &g->no[i + 1], (g->nn - i - 1) * sizeof(*g->no));
            g->nn--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
static ng_status setprop(prop** pp, size_t* n, size_t* cap, ng_symbol_id k, const ng_value* v) {
    size_t i;
    prop* p;
    ng_value copy;
    if (!v || !k)
        return NG_INVALID_ARGUMENT;
    if (!ng_valid_value(v))
        return NG_INVALID_ARGUMENT;
    if (valcopy(&copy, v) != NG_OK)
        return NG_OOM;
    for (i = 0; i < *n; i++)
        if ((*pp)[i].key == k) {
            valfree(&(*pp)[i].v);
            (*pp)[i].v = copy;
            return NG_OK;
        }
    if (!grow((void**)pp, cap, *n + 1, sizeof(**pp))) {
        valfree(&copy);
        return NG_OOM;
    }
    p = &(*pp)[(*n)++];
    p->key = k;
    p->v = copy;
    return NG_OK;
}
static ng_status ng_node_set_constraint_check(const ng_graph* g,
                                              const node_i* n,
                                              ng_symbol_id k,
                                              const ng_value* v) {
    size_t i, j;
    for (i = 0; i < g->nc; i++)
        if (g->co[i].key == k && ng_node_matches_label(n, g->co[i].label)) {
            if (g->co[i].kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY && v->type == NG_VALUE_NULL)
                return NG_NOT_FOUND;
            if (g->co[i].kind == NG_NODE_CONSTRAINT_UNIQUE_PROPERTY && v->type != NG_VALUE_NULL)
                for (j = 0; j < g->nn; j++) {
                    const prop* p;
                    if (g->no[j].id == n->id || !ng_node_matches_label(&g->no[j], g->co[i].label))
                        continue;
                    p = findprop(g->no[j].p, g->no[j].np, k);
                    if (p && p->v.type != NG_VALUE_NULL && ng_value_equal(&p->v, v))
                        return NG_EXISTS;
                }
        }
    return NG_OK;
}
static int ng_node_unset_allowed(const ng_graph* g, const node_i* n, ng_symbol_id k) {
    size_t i;
    for (i = 0; i < g->nc; i++)
        if (g->co[i].kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY && g->co[i].key == k &&
            ng_node_matches_label(n, g->co[i].label))
            return 0;
    return 1;
}
ng_status ng_node_set(ng_graph* g, ng_id id, ng_symbol_id k, const ng_value* v) {
    node_i* n = g ? node(g, id) : NULL;
    ng_status s;
    if (!g || !v || !k)
        return NG_INVALID_ARGUMENT;
    if (!ng_valid_value(v))
        return NG_INVALID_ARGUMENT;
    if (!n)
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, k))
        return NG_NOT_FOUND;
    s = ng_node_set_constraint_check(g, n, k, v);
    if (s != NG_OK)
        return s;
    return setprop(&n->p, &n->np, &n->cap, k, v);
}
static int ng_label_list_matches(const ng_symbol_id* l, size_t n, ng_symbol_id label) {
    size_t i;
    if (!label)
        return 1;
    for (i = 0; i < n; i++)
        if (l[i] == label)
            return 1;
    return 0;
}
static const ng_value* ng_property_list_find(const ng_property* p, size_t n, ng_symbol_id key) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i].key == key)
            return &p[i].value;
    return NULL;
}
static ng_status ng_node_create_properties_check(
    const ng_graph* g, const ng_symbol_id* l, size_t nl, const ng_property* p, size_t np) {
    size_t i, j;
    if (!g || (nl && !l) || (np && !p))
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < nl; i++) {
        if (!l[i] || !ng_symbol_name(g, l[i]))
            return NG_NOT_FOUND;
        for (j = 0; j < i; j++)
            if (l[j] == l[i])
                return NG_INVALID_ARGUMENT;
    }
    for (i = 0; i < np; i++) {
        if (!p[i].key)
            return NG_INVALID_ARGUMENT;
        if (!ng_symbol_name(g, p[i].key))
            return NG_NOT_FOUND;
        if (!ng_valid_value(&p[i].value) ||
            (p[i].value.type == NG_VALUE_STRING && !p[i].value.as.string) ||
            (p[i].value.type == NG_VALUE_BYTES && p[i].value.length && !p[i].value.as.bytes))
            return NG_INVALID_ARGUMENT;
        for (j = 0; j < i; j++)
            if (p[j].key == p[i].key)
                return NG_INVALID_ARGUMENT;
    }
    for (i = 0; i < g->nc; i++)
        if (ng_label_list_matches(l, nl, g->co[i].label)) {
            const ng_value* v = ng_property_list_find(p, np, g->co[i].key);
            if (g->co[i].kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY &&
                (!v || v->type == NG_VALUE_NULL))
                return NG_NOT_FOUND;
            if (g->co[i].kind == NG_NODE_CONSTRAINT_UNIQUE_PROPERTY && v &&
                v->type != NG_VALUE_NULL)
                for (j = 0; j < g->nn; j++) {
                    const prop* x;
                    if (!ng_node_matches_label(&g->no[j], g->co[i].label))
                        continue;
                    x = findprop(g->no[j].p, g->no[j].np, g->co[i].key);
                    if (x && x->v.type != NG_VALUE_NULL && ng_value_equal(&x->v, v))
                        return NG_EXISTS;
                }
        }
    return NG_OK;
}
ng_status ng_node_create_with_properties(ng_graph* g,
                                         const ng_symbol_id* l,
                                         size_t nl,
                                         const ng_property* p,
                                         size_t np,
                                         ng_node_id* out) {
    ng_node_id id;
    size_t i;
    ng_status s;
    if (!out)
        return NG_INVALID_ARGUMENT;
    s = ng_node_create_properties_check(g, l, nl, p, np);
    if (s != NG_OK)
        return s;
    s = ng_node_create(g, l, nl, &id);
    if (s != NG_OK)
        return s;
    for (i = 0; i < np; i++) {
        s = ng_node_set(g, id, p[i].key, &p[i].value);
        if (s != NG_OK) {
            ng_node_delete(g, id);
            return s;
        }
    }
    *out = id;
    return NG_OK;
}
ng_status ng_relationship_set(ng_graph* g, ng_id id, ng_symbol_id k, const ng_value* v) {
    size_t i;
    if (!g || !v)
        return NG_INVALID_ARGUMENT;
    if (!ng_valid_value(v))
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++)
        if (g->re[i].id == id)
            return setprop(&g->re[i].p, &g->re[i].np, &g->re[i].cap, k, v);
    return NG_NOT_FOUND;
}
static ng_status unsetprop(prop* p, size_t* n, ng_symbol_id k) {
    size_t i;
    if (!k)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < *n; i++)
        if (p[i].key == k) {
            valfree(&p[i].v);
            if (i + 1 < *n)
                memmove(&p[i], &p[i + 1], (*n - i - 1) * sizeof(*p));
            (*n)--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
ng_status ng_node_unset(ng_graph* g, ng_node_id id, ng_symbol_id k) {
    node_i* n;
    if (!g || !k)
        return NG_INVALID_ARGUMENT;
    n = node(g, id);
    if (!n)
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, k))
        return NG_NOT_FOUND;
    if (!ng_node_unset_allowed(g, n, k))
        return NG_NOT_FOUND;
    return unsetprop(n->p, &n->np, k);
}
ng_status ng_relationship_unset(ng_graph* g, ng_relationship_id id, ng_symbol_id k) {
    size_t i;
    if (!g)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++)
        if (g->re[i].id == id)
            return unsetprop(g->re[i].p, &g->re[i].np, k);
    return NG_NOT_FOUND;
}
size_t ng_node_count(const ng_graph* g) {
    return g ? g->nn : 0;
}
size_t ng_relationship_count(const ng_graph* g) {
    return g ? g->nr : 0;
}
size_t ng_symbol_count(const ng_graph* g) {
    return g ? g->ns : 0;
}
ng_status ng_node_get(const ng_graph* g, ng_node_id id, ng_node* out) {
    size_t i;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nn; i++)
        if (g->no[i].id == id) {
            out->id = id;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
ng_status ng_relationship_get(const ng_graph* g, ng_relationship_id id, ng_relationship* out) {
    size_t i;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++)
        if (g->re[i].id == id) {
            out->id = g->re[i].id;
            out->source = g->re[i].src;
            out->target = g->re[i].dst;
            out->type = g->re[i].type;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
static int rebuild_adjacency(ng_graph* g) {
    size_t i, j, *counts;
    if (!g)
        return 0;
    free(g->ao);
    free(g->ai);
    g->ao = NULL;
    g->ai = NULL;
    g->an = 0;
    if (!g->nn)
        return 1;
    counts = calloc(g->nn, sizeof(*counts));
    if (!counts)
        return 0;
    for (i = 0; i < g->nr; i++)
        for (j = 0; j < g->nn; j++)
            if (g->no[j].id == g->re[i].src || g->no[j].id == g->re[i].dst)
                counts[j]++;
    g->ao = calloc(g->nn + 1, sizeof(*g->ao));
    if (!g->ao) {
        free(counts);
        return 0;
    }
    for (i = 0; i < g->nn; i++)
        g->ao[i + 1] = g->ao[i] + counts[i];
    g->an = g->ao[g->nn];
    g->ai = malloc(g->an * sizeof(*g->ai));
    if (g->an && !g->ai) {
        free(counts);
        free(g->ao);
        g->ao = NULL;
        return 0;
    }
    memset(counts, 0, g->nn * sizeof(*counts));
    for (i = 0; i < g->nr; i++)
        for (j = 0; j < g->nn; j++)
            if (g->no[j].id == g->re[i].src || g->no[j].id == g->re[i].dst)
                g->ai[g->ao[j] + counts[j]++] = i;
    free(counts);
    return 1;
}
ng_status ng_node_relationships(const ng_graph* g,
                                ng_node_id id,
                                ng_direction d,
                                ng_symbol_id type,
                                ng_relationship_visitor visit,
                                void* ctx) {
    size_t i, j;
    ng_relationship r;
    ng_graph* x = (ng_graph*)g;
    if (!g || !visit || d > NG_DIRECTION_EITHER)
        return NG_INVALID_ARGUMENT;
    if (!node(x, id))
        return NG_NOT_FOUND;
    if (!rebuild_adjacency(x))
        return NG_OOM;
    for (j = 0; j < g->nn; j++)
        if (g->no[j].id == id)
            for (i = g->ao[j]; i < g->ao[j + 1]; i++) {
                const rel_i* z = &g->re[g->ai[i]];
                int match = (d == NG_DIRECTION_OUTGOING   ? z->src == id
                             : d == NG_DIRECTION_INCOMING ? z->dst == id
                                                          : (z->src == id || z->dst == id));
                if (match && (!type || z->type == type)) {
                    r.id = z->id;
                    r.source = z->src;
                    r.target = z->dst;
                    r.type = z->type;
                    if (!visit(&r, ctx))
                        return NG_OK;
                }
            }
    return NG_OK;
}
ng_status ng_node_has_label(const ng_graph* g, ng_node_id id, ng_symbol_id label, int* out) {
    size_t i;
    node_i* n;
    if (!g || !out || !label)
        return NG_INVALID_ARGUMENT;
    n = node((ng_graph*)g, id);
    if (!n)
        return NG_NOT_FOUND;
    *out = 0;
    for (i = 0; i < n->nl; i++)
        if (n->labels[i] == label) {
            *out = 1;
            break;
        }
    return NG_OK;
}
ng_status ng_node_property(const ng_graph* g, ng_node_id id, ng_symbol_id key, ng_value* out) {
    node_i* n;
    const prop* p;
    if (!g || !out || !key)
        return NG_INVALID_ARGUMENT;
    n = node((ng_graph*)g, id);
    if (!n)
        return NG_NOT_FOUND;
    p = findprop(n->p, n->np, key);
    if (!p)
        return NG_NOT_FOUND;
    *out = p->v;
    return NG_OK;
}
ng_status ng_relationship_property(const ng_graph* g,
                                   ng_relationship_id id,
                                   ng_symbol_id key,
                                   ng_value* out) {
    size_t i;
    const prop* p;
    if (!g || !out || !key)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++)
        if (g->re[i].id == id) {
            p = findprop(g->re[i].p, g->re[i].np, key);
            if (!p)
                return NG_NOT_FOUND;
            *out = p->v;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
ng_status ng_traverse(const ng_graph* g,
                      ng_node_id start,
                      const ng_traversal_options* o,
                      ng_node_visitor visit,
                      void* ctx) {
    ng_node_id* q;
    uint32_t* d;
    unsigned char* seen;
    size_t head = 0, tail = 0, i, j;
    uint64_t count = 0;
    ng_traversal_options def = {NG_DIRECTION_EITHER, 0, 0, UINT32_MAX, 0};
    if (!g || !visit)
        return NG_INVALID_ARGUMENT;
    if (!o)
        o = &def;
    if (o->direction > NG_DIRECTION_EITHER || (o->type_count && !o->types))
        return NG_INVALID_ARGUMENT;
    if (!node((ng_graph*)g, start))
        return NG_NOT_FOUND;
    q = malloc(g->nn * sizeof(*q));
    d = malloc(g->nn * sizeof(*d));
    seen = calloc(g->nn, 1);
    if ((g->nn && (!q || !d || !seen))) {
        free(q);
        free(d);
        free(seen);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        if (g->no[i].id == start) {
            seen[i] = 1;
            q[tail] = start;
            d[tail++] = 0;
            break;
        }
    while (head < tail) {
        ng_node_id cur = q[head];
        uint32_t depth = d[head++];
        if (o->visit_limit && count >= o->visit_limit)
            break;
        count++;
        if (!visit(cur, depth, ctx))
            break;
        if (depth == o->max_depth)
            continue;
        for (i = 0; i < g->nr; i++) {
            const rel_i* r = &g->re[i];
            ng_node_id next = 0;
            int dir = (o->direction == NG_DIRECTION_OUTGOING   ? r->src == cur
                       : o->direction == NG_DIRECTION_INCOMING ? r->dst == cur
                                                               : (r->src == cur || r->dst == cur));
            if (!dir)
                continue;
            if (o->type_count) {
                int ok = 0;
                for (j = 0; j < o->type_count; j++)
                    if (o->types[j] == r->type) {
                        ok = 1;
                        break;
                    }
                if (!ok)
                    continue;
            }
            if (o->direction == NG_DIRECTION_INCOMING ||
                (o->direction == NG_DIRECTION_EITHER && r->src != cur))
                next = r->src;
            else
                next = r->dst;
            for (j = 0; j < g->nn; j++)
                if (g->no[j].id == next && !seen[j]) {
                    seen[j] = 1;
                    q[tail] = next;
                    d[tail++] = depth + 1;
                    break;
                }
        }
    }
    free(q);
    free(d);
    free(seen);
    return NG_OK;
}
static int ng_analytics_symbol_ok(const ng_graph* g, ng_symbol_id type) {
    size_t i;
    if (!type)
        return 1;
    for (i = 0; i < g->ns; i++)
        if (g->sy[i].id == type)
            return 1;
    return 0;
}
static int ng_analytics_rel_ok(const rel_i* r, ng_symbol_id type) {
    return !type || r->type == type;
}
static int ng_analytics_has_size(const size_t* a, size_t n, size_t v) {
    size_t i;
    for (i = 0; i < n; i++)
        if (a[i] == v)
            return 1;
    return 0;
}
static ng_status ng_analytics_neighbors(
    const ng_graph* g, size_t pos, ng_symbol_id type, size_t** out, size_t* out_count) {
    size_t i, n = 0, cap = 0, *a = NULL;
    if (!out || !out_count)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nr; i++) {
        const rel_i* r = &g->re[i];
        ng_node_id other = 0;
        size_t p;
        if (!ng_analytics_rel_ok(r, type))
            continue;
        if (r->src == g->no[pos].id)
            other = r->dst;
        else if (r->dst == g->no[pos].id)
            other = r->src;
        else
            continue;
        p = ng_node_position(g, other);
        if (p == SIZE_MAX)
            continue;
        if (!ng_analytics_has_size(a, n, p)) {
            if (n == cap) {
                size_t c = cap ? cap * 2 : 8;
                size_t* q = (size_t*)realloc(a, c * sizeof(*a));
                if (!q) {
                    free(a);
                    return NG_OOM;
                }
                a = q;
                cap = c;
            }
            a[n++] = p;
        }
    }
    *out = a;
    *out_count = n;
    return NG_OK;
}
static int ng_analytics_adjacent(const ng_graph* g, size_t a, size_t b, ng_symbol_id type) {
    size_t i;
    ng_node_id x = g->no[a].id, y = g->no[b].id;
    for (i = 0; i < g->nr; i++) {
        const rel_i* r = &g->re[i];
        if (ng_analytics_rel_ok(r, type) &&
            ((r->src == x && r->dst == y) || (r->src == y && r->dst == x)))
            return 1;
    }
    return 0;
}
static ng_status ng_analytics_check_output(
    const ng_graph* g, ng_symbol_id type, const void* out, size_t capacity, size_t* out_count) {
    if (!g)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, type))
        return NG_NOT_FOUND;
    if (out_count)
        *out_count = g->nn;
    if (capacity < g->nn)
        return NG_LIMIT;
    if (g->nn && !out)
        return NG_INVALID_ARGUMENT;
    return NG_OK;
}
ng_status ng_label_propagation(const ng_graph* g,
                               ng_direction direction,
                               ng_symbol_id type,
                               uint32_t iterations,
                               ng_node_component* out,
                               size_t capacity,
                               size_t* out_count) {
    uint64_t* labels;
    uint64_t* next;
    size_t i, j;
    ng_status s;
    if (direction > NG_DIRECTION_EITHER || !iterations)
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    labels = (uint64_t*)malloc(g->nn * sizeof(*labels));
    next = (uint64_t*)malloc(g->nn * sizeof(*next));
    if ((g->nn && !labels) || (g->nn && !next)) {
        free(labels);
        free(next);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        labels[i] = (uint64_t)i;
    for (j = 0; j < iterations; j++) {
        int changed = 0;
        for (i = 0; i < g->nn; i++) {
            uint64_t best = labels[i];
            size_t best_frequency = 0, candidate;
            for (candidate = 0; candidate < g->nn; candidate++) {
                size_t frequency = 0, node_index, edge;
                int candidate_adjacent = 0;
                for (edge = 0; edge < g->nr; edge++) {
                    const rel_i* rel = &g->re[edge];
                    if (!ng_analytics_rel_ok(rel, type))
                        continue;
                    if ((direction == NG_DIRECTION_OUTGOING &&
                         rel->src == g->no[i].id && rel->dst == g->no[candidate].id) ||
                        (direction == NG_DIRECTION_INCOMING &&
                         rel->dst == g->no[i].id && rel->src == g->no[candidate].id) ||
                        (direction == NG_DIRECTION_EITHER &&
                         ((rel->src == g->no[i].id && rel->dst == g->no[candidate].id) ||
                          (rel->dst == g->no[i].id && rel->src == g->no[candidate].id)))) {
                        candidate_adjacent = 1;
                        break;
                    }
                }
                if (!candidate_adjacent)
                    continue;
                for (node_index = 0; node_index < g->nn; node_index++) {
                    for (edge = 0; edge < g->nr; edge++) {
                        const rel_i* rel = &g->re[edge];
                        if (!ng_analytics_rel_ok(rel, type))
                            continue;
                        if (labels[node_index] != labels[candidate])
                            break;
                        if ((direction == NG_DIRECTION_OUTGOING &&
                             rel->src == g->no[i].id && rel->dst == g->no[node_index].id) ||
                            (direction == NG_DIRECTION_INCOMING &&
                             rel->dst == g->no[i].id && rel->src == g->no[node_index].id) ||
                            (direction == NG_DIRECTION_EITHER &&
                             ((rel->src == g->no[i].id && rel->dst == g->no[node_index].id) ||
                              (rel->dst == g->no[i].id && rel->src == g->no[node_index].id)))) {
                            frequency++;
                            break;
                        }
                    }
                }
                if (frequency > best_frequency ||
                    (frequency == best_frequency && labels[candidate] < best)) {
                    best = labels[candidate];
                    best_frequency = frequency;
                }
            }
            next[i] = best;
            if (next[i] != labels[i])
                changed = 1;
        }
        memcpy(labels, next, g->nn * sizeof(*labels));
        if (!changed)
            break;
    }
    for (i = 0; i < g->nn; i++) {
        out[i].node = g->no[i].id;
        out[i].component = labels[i];
    }
    free(labels);
    free(next);
    return NG_OK;
}
ng_status ng_knn(const ng_graph* g,
                 ng_node_id source,
                 ng_direction direction,
                 ng_symbol_id type,
                 size_t k,
                 ng_link_score* out,
                 size_t capacity,
                 size_t* out_count) {
    size_t source_pos, i, j, candidate_count = 0, wanted;
    ng_link_score* candidates;
    if (!g || direction > NG_DIRECTION_EITHER || !k || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    source_pos = ng_node_position(g, source);
    if (source_pos == SIZE_MAX)
        return NG_NOT_FOUND;
    candidates = (ng_link_score*)calloc(g->nn > 1 ? g->nn - 1 : 1, sizeof(*candidates));
    if (!candidates)
        return NG_OOM;
    for (i = 0; i < g->nn; i++) {
        unsigned char* a;
        unsigned char* b;
        size_t intersection = 0, union_count = 0;
        if (i == source_pos)
            continue;
        a = (unsigned char*)calloc(g->nn, 1);
        b = (unsigned char*)calloc(g->nn, 1);
        if (!a || !b) {
            free(a);
            free(b);
            free(candidates);
            return NG_OOM;
        }
        for (j = 0; j < g->nr; j++) {
            const rel_i* r = &g->re[j];
            size_t other = SIZE_MAX;
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[source_pos].id)
                other = ng_node_position(g, r->dst);
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[source_pos].id)
                other = ng_node_position(g, r->src);
            else if (direction == NG_DIRECTION_EITHER) {
                if (r->src == g->no[source_pos].id)
                    other = ng_node_position(g, r->dst);
                else if (r->dst == g->no[source_pos].id)
                    other = ng_node_position(g, r->src);
            }
            if (other != SIZE_MAX)
                a[other] = 1;
            other = SIZE_MAX;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[i].id)
                other = ng_node_position(g, r->dst);
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[i].id)
                other = ng_node_position(g, r->src);
            else if (direction == NG_DIRECTION_EITHER) {
                if (r->src == g->no[i].id)
                    other = ng_node_position(g, r->dst);
                else if (r->dst == g->no[i].id)
                    other = ng_node_position(g, r->src);
            }
            if (other != SIZE_MAX)
                b[other] = 1;
        }
        for (j = 0; j < g->nn; j++) {
            if (a[j] || b[j])
                union_count++;
            if (a[j] && b[j])
                intersection++;
        }
        candidates[candidate_count].source = source;
        candidates[candidate_count].target = g->no[i].id;
        candidates[candidate_count].score = union_count ? (double)intersection / union_count : 0.0;
        candidate_count++;
        free(a);
        free(b);
    }
    for (i = 0; i < candidate_count; i++)
        for (j = i + 1; j < candidate_count; j++)
            if (candidates[j].score > candidates[i].score ||
                (candidates[j].score == candidates[i].score &&
                 candidates[j].target < candidates[i].target)) {
                ng_link_score swap = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = swap;
            }
    wanted = k < candidate_count ? k : candidate_count;
    if (!out_count || !out || capacity < wanted) {
        free(candidates);
        return NG_LIMIT;
    }
    memcpy(out, candidates, wanted * sizeof(*out));
    *out_count = wanted;
    free(candidates);
    return NG_OK;
}
ng_status ng_knn_filtered(const ng_graph* g,
                          ng_node_id source,
                          ng_direction direction,
                          ng_symbol_id type,
                          ng_symbol_id candidate_label,
                          size_t k,
                          ng_link_score* out,
                          size_t capacity,
                          size_t* out_count) {
    ng_link_score* ranked;
    size_t ranked_count = 0, i, selected = 0;
    if (!g || !candidate_label || !ng_analytics_symbol_ok(g, candidate_label))
        return NG_INVALID_ARGUMENT;
    if (!k)
        return NG_INVALID_ARGUMENT;
    ranked = (ng_link_score*)calloc(g->nn ? g->nn : 1, sizeof(*ranked));
    if (!ranked)
        return NG_OOM;
    if (ng_knn(g, source, direction, type, g->nn ? g->nn - 1 : 1, ranked, g->nn, &ranked_count) !=
        NG_OK) {
        free(ranked);
        return NG_PARSE_ERROR;
    }
    for (i = 0; i < ranked_count && selected < k; i++) {
        size_t position = ng_node_position(g, ranked[i].target);
        int has_label = 0;
        if (position != SIZE_MAX) {
            size_t label_index;
            for (label_index = 0; label_index < g->no[position].nl; label_index++)
                if (g->no[position].labels[label_index] == candidate_label) {
                    has_label = 1;
                    break;
                }
        }
        if (has_label) {
            if (selected >= capacity || !out) {
                free(ranked);
                return NG_LIMIT;
            }
            out[selected++] = ranked[i];
        }
    }
    if (!out_count) {
        free(ranked);
        return NG_INVALID_ARGUMENT;
    }
    *out_count = selected;
    free(ranked);
    return NG_OK;
}
ng_status ng_louvain(const ng_graph* g,
                     ng_symbol_id type,
                     uint32_t iterations,
                     ng_node_component* out,
                     size_t capacity,
                     size_t* out_count) {
    uint64_t* community;
    double* degree;
    double* total;
    size_t i, j;
    ng_status s;
    if (!g || !iterations || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    community = (uint64_t*)malloc(g->nn * sizeof(*community));
    degree = (double*)calloc(g->nn, sizeof(*degree));
    total = (double*)calloc(g->nn, sizeof(*total));
    if ((g->nn && !community) || (g->nn && (!degree || !total))) {
        free(community);
        free(degree);
        free(total);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        community[i] = (uint64_t)i;
    for (i = 0; i < g->nr; i++) {
        const rel_i* rel = &g->re[i];
        size_t a, b;
        if (!ng_analytics_rel_ok(rel, type))
            continue;
        a = ng_node_position(g, rel->src);
        b = ng_node_position(g, rel->dst);
        if (a != SIZE_MAX && b != SIZE_MAX) {
            degree[a] += 1.0;
            degree[b] += 1.0;
        }
    }
    for (i = 0; i < g->nn; i++)
        total[i] = degree[i];
    for (j = 0; j < iterations; j++) {
        int changed = 0;
        for (i = 0; i < g->nn; i++) {
            uint64_t old = community[i], best = community[i];
            double best_gain = 0.0;
            size_t candidate;
            total[old] -= degree[i];
            for (candidate = 0; candidate < g->nn; candidate++) {
                double links = 0.0, gain;
                size_t edge;
                for (edge = 0; edge < g->nr; edge++) {
                    const rel_i* rel = &g->re[edge];
                    size_t other = SIZE_MAX;
                    if (!ng_analytics_rel_ok(rel, type))
                        continue;
                    if (rel->src == g->no[i].id)
                        other = ng_node_position(g, rel->dst);
                    else if (rel->dst == g->no[i].id)
                        other = ng_node_position(g, rel->src);
                    if (other != SIZE_MAX && community[other] == (uint64_t)candidate)
                        links += 1.0;
                }
                gain = links - degree[i] * total[candidate] / 2.0;
                if (gain > best_gain ||
                    (gain == best_gain && (uint64_t)candidate < best)) {
                    best = (uint64_t)candidate;
                    best_gain = gain;
                }
            }
            community[i] = best;
            total[best] += degree[i];
            if (best != old)
                changed = 1;
        }
        if (!changed)
            break;
    }
    for (i = 0; i < g->nn; i++) {
        out[i].node = g->no[i].id;
        out[i].component = community[i];
    }
    free(community);
    free(degree);
    free(total);
    return NG_OK;
}
ng_status ng_dijkstra(const ng_graph* g,
                      ng_node_id start,
                      ng_node_id target,
                      ng_direction direction,
                      ng_symbol_id type,
                      ng_symbol_id weight_key,
                      ng_node_id* out_path,
                      size_t capacity,
                      size_t* out_count,
                      double* out_distance) {
    size_t source, destination, i, path_count = 0, path_length;
    double* distance;
    size_t* previous;
    unsigned char* used;
    if (!g || direction > NG_DIRECTION_EITHER || !ng_analytics_symbol_ok(g, type) ||
        (weight_key && !ng_analytics_symbol_ok(g, weight_key)))
        return NG_INVALID_ARGUMENT;
    source = ng_node_position(g, start);
    destination = ng_node_position(g, target);
    if (source == SIZE_MAX || destination == SIZE_MAX)
        return NG_NOT_FOUND;
    distance = (double*)malloc(g->nn * sizeof(*distance));
    previous = (size_t*)malloc(g->nn * sizeof(*previous));
    used = (unsigned char*)calloc(g->nn, 1);
    if ((g->nn && !distance) || (g->nn && (!previous || !used))) {
        free(distance);
        free(previous);
        free(used);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++) {
        distance[i] = 1e300;
        previous[i] = SIZE_MAX;
    }
    distance[source] = 0;
    for (;;) {
        size_t current = SIZE_MAX;
        for (i = 0; i < g->nn; i++)
            if (!used[i] && (current == SIZE_MAX || distance[i] < distance[current]))
                current = i;
        if (current == SIZE_MAX || distance[current] >= 1e299)
            break;
        used[current] = 1;
        if (current == destination)
            break;
        for (i = 0; i < g->nr; i++) {
            const rel_i* r = &g->re[i];
            size_t next = SIZE_MAX;
            double weight = 1.0;
            const prop* property;
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[current].id)
                next = ng_node_position(g, r->dst);
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[current].id)
                next = ng_node_position(g, r->src);
            else if (direction == NG_DIRECTION_EITHER) {
                if (r->src == g->no[current].id)
                    next = ng_node_position(g, r->dst);
                else if (r->dst == g->no[current].id)
                    next = ng_node_position(g, r->src);
            }
            if (next == SIZE_MAX || used[next])
                continue;
            if (weight_key) {
                property = findprop(r->p, r->np, weight_key);
                if (property) {
                    if (property->v.type == NG_VALUE_INT64)
                        weight = (double)property->v.as.integer;
                    else if (property->v.type == NG_VALUE_DOUBLE)
                        weight = property->v.as.real;
                    else
                        return (free(distance), free(previous), free(used), NG_PARSE_ERROR);
                    if (weight < 0)
                        return (free(distance), free(previous), free(used), NG_PARSE_ERROR);
                }
            }
            if (distance[current] + weight < distance[next]) {
                distance[next] = distance[current] + weight;
                previous[next] = current;
            }
        }
    }
    if (distance[destination] >= 1e299) {
        free(distance);
        free(previous);
        free(used);
        if (out_count)
            *out_count = 0;
        return NG_NOT_FOUND;
    }
    for (i = destination;; i = previous[i]) {
        path_count++;
        if (i == source)
            break;
    }
    path_length = path_count;
    if (!out_count || capacity < path_length || !out_path) {
        free(distance);
        free(previous);
        free(used);
        return NG_LIMIT;
    }
    for (i = destination; i != SIZE_MAX; i = previous[i])
        out_path[--path_count] = g->no[i].id;
    *out_count = path_length;
    if (out_distance)
        *out_distance = distance[destination];
    free(distance);
    free(previous);
    free(used);
    return NG_OK;
}
ng_status ng_bfs_path(const ng_graph* g,
                      ng_node_id start,
                      ng_node_id target,
                      ng_direction direction,
                      ng_symbol_id type,
                      ng_node_id* out_path,
                      size_t capacity,
                      size_t* out_count) {
    size_t source, destination, head = 0, tail = 0, i, path_count = 0, path_length;
    size_t* queue;
    size_t* previous;
    unsigned char* visited;
    if (!g || direction > NG_DIRECTION_EITHER || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    source = ng_node_position(g, start);
    destination = ng_node_position(g, target);
    if (source == SIZE_MAX || destination == SIZE_MAX)
        return NG_NOT_FOUND;
    queue = (size_t*)malloc(g->nn * sizeof(*queue));
    previous = (size_t*)malloc(g->nn * sizeof(*previous));
    visited = (unsigned char*)calloc(g->nn, 1);
    if ((g->nn && !queue) || (g->nn && (!previous || !visited))) {
        free(queue);
        free(previous);
        free(visited);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        previous[i] = SIZE_MAX;
    queue[tail++] = source;
    visited[source] = 1;
    while (head < tail && !visited[destination]) {
        size_t current = queue[head++];
        for (i = 0; i < g->nr; i++) {
            const rel_i* r = &g->re[i];
            size_t next = SIZE_MAX;
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[current].id)
                next = ng_node_position(g, r->dst);
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[current].id)
                next = ng_node_position(g, r->src);
            else if (direction == NG_DIRECTION_EITHER) {
                if (r->src == g->no[current].id)
                    next = ng_node_position(g, r->dst);
                else if (r->dst == g->no[current].id)
                    next = ng_node_position(g, r->src);
            }
            if (next == SIZE_MAX || visited[next])
                continue;
            visited[next] = 1;
            previous[next] = current;
            queue[tail++] = next;
        }
    }
    if (!visited[destination]) {
        free(queue);
        free(previous);
        free(visited);
        if (out_count)
            *out_count = 0;
        return NG_NOT_FOUND;
    }
    for (i = destination;; i = previous[i]) {
        path_count++;
        if (i == source)
            break;
    }
    path_length = path_count;
    if (!out_count || !out_path || capacity < path_length) {
        free(queue);
        free(previous);
        free(visited);
        return NG_LIMIT;
    }
    for (i = destination; i != SIZE_MAX; i = previous[i])
        out_path[--path_count] = g->no[i].id;
    *out_count = path_length;
    free(queue);
    free(previous);
    free(visited);
    return NG_OK;
}
static ng_status ng_enumerate_paths_dfs(const ng_graph* g,
                                        size_t current,
                                        size_t target,
                                        ng_direction direction,
                                        ng_symbol_id type,
                                        uint32_t max_depth,
                                        size_t max_paths,
                                        ng_node_id* path,
                                        size_t depth,
                                        unsigned char* visited,
                                        ng_path_visitor visitor,
                                        void* context,
                                        size_t* found,
                                        int* stop) {
    size_t i;
    if (current == target) {
        (*found)++;
        if (!visitor(path, depth + 1, context))
            *stop = 1;
        return NG_OK;
    }
    if (depth >= max_depth || *stop || (max_paths && *found >= max_paths))
        return NG_OK;
    for (i = 0; i < g->nr; i++) {
        const rel_i* r = &g->re[i];
        size_t next = SIZE_MAX;
        if (!ng_analytics_rel_ok(r, type))
            continue;
        if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[current].id)
            next = ng_node_position(g, r->dst);
        else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[current].id)
            next = ng_node_position(g, r->src);
        else if (direction == NG_DIRECTION_EITHER) {
            if (r->src == g->no[current].id)
                next = ng_node_position(g, r->dst);
            else if (r->dst == g->no[current].id)
                next = ng_node_position(g, r->src);
        }
        if (next == SIZE_MAX || visited[next])
            continue;
        visited[next] = 1;
        path[depth + 1] = g->no[next].id;
        if (ng_enumerate_paths_dfs(g,
                                   next,
                                   target,
                                   direction,
                                   type,
                                   max_depth,
                                   max_paths,
                                   path,
                                   depth + 1,
                                   visited,
                                   visitor,
                                   context,
                                   found,
                                   stop) != NG_OK)
            return NG_OOM;
        visited[next] = 0;
        if (*stop || (max_paths && *found >= max_paths))
            break;
    }
    return NG_OK;
}
ng_status ng_enumerate_paths(const ng_graph* g,
                             ng_node_id start,
                             ng_node_id target,
                             ng_direction direction,
                             ng_symbol_id type,
                             uint32_t max_depth,
                             size_t max_paths,
                             ng_path_visitor visitor,
                             void* context,
                             size_t* out_count) {
    size_t source, destination, found = 0;
    ng_node_id* path;
    unsigned char* visited;
    int stop = 0;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER || !max_depth || !visitor ||
        !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    source = ng_node_position(g, start);
    destination = ng_node_position(g, target);
    if (source == SIZE_MAX || destination == SIZE_MAX)
        return NG_NOT_FOUND;
    path = (ng_node_id*)malloc(((size_t)max_depth + 1) * sizeof(*path));
    visited = (unsigned char*)calloc(g->nn, 1);
    if (!path || (g->nn && !visited)) {
        free(path);
        free(visited);
        return NG_OOM;
    }
    path[0] = start;
    visited[source] = 1;
    s = ng_enumerate_paths_dfs(g,
                               source,
                               destination,
                               direction,
                               type,
                               max_depth,
                               max_paths,
                               path,
                               0,
                               visited,
                               visitor,
                               context,
                               &found,
                               &stop);
    free(path);
    free(visited);
    if (out_count)
        *out_count = found;
    return s;
}
ng_status ng_a_star(const ng_graph* g,
                    ng_node_id start,
                    ng_node_id target,
                    ng_direction direction,
                    ng_symbol_id type,
                    ng_symbol_id weight_key,
                    ng_path_heuristic heuristic,
                    void* heuristic_context,
                    ng_node_id* out_path,
                    size_t capacity,
                    size_t* out_count,
                    double* out_distance) {
    size_t source, destination, i, path_count = 0, path_length;
    double* distance;
    double* score;
    size_t* previous;
    unsigned char* used;
    if (!g || direction > NG_DIRECTION_EITHER || !heuristic ||
        !ng_analytics_symbol_ok(g, type) ||
        (weight_key && !ng_analytics_symbol_ok(g, weight_key)))
        return NG_INVALID_ARGUMENT;
    source = ng_node_position(g, start);
    destination = ng_node_position(g, target);
    if (source == SIZE_MAX || destination == SIZE_MAX)
        return NG_NOT_FOUND;
    distance = (double*)malloc(g->nn * sizeof(*distance));
    score = (double*)malloc(g->nn * sizeof(*score));
    previous = (size_t*)malloc(g->nn * sizeof(*previous));
    used = (unsigned char*)calloc(g->nn, 1);
    if ((g->nn && (!distance || !score || !previous || !used))) {
        free(distance);
        free(score);
        free(previous);
        free(used);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++) {
        distance[i] = 1e300;
        score[i] = 1e300;
        previous[i] = SIZE_MAX;
    }
    distance[source] = 0;
    score[source] = heuristic(start, target, heuristic_context);
    if (score[source] < 0)
        return (free(distance), free(score), free(previous), free(used), NG_INVALID_ARGUMENT);
    for (;;) {
        size_t current = SIZE_MAX;
        for (i = 0; i < g->nn; i++)
            if (!used[i] && (current == SIZE_MAX || score[i] < score[current]))
                current = i;
        if (current == SIZE_MAX || score[current] >= 1e299)
            break;
        used[current] = 1;
        if (current == destination)
            break;
        for (i = 0; i < g->nr; i++) {
            const rel_i* r = &g->re[i];
            size_t next = SIZE_MAX;
            double weight = 1.0, estimate, candidate;
            const prop* property;
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[current].id)
                next = ng_node_position(g, r->dst);
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[current].id)
                next = ng_node_position(g, r->src);
            else if (direction == NG_DIRECTION_EITHER) {
                if (r->src == g->no[current].id)
                    next = ng_node_position(g, r->dst);
                else if (r->dst == g->no[current].id)
                    next = ng_node_position(g, r->src);
            }
            if (next == SIZE_MAX || used[next])
                continue;
            if (weight_key) {
                property = findprop(r->p, r->np, weight_key);
                if (property) {
                    if (property->v.type == NG_VALUE_INT64)
                        weight = (double)property->v.as.integer;
                    else if (property->v.type == NG_VALUE_DOUBLE)
                        weight = property->v.as.real;
                    else
                        return (free(distance), free(score), free(previous), free(used),
                                NG_PARSE_ERROR);
                    if (weight < 0)
                        return (free(distance), free(score), free(previous), free(used),
                                NG_PARSE_ERROR);
                }
            }
            candidate = distance[current] + weight;
            if (candidate >= distance[next])
                continue;
            estimate = heuristic(g->no[next].id, target, heuristic_context);
            if (estimate < 0)
                return (free(distance), free(score), free(previous), free(used),
                        NG_INVALID_ARGUMENT);
            distance[next] = candidate;
            score[next] = candidate + estimate;
            previous[next] = current;
        }
    }
    if (distance[destination] >= 1e299) {
        free(distance);
        free(score);
        free(previous);
        free(used);
        if (out_count)
            *out_count = 0;
        return NG_NOT_FOUND;
    }
    for (i = destination;; i = previous[i]) {
        path_count++;
        if (i == source)
            break;
    }
    path_length = path_count;
    if (!out_count || !out_path || capacity < path_length) {
        free(distance);
        free(score);
        free(previous);
        free(used);
        return NG_LIMIT;
    }
    for (i = destination; i != SIZE_MAX; i = previous[i])
        out_path[--path_count] = g->no[i].id;
    *out_count = path_length;
    if (out_distance)
        *out_distance = distance[destination];
    free(distance);
    free(score);
    free(previous);
    free(used);
    return NG_OK;
}
ng_status ng_degree_centrality(const ng_graph* g,
                               ng_direction direction,
                               ng_symbol_id type,
                               ng_node_score* out,
                               size_t capacity,
                               size_t* out_count) {
    size_t i, j;
    ng_status s;
    if (direction > NG_DIRECTION_EITHER)
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++) {
        uint64_t degree = 0;
        for (j = 0; j < g->nr; j++) {
            const rel_i* r = &g->re[j];
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (direction == NG_DIRECTION_OUTGOING && r->src == g->no[i].id)
                degree++;
            else if (direction == NG_DIRECTION_INCOMING && r->dst == g->no[i].id)
                degree++;
            else if (direction == NG_DIRECTION_EITHER &&
                     (r->src == g->no[i].id || r->dst == g->no[i].id))
                degree++;
        }
        out[i].node = g->no[i].id;
        out[i].score = (double)degree;
    }
    return NG_OK;
}
ng_status ng_pagerank(const ng_graph* g,
                      ng_symbol_id type,
                      double damping,
                      uint32_t iterations,
                      ng_node_score* out,
                      size_t capacity,
                      size_t* out_count) {
    double *rank, *next;
    uint64_t* outdeg;
    size_t i, n;
    ng_status s;
    if (!g)
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    n = g->nn;
    if (!n)
        return NG_OK;
    if (damping <= 0.0 || damping >= 1.0)
        damping = 0.85;
    if (!iterations)
        iterations = 20;
    rank = (double*)malloc(n * sizeof(*rank));
    next = (double*)malloc(n * sizeof(*next));
    outdeg = (uint64_t*)calloc(n, sizeof(*outdeg));
    if (!rank || !next || !outdeg) {
        free(rank);
        free(next);
        free(outdeg);
        return NG_OOM;
    }
    for (i = 0; i < n; i++)
        rank[i] = 1.0 / (double)n;
    for (i = 0; i < g->nr; i++)
        if (ng_analytics_rel_ok(&g->re[i], type)) {
            size_t p = ng_node_position(g, g->re[i].src);
            if (p != SIZE_MAX)
                outdeg[p]++;
        }
    while (iterations--) {
        double dangling = 0.0;
        for (i = 0; i < n; i++) {
            next[i] = (1.0 - damping) / (double)n;
            if (!outdeg[i])
                dangling += rank[i];
        }
        for (i = 0; i < n; i++)
            next[i] += damping * dangling / (double)n;
        for (i = 0; i < g->nr; i++)
            if (ng_analytics_rel_ok(&g->re[i], type)) {
                size_t a = ng_node_position(g, g->re[i].src), b = ng_node_position(g, g->re[i].dst);
                if (a != SIZE_MAX && b != SIZE_MAX && outdeg[a])
                    next[b] += damping * rank[a] / (double)outdeg[a];
            }
        for (i = 0; i < n; i++)
            rank[i] = next[i];
    }
    for (i = 0; i < n; i++) {
        out[i].node = g->no[i].id;
        out[i].score = rank[i];
    }
    free(rank);
    free(next);
    free(outdeg);
    return NG_OK;
}
ng_status ng_eigenvector_centrality(const ng_graph* g,
                                    ng_direction direction,
                                    ng_symbol_id type,
                                    uint32_t iterations,
                                    ng_node_score* out,
                                    size_t capacity,
                                    size_t* out_count) {
    double* values;
    double* next;
    size_t i, j;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER || !iterations)
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    values = (double*)malloc(g->nn * sizeof(*values));
    next = (double*)malloc(g->nn * sizeof(*next));
    if ((g->nn && !values) || (g->nn && !next)) {
        free(values);
        free(next);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        values[i] = 1.0;
    while (iterations--) {
        double norm = 0.0;
        for (i = 0; i < g->nn; i++) {
            next[i] = 0.0;
            for (j = 0; j < g->nr; j++) {
                const rel_i* rel = &g->re[j];
                size_t other = SIZE_MAX;
                if (!ng_analytics_rel_ok(rel, type))
                    continue;
                if (direction == NG_DIRECTION_OUTGOING && rel->src == g->no[i].id)
                    other = ng_node_position(g, rel->dst);
                else if (direction == NG_DIRECTION_INCOMING && rel->dst == g->no[i].id)
                    other = ng_node_position(g, rel->src);
                else if (direction == NG_DIRECTION_EITHER) {
                    if (rel->src == g->no[i].id)
                        other = ng_node_position(g, rel->dst);
                    else if (rel->dst == g->no[i].id)
                        other = ng_node_position(g, rel->src);
                }
                if (other != SIZE_MAX)
                    next[i] += values[other];
            }
            norm += next[i] * next[i];
        }
        norm = sqrt(norm);
        if (norm == 0.0)
            break;
        for (i = 0; i < g->nn; i++)
            values[i] = next[i] / norm;
    }
    for (i = 0; i < g->nn; i++) {
        out[i].node = g->no[i].id;
        out[i].score = values[i];
    }
    free(values);
    free(next);
    return NG_OK;
}
static uint64_t ng_embedding_random(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
ng_status ng_fastrp(const ng_graph* g,
                    ng_direction direction,
                    ng_symbol_id type,
                    uint32_t iterations,
                    size_t dimensions,
                    uint64_t seed,
                    double* out,
                    size_t capacity,
                    size_t* out_count) {
    double* vectors;
    double* next;
    size_t total, i, d, step;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER || !iterations || !dimensions ||
        !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    if (dimensions > SIZE_MAX / (g->nn ? g->nn : 1))
        return NG_LIMIT;
    total = g->nn * dimensions;
    if (capacity < total || (total && !out))
        return NG_LIMIT;
    s = ng_analytics_check_output(g, type, g->nn ? out : NULL, g->nn, out_count);
    if (s != NG_OK)
        return s;
    vectors = (double*)malloc(total * sizeof(*vectors));
    next = (double*)malloc(total * sizeof(*next));
    if ((total && !vectors) || (total && !next)) {
        free(vectors);
        free(next);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        for (d = 0; d < dimensions; d++)
            vectors[i * dimensions + d] =
                (double)(ng_embedding_random(seed + i * UINT64_C(1315423911) + d) % 2001) /
                    1000.0 -
                1.0;
    for (step = 0; step < iterations; step++) {
        for (i = 0; i < g->nn; i++)
            for (d = 0; d < dimensions; d++)
                next[i * dimensions + d] = vectors[i * dimensions + d];
        for (i = 0; i < g->nr; i++) {
            const rel_i* rel = &g->re[i];
            size_t a = ng_node_position(g, rel->src), b = ng_node_position(g, rel->dst);
            if (!ng_analytics_rel_ok(rel, type) || a == SIZE_MAX || b == SIZE_MAX)
                continue;
            if (direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER)
                for (d = 0; d < dimensions; d++)
                    next[b * dimensions + d] += vectors[a * dimensions + d];
            if (direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER)
                for (d = 0; d < dimensions; d++)
                    next[a * dimensions + d] += vectors[b * dimensions + d];
        }
        for (i = 0; i < g->nn; i++) {
            double norm = 0.0;
            for (d = 0; d < dimensions; d++)
                norm += next[i * dimensions + d] * next[i * dimensions + d];
            norm = sqrt(norm);
            for (d = 0; d < dimensions; d++)
                vectors[i * dimensions + d] = norm ? next[i * dimensions + d] / norm
                                                     : next[i * dimensions + d];
        }
    }
    memcpy(out, vectors, total * sizeof(*out));
    free(vectors);
    free(next);
    return NG_OK;
}
ng_status ng_node2vec(const ng_graph* g,
                      ng_direction direction,
                      ng_symbol_id type,
                      double p,
                      double q,
                      uint32_t walks_per_node,
                      uint32_t walk_length,
                      size_t dimensions,
                      uint64_t seed,
                      double* out,
                      size_t capacity,
                      size_t* out_count) {
    double* base;
    size_t total, i, d, walk;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER || !p || !q || p < 0.0 || q < 0.0 ||
        !walks_per_node || !walk_length || !dimensions || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    if (dimensions > SIZE_MAX / (g->nn ? g->nn : 1))
        return NG_LIMIT;
    total = g->nn * dimensions;
    if (capacity < total || (total && !out))
        return NG_LIMIT;
    s = ng_analytics_check_output(g, type, g->nn ? out : NULL, g->nn, out_count);
    if (s != NG_OK)
        return s;
    base = (double*)malloc(total * sizeof(*base));
    if (total && !base)
        return NG_OOM;
    for (i = 0; i < g->nn; i++) {
        for (d = 0; d < dimensions; d++) {
            base[i * dimensions + d] =
                (double)(ng_embedding_random(seed + i * UINT64_C(1315423911) + d) % 2001) /
                    1000.0 - 1.0;
            out[i * dimensions + d] = 0.0;
        }
    }
    for (i = 0; i < g->nn; i++) {
        size_t samples = 0;
        for (walk = 0; walk < walks_per_node; walk++) {
            size_t current = i, previous = SIZE_MAX, step;
            for (step = 0; step < walk_length; step++) {
                size_t* candidates = NULL;
                size_t candidate_count = 0, j, chosen;
                double total_weight = 0.0, pick, cumulative;
                for (j = 0; j < g->nr; j++) {
                    const rel_i* rel = &g->re[j];
                    size_t other = SIZE_MAX;
                    if (!ng_analytics_rel_ok(rel, type))
                        continue;
                    if ((direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER) &&
                        ng_node_position(g, rel->src) == current)
                        other = ng_node_position(g, rel->dst);
                    else if ((direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER) &&
                             ng_node_position(g, rel->dst) == current)
                        other = ng_node_position(g, rel->src);
                    if (other != SIZE_MAX)
                        candidate_count++;
                }
                if (!candidate_count)
                    break;
                candidates = (size_t*)malloc(candidate_count * sizeof(*candidates));
                if (!candidates) {
                    free(base);
                    return NG_OOM;
                }
                candidate_count = 0;
                for (j = 0; j < g->nr; j++) {
                    const rel_i* rel = &g->re[j];
                    size_t other = SIZE_MAX;
                    if (!ng_analytics_rel_ok(rel, type))
                        continue;
                    if ((direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER) &&
                        ng_node_position(g, rel->src) == current)
                        other = ng_node_position(g, rel->dst);
                    else if ((direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER) &&
                             ng_node_position(g, rel->dst) == current)
                        other = ng_node_position(g, rel->src);
                    if (other == SIZE_MAX)
                        continue;
                    candidates[candidate_count++] = other;
                    total_weight += previous == SIZE_MAX ? 1.0 :
                        (other == previous ? 1.0 / p :
                         ng_analytics_adjacent(g, previous, other, type) ? 1.0 : 1.0 / q);
                }
                pick = (double)(ng_embedding_random(seed + i * UINT64_C(11400714819323198485) +
                                                    walk * UINT64_C(7046029254386353131) + step) %
                                UINT64_C(1000000)) /
                       1000000.0 * total_weight;
                cumulative = 0.0;
                chosen = candidates[candidate_count - 1];
                for (j = 0; j < candidate_count; j++) {
                    double weight = previous == SIZE_MAX ? 1.0 :
                        (candidates[j] == previous ? 1.0 / p :
                         ng_analytics_adjacent(g, previous, candidates[j], type) ? 1.0 : 1.0 / q);
                    cumulative += weight;
                    if (pick < cumulative) {
                        chosen = candidates[j];
                        break;
                    }
                }
                free(candidates);
                previous = current;
                current = chosen;
                samples++;
                for (d = 0; d < dimensions; d++)
                    out[i * dimensions + d] += base[current * dimensions + d];
            }
        }
        if (samples)
            for (d = 0; d < dimensions; d++)
                out[i * dimensions + d] /= (double)samples;
    }
    free(base);
    return NG_OK;
}
ng_status ng_graphsage(const ng_graph* g,
                       ng_direction direction,
                       ng_symbol_id type,
                       uint32_t iterations,
                       size_t input_dimensions,
                       size_t output_dimensions,
                       const double* features,
                       uint64_t seed,
                       double* out,
                       size_t capacity,
                       size_t* out_count) {
    double* current;
    double* aggregate;
    double* next;
    size_t current_dimensions, max_dimensions, total, i, j, d, k;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER || !iterations || !input_dimensions ||
        !output_dimensions || !features || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    if (input_dimensions > SIZE_MAX / (g->nn ? g->nn : 1) ||
        output_dimensions > SIZE_MAX / (g->nn ? g->nn : 1))
        return NG_LIMIT;
    max_dimensions = input_dimensions > output_dimensions ? input_dimensions : output_dimensions;
    total = g->nn * output_dimensions;
    if (capacity < total || (total && !out))
        return NG_LIMIT;
    s = ng_analytics_check_output(g, type, g->nn ? out : NULL, g->nn, out_count);
    if (s != NG_OK)
        return s;
    current_dimensions = input_dimensions;
    current = (double*)malloc(g->nn * current_dimensions * sizeof(*current));
    aggregate = (double*)malloc(g->nn * max_dimensions * sizeof(*aggregate));
    next = (double*)malloc(g->nn * output_dimensions * sizeof(*next));
    if ((g->nn && (!current || !aggregate || !next))) {
        free(current);
        free(aggregate);
        free(next);
        return NG_OOM;
    }
    if (g->nn)
        memcpy(current, features, g->nn * current_dimensions * sizeof(*current));
    for (k = 0; k < iterations; k++) {
        size_t next_dimensions = output_dimensions;
        for (i = 0; i < g->nn; i++) {
            size_t count = 1;
            for (d = 0; d < current_dimensions; d++)
                aggregate[i * current_dimensions + d] = current[i * current_dimensions + d];
            for (j = 0; j < g->nr; j++) {
                const rel_i* rel = &g->re[j];
                size_t other = SIZE_MAX;
                if (!ng_analytics_rel_ok(rel, type))
                    continue;
                if ((direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER) &&
                    ng_node_position(g, rel->src) == i)
                    other = ng_node_position(g, rel->dst);
                else if ((direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER) &&
                         ng_node_position(g, rel->dst) == i)
                    other = ng_node_position(g, rel->src);
                if (other == SIZE_MAX)
                    continue;
                for (d = 0; d < current_dimensions; d++)
                    aggregate[i * current_dimensions + d] += current[other * current_dimensions + d];
                count++;
            }
            for (d = 0; d < current_dimensions; d++)
                aggregate[i * current_dimensions + d] /= (double)count;
        }
        for (i = 0; i < g->nn; i++) {
            for (d = 0; d < next_dimensions; d++) {
                double value = 0.0;
                for (j = 0; j < current_dimensions; j++) {
                    uint64_t random = ng_embedding_random(
                        seed + k * UINT64_C(7046029254386353131) +
                        j * UINT64_C(1315423911) + d * UINT64_C(2654435761));
                    double weight = (double)(random % 2001) / 1000.0 - 1.0;
                    value += aggregate[i * current_dimensions + j] * weight;
                }
                next[i * next_dimensions + d] = tanh(value / sqrt((double)current_dimensions));
            }
        }
        if (k + 1 < iterations) {
            double* resized = (double*)realloc(current, g->nn * next_dimensions * sizeof(*current));
            if (g->nn && !resized) {
                free(current);
                free(aggregate);
                free(next);
                return NG_OOM;
            }
            current = resized;
        }
        if (g->nn)
            memcpy(current, next, g->nn * next_dimensions * sizeof(*current));
        current_dimensions = next_dimensions;
    }
    if (total)
        memcpy(out, current, total * sizeof(*out));
    free(current);
    free(aggregate);
    free(next);
    return NG_OK;
}
static size_t ng_graphsage_layer_input(const ng_graphsage_model* model, size_t layer) {
    return layer == 0 ? model->input_dimensions : model->output_dimensions;
}
static void ng_graphsage_model_release(ng_graphsage_model* model) {
    size_t i;
    if (!model)
        return;
    for (i = 0; i < model->layers; i++) {
        free(model->weights[i]);
        free(model->biases[i]);
    }
    free(model->weights);
    free(model->biases);
    free(model);
}
ng_status ng_graphsage_model_create(const ng_graphsage_config* config,
                                    ng_graphsage_model** out) {
    ng_graphsage_model* model;
    size_t i, j, input, count;
    if (!config || !out || !config->layers || !config->input_dimensions ||
        !config->output_dimensions || config->normalize_features < 0 ||
        config->normalize_features > 1)
        return NG_INVALID_ARGUMENT;
    model = (ng_graphsage_model*)calloc(1, sizeof(*model));
    if (!model)
        return NG_OOM;
    model->layers = config->layers;
    model->input_dimensions = config->input_dimensions;
    model->output_dimensions = config->output_dimensions;
    model->neighborhood_sample = config->neighborhood_sample;
    model->normalize_features = config->normalize_features;
    model->seed = config->seed;
    model->weights = (double**)calloc(model->layers, sizeof(*model->weights));
    model->biases = (double**)calloc(model->layers, sizeof(*model->biases));
    if (!model->weights || !model->biases) {
        ng_graphsage_model_release(model);
        return NG_OOM;
    }
    for (i = 0; i < model->layers; i++) {
        input = ng_graphsage_layer_input(model, i);
        if (input > SIZE_MAX / model->output_dimensions) {
            ng_graphsage_model_release(model);
            return NG_LIMIT;
        }
        count = input * model->output_dimensions;
        model->weights[i] = (double*)malloc(count * sizeof(double));
        model->biases[i] = (double*)calloc(model->output_dimensions, sizeof(double));
        if ((count && !model->weights[i]) || !model->biases[i]) {
            ng_graphsage_model_release(model);
            return NG_OOM;
        }
        for (j = 0; j < count; j++)
            model->weights[i][j] =
                ((double)(ng_embedding_random(model->seed + i * UINT64_C(7046029254386353131) + j) %
                          2001) /
                     1000.0 - 1.0) /
                sqrt((double)input);
    }
    *out = model;
    return NG_OK;
}
void ng_graphsage_model_free(ng_graphsage_model* model) {
    ng_graphsage_model_release(model);
}
static void ng_graphsage_normalize(const double* input,
                                   size_t count,
                                   size_t dimensions,
                                   double* output) {
    size_t i, d;
    for (d = 0; d < dimensions; d++) {
        double mean = 0.0, variance = 0.0;
        for (i = 0; i < count; i++)
            mean += input[i * dimensions + d];
        if (count)
            mean /= (double)count;
        for (i = 0; i < count; i++) {
            double delta = input[i * dimensions + d] - mean;
            variance += delta * delta;
        }
        variance = count ? sqrt(variance / (double)count) : 1.0;
        if (variance < 1e-12)
            variance = 1.0;
        for (i = 0; i < count; i++)
            output[i * dimensions + d] = (input[i * dimensions + d] - mean) / variance;
    }
}
static ng_status ng_graphsage_aggregate(const ng_graph* g,
                                        ng_direction direction,
                                        ng_symbol_id type,
                                        size_t sample,
                                        const double* current,
                                        size_t dimensions,
                                        double* aggregate,
                                        ng_graphsage_layer_cache* cache) {
    size_t i, j, d;
    if (cache) {
        cache->dimensions = dimensions;
        cache->neighbor_offsets = (size_t*)calloc(g->nn + 1, sizeof(size_t));
        cache->neighbors = g->nr ? (size_t*)malloc(g->nr * sizeof(size_t)) : NULL;
        cache->neighbor_count = 0;
        if ((g->nn && !cache->neighbor_offsets) || (g->nr && !cache->neighbors))
            return NG_OOM;
    }
    for (i = 0; i < g->nn; i++) {
        size_t* neighbors = NULL;
        size_t count = 0, limit, start = 0;
        for (j = 0; j < g->nr; j++) {
            const rel_i* rel = &g->re[j];
            if (!ng_analytics_rel_ok(rel, type))
                continue;
            if (((direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER) &&
                 ng_node_position(g, rel->src) == i) ||
                ((direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER) &&
                 ng_node_position(g, rel->dst) == i))
                count++;
        }
        limit = sample && count > sample ? sample : count;
        if (count) {
            size_t n = 0;
            neighbors = (size_t*)malloc(count * sizeof(*neighbors));
            if (!neighbors)
                return NG_OOM;
            for (j = 0; j < g->nr; j++) {
                const rel_i* rel = &g->re[j];
                size_t other = SIZE_MAX;
                if (!ng_analytics_rel_ok(rel, type))
                    continue;
                if ((direction == NG_DIRECTION_OUTGOING || direction == NG_DIRECTION_EITHER) &&
                    ng_node_position(g, rel->src) == i)
                    other = ng_node_position(g, rel->dst);
                else if ((direction == NG_DIRECTION_INCOMING || direction == NG_DIRECTION_EITHER) &&
                         ng_node_position(g, rel->dst) == i)
                    other = ng_node_position(g, rel->src);
                if (other != SIZE_MAX)
                    neighbors[n++] = other;
            }
            start = (size_t)(ng_embedding_random((uint64_t)i + dimensions) % count);
        }
        for (d = 0; d < dimensions; d++)
            aggregate[i * dimensions + d] = current[i * dimensions + d];
        for (j = 0; j < limit; j++) {
            size_t other = neighbors[(start + j) % count];
            if (cache)
                cache->neighbors[cache->neighbor_count++] = other;
            for (d = 0; d < dimensions; d++)
                aggregate[i * dimensions + d] += current[other * dimensions + d];
        }
        if (cache)
            cache->neighbor_offsets[i + 1] = cache->neighbor_count;
        for (d = 0; d < dimensions; d++)
            aggregate[i * dimensions + d] /= (double)(limit + 1);
        free(neighbors);
    }
    return NG_OK;
}
static ng_status ng_graphsage_forward(const ng_graphsage_model* model,
                                   const ng_graph* g,
                                   ng_direction direction,
                                   ng_symbol_id type,
                                   const double* features,
                                   double* out,
                                   size_t capacity,
                                   size_t* out_count,
                                   ng_graphsage_forward_cache* out_cache) {
    double *current = NULL, *normalized = NULL, *aggregate = NULL, *next = NULL;
    ng_graphsage_forward_cache cache = {0};
    size_t current_dimensions, max_dimensions, total, i, j, d, layer;
    ng_status status = NG_OK;
    if (!model || !g || !features || direction > NG_DIRECTION_EITHER ||
        !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    if (model->input_dimensions > SIZE_MAX / (g->nn ? g->nn : 1) ||
        model->output_dimensions > SIZE_MAX / (g->nn ? g->nn : 1))
        return NG_LIMIT;
    max_dimensions = model->input_dimensions > model->output_dimensions
                         ? model->input_dimensions
                         : model->output_dimensions;
    total = g->nn * model->output_dimensions;
    if (capacity < total || (total && !out))
        return NG_LIMIT;
    status = ng_analytics_check_output(g, type, g->nn ? out : NULL, g->nn, out_count);
    if (status != NG_OK)
        return status;
    current_dimensions = model->input_dimensions;
    cache.layer_count = model->layers;
    cache.input_dimensions = current_dimensions;
    cache.layers = (ng_graphsage_layer_cache*)calloc(model->layers, sizeof(*cache.layers));
    current = (double*)malloc(g->nn * max_dimensions * sizeof(*current));
    normalized = model->normalize_features ?
        (double*)malloc(g->nn * current_dimensions * sizeof(*normalized)) : NULL;
    aggregate = (double*)malloc(g->nn * model->output_dimensions * sizeof(*aggregate));
    next = (double*)malloc(g->nn * model->output_dimensions * sizeof(*next));
    if (!cache.layers || (g->nn && (!current || (model->normalize_features && !normalized) || !aggregate || !next))) {
        status = NG_OOM;
        goto finish;
    }
    if (g->nn) {
        if (model->normalize_features) {
            ng_graphsage_normalize(features, g->nn, current_dimensions, normalized);
            memcpy(current, normalized, g->nn * current_dimensions * sizeof(*current));
        } else
            memcpy(current, features, g->nn * current_dimensions * sizeof(*current));
        cache.input_activation = (double*)malloc(
            g->nn * current_dimensions * sizeof(double));
        if (!cache.input_activation) {
            status = NG_OOM;
            goto finish;
        }
        memcpy(cache.input_activation, current,
               g->nn * current_dimensions * sizeof(double));
    }
    for (layer = 0; layer < model->layers; layer++) {
        cache.layers[layer].aggregate = (double*)malloc(
            g->nn * current_dimensions * sizeof(double));
        cache.layers[layer].pre_activation = (double*)malloc(
            g->nn * model->output_dimensions * sizeof(double));
        cache.layers[layer].activation = (double*)malloc(
            g->nn * model->output_dimensions * sizeof(double));
        if (g->nn && (!cache.layers[layer].aggregate || !cache.layers[layer].pre_activation ||
                      !cache.layers[layer].activation)) {
            status = NG_OOM;
            goto finish;
        }
        status = ng_graphsage_aggregate(g, direction, type, model->neighborhood_sample,
                                        current, current_dimensions, aggregate,
                                        &cache.layers[layer]);
        if (status != NG_OK)
            goto finish;
        if (g->nn)
            memcpy(cache.layers[layer].aggregate, aggregate,
                   g->nn * current_dimensions * sizeof(double));
        for (i = 0; i < g->nn; i++)
            for (d = 0; d < model->output_dimensions; d++) {
                double value = model->biases[layer][d];
                for (j = 0; j < current_dimensions; j++)
                    value += aggregate[i * current_dimensions + j] *
                             model->weights[layer][j * model->output_dimensions + d];
                cache.layers[layer].pre_activation[i * model->output_dimensions + d] = value;
                next[i * model->output_dimensions + d] = tanh(value);
                cache.layers[layer].activation[i * model->output_dimensions + d] =
                    next[i * model->output_dimensions + d];
            }
        if (layer + 1 < model->layers) {
            memcpy(current, next, g->nn * model->output_dimensions * sizeof(*current));
            current_dimensions = model->output_dimensions;
        }
    }
    if (total)
        memcpy(out, next, total * sizeof(*out));
finish:
    free(current);
    free(normalized);
    free(aggregate);
    free(next);
    if (out_cache && status == NG_OK) {
        *out_cache = cache;
        memset(&cache, 0, sizeof(cache));
    }
    ng_graphsage_forward_cache_free(&cache);
    return status;
}
ng_status ng_graphsage_model_infer(const ng_graphsage_model* model,
                                   const ng_graph* g,
                                   ng_direction direction,
                                   ng_symbol_id type,
                                   const double* features,
                                   double* out,
                                   size_t capacity,
                                   size_t* out_count) {
    return ng_graphsage_forward(model, g, direction, type, features, out, capacity, out_count, NULL);
}
ng_status ng_graphsage_model_train(ng_graphsage_model* model,
                                   const ng_graph* g,
                                   ng_direction direction,
                                   ng_symbol_id type,
                                   const double* features,
                                   const double* targets,
                                   uint32_t epochs,
                                   double learning_rate,
                                   double* out_loss) {
    ng_graphsage_training_options options;
    ng_graphsage_training_report report;
    ng_status status;
    if (!model || !g || !features || !targets || !epochs || learning_rate <= 0.0)
        return NG_INVALID_ARGUMENT;
    options.epochs = epochs;
    options.learning_rate = learning_rate;
    options.batch_size = 0;
    options.validation_split = 0.0;
    options.seed = model->seed;
    options.loss = NG_GRAPHSAGE_LOSS_MSE;
    status = ng_graphsage_model_train_ex(model, g, direction, type, features, targets,
                                         &options, &report);
    if (status == NG_OK && out_loss)
        *out_loss = report.training_loss;
    return status;
}
static double ng_graphsage_loss_value(double prediction,
                                      double target,
                                      ng_graphsage_loss_kind kind) {
    if (kind == NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY) {
        double probability = 1.0 / (1.0 + exp(-prediction));
        if (probability < 1e-12)
            probability = 1e-12;
        if (probability > 1.0 - 1e-12)
            probability = 1.0 - 1e-12;
        return -(target * log(probability) + (1.0 - target) * log(1.0 - probability));
    }
    {
        double delta = prediction - target;
        return delta * delta;
    }
}
static double ng_graphsage_loss_gradient(double prediction,
                                         double target,
                                         ng_graphsage_loss_kind kind) {
    if (kind == NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY) {
        double probability = 1.0 / (1.0 + exp(-prediction));
        return probability - target;
    }
    return 2.0 * (prediction - target);
}
static void ng_graphsage_backprop_layer(const ng_graphsage_layer_cache* layer,
                                        const double* output_gradient,
                                        size_t node_count,
                                        size_t input_dimensions,
                                        size_t output_dimensions,
                                        const double* weights,
                                        double* aggregation_gradient,
                                        double* weight_gradient,
                                        double* bias_gradient) {
    size_t i, j, d;
    for (i = 0; i < node_count; i++)
        for (d = 0; d < output_dimensions; d++) {
            size_t o = i * output_dimensions + d;
            double delta = output_gradient[o] * (1.0 - layer->activation[o] * layer->activation[o]);
            bias_gradient[d] += delta;
            for (j = 0; j < input_dimensions; j++) {
                weight_gradient[j * output_dimensions + d] +=
                    layer->aggregate[i * input_dimensions + j] * delta;
                aggregation_gradient[i * input_dimensions + j] +=
                    weights[j * output_dimensions + d] * delta;
            }
        }
}
static void ng_graphsage_backprop_aggregation(const ng_graphsage_layer_cache* layer,
                                              const double* aggregation_gradient,
                                              size_t node_count,
                                              size_t dimensions,
                                              double* input_gradient) {
    size_t i, d, k;
    for (i = 0; i < node_count; i++) {
        size_t start = layer->neighbor_offsets ? layer->neighbor_offsets[i] : 0;
        size_t end = layer->neighbor_offsets ? layer->neighbor_offsets[i + 1] : 0;
        double scale = 1.0 / (double)(end - start + 1);
        for (d = 0; d < dimensions; d++) {
            double share = aggregation_gradient[i * dimensions + d] * scale;
            input_gradient[i * dimensions + d] += share;
            for (k = start; k < end; k++)
                input_gradient[layer->neighbors[k] * dimensions + d] += share;
        }
    }
}
static ng_status ng_graphsage_gradients(const ng_graphsage_model* model,
                                        const ng_graph* g,
                                        ng_direction direction,
                                        ng_symbol_id type,
                                        const double* features,
                                        const double* targets,
                                        const size_t* rows,
                                        size_t row_count,
                                        ng_graphsage_loss_kind kind,
                                        double* prediction,
                                        ng_graphsage_gradient_cache* gradients,
                                        double* out_loss) {
    ng_graphsage_forward_cache cache = {0};
    ng_status status;
    size_t total, input_dimensions, i, d, layer;
    double loss = 0.0, scale;
    if (!model || !g || !features || !targets || !rows ||
        !row_count || !prediction || !gradients)
        return NG_INVALID_ARGUMENT;
    total = g->nn * model->output_dimensions;
    status = ng_graphsage_forward(model, g, direction, type, features, prediction,
                                  total, NULL, &cache);
    if (status != NG_OK)
        return status;
    gradients->layer_count = model->layers;
    gradients->layers = (ng_graphsage_gradient_layer*)calloc(model->layers,
                                                             sizeof(*gradients->layers));
    gradients->input_gradient = (double*)calloc(g->nn * model->input_dimensions,
                                                sizeof(*gradients->input_gradient));
    if (!gradients->layers || (g->nn && !gradients->input_gradient)) {
        status = NG_OOM;
        goto finish;
    }
    for (layer = 0; layer < model->layers; layer++) {
        input_dimensions = ng_graphsage_layer_input(model, layer);
        gradients->layers[layer].activation_gradient =
            (double*)calloc(total, sizeof(*gradients->layers[layer].activation_gradient));
        gradients->layers[layer].aggregation_gradient =
            (double*)calloc(g->nn * input_dimensions,
                            sizeof(*gradients->layers[layer].aggregation_gradient));
        gradients->layers[layer].weight_gradient =
            (double*)calloc(input_dimensions * model->output_dimensions,
                            sizeof(*gradients->layers[layer].weight_gradient));
        gradients->layers[layer].bias_gradient =
            (double*)calloc(model->output_dimensions,
                            sizeof(*gradients->layers[layer].bias_gradient));
        if ((total && !gradients->layers[layer].activation_gradient) ||
            (g->nn && !gradients->layers[layer].aggregation_gradient) ||
            (input_dimensions && model->output_dimensions &&
             !gradients->layers[layer].weight_gradient) ||
            (model->output_dimensions && !gradients->layers[layer].bias_gradient)) {
            status = NG_OOM;
            goto finish;
        }
    }
    scale = 1.0 / (double)(row_count * model->output_dimensions);
    for (i = 0; i < row_count; i++)
        for (d = 0; d < model->output_dimensions; d++) {
            size_t offset = rows[i] * model->output_dimensions + d;
            loss += ng_graphsage_loss_value(prediction[offset], targets[offset], kind) * scale;
            gradients->layers[model->layers - 1].activation_gradient[offset] =
                ng_graphsage_loss_gradient(prediction[offset], targets[offset], kind) * scale;
        }
    for (layer = model->layers; layer > 0; layer--) {
        size_t index = layer - 1;
        double* previous_gradient;
        input_dimensions = ng_graphsage_layer_input(model, index);
        ng_graphsage_backprop_layer(&cache.layers[index],
                                    gradients->layers[index].activation_gradient,
                                    g->nn, input_dimensions, model->output_dimensions,
                                    model->weights[index],
                                    gradients->layers[index].aggregation_gradient,
                                    gradients->layers[index].weight_gradient,
                                    gradients->layers[index].bias_gradient);
        previous_gradient = index ? gradients->layers[index - 1].activation_gradient
                                  : gradients->input_gradient;
        ng_graphsage_backprop_aggregation(&cache.layers[index],
                                          gradients->layers[index].aggregation_gradient,
                                          g->nn, input_dimensions,
                                          previous_gradient);
    }
    if (out_loss)
        *out_loss = loss;
finish:
    ng_graphsage_forward_cache_free(&cache);
    if (status != NG_OK)
        ng_graphsage_gradient_cache_free(gradients);
    return status;
}
static double ng_graphsage_loss_rows(const ng_graphsage_model* model,
                                     const ng_graph* g,
                                     ng_direction direction,
                                     ng_symbol_id type,
                                     const double* features,
                                     const double* targets,
                                     const size_t* rows,
                                     size_t row_count,
                                     ng_graphsage_loss_kind kind,
                                     double* prediction) {
    size_t i, d;
    double loss = 0.0;
    ng_graphsage_forward_cache cache = {0};
    if (ng_graphsage_forward(model, g, direction, type, features, prediction,
                             g->nn * model->output_dimensions, NULL, &cache) != NG_OK)
        return -1.0;
    for (i = 0; i < row_count; i++)
        for (d = 0; d < model->output_dimensions; d++) {
            size_t offset = rows[i] * model->output_dimensions + d;
            loss += ng_graphsage_loss_value(prediction[offset], targets[offset], kind);
        }
    ng_graphsage_forward_cache_free(&cache);
    return row_count ? loss / (double)(row_count * model->output_dimensions) : 0.0;
}
ng_status ng_test_graphsage_finite_difference_gradient_check(const ng_graph* g,
                                                             ng_direction direction,
                                                             ng_symbol_id type,
                                                             uint32_t layers,
                                                             int normalize_features,
                                                             int mini_batch,
                                                             ng_graphsage_loss_kind kind,
                                                             double* out_max_delta) {
    ng_graphsage_config config = {layers, 2, 2, 1, normalize_features ? 1 : 0, 123};
    ng_graphsage_model* model = NULL;
    ng_graphsage_gradient_cache gradients = {0};
    double *features = NULL, *targets = NULL, *prediction = NULL;
    size_t* rows = NULL;
    size_t i, j, layer;
    double max_delta = 0.0;
    const double epsilon = 0.000001;
    ng_status status;
    if (!g || !layers || normalize_features < 0 || normalize_features > 1 ||
        mini_batch < 0 || mini_batch > 1 ||
        kind > NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY || !out_max_delta ||
        direction > NG_DIRECTION_EITHER ||
        !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    features = (double*)malloc(g->nn * config.input_dimensions * sizeof(*features));
    targets = (double*)malloc(g->nn * config.output_dimensions * sizeof(*targets));
    prediction = (double*)malloc(g->nn * config.output_dimensions * sizeof(*prediction));
    rows = (size_t*)malloc(g->nn * sizeof(*rows));
    if ((g->nn && (!features || !targets || !prediction || !rows))) {
        status = NG_OOM;
        goto finish;
    }
    for (i = 0; i < g->nn; i++) {
        rows[i] = i;
        features[i * 2] = 0.25 + (double)(i % 3) * 0.5;
        features[i * 2 + 1] = 1.0 - (double)(i % 5) * 0.1;
        if (kind == NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY) {
            targets[i * 2] = (double)(i % 2);
            targets[i * 2 + 1] = (double)((i + 1) % 2);
        } else {
            targets[i * 2] = 0.15 + (double)(i % 4) * 0.07;
            targets[i * 2 + 1] = -0.2 + (double)(i % 3) * 0.11;
        }
    }
    status = ng_graphsage_model_create(&config, &model);
    if (status != NG_OK)
        goto finish;
    if (mini_batch && g->nn > 2) {
        rows[0] = 0;
        rows[1] = 2;
        rows[2] = g->nn - 1;
    }
    status = ng_graphsage_gradients(model, g, direction, type, features, targets,
                                    rows, mini_batch && g->nn > 2 ? 3 : g->nn,
                                    kind, prediction, &gradients, NULL);
    if (status != NG_OK)
        goto finish;
    for (layer = 0; layer < model->layers; layer++) {
        size_t input_dimensions = ng_graphsage_layer_input(model, layer);
        size_t weight_count = input_dimensions * model->output_dimensions;
        for (j = 0; j < weight_count; j++) {
            double plus, minus, finite;
            model->weights[layer][j] += epsilon;
            plus = ng_graphsage_loss_rows(model, g, direction, type, features, targets,
                                          rows, mini_batch && g->nn > 2 ? 3 : g->nn,
                                          kind, prediction);
            model->weights[layer][j] -= 2.0 * epsilon;
            minus = ng_graphsage_loss_rows(model, g, direction, type, features, targets,
                                           rows, mini_batch && g->nn > 2 ? 3 : g->nn,
                                           kind, prediction);
            model->weights[layer][j] += epsilon;
            finite = (plus - minus) / (2.0 * epsilon);
            if (fabs(finite - gradients.layers[layer].weight_gradient[j]) > max_delta)
                max_delta = fabs(finite - gradients.layers[layer].weight_gradient[j]);
        }
        for (j = 0; j < model->output_dimensions; j++) {
            double plus, minus, finite;
            model->biases[layer][j] += epsilon;
            plus = ng_graphsage_loss_rows(model, g, direction, type, features, targets,
                                          rows, mini_batch && g->nn > 2 ? 3 : g->nn,
                                          kind, prediction);
            model->biases[layer][j] -= 2.0 * epsilon;
            minus = ng_graphsage_loss_rows(model, g, direction, type, features, targets,
                                           rows, mini_batch && g->nn > 2 ? 3 : g->nn,
                                           kind, prediction);
            model->biases[layer][j] += epsilon;
            finite = (plus - minus) / (2.0 * epsilon);
            if (fabs(finite - gradients.layers[layer].bias_gradient[j]) > max_delta)
                max_delta = fabs(finite - gradients.layers[layer].bias_gradient[j]);
        }
    }
    *out_max_delta = max_delta;
finish:
    ng_graphsage_gradient_cache_free(&gradients);
    ng_graphsage_model_free(model);
    free(features);
    free(targets);
    free(prediction);
    free(rows);
    return status;
}
ng_status ng_test_graphsage_single_layer_mse_gradient_check(const ng_graph* g,
                                                            ng_direction direction,
                                                            ng_symbol_id type,
                                                            double* out_max_delta) {
    return ng_test_graphsage_finite_difference_gradient_check(
        g, direction, type, 1, 1, 0, NG_GRAPHSAGE_LOSS_MSE, out_max_delta);
}
ng_status ng_graphsage_model_train_ex_diagnostics(
    ng_graphsage_model* model,
    const ng_graph* g,
    ng_direction direction,
    ng_symbol_id type,
    const double* features,
    const double* targets,
    const ng_graphsage_training_options* options,
    ng_graphsage_training_report* report,
    ng_graphsage_training_diagnostics* diagnostics) {
    double* prediction;
    size_t* rows;
    size_t train_count, validation_count, batch_size, epoch, start, i, layer, j, input, count;
    double previous_epoch_loss = 0.0;
    int have_previous_epoch_loss = 0;
    if (!model || !g || !features || !targets || !options || !options->epochs ||
        options->learning_rate <= 0.0 || options->validation_split < 0.0 ||
        options->validation_split >= 1.0 || direction > NG_DIRECTION_EITHER ||
        !ng_analytics_symbol_ok(g, type) || options->loss > NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY)
        return NG_INVALID_ARGUMENT;
    if (diagnostics) {
        diagnostics->epoch_count = 0;
        diagnostics->epochs_run = 0;
        diagnostics->converged = 0;
        diagnostics->convergence_delta = 0.0;
        diagnostics->validation_start = 0;
        diagnostics->validation_row_count = 0;
        diagnostics->validation_seed = options->seed;
    }
    if (options->loss == NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY) {
        size_t target_count = g->nn * model->output_dimensions;
        for (size_t i = 0; i < target_count; i++)
            if (targets[i] < 0.0 || targets[i] > 1.0)
                return NG_INVALID_ARGUMENT;
    }
    validation_count = (size_t)((double)g->nn * options->validation_split);
    train_count = g->nn - validation_count;
    if (!train_count)
        return NG_INVALID_ARGUMENT;
    batch_size = options->batch_size ? options->batch_size : train_count;
    rows = (size_t*)malloc(g->nn * sizeof(*rows));
    prediction = (double*)malloc(g->nn * model->output_dimensions * sizeof(*prediction));
    if ((g->nn && !rows) || (g->nn && !prediction)) {
        free(rows);
        free(prediction);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        rows[i] = i;
    if (diagnostics) {
        diagnostics->validation_start = train_count;
        diagnostics->validation_row_count = validation_count;
        if (diagnostics->validation_rows) {
            size_t copy_count = validation_count < diagnostics->validation_row_capacity
                                    ? validation_count
                                    : diagnostics->validation_row_capacity;
            for (i = 0; i < copy_count; i++)
                diagnostics->validation_rows[i] = rows[train_count + i];
        }
    }
    for (epoch = 0; epoch < options->epochs; epoch++) {
        for (start = 0; start < train_count; start += batch_size) {
            size_t current_count = train_count - start < batch_size ? train_count - start : batch_size;
            for (i = 0; i < current_count; i++) {
                size_t position = start + i;
                size_t swap = (size_t)(ng_embedding_random(options->seed + epoch * UINT64_C(1315423911) +
                                                            position) % train_count);
                size_t tmp = rows[position];
                rows[position] = rows[swap];
                rows[swap] = tmp;
            }
            ng_graphsage_gradient_cache gradients = {0};
            ng_status status = ng_graphsage_gradients(
                model, g, direction, type, features, targets, rows + start,
                current_count, options->loss, prediction, &gradients, NULL);
            if (status != NG_OK) {
                free(rows);
                free(prediction);
                return status;
            }
            for (layer = 0; layer < model->layers; layer++) {
                input = ng_graphsage_layer_input(model, layer);
                count = input * model->output_dimensions;
                for (j = 0; j < count; j++)
                    model->weights[layer][j] -= options->learning_rate *
                                                gradients.layers[layer].weight_gradient[j];
                for (j = 0; j < model->output_dimensions; j++)
                    model->biases[layer][j] -= options->learning_rate *
                                               gradients.layers[layer].bias_gradient[j];
            }
            ng_graphsage_gradient_cache_free(&gradients);
        }
        if (diagnostics) {
            double training_loss = ng_graphsage_loss_rows(model, g, direction, type, features,
                                                         targets, rows, train_count,
                                                         options->loss, prediction);
            double validation_loss = validation_count
                                         ? ng_graphsage_loss_rows(model, g, direction, type,
                                                                  features, targets,
                                                                  rows + train_count,
                                                                  validation_count,
                                                                  options->loss, prediction)
                                         : 0.0;
            if (training_loss < 0.0 || validation_loss < 0.0) {
                free(rows);
                free(prediction);
                return NG_INVALID_ARGUMENT;
            }
            if (diagnostics->epoch_count < diagnostics->epoch_capacity) {
                if (diagnostics->epoch_training_losses)
                    diagnostics->epoch_training_losses[diagnostics->epoch_count] =
                        training_loss;
                if (diagnostics->epoch_validation_losses)
                    diagnostics->epoch_validation_losses[diagnostics->epoch_count] =
                        validation_loss;
            }
            diagnostics->epoch_count++;
            diagnostics->epochs_run = epoch + 1;
            if (have_previous_epoch_loss) {
                diagnostics->convergence_delta = fabs(previous_epoch_loss - training_loss);
                if (diagnostics->convergence_tolerance > 0.0 &&
                    diagnostics->convergence_delta <= diagnostics->convergence_tolerance) {
                    diagnostics->converged = 1;
                    break;
                }
            }
            previous_epoch_loss = training_loss;
            have_previous_epoch_loss = 1;
        }
    }
    if (report) {
        report->training_loss = ng_graphsage_loss_rows(model, g, direction, type, features, targets,
                                                       rows, train_count, options->loss, prediction);
        report->validation_samples = validation_count;
        report->training_samples = train_count;
        report->validation_loss = validation_count
                                      ? ng_graphsage_loss_rows(model, g, direction, type, features,
                                                               targets, rows + train_count,
                                                               validation_count, options->loss, prediction)
                                      : 0.0;
    }
    free(rows);
    free(prediction);
    return NG_OK;
}
ng_status ng_graphsage_model_train_ex(ng_graphsage_model* model,
                                      const ng_graph* g,
                                      ng_direction direction,
                                      ng_symbol_id type,
                                      const double* features,
                                      const double* targets,
                                      const ng_graphsage_training_options* options,
                                      ng_graphsage_training_report* report) {
    return ng_graphsage_model_train_ex_diagnostics(model, g, direction, type, features,
                                                   targets, options, report, NULL);
}
static int ng_graphsage_write(FILE* file, const void* data, size_t size) {
    return fwrite(data, 1, size, file) == size;
}
ng_status ng_graphsage_model_save(const ng_graphsage_model* model, const char* path) {
    FILE* file;
    uint64_t magic = UINT64_C(0x4e47534147455331);
    size_t i;
    if (!model || !path)
        return NG_INVALID_ARGUMENT;
    file = fopen(path, "wb");
    if (!file)
        return NG_IO_ERROR;
    if (!ng_graphsage_write(file, &magic, sizeof(magic)) ||
        !ng_graphsage_write(file, &model->layers, sizeof(model->layers)) ||
        !ng_graphsage_write(file, &model->input_dimensions, sizeof(model->input_dimensions)) ||
        !ng_graphsage_write(file, &model->output_dimensions, sizeof(model->output_dimensions)) ||
        !ng_graphsage_write(file, &model->neighborhood_sample, sizeof(model->neighborhood_sample)) ||
        !ng_graphsage_write(file, &model->normalize_features, sizeof(model->normalize_features)) ||
        !ng_graphsage_write(file, &model->seed, sizeof(model->seed))) {
        fclose(file);
        return NG_IO_ERROR;
    }
    for (i = 0; i < model->layers; i++) {
        size_t input = ng_graphsage_layer_input(model, i);
        if (!ng_graphsage_write(file, model->weights[i], input * model->output_dimensions * sizeof(double)) ||
            !ng_graphsage_write(file, model->biases[i], model->output_dimensions * sizeof(double))) {
            fclose(file);
            return NG_IO_ERROR;
        }
    }
    return fclose(file) == 0 ? NG_OK : NG_IO_ERROR;
}
ng_status ng_graphsage_model_load(const char* path, ng_graphsage_model** out) {
    FILE* file;
    uint64_t magic;
    ng_graphsage_config config;
    ng_graphsage_model* model = NULL;
    size_t i;
    if (!path || !out)
        return NG_INVALID_ARGUMENT;
    file = fopen(path, "rb");
    if (!file)
        return NG_IO_ERROR;
    if (fread(&magic, sizeof(magic), 1, file) != 1 || magic != UINT64_C(0x4e47534147455331) ||
        fread(&config.layers, sizeof(config.layers), 1, file) != 1 ||
        fread(&config.input_dimensions, sizeof(config.input_dimensions), 1, file) != 1 ||
        fread(&config.output_dimensions, sizeof(config.output_dimensions), 1, file) != 1 ||
        fread(&config.neighborhood_sample, sizeof(config.neighborhood_sample), 1, file) != 1 ||
        fread(&config.normalize_features, sizeof(config.normalize_features), 1, file) != 1 ||
        fread(&config.seed, sizeof(config.seed), 1, file) != 1) {
        fclose(file);
        return NG_CORRUPT;
    }
    if (ng_graphsage_model_create(&config, &model) != NG_OK) {
        fclose(file);
        return NG_OOM;
    }
    for (i = 0; i < model->layers; i++) {
        size_t input = ng_graphsage_layer_input(model, i);
        if (fread(model->weights[i], input * model->output_dimensions * sizeof(double), 1, file) != 1 ||
            fread(model->biases[i], model->output_dimensions * sizeof(double), 1, file) != 1) {
            ng_graphsage_model_release(model);
            fclose(file);
            return NG_CORRUPT;
        }
    }
    fclose(file);
    *out = model;
    return NG_OK;
}
ng_status ng_vector_search_cosine(const double* vectors,
                                  size_t vector_count,
                                  size_t dimensions,
                                  const double* query,
                                  size_t k,
                                  ng_vector_score* out,
                                  size_t capacity,
                                  size_t* out_count) {
    size_t i, d, count = 0;
    if (!vectors || !dimensions || !query || !k || capacity < k || !out)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < vector_count; i++) {
        double dot = 0.0, norm = 0.0, qnorm = 0.0, score;
        for (d = 0; d < dimensions; d++) {
            double value = vectors[i * dimensions + d];
            dot += value * query[d];
            norm += value * value;
            qnorm += query[d] * query[d];
        }
        score = norm > 0.0 && qnorm > 0.0 ? dot / sqrt(norm * qnorm) : 0.0;
        if (count < k)
            out[count++] = (ng_vector_score){i, score};
        else {
            size_t worst = 0;
            for (d = 1; d < count; d++)
                if (out[d].score < out[worst].score)
                    worst = d;
            if (score > out[worst].score)
                out[worst] = (ng_vector_score){i, score};
        }
    }
    for (i = 0; i < count; i++)
        for (d = i + 1; d < count; d++)
            if (out[d].score > out[i].score) {
                ng_vector_score tmp = out[i];
                out[i] = out[d];
                out[d] = tmp;
            }
    if (out_count)
        *out_count = count;
    return NG_OK;
}
static ng_status ng_distance_centrality(const ng_graph* g,
                                        ng_direction direction,
                                        ng_symbol_id type,
                                        int harmonic,
                                        ng_node_score* out,
                                        size_t capacity,
                                        size_t* out_count) {
    size_t i, j;
    ng_status s;
    if (!g || direction > NG_DIRECTION_EITHER)
        return NG_INVALID_ARGUMENT;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++) {
        size_t* distances = (size_t*)malloc(g->nn * sizeof(*distances));
        size_t* queue = (size_t*)malloc(g->nn * sizeof(*queue));
        size_t head = 0, tail = 0, reachable = 0;
        double total = 0.0;
        if ((g->nn && !distances) || (g->nn && !queue)) {
            free(distances);
            free(queue);
            return NG_OOM;
        }
        for (j = 0; j < g->nn; j++)
            distances[j] = SIZE_MAX;
        distances[i] = 0;
        queue[tail++] = i;
        while (head < tail) {
            size_t current = queue[head++];
            for (j = 0; j < g->nr; j++) {
                const rel_i* rel = &g->re[j];
                size_t next = SIZE_MAX;
                if (!ng_analytics_rel_ok(rel, type))
                    continue;
                if (direction == NG_DIRECTION_OUTGOING && rel->src == g->no[current].id)
                    next = ng_node_position(g, rel->dst);
                else if (direction == NG_DIRECTION_INCOMING && rel->dst == g->no[current].id)
                    next = ng_node_position(g, rel->src);
                else if (direction == NG_DIRECTION_EITHER) {
                    if (rel->src == g->no[current].id)
                        next = ng_node_position(g, rel->dst);
                    else if (rel->dst == g->no[current].id)
                        next = ng_node_position(g, rel->src);
                }
                if (next != SIZE_MAX && distances[next] == SIZE_MAX) {
                    distances[next] = distances[current] + 1;
                    queue[tail++] = next;
                }
            }
        }
        for (j = 0; j < g->nn; j++)
            if (j != i && distances[j] != SIZE_MAX) {
                reachable++;
                total += harmonic ? 1.0 / (double)distances[j] : (double)distances[j];
            }
        out[i].node = g->no[i].id;
        out[i].score = total == 0.0 ? 0.0
                                   : (harmonic ? total
                                               : (double)reachable / total);
        free(distances);
        free(queue);
    }
    return NG_OK;
}
ng_status ng_closeness_centrality(const ng_graph* g,
                                  ng_direction direction,
                                  ng_symbol_id type,
                                  ng_node_score* out,
                                  size_t capacity,
                                  size_t* out_count) {
    return ng_distance_centrality(g, direction, type, 0, out, capacity, out_count);
}
ng_status ng_harmonic_centrality(const ng_graph* g,
                                 ng_direction direction,
                                 ng_symbol_id type,
                                 ng_node_score* out,
                                 size_t capacity,
                                 size_t* out_count) {
    return ng_distance_centrality(g, direction, type, 1, out, capacity, out_count);
}
ng_status ng_weakly_connected_components(const ng_graph* g,
                                         ng_symbol_id type,
                                         ng_node_component* out,
                                         size_t capacity,
                                         size_t* out_count) {
    unsigned char* seen;
    size_t* q;
    size_t i, component = 0;
    ng_status s;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    seen = (unsigned char*)calloc(g->nn, 1);
    q = (size_t*)malloc(g->nn * sizeof(*q));
    if (g->nn && (!seen || !q)) {
        free(seen);
        free(q);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        if (!seen[i]) {
            size_t head = 0, tail = 0;
            seen[i] = 1;
            q[tail++] = i;
            while (head < tail) {
                size_t cur = q[head++], j;
                out[cur].node = g->no[cur].id;
                out[cur].component = (uint64_t)component;
                for (j = 0; j < g->nr; j++) {
                    const rel_i* r = &g->re[j];
                    ng_node_id other = 0;
                    size_t p;
                    if (!ng_analytics_rel_ok(r, type))
                        continue;
                    if (r->src == g->no[cur].id)
                        other = r->dst;
                    else if (r->dst == g->no[cur].id)
                        other = r->src;
                    else
                        continue;
                    p = ng_node_position(g, other);
                    if (p != SIZE_MAX && !seen[p]) {
                        seen[p] = 1;
                        q[tail++] = p;
                    }
                }
            }
            component++;
        }
    free(seen);
    free(q);
    return NG_OK;
}
static ng_status ng_analytics_reach(
    const ng_graph* g, size_t start, ng_symbol_id type, int reverse, unsigned char* seen) {
    size_t *q, head = 0, tail = 0;
    if (!seen)
        return NG_INVALID_ARGUMENT;
    q = (size_t*)malloc(g->nn * sizeof(*q));
    if (g->nn && !q)
        return NG_OOM;
    seen[start] = 1;
    q[tail++] = start;
    while (head < tail) {
        size_t cur = q[head++], i;
        for (i = 0; i < g->nr; i++) {
            const rel_i* r = &g->re[i];
            size_t p = SIZE_MAX;
            if (!ng_analytics_rel_ok(r, type))
                continue;
            if (!reverse && r->src == g->no[cur].id)
                p = ng_node_position(g, r->dst);
            else if (reverse && r->dst == g->no[cur].id)
                p = ng_node_position(g, r->src);
            if (p != SIZE_MAX && !seen[p]) {
                seen[p] = 1;
                q[tail++] = p;
            }
        }
    }
    free(q);
    return NG_OK;
}
ng_status ng_strongly_connected_components(const ng_graph* g,
                                           ng_symbol_id type,
                                           ng_node_component* out,
                                           size_t capacity,
                                           size_t* out_count) {
    unsigned char *assigned, *fwd, *rev;
    size_t i, j, component = 0;
    ng_status s;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    assigned = (unsigned char*)calloc(g->nn, 1);
    fwd = (unsigned char*)calloc(g->nn, 1);
    rev = (unsigned char*)calloc(g->nn, 1);
    if (g->nn && (!assigned || !fwd || !rev)) {
        free(assigned);
        free(fwd);
        free(rev);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        if (!assigned[i]) {
            memset(fwd, 0, g->nn);
            memset(rev, 0, g->nn);
            s = ng_analytics_reach(g, i, type, 0, fwd);
            if (s == NG_OK)
                s = ng_analytics_reach(g, i, type, 1, rev);
            if (s != NG_OK) {
                free(assigned);
                free(fwd);
                free(rev);
                return s;
            }
            for (j = 0; j < g->nn; j++)
                if (!assigned[j] && fwd[j] && rev[j]) {
                    assigned[j] = 1;
                    out[j].node = g->no[j].id;
                    out[j].component = (uint64_t)component;
                }
            component++;
        }
    free(assigned);
    free(fwd);
    free(rev);
    return NG_OK;
}
ng_status ng_triangle_count(
    const ng_graph* g, ng_symbol_id type, ng_node_metric* out, size_t capacity, size_t* out_count) {
    size_t i;
    ng_status s;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++) {
        size_t *a = NULL, n = 0, j, k;
        uint64_t triangles = 0;
        s = ng_analytics_neighbors(g, i, type, &a, &n);
        if (s != NG_OK)
            return s;
        for (j = 0; j < n; j++)
            for (k = j + 1; k < n; k++)
                if (ng_analytics_adjacent(g, a[j], a[k], type))
                    triangles++;
        out[i].node = g->no[i].id;
        out[i].value = triangles;
        free(a);
    }
    return NG_OK;
}
ng_status ng_local_clustering_coefficient(
    const ng_graph* g, ng_symbol_id type, ng_node_score* out, size_t capacity, size_t* out_count) {
    size_t i;
    ng_status s;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++) {
        size_t *a = NULL, n = 0, j, k;
        uint64_t links = 0;
        s = ng_analytics_neighbors(g, i, type, &a, &n);
        if (s != NG_OK)
            return s;
        for (j = 0; j < n; j++)
            for (k = j + 1; k < n; k++)
                if (ng_analytics_adjacent(g, a[j], a[k], type))
                    links++;
        out[i].node = g->no[i].id;
        out[i].score = n < 2 ? 0.0 : (2.0 * (double)links) / ((double)n * (double)(n - 1));
        free(a);
    }
    return NG_OK;
}
ng_status ng_common_neighbors(
    const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out) {
    size_t pa, pb, *na = NULL, *nb = NULL, ca = 0, cb = 0, i, count = 0;
    ng_status s;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, type))
        return NG_NOT_FOUND;
    pa = ng_node_position(g, a);
    pb = ng_node_position(g, b);
    if (pa == SIZE_MAX || pb == SIZE_MAX)
        return NG_NOT_FOUND;
    s = ng_analytics_neighbors(g, pa, type, &na, &ca);
    if (s == NG_OK)
        s = ng_analytics_neighbors(g, pb, type, &nb, &cb);
    if (s != NG_OK) {
        free(na);
        free(nb);
        return s;
    }
    for (i = 0; i < ca; i++)
        if (ng_analytics_has_size(nb, cb, na[i]))
            count++;
    free(na);
    free(nb);
    *out = count;
    return NG_OK;
}
ng_status ng_total_neighbors(
    const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out) {
    size_t pa, pb, *na = NULL, *nb = NULL, ca = 0, cb = 0, i, count;
    ng_status s;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, type))
        return NG_NOT_FOUND;
    pa = ng_node_position(g, a);
    pb = ng_node_position(g, b);
    if (pa == SIZE_MAX || pb == SIZE_MAX)
        return NG_NOT_FOUND;
    s = ng_analytics_neighbors(g, pa, type, &na, &ca);
    if (s == NG_OK)
        s = ng_analytics_neighbors(g, pb, type, &nb, &cb);
    if (s != NG_OK) {
        free(na);
        free(nb);
        return s;
    }
    count = ca;
    for (i = 0; i < cb; i++)
        if (!ng_analytics_has_size(na, ca, nb[i]))
            count++;
    free(na);
    free(nb);
    *out = (uint64_t)count;
    return NG_OK;
}
ng_status ng_preferential_attachment(
    const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out) {
    size_t pa, pb, *na = NULL, *nb = NULL, ca = 0, cb = 0;
    ng_status s;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, type))
        return NG_NOT_FOUND;
    pa = ng_node_position(g, a);
    pb = ng_node_position(g, b);
    if (pa == SIZE_MAX || pb == SIZE_MAX)
        return NG_NOT_FOUND;
    s = ng_analytics_neighbors(g, pa, type, &na, &ca);
    if (s == NG_OK)
        s = ng_analytics_neighbors(g, pb, type, &nb, &cb);
    free(na);
    free(nb);
    if (s != NG_OK)
        return s;
    *out = (uint64_t)ca * (uint64_t)cb;
    return NG_OK;
}
static ng_status ng_link_prediction_score(const ng_graph* g,
                                          ng_node_id a,
                                          ng_node_id b,
                                          ng_symbol_id type,
                                          int adamic,
                                          double* out) {
    size_t pa, pb, *na = NULL, *nb = NULL, ca = 0, cb = 0, i;
    double score = 0.0;
    ng_status s;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, type))
        return NG_NOT_FOUND;
    pa = ng_node_position(g, a);
    pb = ng_node_position(g, b);
    if (pa == SIZE_MAX || pb == SIZE_MAX)
        return NG_NOT_FOUND;
    s = ng_analytics_neighbors(g, pa, type, &na, &ca);
    if (s == NG_OK)
        s = ng_analytics_neighbors(g, pb, type, &nb, &cb);
    if (s != NG_OK) {
        free(na);
        free(nb);
        return s;
    }
    for (i = 0; i < ca; i++) {
        size_t degree = 0, *common_neighbors = NULL;
        if (!ng_analytics_has_size(nb, cb, na[i]))
            continue;
        s = ng_analytics_neighbors(g, na[i], type, &common_neighbors, &degree);
        if (s != NG_OK) {
            free(na);
            free(nb);
            return s;
        }
        free(common_neighbors);
        if (degree > 0)
            score += adamic ? (degree > 1 ? 1.0 / log((double)degree) : 0.0)
                            : 1.0 / (double)degree;
    }
    free(na);
    free(nb);
    *out = score;
    return NG_OK;
}
ng_status ng_adamic_adar(const ng_graph* g,
                         ng_node_id a,
                         ng_node_id b,
                         ng_symbol_id type,
                         double* out) {
    return ng_link_prediction_score(g, a, b, type, 1, out);
}
ng_status ng_resource_allocation(const ng_graph* g,
                                 ng_node_id a,
                                 ng_node_id b,
                                 ng_symbol_id type,
                                 double* out) {
    return ng_link_prediction_score(g, a, b, type, 0, out);
}
static void ng_articulation_visit(const ng_graph* g,
                                  size_t current,
                                  ng_symbol_id type,
                                  size_t parent_edge,
                                  size_t* clock,
                                  size_t* discovery,
                                  size_t* low,
                                  unsigned char* points,
                                  unsigned char* bridge) {
    size_t i, children = 0;
    discovery[current] = low[current] = ++*clock;
    for (i = 0; i < g->nr; i++) {
        const rel_i* rel = &g->re[i];
        size_t next = SIZE_MAX;
        if (i == parent_edge || !ng_analytics_rel_ok(rel, type))
            continue;
        if (rel->src == g->no[current].id)
            next = ng_node_position(g, rel->dst);
        else if (rel->dst == g->no[current].id)
            next = ng_node_position(g, rel->src);
        if (next == SIZE_MAX)
            continue;
        if (!discovery[next]) {
            children++;
            ng_articulation_visit(g,
                                  next,
                                  type,
                                  i,
                                  clock,
                                  discovery,
                                  low,
                                  points,
                                  bridge);
            if (low[next] < low[current])
                low[current] = low[next];
            if (parent_edge != SIZE_MAX && low[next] >= discovery[current])
                points[current] = 1;
            if (low[next] > discovery[current])
                bridge[i] = 1;
        } else if (discovery[next] < low[current]) {
            low[current] = discovery[next];
        }
    }
    if (parent_edge == SIZE_MAX && children > 1)
        points[current] = 1;
}
ng_status ng_articulation_points(const ng_graph* g,
                                 ng_symbol_id type,
                                 ng_node_id* out,
                                 size_t capacity,
                                 size_t* out_count) {
    size_t* discovery;
    size_t* low;
    unsigned char* points;
    unsigned char* bridge;
    size_t clock = 0, count = 0, i;
    if (!g || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    discovery = (size_t*)calloc(g->nn, sizeof(*discovery));
    low = (size_t*)calloc(g->nn, sizeof(*low));
    points = (unsigned char*)calloc(g->nn, 1);
    bridge = (unsigned char*)calloc(g->nr, 1);
    if ((g->nn && (!discovery || !low || !points)) || (g->nr && !bridge)) {
        free(discovery);
        free(low);
        free(points);
        free(bridge);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        if (!discovery[i])
            ng_articulation_visit(g,
                                  i,
                                  type,
                                  SIZE_MAX,
                                  &clock,
                                  discovery,
                                  low,
                                  points,
                                  bridge);
    for (i = 0; i < g->nn; i++)
        if (points[i]) {
            if (!out || count >= capacity) {
                free(discovery);
                free(low);
                free(points);
                free(bridge);
                return NG_LIMIT;
            }
            out[count++] = g->no[i].id;
        }
    if (out_count)
        *out_count = count;
    free(discovery);
    free(low);
    free(points);
    free(bridge);
    return NG_OK;
}
ng_status ng_bridges(const ng_graph* g,
                     ng_symbol_id type,
                     ng_relationship_id* out,
                     size_t capacity,
                     size_t* out_count) {
    size_t* discovery;
    size_t* low;
    unsigned char* points;
    unsigned char* bridge;
    size_t clock = 0, count = 0, i;
    if (!g || !ng_analytics_symbol_ok(g, type))
        return NG_INVALID_ARGUMENT;
    discovery = (size_t*)calloc(g->nn, sizeof(*discovery));
    low = (size_t*)calloc(g->nn, sizeof(*low));
    points = (unsigned char*)calloc(g->nn, 1);
    bridge = (unsigned char*)calloc(g->nr, 1);
    if ((g->nn && (!discovery || !low || !points)) || (g->nr && !bridge)) {
        free(discovery);
        free(low);
        free(points);
        free(bridge);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        if (!discovery[i])
            ng_articulation_visit(g,
                                  i,
                                  type,
                                  SIZE_MAX,
                                  &clock,
                                  discovery,
                                  low,
                                  points,
                                  bridge);
    for (i = 0; i < g->nr; i++)
        if (bridge[i]) {
            if (!out || count >= capacity) {
                free(discovery);
                free(low);
                free(points);
                free(bridge);
                return NG_LIMIT;
            }
            out[count++] = g->re[i].id;
        }
    if (out_count)
        *out_count = count;
    free(discovery);
    free(low);
    free(points);
    free(bridge);
    return NG_OK;
}
ng_status ng_minimum_spanning_tree(const ng_graph* g,
                                   ng_symbol_id type,
                                   ng_symbol_id weight_key,
                                   ng_relationship_id* out,
                                   size_t capacity,
                                   size_t* out_count,
                                   double* out_weight) {
    typedef struct {
        size_t index, source, target;
        double weight;
    } edge;
    edge* edges;
    size_t* parent;
    size_t edge_count = 0, selected = 0, i, j;
    double total = 0.0;
    if (!g || !ng_analytics_symbol_ok(g, type) ||
        (weight_key && !ng_analytics_symbol_ok(g, weight_key)))
        return NG_INVALID_ARGUMENT;
    edges = (edge*)calloc(g->nr ? g->nr : 1, sizeof(*edges));
    parent = (size_t*)malloc(g->nn * sizeof(*parent));
    if (!edges || (g->nn && !parent)) {
        free(edges);
        free(parent);
        return NG_OOM;
    }
    for (i = 0; i < g->nn; i++)
        parent[i] = i;
    for (i = 0; i < g->nr; i++) {
        const rel_i* rel = &g->re[i];
        const prop* property;
        size_t source, target;
        double weight = 1.0;
        if (!ng_analytics_rel_ok(rel, type))
            continue;
        source = ng_node_position(g, rel->src);
        target = ng_node_position(g, rel->dst);
        if (source == SIZE_MAX || target == SIZE_MAX)
            continue;
        if (weight_key) {
            property = findprop(rel->p, rel->np, weight_key);
            if (property) {
                if (property->v.type == NG_VALUE_INT64)
                    weight = (double)property->v.as.integer;
                else if (property->v.type == NG_VALUE_DOUBLE)
                    weight = property->v.as.real;
                else {
                    free(edges);
                    free(parent);
                    return NG_PARSE_ERROR;
                }
                if (weight < 0) {
                    free(edges);
                    free(parent);
                    return NG_PARSE_ERROR;
                }
            }
        }
        edges[edge_count++] = (edge){i, source, target, weight};
    }
    for (i = 0; i < edge_count; i++)
        for (j = i + 1; j < edge_count; j++)
            if (edges[j].weight < edges[i].weight) {
                edge swap = edges[i];
                edges[i] = edges[j];
                edges[j] = swap;
            }
    for (i = 0; i < edge_count; i++) {
        size_t a = edges[i].source, b = edges[i].target;
        while (parent[a] != a)
            a = parent[a];
        while (parent[b] != b)
            b = parent[b];
        if (a == b)
            continue;
        parent[a] = b;
        if (!out || selected >= capacity) {
            free(edges);
            free(parent);
            return NG_LIMIT;
        }
        out[selected++] = g->re[edges[i].index].id;
        total += edges[i].weight;
    }
    if (out_count)
        *out_count = selected;
    if (out_weight)
        *out_weight = total;
    free(edges);
    free(parent);
    return NG_OK;
}
ng_status ng_max_flow(const ng_graph* g,
                      ng_node_id source,
                      ng_node_id target,
                      ng_symbol_id type,
                      ng_symbol_id capacity_key,
                      double* out_flow) {
    size_t source_pos, target_pos, i, j;
    double* residual = NULL;
    size_t* parent = NULL;
    size_t* queue = NULL;
    double flow = 0.0;
    if (!g || !out_flow || source == target || !ng_analytics_symbol_ok(g, type) ||
        (capacity_key && !ng_analytics_symbol_ok(g, capacity_key)))
        return NG_INVALID_ARGUMENT;
    source_pos = ng_node_position(g, source);
    target_pos = ng_node_position(g, target);
    if (source_pos == SIZE_MAX || target_pos == SIZE_MAX)
        return NG_NOT_FOUND;
    if (g->nn > SIZE_MAX / (g->nn ? g->nn : 1) ||
        !(residual = (double*)calloc(g->nn * g->nn, sizeof(*residual))) ||
        !(parent = (size_t*)malloc(g->nn * sizeof(*parent))) ||
        !(queue = (size_t*)malloc(g->nn * sizeof(*queue)))) {
        free(residual);
        free(parent);
        free(queue);
        return NG_OOM;
    }
    for (i = 0; i < g->nr; i++) {
        const rel_i* rel = &g->re[i];
        const prop* property;
        size_t a, b;
        double capacity = 1.0;
        if (!ng_analytics_rel_ok(rel, type))
            continue;
        a = ng_node_position(g, rel->src);
        b = ng_node_position(g, rel->dst);
        if (a == SIZE_MAX || b == SIZE_MAX)
            continue;
        if (capacity_key && (property = findprop(rel->p, rel->np, capacity_key))) {
            if (property->v.type == NG_VALUE_INT64)
                capacity = (double)property->v.as.integer;
            else if (property->v.type == NG_VALUE_DOUBLE)
                capacity = property->v.as.real;
            else {
                free(residual);
                free(parent);
                free(queue);
                return NG_PARSE_ERROR;
            }
            if (capacity < 0) {
                free(residual);
                free(parent);
                free(queue);
                return NG_PARSE_ERROR;
            }
        }
        residual[a * g->nn + b] += capacity;
    }
    for (;;) {
        size_t head = 0, tail = 0;
        double path_flow = 1e300;
        unsigned char* visited = (unsigned char*)calloc(g->nn, 1);
        if (g->nn && !visited) {
            free(residual);
            free(parent);
            free(queue);
            return NG_OOM;
        }
        for (i = 0; i < g->nn; i++)
            parent[i] = SIZE_MAX;
        visited[source_pos] = 1;
        queue[tail++] = source_pos;
        while (head < tail && !visited[target_pos]) {
            size_t current = queue[head++];
            for (j = 0; j < g->nn; j++)
                if (!visited[j] && residual[current * g->nn + j] > 0.0) {
                    visited[j] = 1;
                    parent[j] = current;
                    queue[tail++] = j;
                }
        }
        if (!visited[target_pos]) {
            free(visited);
            break;
        }
        for (i = target_pos; i != source_pos; i = parent[i])
            if (residual[parent[i] * g->nn + i] < path_flow)
                path_flow = residual[parent[i] * g->nn + i];
        for (i = target_pos; i != source_pos; i = parent[i]) {
            size_t previous = parent[i];
            residual[previous * g->nn + i] -= path_flow;
            residual[i * g->nn + previous] += path_flow;
        }
        flow += path_flow;
        free(visited);
    }
    *out_flow = flow;
    free(residual);
    free(parent);
    free(queue);
    return NG_OK;
}
ng_status ng_topological_sort(
    const ng_graph* g, ng_symbol_id type, ng_node_id* out, size_t capacity, size_t* out_count) {
    uint64_t* indeg;
    size_t *q, head = 0, tail = 0, i, emitted = 0;
    ng_status s;
    s = ng_analytics_check_output(g, type, out, capacity, out_count);
    if (s != NG_OK)
        return s;
    indeg = (uint64_t*)calloc(g->nn, sizeof(*indeg));
    q = (size_t*)malloc(g->nn * sizeof(*q));
    if (g->nn && (!indeg || !q)) {
        free(indeg);
        free(q);
        return NG_OOM;
    }
    for (i = 0; i < g->nr; i++)
        if (ng_analytics_rel_ok(&g->re[i], type)) {
            size_t p = ng_node_position(g, g->re[i].dst);
            if (p != SIZE_MAX)
                indeg[p]++;
        }
    for (i = 0; i < g->nn; i++)
        if (!indeg[i])
            q[tail++] = i;
    while (head < tail) {
        size_t cur = q[head++], j;
        out[emitted++] = g->no[cur].id;
        for (j = 0; j < g->nr; j++)
            if (ng_analytics_rel_ok(&g->re[j], type) && g->re[j].src == g->no[cur].id) {
                size_t p = ng_node_position(g, g->re[j].dst);
                if (p != SIZE_MAX && indeg[p] > 0 && --indeg[p] == 0)
                    q[tail++] = p;
            }
    }
    free(indeg);
    free(q);
    return emitted == g->nn ? NG_OK : NG_EXISTS;
}
static uint64_t ng_random_walk_next(uint64_t* state) {
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * 2685821657736338717ULL;
}
ng_status ng_random_walk(const ng_graph* g,
                         ng_node_id start,
                         const ng_random_walk_options* options,
                         ng_node_id* out,
                         size_t capacity,
                         size_t* out_count) {
    ng_node_id *candidates = NULL, current = start;
    size_t i, count = 0, candidate_count;
    uint64_t state;
    if (!g || !options)
        return NG_INVALID_ARGUMENT;
    if (options->direction > NG_DIRECTION_EITHER)
        return NG_INVALID_ARGUMENT;
    if (!ng_analytics_symbol_ok(g, options->type))
        return NG_NOT_FOUND;
    if (ng_node_position(g, start) == SIZE_MAX)
        return NG_NOT_FOUND;
    if (capacity < (size_t)options->max_steps + 1) {
        if (out_count)
            *out_count = (size_t)options->max_steps + 1;
        return NG_LIMIT;
    }
    if (!out)
        return NG_INVALID_ARGUMENT;
    if (out_count)
        *out_count = 0;
    state = options->seed ? options->seed : 0x9e3779b97f4a7c15ULL;
    out[count++] = current;
    candidates = g->nr ? (ng_node_id*)malloc(g->nr * sizeof(*candidates)) : NULL;
    if (g->nr && !candidates)
        return NG_OOM;
    for (i = 0; i < options->max_steps; i++) {
        size_t j;
        candidate_count = 0;
        for (j = 0; j < g->nr; j++) {
            const rel_i* r = &g->re[j];
            ng_node_id next = 0;
            if (!ng_analytics_rel_ok(r, options->type))
                continue;
            if (options->direction == NG_DIRECTION_OUTGOING && r->src == current)
                next = r->dst;
            else if (options->direction == NG_DIRECTION_INCOMING && r->dst == current)
                next = r->src;
            else if (options->direction == NG_DIRECTION_EITHER) {
                if (r->src == current)
                    next = r->dst;
                else if (r->dst == current)
                    next = r->src;
            }
            if (next)
                candidates[candidate_count++] = next;
        }
        if (!candidate_count)
            break;
        current = candidates[ng_random_walk_next(&state) % candidate_count];
        out[count++] = current;
    }
    free(candidates);
    if (out_count)
        *out_count = count;
    return NG_OK;
}
const char* ng_symbol_name(const ng_graph* g, ng_symbol_id id) {
    size_t i;
    if (!g)
        return NULL;
    for (i = 0; i < g->ns; i++)
        if (g->sy[i].id == id)
            return g->sy[i].s;
    return NULL;
}
ng_status ng_validate(const ng_graph* g) {
    size_t i, j, k;
    if (!g)
        return NG_INVALID_ARGUMENT;
    if (!g->next_node || !g->next_rel || !g->next_sym)
        return NG_CORRUPT;
    for (i = 0; i < g->ns; i++) {
        if (!g->sy[i].id || !g->sy[i].s || !*g->sy[i].s || g->sy[i].id >= g->next_sym)
            return NG_CORRUPT;
        for (j = 0; j < i; j++)
            if (g->sy[j].id == g->sy[i].id || !strcmp(g->sy[j].s, g->sy[i].s))
                return NG_CORRUPT;
    }
    for (i = 0; i < g->nn; i++) {
        if (!g->no[i].id || g->no[i].id >= g->next_node)
            return NG_CORRUPT;
        for (j = 0; j < i; j++)
            if (g->no[j].id == g->no[i].id)
                return NG_CORRUPT;
        for (j = 0; j < g->no[i].nl; j++) {
            if (!g->no[i].labels[j] || !ng_symbol_name(g, g->no[i].labels[j]))
                return NG_CORRUPT;
            for (k = 0; k < j; k++)
                if (g->no[i].labels[k] == g->no[i].labels[j])
                    return NG_CORRUPT;
        }
        for (j = 0; j < g->no[i].np; j++) {
            if (!g->no[i].p[j].key || !ng_symbol_name(g, g->no[i].p[j].key) ||
                !ng_valid_value(&g->no[i].p[j].v))
                return NG_CORRUPT;
            for (k = 0; k < j; k++)
                if (g->no[i].p[k].key == g->no[i].p[j].key)
                    return NG_CORRUPT;
        }
    }
    for (i = 0; i < g->nr; i++) {
        if (!g->re[i].id || g->re[i].id >= g->next_rel || !node((ng_graph*)g, g->re[i].src) ||
            !node((ng_graph*)g, g->re[i].dst) || !ng_symbol_name(g, g->re[i].type))
            return NG_CORRUPT;
        for (j = 0; j < i; j++)
            if (g->re[j].id == g->re[i].id)
                return NG_CORRUPT;
        for (j = 0; j < g->re[i].np; j++) {
            if (!g->re[i].p[j].key || !ng_symbol_name(g, g->re[i].p[j].key) ||
                !ng_valid_value(&g->re[i].p[j].v))
                return NG_CORRUPT;
            for (k = 0; k < j; k++)
                if (g->re[i].p[k].key == g->re[i].p[j].key)
                    return NG_CORRUPT;
        }
    }
    for (i = 0; i < g->nc; i++) {
        if ((g->co[i].kind != NG_NODE_CONSTRAINT_REQUIRED_PROPERTY &&
             g->co[i].kind != NG_NODE_CONSTRAINT_UNIQUE_PROPERTY) ||
            !g->co[i].key || !ng_symbol_name(g, g->co[i].key) ||
            (g->co[i].label && !ng_symbol_name(g, g->co[i].label)))
            return NG_CORRUPT;
        for (j = 0; j < i; j++)
            if (g->co[j].kind == g->co[i].kind && g->co[j].label == g->co[i].label &&
                g->co[j].key == g->co[i].key)
                return NG_CORRUPT;
    }
    for (i = 0; i < g->nix; i++) {
        if (!g->ix[i].key || !ng_symbol_name(g, g->ix[i].key) ||
            (g->ix[i].label && !ng_symbol_name(g, g->ix[i].label)))
            return NG_CORRUPT;
        for (j = 0; j < i; j++)
            if (g->ix[j].label == g->ix[i].label && g->ix[j].key == g->ix[i].key)
                return NG_CORRUPT;
    }
    return ng_validate_constraints_all(g);
}
const char* ng_status_name(ng_status s) {
    static const char* n[] = {[0] = "ok",
                              "invalid argument",
                              "not found",
                              "parse error",
                              "exists",
                              "out of memory",
                              "io error",
                              "corrupt",
                              "limit"};
    return s <= NG_LIMIT ? n[s] : "unknown";
}
static int ng_value_equal(const ng_value* a, const ng_value* b) {
    uint64_t x, y;
    size_t i;
    if (!a || !b || a->type != b->type)
        return 0;
    if (a->type == NG_VALUE_NULL)
        return 1;
    if (a->type == NG_VALUE_BOOL)
        return a->as.boolean == b->as.boolean;
    if (a->type == NG_VALUE_INT64)
        return a->as.integer == b->as.integer;
    if (a->type == NG_VALUE_DOUBLE) {
        memcpy(&x, &a->as.real, 8);
        memcpy(&y, &b->as.real, 8);
        return x == y;
    }
    if (a->length != b->length)
        return 0;
    if (a->type == NG_VALUE_STRING)
        return !a->length || !memcmp(a->as.string, b->as.string, a->length);
    if (a->type == NG_VALUE_BYTES)
        return !a->length || !memcmp(a->as.bytes, b->as.bytes, a->length);
    if (a->type == NG_VALUE_LIST) {
        if (!a->as.list || !b->as.list)
            return a->as.list == b->as.list;
        for (i = 0; i < a->as.list->count; i++)
            if (!ng_value_equal(&a->as.list->items[i], &b->as.list->items[i]))
                return 0;
        return 1;
    }
    if (a->type == NG_VALUE_MAP) {
        if (!a->as.map || !b->as.map)
            return a->as.map == b->as.map;
        if (a->as.map->count != b->as.map->count)
            return 0;
        for (i = 0; i < a->as.map->count; i++) {
            if (strcmp(a->as.map->entries[i].key, b->as.map->entries[i].key) ||
                !ng_value_equal(&a->as.map->entries[i].value, &b->as.map->entries[i].value))
                return 0;
        }
        return 1;
    }
    return 0;
}
static int ng_query_compare_match(const ng_value* a, const ng_value* b, int op);
static int ng_param_name_equal(const char* a, const char* b, size_t n) {
    return a && b && strlen(a) == n && !memcmp(a, b, n);
}
static ng_status ng_query_resolve_value(const ng_value* in, ng_value* out) {
    size_t i;
    if (!in || !out)
        return NG_INVALID_ARGUMENT;
    if (in->type != NG_VALUE_PARAM) {
        *out = *in;
        return NG_OK;
    }
    for (i = 0; i < ng_query_parameter_count; i++)
        if (ng_param_name_equal(ng_query_parameters[i].name, in->as.string, in->length)) {
            *out = ng_query_parameters[i].value;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
static int ng_query_resolve_compare(const ng_value* actual, const ng_value* expected, int op) {
    ng_value resolved;
    ng_status s = ng_query_resolve_value(expected, &resolved);
    if (s != NG_OK) {
        ng_query_parameter_error = 1;
        return 0;
    }
    return ng_query_compare_match(actual, &resolved, op);
}
static int ng_node_matches_label(const node_i* n, ng_symbol_id label) {
    size_t j;
    if (!label)
        return 1;
    for (j = 0; j < n->nl; j++)
        if (n->labels[j] == label)
            return 1;
    return 0;
}
ng_status ng_find_nodes(const ng_graph* g,
                        ng_symbol_id label,
                        ng_symbol_id key,
                        const ng_value* v,
                        ng_node_match_visitor visit,
                        void* ctx) {
    size_t i, j;
    if (!g || !v || !key || !visit)
        return NG_INVALID_ARGUMENT;
    if (!ng_valid_value(v))
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nn; i++) {
        int has = label == 0;
        for (j = 0; label && j < g->no[i].nl; j++)
            if (g->no[i].labels[j] == label)
                has = 1;
        if (has) {
            const prop* p = findprop(g->no[i].p, g->no[i].np, key);
            if (p && ng_value_equal(&p->v, v) && !visit(g->no[i].id, ctx))
                break;
        }
    }
    return NG_OK;
}
ng_status ng_require_node_property(const ng_graph* g,
                                   ng_symbol_id label,
                                   ng_symbol_id key,
                                   ng_node_id* out_node) {
    size_t i;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    if (label && !ng_symbol_name(g, label))
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, key))
        return NG_NOT_FOUND;
    if (out_node)
        *out_node = 0;
    for (i = 0; i < g->nn; i++)
        if (ng_node_matches_label(&g->no[i], label)) {
            const prop* p = findprop(g->no[i].p, g->no[i].np, key);
            if (!p || p->v.type == NG_VALUE_NULL) {
                if (out_node)
                    *out_node = g->no[i].id;
                return NG_NOT_FOUND;
            }
        }
    return NG_OK;
}
ng_status ng_unique_node_property(const ng_graph* g,
                                  ng_symbol_id label,
                                  ng_symbol_id key,
                                  ng_node_id* out_first,
                                  ng_node_id* out_second) {
    size_t i, j;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    if (label && !ng_symbol_name(g, label))
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, key))
        return NG_NOT_FOUND;
    if (out_first)
        *out_first = 0;
    if (out_second)
        *out_second = 0;
    for (i = 0; i < g->nn; i++)
        if (ng_node_matches_label(&g->no[i], label)) {
            const prop* a = findprop(g->no[i].p, g->no[i].np, key);
            if (!a || a->v.type == NG_VALUE_NULL)
                continue;
            for (j = i + 1; j < g->nn; j++)
                if (ng_node_matches_label(&g->no[j], label)) {
                    const prop* b = findprop(g->no[j].p, g->no[j].np, key);
                    if (b && b->v.type != NG_VALUE_NULL && ng_value_equal(&a->v, &b->v)) {
                        if (out_first)
                            *out_first = g->no[i].id;
                        if (out_second)
                            *out_second = g->no[j].id;
                        return NG_EXISTS;
                    }
                }
        }
    return NG_OK;
}
static ng_status ng_check_one_constraint(const ng_graph* g, const constraint_i* c) {
    ng_node_id a = 0, b = 0;
    if (c->kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY)
        return ng_require_node_property(g, c->label, c->key, &a);
    if (c->kind == NG_NODE_CONSTRAINT_UNIQUE_PROPERTY)
        return ng_unique_node_property(g, c->label, c->key, &a, &b);
    return NG_CORRUPT;
}
static ng_status ng_validate_constraints_all(const ng_graph* g) {
    size_t i;
    ng_status s;
    if (!g)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nc; i++) {
        s = ng_check_one_constraint(g, &g->co[i]);
        if (s != NG_OK)
            return s;
    }
    return NG_OK;
}
ng_status ng_node_constraint_create(ng_graph* g,
                                    ng_node_constraint_kind kind,
                                    ng_symbol_id label,
                                    ng_symbol_id key) {
    size_t i;
    constraint_i c;
    ng_status s;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    if (kind != NG_NODE_CONSTRAINT_REQUIRED_PROPERTY && kind != NG_NODE_CONSTRAINT_UNIQUE_PROPERTY)
        return NG_INVALID_ARGUMENT;
    if (label && !ng_symbol_name(g, label))
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, key))
        return NG_NOT_FOUND;
    for (i = 0; i < g->nc; i++)
        if (g->co[i].kind == kind && g->co[i].label == label && g->co[i].key == key)
            return NG_EXISTS;
    c.kind = kind;
    c.label = label;
    c.key = key;
    s = ng_check_one_constraint(g, &c);
    if (s != NG_OK)
        return s;
    if (!grow((void**)&g->co, &g->cc, g->nc + 1, sizeof(*g->co)))
        return NG_OOM;
    g->co[g->nc++] = c;
    return NG_OK;
}
ng_status ng_node_constraint_drop(ng_graph* g,
                                  ng_node_constraint_kind kind,
                                  ng_symbol_id label,
                                  ng_symbol_id key) {
    size_t i;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nc; i++)
        if (g->co[i].kind == kind && g->co[i].label == label && g->co[i].key == key) {
            if (i + 1 < g->nc)
                memmove(&g->co[i], &g->co[i + 1], (g->nc - i - 1) * sizeof(*g->co));
            g->nc--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
size_t ng_node_constraint_count(const ng_graph* g) {
    return g ? g->nc : 0;
}
ng_status ng_node_constraint_get(const ng_graph* g,
                                 size_t index,
                                 ng_node_constraint_kind* kind,
                                 ng_symbol_id* label,
                                 ng_symbol_id* key) {
    if (!g || !kind || !label || !key)
        return NG_INVALID_ARGUMENT;
    if (index >= g->nc)
        return NG_NOT_FOUND;
    *kind = g->co[index].kind;
    *label = g->co[index].label;
    *key = g->co[index].key;
    return NG_OK;
}
ng_status ng_node_index_create(ng_graph* g, ng_symbol_id label, ng_symbol_id key) {
    size_t i;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    if (label && !ng_symbol_name(g, label))
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, key))
        return NG_NOT_FOUND;
    for (i = 0; i < g->nix; i++)
        if (g->ix[i].label == label && g->ix[i].key == key)
            return NG_EXISTS;
    if (!grow((void**)&g->ix, &g->cix, g->nix + 1, sizeof(*g->ix)))
        return NG_OOM;
    g->ix[g->nix].label = label;
    g->ix[g->nix].key = key;
    g->nix++;
    return NG_OK;
}
ng_status ng_node_index_drop(ng_graph* g, ng_symbol_id label, ng_symbol_id key) {
    size_t i;
    if (!g || !key)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->nix; i++)
        if (g->ix[i].label == label && g->ix[i].key == key) {
            if (i + 1 < g->nix)
                memmove(&g->ix[i], &g->ix[i + 1], (g->nix - i - 1) * sizeof(*g->ix));
            g->nix--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
size_t ng_node_index_count(const ng_graph* g) {
    return g ? g->nix : 0;
}
ng_status
ng_node_index_get(const ng_graph* g, size_t index, ng_symbol_id* label, ng_symbol_id* key) {
    if (!g || !label || !key)
        return NG_INVALID_ARGUMENT;
    if (index >= g->nix)
        return NG_NOT_FOUND;
    *label = g->ix[index].label;
    *key = g->ix[index].key;
    return NG_OK;
}
static int ng_compare_index_entries(const void* a, const void* b) {
    const ng_node_index_entry *x = (const ng_node_index_entry*)a,
                              *y = (const ng_node_index_entry*)b;
    return x->id > y->id ? 1 : x->id < y->id ? -1 : 0;
}
ng_status
ng_node_index_build(const ng_graph* g, ng_symbol_id label, ng_symbol_id key, ng_node_index** out) {
    ng_node_index* idx;
    size_t i, j;
    if (!g || !key || !out)
        return NG_INVALID_ARGUMENT;
    if (label && !ng_symbol_name(g, label))
        return NG_NOT_FOUND;
    if (!ng_symbol_name(g, key))
        return NG_NOT_FOUND;
    idx = (ng_node_index*)calloc(1, sizeof(*idx));
    if (!idx)
        return NG_OOM;
    idx->label = label;
    idx->key = key;
    for (i = 0; i < g->nn; i++) {
        int has = label == 0;
        const prop* p;
        for (j = 0; label && j < g->no[i].nl; j++)
            if (g->no[i].labels[j] == label)
                has = 1;
        if (!has)
            continue;
        p = findprop(g->no[i].p, g->no[i].np, key);
        if (!p)
            continue;
        if (!grow((void**)&idx->entries, &idx->cap, idx->count + 1, sizeof(*idx->entries))) {
            ng_node_index_free(idx);
            return NG_OOM;
        }
        idx->entries[idx->count].id = g->no[i].id;
        if (valcopy(&idx->entries[idx->count].value, &p->v) != NG_OK) {
            ng_node_index_free(idx);
            return NG_OOM;
        }
        idx->count++;
    }
    qsort(idx->entries, idx->count, sizeof(*idx->entries), ng_compare_index_entries);
    *out = idx;
    return NG_OK;
}
ng_status ng_node_index_find(const ng_node_index* idx,
                             const ng_value* v,
                             ng_node_match_visitor visit,
                             void* ctx) {
    size_t i;
    if (!idx || !v || !visit)
        return NG_INVALID_ARGUMENT;
    if (!ng_valid_value(v))
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < idx->count; i++)
        if (ng_value_equal(&idx->entries[i].value, v) && !visit(idx->entries[i].id, ctx))
            break;
    return NG_OK;
}
void ng_node_index_free(ng_node_index* idx) {
    size_t i;
    if (!idx)
        return;
    for (i = 0; i < idx->count; i++)
        valfree(&idx->entries[i].value);
    free(idx->entries);
    free(idx);
}
static int ng_procedure_name_valid(const char* name) {
    if (!name || !ng_ident_char((unsigned char)*name) || isdigit((unsigned char)*name))
        return 0;
    while (*name)
        if (!ng_ident_char((unsigned char)*name++))
            return 0;
    return 1;
}
ng_status
ng_procedure_register(ng_graph* g, const char* name, ng_procedure_handler handler, void* context) {
    size_t i;
    if (!g || !ng_procedure_name_valid(name) || !handler)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->procedure_count; i++)
        if (!strcmp(g->procedures[i].name, name))
            return NG_EXISTS;
    if (!grow((void**)&g->procedures,
              &g->procedure_capacity,
              g->procedure_count + 1,
              sizeof(*g->procedures)))
        return NG_OOM;
    g->procedures[g->procedure_count].name = dupstr(name);
    if (!g->procedures[g->procedure_count].name)
        return NG_OOM;
    g->procedures[g->procedure_count].handler = handler;
    g->procedures[g->procedure_count].context = context;
    g->procedure_count++;
    return NG_OK;
}
ng_status ng_procedure_unregister(ng_graph* g, const char* name) {
    size_t i;
    if (!g || !name)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < g->procedure_count; i++)
        if (!strcmp(g->procedures[i].name, name)) {
            free(g->procedures[i].name);
            if (i + 1 < g->procedure_count)
                memmove(&g->procedures[i],
                        &g->procedures[i + 1],
                        (g->procedure_count - i - 1) * sizeof(*g->procedures));
            g->procedure_count--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
#define NG_QUERY_MAX_TERMS 8
#define NG_QUERY_MAX_LIST_VALUES 8
#define NG_QUERY_MAX_EXPR 16
#define NG_QUERY_MAX_PROPS 8
typedef struct {
    char key[128];
    ng_value value;
} ng_query_prop;
typedef struct {
    char key[128], var;
    int is_id, connector, op, value_count;
    ng_value value, values[NG_QUERY_MAX_LIST_VALUES];
} ng_query_term;
typedef struct {
    int kind, left, right, term;
} ng_query_expr;
typedef struct {
    char left_label[128], right_label[128], rel_type[128], key[128], return_key[128],
        return_keys[8][128], return_aliases[8][128], order_key[128], left_var_name[64],
        right_var_name[64], rel_var_name[64];
    int has_relationship, has_second_node, has_where, where_is_id, has_limit, has_skip, has_order,
        order_is_property, order_is_id, order_desc, return_is_property, return_is_id,
        has_var_length, return_count, return_is_properties[8], return_is_ids[8],
        return_has_aliases[8], term_count, expr_count, where_root, rel_prop_count, rel_dir;
    char where_var, return_var, return_vars[8], order_var, rel_var;
    ng_query_term terms[NG_QUERY_MAX_TERMS];
    ng_query_expr exprs[NG_QUERY_MAX_EXPR];
    ng_query_prop rel_props[NG_QUERY_MAX_PROPS];
    ng_value value;
    uint64_t limit, skip;
    uint32_t min_depth, max_depth;
} ng_query_plan;
static ng_status ng_query_capture_legacy_schema(const ng_query_plan* plan);
static const char* ng_skip_ws(const char* p) {
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}
static ng_symbol_id ng_symbol_id_by_text(const ng_graph* g, const char* s) {
    size_t i;
    for (i = 0; i < g->ns; i++)
        if (!strcmp(g->sy[i].s, s))
            return g->sy[i].id;
    return 0;
}
static int ng_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}
static ng_status ng_query_parse_prop_map(const char** pp, ng_query_prop* props, size_t* count);
static int ng_query_expr_add(ng_query_plan* plan, int kind, int left, int right, int term);
static ng_status ng_query_add_inline_props(ng_query_plan* plan,
                                           char role,
                                           const ng_query_prop* props,
                                           size_t prop_count) {
    size_t i;
    int root = plan->where_root;
    for (i = 0; i < prop_count; i++) {
        ng_query_term* t;
        int term, node;
        if (plan->term_count >= NG_QUERY_MAX_TERMS)
            return NG_PARSE_ERROR;
        term = plan->term_count;
        t = &plan->terms[term];
        memset(t, 0, sizeof(*t));
        t->var = role;
        t->op = 0;
        t->value = props[i].value;
        strcpy(t->key, props[i].key);
        node = ng_query_expr_add(plan, 0, -1, -1, term);
        if (node < 0)
            return NG_PARSE_ERROR;
        plan->term_count++;
        root = root < 0 ? node : ng_query_expr_add(plan, 1, root, node, -1);
        if (root < 0)
            return NG_PARSE_ERROR;
        if (term == 0) {
            plan->where_var = t->var;
            plan->where_is_id = t->is_id;
            strcpy(plan->key, t->key);
            plan->value = t->value;
        }
    }
    if (prop_count) {
        plan->has_where = 1;
        plan->where_root = root;
    }
    return NG_OK;
}
static ng_status ng_query_parse_node_role(const char** pp,
                                          ng_query_plan* plan,
                                          char role,
                                          char* var,
                                          size_t var_capacity,
                                          char* label,
                                          size_t label_capacity) {
    const char *p = *pp, *s;
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    size_t n, prop_count = 0;
    if (*p != '(')
        return NG_PARSE_ERROR;
    p++;
    p = ng_skip_ws(p);
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (n >= var_capacity)
            return NG_PARSE_ERROR;
        memcpy(var, s, n);
        var[n] = 0;
        p = ng_skip_ws(p);
    }
    if (*p == ':') {
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= label_capacity)
            return NG_PARSE_ERROR;
        memcpy(label, s, n);
        label[n] = 0;
        p = ng_skip_ws(p);
    }
    if (ng_query_parse_prop_map(&p, props, &prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_add_inline_props(plan, role, props, prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != ')')
        return NG_PARSE_ERROR;
    *pp = p + 1;
    return NG_OK;
}
static int ng_query_resolve_var(const ng_query_plan* plan, const char* name, char* out) {
    if (!name || !*name || !out)
        return 0;
    if (plan->left_var_name[0] && !strcmp(name, plan->left_var_name)) {
        *out = 'n';
        return 1;
    }
    if (plan->right_var_name[0] && !strcmp(name, plan->right_var_name) &&
        (plan->has_relationship || plan->has_second_node)) {
        *out = 'm';
        return 1;
    }
    if (plan->rel_var_name[0] && !strcmp(name, plan->rel_var_name) && plan->has_relationship) {
        *out = 'r';
        return 1;
    }
    return 0;
}
static ng_status ng_query_parse_var_ref(const char** pp, const ng_query_plan* plan, char* out) {
    const char *p = ng_skip_ws(*pp), *s;
    char name[64];
    size_t n;
    if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    s = p;
    while (ng_ident_char((unsigned char)*p))
        p++;
    n = (size_t)(p - s);
    if (!n || n >= sizeof(name))
        return NG_PARSE_ERROR;
    memcpy(name, s, n);
    name[n] = 0;
    if (!ng_query_resolve_var(plan, name, out))
        return NG_PARSE_ERROR;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_value(const char** pp, ng_value* v) {
    const char* p = ng_skip_ws(*pp);
    uint64_t mag = 0, limit;
    int neg = 0;
    if (*p == '$') {
        const char* s = ++p;
        size_t n;
        if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
            return NG_PARSE_ERROR;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        v->type = NG_VALUE_PARAM;
        v->length = n;
        v->as.string = s;
        *pp = p;
        return NG_OK;
    }
    if (*p == '"') {
        char* q;
        size_t n;
        p++;
        q = strchr(p, '"');
        if (!q)
            return NG_PARSE_ERROR;
        n = (size_t)(q - p);
        v->type = NG_VALUE_STRING;
        v->length = n;
        v->as.string = p;
        *pp = q + 1;
        return NG_OK;
    }
    if (!strncmp(p, "true", 4) && !ng_ident_char((unsigned char)p[4])) {
        v->type = NG_VALUE_BOOL;
        v->length = 0;
        v->as.boolean = 1;
        *pp = p + 4;
        return NG_OK;
    }
    if (!strncmp(p, "false", 5) && !ng_ident_char((unsigned char)p[5])) {
        v->type = NG_VALUE_BOOL;
        v->length = 0;
        v->as.boolean = 0;
        *pp = p + 5;
        return NG_OK;
    }
    if (!strncmp(p, "null", 4) && !ng_ident_char((unsigned char)p[4])) {
        v->type = NG_VALUE_NULL;
        v->length = 0;
        *pp = p + 4;
        return NG_OK;
    }
    {
        const char* scan = p;
        if (*scan == '-')
            scan++;
        while (isdigit((unsigned char)*scan))
            scan++;
        if ((*scan == '.' && scan[1] != '.') || *scan == 'e' || *scan == 'E') {
            char* end;
            double value = strtod(p, &end);
            if (end == p)
                return NG_PARSE_ERROR;
            v->type = NG_VALUE_DOUBLE;
            v->length = 0;
            v->as.real = value;
            *pp = end;
            return NG_OK;
        }
    }
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (!isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    limit = neg ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
    while (isdigit((unsigned char)*p)) {
        unsigned d = (unsigned)(*p - '0');
        if (mag > (limit - d) / 10u)
            return NG_PARSE_ERROR;
        mag = mag * 10u + d;
        p++;
    }
    v->type = NG_VALUE_INT64;
    v->length = 0;
    v->as.integer =
        neg ? (mag == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)mag) : (int64_t)mag;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_depth(const char** pp, ng_query_plan* plan) {
    const char* p = *pp;
    uint64_t a = 0, b = 0;
    if (*p != '*')
        return NG_OK;
    p++;
    if (!isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    while (isdigit((unsigned char)*p)) {
        unsigned d = (unsigned)(*p - '0');
        if (a > (64 - d) / 10u)
            return NG_PARSE_ERROR;
        a = a * 10u + d;
        p++;
    }
    b = a;
    if (p[0] == '.' && p[1] == '.') {
        p += 2;
        if (!isdigit((unsigned char)*p))
            return NG_PARSE_ERROR;
        b = 0;
        while (isdigit((unsigned char)*p)) {
            unsigned d = (unsigned)(*p - '0');
            if (b > (64 - d) / 10u)
                return NG_PARSE_ERROR;
            b = b * 10u + d;
            p++;
        }
    }
    if (a < 1 || b < a || b > 64)
        return NG_PARSE_ERROR;
    plan->has_var_length = 1;
    plan->min_depth = (uint32_t)a;
    plan->max_depth = (uint32_t)b;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_return_list(const char** pp, ng_query_plan* plan) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    if (strncmp(p, "RETURN", 6) || !isspace((unsigned char)p[6]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 6);
    for (;;) {
        int i = plan->return_count;
        if (i >= 8)
            return NG_PARSE_ERROR;
        if (ng_query_parse_var_ref(&p, plan, &plan->return_vars[i]) != NG_OK)
            return NG_PARSE_ERROR;
        if (*p == '.') {
            p++;
            s = p;
            while (ng_ident_char((unsigned char)*p))
                p++;
            n = (size_t)(p - s);
            if (!n || n >= sizeof(plan->return_keys[i]))
                return NG_PARSE_ERROR;
            memcpy(plan->return_keys[i], s, n);
            plan->return_keys[i][n] = 0;
            plan->return_is_properties[i] = 1;
            if (!strcmp(plan->return_keys[i], "id"))
                plan->return_is_ids[i] = 1;
        }
        p = ng_skip_ws(p);
        if (!strncmp(p, "AS", 2) && isspace((unsigned char)p[2])) {
            p = ng_skip_ws(p + 2);
            s = p;
            if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
                return NG_PARSE_ERROR;
            while (ng_ident_char((unsigned char)*p))
                p++;
            n = (size_t)(p - s);
            if (!n || n >= sizeof(plan->return_aliases[i]))
                return NG_PARSE_ERROR;
            memcpy(plan->return_aliases[i], s, n);
            plan->return_aliases[i][n] = 0;
            plan->return_has_aliases[i] = 1;
            p = ng_skip_ws(p);
        }
        plan->return_count++;
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
    }
    plan->return_var = plan->return_vars[0];
    strcpy(plan->return_key, plan->return_keys[0]);
    plan->return_is_property = plan->return_is_properties[0];
    plan->return_is_id = plan->return_is_ids[0];
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_uint64(const char** pp, uint64_t* out) {
    const char* p = ng_skip_ws(*pp);
    uint64_t v = 0;
    if (!isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    while (isdigit((unsigned char)*p)) {
        unsigned d = (unsigned)(*p - '0');
        if (v > (UINT64_MAX - d) / 10u)
            return NG_PARSE_ERROR;
        v = v * 10u + d;
        p++;
    }
    *out = v;
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static ng_status ng_query_parse_order_by(const char** pp, ng_query_plan* plan) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    if (plan->has_order)
        return NG_PARSE_ERROR;
    if (ng_query_parse_var_ref(&p, plan, &plan->order_var) != NG_OK)
        return NG_PARSE_ERROR;
    if (*p == '.') {
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(plan->order_key))
            return NG_PARSE_ERROR;
        memcpy(plan->order_key, s, n);
        plan->order_key[n] = 0;
        plan->order_is_property = 1;
        if (!strcmp(plan->order_key, "id"))
            plan->order_is_id = 1;
    } else
        plan->order_is_id = 1;
    p = ng_skip_ws(p);
    if (!strncmp(p, "ASC", 3) && !ng_ident_char((unsigned char)p[3]))
        p = ng_skip_ws(p + 3);
    else if (!strncmp(p, "DESC", 4) && !ng_ident_char((unsigned char)p[4])) {
        plan->order_desc = 1;
        p = ng_skip_ws(p + 4);
    }
    plan->has_order = 1;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_term(const char** pp, ng_query_plan* plan, int connector) {
    const char *p = ng_skip_ws(*pp), *s;
    ng_query_term* t;
    size_t n;
    if (plan->term_count >= NG_QUERY_MAX_TERMS)
        return NG_PARSE_ERROR;
    t = &plan->terms[plan->term_count];
    memset(t, 0, sizeof(*t));
    t->connector = connector;
    if (!strncmp(p, "id(", 3)) {
        p = ng_skip_ws(p + 3);
        if (ng_query_parse_var_ref(&p, plan, &t->var) != NG_OK)
            return NG_PARSE_ERROR;
        if (*p != ')')
            return NG_PARSE_ERROR;
        p++;
        t->is_id = 1;
        strcpy(t->key, "id");
    } else {
        if (ng_query_parse_var_ref(&p, plan, &t->var) != NG_OK)
            return NG_PARSE_ERROR;
        if (*p != '.')
            return NG_PARSE_ERROR;
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(t->key))
            return NG_PARSE_ERROR;
        memcpy(t->key, s, n);
        t->key[n] = 0;
        if (!strcmp(t->key, "id"))
            t->is_id = 1;
    }
    p = ng_skip_ws(p);
    if (!strncmp(p, "<>", 2)) {
        t->op = 2;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, "<=", 2)) {
        t->op = 4;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, ">=", 2)) {
        t->op = 6;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (*p == '<') {
        t->op = 3;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (*p == '>') {
        t->op = 5;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (*p == '=') {
        t->op = 0;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
        if (t->is_id && t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, "IS", 2) && isspace((unsigned char)p[2])) {
        p = ng_skip_ws(p + 2);
        if (!strncmp(p, "NOT", 3) && isspace((unsigned char)p[3])) {
            p = ng_skip_ws(p + 3);
            if (strncmp(p, "NULL", 4) || ng_ident_char((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            t->op = 8;
            p += 4;
        } else {
            if (strncmp(p, "NULL", 4) || ng_ident_char((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            t->op = 7;
            p += 4;
        }
    } else if (!strncmp(p, "IN", 2) && isspace((unsigned char)p[2])) {
        t->op = 1;
        p = ng_skip_ws(p + 2);
        if (*p != '[')
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (*p == ']')
            return NG_PARSE_ERROR;
        for (;;) {
            if (t->value_count >= NG_QUERY_MAX_LIST_VALUES)
                return NG_PARSE_ERROR;
            if (ng_query_parse_value(&p, &t->values[t->value_count]) != NG_OK)
                return NG_PARSE_ERROR;
            if (t->is_id && t->values[t->value_count].type != NG_VALUE_INT64 &&
                t->values[t->value_count].type != NG_VALUE_PARAM)
                return NG_PARSE_ERROR;
            t->value_count++;
            p = ng_skip_ws(p);
            if (*p != ',')
                break;
            p = ng_skip_ws(p + 1);
        }
        if (*p != ']')
            return NG_PARSE_ERROR;
        p++;
        t->value = t->values[0];
    } else
        return NG_PARSE_ERROR;
    if (plan->term_count == 0) {
        plan->where_var = t->var;
        plan->where_is_id = t->is_id;
        strcpy(plan->key, t->key);
        plan->value = t->value;
    }
    plan->term_count++;
    plan->has_where = 1;
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static int ng_query_expr_add(ng_query_plan* plan, int kind, int left, int right, int term) {
    int i = plan->expr_count;
    if (i >= NG_QUERY_MAX_EXPR)
        return -1;
    plan->exprs[i].kind = kind;
    plan->exprs[i].left = left;
    plan->exprs[i].right = right;
    plan->exprs[i].term = term;
    plan->expr_count++;
    return i;
}
static ng_status ng_query_parse_or_expr(const char** pp, ng_query_plan* plan, int* out);
static ng_status ng_query_parse_primary_expr(const char** pp, ng_query_plan* plan, int* out) {
    const char* p = ng_skip_ws(*pp);
    int node, term;
    if (!strncmp(p, "NOT", 3) && isspace((unsigned char)p[3])) {
        int child;
        p = ng_skip_ws(p + 3);
        if (ng_query_parse_primary_expr(&p, plan, &child) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_query_expr_add(plan, 3, child, -1, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        *pp = p;
        *out = node;
        return NG_OK;
    }
    if (*p == '(') {
        p = ng_skip_ws(p + 1);
        if (ng_query_parse_or_expr(&p, plan, &node) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p != ')')
            return NG_PARSE_ERROR;
        *pp = ng_skip_ws(p + 1);
        *out = node;
        return NG_OK;
    }
    term = plan->term_count;
    if (ng_query_parse_term(&p, plan, 0) != NG_OK)
        return NG_PARSE_ERROR;
    node = ng_query_expr_add(plan, 0, -1, -1, term);
    if (node < 0)
        return NG_PARSE_ERROR;
    *pp = p;
    *out = node;
    return NG_OK;
}
static ng_status ng_query_parse_and_expr(const char** pp, ng_query_plan* plan, int* out) {
    const char* p = *pp;
    int left, right, node;
    if (ng_query_parse_primary_expr(&p, plan, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (strncmp(p, "AND", 3) || !isspace((unsigned char)p[3]))
            break;
        p = ng_skip_ws(p + 3);
        if (ng_query_parse_primary_expr(&p, plan, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_query_expr_add(plan, 1, left, right, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static ng_status ng_query_parse_or_expr(const char** pp, ng_query_plan* plan, int* out) {
    const char* p = *pp;
    int left, right, node;
    if (ng_query_parse_and_expr(&p, plan, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (strncmp(p, "OR", 2) || !isspace((unsigned char)p[2]))
            break;
        p = ng_skip_ws(p + 2);
        if (ng_query_parse_and_expr(&p, plan, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_query_expr_add(plan, 2, left, right, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static ng_status ng_query_parse_where(const char** pp, ng_query_plan* plan) {
    const char* p = *pp;
    int root;
    if (ng_query_parse_or_expr(&p, plan, &root) != NG_OK)
        return NG_PARSE_ERROR;
    if (plan->where_root >= 0) {
        root = ng_query_expr_add(plan, 1, plan->where_root, root, -1);
        if (root < 0)
            return NG_PARSE_ERROR;
    }
    plan->where_root = root;
    plan->has_where = 1;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse_relationship_pattern(const char** pp, ng_query_plan* plan) {
    const char *p = *pp, *s;
    size_t n, pc = 0;
    if (*p == '<') {
        p++;
        if (*p != '-')
            return NG_PARSE_ERROR;
        plan->rel_dir = -1;
        p++;
    } else if (*p == '-') {
        plan->rel_dir = 1;
        p++;
    } else
        return NG_PARSE_ERROR;
    if (*p != '[')
        return NG_PARSE_ERROR;
    p++;
    p = ng_skip_ws(p);
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (n >= sizeof(plan->rel_var_name))
            return NG_PARSE_ERROR;
        memcpy(plan->rel_var_name, s, n);
        plan->rel_var_name[n] = 0;
        plan->rel_var = 'r';
        p = ng_skip_ws(p);
    }
    if (*p == ':') {
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(plan->rel_type))
            return NG_PARSE_ERROR;
        memcpy(plan->rel_type, s, n);
        plan->rel_type[n] = 0;
        p = ng_skip_ws(p);
    }
    if (ng_query_parse_depth(&p, plan) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (ng_query_parse_prop_map(&p, plan->rel_props, &pc) != NG_OK)
        return NG_PARSE_ERROR;
    plan->rel_prop_count = (int)pc;
    p = ng_skip_ws(p);
    if (*p != ']')
        return NG_PARSE_ERROR;
    p++;
    if (plan->rel_dir < 0) {
        if (*p != '-')
            return NG_PARSE_ERROR;
        p++;
    } else {
        if (*p != '-')
            return NG_PARSE_ERROR;
        p++;
        if (*p == '>')
            p++;
        else
            plan->rel_dir = 0;
    }
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_parse(const char* q, ng_query_plan* plan) {
    const char* p;
    memset(plan, 0, sizeof(*plan));
    plan->return_var = 'n';
    plan->where_var = 'n';
    plan->where_root = -1;
    plan->min_depth = 1;
    plan->max_depth = 1;
    plan->rel_dir = 1;
    if (!q)
        return NG_INVALID_ARGUMENT;
    p = ng_skip_ws(q);
    if (strncmp(p, "MATCH", 5) || !isspace((unsigned char)p[5]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 5);
    if (ng_query_parse_node_role(&p,
                                 plan,
                                 'n',
                                 plan->left_var_name,
                                 sizeof(plan->left_var_name),
                                 plan->left_label,
                                 sizeof(plan->left_label)) != NG_OK)
        return NG_PARSE_ERROR;
    if (*p == '-' || *p == '<') {
        if (ng_query_parse_relationship_pattern(&p, plan) != NG_OK)
            return NG_PARSE_ERROR;
        if (ng_query_parse_node_role(&p,
                                     plan,
                                     'm',
                                     plan->right_var_name,
                                     sizeof(plan->right_var_name),
                                     plan->right_label,
                                     sizeof(plan->right_label)) != NG_OK)
            return NG_PARSE_ERROR;
        plan->has_relationship = 1;
    }
    p = ng_skip_ws(p);
    if (!plan->has_relationship && !strncmp(p, "MATCH", 5) && isspace((unsigned char)p[5])) {
        p = ng_skip_ws(p + 5);
        if (ng_query_parse_node_role(&p,
                                     plan,
                                     'm',
                                     plan->right_var_name,
                                     sizeof(plan->right_var_name),
                                     plan->right_label,
                                     sizeof(plan->right_label)) != NG_OK)
            return NG_PARSE_ERROR;
        plan->has_second_node = 1;
        p = ng_skip_ws(p);
    }
    if (!strncmp(p, "WHERE", 5) && isspace((unsigned char)p[5])) {
        p = ng_skip_ws(p + 5);
        if (ng_query_parse_where(&p, plan) != NG_OK)
            return NG_PARSE_ERROR;
    }
    if (ng_query_parse_return_list(&p, plan) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    while (*p) {
        if (!strncmp(p, "ORDER", 5) && isspace((unsigned char)p[5])) {
            p = ng_skip_ws(p + 5);
            if (strncmp(p, "BY", 2) || !isspace((unsigned char)p[2]))
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 2);
            if (ng_query_parse_order_by(&p, plan) != NG_OK)
                return NG_PARSE_ERROR;
        } else if (!strncmp(p, "SKIP", 4) && isspace((unsigned char)p[4])) {
            if (plan->has_skip)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 4);
            if (ng_query_parse_uint64(&p, &plan->skip) != NG_OK)
                return NG_PARSE_ERROR;
            plan->has_skip = 1;
        } else if (!strncmp(p, "LIMIT", 5) && isspace((unsigned char)p[5])) {
            if (plan->has_limit)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 5);
            if (ng_query_parse_uint64(&p, &plan->limit) != NG_OK)
                return NG_PARSE_ERROR;
            plan->has_limit = 1;
        } else
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    return NG_OK;
}
static int ng_node_has_label_id(const node_i* n, ng_symbol_id label) {
    size_t i;
    if (!label)
        return 1;
    for (i = 0; i < n->nl; i++)
        if (n->labels[i] == label)
            return 1;
    return 0;
}
static int ng_query_label_matches(const node_i* n, ng_symbol_id label) {
    return ng_node_has_label_id(n, label);
}
static int ng_query_rel_matches_props(const rel_i* r, const ng_property* props, size_t prop_count) {
    size_t i;
    if (!r)
        return 0;
    for (i = 0; i < prop_count; i++) {
        const prop* p = findprop(r->p, r->np, props[i].key);
        ng_value v;
        if (!p)
            return 0;
        if (ng_query_resolve_value(&props[i].value, &v) != NG_OK) {
            ng_query_parameter_error = 1;
            return 0;
        }
        if (!ng_value_equal(&p->v, &v))
            return 0;
    }
    return 1;
}
static int ng_compare_values(const ng_value* a, const ng_value* b, int* out) {
    int c;
    if (!a || !b || !out)
        return 0;
    if ((a->type == NG_VALUE_INT64 || a->type == NG_VALUE_DOUBLE) &&
        (b->type == NG_VALUE_INT64 || b->type == NG_VALUE_DOUBLE)) {
        double ax = a->type == NG_VALUE_DOUBLE ? a->as.real : (double)a->as.integer,
               bx = b->type == NG_VALUE_DOUBLE ? b->as.real : (double)b->as.integer;
        *out = ax < bx ? -1 : ax > bx ? 1 : 0;
        return 1;
    }
    if (a->type != b->type)
        return 0;
    if (a->type == NG_VALUE_INT64)
        c = a->as.integer < b->as.integer ? -1 : a->as.integer > b->as.integer ? 1 : 0;
    else if (a->type == NG_VALUE_DOUBLE)
        c = a->as.real < b->as.real ? -1 : a->as.real > b->as.real ? 1 : 0;
    else if (a->type == NG_VALUE_BOOL)
        c = a->as.boolean < b->as.boolean ? -1 : a->as.boolean > b->as.boolean ? 1 : 0;
    else if (a->type == NG_VALUE_STRING) {
        size_t n = a->length < b->length ? a->length : b->length;
        c = n ? memcmp(a->as.string, b->as.string, n) : 0;
        if (!c)
            c = a->length < b->length ? -1 : a->length > b->length ? 1 : 0;
    } else
        return 0;
    *out = c;
    return 1;
}
static int ng_query_compare_match(const ng_value* a, const ng_value* b, int op) {
    int c;
    if (op == 0)
        return ng_value_equal(a, b);
    if (op == 2)
        return !ng_value_equal(a, b);
    if (!ng_compare_values(a, b, &c))
        return 0;
    if (op == 3)
        return c < 0;
    if (op == 4)
        return c <= 0;
    if (op == 5)
        return c > 0;
    if (op == 6)
        return c >= 0;
    return 0;
}
static int ng_query_node_term_matches(const node_i* n, const ng_query_term* t, ng_symbol_id key) {
    const prop* p;
    size_t i;
    ng_value idv, v;
    if (!n)
        return 0;
    if (t->is_id) {
        idv.type = NG_VALUE_INT64;
        idv.length = 0;
        idv.as.integer = (int64_t)n->id;
        if (t->op == 7)
            return 0;
        if (t->op == 8)
            return 1;
        if (t->op == 1) {
            for (i = 0; i < (size_t)t->value_count; i++)
                if (ng_query_resolve_compare(&idv, &t->values[i], 0))
                    return 1;
            return 0;
        }
        return ng_query_resolve_compare(&idv, &t->value, t->op);
    }
    if (!key)
        return t->op == 7;
    if (!key)
        return 0;
    p = findprop(n->p, n->np, key);
    if (!p)
        return t->op == 7;
    if (t->op == 7)
        return p->v.type == NG_VALUE_NULL;
    if (t->op == 8)
        return p->v.type != NG_VALUE_NULL;
    if (t->op == 1) {
        for (i = 0; i < (size_t)t->value_count; i++) {
            if (ng_query_resolve_value(&t->values[i], &v) != NG_OK) {
                ng_query_parameter_error = 1;
                return 0;
            }
            if (ng_value_equal(&p->v, &v))
                return 1;
        }
        return 0;
    }
    return ng_query_resolve_compare(&p->v, &t->value, t->op);
}
static int ng_query_rel_term_matches(const rel_i* r, const ng_query_term* t, ng_symbol_id key) {
    const prop* p;
    size_t i;
    ng_value idv, v;
    if (!r)
        return 0;
    if (t->is_id) {
        idv.type = NG_VALUE_INT64;
        idv.length = 0;
        idv.as.integer = (int64_t)r->id;
        if (t->op == 7)
            return 0;
        if (t->op == 8)
            return 1;
        if (t->op == 1) {
            for (i = 0; i < (size_t)t->value_count; i++)
                if (ng_query_resolve_compare(&idv, &t->values[i], 0))
                    return 1;
            return 0;
        }
        return ng_query_resolve_compare(&idv, &t->value, t->op);
    }
    if (!key)
        return t->op == 7;
    if (!key)
        return 0;
    p = findprop(r->p, r->np, key);
    if (!p)
        return t->op == 7;
    if (t->op == 7)
        return p->v.type == NG_VALUE_NULL;
    if (t->op == 8)
        return p->v.type != NG_VALUE_NULL;
    if (t->op == 1) {
        for (i = 0; i < (size_t)t->value_count; i++) {
            if (ng_query_resolve_value(&t->values[i], &v) != NG_OK) {
                ng_query_parameter_error = 1;
                return 0;
            }
            if (ng_value_equal(&p->v, &v))
                return 1;
        }
        return 0;
    }
    return ng_query_resolve_compare(&p->v, &t->value, t->op);
}
static int ng_query_expr_matches(const ng_query_plan* plan,
                                 int expr,
                                 const node_i* left,
                                 const rel_i* rel,
                                 const node_i* right,
                                 const ng_symbol_id* term_keys) {
    const ng_query_expr* e;
    const ng_query_term* t;
    const node_i* n;
    if (expr < 0 || expr >= plan->expr_count)
        return 0;
    e = &plan->exprs[expr];
    if (e->kind == 1)
        return ng_query_expr_matches(plan, e->left, left, rel, right, term_keys) &&
               ng_query_expr_matches(plan, e->right, left, rel, right, term_keys);
    if (e->kind == 2)
        return ng_query_expr_matches(plan, e->left, left, rel, right, term_keys) ||
               ng_query_expr_matches(plan, e->right, left, rel, right, term_keys);
    if (e->kind == 3)
        return !ng_query_expr_matches(plan, e->left, left, rel, right, term_keys);
    if (e->term < 0 || e->term >= plan->term_count)
        return 0;
    t = &plan->terms[e->term];
    if (t->var == 'r')
        return ng_query_rel_term_matches(rel, t, term_keys[e->term]);
    n = t->var == 'm' ? right : left;
    return ng_query_node_term_matches(n, t, term_keys[e->term]);
}
static int ng_query_where_matches(const ng_query_plan* plan,
                                  const node_i* left,
                                  const rel_i* rel,
                                  const node_i* right,
                                  const ng_symbol_id* term_keys) {
    if (!plan->has_where)
        return 1;
    return ng_query_expr_matches(plan, plan->where_root, left, rel, right, term_keys);
}
static ng_symbol_id ng_query_sort_key;
static const ng_query_plan* ng_query_sort_plan;
static int ng_query_node_order_compare(const void* a, const void* b) {
    const node_i *const *x = (const node_i* const*)a, *const *y = (const node_i* const*)b;
    ng_value ax, bx;
    const prop *ap, *bp;
    int c = 0;
    if (ng_query_sort_plan->order_is_id || !ng_query_sort_plan->order_is_property) {
        ax.type = NG_VALUE_INT64;
        ax.length = 0;
        ax.as.integer = (int64_t)(*x)->id;
        bx.type = NG_VALUE_INT64;
        bx.length = 0;
        bx.as.integer = (int64_t)(*y)->id;
        ng_compare_values(&ax, &bx, &c);
    } else {
        ap = findprop((*x)->p, (*x)->np, ng_query_sort_key);
        bp = findprop((*y)->p, (*y)->np, ng_query_sort_key);
        if (!ap && !bp)
            c = 0;
        else if (!ap)
            c = 1;
        else if (!bp)
            c = -1;
        else if (!ng_compare_values(&ap->v, &bp->v, &c))
            c = 0;
    }
    if (!c)
        c = (*x)->id > (*y)->id ? 1 : (*x)->id < (*y)->id ? -1 : 0;
    return ng_query_sort_plan->order_desc ? -c : c;
}
static size_t ng_node_position(const ng_graph* g, ng_node_id id) {
    size_t i;
    for (i = 0; i < g->nn; i++)
        if (g->no[i].id == id)
            return i;
    return SIZE_MAX;
}
static ng_status ng_query_print_generic(const ng_graph* g, const char* q, FILE* out, int* handled);
ng_status ng_query_nodes(const ng_graph* g, const char* q, ng_node_match_visitor visit, void* ctx) {
    ng_query_plan plan;
    ng_symbol_id left_label = 0, right_label = 0, rel_type = 0, term_keys[NG_QUERY_MAX_TERMS] = {0},
                 order_key = 0;
    ng_property rel_props[NG_QUERY_MAX_PROPS];
    size_t i, ti;
    uint64_t matched = 0, emitted = 0;
    ng_status s;
    if (!g || !q || !visit)
        return NG_INVALID_ARGUMENT;
    s = ng_query_parse(q, &plan);
    if (s != NG_OK)
        return s;
    if (plan.return_count != 1 || plan.return_var == 'r')
        return NG_PARSE_ERROR;
    if (plan.left_label[0]) {
        left_label = ng_symbol_id_by_text(g, plan.left_label);
        if (!left_label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    if (plan.rel_type[0]) {
        rel_type = ng_symbol_id_by_text(g, plan.rel_type);
        if (!rel_type)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    for (ti = 0; ti < (size_t)plan.rel_prop_count; ti++) {
        rel_props[ti].value = plan.rel_props[ti].value;
        rel_props[ti].key = ng_symbol_id_by_text(g, plan.rel_props[ti].key);
        if (!rel_props[ti].key)
            return NG_OK;
    }
    if (plan.has_order && (plan.has_relationship || plan.has_second_node))
        return NG_PARSE_ERROR;
    if (plan.has_order && plan.order_is_property && !plan.order_is_id) {
        order_key = ng_symbol_id_by_text(g, plan.order_key);
        if (!order_key)
            return NG_OK;
    }
    if (plan.has_second_node) {
        size_t j;
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], left_label))
                continue;
            for (j = 0; j < g->nn; j++) {
                ng_node_id outid;
                if (!ng_query_label_matches(&g->no[j], right_label) ||
                    !ng_query_where_matches(&plan, &g->no[i], NULL, &g->no[j], term_keys))
                    continue;
                if (matched++ < plan.skip)
                    continue;
                if (plan.has_limit && emitted >= plan.limit)
                    return NG_OK;
                outid = plan.return_var == 'm' ? g->no[j].id : g->no[i].id;
                emitted++;
                if (!visit(outid, ctx))
                    return NG_OK;
            }
        }
        return NG_OK;
    }
    if (!plan.has_relationship) {
        const node_i** rows = NULL;
        size_t count = 0, cap = 0;
        if (plan.has_order) {
            for (i = 0; i < g->nn; i++)
                if (ng_query_label_matches(&g->no[i], left_label) &&
                    ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys)) {
                    if (!grow((void**)&rows, &cap, count + 1, sizeof(*rows))) {
                        free(rows);
                        return NG_OOM;
                    }
                    rows[count++] = &g->no[i];
                }
            ng_query_sort_key = order_key;
            ng_query_sort_plan = &plan;
            qsort(rows, count, sizeof(*rows), ng_query_node_order_compare);
            for (i = 0; i < count; i++) {
                if (matched++ < plan.skip)
                    continue;
                if (plan.has_limit && emitted >= plan.limit)
                    break;
                emitted++;
                if (!visit(rows[i]->id, ctx))
                    break;
            }
            free(rows);
            return NG_OK;
        }
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], left_label) ||
                !ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys))
                continue;
            if (matched++ < plan.skip)
                continue;
            if (plan.has_limit && emitted >= plan.limit)
                break;
            emitted++;
            if (!visit(g->no[i].id, ctx))
                break;
        }
        return NG_OK;
    }
    for (i = 0; i < g->nn; i++) {
        ng_node_id* qids;
        uint32_t* depths;
        unsigned char* seen_depth;
        size_t head = 0, tail = 0, cap, j;
        if (plan.has_limit && emitted >= plan.limit)
            break;
        if (!ng_query_label_matches(&g->no[i], left_label))
            continue;
        cap = g->nn * (size_t)(plan.max_depth + 1);
        qids = malloc(cap * sizeof(*qids));
        depths = malloc(cap * sizeof(*depths));
        seen_depth = calloc(cap, 1);
        if (cap && (!qids || !depths || !seen_depth)) {
            free(qids);
            free(depths);
            free(seen_depth);
            return NG_OOM;
        }
        qids[tail] = g->no[i].id;
        depths[tail++] = 0;
        seen_depth[i * (size_t)(plan.max_depth + 1)] = 1;
        while (head < tail) {
            ng_node_id cur = qids[head];
            uint32_t depth = depths[head++];
            if (depth == plan.max_depth)
                continue;
            for (j = 0; j < g->nr; j++) {
                node_i* right;
                size_t pos, slot;
                uint32_t nd;
                if ((rel_type && g->re[j].type != rel_type) ||
                    !ng_query_rel_matches_props(&g->re[j], rel_props, (size_t)plan.rel_prop_count))
                    continue;
                if (plan.rel_dir > 0) {
                    if (g->re[j].src != cur)
                        continue;
                    right = node((ng_graph*)g, g->re[j].dst);
                } else if (plan.rel_dir < 0) {
                    if (g->re[j].dst != cur)
                        continue;
                    right = node((ng_graph*)g, g->re[j].src);
                } else {
                    if (g->re[j].src == cur)
                        right = node((ng_graph*)g, g->re[j].dst);
                    else if (g->re[j].dst == cur)
                        right = node((ng_graph*)g, g->re[j].src);
                    else
                        continue;
                }
                if (!right)
                    continue;
                nd = depth + 1;
                if (nd >= plan.min_depth && ng_query_label_matches(right, right_label) &&
                    ng_query_where_matches(&plan, &g->no[i], &g->re[j], right, term_keys)) {
                    ng_node_id out = plan.return_var == 'm' ? right->id : g->no[i].id;
                    if (matched++ < plan.skip) {
                    } else {
                        if (plan.has_limit && emitted >= plan.limit) {
                            free(qids);
                            free(depths);
                            free(seen_depth);
                            return NG_OK;
                        }
                        emitted++;
                        if (!visit(out, ctx)) {
                            free(qids);
                            free(depths);
                            free(seen_depth);
                            return NG_OK;
                        }
                    }
                }
                if (nd < plan.max_depth) {
                    pos = ng_node_position(g, right->id);
                    if (pos != SIZE_MAX) {
                        slot = pos * (size_t)(plan.max_depth + 1) + nd;
                        if (!seen_depth[slot]) {
                            seen_depth[slot] = 1;
                            qids[tail] = right->id;
                            depths[tail++] = nd;
                        }
                    }
                }
            }
        }
        free(qids);
        free(depths);
        free(seen_depth);
    }
    return NG_OK;
}
ng_status ng_query_explain(const char* q, char* b, size_t c) {
    ng_query_plan plan;
    ng_status s;
    int n;
    if (!q || !b || !c)
        return NG_INVALID_ARGUMENT;
    s = ng_query_parse(q, &plan);
    if (s != NG_OK)
        return s;
    if (plan.has_relationship)
        n = snprintf(b,
                     c,
                     "Expand%s%s%s%s depth=%u..%u%s%s%s%s%s%s return=%c%s",
                     plan.left_label[0] ? " left_label=" : "",
                     plan.left_label[0] ? plan.left_label : "",
                     plan.rel_type[0] ? " type=" : "",
                     plan.rel_type[0] ? plan.rel_type : "",
                     (unsigned)plan.min_depth,
                     (unsigned)plan.max_depth,
                     plan.right_label[0] ? " right_label=" : "",
                     plan.right_label[0] ? plan.right_label : "",
                     plan.has_where ? " filter=" : "",
                     plan.has_where ? plan.key : "",
                     plan.return_is_property ? " project=." : "",
                     plan.return_is_property ? plan.return_key : "",
                     plan.return_var,
                     plan.has_limit ? " limit" : "");
    else
        n = snprintf(b,
                     c,
                     "NodeScan%s%s%s%s%s%s%s%s",
                     plan.left_label[0] ? " label=" : "",
                     plan.left_label[0] ? plan.left_label : "",
                     plan.has_where ? " filter=n." : "",
                     plan.has_where ? plan.key : "",
                     plan.has_where ? " = <literal>" : "",
                     plan.return_is_property ? " project=." : "",
                     plan.return_is_property ? plan.return_key : "",
                     plan.has_limit ? " limit" : "");
    if (n < 0)
        return NG_IO_ERROR;
    if ((size_t)n >= c)
        return NG_LIMIT;
    return NG_OK;
}

static int ng_print_value(FILE* out, const ng_value* v) {
    size_t i;
    if (v->type == NG_VALUE_NULL)
        return fprintf(out, "null") >= 0;
    if (v->type == NG_VALUE_BOOL)
        return fprintf(out, "%s", v->as.boolean ? "true" : "false") >= 0;
    if (v->type == NG_VALUE_INT64)
        return fprintf(out, "%lld", (long long)v->as.integer) >= 0;
    if (v->type == NG_VALUE_DOUBLE)
        return fprintf(out, "%.17g", v->as.real) >= 0;
    if (v->type == NG_VALUE_STRING)
        return fwrite(v->as.string, 1, v->length, out) == v->length;
    if (v->type == NG_VALUE_BYTES) {
        if (fputs("0x", out) < 0)
            return 0;
        for (i = 0; i < v->length; i++)
            if (fprintf(out, "%02x", v->as.bytes[i]) < 0)
                return 0;
        return 1;
    }
    if (v->type == NG_VALUE_LIST) {
        if (fputc('[', out) == EOF)
            return 0;
        if (v->as.list)
            for (i = 0; i < v->as.list->count; i++) {
                if (i && fputs(", ", out) < 0)
                    return 0;
                if (!ng_print_value(out, &v->as.list->items[i]))
                    return 0;
            }
        return fputc(']', out) != EOF;
    }
    if (v->type == NG_VALUE_MAP) {
        if (fputc('{', out) == EOF)
            return 0;
        if (v->as.map)
            for (i = 0; i < v->as.map->count; i++) {
                if (i && fputs(", ", out) < 0)
                    return 0;
                if (fprintf(out, "%s: ", v->as.map->entries[i].key) < 0 ||
                    !ng_print_value(out, &v->as.map->entries[i].value))
                    return 0;
            }
        return fputc('}', out) != EOF;
    }
    return 0;
}
static ng_status ng_query_print_node_item(
    const node_i* n, const ng_query_plan* plan, size_t item, ng_symbol_id key, FILE* out) {
    const prop* p;
    if (!n)
        return NG_INVALID_ARGUMENT;
    if (plan->return_is_ids[item] || !plan->return_is_properties[item])
        return fprintf(out, "%llu", (unsigned long long)n->id) < 0 ? NG_IO_ERROR : NG_OK;
    p = findprop(n->p, n->np, key);
    if (!p)
        return NG_NOT_FOUND;
    if (!ng_print_value(out, &p->v))
        return NG_IO_ERROR;
    return NG_OK;
}
static ng_status ng_query_print_rel_item(
    const rel_i* r, const ng_query_plan* plan, size_t item, ng_symbol_id key, FILE* out) {
    const prop* p;
    if (!r)
        return NG_INVALID_ARGUMENT;
    if (plan->return_is_ids[item] || !plan->return_is_properties[item])
        return fprintf(out, "%llu", (unsigned long long)r->id) < 0 ? NG_IO_ERROR : NG_OK;
    p = findprop(r->p, r->np, key);
    if (!p)
        return NG_NOT_FOUND;
    if (!ng_print_value(out, &p->v))
        return NG_IO_ERROR;
    return NG_OK;
}
static ng_status ng_query_print_row(const node_i* left,
                                    const rel_i* rel,
                                    const node_i* right,
                                    const ng_query_plan* plan,
                                    const ng_symbol_id* keys,
                                    FILE* out) {
    size_t i;
    ng_status s;
    for (i = 0; i < (size_t)plan->return_count; i++) {
        if (i && fputc('\t', out) == EOF)
            return NG_IO_ERROR;
        if (plan->return_vars[i] == 'r')
            s = ng_query_print_rel_item(rel, plan, i, keys[i], out);
        else {
            s = ng_query_print_node_item(
                plan->return_vars[i] == 'm' ? right : left, plan, i, keys[i], out);
        }
        if (s != NG_OK)
            return s;
    }
    return fputc('\n', out) == EOF ? NG_IO_ERROR : NG_OK;
}
static ng_status ng_query_print_active(const ng_graph* g, const char* q, FILE* out) {
    ng_query_plan plan;
    ng_symbol_id left_label = 0, right_label = 0, rel_type = 0, return_keys[8] = {0},
                 term_keys[NG_QUERY_MAX_TERMS] = {0}, order_key = 0;
    ng_property rel_props[NG_QUERY_MAX_PROPS];
    size_t i, ri, ti;
    uint64_t matched = 0, emitted = 0;
    ng_status s;
    int handled = 0;
    if (!g || !q || !out)
        return NG_INVALID_ARGUMENT;
    s = ng_query_print_generic(g, q, out, &handled);
    if (handled)
        return s;
    s = ng_query_parse(q, &plan);
    if (s != NG_OK)
        return s;
    s = ng_query_capture_legacy_schema(&plan);
    if (s != NG_OK)
        return s;
    if (plan.left_label[0]) {
        left_label = ng_symbol_id_by_text(g, plan.left_label);
        if (!left_label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    if (plan.rel_type[0]) {
        rel_type = ng_symbol_id_by_text(g, plan.rel_type);
        if (!rel_type)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    for (ti = 0; ti < (size_t)plan.rel_prop_count; ti++) {
        rel_props[ti].value = plan.rel_props[ti].value;
        rel_props[ti].key = ng_symbol_id_by_text(g, plan.rel_props[ti].key);
        if (!rel_props[ti].key)
            return NG_OK;
    }
    for (ri = 0; ri < (size_t)plan.return_count; ri++)
        if (plan.return_is_properties[ri] && !plan.return_is_ids[ri]) {
            return_keys[ri] = ng_symbol_id_by_text(g, plan.return_keys[ri]);
            if (!return_keys[ri])
                return NG_OK;
        }
    if (plan.has_order && (plan.has_relationship || plan.has_second_node))
        return NG_PARSE_ERROR;
    if (plan.has_order && plan.order_is_property && !plan.order_is_id) {
        order_key = ng_symbol_id_by_text(g, plan.order_key);
        if (!order_key)
            return NG_OK;
    }
    if (plan.has_second_node) {
        size_t j;
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], left_label))
                continue;
            for (j = 0; j < g->nn; j++) {
                if (!ng_query_label_matches(&g->no[j], right_label) ||
                    !ng_query_where_matches(&plan, &g->no[i], NULL, &g->no[j], term_keys))
                    continue;
                if (ng_query_parameter_error)
                    return NG_NOT_FOUND;
                if (matched++ < plan.skip)
                    continue;
                if (plan.has_limit && emitted >= plan.limit)
                    return NG_OK;
                s = ng_query_print_row(&g->no[i], NULL, &g->no[j], &plan, return_keys, out);
                if (s != NG_OK)
                    return s;
                emitted++;
            }
        }
        return NG_OK;
    }
    if (!plan.has_relationship) {
        const node_i** rows = NULL;
        size_t count = 0, cap = 0;
        if (plan.has_order) {
            for (i = 0; i < g->nn; i++)
                if (ng_query_label_matches(&g->no[i], left_label) &&
                    ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys)) {
                    if (ng_query_parameter_error) {
                        free(rows);
                        return NG_NOT_FOUND;
                    }
                    if (!grow((void**)&rows, &cap, count + 1, sizeof(*rows))) {
                        free(rows);
                        return NG_OOM;
                    }
                    rows[count++] = &g->no[i];
                }
            ng_query_sort_key = order_key;
            ng_query_sort_plan = &plan;
            qsort(rows, count, sizeof(*rows), ng_query_node_order_compare);
            for (i = 0; i < count; i++) {
                if (matched++ < plan.skip)
                    continue;
                if (plan.has_limit && emitted >= plan.limit)
                    break;
                s = ng_query_print_row(rows[i], NULL, NULL, &plan, return_keys, out);
                if (s != NG_OK) {
                    free(rows);
                    return s;
                }
                emitted++;
            }
            free(rows);
            return NG_OK;
        }
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], left_label) ||
                !ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys)) {
                if (ng_query_parameter_error)
                    return NG_NOT_FOUND;
                continue;
            }
            if (matched++ < plan.skip)
                continue;
            if (plan.has_limit && emitted >= plan.limit)
                break;
            s = ng_query_print_row(&g->no[i], NULL, NULL, &plan, return_keys, out);
            if (s != NG_OK)
                return s;
            emitted++;
        }
        return NG_OK;
    }
    for (i = 0; i < g->nn; i++) {
        ng_node_id* qids;
        uint32_t* depths;
        unsigned char* seen_depth;
        size_t head = 0, tail = 0, cap, j;
        if (plan.has_limit && emitted >= plan.limit)
            break;
        if (!ng_query_label_matches(&g->no[i], left_label))
            continue;
        cap = g->nn * (size_t)(plan.max_depth + 1);
        qids = malloc(cap * sizeof(*qids));
        depths = malloc(cap * sizeof(*depths));
        seen_depth = calloc(cap, 1);
        if (cap && (!qids || !depths || !seen_depth)) {
            free(qids);
            free(depths);
            free(seen_depth);
            return NG_OOM;
        }
        qids[tail] = g->no[i].id;
        depths[tail++] = 0;
        seen_depth[i * (size_t)(plan.max_depth + 1)] = 1;
        while (head < tail) {
            ng_node_id cur = qids[head];
            uint32_t depth = depths[head++];
            if (depth == plan.max_depth)
                continue;
            for (j = 0; j < g->nr; j++) {
                node_i* right;
                size_t pos, slot;
                uint32_t nd;
                if ((rel_type && g->re[j].type != rel_type) ||
                    !ng_query_rel_matches_props(
                        &g->re[j], rel_props, (size_t)plan.rel_prop_count)) {
                    if (ng_query_parameter_error) {
                        free(qids);
                        free(depths);
                        free(seen_depth);
                        return NG_NOT_FOUND;
                    }
                    continue;
                }
                if (plan.rel_dir > 0) {
                    if (g->re[j].src != cur)
                        continue;
                    right = node((ng_graph*)g, g->re[j].dst);
                } else if (plan.rel_dir < 0) {
                    if (g->re[j].dst != cur)
                        continue;
                    right = node((ng_graph*)g, g->re[j].src);
                } else {
                    if (g->re[j].src == cur)
                        right = node((ng_graph*)g, g->re[j].dst);
                    else if (g->re[j].dst == cur)
                        right = node((ng_graph*)g, g->re[j].src);
                    else
                        continue;
                }
                if (!right)
                    continue;
                nd = depth + 1;
                if (nd >= plan.min_depth && ng_query_label_matches(right, right_label) &&
                    ng_query_where_matches(&plan, &g->no[i], &g->re[j], right, term_keys)) {
                    if (ng_query_parameter_error) {
                        free(qids);
                        free(depths);
                        free(seen_depth);
                        return NG_NOT_FOUND;
                    }
                    if (matched++ < plan.skip) {
                    } else {
                        if (plan.has_limit && emitted >= plan.limit) {
                            free(qids);
                            free(depths);
                            free(seen_depth);
                            return NG_OK;
                        }
                        s = ng_query_print_row(
                            &g->no[i], &g->re[j], right, &plan, return_keys, out);
                        if (s != NG_OK) {
                            free(qids);
                            free(depths);
                            free(seen_depth);
                            return s;
                        }
                        emitted++;
                    }
                }
                if (nd < plan.max_depth) {
                    pos = ng_node_position(g, right->id);
                    if (pos != SIZE_MAX) {
                        slot = pos * (size_t)(plan.max_depth + 1) + nd;
                        if (!seen_depth[slot]) {
                            seen_depth[slot] = 1;
                            qids[tail] = right->id;
                            depths[tail++] = nd;
                        }
                    }
                }
            }
        }
        free(qids);
        free(depths);
        free(seen_depth);
    }
    return NG_OK;
}
static int ng_parameter_name_valid(const char* s) {
    if (!s || !ng_ident_char((unsigned char)*s) || isdigit((unsigned char)*s))
        return 0;
    while (*s) {
        if (!ng_ident_char((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}
static ng_status ng_query_parameters_valid(const ng_parameter* p, size_t n) {
    size_t i;
    if (n && !p)
        return NG_INVALID_ARGUMENT;
    for (i = 0; i < n; i++)
        if (!ng_parameter_name_valid(p[i].name) || !ng_valid_value(&p[i].value))
            return NG_INVALID_ARGUMENT;
    return NG_OK;
}
static ng_status ng_query_parameters_cover_query(const char* q, const ng_parameter* p, size_t n) {
    size_t i;
    if (!q)
        return NG_INVALID_ARGUMENT;
    for (; *q; q++) {
        if (*q == '"') {
            q++;
            while (*q && *q != '"')
                q++;
            if (!*q)
                return NG_PARSE_ERROR;
            continue;
        }
        if (*q == '$') {
            const char* s = ++q;
            size_t len;
            if (!ng_ident_char((unsigned char)*q) || isdigit((unsigned char)*q))
                return NG_PARSE_ERROR;
            while (ng_ident_char((unsigned char)*q))
                q++;
            len = (size_t)(q - s);
            for (i = 0; i < n; i++)
                if (ng_param_name_equal(p[i].name, s, len))
                    break;
            if (i == n)
                return NG_NOT_FOUND;
            q--;
        }
    }
    return NG_OK;
}
ng_status ng_query_print_params(
    const ng_graph* g, const char* q, const ng_parameter* p, size_t n, FILE* out) {
    const ng_parameter* oldp = ng_query_parameters;
    size_t oldn = ng_query_parameter_count;
    int olde = ng_query_parameter_error;
    ng_status s = ng_query_parameters_valid(p, n);
    if (s != NG_OK)
        return s;
    s = ng_query_parameters_cover_query(q, p, n);
    if (s != NG_OK)
        return s;
    ng_query_parameters = p;
    ng_query_parameter_count = n;
    ng_query_parameter_error = 0;
    s = ng_query_print_active(g, q, out);
    if (s == NG_OK && ng_query_parameter_error)
        s = NG_NOT_FOUND;
    ng_query_parameters = oldp;
    ng_query_parameter_count = oldn;
    ng_query_parameter_error = olde;
    return s;
}
ng_status ng_query_print(const ng_graph* g, const char* q, FILE* out) {
    return ng_query_print_params(g, q, NULL, 0, out);
}
static ng_status ng_query_parse_prop_map(const char** pp, ng_query_prop* props, size_t* count) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    if (*p != '{') {
        *count = 0;
        return NG_OK;
    }
    p = ng_skip_ws(p + 1);
    *count = 0;
    if (*p == '}') {
        *pp = p + 1;
        return NG_OK;
    }
    for (;;) {
        if (*count >= NG_QUERY_MAX_PROPS)
            return NG_PARSE_ERROR;
        s = p;
        if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
            return NG_PARSE_ERROR;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(props[*count].key))
            return NG_PARSE_ERROR;
        memcpy(props[*count].key, s, n);
        props[*count].key[n] = 0;
        p = ng_skip_ws(p);
        if (*p != ':')
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (ng_query_parse_value(&p, &props[*count].value) != NG_OK)
            return NG_PARSE_ERROR;
        (*count)++;
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
    }
    if (*p != '}')
        return NG_PARSE_ERROR;
    *pp = p + 1;
    return NG_OK;
}
#define NG_CY_MAX_VARS 24
#define NG_CY_MAX_MATCHES 8
#define NG_CY_MAX_NODES 8
#define NG_CY_MAX_RELS 7
#define NG_CY_MAX_PATH_LENGTH 64
#define NG_CY_MAX_RETURNS 8
#define NG_CY_MAX_ROWS 4096
#define NG_CY_MAX_SCALARS 32
typedef struct {
    char name[64];
    int kind, in_scope;
} ng_cy_var;
typedef struct {
    char var[64], label[128];
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    int prop_scalars[NG_QUERY_MAX_PROPS];
    size_t prop_count;
    int var_index;
} ng_cy_node_pat;
typedef struct {
    char var[64], type[128];
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    int prop_scalars[NG_QUERY_MAX_PROPS];
    size_t prop_count;
    int var_index, dir, has_var_length;
    uint32_t min_depth, max_depth;
} ng_cy_rel_pat;
typedef struct {
    ng_cy_node_pat nodes[NG_CY_MAX_NODES];
    ng_cy_rel_pat rels[NG_CY_MAX_RELS];
    size_t node_count, rel_count;
    int path_var_index;
} ng_cy_match;
typedef struct {
    ng_id nodes[NG_CY_MAX_PATH_LENGTH + 1];
    ng_id relationships[NG_CY_MAX_PATH_LENGTH];
    size_t node_count, relationship_count;
} ng_cy_path;
typedef struct {
    int kind, left, right, var_index, is_property, is_id, list_count, direct_binding,
        list_items[NG_QUERY_MAX_LIST_VALUES];
    int slice_start, slice_end;
    int comprehension_var, comprehension_source, comprehension_filter, comprehension_value;
    int case_operand, case_simple;
    char key[128];
    ng_value value;
    size_t map_count;
    char map_keys[NG_QUERY_MAX_PROPS][128];
    int map_items[NG_QUERY_MAX_PROPS];
} ng_cy_scalar;
typedef struct {
    int var_index, is_property, is_id, scalar_index, out_var_index, out_kind, aggregate,
        aggregate_distinct, count_star;
    char key[128], out_name[64], source[128];
} ng_cy_projection;
typedef struct {
    int scalar_index, desc, proj_index;
} ng_cy_order;
typedef struct {
    int var_index, is_id, op, value_count;
    char key[128];
    ng_value value, values[NG_QUERY_MAX_LIST_VALUES];
} ng_cy_term;
typedef struct {
    int kind, left, right, term;
} ng_cy_expr;
typedef struct {
    ng_cy_var vars[NG_CY_MAX_VARS];
    size_t var_count;
    ng_cy_match matches[NG_CY_MAX_MATCHES];
    size_t match_count;
    ng_cy_projection returns[NG_CY_MAX_RETURNS];
    size_t return_count;
    ng_cy_term terms[NG_QUERY_MAX_TERMS];
    ng_cy_expr exprs[NG_QUERY_MAX_EXPR];
    ng_cy_scalar scalars[NG_CY_MAX_SCALARS];
    int term_count, expr_count, scalar_count, where_root, has_where, has_skip, has_limit, distinct,
        create_mode;
    char merge_on_create[4096], merge_on_match[4096];
    uint64_t skip, limit;
} ng_cy_query;
typedef struct {
    ng_value values[NG_CY_MAX_RETURNS];
} ng_cy_result_key;
typedef struct {
    int kind;
    ng_id id;
    ng_value value;
    void* pointer;
} ng_cy_binding;
typedef struct {
    ng_cy_binding values[NG_CY_MAX_VARS];
} ng_cy_row;
typedef struct {
    ng_cy_result_key key;
    ng_cy_row row;
} ng_cy_projected_row;
typedef struct {
    int valid;
    size_t count;
    char names[NG_CY_MAX_RETURNS][128];
    ng_value_type types[NG_CY_MAX_RETURNS];
    int type_known[NG_CY_MAX_RETURNS];
} ng_query_schema;
static ng_query_schema* ng_query_active_schema;
static ng_status ng_cy_parse_create_pattern(const char** pp, ng_cy_query* out);
static ng_status
ng_cy_execute_create_match(ng_graph* g, ng_cy_query* q, const ng_cy_match* m, ng_cy_row* row);
static ng_status ng_cy_parse_scalar_add(const char** pp, ng_cy_query* q, int* out);
static ng_status ng_cy_eval_scalar(
    const ng_graph* g, const ng_cy_query* q, const ng_cy_row* row, int index, ng_value* out);
static int ng_cy_scalar_temporary(const ng_cy_query* q, int index) {
    int kind;
    if (index < 0 || index >= q->scalar_count)
        return 0;
    kind = q->scalars[index].kind;
    return kind == 2 || kind == 7 || kind == 8 || kind == 9 || kind == 10 || kind == 11 ||
           kind == 16 || kind == 17 || kind == 19 || kind == 20 || kind == 21 || kind == 22 ||
           kind == 23 || kind == 24 || kind == 25 || kind == 26;
}
static ng_status ng_cy_props_to_symbols_row(ng_graph* g,
                                            ng_cy_query* q,
                                            const ng_query_prop* props,
                                            const int* scalars,
                                            size_t prop_count,
                                            const ng_cy_row* row,
                                            ng_property* out);
static ng_status ng_query_props_to_symbols(ng_graph* g,
                                           const ng_query_prop* props,
                                           size_t prop_count,
                                           ng_property* out);
static int
ng_query_node_matches_props(const node_i* n, const ng_property* props, size_t prop_count);
static ng_status ng_cy_parse_ident(const char** pp, char* out, size_t cap) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    s = p;
    while (ng_ident_char((unsigned char)*p))
        p++;
    n = (size_t)(p - s);
    if (!n || n >= cap)
        return NG_PARSE_ERROR;
    memcpy(out, s, n);
    out[n] = 0;
    *pp = p;
    return NG_OK;
}
static int ng_cy_var_index(ng_cy_query* q, const char* name, int kind, int create) {
    size_t i;
    if (!name || !*name)
        return -1;
    for (i = 0; i < q->var_count; i++)
        if (q->vars[i].in_scope && !strcmp(q->vars[i].name, name)) {
            return q->vars[i].kind == kind ? (int)i : -2;
        }
    if (!create)
        return -1;
    if (q->var_count >= NG_CY_MAX_VARS)
        return -2;
    strcpy(q->vars[q->var_count].name, name);
    q->vars[q->var_count].kind = kind;
    q->vars[q->var_count].in_scope = 1;
    return (int)q->var_count++;
}
static int ng_cy_var_lookup(ng_cy_query* q, const char* name) {
    size_t i;
    if (!name || !*name)
        return -1;
    for (i = 0; i < q->var_count; i++)
        if (q->vars[i].in_scope && !strcmp(q->vars[i].name, name))
            return (int)i;
    return -1;
}
static int ng_cy_expr_add(ng_cy_query* q, int kind, int left, int right, int term) {
    int i = q->expr_count;
    if (i >= NG_QUERY_MAX_EXPR)
        return -1;
    q->exprs[i].kind = kind;
    q->exprs[i].left = left;
    q->exprs[i].right = right;
    q->exprs[i].term = term;
    q->expr_count++;
    return i;
}
static ng_status ng_cy_add_inline_props(ng_cy_query* q,
                                        int var_index,
                                        const ng_query_prop* props,
                                        size_t prop_count) {
    size_t i;
    int root = q->where_root;
    for (i = 0; i < prop_count; i++) {
        ng_cy_term* t;
        int term, node;
        if (q->term_count >= NG_QUERY_MAX_TERMS)
            return NG_PARSE_ERROR;
        term = q->term_count;
        t = &q->terms[term];
        memset(t, 0, sizeof(*t));
        t->var_index = var_index;
        t->op = 0;
        t->value = props[i].value;
        strcpy(t->key, props[i].key);
        node = ng_cy_expr_add(q, 0, -1, -1, term);
        if (node < 0)
            return NG_PARSE_ERROR;
        q->term_count++;
        root = root < 0 ? node : ng_cy_expr_add(q, 1, root, node, -1);
        if (root < 0)
            return NG_PARSE_ERROR;
    }
    if (prop_count) {
        q->has_where = 1;
        q->where_root = root;
    }
    return NG_OK;
}
static ng_status ng_cy_parse_expr_map(
    const char** pp, ng_cy_query* q, ng_query_prop* props, int* scalars, size_t* count) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    int scalar;
    if (*p != '{') {
        *count = 0;
        return NG_OK;
    }
    p = ng_skip_ws(p + 1);
    *count = 0;
    while (*p != '}') {
        if (*count >= NG_QUERY_MAX_PROPS)
            return NG_PARSE_ERROR;
        s = p;
        if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
            return NG_PARSE_ERROR;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(props[*count].key))
            return NG_PARSE_ERROR;
        memcpy(props[*count].key, s, n);
        props[*count].key[n] = 0;
        p = ng_skip_ws(p);
        if (*p != ':')
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
            return NG_PARSE_ERROR;
        scalars[*count] = scalar;
        props[*count].value.type = NG_VALUE_NULL;
        props[*count].value.length = 0;
        if (q->scalars[scalar].kind == 0)
            props[*count].value = q->scalars[scalar].value;
        (*count)++;
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (*p == '}')
            return NG_PARSE_ERROR;
    }
    if (*p != '}')
        return NG_PARSE_ERROR;
    *pp = p + 1;
    return NG_OK;
}
static ng_status ng_cy_parse_node(const char** pp, ng_cy_query* q, ng_cy_node_pat* out) {
    const char* p = ng_skip_ws(*pp);
    char tmp[64];
    memset(out, 0, sizeof(*out));
    {
        size_t pi;
        for (pi = 0; pi < NG_QUERY_MAX_PROPS; pi++)
            out->prop_scalars[pi] = -1;
    }
    out->var_index = -1;
    if (*p != '(')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        if (ng_cy_parse_ident(&p, out->var, sizeof(out->var)) != NG_OK)
            return NG_PARSE_ERROR;
        out->var_index = ng_cy_var_index(q, out->var, 1, 1);
        if (out->var_index < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    if (*p == ':') {
        p++;
        if (ng_cy_parse_ident(&p, out->label, sizeof(out->label)) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        while (*p == ':') {
            p++;
            if (ng_cy_parse_ident(&p, tmp, sizeof(tmp)) != NG_OK)
                return NG_PARSE_ERROR;
            return NG_PARSE_ERROR;
        }
    }
    if (q->create_mode) {
        if (ng_cy_parse_expr_map(&p, q, out->props, out->prop_scalars, &out->prop_count) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (ng_query_parse_prop_map(&p, out->props, &out->prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    if (out->prop_count && out->var_index < 0) {
        snprintf(out->var, sizeof(out->var), "__anon_node_%u", (unsigned)q->var_count);
        out->var_index = ng_cy_var_index(q, out->var, 1, 1);
        if (out->var_index < 0)
            return NG_PARSE_ERROR;
    }
    if (!q->create_mode && out->var_index >= 0 &&
        ng_cy_add_inline_props(q, out->var_index, out->props, out->prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != ')')
        return NG_PARSE_ERROR;
    *pp = p + 1;
    return NG_OK;
}
static ng_status ng_cy_parse_rel(const char** pp, ng_cy_query* q, ng_cy_rel_pat* out) {
    const char* p = ng_skip_ws(*pp);
    uint64_t a, b;
    memset(out, 0, sizeof(*out));
    {
        size_t pi;
        for (pi = 0; pi < NG_QUERY_MAX_PROPS; pi++)
            out->prop_scalars[pi] = -1;
    }
    out->var_index = -1;
    out->dir = 1;
    out->min_depth = 1;
    out->max_depth = 1;
    if (*p == '<') {
        p++;
        if (*p != '-')
            return NG_PARSE_ERROR;
        out->dir = -1;
        p++;
    } else if (*p == '-')
        p++;
    else
        return NG_PARSE_ERROR;
    if (*p != '[')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        if (ng_cy_parse_ident(&p, out->var, sizeof(out->var)) != NG_OK)
            return NG_PARSE_ERROR;
        out->var_index = ng_cy_var_index(q, out->var, 2, 1);
        if (out->var_index < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    if (*p == ':') {
        p++;
        if (ng_cy_parse_ident(&p, out->type, sizeof(out->type)) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p == '|')
            return NG_PARSE_ERROR;
    }
    if (*p == '*') {
        out->has_var_length = 1;
        p = ng_skip_ws(p + 1);
        if (ng_query_parse_uint64(&p, &a) != NG_OK || a < 1 || a > 64)
            return NG_PARSE_ERROR;
        b = a;
        p = ng_skip_ws(p);
        if (p[0] == '.' && p[1] == '.') {
            p = ng_skip_ws(p + 2);
            if (ng_query_parse_uint64(&p, &b) != NG_OK || b < a || b > 64)
                return NG_PARSE_ERROR;
        }
        out->min_depth = (uint32_t)a;
        out->max_depth = (uint32_t)b;
        if (out->var_index >= 0)
            return NG_PARSE_ERROR;
    }
    if (q->create_mode) {
        if (ng_cy_parse_expr_map(&p, q, out->props, out->prop_scalars, &out->prop_count) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (ng_query_parse_prop_map(&p, out->props, &out->prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    if (out->prop_count && out->var_index < 0 && !out->has_var_length) {
        snprintf(out->var, sizeof(out->var), "__anon_rel_%u", (unsigned)q->var_count);
        out->var_index = ng_cy_var_index(q, out->var, 2, 1);
        if (out->var_index < 0)
            return NG_PARSE_ERROR;
    }
    if (!q->create_mode && out->var_index >= 0 &&
        ng_cy_add_inline_props(q, out->var_index, out->props, out->prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != ']')
        return NG_PARSE_ERROR;
    p++;
    if (out->dir < 0) {
        if (*p != '-')
            return NG_PARSE_ERROR;
        p++;
    } else {
        if (*p != '-')
            return NG_PARSE_ERROR;
        p++;
        if (*p == '>')
            p++;
        else
            out->dir = 0;
    }
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_parse_match_pattern(const char** pp, ng_cy_query* q, const char* kw) {
    const char* p = ng_skip_ws(*pp);
    ng_cy_match* m;
    size_t k = strlen(kw);
    if (q->match_count >= NG_CY_MAX_MATCHES)
        return NG_PARSE_ERROR;
    if (strncmp(p, kw, k) || !isspace((unsigned char)p[k]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + k);
    m = &q->matches[q->match_count];
    memset(m, 0, sizeof(*m));
    m->path_var_index = -1;
    {
        const char* path_end = p;
        char path_name[64];
        if (ng_cy_parse_ident(&path_end, path_name, sizeof(path_name)) == NG_OK) {
            path_end = ng_skip_ws(path_end);
            if (*path_end == '=') {
                m->path_var_index = ng_cy_var_index(q, path_name, 4, 1);
                if (m->path_var_index < 0)
                    return NG_PARSE_ERROR;
                p = ng_skip_ws(path_end + 1);
            }
        }
    }
    if (ng_cy_parse_node(&p, q, &m->nodes[m->node_count++]) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != '-' && *p != '<')
            break;
        if (m->rel_count >= NG_CY_MAX_RELS || m->node_count >= NG_CY_MAX_NODES)
            return NG_PARSE_ERROR;
        if (ng_cy_parse_rel(&p, q, &m->rels[m->rel_count++]) != NG_OK)
            return NG_PARSE_ERROR;
        if (ng_cy_parse_node(&p, q, &m->nodes[m->node_count++]) != NG_OK)
            return NG_PARSE_ERROR;
    }
    q->match_count++;
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_parse_match_clause(const char** pp, ng_cy_query* q) {
    return ng_cy_parse_match_pattern(pp, q, "MATCH");
}
static ng_status ng_cy_parse_term(const char** pp, ng_cy_query* q) {
    const char *p = ng_skip_ws(*pp), *s;
    char name[64];
    ng_cy_term* t;
    size_t n;
    if (q->term_count >= NG_QUERY_MAX_TERMS)
        return NG_PARSE_ERROR;
    t = &q->terms[q->term_count];
    memset(t, 0, sizeof(*t));
    if (!strncmp(p, "id(", 3)) {
        p = ng_skip_ws(p + 3);
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
            return NG_PARSE_ERROR;
        t->var_index = ng_cy_var_index(q, name, 1, 0);
        if (t->var_index < 0)
            t->var_index = ng_cy_var_index(q, name, 2, 0);
        if (t->var_index < 0 || *p != ')')
            return NG_PARSE_ERROR;
        p++;
        t->is_id = 1;
        strcpy(t->key, "id");
    } else {
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
            return NG_PARSE_ERROR;
        t->var_index = ng_cy_var_lookup(q, name);
        if (t->var_index < 0)
            return NG_PARSE_ERROR;
        if (q->vars[t->var_index].kind == 3) {
            if (*p == '.')
                return NG_PARSE_ERROR;
            t->key[0] = 0;
        } else if (*p == '.') {
            p++;
            s = p;
            while (ng_ident_char((unsigned char)*p))
                p++;
            n = (size_t)(p - s);
            if (!n || n >= sizeof(t->key))
                return NG_PARSE_ERROR;
            memcpy(t->key, s, n);
            t->key[n] = 0;
            if (!strcmp(t->key, "id"))
                t->is_id = 1;
        } else
            t->key[0] = 0;
    }
    p = ng_skip_ws(p);
    if (!strncmp(p, "<>", 2)) {
        t->op = 2;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, "<=", 2)) {
        t->op = 4;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, ">=", 2)) {
        t->op = 6;
        p += 2;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (*p == '<') {
        t->op = 3;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (*p == '>') {
        t->op = 5;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (*p == '=') {
        t->op = 0;
        p++;
        if (ng_query_parse_value(&p, &t->value) != NG_OK)
            return NG_PARSE_ERROR;
    } else if (!strncmp(p, "IS", 2) && isspace((unsigned char)p[2])) {
        p = ng_skip_ws(p + 2);
        if (!strncmp(p, "NOT", 3) && isspace((unsigned char)p[3])) {
            p = ng_skip_ws(p + 3);
            if (strncmp(p, "NULL", 4) || ng_ident_char((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            t->op = 8;
            p += 4;
        } else {
            if (strncmp(p, "NULL", 4) || ng_ident_char((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            t->op = 7;
            p += 4;
        }
    } else if (!strncmp(p, "IN", 2) && isspace((unsigned char)p[2])) {
        t->op = 1;
        p = ng_skip_ws(p + 2);
        if (*p != '[')
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (*p == ']')
            return NG_PARSE_ERROR;
        for (;;) {
            if (t->value_count >= NG_QUERY_MAX_LIST_VALUES)
                return NG_PARSE_ERROR;
            if (ng_query_parse_value(&p, &t->values[t->value_count]) != NG_OK)
                return NG_PARSE_ERROR;
            t->value_count++;
            p = ng_skip_ws(p);
            if (*p != ',')
                break;
            p = ng_skip_ws(p + 1);
        }
        if (*p != ']')
            return NG_PARSE_ERROR;
        p++;
        t->value = t->values[0];
    } else
        return NG_PARSE_ERROR;
    if (!t->key[0] && !t->is_id && q->vars[t->var_index].kind != 3 && t->op != 7 && t->op != 8)
        return NG_PARSE_ERROR;
    if (t->is_id && t->op != 7 && t->op != 8) {
        size_t i;
        if (t->op == 1) {
            for (i = 0; i < (size_t)t->value_count; i++)
                if (t->values[i].type != NG_VALUE_INT64 && t->values[i].type != NG_VALUE_PARAM)
                    return NG_PARSE_ERROR;
        } else if (t->value.type != NG_VALUE_INT64 && t->value.type != NG_VALUE_PARAM)
            return NG_PARSE_ERROR;
    }
    q->term_count++;
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static ng_status ng_cy_parse_or(const char** pp, ng_cy_query* q, int* out);
static ng_status ng_cy_parse_primary(const char** pp, ng_cy_query* q, int* out) {
    const char* p = ng_skip_ws(*pp);
    int node, term;
    if (!strncmp(p, "NOT", 3) && isspace((unsigned char)p[3])) {
        int child;
        p = ng_skip_ws(p + 3);
        if (ng_cy_parse_primary(&p, q, &child) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_expr_add(q, 3, child, -1, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        *pp = p;
        *out = node;
        return NG_OK;
    }
    if (*p == '(') {
        p = ng_skip_ws(p + 1);
        if (ng_cy_parse_or(&p, q, &node) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p != ')')
            return NG_PARSE_ERROR;
        *pp = ng_skip_ws(p + 1);
        *out = node;
        return NG_OK;
    }
    term = q->term_count;
    if (ng_cy_parse_term(&p, q) != NG_OK)
        return NG_PARSE_ERROR;
    node = ng_cy_expr_add(q, 0, -1, -1, term);
    if (node < 0)
        return NG_PARSE_ERROR;
    *pp = p;
    *out = node;
    return NG_OK;
}
static ng_status ng_cy_parse_and(const char** pp, ng_cy_query* q, int* out) {
    const char* p = *pp;
    int left, right, node;
    if (ng_cy_parse_primary(&p, q, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (strncmp(p, "AND", 3) || !isspace((unsigned char)p[3]))
            break;
        p = ng_skip_ws(p + 3);
        if (ng_cy_parse_primary(&p, q, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_expr_add(q, 1, left, right, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static ng_status ng_cy_parse_or(const char** pp, ng_cy_query* q, int* out) {
    const char* p = *pp;
    int left, right, node;
    if (ng_cy_parse_and(&p, q, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (strncmp(p, "OR", 2) || !isspace((unsigned char)p[2]))
            break;
        p = ng_skip_ws(p + 2);
        if (ng_cy_parse_and(&p, q, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_expr_add(q, 2, left, right, -1);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static int ng_cy_scalar_add(ng_cy_query* q,
                            int kind,
                            int left,
                            int right,
                            int var_index,
                            const char* key,
                            const ng_value* value) {
    int i = q->scalar_count;
    if (i >= NG_CY_MAX_SCALARS)
        return -1;
    memset(&q->scalars[i], 0, sizeof(q->scalars[i]));
    q->scalars[i].kind = kind;
    q->scalars[i].left = left;
    q->scalars[i].right = right;
    q->scalars[i].var_index = var_index;
    if (key)
        strcpy(q->scalars[i].key, key);
    if (value)
        q->scalars[i].value = *value;
    q->scalar_count++;
    return i;
}
static ng_status ng_cy_parse_scalar_add(const char** pp, ng_cy_query* q, int* out);
static ng_status ng_cy_parse_scalar_atom(const char** pp, ng_cy_query* q, int* out) {
    const char *p = ng_skip_ws(*pp), *s;
    char name[64], key[128];
    ng_value v;
    size_t n;
    int vi, direct_binding = 0;
    if (*p == '(') {
        p = ng_skip_ws(p + 1);
        if (ng_cy_parse_scalar_add(&p, q, out) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p != ')')
            return NG_PARSE_ERROR;
        *pp = ng_skip_ws(p + 1);
        return NG_OK;
    }
    if (*p == '[') {
        const char* look = ng_skip_ws(p + 1);
        char comprehension_name[64];
        const char* after_name = look;
        if (ng_cy_parse_ident(&after_name, comprehension_name, sizeof(comprehension_name)) == NG_OK &&
            ng_skip_ws(after_name)[0] == 'I' &&
            !strncmp(ng_skip_ws(after_name), "IN", 2) &&
            isspace((unsigned char)ng_skip_ws(after_name)[2])) {
            int comprehension = ng_cy_scalar_add(q, 11, -1, -1, -1, NULL, NULL);
            int variable, source, filter = -1, value;
            if (comprehension < 0 || ng_cy_var_lookup(q, comprehension_name) >= 0)
                return NG_PARSE_ERROR;
            variable = ng_cy_var_index(q, comprehension_name, 3, 1);
            if (variable < 0)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(after_name) + 2;
            if (ng_cy_parse_scalar_add(&p, q, &source) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p);
            if (!strncmp(p, "WHERE", 5) && isspace((unsigned char)p[5])) {
                p = ng_skip_ws(p + 5);
                if (ng_cy_parse_or(&p, q, &filter) != NG_OK)
                    return NG_PARSE_ERROR;
                p = ng_skip_ws(p);
            }
            if (*p != '|')
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 1);
            if (ng_cy_parse_scalar_add(&p, q, &value) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p);
            if (*p != ']')
                return NG_PARSE_ERROR;
            q->vars[variable].in_scope = 0;
            q->scalars[comprehension].comprehension_var = variable;
            q->scalars[comprehension].comprehension_source = source;
            q->scalars[comprehension].comprehension_filter = filter;
            q->scalars[comprehension].comprehension_value = value;
            *out = comprehension;
            *pp = ng_skip_ws(p + 1);
            return NG_OK;
        }
        int list = ng_cy_scalar_add(q, 7, -1, -1, -1, NULL, NULL);
        if (list < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (*p != ']')
            for (;;) {
                if (q->scalars[list].list_count >= NG_QUERY_MAX_LIST_VALUES)
                    return NG_PARSE_ERROR;
                if (ng_cy_parse_scalar_add(
                        &p, q, &q->scalars[list].list_items[q->scalars[list].list_count]) != NG_OK)
                    return NG_PARSE_ERROR;
                q->scalars[list].list_count++;
                p = ng_skip_ws(p);
                if (*p != ',')
                    break;
                p = ng_skip_ws(p + 1);
            }
        if (*p != ']')
            return NG_PARSE_ERROR;
        *out = list;
        *pp = ng_skip_ws(p + 1);
        return NG_OK;
    }
    if (*p == '{') {
        int map = ng_cy_scalar_add(q, 8, -1, -1, -1, NULL, NULL);
        if (map < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
        if (*p != '}')
            for (;;) {
                const char* key_start = p;
                size_t key_length;
                int item;
                if (q->scalars[map].map_count >= NG_QUERY_MAX_PROPS)
                    return NG_PARSE_ERROR;
                if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
                    return NG_PARSE_ERROR;
                while (ng_ident_char((unsigned char)*p))
                    p++;
                key_length = (size_t)(p - key_start);
                if (!key_length || key_length >= sizeof(q->scalars[map].map_keys[0]))
                    return NG_PARSE_ERROR;
                memcpy(q->scalars[map].map_keys[q->scalars[map].map_count], key_start, key_length);
                q->scalars[map].map_keys[q->scalars[map].map_count][key_length] = 0;
                p = ng_skip_ws(p);
                if (*p != ':')
                    return NG_PARSE_ERROR;
                p = ng_skip_ws(p + 1);
                if (ng_cy_parse_scalar_add(&p, q, &item) != NG_OK)
                    return NG_PARSE_ERROR;
                q->scalars[map].map_items[q->scalars[map].map_count++] = item;
                p = ng_skip_ws(p);
                if (*p != ',')
                    break;
                p = ng_skip_ws(p + 1);
                if (*p == '}')
                    return NG_PARSE_ERROR;
            }
        if (*p != '}')
            return NG_PARSE_ERROR;
        *out = map;
        *pp = ng_skip_ws(p + 1);
        return NG_OK;
    }
    if (!strncmp(p, "CASE", 4) && isspace((unsigned char)p[4])) {
        int expression = ng_cy_scalar_add(q, 20, -1, -1, -1, NULL, NULL);
        int condition, value;
        if (expression < 0)
            return NG_PARSE_ERROR;
        q->scalars[expression].case_operand = -1;
        p = ng_skip_ws(p + 4);
        if (strncmp(p, "WHEN", 4) || !isspace((unsigned char)p[4])) {
            if (ng_cy_parse_scalar_add(&p, q, &condition) != NG_OK)
                return NG_PARSE_ERROR;
            q->scalars[expression].case_operand = condition;
            q->scalars[expression].case_simple = 1;
            p = ng_skip_ws(p);
        }
        while (!strncmp(p, "WHEN", 4) && isspace((unsigned char)p[4])) {
            p = ng_skip_ws(p + 4);
            if (q->scalars[expression].case_simple) {
                if (ng_cy_parse_scalar_add(&p, q, &condition) != NG_OK)
                    return NG_PARSE_ERROR;
            } else if (ng_cy_parse_or(&p, q, &condition) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p);
            if (strncmp(p, "THEN", 4) || !isspace((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 4);
            if (ng_cy_parse_scalar_add(&p, q, &value) != NG_OK ||
                q->scalars[expression].list_count + 2 > NG_QUERY_MAX_LIST_VALUES)
                return NG_PARSE_ERROR;
            q->scalars[expression].list_items[q->scalars[expression].list_count++] = condition;
            q->scalars[expression].list_items[q->scalars[expression].list_count++] = value;
            p = ng_skip_ws(p);
        }
        if (!q->scalars[expression].list_count)
            return NG_PARSE_ERROR;
        q->scalars[expression].slice_end = -1;
        if (!strncmp(p, "ELSE", 4) && isspace((unsigned char)p[4])) {
            p = ng_skip_ws(p + 4);
            if (ng_cy_parse_scalar_add(&p, q, &value) != NG_OK)
                return NG_PARSE_ERROR;
            q->scalars[expression].slice_end = value;
            p = ng_skip_ws(p);
        }
        if (strncmp(p, "END", 3) || (ng_ident_char((unsigned char)p[3])))
            return NG_PARSE_ERROR;
        *out = expression;
        *pp = ng_skip_ws(p + 3);
        return NG_OK;
    }
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        const char* function_end = p;
        char function[32];
        int function_kind = 0, argument, arguments[NG_QUERY_MAX_LIST_VALUES];
        size_t argument_count = 0;
        if (ng_cy_parse_ident(&function_end, function, sizeof(function)) == NG_OK) {
            function_end = ng_skip_ws(function_end);
            if (*function_end == '(') {
                if (!strcmp(function, "size"))
                    function_kind = 13;
                else if (!strcmp(function, "head"))
                    function_kind = 14;
                else if (!strcmp(function, "last"))
                    function_kind = 15;
                else if (!strcmp(function, "tail"))
                    function_kind = 16;
                else if (!strcmp(function, "reverse"))
                    function_kind = 17;
                else if (!strcmp(function, "toString"))
                    function_kind = 18;
                else if (!strcmp(function, "coalesce"))
                    function_kind = 19;
                else if (!strcmp(function, "nodes"))
                    function_kind = 21;
                else if (!strcmp(function, "relationships"))
                    function_kind = 22;
                else if (!strcmp(function, "toLower"))
                    function_kind = 23;
                else if (!strcmp(function, "toUpper"))
                    function_kind = 24;
                else if (!strcmp(function, "trim"))
                    function_kind = 25;
                else if (!strcmp(function, "abs"))
                    function_kind = 26;
                if (!function_kind && strcmp(function, "id"))
                    return NG_PARSE_ERROR;
                if (!function_kind)
                    goto scalar_function_done;
                p = ng_skip_ws(function_end + 1);
                if (*p != ')') {
                    for (;;) {
                        if (argument_count >= NG_QUERY_MAX_LIST_VALUES ||
                            ng_cy_parse_scalar_add(&p, q, &argument) != NG_OK)
                            return NG_PARSE_ERROR;
                        arguments[argument_count++] = argument;
                        p = ng_skip_ws(p);
                        if (*p != ',')
                            break;
                        p = ng_skip_ws(p + 1);
                    }
                }
                if (*p != ')')
                    return NG_PARSE_ERROR;
                *out = ng_cy_scalar_add(q, function_kind, -1, -1, -1, NULL, NULL);
                if (*out < 0)
                    return NG_PARSE_ERROR;
                q->scalars[*out].list_count = argument_count;
                memcpy(q->scalars[*out].list_items,
                       arguments,
                       argument_count * sizeof(arguments[0]));
                p = ng_skip_ws(p + 1);
                *pp = p;
                return NG_OK;
            }
        }
    }
scalar_function_done:
    if (!strncmp(p, "id(", 3)) {
        p = ng_skip_ws(p + 3);
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
            return NG_PARSE_ERROR;
        vi = ng_cy_var_index(q, name, 1, 0);
        if (vi < 0)
            vi = ng_cy_var_index(q, name, 2, 0);
        if (vi < 0 || *p != ')')
            return NG_PARSE_ERROR;
        p++;
        *out = ng_cy_scalar_add(q, 1, -1, -1, vi, "id", NULL);
        if (*out < 0)
            return NG_PARSE_ERROR;
        *pp = ng_skip_ws(p);
        return NG_OK;
    }
    if (*p == '$' || *p == '"' || *p == '-' || isdigit((unsigned char)*p) ||
        !strncmp(p, "true", 4) || !strncmp(p, "false", 5) || !strncmp(p, "null", 4)) {
        if (ng_query_parse_value(&p, &v) != NG_OK)
            return NG_PARSE_ERROR;
        *out = ng_cy_scalar_add(q, 0, -1, -1, -1, NULL, &v);
        if (*out < 0)
            return NG_PARSE_ERROR;
        *pp = ng_skip_ws(p);
        return NG_OK;
    }
    if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
        return NG_PARSE_ERROR;
    vi = ng_cy_var_lookup(q, name);
    if (vi < 0)
        return NG_PARSE_ERROR;
    key[0] = 0;
    if (*p == '.') {
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= sizeof(key))
            return NG_PARSE_ERROR;
        memcpy(key, s, n);
        key[n] = 0;
    } else {
        strcpy(key, (q->vars[vi].kind == 3 || q->vars[vi].kind == 4) ? "" : "id");
        direct_binding = q->vars[vi].kind == 1 || q->vars[vi].kind == 2;
    }
    *out = ng_cy_scalar_add(q, 1, -1, -1, vi, key, NULL);
    if (*out < 0)
        return NG_PARSE_ERROR;
    q->scalars[*out].direct_binding = direct_binding;
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static ng_status ng_cy_parse_scalar_primary(const char** pp, ng_cy_query* q, int* out) {
    const char* p;
    int base;
    if (ng_cy_parse_scalar_atom(pp, q, &base) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(*pp);
    while (*p == '[') {
        int start = -1, end = -1, index = -1, slice;
        p = ng_skip_ws(p + 1);
        if (*p != '.' || p[1] != '.') {
            if (ng_cy_parse_scalar_add(&p, q, &start) != NG_OK)
                return NG_PARSE_ERROR;
        }
        p = ng_skip_ws(p);
        if (p[0] == '.' && p[1] == '.') {
            p = ng_skip_ws(p + 2);
            if (*p != ']') {
                if (ng_cy_parse_scalar_add(&p, q, &end) != NG_OK)
                    return NG_PARSE_ERROR;
            }
            p = ng_skip_ws(p);
            slice = ng_cy_scalar_add(q, 10, base, -1, -1, NULL, NULL);
            if (slice < 0)
                return NG_PARSE_ERROR;
            q->scalars[slice].slice_start = start;
            q->scalars[slice].slice_end = end;
            base = slice;
        } else {
            if (start < 0 || *p != ']')
                return NG_PARSE_ERROR;
            index = ng_cy_scalar_add(q, 9, base, start, -1, NULL, NULL);
            if (index < 0)
                return NG_PARSE_ERROR;
            base = index;
        }
        if (*p != ']')
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p + 1);
    }
    *out = base;
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_parse_scalar_unary(const char** pp, ng_cy_query* q, int* out) {
    const char* p = ng_skip_ws(*pp);
    int child, node;
    if (*p == '-') {
        p++;
        if (ng_cy_parse_scalar_unary(&p, q, &child) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_scalar_add(q, 6, child, -1, -1, NULL, NULL);
        if (node < 0)
            return NG_PARSE_ERROR;
        *pp = p;
        *out = node;
        return NG_OK;
    }
    return ng_cy_parse_scalar_primary(pp, q, out);
}
static ng_status ng_cy_parse_scalar_mul(const char** pp, ng_cy_query* q, int* out) {
    const char* p = *pp;
    int left, right, node, kind;
    if (ng_cy_parse_scalar_unary(&p, q, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != '*' && *p != '/')
            break;
        kind = *p == '*' ? 4 : 5;
        p++;
        if (ng_cy_parse_scalar_unary(&p, q, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_scalar_add(q, kind, left, right, -1, NULL, NULL);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static ng_status ng_cy_parse_scalar_add(const char** pp, ng_cy_query* q, int* out) {
    const char* p = *pp;
    int left, right, node, kind;
    if (ng_cy_parse_scalar_mul(&p, q, &left) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != '+' && *p != '-')
            break;
        kind = *p == '+' ? 2 : 3;
        p++;
        if (ng_cy_parse_scalar_mul(&p, q, &right) != NG_OK)
            return NG_PARSE_ERROR;
        node = ng_cy_scalar_add(q, kind, left, right, -1, NULL, NULL);
        if (node < 0)
            return NG_PARSE_ERROR;
        left = node;
    }
    *pp = p;
    *out = left;
    return NG_OK;
}
static ng_status ng_cy_parse_projection_list(const char** pp,
                                             ng_cy_query* q,
                                             const char* keyword,
                                             ng_cy_projection* projs,
                                             size_t* count,
                                             int* distinct,
                                             int for_with);
static ng_status ng_cy_parse_return(const char** pp, ng_cy_query* q) {
    return ng_cy_parse_projection_list(
        pp, q, "RETURN", q->returns, &q->return_count, &q->distinct, 0);
}
static ng_status ng_cy_parse_query(const char* q, ng_cy_query* out) {
    const char* p = ng_skip_ws(q);
    memset(out, 0, sizeof(*out));
    out->where_root = -1;
    if (strncmp(p, "MATCH", 5) || !isspace((unsigned char)p[5]))
        return NG_PARSE_ERROR;
    while (!strncmp(p, "MATCH", 5) && isspace((unsigned char)p[5])) {
        if (ng_cy_parse_match_clause(&p, out) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    if (!strncmp(p, "WHERE", 5) && isspace((unsigned char)p[5])) {
        int root;
        p = ng_skip_ws(p + 5);
        if (ng_cy_parse_or(&p, out, &root) != NG_OK)
            return NG_PARSE_ERROR;
        if (out->where_root >= 0) {
            root = ng_cy_expr_add(out, 1, out->where_root, root, -1);
            if (root < 0)
                return NG_PARSE_ERROR;
        }
        out->where_root = root;
        out->has_where = 1;
        p = ng_skip_ws(p);
    }
    if (ng_cy_parse_return(&p, out) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    while (*p) {
        if (!strncmp(p, "ORDER", 5) && isspace((unsigned char)p[5]))
            return NG_PARSE_ERROR;
        else if (!strncmp(p, "SKIP", 4) && isspace((unsigned char)p[4])) {
            if (out->has_skip)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 4);
            if (ng_query_parse_uint64(&p, &out->skip) != NG_OK)
                return NG_PARSE_ERROR;
            out->has_skip = 1;
        } else if (!strncmp(p, "LIMIT", 5) && isspace((unsigned char)p[5])) {
            if (out->has_limit)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 5);
            if (ng_query_parse_uint64(&p, &out->limit) != NG_OK)
                return NG_PARSE_ERROR;
            out->has_limit = 1;
        } else
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    return NG_OK;
}
static int ng_cy_bind(ng_cy_row* row, int var_index, int kind, ng_id id) {
    if (var_index < 0)
        return 1;
    if (row->values[var_index].kind) {
        return row->values[var_index].kind == kind && row->values[var_index].id == id;
    }
    row->values[var_index].kind = kind;
    row->values[var_index].id = id;
    return 1;
}
static int ng_cy_bind_path(ng_cy_row* row, const ng_cy_match* m) {
    ng_cy_path* path;
    size_t i;
    if (m->path_var_index < 0)
        return 1;
    path = (ng_cy_path*)calloc(1, sizeof(*path));
    if (!path)
        return 0;
    path->node_count = m->node_count;
    path->relationship_count = m->rel_count;
    for (i = 0; i < m->node_count; i++) {
        if (m->nodes[i].var_index < 0 || row->values[m->nodes[i].var_index].kind != 1) {
            free(path);
            return 0;
        }
        path->nodes[i] = row->values[m->nodes[i].var_index].id;
    }
    for (i = 0; i < m->rel_count; i++) {
        if (m->rels[i].var_index < 0 || row->values[m->rels[i].var_index].kind != 2) {
            free(path);
            return 0;
        }
        path->relationships[i] = row->values[m->rels[i].var_index].id;
    }
    row->values[m->path_var_index].kind = 4;
    row->values[m->path_var_index].pointer = path;
    return 1;
}
static int ng_cy_path_append(ng_cy_row* row, int path_var_index, ng_id relationship, ng_id node_id) {
    ng_cy_path* old_path;
    ng_cy_path* path;
    if (path_var_index < 0)
        return 1;
    if (row->values[path_var_index].kind != 4 || !row->values[path_var_index].pointer)
        return 0;
    old_path = (ng_cy_path*)row->values[path_var_index].pointer;
    if (old_path->node_count >= NG_CY_MAX_PATH_LENGTH + 1 ||
        old_path->relationship_count >= NG_CY_MAX_PATH_LENGTH)
        return 0;
    path = (ng_cy_path*)malloc(sizeof(*path));
    if (!path)
        return 0;
    *path = *old_path;
    path->relationships[path->relationship_count++] = relationship;
    path->nodes[path->node_count++] = node_id;
    row->values[path_var_index].pointer = path;
    return 1;
}
static ng_status ng_cy_path_to_value(const ng_cy_path* path, ng_value* out) {
    ng_value_map* map;
    ng_value_list* node_list;
    ng_value_list* relationship_list;
    ng_value* nodes;
    ng_value* relationships;
    size_t i;
    if (!path || !out)
        return NG_INVALID_ARGUMENT;
    map = (ng_value_map*)calloc(1, sizeof(*map));
    if (!map || !(map->entries = (ng_value_map_entry*)calloc(2, sizeof(*map->entries)))) {
        free(map);
        return NG_OOM;
    }
    map->count = 2;
    map->entries[0].key = dupstr("nodes");
    map->entries[1].key = dupstr("relationships");
    nodes = (ng_value*)calloc(path->node_count, sizeof(*nodes));
    relationships = (ng_value*)calloc(path->relationship_count, sizeof(*relationships));
    if (!map->entries[0].key || !map->entries[1].key ||
        (path->node_count && !nodes) || (path->relationship_count && !relationships)) {
        ng_value cleanup = {.type = NG_VALUE_MAP, .as.map = map};
        free(nodes);
        free(relationships);
        valfree(&cleanup);
        return NG_OOM;
    }
    for (i = 0; i < path->node_count; i++) {
        nodes[i].type = NG_VALUE_INT64;
        nodes[i].as.integer = (int64_t)path->nodes[i];
    }
    for (i = 0; i < path->relationship_count; i++) {
        relationships[i].type = NG_VALUE_INT64;
        relationships[i].as.integer = (int64_t)path->relationships[i];
    }
    map->entries[0].value.type = NG_VALUE_LIST;
    map->entries[0].value.length = path->node_count;
    node_list = (ng_value_list*)calloc(1, sizeof(*node_list));
    relationship_list = (ng_value_list*)calloc(1, sizeof(*relationship_list));
    map->entries[0].value.as.list = node_list;
    map->entries[1].value.type = NG_VALUE_LIST;
    map->entries[1].value.length = path->relationship_count;
    map->entries[1].value.as.list = relationship_list;
    if (!map->entries[0].value.as.list || !map->entries[1].value.as.list) {
        ng_value cleanup = {.type = NG_VALUE_MAP, .as.map = map};
        free(nodes);
        free(relationships);
        valfree(&cleanup);
        return NG_OOM;
    }
    node_list->count = path->node_count;
    node_list->items = nodes;
    relationship_list->count = path->relationship_count;
    relationship_list->items = relationships;
    out->type = NG_VALUE_MAP;
    out->length = 2;
    out->as.map = map;
    return NG_OK;
}
static int ng_cy_node_matches(const ng_graph* g, const node_i* n, const ng_cy_node_pat* p) {
    ng_symbol_id label;
    if (!n)
        return 0;
    if (p->label[0]) {
        label = ng_symbol_id_by_text(g, p->label);
        if (!label || !ng_query_label_matches(n, label))
            return 0;
    }
    return 1;
}
static int ng_cy_rel_matches(const ng_graph* g, const rel_i* r, const ng_cy_rel_pat* p) {
    ng_symbol_id type;
    size_t i;
    if (!r)
        return 0;
    if (p->type[0]) {
        type = ng_symbol_id_by_text(g, p->type);
        if (!type || r->type != type)
            return 0;
    }
    for (i = 0; i < p->prop_count; i++) {
        ng_symbol_id key = ng_symbol_id_by_text(g, p->props[i].key);
        const prop* pr;
        ng_value v;
        if (!key)
            return 0;
        pr = findprop(r->p, r->np, key);
        if (!pr)
            return 0;
        if (ng_query_resolve_value(&p->props[i].value, &v) != NG_OK) {
            ng_query_parameter_error = 1;
            return 0;
        }
        if (!ng_value_equal(&pr->v, &v))
            return 0;
    }
    return 1;
}
static int ng_cy_term_matches(const ng_graph* g,
                              const ng_cy_query* q,
                              const ng_cy_row* row,
                              const ng_cy_term* t) {
    ng_cy_binding b = row->values[t->var_index];
    ng_value idv, v;
    const prop* p = NULL;
    ng_symbol_id key = 0;
    size_t i;
    (void)q;
    if (!b.kind)
        return t->op == 7;
    if (b.kind == 3) {
        if (b.value.type == NG_VALUE_NULL) {
            if (t->op == 7)
                return 1;
            if (t->op == 8)
                return 0;
            return 0;
        }
        if (t->is_id || t->key[0])
            return 0;
        if (t->op == 7)
            return 0;
        if (t->op == 8)
            return 1;
        if (t->op == 1) {
            for (i = 0; i < (size_t)t->value_count; i++) {
                if (ng_query_resolve_value(&t->values[i], &v) != NG_OK) {
                    ng_query_parameter_error = 1;
                    return 0;
                }
                if (ng_value_equal(&b.value, &v))
                    return 1;
            }
            return 0;
        }
        return ng_query_resolve_compare(&b.value, &t->value, t->op);
    }
    if (!t->key[0] && !t->is_id)
        return t->op == 8;
    if (t->is_id) {
        idv.type = NG_VALUE_INT64;
        idv.length = 0;
        idv.as.integer = (int64_t)b.id;
        if (t->op == 7)
            return 0;
        if (t->op == 8)
            return 1;
        if (t->op == 1) {
            for (i = 0; i < (size_t)t->value_count; i++)
                if (ng_query_resolve_compare(&idv, &t->values[i], 0))
                    return 1;
            return 0;
        }
        return ng_query_resolve_compare(&idv, &t->value, t->op);
    }
    key = ng_symbol_id_by_text(g, t->key);
    if (key) {
        if (b.kind == 1) {
            node_i* n = node((ng_graph*)g, b.id);
            if (n)
                p = findprop(n->p, n->np, key);
        } else {
            for (i = 0; i < g->nr; i++)
                if (g->re[i].id == b.id) {
                    p = findprop(g->re[i].p, g->re[i].np, key);
                    break;
                }
        }
    }
    if (!p)
        return t->op == 7;
    if (t->op == 7)
        return p->v.type == NG_VALUE_NULL;
    if (t->op == 8)
        return p->v.type != NG_VALUE_NULL;
    if (t->op == 1) {
        for (i = 0; i < (size_t)t->value_count; i++) {
            if (ng_query_resolve_value(&t->values[i], &v) != NG_OK) {
                ng_query_parameter_error = 1;
                return 0;
            }
            if (ng_value_equal(&p->v, &v))
                return 1;
        }
        return 0;
    }
    return ng_query_resolve_compare(&p->v, &t->value, t->op);
}
static int
ng_cy_expr_matches(const ng_graph* g, const ng_cy_query* q, const ng_cy_row* row, int expr) {
    const ng_cy_expr* e;
    if (expr < 0)
        return 1;
    if (expr >= q->expr_count)
        return 0;
    e = &q->exprs[expr];
    if (e->kind == 1)
        return ng_cy_expr_matches(g, q, row, e->left) && ng_cy_expr_matches(g, q, row, e->right);
    if (e->kind == 2)
        return ng_cy_expr_matches(g, q, row, e->left) || ng_cy_expr_matches(g, q, row, e->right);
    if (e->kind == 3)
        return !ng_cy_expr_matches(g, q, row, e->left);
    return e->term >= 0 && e->term < q->term_count &&
           ng_cy_term_matches(g, q, row, &q->terms[e->term]);
}
static ng_status ng_cy_apply_where(
    const ng_graph* g, const ng_cy_query* q, ng_cy_row* rows, size_t* count, int root);
static int ng_cy_append_row(ng_cy_row** rows, size_t* count, size_t* cap, const ng_cy_row* row) {
    if (*count >= NG_CY_MAX_ROWS)
        return 0;
    if (!grow((void**)rows, cap, *count + 1, sizeof(**rows)))
        return 0;
    (*rows)[(*count)++] = *row;
    return 1;
}
static ng_status ng_cy_expand_from_node(const ng_graph* g,
                                        const ng_cy_query* q,
                                        const ng_cy_match* m,
                                        size_t pos,
                                        const node_i* cur,
                                        const ng_cy_row* row,
                                        ng_cy_row** out,
                                        size_t* out_count,
                                        size_t* out_cap);
static ng_status ng_cy_expand_var_rel(const ng_graph* g,
                                      const ng_cy_query* q,
                                      const ng_cy_match* m,
                                      size_t pos,
                                      const node_i* cur,
                                      uint32_t depth,
                                      const ng_cy_row* row,
                                      unsigned char* seen,
                                      ng_cy_row** out,
                                      size_t* out_count,
                                      size_t* out_cap) {
    const ng_cy_rel_pat* pat = &m->rels[pos];
    size_t i;
    if (depth >= pat->min_depth) {
        ng_cy_row nr = *row;
        if (ng_cy_node_matches(g, cur, &m->nodes[pos + 1]) &&
            ng_cy_bind(&nr, m->nodes[pos + 1].var_index, 1, cur->id)) {
            if (ng_cy_expand_from_node(g, q, m, pos + 1, cur, &nr, out, out_count, out_cap) !=
                NG_OK)
                return NG_OOM;
        }
    }
    if (depth >= pat->max_depth)
        return NG_OK;
    for (i = 0; i < g->nr; i++) {
        const rel_i* r = &g->re[i];
        node_i* next = NULL;
        size_t npos, slot;
        uint32_t nd = depth + 1;
        if (!ng_cy_rel_matches(g, r, pat))
            continue;
        if (pat->dir > 0) {
            if (r->src != cur->id)
                continue;
            next = node((ng_graph*)g, r->dst);
        } else if (pat->dir < 0) {
            if (r->dst != cur->id)
                continue;
            next = node((ng_graph*)g, r->src);
        } else {
            if (r->src == cur->id)
                next = node((ng_graph*)g, r->dst);
            else if (r->dst == cur->id)
                next = node((ng_graph*)g, r->src);
            else
                continue;
        }
        if (!next)
            continue;
        npos = ng_node_position(g, next->id);
        if (npos == SIZE_MAX)
            continue;
        slot = npos * (size_t)(pat->max_depth + 1) + nd;
        if (seen[slot])
            continue;
        seen[slot] = 1;
        {
            ng_cy_row nr = *row;
            if (!ng_cy_path_append(&nr, m->path_var_index, r->id, next->id))
                return NG_OOM;
            if (ng_cy_expand_var_rel(g, q, m, pos, next, nd, &nr, seen, out, out_count, out_cap) !=
                NG_OK)
                return NG_OOM;
        }
    }
    return NG_OK;
}
static ng_status ng_cy_expand_from_node(const ng_graph* g,
                                        const ng_cy_query* q,
                                        const ng_cy_match* m,
                                        size_t pos,
                                        const node_i* cur,
                                        const ng_cy_row* row,
                                        ng_cy_row** out,
                                        size_t* out_count,
                                        size_t* out_cap) {
    size_t i;
    if (pos >= m->rel_count) {
        ng_cy_row nr = *row;
        if (m->path_var_index < 0 || nr.values[m->path_var_index].kind != 4) {
            if (!ng_cy_bind_path(&nr, m))
                return NG_OOM;
        }
        return ng_cy_append_row(out, out_count, out_cap, &nr) ? NG_OK : NG_OOM;
    }
    if (m->rels[pos].has_var_length) {
        size_t start = ng_node_position(g, cur->id), cap;
        ng_cy_row path_row = *row;
        if (start == SIZE_MAX)
            return NG_OK;
        if (m->path_var_index >= 0) {
            ng_cy_path* path = (ng_cy_path*)calloc(1, sizeof(*path));
            if (!path)
                return NG_OOM;
            path->nodes[0] = cur->id;
            path->node_count = 1;
            path_row.values[m->path_var_index].kind = 4;
            path_row.values[m->path_var_index].pointer = path;
        }
        cap = g->nn * (size_t)(m->rels[pos].max_depth + 1);
        {
            unsigned char* seen = (unsigned char*)calloc(cap, 1);
            ng_status s;
            if (cap && !seen)
                return NG_OOM;
            seen[start * (size_t)(m->rels[pos].max_depth + 1)] = 1;
            s = ng_cy_expand_var_rel(
                g, q, m, pos, cur, 0, &path_row, seen, out, out_count, out_cap);
            free(seen);
            return s;
        }
    }
    for (i = 0; i < g->nr; i++) {
        const rel_i* r = &g->re[i];
        node_i* next = NULL;
        ng_cy_row nr = *row;
        if (!ng_cy_rel_matches(g, r, &m->rels[pos]))
            continue;
        if (m->rels[pos].dir > 0) {
            if (r->src != cur->id)
                continue;
            next = node((ng_graph*)g, r->dst);
        } else if (m->rels[pos].dir < 0) {
            if (r->dst != cur->id)
                continue;
            next = node((ng_graph*)g, r->src);
        } else {
            if (r->src == cur->id)
                next = node((ng_graph*)g, r->dst);
            else if (r->dst == cur->id)
                next = node((ng_graph*)g, r->src);
            else
                continue;
        }
        if (!next || !ng_cy_node_matches(g, next, &m->nodes[pos + 1]))
            continue;
        if (!ng_cy_bind(&nr, m->rels[pos].var_index, 2, r->id) ||
            !ng_cy_bind(&nr, m->nodes[pos + 1].var_index, 1, next->id))
            continue;
        if (m->path_var_index >= 0 && nr.values[m->path_var_index].kind == 4 &&
            !ng_cy_path_append(&nr, m->path_var_index, r->id, next->id))
            return NG_OOM;
        if (ng_cy_expand_from_node(g, q, m, pos + 1, next, &nr, out, out_count, out_cap) != NG_OK)
            return NG_OOM;
    }
    return NG_OK;
}
static ng_status ng_cy_apply_match(const ng_graph* g,
                                   const ng_cy_query* q,
                                   const ng_cy_match* m,
                                   const ng_cy_row* in,
                                   size_t in_count,
                                   ng_cy_row** out,
                                   size_t* out_count) {
    size_t i, j, cap = 0;
    *out = NULL;
    *out_count = 0;
    for (i = 0; i < in_count; i++) {
        const ng_cy_row* row = &in[i];
        int vi = m->nodes[0].var_index;
        if (vi >= 0 && row->values[vi].kind) {
            node_i* n;
            if (row->values[vi].kind == 3 && row->values[vi].value.type == NG_VALUE_NULL)
                continue;
            if (row->values[vi].kind != 1)
                return NG_PARSE_ERROR;
            n = node((ng_graph*)g, row->values[vi].id);
            if (n && ng_cy_node_matches(g, n, &m->nodes[0])) {
                if (ng_cy_expand_from_node(g, q, m, 0, n, row, out, out_count, &cap) != NG_OK) {
                    free(*out);
                    return NG_OOM;
                }
            }
        } else {
            for (j = 0; j < g->nn; j++) {
                ng_cy_row nr = *row;
                if (!ng_cy_node_matches(g, &g->no[j], &m->nodes[0]))
                    continue;
                if (!ng_cy_bind(&nr, vi, 1, g->no[j].id))
                    continue;
                if (ng_cy_expand_from_node(g, q, m, 0, &g->no[j], &nr, out, out_count, &cap) !=
                    NG_OK) {
                    free(*out);
                    return NG_OOM;
                }
            }
        }
    }
    return NG_OK;
}
static void ng_cy_bind_optional_nulls(ng_cy_row* row, const ng_cy_match* m) {
    size_t i;
    ng_value v;
    memset(&v, 0, sizeof(v));
    v.type = NG_VALUE_NULL;
    for (i = 0; i < m->node_count; i++)
        if (m->nodes[i].var_index >= 0 && !row->values[m->nodes[i].var_index].kind) {
            row->values[m->nodes[i].var_index].kind = 3;
            row->values[m->nodes[i].var_index].value = v;
        }
    for (i = 0; i < m->rel_count; i++)
        if (m->rels[i].var_index >= 0 && !row->values[m->rels[i].var_index].kind) {
            row->values[m->rels[i].var_index].kind = 3;
            row->values[m->rels[i].var_index].value = v;
        }
}
static ng_status ng_cy_apply_optional_match(const ng_graph* g,
                                            const ng_cy_query* q,
                                            const ng_cy_match* m,
                                            const ng_cy_row* in,
                                            size_t in_count,
                                            int where_root,
                                            ng_cy_row** out,
                                            size_t* out_count) {
    size_t i, cap = 0;
    *out = NULL;
    *out_count = 0;
    for (i = 0; i < in_count; i++) {
        ng_cy_row* tmp = NULL;
        size_t tmp_count = 0, j;
        ng_status s = ng_cy_apply_match(g, q, m, &in[i], 1, &tmp, &tmp_count);
        if (s != NG_OK) {
            free(*out);
            return s;
        }
        if (where_root >= 0)
            ng_cy_apply_where(g, q, tmp, &tmp_count, where_root);
        if (tmp_count) {
            for (j = 0; j < tmp_count; j++)
                if (!ng_cy_append_row(out, out_count, &cap, &tmp[j])) {
                    free(tmp);
                    free(*out);
                    return NG_OOM;
                }
        } else {
            ng_cy_row nr = in[i];
            ng_cy_bind_optional_nulls(&nr, m);
            if (!ng_cy_append_row(out, out_count, &cap, &nr)) {
                free(tmp);
                free(*out);
                return NG_OOM;
            }
        }
        free(tmp);
    }
    return NG_OK;
}
static ng_status ng_cy_eval_scalar(
    const ng_graph* g, const ng_cy_query* q, const ng_cy_row* row, int index, ng_value* out) {
    const ng_cy_scalar* s;
    ng_value a, b;
    const prop* pr = NULL;
    ng_symbol_id key;
    size_t i;
    if (index < 0 || index >= q->scalar_count)
        return NG_PARSE_ERROR;
    s = &q->scalars[index];
    if (s->kind == 0)
        return ng_query_resolve_value(&s->value, out);
    if (s->kind == 1) {
        ng_cy_binding bind = row->values[s->var_index];
        if (!bind.kind) {
            out->type = NG_VALUE_NULL;
            out->length = 0;
            return NG_OK;
        }
        if (bind.kind == 3) {
            if (bind.value.type == NG_VALUE_NULL) {
                out->type = NG_VALUE_NULL;
                out->length = 0;
                return NG_OK;
            }
            if (s->key[0]) {
                size_t map_index;
                if (bind.value.type != NG_VALUE_MAP || !bind.value.as.map)
                    return NG_PARSE_ERROR;
                for (map_index = 0; map_index < bind.value.as.map->count; map_index++)
                    if (!strcmp(bind.value.as.map->entries[map_index].key, s->key)) {
                        *out = bind.value.as.map->entries[map_index].value;
                        return NG_OK;
                    }
                out->type = NG_VALUE_NULL;
                out->length = 0;
                return NG_OK;
            }
            *out = bind.value;
            return NG_OK;
        }
        if (bind.kind == 4 && !s->key[0])
            return ng_cy_path_to_value((const ng_cy_path*)bind.pointer, out);
        if (!strcmp(s->key, "id")) {
            out->type = NG_VALUE_INT64;
            out->length = 0;
            out->as.integer = (int64_t)bind.id;
            return NG_OK;
        }
        key = ng_symbol_id_by_text(g, s->key);
        if (key) {
            if (bind.kind == 1) {
                node_i* n = node((ng_graph*)g, bind.id);
                if (n)
                    pr = findprop(n->p, n->np, key);
            } else {
                for (i = 0; i < g->nr; i++)
                    if (g->re[i].id == bind.id) {
                        pr = findprop(g->re[i].p, g->re[i].np, key);
                        break;
                    }
            }
        }
        if (!pr) {
            out->type = NG_VALUE_NULL;
            out->length = 0;
            return NG_OK;
        }
        *out = pr->v;
        return NG_OK;
    }
    if (s->kind == 9 || s->kind == 10) {
        ng_value base;
        if (ng_cy_eval_scalar(g, q, row, s->left, &base) != NG_OK)
            return NG_PARSE_ERROR;
        if (base.type == NG_VALUE_NULL) {
            out->type = NG_VALUE_NULL;
            out->length = 0;
        } else if (base.type != NG_VALUE_LIST || !base.as.list) {
            if (ng_cy_scalar_temporary(q, s->left))
                valfree(&base);
            return NG_PARSE_ERROR;
        } else if (s->kind == 9) {
            int64_t index_value;
            size_t position;
            if (ng_cy_eval_scalar(g, q, row, s->right, &a) != NG_OK ||
                a.type != NG_VALUE_INT64) {
                if (ng_cy_scalar_temporary(q, s->left))
                    valfree(&base);
                return NG_PARSE_ERROR;
            }
            index_value = a.as.integer;
            if (index_value < 0)
                index_value += (int64_t)base.as.list->count;
            if (index_value < 0 || (uint64_t)index_value >= base.as.list->count) {
                out->type = NG_VALUE_NULL;
                out->length = 0;
            } else {
                position = (size_t)index_value;
                if (valcopy(out, &base.as.list->items[position]) != NG_OK) {
                    if (ng_cy_scalar_temporary(q, s->left))
                        valfree(&base);
                    return NG_OOM;
                }
            }
        } else {
            int64_t first = s->slice_start < 0 ? 0 : 0, last;
            ng_value bound;
            ng_value_list* list;
            size_t i, count;
            if (s->slice_start >= 0) {
                if (ng_cy_eval_scalar(g, q, row, s->slice_start, &bound) != NG_OK ||
                    bound.type != NG_VALUE_INT64) {
                    if (ng_cy_scalar_temporary(q, s->left))
                        valfree(&base);
                    return NG_PARSE_ERROR;
                }
                first = bound.as.integer;
            }
            last = (int64_t)base.as.list->count;
            if (s->slice_end >= 0) {
                if (ng_cy_eval_scalar(g, q, row, s->slice_end, &bound) != NG_OK ||
                    bound.type != NG_VALUE_INT64) {
                    if (ng_cy_scalar_temporary(q, s->left))
                        valfree(&base);
                    return NG_PARSE_ERROR;
                }
                last = bound.as.integer;
            }
            if (first < 0)
                first += (int64_t)base.as.list->count;
            if (last < 0)
                last += (int64_t)base.as.list->count;
            if (first < 0)
                first = 0;
            if (last < 0)
                last = 0;
            if ((uint64_t)first > base.as.list->count)
                first = (int64_t)base.as.list->count;
            if ((uint64_t)last > base.as.list->count)
                last = (int64_t)base.as.list->count;
            count = last > first ? (size_t)(last - first) : 0;
            list = (ng_value_list*)calloc(1, sizeof(*list));
            if (!list || (count && !(list->items = (ng_value*)calloc(count, sizeof(*list->items))))) {
                free(list);
                if (ng_cy_scalar_temporary(q, s->left))
                    valfree(&base);
                return NG_OOM;
            }
            list->count = count;
            for (i = 0; i < count; i++)
                if (valcopy(&list->items[i], &base.as.list->items[(size_t)first + i]) != NG_OK) {
                    ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                    valfree(&cleanup);
                    if (ng_cy_scalar_temporary(q, s->left))
                        valfree(&base);
                    return NG_OOM;
                }
            out->type = NG_VALUE_LIST;
            out->length = count;
            out->as.list = list;
        }
        if (ng_cy_scalar_temporary(q, s->left))
            valfree(&base);
        return NG_OK;
    }
    if (s->kind == 21 || s->kind == 22) {
        ng_cy_binding binding;
        ng_cy_path* path;
        ng_value_list* list;
        size_t i, count;
        if (s->list_count != 1 || q->scalars[s->list_items[0]].kind != 1)
            return NG_PARSE_ERROR;
        binding = row->values[q->scalars[s->list_items[0]].var_index];
        if (!binding.kind || binding.kind == 3) {
            out->type = NG_VALUE_NULL;
            out->length = 0;
            return NG_OK;
        }
        if (binding.kind != 4 || !binding.pointer)
            return NG_PARSE_ERROR;
        path = (ng_cy_path*)binding.pointer;
        count = s->kind == 21 ? path->node_count : path->relationship_count;
        list = (ng_value_list*)calloc(1, sizeof(*list));
        if (!list)
            return NG_OOM;
        list->count = count;
        if (count && !(list->items = (ng_value*)calloc(count, sizeof(*list->items)))) {
            free(list);
            return NG_OOM;
        }
        for (i = 0; i < count; i++) {
            list->items[i].type = NG_VALUE_INT64;
            list->items[i].as.integer = (int64_t)(s->kind == 21 ? path->nodes[i]
                                                                  : path->relationships[i]);
        }
        out->type = NG_VALUE_LIST;
        out->length = count;
        out->as.list = list;
        return NG_OK;
    }
    if (s->kind >= 23 && s->kind <= 26) {
        ng_value argument;
        char* text;
        size_t i, start, end;
        if (s->list_count != 1 ||
            ng_cy_eval_scalar(g, q, row, s->list_items[0], &argument) != NG_OK)
            return NG_PARSE_ERROR;
        if (argument.type == NG_VALUE_NULL) {
            *out = argument;
            return NG_OK;
        }
        if (s->kind == 26) {
            if (argument.type == NG_VALUE_INT64) {
                out->type = NG_VALUE_INT64;
                out->as.integer = argument.as.integer < 0 ? -argument.as.integer : argument.as.integer;
                out->length = 0;
                return NG_OK;
            }
            if (argument.type == NG_VALUE_DOUBLE) {
                out->type = NG_VALUE_DOUBLE;
                out->as.real = argument.as.real < 0 ? -argument.as.real : argument.as.real;
                out->length = 0;
                return NG_OK;
            }
            return NG_PARSE_ERROR;
        }
        if (argument.type != NG_VALUE_STRING)
            return NG_PARSE_ERROR;
        start = 0;
        end = argument.length;
        if (s->kind == 25) {
            while (start < end && isspace((unsigned char)argument.as.string[start]))
                start++;
            while (end > start && isspace((unsigned char)argument.as.string[end - 1]))
                end--;
        }
        text = (char*)malloc(end - start + 1);
        if (!text)
            return NG_OOM;
        for (i = start; i < end; i++)
            text[i - start] = s->kind == 23
                                   ? (char)tolower((unsigned char)argument.as.string[i])
                                   : (s->kind == 24 ? (char)toupper((unsigned char)argument.as.string[i])
                                                    : argument.as.string[i]);
        text[end - start] = 0;
        out->type = NG_VALUE_STRING;
        out->length = end - start;
        out->as.string = text;
        return NG_OK;
    }
    if (s->kind >= 13 && s->kind <= 19) {
        ng_value argument, result;
        size_t argument_index, i;
        if (s->kind == 18) {
            FILE* text;
            long length;
            char* string;
            if (s->list_count != 1 ||
                ng_cy_eval_scalar(g, q, row, s->list_items[0], &argument) != NG_OK)
                return NG_PARSE_ERROR;
            text = tmpfile();
            if (!text || !ng_print_value(text, &argument) || fflush(text) != 0 ||
                fseek(text, 0, SEEK_END) != 0) {
                if (text)
                    fclose(text);
                return NG_IO_ERROR;
            }
            length = ftell(text);
            if (length < 0 || fseek(text, 0, SEEK_SET) != 0 ||
                !(string = (char*)malloc((size_t)length + 1))) {
                fclose(text);
                return length < 0 ? NG_IO_ERROR : NG_OOM;
            }
            if (length && fread(string, 1, (size_t)length, text) != (size_t)length) {
                free(string);
                fclose(text);
                return NG_IO_ERROR;
            }
            string[length] = 0;
            fclose(text);
            out->type = NG_VALUE_STRING;
            out->length = (size_t)length;
            out->as.string = string;
            return NG_OK;
        }
        if (s->kind == 19) {
            memset(out, 0, sizeof(*out));
            out->type = NG_VALUE_NULL;
            for (argument_index = 0; argument_index < (size_t)s->list_count; argument_index++) {
                if (ng_cy_eval_scalar(g, q, row, s->list_items[argument_index], &result) != NG_OK)
                    return NG_PARSE_ERROR;
                if (result.type != NG_VALUE_NULL) {
                    *out = result;
                    return NG_OK;
                }
            }
            return NG_OK;
        }
        if (s->list_count != 1 ||
            ng_cy_eval_scalar(g, q, row, s->list_items[0], &argument) != NG_OK)
            return NG_PARSE_ERROR;
        if (s->kind == 13) {
            if (argument.type == NG_VALUE_LIST && argument.as.list)
                out->as.integer = (int64_t)argument.as.list->count;
            else if (argument.type == NG_VALUE_MAP && argument.as.map)
                out->as.integer = (int64_t)argument.as.map->count;
            else if (argument.type == NG_VALUE_STRING)
                out->as.integer = (int64_t)argument.length;
            else if (argument.type == NG_VALUE_NULL) {
                out->type = NG_VALUE_NULL;
                out->length = 0;
                return NG_OK;
            } else
                return NG_PARSE_ERROR;
            out->type = NG_VALUE_INT64;
            out->length = 0;
            return NG_OK;
        }
        if (argument.type != NG_VALUE_LIST || !argument.as.list)
            return argument.type == NG_VALUE_NULL ? (*out = argument, NG_OK) : NG_PARSE_ERROR;
        if (s->kind == 14 || s->kind == 15) {
            if (!argument.as.list->count) {
                out->type = NG_VALUE_NULL;
                out->length = 0;
            } else {
                i = s->kind == 14 ? 0 : argument.as.list->count - 1;
                if (valcopy(out, &argument.as.list->items[i]) != NG_OK)
                    return NG_OOM;
            }
            return NG_OK;
        }
        {
            ng_value_list* list = (ng_value_list*)calloc(1, sizeof(*list));
            if (!list)
                return NG_OOM;
            list->count = s->kind == 16 ? (argument.as.list->count ? argument.as.list->count - 1 : 0)
                                         : argument.as.list->count;
            if (list->count)
                list->items = (ng_value*)calloc(list->count, sizeof(*list->items));
            if (list->count && !list->items) {
                free(list);
                return NG_OOM;
            }
            for (i = 0; i < list->count; i++) {
                size_t source = s->kind == 16 ? i + 1 : argument.as.list->count - i - 1;
                if (valcopy(&list->items[i], &argument.as.list->items[source]) != NG_OK) {
                    ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                    valfree(&cleanup);
                    return NG_OOM;
                }
            }
            out->type = NG_VALUE_LIST;
            out->length = list->count;
            out->as.list = list;
            return NG_OK;
        }
    }
    if (s->kind == 20) {
        size_t branch;
        ng_value operand, condition;
        if (s->case_simple &&
            ng_cy_eval_scalar(g, q, row, s->case_operand, &operand) != NG_OK)
            return NG_PARSE_ERROR;
        for (branch = 0; branch + 1 < (size_t)s->list_count; branch += 2) {
            int matches = s->case_simple
                              ? (ng_cy_eval_scalar(g, q, row, s->list_items[branch], &condition) ==
                                     NG_OK &&
                                 ng_value_equal(&operand, &condition))
                              : ng_cy_expr_matches(g, q, row, s->list_items[branch]);
            if (matches)
                return ng_cy_eval_scalar(g, q, row, s->list_items[branch + 1], out);
        }
        if (s->slice_end >= 0)
            return ng_cy_eval_scalar(g, q, row, s->slice_end, out);
        out->type = NG_VALUE_NULL;
        out->length = 0;
        return NG_OK;
    }
    if (s->kind == 11) {
        ng_value source;
        ng_value_list* list;
        size_t j, capacity = 0;
        if (ng_cy_eval_scalar(g, q, row, s->comprehension_source, &source) != NG_OK)
            return NG_PARSE_ERROR;
        if (source.type == NG_VALUE_NULL) {
            out->type = NG_VALUE_LIST;
            out->length = 0;
            out->as.list = (ng_value_list*)calloc(1, sizeof(*out->as.list));
            if (!out->as.list)
                return NG_OOM;
            return NG_OK;
        }
        if (source.type != NG_VALUE_LIST || !source.as.list)
            return NG_PARSE_ERROR;
        list = (ng_value_list*)calloc(1, sizeof(*list));
        if (!list)
            return NG_OOM;
        for (j = 0; j < source.as.list->count; j++) {
            ng_cy_row scoped = *row;
            ng_value item, evaluated;
            scoped.values[s->comprehension_var].kind = 3;
            scoped.values[s->comprehension_var].value = source.as.list->items[j];
            if (s->comprehension_filter >= 0 &&
                !ng_cy_expr_matches(g, q, &scoped, s->comprehension_filter))
                continue;
            if (ng_cy_eval_scalar(g, q, &scoped, s->comprehension_value, &evaluated) != NG_OK)
                return NG_PARSE_ERROR;
            if (!grow((void**)&list->items, &capacity, list->count + 1, sizeof(*list->items))) {
                ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                valfree(&cleanup);
                return NG_OOM;
            }
            item = evaluated;
            if (valcopy(&list->items[list->count], &item) != NG_OK) {
                ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                valfree(&cleanup);
                return NG_OOM;
            }
            list->count++;
            if (ng_cy_scalar_temporary(q, s->comprehension_value))
                valfree(&item);
        }
        out->type = NG_VALUE_LIST;
        out->length = list->count;
        out->as.list = list;
        return NG_OK;
    }
    if (s->kind == 7) {
        ng_value_list* list = (ng_value_list*)calloc(1, sizeof(*list));
        size_t j;
        if (!list)
            return NG_OOM;
        list->count = (size_t)s->list_count;
        if (list->count) {
            list->items = (ng_value*)calloc(list->count, sizeof(*list->items));
            if (!list->items) {
                free(list);
                return NG_OOM;
            }
            for (j = 0; j < list->count; j++) {
                ng_value evaluated;
                if (ng_cy_eval_scalar(g, q, row, s->list_items[j], &evaluated) != NG_OK) {
                    ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                    valfree(&cleanup);
                    return NG_PARSE_ERROR;
                }
                if (valcopy(&list->items[j], &evaluated) != NG_OK) {
                    if (q->scalars[s->list_items[j]].kind == 7 ||
                        q->scalars[s->list_items[j]].kind == 8)
                        valfree(&evaluated);
                    ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                    valfree(&cleanup);
                    return NG_OOM;
                }
                if (q->scalars[s->list_items[j]].kind == 7 ||
                    q->scalars[s->list_items[j]].kind == 8)
                    valfree(&evaluated);
            }
        }
        out->type = NG_VALUE_LIST;
        out->length = list->count;
        out->as.list = list;
        return NG_OK;
    }
    if (s->kind == 8) {
        ng_value_map* map = (ng_value_map*)calloc(1, sizeof(*map));
        size_t j;
        if (!map)
            return NG_OOM;
        map->count = s->map_count;
        if (map->count) {
            map->entries = (ng_value_map_entry*)calloc(map->count, sizeof(*map->entries));
            if (!map->entries) {
                free(map);
                return NG_OOM;
            }
            for (j = 0; j < map->count; j++) {
                ng_value evaluated;
                map->entries[j].key = dupstr(s->map_keys[j]);
                if (!map->entries[j].key ||
                    ng_cy_eval_scalar(g, q, row, s->map_items[j], &evaluated) != NG_OK) {
                    ng_value cleanup = {.type = NG_VALUE_MAP, .as.map = map};
                    valfree(&cleanup);
                    return NG_PARSE_ERROR;
                }
                if (valcopy(&map->entries[j].value, &evaluated) != NG_OK) {
                    if (q->scalars[s->map_items[j]].kind == 7 ||
                        q->scalars[s->map_items[j]].kind == 8)
                        valfree(&evaluated);
                    ng_value cleanup = {.type = NG_VALUE_MAP, .as.map = map};
                    valfree(&cleanup);
                    return NG_OOM;
                }
                if (q->scalars[s->map_items[j]].kind == 7 || q->scalars[s->map_items[j]].kind == 8)
                    valfree(&evaluated);
            }
        }
        out->type = NG_VALUE_MAP;
        out->length = map->count;
        out->as.map = map;
        return NG_OK;
    }
    if (s->kind == 6) {
        if (ng_cy_eval_scalar(g, q, row, s->left, &a) != NG_OK)
            return NG_PARSE_ERROR;
        if (a.type != NG_VALUE_INT64)
            return NG_PARSE_ERROR;
        out->type = NG_VALUE_INT64;
        out->length = 0;
        out->as.integer = -a.as.integer;
        return NG_OK;
    }
    if (ng_cy_eval_scalar(g, q, row, s->left, &a) != NG_OK ||
        ng_cy_eval_scalar(g, q, row, s->right, &b) != NG_OK)
        return NG_PARSE_ERROR;
    if (s->kind == 2 && a.type == NG_VALUE_LIST && b.type == NG_VALUE_LIST && a.as.list &&
        b.as.list) {
        ng_value_list* list = (ng_value_list*)calloc(1, sizeof(*list));
        size_t ai, bi;
        if (!list)
            return NG_OOM;
        list->count = a.as.list->count + b.as.list->count;
        if (list->count)
            list->items = (ng_value*)calloc(list->count, sizeof(*list->items));
        if (list->count && !list->items) {
            free(list);
            return NG_OOM;
        }
        for (ai = 0; ai < a.as.list->count; ai++)
            if (valcopy(&list->items[ai], &a.as.list->items[ai]) != NG_OK) {
                ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                valfree(&cleanup);
                return NG_OOM;
            }
        for (bi = 0; bi < b.as.list->count; bi++)
            if (valcopy(&list->items[a.as.list->count + bi], &b.as.list->items[bi]) != NG_OK) {
                ng_value cleanup = {.type = NG_VALUE_LIST, .as.list = list};
                valfree(&cleanup);
                return NG_OOM;
            }
        out->type = NG_VALUE_LIST;
        out->length = list->count;
        out->as.list = list;
        if (ng_cy_scalar_temporary(q, s->left))
            valfree(&a);
        if (ng_cy_scalar_temporary(q, s->right))
            valfree(&b);
        return NG_OK;
    }
    if (a.type != NG_VALUE_INT64 || b.type != NG_VALUE_INT64)
        return NG_PARSE_ERROR;
    out->type = NG_VALUE_INT64;
    out->length = 0;
    if (s->kind == 2)
        out->as.integer = a.as.integer + b.as.integer;
    else if (s->kind == 3)
        out->as.integer = a.as.integer - b.as.integer;
    else if (s->kind == 4)
        out->as.integer = a.as.integer * b.as.integer;
    else if (s->kind == 5) {
        if (!b.as.integer)
            return NG_PARSE_ERROR;
        out->as.integer = a.as.integer / b.as.integer;
    } else
        return NG_PARSE_ERROR;
    return NG_OK;
}
static ng_status ng_cy_apply_unwind(
    const ng_graph* g, ng_cy_query* q, ng_cy_row** rows, size_t* row_count, const char** pp) {
    const char* p = ng_skip_ws(*pp + 6);
    ng_cy_row* out = NULL;
    size_t out_count = 0, out_cap = 0, i, j;
    int scalar, vi;
    char name[64];
    ng_status s;
    if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (strncmp(p, "AS", 2) || !isspace((unsigned char)p[2]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 2);
    if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_cy_var_lookup(q, name) >= 0)
        return NG_PARSE_ERROR;
    vi = ng_cy_var_index(q, name, 3, 1);
    if (vi < 0)
        return NG_PARSE_ERROR;
    for (i = 0; i < *row_count; i++) {
        const ng_cy_scalar* src = &q->scalars[scalar];
        if (src->kind == 7) {
            for (j = 0; j < (size_t)src->list_count; j++) {
                ng_value item;
                ng_cy_row nr = (*rows)[i];
                s = ng_cy_eval_scalar(g, q, &(*rows)[i], src->list_items[j], &item);
                if (s != NG_OK) {
                    free(out);
                    return s;
                }
                nr.values[vi].kind = 3;
                nr.values[vi].value = item;
                if (!ng_cy_append_row(&out, &out_count, &out_cap, &nr)) {
                    free(out);
                    return out_count >= NG_CY_MAX_ROWS ? NG_LIMIT : NG_OOM;
                }
            }
        } else {
            ng_value list;
            s = ng_cy_eval_scalar(g, q, &(*rows)[i], scalar, &list);
            if (s != NG_OK) {
                free(out);
                return s;
            }
            if (list.type == NG_VALUE_NULL)
                continue;
            if (list.type != NG_VALUE_LIST || !list.as.list) {
                free(out);
                return NG_PARSE_ERROR;
            }
            for (j = 0; j < list.as.list->count; j++) {
                ng_cy_row nr = (*rows)[i];
                nr.values[vi].kind = 3;
                nr.values[vi].value = list.as.list->items[j];
                if (!ng_cy_append_row(&out, &out_count, &out_cap, &nr)) {
                    free(out);
                    return out_count >= NG_CY_MAX_ROWS ? NG_LIMIT : NG_OOM;
                }
            }
        }
    }
    free(*rows);
    *rows = out;
    *row_count = out_count;
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static int ng_cy_clause_starts(const char* p, const char* kw) {
    size_t n = strlen(kw);
    return !strncmp(p, kw, n) && isspace((unsigned char)p[n]);
}
static int ng_cy_has_with(const char* q) {
    const char* p = q;
    while (*p) {
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "WITH", 4) && isspace((unsigned char)p[4])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "UNWIND", 6) && isspace((unsigned char)p[6])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "OPTIONAL", 8) && isspace((unsigned char)p[8])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "ORDER", 5) && isspace((unsigned char)p[5])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "SET", 3) && isspace((unsigned char)p[3])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "DELETE", 6) && isspace((unsigned char)p[6])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "DETACH", 6) && isspace((unsigned char)p[6])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "REMOVE", 6) && isspace((unsigned char)p[6])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "MERGE", 5) && isspace((unsigned char)p[5])))
            return 1;
        if ((p == q || !ng_ident_char((unsigned char)p[-1])) &&
            (!strncmp(p, "CALL", 4) && isspace((unsigned char)p[4])))
            return 1;
        p++;
    }
    return 0;
}
static int ng_cy_has_write_clause(const char* q) {
    const char* p = q;
    while (*p) {
        p = ng_skip_ws(p);
        if (ng_cy_clause_starts(p, "CREATE") || ng_cy_clause_starts(p, "MERGE") ||
            ng_cy_clause_starts(p, "SET") || ng_cy_clause_starts(p, "DELETE") ||
            ng_cy_clause_starts(p, "DETACH") || ng_cy_clause_starts(p, "REMOVE"))
            return 1;
        p++;
    }
    return 0;
}
static int ng_query_has_union(const char* query) {
    const char* p = query;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                return 0;
            p++;
            continue;
        }
        if (!strncmp(p, "UNION", 5) && (p == query || !ng_ident_char((unsigned char)p[-1])) &&
            !ng_ident_char((unsigned char)p[5]))
            return 1;
        p++;
    }
    return 0;
}
static int ng_query_has_statement_separator(const char* query) {
    const char* p = query;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                return 0;
            p++;
            continue;
        }
        if (*p == ';')
            return 1;
        p++;
    }
    return 0;
}
static ng_status ng_cy_remove_label(ng_graph* g, ng_node_id id, ng_symbol_id label) {
    node_i* n = node(g, id);
    size_t i;
    if (!n || !label)
        return NG_NOT_FOUND;
    for (i = 0; i < n->nl; i++)
        if (n->labels[i] == label) {
            if (i + 1 < n->nl)
                memmove(&n->labels[i], &n->labels[i + 1], (n->nl - i - 1) * sizeof(*n->labels));
            n->nl--;
            return NG_OK;
        }
    return NG_NOT_FOUND;
}
static ng_status ng_cy_apply_remove_to_rows(
    ng_graph* g, ng_cy_query* q, ng_cy_row* rows, size_t row_count, const char** pp, int* changed) {
    const char* p = ng_skip_ws(*pp + 6);
    ng_status s;
    for (;;) {
        char name[64], key_text[128];
        int vi;
        ng_symbol_id key;
        size_t i, n;
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
            return NG_PARSE_ERROR;
        vi = ng_cy_var_lookup(q, name);
        if (vi < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p != '.' && *p != ':')
            return NG_PARSE_ERROR;
        {
            int label = *p == ':';
            p++;
            for (n = 0; ng_ident_char((unsigned char)p[n]); n++)
                ;
            if (!n || n >= sizeof(key_text))
                return NG_PARSE_ERROR;
            memcpy(key_text, p, n);
            key_text[n] = 0;
            p = ng_skip_ws(p + n);
            key = ng_symbol_id_by_text(g, key_text);
            if (key)
                for (i = 0; i < row_count; i++) {
                    ng_cy_binding b = rows[i].values[vi];
                    if (!b.kind || b.kind == 3)
                        continue;
                    if (label) {
                        if (b.kind != 1)
                            return NG_PARSE_ERROR;
                        s = ng_cy_remove_label(g, b.id, key);
                    } else {
                        if (b.kind == 1)
                            s = ng_node_unset(g, b.id, key);
                        else if (b.kind == 2)
                            s = ng_relationship_unset(g, b.id, key);
                        else
                            return NG_PARSE_ERROR;
                    }
                    if (s == NG_OK && changed)
                        *changed = 1;
                    if (s != NG_OK && s != NG_NOT_FOUND)
                        return s;
                }
        }
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p || ng_cy_clause_starts(p, "RETURN") || ng_cy_clause_starts(p, "WITH") ||
            ng_cy_clause_starts(p, "MATCH") || ng_cy_clause_starts(p, "OPTIONAL") ||
            ng_cy_clause_starts(p, "DELETE") || ng_cy_clause_starts(p, "DETACH") ||
            ng_cy_clause_starts(p, "REMOVE") || ng_cy_clause_starts(p, "SET") ||
            ng_cy_clause_starts(p, "CREATE") || ng_cy_clause_starts(p, "MERGE"))
            return NG_PARSE_ERROR;
    }
    *pp = p;
    return NG_OK;
}
static void ng_cy_scope_clear(ng_cy_query* q) {
    size_t i;
    for (i = 0; i < q->var_count; i++)
        q->vars[i].in_scope = 0;
}
static int ng_cy_scope_find_any(ng_cy_query* q, const char* name) {
    size_t i;
    for (i = 0; i < q->var_count; i++)
        if (!q->vars[i].in_scope && !strcmp(q->vars[i].name, name))
            return (int)i;
    return -1;
}
static int ng_cy_scope_project_var(ng_cy_query* q, const char* name, int kind) {
    int i = ng_cy_scope_find_any(q, name);
    if (i >= 0) {
        q->vars[i].kind = kind;
        q->vars[i].in_scope = 1;
        return i;
    }
    return ng_cy_var_index(q, name, kind, 1);
}
static int ng_cy_projection_seen(const ng_cy_result_key* seen,
                                 size_t seen_count,
                                 const ng_cy_result_key* key,
                                 size_t count) {
    size_t i, j;
    for (i = 0; i < seen_count; i++) {
        int same = 1;
        for (j = 0; j < count; j++)
            if (!ng_value_equal(&seen[i].values[j], &key->values[j])) {
                same = 0;
                break;
            }
        if (same)
            return 1;
    }
    return 0;
}
static ng_status ng_cy_parse_projection_list(const char** pp,
                                             ng_cy_query* q,
                                             const char* keyword,
                                             ng_cy_projection* projs,
                                             size_t* count,
                                             int* distinct,
                                             int for_with) {
    const char* p = ng_skip_ws(*pp);
    size_t kw = strlen(keyword);
    if (strncmp(p, keyword, kw) || !isspace((unsigned char)p[kw]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + kw);
    *count = 0;
    *distinct = 0;
    if (!strncmp(p, "DISTINCT", 8) && isspace((unsigned char)p[8])) {
        *distinct = 1;
        p = ng_skip_ws(p + 8);
    }
    for (;;) {
        ng_cy_projection* r;
        const char* expression_start = p;
        int scalar = -1, kind = 3;
        char alias[64] = {0};
        if (*count >= NG_CY_MAX_RETURNS)
            return NG_PARSE_ERROR;
        r = &projs[*count];
        memset(r, 0, sizeof(*r));
        r->var_index = -1;
        r->scalar_index = -1;
        r->out_var_index = -1;
        if ((!strncmp(p, "count", 5) || !strncmp(p, "sum", 3) || !strncmp(p, "collect", 7))) {
            const char* fn = p;
            size_t fl = !strncmp(p, "count", 5) ? 5 : (!strncmp(p, "sum", 3) ? 3 : 7);
            if (ng_ident_char((unsigned char)p[fl]))
                goto scalar_projection;
            r->aggregate = fl == 5 ? 1 : (fl == 3 ? 2 : 3);
            p = ng_skip_ws(p + fl);
            if (*p != '(')
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 1);
            if (!strncmp(p, "DISTINCT", 8) && isspace((unsigned char)p[8])) {
                r->aggregate_distinct = 1;
                p = ng_skip_ws(p + 8);
            }
            if (r->aggregate == 1 && *p == '*') {
                r->count_star = 1;
                p = ng_skip_ws(p + 1);
                if (r->aggregate_distinct)
                    return NG_PARSE_ERROR;
            } else {
                if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
                    return NG_PARSE_ERROR;
                r->scalar_index = scalar;
            }
            p = ng_skip_ws(p);
            if (*p != ')')
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 1);
            (void)fn;
        } else {
        scalar_projection:
            if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
                return NG_PARSE_ERROR;
            r->scalar_index = scalar;
        }
        if (r->scalar_index >= 0 && q->scalars[r->scalar_index].kind == 1) {
            r->var_index = q->scalars[r->scalar_index].var_index;
            r->is_property =
                q->scalars[r->scalar_index].key[0] && strcmp(q->scalars[r->scalar_index].key, "id")
                    ? 1
                    : 0;
            r->is_id = q->scalars[r->scalar_index].key[0] && !r->is_property;
            strcpy(r->key, q->scalars[r->scalar_index].key);
        }
        p = ng_skip_ws(p);
        {
            size_t source_length = (size_t)(p - expression_start);
            if (!source_length || source_length >= sizeof(r->source))
                return NG_PARSE_ERROR;
            memcpy(r->source, expression_start, source_length);
            r->source[source_length] = 0;
        }
        if (!strncmp(p, "AS", 2) && isspace((unsigned char)p[2])) {
            p = ng_skip_ws(p + 2);
            if (ng_cy_parse_ident(&p, alias, sizeof(alias)) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p);
        } else if (for_with && r->var_index >= 0 && !r->is_property && !r->aggregate) {
            strcpy(alias, q->vars[r->var_index].name);
            kind = q->vars[r->var_index].kind;
        } else if (for_with)
            return NG_PARSE_ERROR;
        if (alias[0]) {
            strcpy(r->out_name, alias);
            r->out_kind = kind;
        }
        (*count)++;
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p)
            return NG_PARSE_ERROR;
    }
    *pp = p;
    return *count ? NG_OK : NG_PARSE_ERROR;
}
static ng_status ng_cy_apply_where(
    const ng_graph* g, const ng_cy_query* q, ng_cy_row* rows, size_t* count, int root) {
    size_t i, w = 0;
    for (i = 0; i < *count; i++)
        if (ng_cy_expr_matches(g, q, &rows[i], root))
            rows[w++] = rows[i];
    *count = w;
    return NG_OK;
}
typedef struct {
    ng_cy_result_key key;
    ng_cy_binding passthrough[NG_CY_MAX_RETURNS];
    ng_value agg[NG_CY_MAX_RETURNS];
    ng_value_list* seen[NG_CY_MAX_RETURNS];
    int sum_seen[NG_CY_MAX_RETURNS], sum_double[NG_CY_MAX_RETURNS];
} ng_cy_group;
static int ng_cy_has_aggregate(const ng_cy_projection* p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i].aggregate)
            return 1;
    return 0;
}
static int ng_cy_group_key_equal(const ng_cy_group* g,
                                 const ng_cy_result_key* k,
                                 const ng_cy_projection* p,
                                 size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (!p[i].aggregate && !ng_value_equal(&g->key.values[i], &k->values[i]))
            return 0;
    return 1;
}
static int ng_cy_list_contains(const ng_value_list* l, const ng_value* v) {
    size_t i;
    if (!l)
        return 0;
    for (i = 0; i < l->count; i++)
        if (ng_value_equal(&l->items[i], v))
            return 1;
    return 0;
}
static ng_status ng_cy_list_add(ng_value_list** lp, const ng_value* v) {
    ng_value_list* l = *lp;
    ng_value copy;
    ng_value* items;
    if (!l) {
        l = (ng_value_list*)calloc(1, sizeof(*l));
        if (!l)
            return NG_OOM;
        *lp = l;
    }
    items = (ng_value*)realloc(l->items, (l->count + 1) * sizeof(*l->items));
    if (!items)
        return NG_OOM;
    l->items = items;
    memset(&copy, 0, sizeof(copy));
    if (valcopy(&copy, v) != NG_OK)
        return NG_OOM;
    l->items[l->count++] = copy;
    return NG_OK;
}
static ng_status ng_cy_group_init_agg(ng_cy_group* g, const ng_cy_projection* p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i].aggregate) {
            if (p[i].aggregate == 1) {
                g->agg[i].type = NG_VALUE_INT64;
                g->agg[i].as.integer = 0;
            } else if (p[i].aggregate == 2) {
                g->agg[i].type = NG_VALUE_NULL;
            } else {
                ng_value_list* l = (ng_value_list*)calloc(1, sizeof(*l));
                if (!l)
                    return NG_OOM;
                g->agg[i].type = NG_VALUE_LIST;
                g->agg[i].as.list = l;
                g->agg[i].length = 0;
            }
        }
    return NG_OK;
}
static ng_status ng_cy_group_add(ng_cy_group** groups,
                                 size_t* count,
                                 size_t* cap,
                                 const ng_cy_result_key* key,
                                 const ng_cy_projection* p,
                                 size_t n,
                                 const ng_cy_row* row) {
    size_t j;
    if (!grow((void**)groups, cap, *count + 1, sizeof(**groups)))
        return NG_OOM;
    memset(&(*groups)[*count], 0, sizeof(**groups));
    for (j = 0; j < n; j++)
        if (!p[j].aggregate) {
            ng_status s = valcopy(&(*groups)[*count].key.values[j], &key->values[j]);
            if (s != NG_OK)
                return s;
            if (p[j].var_index >= 0 && !p[j].is_property)
                (*groups)[*count].passthrough[j] = row->values[p[j].var_index];
        }
    if (ng_cy_group_init_agg(&(*groups)[*count], p, n) != NG_OK)
        return NG_OOM;
    (*count)++;
    return NG_OK;
}
static ng_status ng_cy_aggregate_row(const ng_graph* g,
                                     const ng_cy_query* q,
                                     const ng_cy_projection* p,
                                     size_t n,
                                     const ng_cy_row* row,
                                     ng_cy_group* grp) {
    size_t j;
    ng_status s;
    for (j = 0; j < n; j++)
        if (p[j].aggregate) {
            ng_value v;
            if (p[j].aggregate == 1 && p[j].count_star) {
                grp->agg[j].as.integer++;
                continue;
            }
            s = ng_cy_eval_scalar(g, q, row, p[j].scalar_index, &v);
            if (s != NG_OK)
                return s;
            if (v.type == NG_VALUE_NULL)
                continue;
            if (p[j].aggregate_distinct) {
                if (ng_cy_list_contains(grp->seen[j], &v))
                    continue;
                s = ng_cy_list_add(&grp->seen[j], &v);
                if (s != NG_OK)
                    return s;
            }
            if (p[j].aggregate == 1)
                grp->agg[j].as.integer++;
            else if (p[j].aggregate == 2) {
                if (v.type != NG_VALUE_INT64 && v.type != NG_VALUE_DOUBLE)
                    return NG_PARSE_ERROR;
                if (!grp->sum_seen[j]) {
                    grp->agg[j] = v;
                    grp->sum_seen[j] = 1;
                    grp->sum_double[j] = v.type == NG_VALUE_DOUBLE;
                } else if (grp->sum_double[j] || v.type == NG_VALUE_DOUBLE) {
                    double cur =
                        grp->sum_double[j] ? grp->agg[j].as.real : (double)grp->agg[j].as.integer;
                    grp->agg[j].type = NG_VALUE_DOUBLE;
                    grp->agg[j].as.real =
                        cur + (v.type == NG_VALUE_DOUBLE ? v.as.real : (double)v.as.integer);
                    grp->sum_double[j] = 1;
                } else
                    grp->agg[j].as.integer += v.as.integer;
            } else if (p[j].aggregate == 3) {
                ng_value_list* l = (ng_value_list*)grp->agg[j].as.list;
                s = ng_cy_list_add(&l, &v);
                if (s != NG_OK)
                    return s;
                grp->agg[j].as.list = l;
                grp->agg[j].length = l->count;
            }
        }
    return NG_OK;
}
static ng_status ng_cy_build_groups(const ng_graph* g,
                                    ng_cy_query* q,
                                    ng_cy_row* rows,
                                    size_t row_count,
                                    const ng_cy_projection* p,
                                    size_t n,
                                    ng_cy_group** out,
                                    size_t* out_count) {
    ng_cy_group* groups = NULL;
    size_t count = 0, cap = 0, i, j;
    ng_status s;
    int has_group = 0;
    for (j = 0; j < n; j++)
        if (!p[j].aggregate)
            has_group = 1;
    if (!has_group && row_count == 0) {
        ng_cy_result_key empty;
        ng_cy_row erow;
        memset(&empty, 0, sizeof(empty));
        memset(&erow, 0, sizeof(erow));
        s = ng_cy_group_add(&groups, &count, &cap, &empty, p, n, &erow);
        if (s != NG_OK) {
            free(groups);
            return s;
        }
    }
    for (i = 0; i < row_count; i++) {
        ng_cy_result_key key;
        size_t gi;
        memset(&key, 0, sizeof(key));
        for (j = 0; j < n; j++)
            if (!p[j].aggregate) {
                s = ng_cy_eval_scalar(g, q, &rows[i], p[j].scalar_index, &key.values[j]);
                if (s != NG_OK) {
                    free(groups);
                    return s;
                }
            }
        for (gi = 0; gi < count; gi++)
            if (ng_cy_group_key_equal(&groups[gi], &key, p, n))
                break;
        if (gi == count) {
            s = ng_cy_group_add(&groups, &count, &cap, &key, p, n, &rows[i]);
            if (s != NG_OK) {
                free(groups);
                return s;
            }
        }
        s = ng_cy_aggregate_row(g, q, p, n, &rows[i], &groups[gi]);
        if (s != NG_OK) {
            free(groups);
            return s;
        }
    }
    *out = groups;
    *out_count = count;
    return NG_OK;
}
static void
ng_cy_group_key(const ng_cy_group* g, const ng_cy_projection* p, size_t n, ng_cy_result_key* out) {
    size_t j;
    memset(out, 0, sizeof(*out));
    for (j = 0; j < n; j++)
        out->values[j] = p[j].aggregate ? g->agg[j] : g->key.values[j];
}
static ng_status
ng_cy_activate_with_scope(ng_cy_query* q, ng_cy_projection* projs, size_t proj_count) {
    size_t i;
    ng_cy_scope_clear(q);
    for (i = 0; i < proj_count; i++) {
        projs[i].out_var_index = ng_cy_scope_project_var(q, projs[i].out_name, projs[i].out_kind);
        if (projs[i].out_var_index < 0)
            return NG_PARSE_ERROR;
    }
    return NG_OK;
}
static ng_status
ng_cy_activate_return_aliases(ng_cy_query* q, ng_cy_projection* projs, size_t proj_count) {
    size_t i;
    for (i = 0; i < proj_count; i++)
        if (projs[i].out_name[0]) {
            projs[i].out_var_index = ng_cy_var_index(q, projs[i].out_name, 3, 1);
            if (projs[i].out_var_index < 0)
                return NG_PARSE_ERROR;
        }
    return NG_OK;
}
static int ng_cy_projection_index_for_scalar(const ng_cy_query* q,
                                             const ng_cy_projection* p,
                                             size_t n,
                                             int scalar) {
    size_t i;
    const ng_cy_scalar* s;
    if (scalar < 0 || scalar >= q->scalar_count)
        return -1;
    s = &q->scalars[scalar];
    for (i = 0; i < n; i++) {
        if (p[i].scalar_index == scalar)
            return (int)i;
        if (s->kind == 1 && p[i].out_var_index >= 0 && s->var_index == p[i].out_var_index &&
            !s->key[0])
            return (int)i;
    }
    return -1;
}
static ng_status ng_cy_parse_order_list(const char** pp,
                                        ng_cy_query* q,
                                        const ng_cy_projection* projs,
                                        size_t proj_count,
                                        ng_cy_order* orders,
                                        size_t* order_count) {
    const char* p = ng_skip_ws(*pp);
    if (!ng_cy_clause_starts(p, "ORDER")) {
        *order_count = 0;
        return NG_OK;
    }
    p = ng_skip_ws(p + 5);
    if (strncmp(p, "BY", 2) || !isspace((unsigned char)p[2]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 2);
    *order_count = 0;
    for (;;) {
        ng_cy_order* o;
        int scalar;
        if (*order_count >= NG_CY_MAX_RETURNS)
            return NG_PARSE_ERROR;
        o = &orders[*order_count];
        memset(o, 0, sizeof(*o));
        o->proj_index = -1;
        if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
            return NG_PARSE_ERROR;
        o->scalar_index = scalar;
        o->proj_index = ng_cy_projection_index_for_scalar(q, projs, proj_count, scalar);
        p = ng_skip_ws(p);
        if (!strncmp(p, "ASC", 3)) {
            if (ng_ident_char((unsigned char)p[3]))
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 3);
        } else if (!strncmp(p, "DESC", 4)) {
            if (ng_ident_char((unsigned char)p[4]))
                return NG_PARSE_ERROR;
            o->desc = 1;
            p = ng_skip_ws(p + 4);
        }
        (*order_count)++;
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p)
            return NG_PARSE_ERROR;
    }
    *pp = p;
    return NG_OK;
}
static int ng_cy_value_compare_order(const ng_value* a, const ng_value* b, int desc) {
    int c;
    if (a->type == NG_VALUE_NULL || b->type == NG_VALUE_NULL) {
        if (a->type == NG_VALUE_NULL && b->type == NG_VALUE_NULL)
            c = 0;
        else
            c = a->type == NG_VALUE_NULL ? 1 : -1;
    } else if (!ng_compare_values(a, b, &c))
        c = (int)a->type - (int)b->type;
    return desc ? -c : c;
}
static const ng_graph* ng_cy_sort_graph;
static const ng_cy_query* ng_cy_sort_query;
static const ng_cy_order* ng_cy_sort_orders;
static size_t ng_cy_sort_order_count;
static int ng_cy_projected_row_compare(const void* a, const void* b) {
    const ng_cy_projected_row *x = (const ng_cy_projected_row*)a,
                              *y = (const ng_cy_projected_row*)b;
    size_t i;
    for (i = 0; i < ng_cy_sort_order_count; i++) {
        ng_value ax, bx;
        int c;
        ng_status sx, sy;
        if (ng_cy_sort_orders[i].proj_index >= 0) {
            ax = x->key.values[ng_cy_sort_orders[i].proj_index];
            bx = y->key.values[ng_cy_sort_orders[i].proj_index];
        } else {
            sx = ng_cy_eval_scalar(ng_cy_sort_graph,
                                   ng_cy_sort_query,
                                   &x->row,
                                   ng_cy_sort_orders[i].scalar_index,
                                   &ax);
            sy = ng_cy_eval_scalar(ng_cy_sort_graph,
                                   ng_cy_sort_query,
                                   &y->row,
                                   ng_cy_sort_orders[i].scalar_index,
                                   &bx);
            if (sx != NG_OK || sy != NG_OK)
                continue;
        }
        c = ng_cy_value_compare_order(&ax, &bx, ng_cy_sort_orders[i].desc);
        if (c)
            return c;
    }
    return 0;
}
static void ng_cy_sort_projected_rows(const ng_graph* g,
                                      const ng_cy_query* q,
                                      ng_cy_projected_row* rows,
                                      size_t count,
                                      const ng_cy_order* orders,
                                      size_t order_count) {
    if (!order_count || count < 2)
        return;
    ng_cy_sort_graph = g;
    ng_cy_sort_query = q;
    ng_cy_sort_orders = orders;
    ng_cy_sort_order_count = order_count;
    qsort(rows, count, sizeof(*rows), ng_cy_projected_row_compare);
}
static ng_status ng_cy_project_rows(const ng_graph* g,
                                    ng_cy_query* q,
                                    ng_cy_row* rows,
                                    size_t row_count,
                                    const ng_cy_projection* projs,
                                    size_t proj_count,
                                    int distinct,
                                    int preserve_original,
                                    ng_cy_projected_row** out,
                                    size_t* out_count) {
    ng_cy_projected_row* items = NULL;
    ng_cy_result_key* seen = NULL;
    size_t i, j, item_count = 0, item_cap = 0, seen_count = 0, seen_cap = 0;
    ng_status s;
    if (ng_cy_has_aggregate(projs, proj_count)) {
        ng_cy_group* groups = NULL;
        size_t group_count = 0;
        s = ng_cy_build_groups(g, q, rows, row_count, projs, proj_count, &groups, &group_count);
        if (s != NG_OK)
            return s;
        for (i = 0; i < group_count; i++) {
            ng_cy_projected_row item;
            ng_cy_group_key(&groups[i], projs, proj_count, &item.key);
            memset(&item.row, 0, sizeof(item.row));
            if (distinct) {
                if (ng_cy_projection_seen(seen, seen_count, &item.key, proj_count))
                    continue;
                if (!grow((void**)&seen, &seen_cap, seen_count + 1, sizeof(*seen))) {
                    free(groups);
                    free(seen);
                    free(items);
                    return NG_OOM;
                }
                seen[seen_count++] = item.key;
            }
            for (j = 0; j < proj_count; j++) {
                if (projs[j].out_var_index >= 0) {
                    if ((projs[j].out_kind == 1 || projs[j].out_kind == 2) && !projs[j].aggregate)
                        item.row.values[projs[j].out_var_index] = groups[i].passthrough[j];
                    else {
                        item.row.values[projs[j].out_var_index].kind = 3;
                        item.row.values[projs[j].out_var_index].value = item.key.values[j];
                    }
                }
            }
            if (!grow((void**)&items, &item_cap, item_count + 1, sizeof(*items))) {
                free(groups);
                free(seen);
                free(items);
                return NG_OOM;
            }
            items[item_count++] = item;
        }
        free(groups);
        free(seen);
        *out = items;
        *out_count = item_count;
        return NG_OK;
    }
    for (i = 0; i < row_count; i++) {
        ng_cy_projected_row item;
        memset(&item, 0, sizeof(item));
        if (preserve_original)
            item.row = rows[i];
        for (j = 0; j < proj_count; j++) {
            ng_value v;
            s = ng_cy_eval_scalar(g, q, &rows[i], projs[j].scalar_index, &v);
            if (s != NG_OK) {
                free(items);
                free(seen);
                return s;
            }
            item.key.values[j] = v;
            if (projs[j].out_var_index >= 0) {
                if (projs[j].out_kind == 1 || projs[j].out_kind == 2)
                    item.row.values[projs[j].out_var_index] = rows[i].values[projs[j].var_index];
                else {
                    item.row.values[projs[j].out_var_index].kind = 3;
                    item.row.values[projs[j].out_var_index].value = v;
                }
            }
        }
        if (distinct) {
            if (ng_cy_projection_seen(seen, seen_count, &item.key, proj_count))
                continue;
            if (!grow((void**)&seen, &seen_cap, seen_count + 1, sizeof(*seen))) {
                free(items);
                free(seen);
                return NG_OOM;
            }
            seen[seen_count++] = item.key;
        }
        if (!grow((void**)&items, &item_cap, item_count + 1, sizeof(*items))) {
            free(items);
            free(seen);
            return NG_OOM;
        }
        items[item_count++] = item;
    }
    free(seen);
    *out = items;
    *out_count = item_count;
    return NG_OK;
}
static ng_status ng_cy_apply_with_projected(const ng_graph* g,
                                            ng_cy_query* q,
                                            ng_cy_row** rows,
                                            size_t* row_count,
                                            const ng_cy_projection* projs,
                                            size_t proj_count,
                                            int distinct,
                                            const ng_cy_order* orders,
                                            size_t order_count,
                                            uint64_t skip,
                                            int has_skip,
                                            uint64_t limit,
                                            int has_limit) {
    ng_cy_projected_row* items = NULL;
    ng_cy_row* out = NULL;
    size_t item_count = 0, i, out_count = 0, out_cap = 0;
    ng_status s = ng_cy_project_rows(
        g, q, *rows, *row_count, projs, proj_count, distinct, 0, &items, &item_count);
    if (s != NG_OK)
        return s;
    ng_cy_sort_projected_rows(g, q, items, item_count, orders, order_count);
    for (i = 0; i < item_count; i++) {
        if (has_skip && i < skip)
            continue;
        if (has_limit && out_count >= limit)
            break;
        if (!grow((void**)&out, &out_cap, out_count + 1, sizeof(*out))) {
            free(items);
            free(out);
            return NG_OOM;
        }
        out[out_count++] = items[i].row;
    }
    free(items);
    free(*rows);
    *rows = out;
    *row_count = out_count;
    return NG_OK;
}
static ng_status ng_cy_emit_key(const ng_cy_result_key* key, size_t count, FILE* out) {
    size_t j;
    for (j = 0; j < count; j++) {
        if (j && fputc('\t', out) == EOF)
            return NG_IO_ERROR;
        if (!ng_print_value(out, &key->values[j]))
            return NG_IO_ERROR;
    }
    return fputc('\n', out) == EOF ? NG_IO_ERROR : NG_OK;
}
static int ng_query_schema_types_compatible(ng_value_type a, ng_value_type b) {
    if (a == b)
        return 1;
    return (a == NG_VALUE_INT64 || a == NG_VALUE_DOUBLE) &&
           (b == NG_VALUE_INT64 || b == NG_VALUE_DOUBLE);
}
static ng_value_type
ng_cy_projection_static_type(const ng_cy_query* q, const ng_cy_projection* projection, int* known) {
    const ng_cy_scalar* scalar;
    *known = 0;
    if (projection->aggregate == 1) {
        *known = 1;
        return NG_VALUE_INT64;
    }
    if (projection->aggregate == 3) {
        *known = 1;
        return NG_VALUE_LIST;
    }
    if (projection->scalar_index < 0)
        return NG_VALUE_NULL;
    scalar = &q->scalars[projection->scalar_index];
    if (projection->aggregate == 2)
        return NG_VALUE_NULL;
    if (scalar->kind == 0 && scalar->value.type != NG_VALUE_NULL &&
        scalar->value.type != NG_VALUE_PARAM) {
        *known = 1;
        return scalar->value.type;
    }
    if (scalar->kind == 1 && !strcmp(scalar->key, "id")) {
        *known = 1;
        return NG_VALUE_INT64;
    }
    return NG_VALUE_NULL;
}
static ng_status ng_cy_capture_schema(const ng_cy_query* q,
                                      const ng_cy_projection* projections,
                                      size_t projection_count,
                                      const ng_cy_projected_row* rows,
                                      size_t row_count) {
    size_t i, j;
    ng_query_schema* schema = ng_query_active_schema;
    if (!schema)
        return NG_OK;
    schema->valid = 1;
    if (schema->count && schema->count != projection_count)
        return NG_PARSE_ERROR;
    schema->count = projection_count;
    for (i = 0; i < projection_count; i++) {
        int known = 0;
        ng_value_type type = ng_cy_projection_static_type(q, &projections[i], &known);
        const char* name =
            projections[i].out_name[0] ? projections[i].out_name : projections[i].source;
        if (!schema->names[i][0]) {
            if (strlen(name) >= sizeof(schema->names[i]))
                return NG_PARSE_ERROR;
            strcpy(schema->names[i], name);
        } else if (strcmp(schema->names[i], name)) {
            return NG_PARSE_ERROR;
        }
        if (known) {
            if (schema->type_known[i] && !ng_query_schema_types_compatible(schema->types[i], type))
                return NG_PARSE_ERROR;
            schema->types[i] = type;
            schema->type_known[i] = 1;
        }
    }
    for (i = 0; i < row_count; i++)
        for (j = 0; j < projection_count; j++) {
            ng_value_type type = rows[i].key.values[j].type;
            if (type == NG_VALUE_NULL)
                continue;
            if (schema->type_known[j] && !ng_query_schema_types_compatible(schema->types[j], type))
                return NG_PARSE_ERROR;
            schema->types[j] = type;
            schema->type_known[j] = 1;
        }
    return NG_OK;
}
static ng_status ng_query_capture_legacy_schema(const ng_query_plan* plan) {
    ng_query_schema* schema = ng_query_active_schema;
    size_t i;
    if (!schema)
        return NG_OK;
    schema->valid = 1;
    schema->count = (size_t)plan->return_count;
    for (i = 0; i < (size_t)plan->return_count; i++) {
        int n;
        if (plan->return_has_aliases[i])
            n = snprintf(schema->names[i], sizeof(schema->names[i]), "%s", plan->return_aliases[i]);
        else if (plan->return_is_properties[i])
            n = snprintf(schema->names[i],
                         sizeof(schema->names[i]),
                         "%c.%s",
                         plan->return_vars[i],
                         plan->return_keys[i]);
        else
            n = snprintf(schema->names[i], sizeof(schema->names[i]), "%c", plan->return_vars[i]);
        if (n < 0 || (size_t)n >= sizeof(schema->names[i]))
            return NG_PARSE_ERROR;
        if (plan->return_is_ids[i]) {
            schema->types[i] = NG_VALUE_INT64;
            schema->type_known[i] = 1;
        }
    }
    return NG_OK;
}
static ng_status ng_cy_emit_rows(const ng_graph* g,
                                 ng_cy_query* q,
                                 ng_cy_row* rows,
                                 size_t row_count,
                                 const ng_cy_projection* ret,
                                 size_t ret_count,
                                 int distinct,
                                 const ng_cy_order* orders,
                                 size_t order_count,
                                 uint64_t skip,
                                 int has_skip,
                                 uint64_t limit,
                                 int has_limit,
                                 FILE* out) {
    ng_cy_projected_row* items = NULL;
    size_t item_count = 0, i, emitted = 0;
    ng_status s =
        ng_cy_project_rows(g, q, rows, row_count, ret, ret_count, distinct, 1, &items, &item_count);
    if (s != NG_OK)
        return s;
    s = ng_cy_capture_schema(q, ret, ret_count, items, item_count);
    if (s != NG_OK) {
        free(items);
        return s;
    }
    ng_cy_sort_projected_rows(g, q, items, item_count, orders, order_count);
    for (i = 0; i < item_count; i++) {
        if (has_skip && i < skip)
            continue;
        if (has_limit && emitted >= limit)
            break;
        s = ng_cy_emit_key(&items[i].key, ret_count, out);
        if (s != NG_OK) {
            free(items);
            return s;
        }
        emitted++;
    }
    free(items);
    return NG_OK;
}
static ng_status ng_cy_apply_create_to_rows(
    ng_graph* g, ng_cy_query* q, ng_cy_row** rows, size_t row_count, size_t start) {
    size_t i, j;
    ng_status s;
    for (i = 0; i < row_count; i++)
        for (j = start; j < q->match_count; j++) {
            s = ng_cy_execute_create_match(g, q, &q->matches[j], &(*rows)[i]);
            if (s != NG_OK)
                return s;
        }
    return NG_OK;
}
static int ng_cy_map_key_index(const ng_property* map, size_t count, ng_symbol_id key) {
    size_t i;
    for (i = 0; i < count; i++)
        if (map[i].key == key)
            return (int)i;
    return -1;
}
static ng_status ng_cy_apply_map_to_binding(ng_graph* g,
                                            ng_cy_binding b,
                                            const ng_property* map,
                                            size_t map_count,
                                            int replace,
                                            int* changed) {
    size_t i;
    ng_status s;
    if (!b.kind || b.kind == 3)
        return NG_PARSE_ERROR;
    if (b.kind == 1) {
        node_i* n = node(g, b.id);
        if (!n)
            return NG_NOT_FOUND;
        if (replace) {
            for (i = n->np; i > 0; i--) {
                int mi = ng_cy_map_key_index(map, map_count, n->p[i - 1].key);
                if (mi < 0 || map[mi].value.type == NG_VALUE_NULL) {
                    s = ng_node_unset(g, b.id, n->p[i - 1].key);
                    if (s != NG_OK)
                        return s;
                }
            }
        }
    } else if (b.kind == 2) {
        rel_i* r = NULL;
        for (i = 0; i < g->nr; i++)
            if (g->re[i].id == b.id) {
                r = &g->re[i];
                break;
            }
        if (!r)
            return NG_NOT_FOUND;
        if (replace) {
            for (i = r->np; i > 0; i--) {
                int mi = ng_cy_map_key_index(map, map_count, r->p[i - 1].key);
                if (mi < 0 || map[mi].value.type == NG_VALUE_NULL) {
                    s = ng_relationship_unset(g, b.id, r->p[i - 1].key);
                    if (s != NG_OK)
                        return s;
                }
            }
        }
    } else
        return NG_PARSE_ERROR;
    for (i = 0; i < map_count; i++) {
        if (map[i].value.type == NG_VALUE_NULL) {
            if (b.kind == 1)
                s = ng_node_unset(g, b.id, map[i].key);
            else
                s = ng_relationship_unset(g, b.id, map[i].key);
            if (s != NG_OK && s != NG_NOT_FOUND)
                return s;
        } else if (b.kind == 1)
            s = ng_node_set(g, b.id, map[i].key, &map[i].value);
        else
            s = ng_relationship_set(g, b.id, map[i].key, &map[i].value);
        if (s != NG_OK)
            return s;
        if (changed)
            *changed = 1;
    }
    return NG_OK;
}
static ng_status ng_cy_apply_set_to_rows(
    ng_graph* g, ng_cy_query* q, ng_cy_row* rows, size_t row_count, const char** pp, int* changed) {
    const char* p = ng_skip_ws(*pp + 3);
    ng_status s;
    for (;;) {
        char name[64], key_text[128];
        ng_symbol_id key;
        int vi;
        size_t i, n;
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
            return NG_PARSE_ERROR;
        vi = ng_cy_var_lookup(q, name);
        if (vi < 0)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p == '=' || (p[0] == '+' && p[1] == '=')) {
            int replace = *p == '=';
            ng_query_prop props[NG_QUERY_MAX_PROPS];
            int prop_scalars[NG_QUERY_MAX_PROPS];
            ng_property map[NG_QUERY_MAX_PROPS];
            size_t prop_count = 0;
            if (replace)
                p++;
            else
                p += 2;
            p = ng_skip_ws(p);
            if (ng_cy_parse_expr_map(&p, q, props, prop_scalars, &prop_count) != NG_OK)
                return NG_PARSE_ERROR;
            for (i = 0; i < prop_count; i++) {
                if (ng_symbol(g, props[i].key, &map[i].key) != NG_OK)
                    return NG_OOM;
            }
            for (i = 0; i < row_count; i++) {
                size_t property_index;
                for (property_index = 0; property_index < prop_count; property_index++) {
                    s = ng_cy_eval_scalar(
                        g, q, &rows[i], prop_scalars[property_index], &map[property_index].value);
                    if (s != NG_OK) {
                        while (property_index > 0) {
                            property_index--;
                            if (q->scalars[prop_scalars[property_index]].kind == 7 ||
                                q->scalars[prop_scalars[property_index]].kind == 8)
                                valfree(&map[property_index].value);
                        }
                        return s;
                    }
                }
                s = ng_cy_apply_map_to_binding(
                    g, rows[i].values[vi], map, prop_count, replace, changed);
                for (property_index = 0; property_index < prop_count; property_index++)
                    if (q->scalars[prop_scalars[property_index]].kind == 7 ||
                        q->scalars[prop_scalars[property_index]].kind == 8)
                        valfree(&map[property_index].value);
                if (s != NG_OK)
                    return s;
            }
        } else {
            if (*p != '.')
                return NG_PARSE_ERROR;
            p++;
            for (n = 0; ng_ident_char((unsigned char)p[n]); n++)
                ;
            if (!n || n >= sizeof(key_text))
                return NG_PARSE_ERROR;
            memcpy(key_text, p, n);
            key_text[n] = 0;
            p = ng_skip_ws(p + n);
            if (*p != '=')
                return NG_PARSE_ERROR;
            p = ng_skip_ws(p + 1);
            {
                int scalar;
                if (ng_cy_parse_scalar_add(&p, q, &scalar) != NG_OK)
                    return NG_PARSE_ERROR;
                if (ng_symbol(g, key_text, &key) != NG_OK)
                    return NG_OOM;
                for (i = 0; i < row_count; i++) {
                    ng_cy_binding b = rows[i].values[vi];
                    ng_value v;
                    if (!b.kind || b.kind == 3)
                        return NG_PARSE_ERROR;
                    s = ng_cy_eval_scalar(g, q, &rows[i], scalar, &v);
                    if (s != NG_OK)
                        return s;
                    if (b.kind == 1)
                        s = ng_node_set(g, b.id, key, &v);
                    else if (b.kind == 2)
                        s = ng_relationship_set(g, b.id, key, &v);
                    else
                        return NG_PARSE_ERROR;
                    if (s != NG_OK)
                        return s;
                    if (changed)
                        *changed = 1;
                }
            }
        }
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p || ng_cy_clause_starts(p, "RETURN") || ng_cy_clause_starts(p, "WITH") ||
            ng_cy_clause_starts(p, "MATCH") || ng_cy_clause_starts(p, "OPTIONAL") ||
            ng_cy_clause_starts(p, "DELETE") || ng_cy_clause_starts(p, "DETACH") ||
            ng_cy_clause_starts(p, "REMOVE") || ng_cy_clause_starts(p, "SET") ||
            ng_cy_clause_starts(p, "CREATE") || ng_cy_clause_starts(p, "MERGE"))
            return NG_PARSE_ERROR;
    }
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_apply_delete_to_rows(
    ng_graph* g, ng_cy_query* q, ng_cy_row* rows, size_t row_count, const char** pp, int* changed) {
    const char* p = ng_skip_ws(*pp + 6);
    ng_id *nodes = NULL, *rels = NULL;
    size_t node_count = 0, node_cap = 0, rel_count = 0, rel_cap = 0, i, j;
    ng_status s;
    for (;;) {
        char name[64];
        int vi;
        if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK) {
            free(nodes);
            free(rels);
            return NG_PARSE_ERROR;
        }
        vi = ng_cy_var_lookup(q, name);
        if (vi < 0) {
            free(nodes);
            free(rels);
            return NG_PARSE_ERROR;
        }
        for (i = 0; i < row_count; i++) {
            ng_cy_binding b = rows[i].values[vi];
            int seen = 0;
            if (!b.kind || b.kind == 3) {
                free(nodes);
                free(rels);
                return NG_PARSE_ERROR;
            }
            if (b.kind == 1) {
                for (j = 0; j < node_count; j++)
                    if (nodes[j] == b.id) {
                        seen = 1;
                        break;
                    }
                if (!seen) {
                    if (!grow((void**)&nodes, &node_cap, node_count + 1, sizeof(*nodes))) {
                        free(nodes);
                        free(rels);
                        return NG_OOM;
                    }
                    nodes[node_count++] = b.id;
                }
            } else if (b.kind == 2) {
                for (j = 0; j < rel_count; j++)
                    if (rels[j] == b.id) {
                        seen = 1;
                        break;
                    }
                if (!seen) {
                    if (!grow((void**)&rels, &rel_cap, rel_count + 1, sizeof(*rels))) {
                        free(nodes);
                        free(rels);
                        return NG_OOM;
                    }
                    rels[rel_count++] = b.id;
                }
            } else {
                free(nodes);
                free(rels);
                return NG_PARSE_ERROR;
            }
        }
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p || ng_cy_clause_starts(p, "RETURN") || ng_cy_clause_starts(p, "WITH") ||
            ng_cy_clause_starts(p, "MATCH") || ng_cy_clause_starts(p, "OPTIONAL") ||
            ng_cy_clause_starts(p, "DELETE") || ng_cy_clause_starts(p, "SET") ||
            ng_cy_clause_starts(p, "CREATE") || ng_cy_clause_starts(p, "MERGE")) {
            free(nodes);
            free(rels);
            return NG_PARSE_ERROR;
        }
    }
    for (i = 0; i < rel_count; i++) {
        s = ng_relationship_delete(g, rels[i]);
        if (s != NG_OK && s != NG_NOT_FOUND) {
            free(nodes);
            free(rels);
            return s;
        }
    }
    for (i = 0; i < node_count; i++) {
        s = ng_node_delete(g, nodes[i]);
        if (s != NG_OK && s != NG_NOT_FOUND) {
            free(nodes);
            free(rels);
            return s;
        }
    }
    if (changed && (node_count || rel_count))
        *changed = 1;
    free(nodes);
    free(rels);
    *pp = ng_skip_ws(p);
    return NG_OK;
}
static ng_status ng_cy_merge_node_from_pattern(ng_graph* g,
                                               ng_cy_query* q,
                                               const ng_cy_node_pat* pat,
                                               ng_cy_row* row,
                                               ng_node_id* out_id,
                                               int* changed) {
    ng_property props[NG_QUERY_MAX_PROPS];
    ng_symbol_id label = 0;
    size_t i;
    ng_status s;
    if (pat->var_index >= 0 && row->values[pat->var_index].kind) {
        node_i* n;
        if (row->values[pat->var_index].kind != 1)
            return NG_PARSE_ERROR;
        n = node(g, row->values[pat->var_index].id);
        if (!n)
            return NG_NOT_FOUND;
        if (pat->label[0]) {
            label = ng_symbol_id_by_text(g, pat->label);
            if (!label || !ng_query_label_matches(n, label))
                return NG_PARSE_ERROR;
        }
        s = ng_cy_props_to_symbols_row(
            g, q, pat->props, pat->prop_scalars, pat->prop_count, row, props);
        if (s != NG_OK)
            return s;
        if (!ng_query_node_matches_props(n, props, pat->prop_count))
            return NG_PARSE_ERROR;
        *out_id = n->id;
        return NG_OK;
    }
    if (!pat->label[0] && !pat->prop_count)
        return NG_PARSE_ERROR;
    if (pat->label[0] && ng_symbol(g, pat->label, &label) != NG_OK)
        return NG_OOM;
    s = ng_cy_props_to_symbols_row(
        g, q, pat->props, pat->prop_scalars, pat->prop_count, row, props);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++)
        if (ng_query_label_matches(&g->no[i], label) &&
            ng_query_node_matches_props(&g->no[i], props, pat->prop_count)) {
            *out_id = g->no[i].id;
            if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 1, *out_id))
                return NG_PARSE_ERROR;
            return NG_OK;
        }
    s = ng_node_create_with_properties(
        g, label ? &label : NULL, label ? 1 : 0, props, pat->prop_count, out_id);
    if (s != NG_OK)
        return s;
    if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 1, *out_id))
        return NG_PARSE_ERROR;
    if (changed)
        *changed = 1;
    return NG_OK;
}
static ng_status ng_cy_merge_relationship_from_pattern(ng_graph* g,
                                                       ng_cy_query* q,
                                                       const ng_cy_rel_pat* pat,
                                                       ng_node_id left,
                                                       ng_node_id right,
                                                       ng_cy_row* row,
                                                       int* changed) {
    ng_property props[NG_QUERY_MAX_PROPS];
    ng_symbol_id type = 0;
    ng_node_id src, dst;
    size_t i, pi;
    ng_relationship_id id;
    ng_status s;
    if (pat->dir == 0 || !pat->type[0] || pat->has_var_length)
        return NG_PARSE_ERROR;
    if (ng_symbol(g, pat->type, &type) != NG_OK)
        return NG_OOM;
    s = ng_cy_props_to_symbols_row(
        g, q, pat->props, pat->prop_scalars, pat->prop_count, row, props);
    if (s != NG_OK)
        return s;
    src = pat->dir < 0 ? right : left;
    dst = pat->dir < 0 ? left : right;
    if (pat->var_index >= 0 && row->values[pat->var_index].kind) {
        rel_i* r = NULL;
        if (row->values[pat->var_index].kind != 2)
            return NG_PARSE_ERROR;
        for (i = 0; i < g->nr; i++)
            if (g->re[i].id == row->values[pat->var_index].id) {
                r = &g->re[i];
                break;
            }
        if (!r || r->src != src || r->dst != dst || r->type != type ||
            !ng_query_rel_matches_props(r, props, pat->prop_count))
            return NG_PARSE_ERROR;
        return NG_OK;
    }
    for (i = 0; i < g->nr; i++)
        if (g->re[i].src == src && g->re[i].dst == dst && g->re[i].type == type &&
            ng_query_rel_matches_props(&g->re[i], props, pat->prop_count)) {
            if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 2, g->re[i].id))
                return NG_PARSE_ERROR;
            return NG_OK;
        }
    s = ng_relationship_create(g, src, type, dst, &id);
    if (s != NG_OK)
        return s;
    for (pi = 0; pi < pat->prop_count; pi++) {
        s = ng_relationship_set(g, id, props[pi].key, &props[pi].value);
        if (s != NG_OK)
            return s;
    }
    if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 2, id))
        return NG_PARSE_ERROR;
    if (changed)
        *changed = 1;
    return NG_OK;
}
static ng_status ng_cy_execute_merge_match(
    ng_graph* g, ng_cy_query* q, const ng_cy_match* m, ng_cy_row* row, int* changed, int* created) {
    ng_node_id ids[NG_CY_MAX_NODES];
    size_t i;
    ng_status s;
    int local_changed = 0;
    memset(ids, 0, sizeof(ids));
    for (i = 0; i < m->node_count; i++) {
        s = ng_cy_merge_node_from_pattern(g, q, &m->nodes[i], row, &ids[i], &local_changed);
        if (s != NG_OK)
            return s;
    }
    for (i = 0; i < m->rel_count; i++) {
        s = ng_cy_merge_relationship_from_pattern(
            g, q, &m->rels[i], ids[i], ids[i + 1], row, &local_changed);
        if (s != NG_OK)
            return s;
    }
    if (local_changed) {
        if (changed)
            *changed = 1;
        if (created)
            *created = 1;
    }
    return NG_OK;
}
static const char* ng_cy_merge_action_end(const char* p) {
    const char* end = p + strlen(p);
    const char* candidate;
    const char* words[] = {" ON CREATE ", " ON MATCH ", " RETURN ", " WITH ", " WHERE "};
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        candidate = strstr(p, words[i]);
        if (candidate && candidate < end)
            end = candidate;
    }
    return end;
}
static ng_status ng_cy_copy_merge_action(char* out, size_t capacity, const char* start, const char* end) {
    size_t length;
    while (end > start && isspace((unsigned char)end[-1]))
        end--;
    length = (size_t)(end - start);
    if (!length || length >= capacity)
        return NG_PARSE_ERROR;
    memcpy(out, start, length);
    out[length] = 0;
    return NG_OK;
}
static ng_status ng_cy_apply_merge_action(
    ng_graph* g, ng_cy_query* q, ng_cy_row* row, const char* action, int* changed) {
    const char* p = action;
    size_t saved_scalars;
    ng_status s;
    if (!action[0])
        return NG_OK;
    saved_scalars = q->scalar_count;
    s = ng_cy_apply_set_to_rows(g, q, row, 1, &p, changed);
    q->scalar_count = saved_scalars;
    if (s != NG_OK || *ng_skip_ws(p))
        return s == NG_OK ? NG_PARSE_ERROR : s;
    return NG_OK;
}
static ng_status ng_cy_apply_merge_to_rows(ng_graph* g,
                                           ng_cy_query* q,
                                           ng_cy_row** rows,
                                           size_t row_count,
                                           const char** pp,
                                           int* changed) {
    size_t before = q->match_count, i, j;
    ng_status s;
    const char* p = ng_skip_ws(*pp + 5);
    q->create_mode = 1;
    if (ng_cy_parse_create_pattern(&p, q) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p || ng_cy_clause_starts(p, "RETURN") || ng_cy_clause_starts(p, "WITH") ||
            ng_cy_clause_starts(p, "MATCH") || ng_cy_clause_starts(p, "OPTIONAL") ||
            ng_cy_clause_starts(p, "DELETE") || ng_cy_clause_starts(p, "SET") ||
            ng_cy_clause_starts(p, "CREATE") || ng_cy_clause_starts(p, "MERGE") || *p == ',')
            return NG_PARSE_ERROR;
        if (ng_cy_parse_create_pattern(&p, q) != NG_OK)
            return NG_PARSE_ERROR;
    }
    q->create_mode = 0;
    q->merge_on_create[0] = 0;
    q->merge_on_match[0] = 0;
    for (;;) {
        const char* action_start;
        const char* action_end;
        if (!strncmp(p, "ON CREATE", 9) && isspace((unsigned char)p[9])) {
            action_start = ng_skip_ws(p + 9);
            if (strncmp(action_start, "SET", 3) || !isspace((unsigned char)action_start[3]))
                return NG_PARSE_ERROR;
            action_end = ng_cy_merge_action_end(action_start);
            if (ng_cy_copy_merge_action(q->merge_on_create,
                                        sizeof(q->merge_on_create),
                                        action_start,
                                        action_end) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(action_end);
        } else if (!strncmp(p, "ON MATCH", 8) && isspace((unsigned char)p[8])) {
            action_start = ng_skip_ws(p + 8);
            if (strncmp(action_start, "SET", 3) || !isspace((unsigned char)action_start[3]))
                return NG_PARSE_ERROR;
            action_end = ng_cy_merge_action_end(action_start);
            if (ng_cy_copy_merge_action(q->merge_on_match,
                                        sizeof(q->merge_on_match),
                                        action_start,
                                        action_end) != NG_OK)
                return NG_PARSE_ERROR;
            p = ng_skip_ws(action_end);
        } else
            break;
    }
    for (i = 0; i < row_count; i++)
        for (j = before; j < q->match_count; j++) {
            int created = 0;
            s = ng_cy_execute_merge_match(g, q, &q->matches[j], &(*rows)[i], changed, &created);
            if (s != NG_OK)
                return s;
            s = ng_cy_apply_merge_action(
                g, q, &(*rows)[i], created ? q->merge_on_create : q->merge_on_match, changed);
            if (s != NG_OK)
                return s;
        }
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_apply_random_walk(
    const ng_graph* g, ng_cy_query* q, ng_cy_row** rows, size_t* row_count, const char** pp) {
    const char* p = ng_skip_ws(*pp + 4);
    char name[64], yield_name[64];
    uint64_t steps = 0, seed = 0;
    int source, output;
    size_t i, j, path_count, cap = 0, out_count = 0;
    ng_cy_row* out = NULL;
    ng_node_id* path = NULL;
    ng_random_walk_options options;
    ng_status st;
    if (strncmp(p, "randomWalk", 10) || ng_ident_char((unsigned char)p[10]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 10);
    if (*p != 40)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (ng_cy_parse_ident(&p, name, sizeof(name)) != NG_OK)
        return NG_PARSE_ERROR;
    source = ng_cy_var_lookup(q, name);
    if (source < 0 || q->vars[source].kind != 1)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != 44)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (ng_query_parse_uint64(&p, &steps) != NG_OK || steps > UINT32_MAX)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p == 44) {
        p = ng_skip_ws(p + 1);
        if (ng_query_parse_uint64(&p, &seed) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
    }
    if (*p != 41)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (strncmp(p, "YIELD", 5) || !isspace((unsigned char)p[5]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 5);
    if (ng_cy_parse_ident(&p, yield_name, sizeof(yield_name)) != NG_OK)
        return NG_PARSE_ERROR;
    output = ng_cy_var_index(q, yield_name, 1, 1);
    if (output < 0)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p && !ng_cy_clause_starts(p, "RETURN") && !ng_cy_clause_starts(p, "WITH") &&
        !ng_cy_clause_starts(p, "MATCH") && !ng_cy_clause_starts(p, "OPTIONAL") &&
        !ng_cy_clause_starts(p, "WHERE") && !ng_cy_clause_starts(p, "ORDER"))
        return NG_PARSE_ERROR;
    options.direction = NG_DIRECTION_OUTGOING;
    options.type = 0;
    options.max_steps = (uint32_t)steps;
    options.seed = seed;
    path = (ng_node_id*)malloc(((size_t)steps + 1) * sizeof(*path));
    if (steps + 1 && !path)
        return NG_OOM;
    for (i = 0; i < *row_count; i++) {
        ng_cy_binding b = (*rows)[i].values[source];
        if (b.kind != 1) {
            free(path);
            free(out);
            return NG_PARSE_ERROR;
        }
        st = ng_random_walk(g, b.id, &options, path, (size_t)steps + 1, &path_count);
        if (st != NG_OK) {
            free(path);
            free(out);
            return st;
        }
        for (j = 0; j < path_count; j++) {
            ng_cy_row nr = (*rows)[i];
            if (!ng_cy_bind(&nr, output, 1, path[j])) {
                free(path);
                free(out);
                return NG_PARSE_ERROR;
            }
            if (!ng_cy_append_row(&out, &out_count, &cap, &nr)) {
                free(path);
                free(out);
                return NG_OOM;
            }
        }
    }
    free(path);
    free(*rows);
    *rows = out;
    *row_count = out_count;
    *pp = p;
    return NG_OK;
}
static procedure_i* ng_find_procedure(ng_graph* g, const char* name) {
    size_t i;
    for (i = 0; i < g->procedure_count; i++)
        if (!strcmp(g->procedures[i].name, name))
            return &g->procedures[i];
    return NULL;
}
static ng_status ng_cy_eval_procedure_argument(const ng_graph* g,
                                               const ng_cy_query* q,
                                               const ng_cy_row* row,
                                               int scalar_index,
                                               ng_procedure_argument* out) {
    const ng_cy_scalar* scalar;
    if (scalar_index < 0 || scalar_index >= q->scalar_count || !out)
        return NG_PARSE_ERROR;
    scalar = &q->scalars[scalar_index];
    memset(out, 0, sizeof(*out));
    if (scalar->kind == 1 && scalar->direct_binding) {
        const ng_cy_binding binding = row->values[scalar->var_index];
        if (!binding.kind || binding.kind == 3)
            return NG_PARSE_ERROR;
        out->kind = binding.kind == 1 ? NG_PROCEDURE_NODE : NG_PROCEDURE_RELATIONSHIP;
        out->id = binding.id;
        return NG_OK;
    }
    out->kind = NG_PROCEDURE_SCALAR;
    return ng_cy_eval_scalar(g, q, row, scalar_index, &out->value);
}
static ng_status ng_cy_apply_registered_procedure(
    ng_graph* g, ng_cy_query* q, ng_cy_row** rows, size_t* row_count, const char** pp) {
    const char* p = ng_skip_ws(*pp + 4);
    char procedure_name[128];
    int arguments[NG_CY_MAX_RETURNS];
    char yield_names[NG_CY_MAX_RETURNS][64];
    char yield_aliases[NG_CY_MAX_RETURNS][64];
    int yield_indices[NG_CY_MAX_RETURNS];
    ng_cy_row* output = NULL;
    size_t argument_count = 0, yield_count = 0, output_count = 0, output_capacity = 0, i;
    procedure_i* procedure;
    if (ng_cy_parse_ident(&p, procedure_name, sizeof(procedure_name)) != NG_OK)
        return NG_PARSE_ERROR;
    if (!strcmp(procedure_name, "randomWalk"))
        return ng_cy_apply_random_walk(g, q, rows, row_count, pp);
    procedure = ng_find_procedure(g, procedure_name);
    if (!procedure)
        return NG_NOT_FOUND;
    p = ng_skip_ws(p);
    if (*p != '(')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (*p != ')')
        for (;;) {
            if (argument_count >= NG_CY_MAX_RETURNS ||
                ng_cy_parse_scalar_add(&p, q, &arguments[argument_count]) != NG_OK)
                return NG_PARSE_ERROR;
            argument_count++;
            p = ng_skip_ws(p);
            if (*p != ',')
                break;
            p = ng_skip_ws(p + 1);
        }
    if (*p != ')')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (!ng_cy_clause_starts(p, "YIELD"))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 5);
    for (;;) {
        if (yield_count >= NG_CY_MAX_RETURNS ||
            ng_cy_parse_ident(&p, yield_names[yield_count], sizeof(yield_names[0])) != NG_OK)
            return NG_PARSE_ERROR;
        strcpy(yield_aliases[yield_count], yield_names[yield_count]);
        p = ng_skip_ws(p);
        if (ng_cy_clause_starts(p, "AS")) {
            p = ng_skip_ws(p + 2);
            if (ng_cy_parse_ident(&p, yield_aliases[yield_count], sizeof(yield_aliases[0])) !=
                NG_OK)
                return NG_PARSE_ERROR;
        }
        yield_indices[yield_count] = ng_cy_var_index(q, yield_aliases[yield_count], 3, 1);
        if (yield_indices[yield_count] < 0)
            return NG_PARSE_ERROR;
        yield_count++;
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
    }
    *pp = p;
    for (i = 0; i < *row_count; i++) {
        ng_procedure_argument values[NG_CY_MAX_RETURNS];
        ng_procedure_field fields[NG_CY_MAX_RETURNS];
        ng_procedure_result result;
        ng_cy_row input = (*rows)[i];
        size_t j;
        for (j = 0; j < argument_count; j++) {
            ng_status s = ng_cy_eval_procedure_argument(g, q, &input, arguments[j], &values[j]);
            if (s != NG_OK) {
                free(output);
                return s;
            }
        }
        memset(fields, 0, sizeof(fields));
        result.fields = fields;
        result.field_capacity = NG_CY_MAX_RETURNS;
        result.field_count = 0;
        if (procedure->handler(g, values, argument_count, &result, procedure->context) != NG_OK) {
            free(output);
            return NG_PARSE_ERROR;
        }
        if (result.field_count > result.field_capacity) {
            free(output);
            return NG_LIMIT;
        }
        for (j = 0; j < result.field_count; j++) {
            size_t k;
            if (!result.fields[j].name || !result.fields[j].name[0] ||
                result.fields[j].kind < NG_PROCEDURE_SCALAR ||
                result.fields[j].kind > NG_PROCEDURE_RELATIONSHIP) {
                free(output);
                return NG_PARSE_ERROR;
            }
            for (k = 0; k < j; k++)
                if (!strcmp(result.fields[k].name, result.fields[j].name)) {
                    free(output);
                    return NG_PARSE_ERROR;
                }
            if (result.fields[j].kind == NG_PROCEDURE_SCALAR &&
                !ng_valid_value(&result.fields[j].value)) {
                free(output);
                return NG_PARSE_ERROR;
            }
            if (result.fields[j].kind == NG_PROCEDURE_NODE &&
                !node(g, result.fields[j].id)) {
                free(output);
                return NG_NOT_FOUND;
            }
            if (result.fields[j].kind == NG_PROCEDURE_RELATIONSHIP) {
                size_t relationship_index;
                for (relationship_index = 0; relationship_index < g->nr; relationship_index++)
                    if (g->re[relationship_index].id == result.fields[j].id)
                        break;
                if (relationship_index == g->nr) {
                    free(output);
                    return NG_NOT_FOUND;
                }
            }
        }
        for (j = 0; j < yield_count; j++) {
            size_t field_index;
            for (field_index = 0; field_index < result.field_count; field_index++)
                if (!strcmp(result.fields[field_index].name, yield_names[j]))
                    break;
            if (field_index == result.field_count) {
                free(output);
                return NG_PARSE_ERROR;
            }
            input.values[yield_indices[j]].kind =
                result.fields[field_index].kind == NG_PROCEDURE_SCALAR
                    ? 3
                    : (int)result.fields[field_index].kind;
            input.values[yield_indices[j]].id = result.fields[field_index].id;
            input.values[yield_indices[j]].value = result.fields[field_index].value;
        }
        if (!ng_cy_append_row(&output, &output_count, &output_capacity, &input)) {
            free(output);
            return output_count >= NG_CY_MAX_ROWS ? NG_LIMIT : NG_OOM;
        }
    }
    free(*rows);
    *rows = output;
    *row_count = output_count;
    return NG_OK;
}
static ng_status
ng_query_execute_with(ng_graph* g, const char* q, FILE* out, int* mutated, int* handled) {
    const char* p = ng_skip_ws(q);
    ng_cy_query cy;
    ng_cy_row *rows = NULL, *next = NULL;
    size_t row_count = 1, next_count = 0;
    ng_status s = NG_OK;
    int did_write = 0, last_write = 0;
    if (handled)
        *handled = 0;
    if (!ng_cy_has_with(p) && !strstr(p, " CALL ") && !strstr(p, " CALL\t"))
        return NG_OK;
    if (handled)
        *handled = 1;
    memset(&cy, 0, sizeof(cy));
    cy.where_root = -1;
    rows = calloc(1, sizeof(*rows));
    if (!rows)
        return NG_OOM;
    for (;;) {
        last_write = 0;
        p = ng_skip_ws(p);
        if (ng_cy_clause_starts(p, "MATCH")) {
            size_t mi = cy.match_count;
            if (ng_cy_parse_match_clause(&p, &cy) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            s = ng_cy_apply_match(g, &cy, &cy.matches[mi], rows, row_count, &next, &next_count);
            free(rows);
            rows = next;
            row_count = next_count;
            next = NULL;
            next_count = 0;
            if (s != NG_OK)
                break;
            if (cy.has_where) {
                s = ng_cy_apply_where(g, &cy, rows, &row_count, cy.where_root);
                if (s != NG_OK)
                    break;
            }
        } else if (ng_cy_clause_starts(p, "OPTIONAL")) {
            size_t mi = cy.match_count;
            int old_root = cy.where_root, old_has = cy.has_where, where_root = -1;
            const char* op = p;
            p = ng_skip_ws(p + 8);
            if (!ng_cy_clause_starts(p, "MATCH")) {
                s = NG_PARSE_ERROR;
                break;
            }
            if (ng_cy_parse_match_pattern(&p, &cy, "MATCH") != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            if (cy.has_where)
                where_root = cy.where_root;
            p = ng_skip_ws(p);
            if (ng_cy_clause_starts(p, "WHERE")) {
                int root;
                p = ng_skip_ws(p + 5);
                if (ng_cy_parse_or(&p, &cy, &root) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                where_root = where_root >= 0 ? ng_cy_expr_add(&cy, 1, where_root, root, -1) : root;
                if (where_root < 0) {
                    s = NG_PARSE_ERROR;
                    break;
                }
            }
            cy.where_root = old_root;
            cy.has_where = old_has;
            s = ng_cy_apply_optional_match(
                g, &cy, &cy.matches[mi], rows, row_count, where_root, &next, &next_count);
            free(rows);
            rows = next;
            row_count = next_count;
            next = NULL;
            next_count = 0;
            if (s != NG_OK)
                break;
            (void)op;
        } else if (ng_cy_clause_starts(p, "UNWIND")) {
            s = ng_cy_apply_unwind(g, &cy, &rows, &row_count, &p);
            if (s != NG_OK)
                break;
        } else if (ng_cy_clause_starts(p, "WHERE")) {
            int root;
            p = ng_skip_ws(p + 5);
            if (ng_cy_parse_or(&p, &cy, &root) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            s = ng_cy_apply_where(g, &cy, rows, &row_count, root);
            if (s != NG_OK)
                break;
        } else if (ng_cy_clause_starts(p, "CALL")) {
            s = ng_cy_apply_registered_procedure(g, &cy, &rows, &row_count, &p);
            if (s != NG_OK)
                break;
        } else if (ng_cy_clause_starts(p, "REMOVE")) {
            s = ng_cy_apply_remove_to_rows(g, &cy, rows, row_count, &p, &did_write);
            if (s != NG_OK)
                break;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "DETACH")) {
            if (!ng_cy_clause_starts(p, "DETACH")) {
                s = NG_PARSE_ERROR;
                break;
            }
            p = ng_skip_ws(p + 6);
            if (!ng_cy_clause_starts(p, "DELETE")) {
                s = NG_PARSE_ERROR;
                break;
            }
            s = ng_cy_apply_delete_to_rows(g, &cy, rows, row_count, &p, &did_write);
            if (s != NG_OK)
                break;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "WITH")) {
            ng_cy_projection projs[NG_CY_MAX_RETURNS];
            ng_cy_order orders[NG_CY_MAX_RETURNS];
            size_t count = 0, order_count = 0;
            int distinct = 0, has_skip = 0, has_limit = 0;
            uint64_t skip = 0, limit = 0;
            if (ng_cy_parse_projection_list(&p, &cy, "WITH", projs, &count, &distinct, 1) !=
                NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            if (ng_cy_activate_with_scope(&cy, projs, count) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            p = ng_skip_ws(p);
            if (ng_cy_parse_order_list(&p, &cy, projs, count, orders, &order_count) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            p = ng_skip_ws(p);
            if (ng_cy_clause_starts(p, "SKIP")) {
                p = ng_skip_ws(p + 4);
                if (ng_query_parse_uint64(&p, &skip) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                has_skip = 1;
            }
            p = ng_skip_ws(p);
            if (ng_cy_clause_starts(p, "LIMIT")) {
                p = ng_skip_ws(p + 5);
                if (ng_query_parse_uint64(&p, &limit) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                has_limit = 1;
            }
            s = ng_cy_apply_with_projected(g,
                                           &cy,
                                           &rows,
                                           &row_count,
                                           projs,
                                           count,
                                           distinct,
                                           orders,
                                           order_count,
                                           skip,
                                           has_skip,
                                           limit,
                                           has_limit);
            if (s != NG_OK)
                break;
        } else if (ng_cy_clause_starts(p, "CREATE")) {
            size_t before = cy.match_count;
            cy.create_mode = 1;
            p = ng_skip_ws(p + 6);
            if (ng_cy_parse_create_pattern(&p, &cy) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            for (;;) {
                p = ng_skip_ws(p);
                if (*p != ',')
                    break;
                p = ng_skip_ws(p + 1);
                if (!*p || ng_cy_clause_starts(p, "RETURN") || *p == ',') {
                    s = NG_PARSE_ERROR;
                    break;
                }
                if (ng_cy_parse_create_pattern(&p, &cy) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
            }
            if (s != NG_OK)
                break;
            cy.create_mode = 0;
            s = ng_cy_apply_create_to_rows(g, &cy, &rows, row_count, before);
            if (s != NG_OK)
                break;
            if (cy.match_count > before)
                did_write = 1;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "SET")) {
            s = ng_cy_apply_set_to_rows(g, &cy, rows, row_count, &p, &did_write);
            if (s != NG_OK)
                break;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "DELETE")) {
            s = ng_cy_apply_delete_to_rows(g, &cy, rows, row_count, &p, &did_write);
            if (s != NG_OK)
                break;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "MERGE")) {
            s = ng_cy_apply_merge_to_rows(g, &cy, &rows, row_count, &p, &did_write);
            if (s != NG_OK)
                break;
            last_write = 1;
        } else if (ng_cy_clause_starts(p, "RETURN")) {
            ng_cy_projection ret[NG_CY_MAX_RETURNS];
            ng_cy_order orders[NG_CY_MAX_RETURNS];
            size_t count = 0, order_count = 0;
            int distinct = 0;
            uint64_t skip = 0, limit = 0;
            int has_skip = 0, has_limit = 0;
            if (ng_cy_parse_projection_list(&p, &cy, "RETURN", ret, &count, &distinct, 0) !=
                NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            if (ng_cy_activate_return_aliases(&cy, ret, count) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            p = ng_skip_ws(p);
            if (ng_cy_parse_order_list(&p, &cy, ret, count, orders, &order_count) != NG_OK) {
                s = NG_PARSE_ERROR;
                break;
            }
            p = ng_skip_ws(p);
            if (ng_cy_clause_starts(p, "SKIP")) {
                p = ng_skip_ws(p + 4);
                if (ng_query_parse_uint64(&p, &skip) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                has_skip = 1;
                p = ng_skip_ws(p);
            }
            if (ng_cy_clause_starts(p, "LIMIT")) {
                p = ng_skip_ws(p + 5);
                if (ng_query_parse_uint64(&p, &limit) != NG_OK) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                has_limit = 1;
                p = ng_skip_ws(p);
            }
            if (*p) {
                s = NG_PARSE_ERROR;
                break;
            }
            s = ng_cy_emit_rows(g,
                                &cy,
                                rows,
                                row_count,
                                ret,
                                count,
                                distinct,
                                orders,
                                order_count,
                                skip,
                                has_skip,
                                limit,
                                has_limit,
                                out);
            break;
        } else {
            s = NG_PARSE_ERROR;
            break;
        }
        if (!*ng_skip_ws(p)) {
            s = last_write ? NG_OK : NG_PARSE_ERROR;
            break;
        }
    }
    if (ng_query_active_schema && !ng_query_active_schema->valid) {
        ng_query_active_schema->valid = 1;
        ng_query_active_schema->count = 0;
    }
    free(rows);
    if (mutated)
        *mutated = did_write;
    return s;
}
static ng_status ng_query_print_generic(const ng_graph* g, const char* q, FILE* out, int* handled) {
    ng_cy_query cy;
    ng_cy_row *rows = NULL, *next = NULL;
    ng_cy_order orders[NG_CY_MAX_RETURNS];
    size_t row_count = 1, next_count = 0, i, w, order_count = 0;
    ng_status s;
    int with_handled = 0, mut = 0;
    if (handled)
        *handled = 0;
    s = ng_query_execute_with((ng_graph*)g, q, out, &mut, &with_handled);
    if (with_handled) {
        if (handled)
            *handled = 1;
        return mut ? NG_PARSE_ERROR : s;
    }
    s = ng_cy_parse_query(q, &cy);
    if (s != NG_OK)
        return NG_OK;
    if (handled)
        *handled = 1;
    rows = calloc(1, sizeof(*rows));
    if (!rows)
        return NG_OOM;
    for (i = 0; i < cy.match_count; i++) {
        s = ng_cy_apply_match(g, &cy, &cy.matches[i], rows, row_count, &next, &next_count);
        free(rows);
        rows = next;
        row_count = next_count;
        next = NULL;
        next_count = 0;
        if (s != NG_OK) {
            free(rows);
            return s;
        }
        if (!row_count)
            break;
    }
    if (cy.has_where) {
        for (i = 0, w = 0; i < row_count; i++)
            if (ng_cy_expr_matches(g, &cy, &rows[i], cy.where_root))
                rows[w++] = rows[i];
        row_count = w;
    }
    s = ng_cy_emit_rows(g,
                        &cy,
                        rows,
                        row_count,
                        cy.returns,
                        cy.return_count,
                        cy.distinct,
                        orders,
                        order_count,
                        cy.skip,
                        cy.has_skip,
                        cy.limit,
                        cy.has_limit,
                        out);
    free(rows);
    return s;
}
static ng_status ng_query_parse_write_node(const char** pp,
                                           char* var,
                                           size_t var_capacity,
                                           char* label,
                                           size_t label_capacity,
                                           ng_query_prop* props,
                                           size_t* prop_count) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    if (*p != '(')
        return NG_PARSE_ERROR;
    p++;
    p = ng_skip_ws(p);
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (n >= var_capacity)
            return NG_PARSE_ERROR;
        memcpy(var, s, n);
        var[n] = 0;
        p = ng_skip_ws(p);
    }
    if (*p == ':') {
        p++;
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (!n || n >= label_capacity)
            return NG_PARSE_ERROR;
        memcpy(label, s, n);
        label[n] = 0;
    }
    p = ng_skip_ws(p);
    if (ng_query_parse_prop_map(&p, props, prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != ')')
        return NG_PARSE_ERROR;
    *pp = ng_skip_ws(p + 1);
    return NG_OK;
}
static ng_status ng_query_parse_optional_return(const char** pp, ng_query_plan* plan) {
    const char* p = ng_skip_ws(*pp);
    if (!*p) {
        *pp = p;
        return NG_OK;
    }
    if (strncmp(p, "RETURN", 6) || !isspace((unsigned char)p[6]))
        return NG_PARSE_ERROR;
    if (ng_query_parse_return_list(&p, plan) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p)
        return NG_PARSE_ERROR;
    *pp = p;
    return NG_OK;
}
static ng_status ng_query_return_keys(ng_graph* g, const ng_query_plan* plan, ng_symbol_id* keys) {
    size_t i;
    for (i = 0; i < (size_t)plan->return_count; i++)
        if (plan->return_is_properties[i] && !plan->return_is_ids[i]) {
            if (ng_symbol(g, plan->return_keys[i], &keys[i]) != NG_OK)
                return NG_OOM;
        }
    return NG_OK;
}
static ng_status ng_query_props_to_symbols(ng_graph* g,
                                           const ng_query_prop* props,
                                           size_t prop_count,
                                           ng_property* out) {
    size_t i;
    ng_status s;
    for (i = 0; i < prop_count; i++) {
        if (ng_symbol(g, props[i].key, &out[i].key) != NG_OK)
            return NG_OOM;
        s = ng_query_resolve_value(&props[i].value, &out[i].value);
        if (s != NG_OK)
            return s;
    }
    return NG_OK;
}
static ng_status ng_cy_props_to_symbols_row(ng_graph* g,
                                            ng_cy_query* q,
                                            const ng_query_prop* props,
                                            const int* scalars,
                                            size_t prop_count,
                                            const ng_cy_row* row,
                                            ng_property* out) {
    size_t i;
    ng_status s;
    for (i = 0; i < prop_count; i++) {
        if (ng_symbol(g, props[i].key, &out[i].key) != NG_OK)
            return NG_OOM;
        if (scalars && scalars[i] >= 0)
            s = ng_cy_eval_scalar(g, q, row, scalars[i], &out[i].value);
        else
            s = ng_query_resolve_value(&props[i].value, &out[i].value);
        if (s != NG_OK)
            return s;
    }
    return NG_OK;
}
static int
ng_query_node_matches_props(const node_i* n, const ng_property* props, size_t prop_count) {
    size_t i;
    for (i = 0; i < prop_count; i++) {
        const prop* p = findprop(n->p, n->np, props[i].key);
        ng_value v;
        if (!p)
            return 0;
        if (ng_query_resolve_value(&props[i].value, &v) != NG_OK) {
            ng_query_parameter_error = 1;
            return 0;
        }
        if (!ng_value_equal(&p->v, &v))
            return 0;
    }
    return 1;
}
static void ng_query_init_return_plan(ng_query_plan* ret, const ng_query_plan* src) {
    memset(ret, 0, sizeof(*ret));
    ret->return_var = 'n';
    ret->where_var = 'n';
    ret->where_root = -1;
    if (src) {
        ret->has_relationship = src->has_relationship;
        ret->has_second_node = src->has_second_node;
        strcpy(ret->left_var_name, src->left_var_name);
        strcpy(ret->right_var_name, src->right_var_name);
        strcpy(ret->rel_var_name, src->rel_var_name);
    }
}
static ng_status ng_cy_parse_create_pattern(const char** pp, ng_cy_query* out) {
    const char* p = ng_skip_ws(*pp);
    ng_cy_match* m;
    if (out->match_count >= NG_CY_MAX_MATCHES)
        return NG_PARSE_ERROR;
    m = &out->matches[out->match_count];
    memset(m, 0, sizeof(*m));
    if (ng_cy_parse_node(&p, out, &m->nodes[m->node_count++]) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != '-' && *p != '<')
            break;
        if (m->rel_count >= NG_CY_MAX_RELS || m->node_count >= NG_CY_MAX_NODES)
            return NG_PARSE_ERROR;
        if (ng_cy_parse_rel(&p, out, &m->rels[m->rel_count]) != NG_OK)
            return NG_PARSE_ERROR;
        if (m->rels[m->rel_count].dir == 0 || !m->rels[m->rel_count].type[0] ||
            m->rels[m->rel_count].has_var_length)
            return NG_PARSE_ERROR;
        m->rel_count++;
        if (ng_cy_parse_node(&p, out, &m->nodes[m->node_count++]) != NG_OK)
            return NG_PARSE_ERROR;
    }
    out->match_count++;
    *pp = p;
    return NG_OK;
}
static ng_status ng_cy_parse_create_query(const char* q, ng_cy_query* out) {
    const char* p = ng_skip_ws(q);
    memset(out, 0, sizeof(*out));
    out->where_root = -1;
    out->create_mode = 1;
    if (strncmp(p, "CREATE", 6) || !isspace((unsigned char)p[6]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 6);
    if (ng_cy_parse_create_pattern(&p, out) != NG_OK)
        return NG_PARSE_ERROR;
    for (;;) {
        p = ng_skip_ws(p);
        if (*p != ',')
            break;
        p = ng_skip_ws(p + 1);
        if (!*p || (!strncmp(p, "RETURN", 6) && isspace((unsigned char)p[6])) || *p == ',')
            return NG_PARSE_ERROR;
        if (ng_cy_parse_create_pattern(&p, out) != NG_OK)
            return NG_PARSE_ERROR;
    }
    p = ng_skip_ws(p);
    if (*p) {
        if (strncmp(p, "RETURN", 6) || !isspace((unsigned char)p[6]))
            return NG_PARSE_ERROR;
        if (ng_cy_parse_return(&p, out) != NG_OK)
            return NG_PARSE_ERROR;
        p = ng_skip_ws(p);
        if (*p)
            return NG_PARSE_ERROR;
    }
    return NG_OK;
}
static ng_status ng_cy_create_node_from_pattern(
    ng_graph* g, ng_cy_query* q, const ng_cy_node_pat* pat, ng_cy_row* row, ng_node_id* out_id) {
    ng_property props[NG_QUERY_MAX_PROPS];
    ng_symbol_id label = 0;
    ng_node_id id;
    ng_status s;
    if (!out_id)
        return NG_INVALID_ARGUMENT;
    if (pat->var_index >= 0 && row->values[pat->var_index].kind) {
        if (row->values[pat->var_index].kind != 1)
            return NG_PARSE_ERROR;
        *out_id = row->values[pat->var_index].id;
        return NG_OK;
    }
    if (pat->label[0] && ng_symbol(g, pat->label, &label) != NG_OK)
        return NG_OOM;
    s = ng_cy_props_to_symbols_row(
        g, q, pat->props, pat->prop_scalars, pat->prop_count, row, props);
    if (s != NG_OK)
        return s;
    s = ng_node_create_with_properties(
        g, label ? &label : NULL, label ? 1 : 0, props, pat->prop_count, &id);
    if (s != NG_OK)
        return s;
    if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 1, id))
        return NG_PARSE_ERROR;
    *out_id = id;
    return NG_OK;
}
static ng_status ng_cy_create_relationship_from_pattern(ng_graph* g,
                                                        ng_cy_query* q,
                                                        const ng_cy_rel_pat* pat,
                                                        ng_node_id left,
                                                        ng_node_id right,
                                                        ng_cy_row* row) {
    ng_property props[NG_QUERY_MAX_PROPS];
    ng_symbol_id type = 0;
    ng_relationship_id id;
    ng_node_id src, dst;
    size_t i;
    ng_status s;
    if (pat->dir == 0 || !pat->type[0])
        return NG_PARSE_ERROR;
    if (pat->var_index >= 0 && row->values[pat->var_index].kind)
        return NG_PARSE_ERROR;
    if (ng_symbol(g, pat->type, &type) != NG_OK)
        return NG_OOM;
    s = ng_cy_props_to_symbols_row(
        g, q, pat->props, pat->prop_scalars, pat->prop_count, row, props);
    if (s != NG_OK)
        return s;
    src = pat->dir < 0 ? right : left;
    dst = pat->dir < 0 ? left : right;
    s = ng_relationship_create(g, src, type, dst, &id);
    if (s != NG_OK)
        return s;
    for (i = 0; i < pat->prop_count; i++) {
        s = ng_relationship_set(g, id, props[i].key, &props[i].value);
        if (s != NG_OK)
            return s;
    }
    if (pat->var_index >= 0 && !ng_cy_bind(row, pat->var_index, 2, id))
        return NG_PARSE_ERROR;
    return NG_OK;
}
static ng_status
ng_cy_execute_create_match(ng_graph* g, ng_cy_query* q, const ng_cy_match* m, ng_cy_row* row) {
    ng_node_id node_ids[NG_CY_MAX_NODES];
    ng_status s;
    size_t i;
    memset(node_ids, 0, sizeof(node_ids));
    for (i = 0; i < m->node_count; i++) {
        s = ng_cy_create_node_from_pattern(g, q, &m->nodes[i], row, &node_ids[i]);
        if (s != NG_OK)
            return s;
    }
    for (i = 0; i < m->rel_count; i++) {
        s = ng_cy_create_relationship_from_pattern(
            g, q, &m->rels[i], node_ids[i], node_ids[i + 1], row);
        if (s != NG_OK)
            return s;
    }
    return NG_OK;
}
static ng_status ng_query_execute_create(ng_graph* g, const char* q, FILE* out, int* mutated) {
    ng_cy_query cy;
    ng_cy_row row;
    ng_status s;
    size_t i;
    cy.create_mode = 1;
    if (ng_cy_parse_create_query(q, &cy) != NG_OK)
        return NG_PARSE_ERROR;
    memset(&row, 0, sizeof(row));
    for (i = 0; i < cy.match_count; i++) {
        s = ng_cy_execute_create_match(g, &cy, &cy.matches[i], &row);
        if (s != NG_OK)
            return s;
    }
    if (ng_query_active_schema) {
        ng_cy_projected_row projected;
        ng_cy_projected_row* items = NULL;
        size_t item_count = 0;
        memset(&projected, 0, sizeof(projected));
        s = ng_cy_project_rows(
            g, &cy, &row, 1, cy.returns, cy.return_count, 0, 1, &items, &item_count);
        if (s == NG_OK) {
            if (item_count)
                projected = items[0];
            s = ng_cy_capture_schema(
                &cy, cy.returns, cy.return_count, item_count ? &projected : NULL, item_count);
        }
        free(items);
        if (s != NG_OK)
            return s;
    }
    if (mutated)
        *mutated = 1;
    for (i = 0; i < cy.return_count; i++) {
        ng_value value;
        if (i && fputc('\t', out) == EOF)
            return NG_IO_ERROR;
        s = ng_cy_eval_scalar(g, &cy, &row, cy.returns[i].scalar_index, &value);
        if (s != NG_OK)
            return s;
        if (!ng_print_value(out, &value))
            return NG_IO_ERROR;
    }
    if (cy.return_count && fputc('\n', out) == EOF)
        return NG_IO_ERROR;
    return NG_OK;
}
static ng_status ng_query_parse_match_write(const char* q, ng_query_plan* plan, const char** tail) {
    const char* p;
    memset(plan, 0, sizeof(*plan));
    plan->return_var = 'n';
    plan->where_var = 'n';
    plan->where_root = -1;
    plan->min_depth = 1;
    plan->max_depth = 1;
    plan->rel_dir = 1;
    p = ng_skip_ws(q);
    if (strncmp(p, "MATCH", 5) || !isspace((unsigned char)p[5]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 5);
    if (ng_query_parse_node_role(&p,
                                 plan,
                                 'n',
                                 plan->left_var_name,
                                 sizeof(plan->left_var_name),
                                 plan->left_label,
                                 sizeof(plan->left_label)) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p == '-' || *p == '<') {
        if (ng_query_parse_relationship_pattern(&p, plan) != NG_OK)
            return NG_PARSE_ERROR;
        if (ng_query_parse_node_role(&p,
                                     plan,
                                     'm',
                                     plan->right_var_name,
                                     sizeof(plan->right_var_name),
                                     plan->right_label,
                                     sizeof(plan->right_label)) != NG_OK)
            return NG_PARSE_ERROR;
        plan->has_relationship = 1;
        p = ng_skip_ws(p);
    } else if (!strncmp(p, "MATCH", 5) && isspace((unsigned char)p[5])) {
        p = ng_skip_ws(p + 5);
        if (ng_query_parse_node_role(&p,
                                     plan,
                                     'm',
                                     plan->right_var_name,
                                     sizeof(plan->right_var_name),
                                     plan->right_label,
                                     sizeof(plan->right_label)) != NG_OK)
            return NG_PARSE_ERROR;
        plan->has_second_node = 1;
        p = ng_skip_ws(p);
    }
    if (!strncmp(p, "WHERE", 5) && isspace((unsigned char)p[5])) {
        p = ng_skip_ws(p + 5);
        if (ng_query_parse_where(&p, plan) != NG_OK)
            return NG_PARSE_ERROR;
    }
    p = ng_skip_ws(p);
    *tail = p;
    return NG_OK;
}
static ng_status ng_query_execute_set(ng_graph* g, const char* q, FILE* out, int* mutated) {
    ng_query_plan plan, ret;
    const char *p, *skey;
    char key_text[128], target;
    ng_value value;
    ng_symbol_id label = 0, right_label = 0, rel_type = 0, key = 0,
                 term_keys[NG_QUERY_MAX_TERMS] = {0}, return_keys[8] = {0};
    ng_property rel_props[NG_QUERY_MAX_PROPS];
    size_t i, j, ti, nkey;
    uint64_t changed = 0;
    ng_status st;
    if (ng_query_parse_match_write(q, &plan, &p) != NG_OK)
        return NG_PARSE_ERROR;
    if (strncmp(p, "SET", 3) || !isspace((unsigned char)p[3]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 3);
    if (ng_query_parse_var_ref(&p, &plan, &target) != NG_OK)
        return NG_PARSE_ERROR;
    if ((target != 'n' && target != 'r') || *p != '.')
        return NG_PARSE_ERROR;
    if (target == 'r' && plan.has_var_length)
        return NG_PARSE_ERROR;
    p++;
    skey = p;
    while (ng_ident_char((unsigned char)*p))
        p++;
    nkey = (size_t)(p - skey);
    if (!nkey || nkey >= sizeof(key_text))
        return NG_PARSE_ERROR;
    memcpy(key_text, skey, nkey);
    key_text[nkey] = 0;
    p = ng_skip_ws(p);
    if (*p != '=')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    if (ng_query_parse_value(&p, &value) != NG_OK)
        return NG_PARSE_ERROR;
    st = ng_query_resolve_value(&value, &value);
    if (st != NG_OK)
        return st;
    ng_query_init_return_plan(&ret, &plan);
    if (ng_query_parse_optional_return(&p, &ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_capture_legacy_schema(&ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (plan.left_label[0]) {
        label = ng_symbol_id_by_text(g, plan.left_label);
        if (!label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    if (plan.rel_type[0]) {
        rel_type = ng_symbol_id_by_text(g, plan.rel_type);
        if (!rel_type)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    for (ti = 0; ti < (size_t)plan.rel_prop_count; ti++) {
        rel_props[ti].value = plan.rel_props[ti].value;
        rel_props[ti].key = ng_symbol_id_by_text(g, plan.rel_props[ti].key);
        if (!rel_props[ti].key)
            return NG_OK;
    }
    if (ng_symbol(g, key_text, &key) != NG_OK)
        return NG_OOM;
    st = ng_query_return_keys(g, &ret, return_keys);
    if (st != NG_OK)
        return st;
    if (target == 'n') {
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], label) ||
                !ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys))
                continue;
            st = ng_node_set(g, g->no[i].id, key, &value);
            if (st != NG_OK)
                return st;
            changed++;
            if (ret.return_count) {
                st = ng_query_print_row(&g->no[i], NULL, NULL, &ret, return_keys, out);
                if (st != NG_OK)
                    return st;
            }
        }
    } else {
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], label))
                continue;
            for (j = 0; j < g->nr; j++) {
                node_i* right;
                if (g->re[j].src != g->no[i].id || (rel_type && g->re[j].type != rel_type) ||
                    !ng_query_rel_matches_props(&g->re[j], rel_props, (size_t)plan.rel_prop_count))
                    continue;
                right = node(g, g->re[j].dst);
                if (!right || !ng_query_label_matches(right, right_label) ||
                    !ng_query_where_matches(&plan, &g->no[i], &g->re[j], right, term_keys))
                    continue;
                st = ng_relationship_set(g, g->re[j].id, key, &value);
                if (st != NG_OK)
                    return st;
                changed++;
                if (ret.return_count) {
                    st = ng_query_print_row(&g->no[i], &g->re[j], right, &ret, return_keys, out);
                    if (st != NG_OK)
                        return st;
                }
            }
        }
    }
    if (mutated && changed)
        *mutated = 1;
    return NG_OK;
}
static ng_status ng_query_execute_delete(ng_graph* g, const char* q, int* mutated) {
    ng_query_plan plan;
    const char* p;
    char target;
    ng_symbol_id label = 0, right_label = 0, rel_type = 0, term_keys[NG_QUERY_MAX_TERMS] = {0};
    ng_property rel_props[NG_QUERY_MAX_PROPS];
    ng_id* ids = NULL;
    size_t i, j, ti, count = 0, cap = 0;
    ng_status st;
    if (ng_query_parse_match_write(q, &plan, &p) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_active_schema) {
        ng_query_active_schema->valid = 1;
        ng_query_active_schema->count = 0;
    }
    if (strncmp(p, "DELETE", 6) || !isspace((unsigned char)p[6]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 6);
    if (ng_query_parse_var_ref(&p, &plan, &target) != NG_OK)
        return NG_PARSE_ERROR;
    if (target != 'n' && target != 'r')
        return NG_PARSE_ERROR;
    if (target == 'r' && !plan.has_relationship)
        return NG_PARSE_ERROR;
    if (target == 'r' && plan.has_var_length)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p)
        return NG_PARSE_ERROR;
    if (plan.left_label[0]) {
        label = ng_symbol_id_by_text(g, plan.left_label);
        if (!label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    if (plan.rel_type[0]) {
        rel_type = ng_symbol_id_by_text(g, plan.rel_type);
        if (!rel_type)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    for (ti = 0; ti < (size_t)plan.rel_prop_count; ti++) {
        rel_props[ti].value = plan.rel_props[ti].value;
        rel_props[ti].key = ng_symbol_id_by_text(g, plan.rel_props[ti].key);
        if (!rel_props[ti].key)
            return NG_OK;
    }
    if (target == 'n') {
        for (i = 0; i < g->nn; i++)
            if (ng_query_label_matches(&g->no[i], label) &&
                ng_query_where_matches(&plan, &g->no[i], NULL, NULL, term_keys)) {
                if (!grow((void**)&ids, &cap, count + 1, sizeof(*ids))) {
                    free(ids);
                    return NG_OOM;
                }
                ids[count++] = g->no[i].id;
            }
        for (i = 0; i < count; i++) {
            st = ng_node_delete(g, ids[i]);
            if (st != NG_OK) {
                free(ids);
                return st;
            }
        }
    } else {
        for (i = 0; i < g->nn; i++) {
            if (!ng_query_label_matches(&g->no[i], label))
                continue;
            for (j = 0; j < g->nr; j++) {
                node_i* right;
                if (g->re[j].src != g->no[i].id || (rel_type && g->re[j].type != rel_type) ||
                    !ng_query_rel_matches_props(&g->re[j], rel_props, (size_t)plan.rel_prop_count))
                    continue;
                right = node(g, g->re[j].dst);
                if (!right || !ng_query_label_matches(right, right_label) ||
                    !ng_query_where_matches(&plan, &g->no[i], &g->re[j], right, term_keys))
                    continue;
                if (!grow((void**)&ids, &cap, count + 1, sizeof(*ids))) {
                    free(ids);
                    return NG_OOM;
                }
                ids[count++] = g->re[j].id;
            }
        }
        for (i = 0; i < count; i++) {
            st = ng_relationship_delete(g, ids[i]);
            if (st != NG_OK) {
                free(ids);
                return st;
            }
        }
    }
    free(ids);
    if (mutated && count)
        *mutated = 1;
    return NG_OK;
}
static ng_status ng_query_parse_bound_node_ref(const char** pp, const char* expected) {
    const char *p = ng_skip_ws(*pp), *s;
    char name[64];
    size_t n;
    if (!expected || !*expected)
        return NG_PARSE_ERROR;
    if (*p != '(')
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + 1);
    s = p;
    if (!ng_ident_char((unsigned char)*p) || isdigit((unsigned char)*p))
        return NG_PARSE_ERROR;
    while (ng_ident_char((unsigned char)*p))
        p++;
    n = (size_t)(p - s);
    if (!n || n >= sizeof(name))
        return NG_PARSE_ERROR;
    memcpy(name, s, n);
    name[n] = 0;
    p = ng_skip_ws(p);
    if (*p != ')' || strcmp(name, expected))
        return NG_PARSE_ERROR;
    *pp = p + 1;
    return NG_OK;
}
static ng_status ng_query_parse_write_relationship_pattern(const char** pp,
                                                           const char* keyword,
                                                           const ng_query_plan* plan,
                                                           char* type,
                                                           size_t type_capacity,
                                                           char* rel_var_name,
                                                           size_t rel_var_capacity,
                                                           ng_query_prop* props,
                                                           size_t* prop_count,
                                                           int* reverse) {
    const char *p = ng_skip_ws(*pp), *s;
    size_t n;
    int rev = 0;
    if (strncmp(p, keyword, strlen(keyword)) || !isspace((unsigned char)p[strlen(keyword)]))
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p + strlen(keyword));
    if (ng_query_parse_bound_node_ref(&p, plan->left_var_name) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (p[0] == '<' && p[1] == '-' && p[2] == '[') {
        rev = 1;
        p += 3;
    } else if (p[0] == '-' && p[1] == '[')
        p += 2;
    else
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    rel_var_name[0] = 0;
    if (ng_ident_char((unsigned char)*p) && !isdigit((unsigned char)*p)) {
        s = p;
        while (ng_ident_char((unsigned char)*p))
            p++;
        n = (size_t)(p - s);
        if (n >= rel_var_capacity)
            return NG_PARSE_ERROR;
        memcpy(rel_var_name, s, n);
        rel_var_name[n] = 0;
        p = ng_skip_ws(p);
    }
    if (*p != ':')
        return NG_PARSE_ERROR;
    p++;
    s = p;
    while (ng_ident_char((unsigned char)*p))
        p++;
    n = (size_t)(p - s);
    if (!n || n >= type_capacity)
        return NG_PARSE_ERROR;
    memcpy(type, s, n);
    type[n] = 0;
    p = ng_skip_ws(p);
    if (ng_query_parse_prop_map(&p, props, prop_count) != NG_OK)
        return NG_PARSE_ERROR;
    p = ng_skip_ws(p);
    if (*p != ']')
        return NG_PARSE_ERROR;
    if (rev) {
        if (p[1] != '-')
            return NG_PARSE_ERROR;
        p += 2;
    } else {
        if (p[1] != '-' || p[2] != '>')
            return NG_PARSE_ERROR;
        p += 3;
    }
    if (ng_query_parse_bound_node_ref(&p, plan->right_var_name) != NG_OK)
        return NG_PARSE_ERROR;
    *pp = ng_skip_ws(p);
    if (reverse)
        *reverse = rev;
    return NG_OK;
}
static ng_status
ng_query_execute_create_relationship(ng_graph* g, const char* q, FILE* out, int* mutated) {
    ng_query_plan plan, ret;
    const char* p;
    char type_text[128] = {0}, rel_var_name[64] = {0};
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    ng_property iprops[NG_QUERY_MAX_PROPS];
    ng_symbol_id left_label = 0, right_label = 0, type = 0, term_keys[NG_QUERY_MAX_TERMS] = {0},
                 return_keys[8] = {0};
    size_t i, j, ti, prop_count = 0;
    int reverse = 0;
    uint64_t changed = 0;
    ng_status st;
    if (ng_query_parse_match_write(q, &plan, &p) != NG_OK)
        return NG_PARSE_ERROR;
    if (!plan.has_second_node)
        return NG_PARSE_ERROR;
    if (ng_query_parse_write_relationship_pattern(&p,
                                                  "CREATE",
                                                  &plan,
                                                  type_text,
                                                  sizeof(type_text),
                                                  rel_var_name,
                                                  sizeof(rel_var_name),
                                                  props,
                                                  &prop_count,
                                                  &reverse) != NG_OK)
        return NG_PARSE_ERROR;
    ng_query_init_return_plan(&ret, &plan);
    ret.has_second_node = 1;
    if (rel_var_name[0]) {
        ret.has_relationship = 1;
        strcpy(ret.rel_var_name, rel_var_name);
    }
    if (ng_query_parse_optional_return(&p, &ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_capture_legacy_schema(&ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (plan.left_label[0]) {
        left_label = ng_symbol_id_by_text(g, plan.left_label);
        if (!left_label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    if (ng_symbol(g, type_text, &type) != NG_OK)
        return NG_OOM;
    st = ng_query_props_to_symbols(g, props, prop_count, iprops);
    if (st != NG_OK)
        return st;
    st = ng_query_return_keys(g, &ret, return_keys);
    if (st != NG_OK)
        return st;
    for (i = 0; i < g->nn; i++) {
        if (!ng_query_label_matches(&g->no[i], left_label))
            continue;
        for (j = 0; j < g->nn; j++) {
            ng_relationship_id relid;
            ng_node_id src = reverse ? g->no[j].id : g->no[i].id,
                       dst = reverse ? g->no[i].id : g->no[j].id;
            rel_i* rel = NULL;
            size_t pi;
            if (!ng_query_label_matches(&g->no[j], right_label) ||
                !ng_query_where_matches(&plan, &g->no[i], NULL, &g->no[j], term_keys))
                continue;
            st = ng_relationship_create(g, src, type, dst, &relid);
            if (st != NG_OK)
                return st;
            rel = &g->re[g->nr - 1];
            for (pi = 0; pi < prop_count; pi++) {
                st = ng_relationship_set(g, relid, iprops[pi].key, &iprops[pi].value);
                if (st != NG_OK)
                    return st;
            }
            changed++;
            if (ret.return_count) {
                st = ng_query_print_row(&g->no[i], rel, &g->no[j], &ret, return_keys, out);
                if (st != NG_OK)
                    return st;
            }
        }
    }
    if (mutated && changed)
        *mutated = 1;
    return NG_OK;
}
static ng_status ng_query_execute_merge(ng_graph* g, const char* q, FILE* out, int* mutated) {
    const char* p = ng_skip_ws(q + 5);
    ng_query_plan ret;
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    ng_property iprops[NG_QUERY_MAX_PROPS];
    ng_symbol_id label = 0, keys[8] = {0};
    char label_text[128] = {0}, var_text[64] = {0};
    size_t prop_count = 0, i;
    node_i* found = NULL;
    ng_node_id id;
    ng_status s;
    ng_query_init_return_plan(&ret, NULL);
    if (ng_query_parse_write_node(
            &p, var_text, sizeof(var_text), label_text, sizeof(label_text), props, &prop_count) !=
        NG_OK)
        return NG_PARSE_ERROR;
    strcpy(ret.left_var_name, var_text);
    if (ng_query_parse_optional_return(&p, &ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_capture_legacy_schema(&ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (label_text[0]) {
        label = ng_symbol_id_by_text(g, label_text);
        if (!label && ng_symbol(g, label_text, &label) != NG_OK)
            return NG_OOM;
    }
    s = ng_query_props_to_symbols(g, props, prop_count, iprops);
    if (s != NG_OK)
        return s;
    for (i = 0; i < g->nn; i++)
        if (ng_query_label_matches(&g->no[i], label) &&
            ng_query_node_matches_props(&g->no[i], iprops, prop_count)) {
            found = &g->no[i];
            break;
        }
    if (!found) {
        s = ng_node_create_with_properties(
            g, label ? &label : NULL, label ? 1 : 0, iprops, prop_count, &id);
        if (s != NG_OK)
            return s;
        found = node(g, id);
        if (mutated)
            *mutated = 1;
    }
    if (!ret.return_count)
        return NG_OK;
    s = ng_query_return_keys(g, &ret, keys);
    if (s != NG_OK)
        return s;
    return ng_query_print_row(found, NULL, NULL, &ret, keys, out);
}
static ng_status
ng_query_execute_merge_relationship(ng_graph* g, const char* q, FILE* out, int* mutated) {
    ng_query_plan plan, ret;
    const char* p;
    char type_text[128] = {0}, rel_var_name[64] = {0};
    ng_query_prop props[NG_QUERY_MAX_PROPS];
    ng_property iprops[NG_QUERY_MAX_PROPS];
    ng_symbol_id left_label = 0, right_label = 0, type = 0, term_keys[NG_QUERY_MAX_TERMS] = {0},
                 return_keys[8] = {0};
    size_t i, j, k, ti, prop_count = 0;
    int reverse = 0;
    uint64_t created = 0;
    ng_status st;
    if (ng_query_parse_match_write(q, &plan, &p) != NG_OK)
        return NG_PARSE_ERROR;
    if (!plan.has_second_node)
        return NG_PARSE_ERROR;
    if (ng_query_parse_write_relationship_pattern(&p,
                                                  "MERGE",
                                                  &plan,
                                                  type_text,
                                                  sizeof(type_text),
                                                  rel_var_name,
                                                  sizeof(rel_var_name),
                                                  props,
                                                  &prop_count,
                                                  &reverse) != NG_OK)
        return NG_PARSE_ERROR;
    ng_query_init_return_plan(&ret, &plan);
    ret.has_second_node = 1;
    if (rel_var_name[0]) {
        ret.has_relationship = 1;
        strcpy(ret.rel_var_name, rel_var_name);
    }
    if (ng_query_parse_optional_return(&p, &ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (ng_query_capture_legacy_schema(&ret) != NG_OK)
        return NG_PARSE_ERROR;
    if (plan.left_label[0]) {
        left_label = ng_symbol_id_by_text(g, plan.left_label);
        if (!left_label)
            return NG_OK;
    }
    if (plan.right_label[0]) {
        right_label = ng_symbol_id_by_text(g, plan.right_label);
        if (!right_label)
            return NG_OK;
    }
    for (ti = 0; ti < (size_t)plan.term_count; ti++)
        if (!plan.terms[ti].is_id)
            term_keys[ti] = ng_symbol_id_by_text(g, plan.terms[ti].key);
    if (ng_symbol(g, type_text, &type) != NG_OK)
        return NG_OOM;
    st = ng_query_props_to_symbols(g, props, prop_count, iprops);
    if (st != NG_OK)
        return st;
    st = ng_query_return_keys(g, &ret, return_keys);
    if (st != NG_OK)
        return st;
    for (i = 0; i < g->nn; i++) {
        if (!ng_query_label_matches(&g->no[i], left_label))
            continue;
        for (j = 0; j < g->nn; j++) {
            rel_i* rel = NULL;
            ng_relationship_id relid;
            ng_node_id src = reverse ? g->no[j].id : g->no[i].id,
                       dst = reverse ? g->no[i].id : g->no[j].id;
            size_t pi;
            if (!ng_query_label_matches(&g->no[j], right_label) ||
                !ng_query_where_matches(&plan, &g->no[i], NULL, &g->no[j], term_keys))
                continue;
            for (k = 0; k < g->nr; k++)
                if (g->re[k].src == src && g->re[k].dst == dst && g->re[k].type == type &&
                    ng_query_rel_matches_props(&g->re[k], iprops, prop_count)) {
                    rel = &g->re[k];
                    break;
                }
            if (!rel) {
                st = ng_relationship_create(g, src, type, dst, &relid);
                if (st != NG_OK)
                    return st;
                for (pi = 0; pi < prop_count; pi++) {
                    st = ng_relationship_set(g, relid, iprops[pi].key, &iprops[pi].value);
                    if (st != NG_OK)
                        return st;
                }
                rel = &g->re[g->nr - 1];
                created++;
            }
            if (ret.return_count) {
                st = ng_query_print_row(&g->no[i], rel, &g->no[j], &ret, return_keys, out);
                if (st != NG_OK)
                    return st;
            }
        }
    }
    if (mutated && created)
        *mutated = 1;
    return NG_OK;
}
static int ng_query_is_match_write_tail(const char* tail) {
    return (!strncmp(tail, "SET", 3) && isspace((unsigned char)tail[3])) ||
           (!strncmp(tail, "DELETE", 6) && isspace((unsigned char)tail[6])) ||
           (!strncmp(tail, "CREATE", 6) && isspace((unsigned char)tail[6])) ||
           (!strncmp(tail, "MERGE", 5) && isspace((unsigned char)tail[5]));
}
static int ng_query_is_write(const char* p) {
    if (ng_query_has_statement_separator(p))
        return ng_cy_has_write_clause(p);
    if (ng_query_has_union(p))
        return ng_cy_has_write_clause(p);
    if (ng_cy_has_with(p))
        return ng_cy_has_write_clause(p);
    if (!strncmp(p, "CREATE", 6) && isspace((unsigned char)p[6]))
        return 1;
    if (!strncmp(p, "MERGE", 5) && isspace((unsigned char)p[5]))
        return 1;
    if (!strncmp(p, "MATCH", 5) && isspace((unsigned char)p[5])) {
        ng_query_plan plan;
        const char* tail;
        return ng_query_parse_match_write(p, &plan, &tail) == NG_OK &&
               ng_query_is_match_write_tail(tail);
    }
    return 0;
}
static ng_status ng_query_execute_union(ng_graph* g, const char* query, FILE* out, int* mutated);
static ng_status ng_query_execute_batch(ng_graph* g, const char* query, FILE* out, int* mutated);
static ng_status ng_query_execute_impl(ng_graph* g, const char* q, FILE* out, int* mutated);
static ng_status ng_query_execute_impl(ng_graph* g, const char* q, FILE* out, int* mutated) {
    const char* p = ng_skip_ws(q);
    int handled = 0;
    ng_status ws;
    if (!g || !q || !out)
        return NG_INVALID_ARGUMENT;
    if (mutated)
        *mutated = 0;
    if (ng_query_has_statement_separator(p))
        return ng_query_execute_batch(g, p, out, mutated);
    if (ng_query_has_union(p))
        return ng_query_execute_union(g, p, out, mutated);
    ws = ng_query_execute_with(g, p, out, mutated, &handled);
    if (handled)
        return ws;
    if (!strncmp(p, "CREATE", 6) && isspace((unsigned char)p[6]))
        return ng_query_execute_create(g, p, out, mutated);
    if (!strncmp(p, "MERGE", 5) && isspace((unsigned char)p[5]))
        return ng_query_execute_merge(g, p, out, mutated);
    if (!strncmp(p, "MATCH", 5) && isspace((unsigned char)p[5])) {
        ng_query_plan plan;
        const char* tail;
        ng_status s = ng_query_parse_match_write(p, &plan, &tail);
        if (s == NG_OK && (!strncmp(tail, "SET", 3) && isspace((unsigned char)tail[3])))
            return ng_query_execute_set(g, p, out, mutated);
        if (s == NG_OK && (!strncmp(tail, "DELETE", 6) && isspace((unsigned char)tail[6])))
            return ng_query_execute_delete(g, p, mutated);
        if (s == NG_OK && (!strncmp(tail, "CREATE", 6) && isspace((unsigned char)tail[6])))
            return ng_query_execute_create_relationship(g, p, out, mutated);
        if (s == NG_OK && (!strncmp(tail, "MERGE", 5) && isspace((unsigned char)tail[5])))
            return ng_query_execute_merge_relationship(g, p, out, mutated);
        return ng_query_print_active(g, p, out);
    }
    return NG_PARSE_ERROR;
}
static int ng_query_union_token(const char* p) {
    return !strncmp(p, "UNION", 5) && !ng_ident_char((unsigned char)p[-1]) &&
           !ng_ident_char((unsigned char)p[5]);
}
static ng_status
ng_query_split_union(const char* query, char** branches, size_t* branch_count, int* union_modes) {
    const char* start = query;
    const char* p = query;
    size_t count = 0;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                return NG_PARSE_ERROR;
            p++;
            continue;
        }
        if (p != query && ng_query_union_token(p)) {
            const char* end = p;
            const char* next = ng_skip_ws(p + 5);
            size_t length;
            if (count >= 8)
                return NG_LIMIT;
            while (end > start && isspace((unsigned char)end[-1]))
                end--;
            length = (size_t)(end - start);
            branches[count] = (char*)malloc(length + 1);
            if (!branches[count])
                return NG_OOM;
            memcpy(branches[count], start, length);
            branches[count][length] = 0;
            count++;
            if (!strncmp(next, "ALL", 3) && !ng_ident_char((unsigned char)next[3])) {
                union_modes[count - 1] = 1;
                next = ng_skip_ws(next + 3);
            } else if (!strncmp(next, "DISTINCT", 8) && !ng_ident_char((unsigned char)next[8])) {
                next = ng_skip_ws(next + 8);
            }
            if (!*next)
                return NG_PARSE_ERROR;
            start = next;
            p = next;
            continue;
        }
        p++;
    }
    if (count >= 8)
        return NG_LIMIT;
    {
        const char* end = p;
        size_t length;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;
        length = (size_t)(end - start);
        if (!length)
            return NG_PARSE_ERROR;
        branches[count] = (char*)malloc(length + 1);
        if (!branches[count])
            return NG_OOM;
        memcpy(branches[count], start, length);
        branches[count][length] = 0;
        count++;
    }
    if (count < 2)
        return NG_NOT_FOUND;
    *branch_count = count;
    return NG_OK;
}
static ng_status ng_query_execute_union(ng_graph* g, const char* query, FILE* out, int* mutated) {
    char* branches[8] = {0};
    char** rows = NULL;
    size_t row_count = 0, row_capacity = 0;
    size_t branch_count = 0, i;
    int union_modes[7] = {0};
    ng_query_schema combined_schema;
    memset(&combined_schema, 0, sizeof(combined_schema));
    ng_status s = ng_query_split_union(query, branches, &branch_count, union_modes);
    if (s != NG_OK)
        return s == NG_NOT_FOUND ? NG_PARSE_ERROR : s;
    if (mutated)
        *mutated = 0;
    for (i = 0; i < branch_count; i++) {
        FILE* branch_output = tmpfile();
        int branch_mutated = 0;
        size_t branch_row_start = row_count;
        ng_query_schema branch_schema;
        ng_query_schema* previous_schema;
        memset(&branch_schema, 0, sizeof(branch_schema));
        if (!branch_output) {
            s = NG_IO_ERROR;
            break;
        }
        previous_schema = ng_query_active_schema;
        ng_query_active_schema = &branch_schema;
        s = ng_query_execute_impl(g, branches[i], branch_output, &branch_mutated);
        ng_query_active_schema = previous_schema;
        if (s == NG_OK && branch_mutated && mutated)
            *mutated = 1;
        if (s == NG_OK) {
            char line[65536];
            if (fflush(branch_output) != 0 || fseek(branch_output, 0, SEEK_SET) != 0)
                s = NG_IO_ERROR;
            while (s == NG_OK && fgets(line, sizeof(line), branch_output)) {
                size_t columns = 1, j;
                char* copy;
                if (!strchr(line, '\n') && !feof(branch_output)) {
                    s = NG_LIMIT;
                    break;
                }
                for (j = 0; line[j]; j++)
                    if (line[j] == '\t')
                        columns++;
                if (branch_schema.count && branch_schema.count != columns) {
                    s = NG_PARSE_ERROR;
                    break;
                }
                if (i > 0 && !union_modes[i - 1])
                    for (j = 0; j < row_count; j++)
                        if (!strcmp(rows[j], line))
                            break;
                if (i > 0 && !union_modes[i - 1] && j < row_count)
                    continue;
                if (row_count == row_capacity) {
                    size_t capacity = row_capacity ? row_capacity * 2 : 16;
                    char** grown = (char**)realloc(rows, capacity * sizeof(*rows));
                    if (!grown) {
                        s = NG_OOM;
                        break;
                    }
                    rows = grown;
                    row_capacity = capacity;
                }
                copy = (char*)malloc(strlen(line) + 1);
                if (!copy) {
                    s = NG_OOM;
                    break;
                }
                strcpy(copy, line);
                rows[row_count++] = copy;
            }
            if (s == NG_OK && ferror(branch_output))
                s = NG_IO_ERROR;
        }
        if (s == NG_OK && !branch_schema.valid)
            s = NG_PARSE_ERROR;
        if (s == NG_OK && branch_schema.count == 0 && row_count != branch_row_start)
            s = NG_PARSE_ERROR;
        if (s == NG_OK) {
            size_t j;
            if (combined_schema.valid && combined_schema.count != branch_schema.count)
                s = NG_PARSE_ERROR;
            else if (!combined_schema.valid)
                combined_schema = branch_schema;
            else
                for (j = 0; j < branch_schema.count; j++) {
                    if (!combined_schema.names[j][0] || !branch_schema.names[j][0] ||
                        strcmp(combined_schema.names[j], branch_schema.names[j])) {
                        s = NG_PARSE_ERROR;
                        break;
                    }
                    if (combined_schema.type_known[j] && branch_schema.type_known[j] &&
                        !ng_query_schema_types_compatible(combined_schema.types[j],
                                                          branch_schema.types[j])) {
                        s = NG_PARSE_ERROR;
                        break;
                    }
                    if (!combined_schema.type_known[j] && branch_schema.type_known[j]) {
                        combined_schema.types[j] = branch_schema.types[j];
                        combined_schema.type_known[j] = 1;
                    }
                }
        }
        fclose(branch_output);
        if (s != NG_OK)
            break;
    }
    if (s == NG_OK)
        for (i = 0; i < row_count; i++)
            if (fputs(rows[i], out) == EOF) {
                s = NG_IO_ERROR;
                break;
            }
    for (i = 0; i < row_count; i++)
        free(rows[i]);
    free(rows);
    for (i = 0; i < branch_count; i++)
        free(branches[i]);
    return s;
}
static ng_status ng_query_execute_batch(ng_graph* g, const char* query, FILE* out, int* mutated) {
    char* statements[16] = {0};
    const char* start = query;
    const char* p = query;
    size_t count = 0, i;
    int did_mutate = 0;
    ng_status s = NG_OK;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                return NG_PARSE_ERROR;
            p++;
            continue;
        }
        if (*p == ';') {
            const char* end = p;
            size_t length;
            while (end > start && isspace((unsigned char)end[-1]))
                end--;
            length = (size_t)(end - start);
            if (!length || count >= 16)
                s = length ? NG_LIMIT : NG_PARSE_ERROR;
            else {
                statements[count] = (char*)malloc(length + 1);
                if (!statements[count])
                    s = NG_OOM;
                else {
                    memcpy(statements[count], start, length);
                    statements[count][length] = 0;
                    count++;
                }
            }
            if (s != NG_OK)
                break;
            start = p + 1;
        }
        p++;
    }
    if (s == NG_OK) {
        const char* end = p;
        size_t length;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;
        length = (size_t)(end - start);
        if (!length || count >= 16)
            s = length ? NG_LIMIT : NG_PARSE_ERROR;
        else {
            statements[count] = (char*)malloc(length + 1);
            if (!statements[count])
                s = NG_OOM;
            else {
                memcpy(statements[count], start, length);
                statements[count][length] = 0;
                count++;
            }
        }
    }
    for (i = 0; s == NG_OK && i < count; i++) {
        int statement_mutated = 0;
        s = ng_query_execute_impl(g, statements[i], out, &statement_mutated);
        if (statement_mutated)
            did_mutate = 1;
    }
    for (i = 0; i < count; i++)
        free(statements[i]);
    if (mutated)
        *mutated = did_mutate;
    return s;
}
static ng_status ng_query_copy_output(FILE* src, FILE* dst) {
    char buf[4096];
    size_t n;
    if (fflush(src) != 0)
        return NG_IO_ERROR;
    if (fseek(src, 0, SEEK_SET) != 0)
        return NG_IO_ERROR;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        if (fwrite(buf, 1, n, dst) != n)
            return NG_IO_ERROR;
    if (ferror(src))
        return NG_IO_ERROR;
    return NG_OK;
}
static ng_status ng_query_execute_active(ng_graph* g, const char* q, FILE* out, int* mutated) {
    const char* p = ng_skip_ws(q);
    ng_transaction* tx = NULL;
    ng_graph* tg;
    FILE* buf;
    ng_status s;
    int tx_mutated = 0;
    if (!g || !q || !out)
        return NG_INVALID_ARGUMENT;
    if (mutated)
        *mutated = 0;
    if (!ng_query_is_write(p))
        return ng_query_execute_impl(g, p, out, mutated);
    s = ng_transaction_begin(g, &tx);
    if (s != NG_OK)
        return s;
    tg = ng_transaction_graph(tx);
    if (!tg) {
        ng_transaction_rollback(tx);
        return NG_INVALID_ARGUMENT;
    }
    buf = tmpfile();
    if (!buf) {
        ng_transaction_rollback(tx);
        return NG_IO_ERROR;
    }
    s = ng_query_execute_impl(tg, p, buf, &tx_mutated);
    if (s == NG_OK)
        s = ng_transaction_commit(tx);
    else
        ng_transaction_rollback(tx);
    if (s == NG_OK) {
        s = ng_query_copy_output(buf, out);
        if (mutated)
            *mutated = tx_mutated;
    }
    fclose(buf);
    return s;
}
ng_status ng_query_execute_params(
    ng_graph* g, const char* q, const ng_parameter* p, size_t n, FILE* out, int* mutated) {
    const ng_parameter* oldp = ng_query_parameters;
    size_t oldn = ng_query_parameter_count;
    int olde = ng_query_parameter_error;
    ng_status s = ng_query_parameters_valid(p, n);
    if (s != NG_OK)
        return s;
    s = ng_query_parameters_cover_query(q, p, n);
    if (s != NG_OK)
        return s;
    ng_query_parameters = p;
    ng_query_parameter_count = n;
    ng_query_parameter_error = 0;
    s = ng_query_execute_active(g, q, out, mutated);
    if (s == NG_OK && ng_query_parameter_error)
        s = NG_NOT_FOUND;
    ng_query_parameters = oldp;
    ng_query_parameter_count = oldn;
    ng_query_parameter_error = olde;
    return s;
}
ng_status ng_query_execute(ng_graph* g, const char* q, FILE* out, int* mutated) {
    return ng_query_execute_params(g, q, NULL, 0, out, mutated);
}
static int field(char** p, char* end, char** out) {
    char *s = *p, *q;
    if (s >= end)
        return 0;
    q = (char*)memchr(s, '\t', (size_t)(end - s));
    if (q)
        *q = 0;
    else
        q = end;
    *out = s;
    *p = q < end ? q + 1 : end;
    return 1;
}
static int csvfield(char** p, char* end, char** out) {
    char *s = *p, *r, *w;
    if (s > end)
        return 0;
    if (s < end && *s == '"') {
        s++;
        r = w = s;
        while (r < end) {
            if (*r == '"') {
                if (r + 1 < end && r[1] == '"') {
                    *w++ = '"';
                    r += 2;
                    continue;
                }
                r++;
                if (r < end && *r != ',')
                    return 0;
                *w = 0;
                *out = s;
                *p = r < end ? r + 1 : end;
                return 1;
            }
            *w++ = *r++;
        }
        return 0;
    }
    r = w = s;
    while (r < end && *r != ',')
        *w++ = *r++;
    *w = 0;
    *out = s;
    *p = r < end ? r + 1 : end;
    return 1;
}
static ng_status entity_node(ng_graph* g, ng_symbol_id ek, const char* name, ng_node_id* out) {
    size_t i;
    ng_value v;
    for (i = 0; i < g->nn; i++) {
        const prop* p = findprop(g->no[i].p, g->no[i].np, ek);
        if (p && p->v.type == NG_VALUE_STRING && !strcmp(p->v.as.string, name)) {
            *out = g->no[i].id;
            return NG_OK;
        }
    }
    if (ng_node_create(g, 0, 0, out) != NG_OK)
        return NG_OOM;
    memset(&v, 0, sizeof(v));
    v.type = NG_VALUE_STRING;
    v.length = strlen(name);
    v.as.string = name;
    return ng_node_set(g, *out, ek, &v);
}
static ng_status import_triple_record(ng_graph* g,
                                      ng_symbol_id ek,
                                      const char* a,
                                      const char* b,
                                      const char* c,
                                      int preserve_parallel,
                                      size_t* n) {
    ng_node_id x, y;
    ng_symbol_id t;
    ng_relationship_id rid;
    if (entity_node(g, ek, a, &x) != NG_OK || entity_node(g, ek, c, &y) != NG_OK ||
        ng_symbol(g, b, &t) != NG_OK)
        return NG_OOM;
    if (!preserve_parallel) {
        size_t i;
        for (i = 0; i < g->nr; i++)
            if (g->re[i].src == x && g->re[i].dst == y && g->re[i].type == t)
                return NG_OK;
    }
    if (ng_relationship_create(g, x, t, y, &rid) != NG_OK)
        return NG_OOM;
    (*n)++;
    return NG_OK;
}
static ng_status
ng_import_triples_impl(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted) {
    FILE* f;
    char line[65536];
    size_t n = 0;
    ng_symbol_id ek;
    ng_status s;
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    if (accepted)
        *accepted = 0;
    s = ng_symbol(g, "__nautylus_entity", &ek);
    if (s != NG_OK)
        return s;
    f = fopen(file, "rb");
    if (!f)
        return NG_IO_ERROR;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *e, *a, *b, *c;
        ng_node_id x, y;
        ng_symbol_id t;
        ng_relationship_id rid;
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && !strchr(line, '\n')) {
            fclose(f);
            return NG_PARSE_ERROR;
        }
        while (len &&
               ((unsigned char)line[len - 1] == '\n' || (unsigned char)line[len - 1] == '\r'))
            line[--len] = 0;
        e = line + len;
        if (!field(&p, e, &a) || !field(&p, e, &b) || !field(&p, e, &c) || p != e || !*a || !*b ||
            !*c) {
            fclose(f);
            return NG_PARSE_ERROR;
        }
        if (entity_node(g, ek, a, &x) != NG_OK || entity_node(g, ek, c, &y) != NG_OK ||
            ng_symbol(g, b, &t) != NG_OK) {
            fclose(f);
            return NG_OOM;
        }
        if (!preserve_parallel) {
            size_t i;
            int exists = 0;
            for (i = 0; i < g->nr; i++)
                if (g->re[i].src == x && g->re[i].dst == y && g->re[i].type == t) {
                    exists = 1;
                    break;
                }
            if (exists)
                continue;
        }
        s = ng_relationship_create(g, x, t, y, &rid);
        if (s != NG_OK) {
            fclose(f);
            return s;
        }
        n++;
    }
    fclose(f);
    if (accepted)
        *accepted = n;
    return NG_OK;
}
static ng_status
ng_import_triples_csv_impl(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted) {
    FILE* f;
    char line[65536];
    size_t n = 0;
    ng_symbol_id ek;
    ng_status s;
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    if (accepted)
        *accepted = 0;
    s = ng_symbol(g, "__nautylus_entity", &ek);
    if (s != NG_OK)
        return s;
    f = fopen(file, "rb");
    if (!f)
        return NG_IO_ERROR;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *e, *a, *b, *c;
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && !strchr(line, '\n')) {
            fclose(f);
            return NG_PARSE_ERROR;
        }
        while (len &&
               ((unsigned char)line[len - 1] == '\n' || (unsigned char)line[len - 1] == '\r'))
            line[--len] = 0;
        e = line + len;
        if (!csvfield(&p, e, &a) || !csvfield(&p, e, &b) || !csvfield(&p, e, &c) || p != e || !*a ||
            !*b || !*c) {
            fclose(f);
            return NG_PARSE_ERROR;
        }
        s = import_triple_record(g, ek, a, b, c, preserve_parallel, &n);
        if (s != NG_OK) {
            fclose(f);
            return s;
        }
    }
    fclose(f);
    if (accepted)
        *accepted = n;
    return NG_OK;
}
ng_status ng_export_triples(const ng_graph* g, const char* file) {
    FILE* f;
    size_t i;
    ng_symbol_id ek;
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    ek = ng_symbol_id_by_text(g, "__nautylus_entity");
    if (!ek)
        return NG_NOT_FOUND;
    f = !strcmp(file, "-") ? stdout : fopen(file, "wb");
    if (!f)
        return NG_IO_ERROR;
    for (i = 0; i < g->nr; i++) {
        const node_i *a = node((ng_graph*)g, g->re[i].src), *b = node((ng_graph*)g, g->re[i].dst);
        const prop *ap = a ? findprop(a->p, a->np, ek) : NULL,
                   *bp = b ? findprop(b->p, b->np, ek) : NULL;
        const char* type = ng_symbol_name(g, g->re[i].type);
        if (!ap || !bp || ap->v.type != NG_VALUE_STRING || bp->v.type != NG_VALUE_STRING || !type) {
            if (f != stdout)
                fclose(f);
            return NG_INVALID_ARGUMENT;
        }
        if (fwrite(ap->v.as.string, 1, ap->v.length, f) != ap->v.length || fputc('\t', f) == EOF ||
            fputs(type, f) < 0 || fputc('\t', f) == EOF ||
            fwrite(bp->v.as.string, 1, bp->v.length, f) != bp->v.length || fputc('\n', f) == EOF) {
            if (f != stdout)
                fclose(f);
            return NG_IO_ERROR;
        }
    }
    if (f != stdout && fclose(f) != 0)
        return NG_IO_ERROR;
    return NG_OK;
}
ng_status ng_import_triples_diagnostic(ng_graph* g,
                                       const char* file,
                                       int preserve_parallel,
                                       size_t* accepted,
                                       ng_import_diagnostic* d) {
    FILE* f;
    char line[65536];
    size_t n = 0;
    if (d) {
        d->line = 0;
        d->column = 0;
        d->status = NG_OK;
    }
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    f = fopen(file, "rb");
    if (!f)
        return NG_IO_ERROR;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *q;
        size_t len = strlen(line), fields = 0;
        while (len &&
               ((unsigned char)line[len - 1] == '\n' || (unsigned char)line[len - 1] == '\r'))
            line[--len] = 0;
        if (len == sizeof(line) - 1 && !strchr(line, '\n')) {
            if (d) {
                d->line = n + 1;
                d->column = sizeof(line);
                d->status = NG_PARSE_ERROR;
            }
            fclose(f);
            return NG_PARSE_ERROR;
        }
        if (!len) {
            if (d) {
                d->line = n + 1;
                d->column = 1;
                d->status = NG_PARSE_ERROR;
            }
            fclose(f);
            return NG_PARSE_ERROR;
        }
        while (p < line + len) {
            q = strchr(p, '\t');
            if (!*p) {
                if (d) {
                    d->line = n + 1;
                    d->column = (size_t)(p - line) + 1;
                    d->status = NG_PARSE_ERROR;
                }
                fclose(f);
                return NG_PARSE_ERROR;
            }
            fields++;
            if (!q)
                break;
            p = q + 1;
        }
        if (fields != 3 || p == line + len || !*(p)) {
            if (d) {
                d->line = n + 1;
                d->column = (size_t)(p - line) + 1;
                d->status = NG_PARSE_ERROR;
            }
            fclose(f);
            return NG_PARSE_ERROR;
        }
        n++;
    }
    if (ferror(f)) {
        fclose(f);
        return NG_IO_ERROR;
    }
    fclose(f);
    return ng_import_triples_impl(g, file, preserve_parallel, accepted);
}
#undef ng_import_property_graph
#if 0
static char *nexttab(char **p){char *s=*p,*q;if(!s)return NULL;q=strchr(s,'\t');if(q){*q=0;*p=q+1;}else *p=NULL;return s;}
static ng_status import_props(ng_graph*g,ng_node_id id,char *s){while(s&&*s){char *e=strchr(s,';'),*eq;if(e)*e=0;eq=strchr(s,'=');if(!eq||eq==s)return NG_PARSE_ERROR;*eq=0;{ng_symbol_id k;ng_value v;v.type=NG_VALUE_STRING;v.length=strlen(eq+1);v.as.string=eq+1;if(ng_symbol(g,s,&k)!=NG_OK||ng_node_set(g,id,k,&v)!=NG_OK)return NG_OOM;}s=e?e+1:NULL;}return NG_OK;}
ng_status ng_import_property_graph(ng_graph*g,const char*nf,const char*rf,int parallel,size_t*accepted,ng_import_diagnostic*d){
    FILE*f;
    char line[65536],*p,*kind,*ext,*labels,*props;
    size_t ln=0,count=0;
    ng_symbol_id eid;
    if(accepted)*accepted=0;
    if(d){
        d->line=0;
        d->column=0;
        d->status=NG_OK;
    }
    if(!g||!nf||!rf)return NG_INVALID_ARGUMENT;
    if(ng_symbol(g,"__nautylus_external_id",&eid)!=NG_OK)return NG_OOM;
    f=fopen(nf,"rb");
    if(!f)return NG_IO_ERROR;
    while(fgets(line,sizeof(line),f)){
        size_t z=strlen(line);
        while(z&&(line[z-1]=='\n'||line[z-1]=='\r'))line[--z]=0;
        ln++;
        p=line;
        kind=nexttab(&p);
        ext=nexttab(&p);
        labels=nexttab(&p);
        props=p;
        if(!kind||strcmp(kind,"node")||!ext||!*ext||!labels||!props){
            if(d){
                d->line=ln;
                d->column=1;
                d->status=NG_PARSE_ERROR;
            }
            fclose(f);
            return NG_PARSE_ERROR;
        }
        {
            ng_node_id id;
            ng_value v;
            size_t i;
            for(i=0;
            i<g->nn;
            i++){
                const prop*x=findprop(g->no[i].p,g->no[i].np,eid);
                if(x&&x->v.type==NG_VALUE_STRING&&!strcmp(x->v.as.string,ext)){
                    id=g->no[i].id;
                    break;
                }
            }
            if(i==g->nn){
                if(ng_node_create(g,0,0,&id)!=NG_OK){
                    fclose(f);
                    return NG_OOM;
                }
                v.type=NG_VALUE_STRING;
                v.length=strlen(ext);
                v.as.string=ext;
                if(ng_node_set(g,id,eid,&v)!=NG_OK){
                    fclose(f);
                    return NG_OOM;
                }
                while(labels&&*labels){
                    char *q=strchr(labels,',');
                    ng_symbol_id l;
                    if(q)*q=0;
                    if(ng_symbol(g,labels,&l)!=NG_OK||ng_node_create(g,0,0,&id)!=NG_OK)return NG_OOM;
                    if(q)labels=q+1;
                    else labels=NULL;
                }
            }
            if(import_props(g,id,props)!=NG_OK){
                fclose(f);
                return NG_PARSE_ERROR;
            }
        }
        count++;
    }
    fclose(f);
    f=fopen(rf,"rb");
    if(!f)return NG_IO_ERROR;
    while(fgets(line,sizeof(line),f)){
        size_t z=strlen(line);
        while(z&&(line[z-1]=='\n'||line[z-1]=='\r'))line[--z]=0;
        ln++;
        p=line;
        kind=nexttab(&p);
        ext=nexttab(&p);
        char*src=nexttab(&p);
        char*type=nexttab(&p);
        char*dst=nexttab(&p);
        props=p;
        if(!kind||strcmp(kind,"relationship")||!ext||!src||!type||!dst||!props){
            if(d){
                d->line=ln;
                d->column=1;
                d->status=NG_PARSE_ERROR;
            }
            fclose(f);
            return NG_PARSE_ERROR;
        }
        ng_node_id a=0,b=0;
        size_t i;
        for(i=0;
        i<g->nn;
        i++){
            const prop*x=findprop(g->no[i].p,g->no[i].np,eid);
            if(x&&x->v.type==NG_VALUE_STRING&&!strcmp(x->v.as.string,src))a=g->no[i].id;
            if(x&&x->v.type==NG_VALUE_STRING&&!strcmp(x->v.as.string,dst))b=g->no[i].id;
        }
        if(!a||!b){
            fclose(f);
            return NG_NOT_FOUND;
        }
        ng_symbol_id t;
        if(ng_symbol(g,type,&t)!=NG_OK){
            fclose(f);
            return NG_OOM;
        }
        if(!parallel)for(i=0;
        i<g->nr;
        i++)if(g->re[i].src==a&&g->re[i].dst==b&&g->re[i].type==t)goto done;
        {
            ng_relationship_id rid;
            if(ng_relationship_create(g,a,t,b,&rid)!=NG_OK){
                fclose(f);
                return NG_OOM;
            }
            count++;
        }
        done:;
        if(props&&*props){
            if(import_props(g,a,props)!=NG_OK){
                fclose(f);
                return NG_PARSE_ERROR;
            }
        }
    }
    fclose(f);
    if(accepted)*accepted=count;
    return NG_OK;
}
#endif

#define ng_import_property_graph ng_import_property_graph_impl
/* Property-graph TSV loader.  Node records are resolved before labels are
   processed; labels can therefore never allocate graph nodes. */
static char* ng_tab_field(char** cursor) {
    char *s = *cursor, *q;
    if (!s)
        return NULL;
    q = strchr(s, '\t');
    if (q) {
        *q = 0;
        *cursor = q + 1;
    } else
        *cursor = NULL;
    return s;
}
static ng_status
ng_import_node_for_id(ng_graph* g, ng_symbol_id external, const char* id, ng_node_id* out) {
    size_t i;
    ng_value v;
    for (i = 0; i < g->nn; i++) {
        const prop* p = findprop(g->no[i].p, g->no[i].np, external);
        if (p && p->v.type == NG_VALUE_STRING && !strcmp(p->v.as.string, id)) {
            *out = g->no[i].id;
            return NG_OK;
        }
    }
    if (ng_node_create(g, 0, 0, out) != NG_OK)
        return NG_OOM;
    v.type = NG_VALUE_STRING;
    v.length = strlen(id);
    v.as.string = id;
    return ng_node_set(g, *out, external, &v);
}
static ng_status ng_attach_labels(ng_graph* g, node_i* n, char* text) {
    while (text && *text) {
        char* q = strchr(text, ',');
        ng_symbol_id label;
        size_t i;
        if (q)
            *q = 0;
        if (!*text)
            return NG_PARSE_ERROR;
        if (ng_symbol(g, text, &label) != NG_OK)
            return NG_OOM;
        for (i = 0; i < n->nl; i++)
            if (n->labels[i] == label)
                break;
        if (i == n->nl) {
            if (!grow((void**)&n->labels, &n->cap, n->nl + 1, sizeof(*n->labels)))
                return NG_OOM;
            n->labels[n->nl++] = label;
        }
        text = q ? q + 1 : NULL;
    }
    return NG_OK;
}
static int ng_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static ng_status ng_decode_hex(const char* t, size_t n, unsigned char** out, size_t* len) {
    size_t i;
    unsigned char* p;
    if (n & 1)
        return NG_PARSE_ERROR;
    if (n / 2 > SIZE_MAX)
        return NG_PARSE_ERROR;
    p = n ? malloc(n / 2) : NULL;
    if (n && !p)
        return NG_OOM;
    for (i = 0; i < n / 2; i++) {
        int a = ng_hex((unsigned char)t[i * 2]), b = ng_hex((unsigned char)t[i * 2 + 1]);
        if (a < 0 || b < 0) {
            free(p);
            return NG_PARSE_ERROR;
        }
        p[i] = (unsigned char)((a << 4) | b);
    }
    *out = p;
    *len = n / 2;
    return NG_OK;
}
static ng_status ng_write_hex(FILE* f, const unsigned char* d, size_t n) {
    static const char h[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++)
        if (fputc(h[d[i] >> 4], f) == EOF || fputc(h[d[i] & 15], f) == EOF)
            return NG_IO_ERROR;
    return NG_OK;
}
static ng_status ng_write_encoded_value(FILE* f, const ng_value* v) {
    uint64_t u;
    char b[32];
    if (!v)
        return NG_INVALID_ARGUMENT;
    if (v->type == NG_VALUE_NULL)
        return fputs("n", f) < 0 ? NG_IO_ERROR : NG_OK;
    if (v->type == NG_VALUE_BOOL)
        return fputs(v->as.boolean ? "b:1" : "b:0", f) < 0 ? NG_IO_ERROR : NG_OK;
    if (v->type == NG_VALUE_INT64) {
        if (fputs("i:", f) < 0)
            return NG_IO_ERROR;
        snprintf(b, sizeof(b), "%lld", (long long)v->as.integer);
        return fputs(b, f) < 0 ? NG_IO_ERROR : NG_OK;
    }
    if (v->type == NG_VALUE_DOUBLE) {
        memcpy(&u, &v->as.real, 8);
        if (fprintf(f, "d:%016llx", (unsigned long long)u) < 0)
            return NG_IO_ERROR;
        return NG_OK;
    }
    if (v->type == NG_VALUE_STRING) {
        if (fputs("s:", f) < 0)
            return NG_IO_ERROR;
        return ng_write_hex(f, (const unsigned char*)v->as.string, v->length);
    }
    if (v->type == NG_VALUE_BYTES) {
        if (fputs("x:", f) < 0)
            return NG_IO_ERROR;
        return ng_write_hex(f, v->as.bytes, v->length);
    }
    return NG_INVALID_ARGUMENT;
}
static ng_status ng_parse_encoded_value(const char* t, size_t n, ng_value* out, void** owned) {
    size_t i, z;
    int neg;
    uint64_t mag = 0, limit;
    unsigned char* p;
    char* copy;
    ng_status s;
    if (!t || !out || !owned)
        return NG_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    *owned = NULL;
    if (n == 1 && t[0] == 'n') {
        out->type = NG_VALUE_NULL;
        return NG_OK;
    }
    if (n < 3 || t[1] != ':')
        return NG_PARSE_ERROR;
    if (t[0] == 'b') {
        if (n != 3 || (t[2] != '0' && t[2] != '1'))
            return NG_PARSE_ERROR;
        out->type = NG_VALUE_BOOL;
        out->as.boolean = t[2] == '1';
        return NG_OK;
    }
    if (t[0] == 'i') {
        if (n < 3)
            return NG_PARSE_ERROR;
        i = 2;
        neg = t[i] == '-';
        if (neg)
            i++;
        if (i == n)
            return NG_PARSE_ERROR;
        if ((n - i) > 1 && t[i] == '0')
            return NG_PARSE_ERROR;
        if (neg && (n - i) == 1 && t[i] == '0')
            return NG_PARSE_ERROR;
        limit = neg ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
        for (; i < n; i++) {
            if (t[i] < '0' || t[i] > '9')
                return NG_PARSE_ERROR;
            if (mag > (limit - (uint64_t)(t[i] - '0')) / 10u)
                return NG_PARSE_ERROR;
            mag = mag * 10u + (uint64_t)(t[i] - '0');
        }
        out->type = NG_VALUE_INT64;
        out->as.integer =
            neg ? (mag == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)mag) : (int64_t)mag;
        return NG_OK;
    }
    if (t[0] == 'd') {
        uint64_t bits = 0;
        int h;
        if (n != 18)
            return NG_PARSE_ERROR;
        for (i = 2; i < 18; i++) {
            h = ng_hex((unsigned char)t[i]);
            if (h < 0)
                return NG_PARSE_ERROR;
            bits = (bits << 4) | (unsigned)h;
        }
        out->type = NG_VALUE_DOUBLE;
        memcpy(&out->as.real, &bits, 8);
        return NG_OK;
    }
    if (t[0] != 's' && t[0] != 'x')
        return NG_PARSE_ERROR;
    s = ng_decode_hex(t + 2, n - 2, &p, &z);
    if (s != NG_OK)
        return s;
    if (t[0] == 's') {
        copy = malloc(z + 1);
        if (z && !copy) {
            free(p);
            return NG_OOM;
        }
        if (z)
            memcpy(copy, p, z);
        copy[z] = 0;
        free(p);
        out->type = NG_VALUE_STRING;
        out->length = z;
        out->as.string = copy;
        *owned = copy;
    } else {
        out->type = NG_VALUE_BYTES;
        out->length = z;
        out->as.bytes = p;
        *owned = p;
    }
    return NG_OK;
}
size_t ng_test_encode_value(const ng_value* v, char* out, size_t cap) {
    FILE* f;
    long n;
    size_t got;
    if (!v || !out || !cap)
        return 0;
    f = tmpfile();
    if (!f || ng_write_encoded_value(f, v) != NG_OK) {
        if (f)
            fclose(f);
        return 0;
    }
    if (fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    n = ftell(f);
    if (n < 0 || (size_t)n + 1 > cap) {
        fclose(f);
        return 0;
    }
    rewind(f);
    got = fread(out, 1, (size_t)n, f);
    fclose(f);
    out[got] = 0;
    return got;
}
ng_status ng_test_decode_value(const char* t, ng_value* out, void** owned) {
    return ng_parse_encoded_value(t, t ? strlen(t) : 0, out, owned);
}

static ng_status ng_attach_properties(ng_graph* g, ng_node_id id, char* text) {
    while (text && *text) {
        char *semi = strchr(text, ';'), *eq;
        ng_value v;
        void* owned = NULL;
        ng_symbol_id key;
        if (semi)
            *semi = 0;
        eq = strchr(text, '=');
        if (!eq || eq == text)
            return NG_PARSE_ERROR;
        *eq = 0;
        if (!strcmp(text, "__nautylus_external_id")) {
            text = semi ? semi + 1 : NULL;
            continue;
        }
        if (ng_symbol(g, text, &key) != NG_OK)
            return NG_OOM;
        if (ng_parse_encoded_value(eq + 1, strlen(eq + 1), &v, &owned) != NG_OK) {
            free(owned);
            return NG_PARSE_ERROR;
        }
        if (ng_node_set(g, id, key, &v) != NG_OK) {
            free(owned);
            return NG_OOM;
        }
        free(owned);
        text = semi ? semi + 1 : NULL;
    }
    return NG_OK;
}

static ng_status ng_attach_relationship_properties(ng_graph* g, ng_relationship_id id, char* text) {
    while (text && *text) {
        char *semi = strchr(text, ';'), *eq;
        ng_value v;
        void* owned = NULL;
        ng_symbol_id key;
        if (semi)
            *semi = 0;
        eq = strchr(text, '=');
        if (!eq || eq == text)
            return NG_PARSE_ERROR;
        *eq = 0;
        if (ng_symbol(g, text, &key) != NG_OK)
            return NG_OOM;
        if (ng_parse_encoded_value(eq + 1, strlen(eq + 1), &v, &owned) != NG_OK) {
            free(owned);
            return NG_PARSE_ERROR;
        }
        if (ng_relationship_set(g, id, key, &v) != NG_OK) {
            free(owned);
            return NG_OOM;
        }
        free(owned);
        text = semi ? semi + 1 : NULL;
    }
    return NG_OK;
}
static void ng_import_error(ng_import_diagnostic* d, size_t line, size_t column) {
    if (d) {
        d->line = line;
        d->column = column;
        d->status = NG_PARSE_ERROR;
    }
}
ng_status ng_import_property_graph(ng_graph* g,
                                   const char* nodes_file,
                                   const char* relationships_file,
                                   int preserve_parallel,
                                   size_t* accepted,
                                   ng_import_diagnostic* d) {
    FILE* f;
    ng_test_set_import_stage(NG_TEST_IMPORT_PARSE_NODES);
    char line[65536];
    size_t line_no = 0, count = 0;
    ng_symbol_id external;
    if (accepted)
        *accepted = 0;
    if (d) {
        d->line = 0;
        d->column = 0;
        d->status = NG_OK;
    }
    if (!g || !nodes_file || !relationships_file)
        return NG_INVALID_ARGUMENT;
    ng_test_set_import_stage(NG_TEST_IMPORT_SYMBOL);
    if (ng_symbol(g, "__nautylus_external_id", &external) != NG_OK)
        return NG_OOM;
    f = fopen(nodes_file, "rb");
    if (!f)
        return NG_IO_ERROR;
    while (fgets(line, sizeof(line), f)) {
        char *cursor = line, *kind, *id, *labels, *properties;
        size_t length = strlen(line);
        line_no++;
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        kind = ng_tab_field(&cursor);
        id = ng_tab_field(&cursor);
        labels = ng_tab_field(&cursor);
        properties = cursor;
        if (!kind || strcmp(kind, "node") || !id || !*id || !labels || !properties) {
            ng_import_error(d, line_no, 1);
            fclose(f);
            return NG_PARSE_ERROR;
        }
        {
            ng_node_id node_id;
            ng_test_set_import_stage(NG_TEST_IMPORT_NODE);
            ng_status s = ng_import_node_for_id(g, external, id, &node_id);
            if (s != NG_OK) {
                fclose(f);
                return s;
            }
            node_i* n = node(g, node_id);
            ng_test_set_import_stage(NG_TEST_IMPORT_LABEL);
            s = ng_attach_labels(g, n, labels);
            if (s != NG_OK) {
                ng_import_error(d, line_no, 1);
                fclose(f);
                return s;
            }
            ng_test_set_import_stage(NG_TEST_IMPORT_NODE_PROPERTY);
            s = ng_attach_properties(g, node_id, properties);
            if (s != NG_OK) {
                ng_import_error(d, line_no, 1);
                fclose(f);
                return s;
            }
            count++;
        }
    }
    fclose(f);
    ng_test_set_import_stage(NG_TEST_IMPORT_PARSE_RELATIONSHIPS);
    f = fopen(relationships_file, "rb");
    if (!f)
        return NG_IO_ERROR;
    while (fgets(line, sizeof(line), f)) {
        char *cursor = line, *kind, *rid, *source, *type, *target, *properties;
        size_t length = strlen(line), i;
        line_no++;
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        kind = ng_tab_field(&cursor);
        rid = ng_tab_field(&cursor);
        source = ng_tab_field(&cursor);
        type = ng_tab_field(&cursor);
        target = ng_tab_field(&cursor);
        properties = cursor;
        if (!kind || strcmp(kind, "relationship") || !rid || !source || !type || !target ||
            !properties) {
            ng_import_error(d, line_no, 1);
            fclose(f);
            return NG_PARSE_ERROR;
        }
        ng_node_id a = 0, b = 0;
        for (i = 0; i < g->nn; i++) {
            const prop* p = findprop(g->no[i].p, g->no[i].np, external);
            if (p && p->v.type == NG_VALUE_STRING) {
                if (!strcmp(p->v.as.string, source))
                    a = g->no[i].id;
                if (!strcmp(p->v.as.string, target))
                    b = g->no[i].id;
            }
        }
        if (!a || !b) {
            fclose(f);
            return NG_NOT_FOUND;
        }
        ng_symbol_id relation_type;
        ng_test_set_import_stage(NG_TEST_IMPORT_SYMBOL);
        if (ng_symbol(g, type, &relation_type) != NG_OK) {
            fclose(f);
            return NG_OOM;
        }
        if (!preserve_parallel) {
            int duplicate = 0;
            for (i = 0; i < g->nr; i++)
                if (g->re[i].src == a && g->re[i].dst == b && g->re[i].type == relation_type) {
                    duplicate = 1;
                    break;
                }
            if (duplicate)
                continue;
        }
        ng_test_set_import_stage(NG_TEST_IMPORT_RELATIONSHIP);
        ng_relationship_id relation_id;
        if (ng_relationship_create(g, a, relation_type, b, &relation_id) != NG_OK) {
            fclose(f);
            return NG_OOM;
        }
        if (properties && *properties) {
            ng_test_set_import_stage(NG_TEST_IMPORT_RELATIONSHIP_PROPERTY);
            ng_status s = ng_attach_relationship_properties(g, relation_id, properties);
            if (s != NG_OK) {
                ng_import_error(d, line_no, 1);
                fclose(f);
                return s;
            }
        }
        count++;
    }
    fclose(f);
    if (accepted)
        *accepted = count;
    ng_test_set_import_stage(NG_TEST_IMPORT_COMPLETE);
    return NG_OK;
}
#undef ng_import_property_graph
#include <stddef.h>
static void ng_free_contents(ng_graph* g) {
    size_t i, j;
    if (!g)
        return;
    for (i = 0; i < g->ns; i++)
        free(g->sy[i].s);
    for (i = 0; i < g->nn; i++) {
        free(g->no[i].labels);
        for (j = 0; j < g->no[i].np; j++)
            valfree(&g->no[i].p[j].v);
        free(g->no[i].p);
    }
    for (i = 0; i < g->nr; i++) {
        for (j = 0; j < g->re[i].np; j++)
            valfree(&g->re[i].p[j].v);
        free(g->re[i].p);
    }
    free(g->sy);
    free(g->no);
    free(g->re);
    free(g->co);
    free(g->ix);
    free(g->path);
    free(g->ao);
    free(g->ai);
    for (i = 0; i < g->procedure_count; i++)
        free(g->procedures[i].name);
    free(g->procedures);
    memset(g, 0, sizeof(*g));
}
static int ng_clone_graph(const ng_graph* src, ng_graph* dst) {
    size_t i, j;
    memset(dst, 0, sizeof(*dst));
    dst->path = dupstr(src->path);
    dst->next_node = src->next_node;
    dst->next_rel = src->next_rel;
    dst->next_sym = src->next_sym;
    if (!dst->path)
        return 0;
    for (i = 0; i < src->ns; i++) {
        if (!grow((void**)&dst->sy, &dst->cs, dst->ns + 1, sizeof(*dst->sy)))
            return 0;
        dst->sy[dst->ns].id = src->sy[i].id;
        dst->sy[dst->ns].s = dupstr(src->sy[i].s);
        if (!dst->sy[dst->ns].s)
            return 0;
        dst->ns++;
    }
    for (i = 0; i < src->nn; i++) {
        node_i* n = &dst->no[dst->nn];
        if (!grow((void**)&dst->no, &dst->cn, dst->nn + 1, sizeof(*dst->no)))
            return 0;
        n = &dst->no[dst->nn++];
        memset(n, 0, sizeof(*n));
        n->id = src->no[i].id;
        if (src->no[i].nl) {
            n->labels = malloc(src->no[i].nl * sizeof(*n->labels));
            if (!n->labels)
                return 0;
            memcpy(n->labels, src->no[i].labels, src->no[i].nl * sizeof(*n->labels));
            n->nl = src->no[i].nl;
            n->cap = n->nl;
        }
        for (j = 0; j < src->no[i].np; j++) {
            if (!grow((void**)&n->p, &n->cap, n->np + 1, sizeof(*n->p)))
                return 0;
            n->p[n->np] = src->no[i].p[j];
            memset(&n->p[n->np].v, 0, sizeof(n->p[n->np].v));
            if (valcopy(&n->p[n->np].v, &src->no[i].p[j].v) != NG_OK)
                return 0;
            n->np++;
        }
    }
    for (i = 0; i < src->nr; i++) {
        rel_i* r;
        if (!grow((void**)&dst->re, &dst->cr, dst->nr + 1, sizeof(*dst->re)))
            return 0;
        r = &dst->re[dst->nr++];
        memset(r, 0, sizeof(*r));
        *r = src->re[i];
        r->p = NULL;
        r->cap = r->np = 0;
        for (j = 0; j < src->re[i].np; j++) {
            if (!grow((void**)&r->p, &r->cap, r->np + 1, sizeof(*r->p)))
                return 0;
            r->p[r->np] = src->re[i].p[j];
            memset(&r->p[r->np].v, 0, sizeof(r->p[r->np].v));
            if (valcopy(&r->p[r->np].v, &src->re[i].p[j].v) != NG_OK)
                return 0;
            r->np++;
        }
    }
    if (src->nc) {
        dst->co = (constraint_i*)malloc(src->nc * sizeof(*dst->co));
        if (!dst->co)
            return 0;
        memcpy(dst->co, src->co, src->nc * sizeof(*dst->co));
        dst->nc = dst->cc = src->nc;
    }
    if (src->nix) {
        dst->ix = (index_i*)malloc(src->nix * sizeof(*dst->ix));
        if (!dst->ix)
            return 0;
        memcpy(dst->ix, src->ix, src->nix * sizeof(*dst->ix));
        dst->nix = dst->cix = src->nix;
    }
    for (i = 0; i < src->procedure_count; i++) {
        if (!grow((void**)&dst->procedures,
                  &dst->procedure_capacity,
                  dst->procedure_count + 1,
                  sizeof(*dst->procedures)))
            return 0;
        dst->procedures[dst->procedure_count] = src->procedures[i];
        dst->procedures[dst->procedure_count].name = dupstr(src->procedures[i].name);
        if (!dst->procedures[dst->procedure_count].name)
            return 0;
        dst->procedure_count++;
    }
    return 1;
}
ng_status
ng_import_triples(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted) {
    ng_graph snapshot;
    ng_status s;
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    if (!ng_clone_graph(g, &snapshot)) {
        ng_free_contents(&snapshot);
        if (accepted)
            *accepted = 0;
        return NG_OOM;
    }
    s = ng_import_triples_impl(g, file, preserve_parallel, accepted);
    if (s == NG_OK)
        s = ng_validate(g);
    if (s != NG_OK) {
        ng_graph failed = *g;
        *g = snapshot;
        ng_free_contents(&failed);
        if (accepted)
            *accepted = 0;
    } else {
        ng_free_contents(&snapshot);
    }
    return s;
}
ng_status
ng_import_triples_csv(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted) {
    ng_graph snapshot;
    ng_status s;
    if (!g || !file)
        return NG_INVALID_ARGUMENT;
    if (!ng_clone_graph(g, &snapshot)) {
        ng_free_contents(&snapshot);
        if (accepted)
            *accepted = 0;
        return NG_OOM;
    }
    s = ng_import_triples_csv_impl(g, file, preserve_parallel, accepted);
    if (s == NG_OK)
        s = ng_validate(g);
    if (s != NG_OK) {
        ng_graph failed = *g;
        *g = snapshot;
        ng_free_contents(&failed);
        if (accepted)
            *accepted = 0;
    } else {
        ng_free_contents(&snapshot);
    }
    return s;
}
ng_status ng_import_property_graph(
    ng_graph* g, const char* n, const char* r, int p, size_t* a, ng_import_diagnostic* d) {
    ng_graph snapshot;
    ng_status s;
    ng_test_import_stage = NG_TEST_IMPORT_NONE;
    ng_test_import_stage_mask_value = 0;
    if (!g)
        return NG_INVALID_ARGUMENT;
    ng_test_set_import_stage(NG_TEST_IMPORT_SNAPSHOT);
    if (!ng_clone_graph(g, &snapshot)) {
        ng_free_contents(&snapshot);
        if (a)
            *a = 0;
        if (d) {
            d->line = 0;
            d->column = 0;
            d->status = NG_OOM;
        }
        return NG_OOM;
    }
    s = ng_import_property_graph_impl(g, n, r, p, a, d);
    if (s == NG_OK)
        s = ng_validate(g);
    if (s != NG_OK) {
        if (a)
            *a = 0;
        if (d && s == NG_OOM) {
            d->line = 0;
            d->column = 0;
            d->status = NG_OOM;
        }
        ng_graph failed = *g;
        *g = snapshot;
        ng_free_contents(&failed);
    } else {
        ng_free_contents(&snapshot);
    }
    return s;
}
ng_status ng_transaction_begin(ng_graph* g, ng_transaction** out) {
    ng_transaction* tx;
    if (!g || !out)
        return NG_INVALID_ARGUMENT;
    tx = (ng_transaction*)calloc(1, sizeof(*tx));
    if (!tx)
        return NG_OOM;
    tx->target = g;
    tx->active = 1;
    if (!ng_clone_graph(g, &tx->working)) {
        ng_free_contents(&tx->working);
        free(tx);
        return NG_OOM;
    }
    *out = tx;
    return NG_OK;
}
ng_graph* ng_transaction_graph(ng_transaction* tx) {
    return tx && tx->active ? &tx->working : NULL;
}
ng_status ng_transaction_commit(ng_transaction* tx) {
    ng_graph old;
    ng_status s;
    if (!tx || !tx->active)
        return NG_INVALID_ARGUMENT;
    s = ng_validate(&tx->working);
    if (s != NG_OK)
        return s;
    old = *tx->target;
    *tx->target = tx->working;
    memset(&tx->working, 0, sizeof(tx->working));
    tx->active = 0;
    ng_free_contents(&old);
    free(tx);
    return NG_OK;
}
void ng_transaction_rollback(ng_transaction* tx) {
    if (!tx)
        return;
    if (tx->active)
        ng_free_contents(&tx->working);
    free(tx);
}
static int ng_export_safe_text(const char* s) {
    return s && !strpbrk(s, "\t\r\n,;=");
}
static int ng_compare_symbol_ids(const void* a, const void* b) {
    ng_symbol_id x = *(const ng_symbol_id*)a, y = *(const ng_symbol_id*)b;
    return x > y ? 1 : x < y ? -1 : 0;
}
static int ng_compare_property_indexes(const void* a, const void* b) {
    const prop *x = *(const prop* const*)a, *y = *(const prop* const*)b;
    return x->key > y->key ? 1 : x->key < y->key ? -1 : 0;
}
static int ng_export_value(FILE* f, const ng_graph* g, const prop* p) {
    const char* k = ng_symbol_name(g, p->key);
    if (!ng_export_safe_text(k))
        return 0;
    return fprintf(f, "%s=", k) >= 0 && ng_write_encoded_value(f, &p->v) == NG_OK;
}
static ng_status ng_export_props(FILE* f, const ng_graph* g, const prop* p, size_t n) {
    const prop** v = NULL;
    size_t i;
    if (n > 1) {
        if (n > SIZE_MAX / sizeof(*v))
            return NG_OOM;
        if (ng_test_maybe_fail() != NG_OK)
            return NG_OOM;
        v = malloc(n * sizeof(*v));
        if (!v)
            return NG_OOM;
        for (i = 0; i < n; i++)
            v[i] = &p[i];
        qsort(v, n, sizeof(*v), ng_compare_property_indexes);
    }
    for (i = 0; i < n; i++) {
        const prop* x = n > 1 ? v[i] : &p[i];
        if (i && fputc(';', f) == EOF) {
            free(v);
            return NG_IO_ERROR;
        }
        if (!ng_export_value(f, g, x)) {
            free(v);
            return NG_INVALID_ARGUMENT;
        }
    }
    free(v);
    return NG_OK;
}
static int ng_path_exists(const char* p) {
    FILE* f = fopen(p, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}
static int ng_backup_existing(const char* path, const char* backup, int* had) {
    *had = 0;
    if (ng_path_exists(backup))
        return 0;
    if (rename(path, backup) == 0) {
        *had = 1;
        return 1;
    }
    return errno == ENOENT;
}
static void ng_restore_backup(const char* path, const char* backup, int had) {
    if (had) {
        remove(path);
        rename(backup, path);
    } else
        remove(path);
}
static ng_status ng_commit_pair(const char* nt,
                                const char* rt,
                                const char* nodes_file,
                                const char* relationships_file,
                                const char* nb,
                                const char* rb) {
    int nhad = 0, rhad = 0;
    if (!ng_backup_existing(nodes_file, nb, &nhad))
        return NG_IO_ERROR;
    if (!ng_backup_existing(relationships_file, rb, &rhad)) {
        ng_restore_backup(nodes_file, nb, nhad);
        return NG_IO_ERROR;
    }
    if (rename(nt, nodes_file) != 0) {
        ng_restore_backup(nodes_file, nb, nhad);
        ng_restore_backup(relationships_file, rb, rhad);
        return NG_IO_ERROR;
    }
    if (rename(rt, relationships_file) != 0) {
        ng_restore_backup(nodes_file, nb, nhad);
        ng_restore_backup(relationships_file, rb, rhad);
        return NG_IO_ERROR;
    }
    remove(nb);
    remove(rb);
    return NG_OK;
}
ng_status ng_export_property_graph(const ng_graph* g,
                                   const char* nodes_file,
                                   const char* relationships_file) {
    FILE *nf = NULL, *rf = NULL;
    char nt[4096], rt[4096], nb[4096], rb[4096];
    size_t i, j;
    ng_id last;
    const char* name;
    ng_status st;
    if (!g || !nodes_file || !relationships_file)
        return NG_INVALID_ARGUMENT;
    if (!strcmp(nodes_file, relationships_file))
        return NG_INVALID_ARGUMENT;
    if (strlen(nodes_file) > sizeof(nt) - 11 || strlen(relationships_file) > sizeof(rt) - 11)
        return NG_INVALID_ARGUMENT;
    snprintf(nt, sizeof(nt), "%s.tmp", nodes_file);
    snprintf(rt, sizeof(rt), "%s.tmp", relationships_file);
    snprintf(nb, sizeof(nb), "%s.nautylusbak", nodes_file);
    snprintf(rb, sizeof(rb), "%s.nautylusbak", relationships_file);
    if (!strcmp(nt, rt) || !strcmp(nb, rb) || !strcmp(nt, nodes_file) ||
        !strcmp(rt, relationships_file))
        return NG_INVALID_ARGUMENT;
    if (ng_path_exists(nb) || ng_path_exists(rb))
        return NG_EXISTS;
    nf = fopen(nt, "wb");
    if (!nf)
        goto io;
    rf = fopen(rt, "wb");
    if (!rf)
        goto io;
    for (last = 0;;) {
        ng_node_id id = 0;
        for (i = 0; i < g->nn; i++)
            if (g->no[i].id > last && (!id || g->no[i].id < id))
                id = g->no[i].id;
        if (!id)
            break;
        last = id;
        for (i = 0; i < g->nn; i++)
            if (g->no[i].id == id) {
                ng_symbol_id* labels = NULL;
                size_t nl = g->no[i].nl;
                if (nl > 1) {
                    if (nl > SIZE_MAX / sizeof(*labels)) {
                        st = NG_OOM;
                        goto fail;
                    }
                    if (ng_test_maybe_fail() != NG_OK) {
                        st = NG_OOM;
                        goto fail;
                    }
                    labels = malloc(nl * sizeof(*labels));
                    if (!labels) {
                        st = NG_OOM;
                        goto fail;
                    }
                    memcpy(labels, g->no[i].labels, nl * sizeof(*labels));
                    qsort(labels, nl, sizeof(*labels), ng_compare_symbol_ids);
                }
                if (fprintf(nf, "node\t%llu\t", (unsigned long long)id) < 0) {
                    free(labels);
                    goto io;
                }
                for (j = 0; j < nl; j++) {
                    name = ng_symbol_name(g, nl > 1 ? labels[j] : g->no[i].labels[j]);
                    if (!ng_export_safe_text(name) || (j && fputc(',', nf) == EOF) ||
                        fputs(name, nf) < 0) {
                        free(labels);
                        goto unsupported;
                    }
                }
                free(labels);
                if (fputc('\t', nf) == EOF)
                    goto io;
                st = ng_export_props(nf, g, g->no[i].p, g->no[i].np);
                if (st != NG_OK)
                    goto fail;
                if (fputc('\n', nf) == EOF)
                    goto io;
                break;
            }
    }
    for (last = 0;;) {
        ng_relationship_id id = 0;
        for (i = 0; i < g->nr; i++)
            if (g->re[i].id > last && (!id || g->re[i].id < id))
                id = g->re[i].id;
        if (!id)
            break;
        last = id;
        for (i = 0; i < g->nr; i++)
            if (g->re[i].id == id) {
                name = ng_symbol_name(g, g->re[i].type);
                if (!ng_export_safe_text(name))
                    goto unsupported;
                if (fprintf(rf,
                            "relationship\t%llu\t%llu\t%s\t%llu\t",
                            (unsigned long long)id,
                            (unsigned long long)g->re[i].src,
                            name,
                            (unsigned long long)g->re[i].dst) < 0)
                    goto io;
                st = ng_export_props(rf, g, g->re[i].p, g->re[i].np);
                if (st != NG_OK)
                    goto fail;
                if (fputc('\n', rf) == EOF)
                    goto io;
                break;
            }
    }
    if (fclose(nf) != 0) {
        nf = NULL;
        goto io_closed;
    }
    nf = NULL;
    if (fclose(rf) != 0) {
        rf = NULL;
        goto io_closed;
    }
    rf = NULL;
    st = ng_commit_pair(nt, rt, nodes_file, relationships_file, nb, rb);
    if (st != NG_OK)
        goto fail_closed;
    return NG_OK;
unsupported:
    st = NG_INVALID_ARGUMENT;
    goto fail;
fail:
    if (nf)
        fclose(nf);
    if (rf)
        fclose(rf);
    remove(nt);
    remove(rt);
    return st;
io:
    if (nf)
        fclose(nf);
    if (rf)
        fclose(rf);
io_closed:
    st = NG_IO_ERROR;
fail_closed:
    remove(nt);
    remove(rt);
    return st;
}
