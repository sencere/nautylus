#define _POSIX_C_SOURCE 200809L
#include "nautylus.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define NAUTYLUS_VERSION "0.1.0-alpha"

static void usage(FILE *out) {
    fprintf(out,
        "usage:\n"
        "  nautylus create FILE\n"
        "  nautylus open FILE\n"
        "  nautylus validate FILE\n"
        "  nautylus stats FILE\n"
        "  nautylus analyze FILE\n"
        "  nautylus analyse FILE\n"
        "  nautylus store DB TRIPLES\n"
        "  nautylus import DB TRIPLES\n"
        "  nautylus store-csv DB TRIPLES_CSV\n"
        "  nautylus import-csv DB TRIPLES_CSV\n"
        "  nautylus export DB TRIPLES|-\n"
        "  nautylus store-ng DB NODES RELATIONSHIPS\n"
        "  nautylus import-ng DB NODES RELATIONSHIPS\n"
        "  nautylus export-ng DB NODES RELATIONSHIPS\n"
        "  nautylus constraint-require DB LABEL KEY\n"
        "  nautylus constraint-unique DB LABEL KEY\n"
        "  nautylus constraint-drop-require DB LABEL KEY\n"
        "  nautylus constraint-drop-unique DB LABEL KEY\n"
        "  nautylus constraints DB\n"
        "  nautylus index-create DB LABEL KEY\n"
        "  nautylus index-drop DB LABEL KEY\n"
        "  nautylus indexes DB\n"
        "  nautylus bench FILE NODE_COUNT\n"
        "  nautylus serve DB PORT\n"
        "  nautylus search DB QUERY\n"
        "  nautylus query DB QUERY\n"
        "  nautylus explain QUERY\n");
}

static void print_stats_to(FILE *out, const ng_graph *g) {
    fprintf(out, "nodes: %lu\nrelationships: %lu\nsymbols: %lu\n",
        (unsigned long)ng_node_count(g),
        (unsigned long)ng_relationship_count(g),
        (unsigned long)ng_symbol_count(g));
}

static void print_stats(const ng_graph *g) {
    print_stats_to(stdout, g);
}

static void print_constraints_to(FILE *out, const ng_graph *g) {
    size_t i, n = ng_node_constraint_count(g);
    for (i = 0; i < n; i++) {
        ng_node_constraint_kind kind;
        ng_symbol_id label, key;
        const char *label_name, *key_name;
        if (ng_node_constraint_get(g, i, &kind, &label, &key) != NG_OK) return;
        label_name = label ? ng_symbol_name(g, label) : "*";
        key_name = ng_symbol_name(g, key);
        fprintf(out, "%s %s %s\n",
            kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY ? "required" : "unique",
            label_name ? label_name : "*",
            key_name ? key_name : "?");
    }
}

static void print_constraints(const ng_graph *g) {
    print_constraints_to(stdout, g);
}

static void print_indexes_to(FILE *out, const ng_graph *g) {
    size_t i, n = ng_node_index_count(g);
    for (i = 0; i < n; i++) {
        ng_symbol_id label, key;
        const char *label_name, *key_name;
        if (ng_node_index_get(g, i, &label, &key) != NG_OK) return;
        label_name = label ? ng_symbol_name(g, label) : "*";
        key_name = ng_symbol_name(g, key);
        fprintf(out, "node %s %s\n",
            label_name ? label_name : "*",
            key_name ? key_name : "?");
    }
}

static void print_indexes(const ng_graph *g) {
    print_indexes_to(stdout, g);
}

static int count_match(ng_node_id node, void *context) {
    (void)node;
    (*(size_t *)context)++;
    return 1;
}

static int parse_size_arg(const char *text, size_t min, size_t max, size_t *out) {
    char *end = 0;
    unsigned long value;
    if (!text || !*text || !out) return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end || value < min || value > max) return 0;
    *out = (size_t)value;
    return 1;
}

