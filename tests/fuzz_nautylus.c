#include "nautylus.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

extern ng_status ng_test_decode_value(const char*, ng_value*, void**);

static char* copy_text(const uint8_t* data, size_t size) {
    char* text = (char*)malloc(size + 1);
    if (!text)
        return NULL;
    if (size)
        memcpy(text, data, size);
    text[size] = 0;
    return text;
}

static int write_file(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    if (size && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

static void make_path(char* out, size_t cap, const char* suffix) {
#ifndef _WIN32
    snprintf(out, cap, "build/fuzz-%lu-%s", (unsigned long)getpid(), suffix);
#else
    snprintf(out, cap, "build/fuzz-%s", suffix);
#endif
}

static void fuzz_query(const uint8_t* data, size_t size) {
    char plan[512];
    char* text = copy_text(data, size);
    if (!text)
        return;
    (void)ng_query_explain(text, plan, sizeof(plan));
    free(text);
}

static void fuzz_value_codec(const uint8_t* data, size_t size) {
    char* text = copy_text(data, size);
    ng_value value;
    void* owned = NULL;
    if (!text)
        return;
    if (ng_test_decode_value(text, &value, &owned) == NG_OK)
        free(owned);
    free(text);
}

static void fuzz_storage(const uint8_t* data, size_t size) {
    char snapshot[128], nodes[128], rels[128], graph_path[128], graph_tmp[132];
    ng_graph* graph = NULL;
    size_t accepted = 0;
    size_t split = size / 2;
    make_path(snapshot, sizeof(snapshot), "snapshot.ng");
    make_path(nodes, sizeof(nodes), "nodes.tsv");
    make_path(rels, sizeof(rels), "rels.tsv");
    make_path(graph_path, sizeof(graph_path), "graph.ng");

    if (write_file(snapshot, data, size)) {
        if (ng_open(&graph, snapshot) == NG_OK)
            (void)ng_validate(graph);
        ng_close(graph);
        graph = NULL;
    }

    if (write_file(nodes, data, split) && write_file(rels, data + split, size - split) &&
        ng_create(&graph, graph_path) == NG_OK) {
        (void)ng_import_property_graph(graph, nodes, rels, 0, &accepted, NULL);
        (void)ng_validate(graph);
        ng_close(graph);
    }

    remove(snapshot);
    remove(nodes);
    remove(rels);
    remove(graph_path);
    snprintf(graph_tmp, sizeof(graph_tmp), "%s.tmp", graph_path);
    remove(graph_tmp);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fuzz_query(data, size);
    fuzz_value_codec(data, size);
    fuzz_storage(data, size);
    return 0;
}

#ifndef NAUTYLUS_LIBFUZZER
int main(int argc, char** argv) {
    int i;
    for (i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb");
        long length;
        uint8_t* data;
        if (!f)
            return 2;
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return 2;
        }
        length = ftell(f);
        if (length < 0) {
            fclose(f);
            return 2;
        }
        rewind(f);
        data = (uint8_t*)malloc((size_t)length);
        if (!data && length) {
            fclose(f);
            return 2;
        }
        if (length && fread(data, 1, (size_t)length, f) != (size_t)length) {
            free(data);
            fclose(f);
            return 2;
        }
        fclose(f);
        LLVMFuzzerTestOneInput(data, (size_t)length);
        free(data);
    }
    return 0;
}
#endif