static ng_status run_bench(const char *path, size_t node_count) {
    ng_graph *bench = 0;
    ng_graph *reopened = 0;
    ng_node_index *index = 0;
    ng_symbol_id label = 0, key = 0, rel = 0;
    ng_node_id previous = 0, current = 0;
    ng_relationship_id relationship = 0;
    ng_property prop;
    ng_value needle;
    size_t i, matches = 0;
    clock_t start, end;
    double seconds;
    ng_status s;

    start = clock();
    s = ng_create(&bench, path);
    if (s != NG_OK) goto done;
    s = ng_symbol(bench, "Bench", &label);
    if (s != NG_OK) goto done;
    s = ng_symbol(bench, "seq", &key);
    if (s != NG_OK) goto done;
    s = ng_symbol(bench, "NEXT", &rel);
    if (s != NG_OK) goto done;
    s = ng_node_index_create(bench, label, key);
    if (s != NG_OK && s != NG_EXISTS) goto done;
    prop.key = key;
    prop.value.type = NG_VALUE_INT64;
    prop.value.length = 0;
    for (i = 0; i < node_count; i++) {
        prop.value.as.integer = (int64_t)i;
        s = ng_node_create_with_properties(bench, &label, 1, &prop, 1, &current);
        if (s != NG_OK) goto done;
        if (previous) {
            s = ng_relationship_create(bench, previous, rel, current, &relationship);
            if (s != NG_OK) goto done;
        }
        previous = current;
    }
    s = ng_save(bench);
    if (s != NG_OK) goto done;
    ng_close(bench);
    bench = 0;
    s = ng_open(&reopened, path);
    if (s != NG_OK) goto done;
    s = ng_validate(reopened);
    if (s != NG_OK) goto done;
    s = ng_node_index_build(reopened, label, key, &index);
    if (s != NG_OK) goto done;
    needle.type = NG_VALUE_INT64;
    needle.length = 0;
    needle.as.integer = (int64_t)(node_count / 2);
    s = ng_node_index_find(index, &needle, count_match, &matches);
    if (s != NG_OK) goto done;
    end = clock();
    seconds = start == (clock_t)-1 || end == (clock_t)-1 ? -1.0 : (double)(end - start) / (double)CLOCKS_PER_SEC;
    printf("nodes: %lu\nrelationships: %lu\nsymbols: %lu\nindex-matches: %lu\nseconds: %.6f\n",
        (unsigned long)ng_node_count(reopened),
        (unsigned long)ng_relationship_count(reopened),
        (unsigned long)ng_symbol_count(reopened),
        (unsigned long)matches,
        seconds);

done:
    ng_node_index_free(index);
    ng_close(reopened);
    ng_close(bench);
    return s;
}

#ifdef _WIN32
static ng_status run_server(const char *path, size_t port) {
    (void)path;
    (void)port;
    fprintf(stderr, "not supported\n");
    return NG_INVALID_ARGUMENT;
}
#else
static void http_write_header(int fd, int code, const char *type, size_t length) {
    const char *status = code == 200 ? "OK" : "Error";
    char header[256];
    int n = snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
        code, status, type, (unsigned long)length);
    if (n > 0 && write(fd, header, (size_t)n) < 0) return;
}

static void http_send(int fd, int code, const char *type, const char *body) {
    size_t length = body ? strlen(body) : 0;
    http_write_header(fd, code, type, length);
    if (length && write(fd, body, length) < 0) return;
}

static void http_send_file(int fd, const char *type, const unsigned char *data, size_t length) {
    http_write_header(fd, 200, type, length);
    if (length && write(fd, data, length) < 0) return;
}

static char *read_file_bytes(const char *path, size_t *out_length) {
    FILE *f = fopen(path, "rb");
    char *data;
    long length;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    length = ftell(f);
    if (length < 0) { fclose(f); return 0; }
    rewind(f);
    data = (char *)malloc((size_t)length + 1);
    if (!data) { fclose(f); return 0; }
    if (fread(data, 1, (size_t)length, f) != (size_t)length) { free(data); fclose(f); return 0; }
    fclose(f);
    data[length] = 0;
    if (out_length) *out_length = (size_t)length;
    return data;
}

static char *run_text_capture(ng_status *status, void (*fn)(FILE *, void *), void *ctx) {
    FILE *f = tmpfile();
    char *data;
    long length;
    if (!f) { *status = NG_IO_ERROR; return 0; }
    fn(f, ctx);
    if (fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0) { fclose(f); *status = NG_IO_ERROR; return 0; }
    length = ftell(f);
    if (length < 0) { fclose(f); *status = NG_IO_ERROR; return 0; }
    rewind(f);
    data = (char *)malloc((size_t)length + 1);
    if (!data) { fclose(f); *status = NG_OOM; return 0; }
    if (fread(data, 1, (size_t)length, f) != (size_t)length) { free(data); fclose(f); *status = NG_IO_ERROR; return 0; }
    fclose(f);
    data[length] = 0;
    *status = NG_OK;
    return data;
}

static void capture_stats(FILE *f, void *ctx) { print_stats_to(f, (const ng_graph *)ctx); }
static void capture_constraints(FILE *f, void *ctx) { print_constraints_to(f, (const ng_graph *)ctx); }
static void capture_indexes(FILE *f, void *ctx) { print_indexes_to(f, (const ng_graph *)ctx); }

typedef struct { const ng_graph *g; const char *query; ng_status status; } query_capture;
static void capture_query(FILE *f, void *ctx) {
    query_capture *q = (query_capture *)ctx;
    q->status = ng_query_print(q->g, q->query, f);
}

typedef struct { ng_graph *g; const char *query; ng_status status; int mutated; } execute_capture;
static void capture_execute(FILE *f, void *ctx) {
    execute_capture *q = (execute_capture *)ctx;
    q->status = ng_query_execute(q->g, q->query, f, &q->mutated);
}

static int capture_query_node_id(ng_node_id node, void *ctx) {
    FILE *f = (FILE *)ctx;
    return fprintf(f, "%llu\n", (unsigned long long)node) >= 0;
}

static void capture_query_nodes(FILE *f, void *ctx) {
    query_capture *q = (query_capture *)ctx;
    q->status = ng_query_nodes(q->g, q->query, capture_query_node_id, f);
}

static char *capture_graph_text(const ng_graph *g, const char *kind, const char *query, ng_status *status) {
    query_capture qc;
    char *out;
    if (!strcmp(kind, "stats")) return run_text_capture(status, capture_stats, (void *)g);
    if (!strcmp(kind, "constraints")) return run_text_capture(status, capture_constraints, (void *)g);
    if (!strcmp(kind, "indexes")) return run_text_capture(status, capture_indexes, (void *)g);
    qc.g = g;
    qc.query = query;
    qc.status = NG_OK;
    if (!strcmp(kind, "query-nodes")) out = run_text_capture(status, capture_query_nodes, &qc);
    else out = run_text_capture(status, capture_query, &qc);
    if (*status == NG_OK && qc.status != NG_OK) {
        free(out);
        *status = qc.status;
        return 0;
    }
    return out;
}

static char *capture_graph_execute(ng_graph *g, const char *query, int *mutated, ng_status *status) {
    execute_capture qc;
    char *out;
    qc.g = g;
    qc.query = query;
    qc.status = NG_OK;
    qc.mutated = 0;
    out = run_text_capture(status, capture_execute, &qc);
    if (*status == NG_OK && qc.status != NG_OK) {
        free(out);
        *status = qc.status;
        return 0;
    }
    if (mutated) *mutated = qc.mutated;
    return out;
}

static int split_pair(char *body, char **label, char **key) {
    char *tab = strchr(body, '\t');
    if (!tab || tab == body || !tab[1]) return 0;
    *tab = 0;
    *label = body;
    *key = tab + 1;
    return 1;
}

static ng_status create_sample(const char *path) {
    ng_graph *g = 0;
    ng_symbol_id person, name, knows;
    ng_node_id a, b, c;
    ng_relationship_id r;
    ng_property p;
    ng_status s = ng_open(&g, path);
    if (s != NG_OK) goto done;
    if ((s = ng_symbol(g, "Person", &person)) != NG_OK) goto done;
    if ((s = ng_symbol(g, "name", &name)) != NG_OK) goto done;
    if ((s = ng_symbol(g, "KNOWS", &knows)) != NG_OK) goto done;
    p.key = name;
    p.value.type = NG_VALUE_STRING;
    p.value.as.string = "Alice";
    p.value.length = 5;
    if ((s = ng_node_create_with_properties(g, &person, 1, &p, 1, &a)) != NG_OK) goto done;
    p.value.as.string = "Bob";
    p.value.length = 3;
    if ((s = ng_node_create_with_properties(g, &person, 1, &p, 1, &b)) != NG_OK) goto done;
    p.value.as.string = "Carol";
    p.value.length = 5;
    if ((s = ng_node_create_with_properties(g, &person, 1, &p, 1, &c)) != NG_OK) goto done;
    if ((s = ng_relationship_create(g, a, knows, b, &r)) != NG_OK) goto done;
    if ((s = ng_relationship_create(g, b, knows, c, &r)) != NG_OK) goto done;
    (void)ng_node_index_create(g, person, name);
    s = ng_save(g);
done:
    ng_close(g);
    return s;
}

static ng_status write_body_file(const char *path, const char *body) {
    FILE *f = fopen(path, "wb");
    size_t n = strlen(body);
    if (!f) return NG_IO_ERROR;
    if (fwrite(body, 1, n, f) != n || fclose(f) != 0) return NG_IO_ERROR;
    return NG_OK;
}

static ng_status make_temp_path(char *out, size_t cap) {
    int fd;
    if (!out || cap < 1) return NG_INVALID_ARGUMENT;
    if (snprintf(out, cap, "/tmp/nautylus-%lu-XXXXXX", (unsigned long)getpid()) >= (int)cap) return NG_INVALID_ARGUMENT;
    fd = mkstemp(out);
    if (fd < 0) return NG_IO_ERROR;
    close(fd);
    remove(out);
    return NG_OK;
}

static int json_put_escaped(FILE *out, const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    if (fputc('"', out) == EOF) return 0;
    while (*p) {
        if (*p == '"' || *p == '\\') {
            if (fputc('\\', out) == EOF || fputc(*p, out) == EOF) return 0;
        } else if (*p == '\n') {
            if (fputs("\\n", out) < 0) return 0;
        } else if (*p == '\r') {
            if (fputs("\\r", out) < 0) return 0;
        } else if (*p == '\t') {
            if (fputs("\\t", out) < 0) return 0;
        } else if (*p < 32) {
            if (fprintf(out, "\\u%04x", (unsigned)*p) < 0) return 0;
        } else if (fputc(*p, out) == EOF) return 0;
        p++;
    }
    return fputc('"', out) != EOF;
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void graph_caption(char *out, size_t cap, const char *id, char *props) {
    char *cursor = props;
    if (cap) out[0] = 0;
    while (cursor && *cursor) {
        char *semi = strchr(cursor, ';'), *eq;
        if (semi) *semi = 0;
        eq = strchr(cursor, '=');
        if (eq) *eq = 0;
        if (eq && (!strcmp(cursor, "name") || !strcmp(cursor, "__nautylus_entity") || !strcmp(cursor, "__nautylus_external_id")) && eq[1] == 's' && eq[2] == ':') {
            const char *hex = eq + 3;
            size_t i, n = strlen(hex) / 2, w = 0;
            for (i = 0; i < n && w + 1 < cap; i++) {
                int a = hex_value((unsigned char)hex[i * 2]), b = hex_value((unsigned char)hex[i * 2 + 1]);
                if (a < 0 || b < 0) break;
                out[w++] = (char)((a << 4) | b);
            }
            out[w] = 0;
            return;
        }
        cursor = semi ? semi + 1 : NULL;
    }
    snprintf(out, cap, "%s", id);
}

static int json_put_encoded_value(FILE *out, const char *encoded) {
    size_t i, n;
    if (!encoded) return fputs("null", out) >= 0;
    if (encoded[0] == 'n' && encoded[1] == 0) return fputs("null", out) >= 0;
    if (encoded[0] == 'b' && encoded[1] == ':' && (encoded[2] == '0' || encoded[2] == '1') && encoded[3] == 0)
        return fputs(encoded[2] == '1' ? "true" : "false", out) >= 0;
    if (encoded[0] == 'i' && encoded[1] == ':') return fputs(encoded + 2, out) >= 0;
    if (encoded[0] == 'd' && encoded[1] == ':' && strlen(encoded + 2) == 16) {
        uint64_t bits = 0;
        double value;
        for (i = 0; i < 16; i++) {
            int digit = hex_value((unsigned char)encoded[2 + i]);
            if (digit < 0) return 0;
            bits = (bits << 4) | (unsigned)digit;
        }
        memcpy(&value, &bits, sizeof(value));
        return fprintf(out, "%.17g", value) >= 0;
    }
    if (encoded[0] == 's' && encoded[1] == ':') {
        n = strlen(encoded + 2) / 2;
        {
            char *text = (char *)malloc(n + 1);
            if (!text) return 0;
            for (i = 0; i < n; i++) {
                int high = hex_value((unsigned char)encoded[2 + i * 2]);
                int low = hex_value((unsigned char)encoded[3 + i * 2]);
                if (high < 0 || low < 0) { free(text); return 0; }
                text[i] = (char)((high << 4) | low);
            }
            text[n] = 0;
            i = json_put_escaped(out, text);
            free(text);
            return (int)i;
        }
    }
    return json_put_escaped(out, encoded);
}

static int json_put_properties(FILE *out, char *props) {
    char *cursor = props;
    int first = 1;
    if (fputc('{', out) == EOF) return 0;
    while (cursor && *cursor) {
        char *semi = strchr(cursor, ';'), *eq;
        if (semi) *semi = 0;
        eq = strchr(cursor, '=');
        if (eq) {
            *eq = 0;
            if (!first && fputc(',', out) == EOF) return 0;
            if (!json_put_escaped(out, cursor) || fputc(':', out) == EOF || !json_put_encoded_value(out, eq + 1)) return 0;
            first = 0;
        }
        cursor = semi ? semi + 1 : NULL;
    }
    return fputc('}', out) != EOF;
}

static char *next_tab(char **cursor) {
    char *start = *cursor, *tab;
    if (!start) return 0;
    tab = strchr(start, '\t');
    if (tab) {
        *tab = 0;
        *cursor = tab + 1;
    } else *cursor = 0;
    return start;
}

static char *graph_json_from_exports(const char *nodes_path, const char *rels_path, ng_status *status) {
    FILE *nodes = fopen(nodes_path, "rb"), *rels = fopen(rels_path, "rb"), *out;
    char line[65536], caption[256];
    char *json;
    long length;
    int first = 1;
    if (!nodes || !rels) { if (nodes) fclose(nodes); if (rels) fclose(rels); *status = NG_IO_ERROR; return 0; }
    out = tmpfile();
    if (!out) { fclose(nodes); fclose(rels); *status = NG_IO_ERROR; return 0; }
    if (fputs("{\"nodes\":[", out) < 0) goto io;
    while (fgets(line, sizeof(line), nodes)) {
        char *kind, *id, *labels, *props, *caption_props, *p = line;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        kind = next_tab(&p);
        id = next_tab(&p);
        labels = next_tab(&p);
        props = p ? p : "";
        if (!kind || strcmp(kind, "node") || !id || !labels) continue;
        caption_props = (char *)malloc(strlen(props) + 1);
        if (!caption_props) goto oom;
        strcpy(caption_props, props);
        graph_caption(caption, sizeof(caption), id, caption_props);
        free(caption_props);
        if (!first && fputc(',', out) == EOF) goto io;
        first = 0;
        if (fputs("{\"id\":", out) < 0 || !json_put_escaped(out, id) || fputs(",\"label\":", out) < 0) goto io;
        if (!json_put_escaped(out, *labels ? labels : "Node") || fputs(",\"caption\":", out) < 0 || !json_put_escaped(out, caption) || fputs(",\"properties\":", out) < 0 || !json_put_properties(out, props) || fputc('}', out) == EOF) goto io;
    }
    if (fputs("],\"links\":[", out) < 0) goto io;
    first = 1;
    while (fgets(line, sizeof(line), rels)) {
        char *kind, *rid, *source, *type, *target, *p = line;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        kind = next_tab(&p);
        rid = next_tab(&p);
        source = next_tab(&p);
        type = next_tab(&p);
        target = next_tab(&p);
        (void)rid;
        if (!kind || strcmp(kind, "relationship") || !source || !type || !target) continue;
        if (!first && fputc(',', out) == EOF) goto io;
        first = 0;
        if (fputs("{\"source\":", out) < 0 || !json_put_escaped(out, source) || fputs(",\"target\":", out) < 0) goto io;
        if (!json_put_escaped(out, target) || fputs(",\"type\":", out) < 0 || !json_put_escaped(out, type) || fputc('}', out) == EOF) goto io;
    }
    if (fputs("]}", out) < 0 || fflush(out) != 0 || fseek(out, 0, SEEK_END) != 0) goto io;
    length = ftell(out);
    if (length < 0) goto io;
    rewind(out);
    json = (char *)malloc((size_t)length + 1);
    if (!json) { fclose(nodes); fclose(rels); fclose(out); *status = NG_OOM; return 0; }
    if (fread(json, 1, (size_t)length, out) != (size_t)length) { free(json); goto io; }
    json[length] = 0;
    fclose(nodes); fclose(rels); fclose(out);
    *status = NG_OK;
    return json;
io:
    fclose(nodes); fclose(rels); fclose(out);
    *status = NG_IO_ERROR;
    return 0;
oom:
    fclose(nodes); fclose(rels); fclose(out); *status = NG_OOM; return 0;
}

static char *capture_graph_json(const char *db_path, ng_status *status) {
    ng_graph *g = 0;
    char nodes_path[4096] = "", rels_path[4096] = "";
    char *json = 0;
    *status = make_temp_path(nodes_path, sizeof(nodes_path));
    if (*status == NG_OK) *status = make_temp_path(rels_path, sizeof(rels_path));
    if (*status == NG_OK) *status = ng_open(&g, db_path);
    if (*status == NG_OK) *status = ng_export_property_graph(g, nodes_path, rels_path);
    ng_close(g);
    if (*status == NG_OK) json = graph_json_from_exports(nodes_path, rels_path, status);
    if (nodes_path[0]) remove(nodes_path);
    if (rels_path[0]) remove(rels_path);
    return json;
}

static void handle_api(int fd, const char *db_path, const char *route, char *body) {
    ng_graph *g = 0;
    ng_status s = NG_OK;
    char *out = 0;
    char tmp[4096] = "";
    if (!strcmp(route, "/api/sample")) {
        s = create_sample(db_path);
        http_send(fd, s == NG_OK ? 200 : 500, "text/plain", s == NG_OK ? "ok\n" : ng_status_name(s));
        return;
    }
    if (!strcmp(route, "/api/import-triples")) {
        size_t accepted = 0;
        s = make_temp_path(tmp, sizeof(tmp));
        if (s == NG_OK) s = write_body_file(tmp, body);
        if (s == NG_OK) s = ng_open(&g, db_path);
        if (s == NG_OK) s = ng_import_triples(g, tmp, 0, &accepted);
        if (s == NG_OK) s = ng_save(g);
        if (tmp[0]) remove(tmp);
        if (s == NG_OK) snprintf(tmp, sizeof(tmp), "accepted: %lu\n", (unsigned long)accepted);
        http_send(fd, s == NG_OK ? 200 : 500, "text/plain", s == NG_OK ? tmp : ng_status_name(s));
        ng_close(g);
        return;
    }
    if (!strcmp(route, "/api/graph")) {
        out = capture_graph_json(db_path, &s);
        http_send(fd, s == NG_OK ? 200 : 500, s == NG_OK ? "application/json" : "text/plain", s == NG_OK && out ? out : ng_status_name(s));
        free(out);
        return;
    }
    s = ng_open(&g, db_path);
    if (s != NG_OK) { http_send(fd, 500, "text/plain", ng_status_name(s)); return; }
    if (!strcmp(route, "/api/stats")) out = capture_graph_text(g, "stats", 0, &s);
    else if (!strcmp(route, "/api/constraints")) out = capture_graph_text(g, "constraints", 0, &s);
    else if (!strcmp(route, "/api/indexes")) out = capture_graph_text(g, "indexes", 0, &s);
    else if (!strcmp(route, "/api/query")) {
        int mutated = 0;
        out = capture_graph_execute(g, body, &mutated, &s);
        if (s == NG_OK && mutated) s = ng_save(g);
    }
    else if (!strcmp(route, "/api/query-nodes")) out = capture_graph_text(g, "query-nodes", body, &s);
    else if (!strcmp(route, "/api/explain")) {
        out = (char *)malloc(512);
        if (!out) s = NG_OOM;
        else s = ng_query_explain(body, out, 512);
    } else if (!strncmp(route, "/api/constraint-", 16) || !strcmp(route, "/api/index-create")) {
        char *label_text, *key_text;
        ng_symbol_id label = 0, key = 0;
        if (!split_pair(body, &label_text, &key_text)) s = NG_INVALID_ARGUMENT;
        if (s == NG_OK) s = ng_symbol(g, label_text, &label);
        if (s == NG_OK) s = ng_symbol(g, key_text, &key);
        if (s == NG_OK && !strcmp(route, "/api/constraint-require")) s = ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, label, key);
        else if (s == NG_OK && !strcmp(route, "/api/constraint-unique")) s = ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, label, key);
        else if (s == NG_OK && !strcmp(route, "/api/index-create")) s = ng_node_index_create(g, label, key);
        else if (s == NG_OK) s = NG_NOT_FOUND;
        if (s == NG_OK || s == NG_EXISTS) {
            s = ng_save(g);
            if (s == NG_OK) {
                out = (char *)malloc(4);
                if (out) memcpy(out, "ok\n", 4);
                else s = NG_OOM;
            }
        }
    } else s = NG_NOT_FOUND;
    http_send(fd, s == NG_OK ? 200 : 500, "text/plain", s == NG_OK && out ? out : ng_status_name(s));
    free(out);
    ng_close(g);
}

static void handle_client(int fd, const char *db_path) {
    char request[65536], method[8], route[256], *body, *cl;
    ssize_t got;
    size_t content_length = 0, header_length;
    got = read(fd, request, sizeof(request) - 1);
    if (got <= 0) return;
    request[got] = 0;
    if (sscanf(request, "%7s %255s", method, route) != 2) { http_send(fd, 400, "text/plain", "bad request"); return; }
    body = strstr(request, "\r\n\r\n");
    if (!body) { http_send(fd, 400, "text/plain", "bad request"); return; }
    body += 4;
    cl = strstr(request, "Content-Length:");
    if (cl) content_length = (size_t)strtoul(cl + 15, 0, 10);
    header_length = (size_t)(body - request);
    while (content_length > (size_t)got - header_length && (size_t)got < sizeof(request) - 1) {
        ssize_t more = read(fd, request + got, sizeof(request) - 1 - (size_t)got);
        if (more <= 0) break;
        got += more;
        request[got] = 0;
    }
    if (!strcmp(route, "/")) {
        size_t n = 0;
        char *data = read_file_bytes("resources/web/index.html", &n);
        if (data) { http_send_file(fd, "text/html; charset=utf-8", (const unsigned char *)data, n); free(data); }
        else http_send(fd, 404, "text/plain", "not found");
    }
    else if (!strcmp(route, "/logo.png")) {
        size_t n = 0;
        char *data = read_file_bytes("resources/logo/logo.png", &n);
        if (data) { http_send_file(fd, "image/png", (const unsigned char *)data, n); free(data); }
        else http_send(fd, 404, "text/plain", "not found");
    } else if (!strcmp(route, "/d3.js")) {
        size_t n = 0;
        char *data = read_file_bytes("resources/d3/d3.v7.min.js", &n);
        if (data) { http_send_file(fd, "application/javascript", (const unsigned char *)data, n); free(data); }
        else http_send(fd, 404, "text/plain", "not found");
    } else if (!strncmp(route, "/api/", 5)) handle_api(fd, db_path, route, body);
    else http_send(fd, 404, "text/plain", "not found");
}

static ng_status run_server(const char *path, size_t port) {
    int server_fd;
    struct sockaddr_in addr;
    int one = 1;
    if (port < 1 || port > 65535) return NG_INVALID_ARGUMENT;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NG_IO_ERROR;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001u);
    addr.sin_port = htons((unsigned short)port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(server_fd, 16) != 0) {
        close(server_fd);
        return NG_IO_ERROR;
    }
    printf("serving http://127.0.0.1:%lu\n", (unsigned long)port);
    fflush(stdout);
    for (;;) {
        int client = accept(server_fd, 0, 0);
        if (client < 0) continue;
        handle_client(client, path);
        close(client);
    }
}
#endif

int main(int argc, char **argv) {
    ng_graph *g = 0;
    ng_status s = NG_INVALID_ARGUMENT;
    size_t n = 0;

    if (argc == 2 && (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version"))) {
        printf("nautylus %s\n", NAUTYLUS_VERSION);
        return 0;
    }
    if (argc < 3) {
        usage(stderr);
        return 2;
    }

    if (!strcmp(argv[1], "create") && argc == 3) {
        s = ng_create(&g, argv[2]);
        if (s == NG_OK) s = ng_save(g);
    } else if (!strcmp(argv[1], "open") && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) {
            s = ng_validate(g);
            if (s == NG_OK) printf("ok\n");
        }
    } else if (!strcmp(argv[1], "validate") && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) {
            s = ng_validate(g);
            printf("%s\n", ng_status_name(s));
        }
    } else if (!strcmp(argv[1], "stats") && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) {
            s = ng_validate(g);
            if (s == NG_OK) {
                print_stats(g);
            }
        }
    } else if ((!strcmp(argv[1], "analyze") || !strcmp(argv[1], "analyse")) && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) {
            s = ng_validate(g);
            if (s == NG_OK) {
                printf("status: ok\n");
                print_stats(g);
            }
        }
    } else if ((!strcmp(argv[1], "import") || !strcmp(argv[1], "store")) && argc == 4) {
        int should_save = !strcmp(argv[1], "store");
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_import_triples(g, argv[3], 0, &n);
        if (s == NG_OK && should_save) s = ng_save(g);
        if (s == NG_OK) printf("accepted: %lu\n", (unsigned long)n);
    } else if ((!strcmp(argv[1], "import-csv") || !strcmp(argv[1], "store-csv")) && argc == 4) {
        int should_save = !strcmp(argv[1], "store-csv");
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_import_triples_csv(g, argv[3], 0, &n);
        if (s == NG_OK && should_save) s = ng_save(g);
        if (s == NG_OK) printf("accepted: %lu\n", (unsigned long)n);
    } else if (!strcmp(argv[1], "export") && argc == 4) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_export_triples(g, argv[3]);
    } else if ((!strcmp(argv[1], "import-ng") || !strcmp(argv[1], "store-ng")) && argc == 5) {
        ng_import_diagnostic d = {0, 0, NG_OK};
        int should_save = !strcmp(argv[1], "store-ng");
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_import_property_graph(g, argv[3], argv[4], 0, &n, &d);
        if (s == NG_OK && should_save) s = ng_save(g);
        if (s == NG_OK) {
            printf("accepted: %lu\n", (unsigned long)n);
        } else if (d.status != NG_OK) {
            fprintf(stderr, "%s at line %lu column %lu\n",
                ng_status_name(d.status), (unsigned long)d.line, (unsigned long)d.column);
        }
    } else if (!strcmp(argv[1], "export-ng") && argc == 5) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_export_property_graph(g, argv[3], argv[4]);
    } else if ((!strcmp(argv[1], "constraint-require") || !strcmp(argv[1], "constraint-unique") || !strcmp(argv[1], "constraint-drop-require") || !strcmp(argv[1], "constraint-drop-unique")) && argc == 5) {
        ng_symbol_id label = 0, key = 0;
        ng_node_constraint_kind kind = (!strcmp(argv[1], "constraint-require") || !strcmp(argv[1], "constraint-drop-require")) ? NG_NODE_CONSTRAINT_REQUIRED_PROPERTY : NG_NODE_CONSTRAINT_UNIQUE_PROPERTY;
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_symbol(g, argv[3], &label);
        if (s == NG_OK) s = ng_symbol(g, argv[4], &key);
        if (s == NG_OK) {
            if (!strcmp(argv[1], "constraint-drop-require") || !strcmp(argv[1], "constraint-drop-unique")) s = ng_node_constraint_drop(g, kind, label, key);
            else s = ng_node_constraint_create(g, kind, label, key);
        }
        if (s == NG_OK) s = ng_save(g);
        if (s == NG_OK) printf("ok\n");
    } else if (!strcmp(argv[1], "constraints") && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) print_constraints(g);
    } else if ((!strcmp(argv[1], "index-create") || !strcmp(argv[1], "index-drop")) && argc == 5) {
        ng_symbol_id label = 0, key = 0;
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_symbol(g, argv[3], &label);
        if (s == NG_OK) s = ng_symbol(g, argv[4], &key);
        if (s == NG_OK) {
            if (!strcmp(argv[1], "index-drop")) s = ng_node_index_drop(g, label, key);
            else s = ng_node_index_create(g, label, key);
        }
        if (s == NG_OK) s = ng_save(g);
        if (s == NG_OK) printf("ok\n");
    } else if (!strcmp(argv[1], "indexes") && argc == 3) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) print_indexes(g);
    } else if (!strcmp(argv[1], "bench") && argc == 4) {
        size_t node_count = 0;
        if (!parse_size_arg(argv[3], 1, 100000, &node_count)) {
            fprintf(stderr, "invalid argument\n");
            return 1;
        }
        s = run_bench(argv[2], node_count);
    } else if (!strcmp(argv[1], "serve") && argc == 4) {
        size_t port = 0;
        if (!parse_size_arg(argv[3], 1, 65535, &port)) {
            fprintf(stderr, "invalid argument\n");
            return 1;
        }
        s = run_server(argv[2], port);
    } else if (!strcmp(argv[1], "query") && argc == 4) {
        int mutated = 0;
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_query_execute(g, argv[3], stdout, &mutated);
        if (s == NG_OK && mutated) s = ng_save(g);
    } else if (!strcmp(argv[1], "search") && argc == 4) {
        s = ng_open(&g, argv[2]);
        if (s == NG_OK) s = ng_query_print(g, argv[3], stdout);
    } else if (!strcmp(argv[1], "explain") && argc == 3) {
        char plan[512];
        s = ng_query_explain(argv[2], plan, sizeof(plan));
        if (s == NG_OK) printf("%s\n", plan);
    } else {
        usage(stderr);
        return 2;
    }

    if (s != NG_OK) fprintf(stderr, "%s\n", ng_status_name(s));
    ng_close(g);
    return s != NG_OK;
}
