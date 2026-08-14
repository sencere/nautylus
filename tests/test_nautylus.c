#include "nautylus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void ng_test_fail_after(size_t count);
extern void ng_test_fail_reset(void);
extern size_t ng_test_encode_value(const ng_value*, char*, size_t);
extern ng_status ng_test_decode_value(const char*, ng_value*, void**);
extern ng_status ng_test_graphsage_single_layer_mse_gradient_check(const ng_graph*,
                                                                   ng_direction,
                                                                   ng_symbol_id,
                                                                   double*);
extern ng_status ng_test_graphsage_finite_difference_gradient_check(const ng_graph*,
                                                                    ng_direction,
                                                                    ng_symbol_id,
                                                                    uint32_t,
                                                                    int,
                                                                    int,
                                                                    ng_graphsage_loss_kind,
                                                                    double*);
extern ng_status ng_test_graphsage_evaluation_equivalence(const ng_graph*,
                                                          ng_direction,
                                                          ng_symbol_id,
                                                          ng_graphsage_loss_kind,
                                                          double*);
extern ng_status ng_test_graphsage_cached_training_equivalence(const ng_graph*,
                                                               ng_direction,
                                                               ng_symbol_id,
                                                               double*);
extern ng_status ng_test_vector_index_approx_recall(double*, double*, double*, double*);
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
extern enum ng_test_import_stage ng_test_import_current_stage(void);
extern uint64_t ng_test_import_stage_mask(void);
#ifndef NAUTYLUS_CLI
#define NAUTYLUS_CLI "./nautylus"
#endif

static int edge_count(const ng_relationship* r, void* ctx) {
    (void)r;
    (*(size_t*)ctx)++;
    return 1;
}
static int node_count_cb(ng_node_id n, uint32_t d, void* ctx) {
    (void)n;
    (void)d;
    (*(size_t*)ctx)++;
    return 1;
}
static void write_import_files(void) {
    FILE *n = fopen("nodes.tsv", "wb"), *r = fopen("rels.tsv", "wb");
    assert(n && r);
    fputs("node\tx\tP,Q,P\ta=s:6f6e65\nnode\ty\tP\tb=s:74776f\n", n);
    fputs("relationship\tr\tx\tKNOWS\ty\tc=s:7468726565\n", r);
    fclose(n);
    fclose(r);
}
static void remove_import_files(void) {
    remove("nodes.tsv");
    remove("rels.tsv");
}
static int same_file(const char* a, const char* b) {
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb");
    int ca, cb;
    if (!x || !y) {
        if (x)
            fclose(x);
        if (y)
            fclose(y);
        return 0;
    }
    do {
        ca = fgetc(x);
        cb = fgetc(y);
        if (ca != cb) {
            fclose(x);
            fclose(y);
            return 0;
        }
    } while (ca != EOF && cb != EOF);
    fclose(x);
    fclose(y);
    return 1;
}
static int match_count_cb(ng_node_id n, void* ctx) {
    (void)n;
    (*(size_t*)ctx)++;
    return 1;
}
typedef struct {
    size_t count;
    size_t length;
    ng_node_id first, last;
} path_context;
static int path_count_cb(const ng_node_id* path, size_t length, void* ctx) {
    path_context* context = (path_context*)ctx;
    context->count++;
    context->length = length;
    context->first = path[0];
    context->last = path[length - 1];
    return 1;
}
static double zero_heuristic(ng_node_id node, ng_node_id target, void* context) {
    (void)node;
    (void)target;
    (void)context;
    return 0.0;
}
static double absd(double x) {
    return x < 0.0 ? -x : x;
}
static ng_status procedure_add(const ng_graph* graph,
                               const ng_procedure_argument* arguments,
                               size_t argument_count,
                               ng_procedure_result* result,
                               void* context) {
    (void)graph;
    (void)context;
    if (argument_count != 2 || arguments[0].kind != NG_PROCEDURE_SCALAR ||
        arguments[1].kind != NG_PROCEDURE_SCALAR || arguments[0].value.type != NG_VALUE_INT64 ||
        arguments[1].value.type != NG_VALUE_INT64 || !result || result->field_capacity < 1)
        return NG_PARSE_ERROR;
    result->fields[0].name = "value";
    result->fields[0].kind = NG_PROCEDURE_SCALAR;
    result->fields[0].value.type = NG_VALUE_INT64;
    result->fields[0].value.as.integer =
        arguments[0].value.as.integer + arguments[1].value.as.integer;
    result->field_count = 1;
    return NG_OK;
}
static ng_status procedure_node_id(const ng_graph* graph,
                                   const ng_procedure_argument* arguments,
                                   size_t argument_count,
                                   ng_procedure_result* result,
                                   void* context) {
    (void)graph;
    (void)context;
    if (argument_count != 1 ||
        (arguments[0].kind != NG_PROCEDURE_NODE &&
         arguments[0].kind != NG_PROCEDURE_RELATIONSHIP) ||
        !result || result->field_capacity < 1)
        return NG_PARSE_ERROR;
    result->fields[0].name = "nodeId";
    result->fields[0].kind = NG_PROCEDURE_SCALAR;
    result->fields[0].value.type = NG_VALUE_INT64;
    result->fields[0].value.as.integer = (int64_t)arguments[0].id;
    result->field_count = 1;
    return NG_OK;
}
static ng_status query_tmp(ng_graph* g, const char* q, int* mutated) {
    FILE* f = tmpfile();
    ng_status s;
    assert(f);
    s = ng_query_execute(g, q, f, mutated);
    assert(fclose(f) == 0);
    return s;
}
static ng_status
query_tmp_params(ng_graph* g, const char* q, const ng_parameter* p, size_t n, int* mutated) {
    FILE* f = tmpfile();
    ng_status s;
    assert(f);
    s = ng_query_execute_params(g, q, p, n, f, mutated);
    assert(fclose(f) == 0);
    return s;
}
static ng_status query_params_file(
    ng_graph* g, const char* q, const ng_parameter* p, size_t n, const char* path, int* mutated) {
    FILE* f = fopen(path, "wb");
    ng_status s;
    assert(f);
    s = ng_query_execute_params(g, q, p, n, f, mutated);
    assert(fclose(f) == 0);
    return s;
}
static void fixture(ng_graph** out, ng_node_id* old) {
    ng_symbol_id p, k;
    ng_value v;
    assert(ng_create(out, "test.ng") == NG_OK);
    assert(ng_symbol(*out, "Existing", &p) == NG_OK);
    assert(ng_symbol(*out, "name", &k) == NG_OK);
    assert(ng_node_create(*out, &p, 1, old) == NG_OK);
    v.type = NG_VALUE_STRING;
    v.length = 4;
    v.as.string = "keep";
    assert(ng_node_set(*out, *old, k, &v) == NG_OK);
    assert(ng_validate(*out) == NG_OK);
}
static void codec_tests(void) {
    char b[256];
    ng_value v, o;
    void* owned;
    size_t n;
    int64_t ints[] = {(int64_t)INT64_MIN, (int64_t)INT64_MAX, -1, 0, 1};
    size_t i;
    for (i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
        memset(&v, 0, sizeof(v));
        v.type = NG_VALUE_INT64;
        v.as.integer = ints[i];
        n = ng_test_encode_value(&v, b, sizeof(b));
        assert(n && ng_test_decode_value(b, &o, &owned) == NG_OK && o.type == v.type &&
               o.as.integer == v.as.integer && !owned);
    }
    for (i = 0; i < 2; i++) {
        uint64_t bits = i ? 0x8000000000000000ULL : 0;
        memset(&v, 0, sizeof(v));
        v.type = NG_VALUE_DOUBLE;
        memcpy(&v.as.real, &bits, 8);
        n = ng_test_encode_value(&v, b, sizeof(b));
        assert(n && ng_test_decode_value(b, &o, &owned) == NG_OK);
        memcpy(&bits, &o.as.real, 8);
        assert(bits == (i ? 0x8000000000000000ULL : 0) && !owned);
    }
    memset(&v, 0, sizeof(v));
    v.type = NG_VALUE_STRING;
    v.length = 7;
    v.as.string = "a\0\t\r\n\\x";
    n = ng_test_encode_value(&v, b, sizeof(b));
    assert(n && ng_test_decode_value(b, &o, &owned) == NG_OK && o.length == v.length &&
           !memcmp(o.as.string, v.as.string, v.length));
    free(owned);
    memset(&v, 0, sizeof(v));
    v.type = NG_VALUE_BYTES;
    v.length = 4;
    v.as.bytes = (const unsigned char*)"\0\t\r\n";
    n = ng_test_encode_value(&v, b, sizeof(b));
    assert(n && ng_test_decode_value(b, &o, &owned) == NG_OK && o.length == v.length &&
           !memcmp(o.as.bytes, v.as.bytes, v.length));
    free(owned);
    {
        const char* bad[] = {"",
                             "n:",
                             "n0",
                             "b:",
                             "b:2",
                             "b:true",
                             "i:",
                             "i:-",
                             "i:01",
                             "i:-0",
                             "i:+1",
                             "i: 1",
                             "i:1 ",
                             "i:9223372036854775808",
                             "i:-9223372036854775809",
                             "d:",
                             "d:0",
                             "d:000000000000000g",
                             "d:00000000000000000",
                             "s:0",
                             "s:zz",
                             "x:0",
                             "x:gg",
                             "q:anything"};
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            memset(&o, 0, sizeof(o));
            owned = (void*)1;
            assert(ng_test_decode_value(bad[i], &o, &owned) == NG_PARSE_ERROR && owned == NULL);
        }
    }
}
int main(void) {
    ng_graph *g, *r;
    ng_symbol_id p, w, k;
    ng_node_id a, b;
    ng_relationship_id e;
    ng_value v;
    size_t n;
    codec_tests();
    remove("test.ng");
    assert(ng_create(&g, "test.ng") == NG_OK);
    assert(ng_symbol(g, "Person", &p) == NG_OK);
    assert(ng_symbol(g, "WORKS_AT", &w) == NG_OK);
    assert(ng_symbol(g, "name", &k) == NG_OK);
    assert(ng_node_create(g, &p, 1, &a) == NG_OK);
    assert(ng_node_create(g, 0, 0, &b) == NG_OK);
    v.type = NG_VALUE_STRING;
    v.length = 5;
    v.as.string = "Alice";
    assert(ng_node_set(g, a, k, &v) == NG_OK);
    assert(ng_relationship_create(g, a, w, b, &e) == NG_OK);
    assert(ng_relationship_create(g, b, w, a, &e) == NG_OK);
    assert(ng_save(g) == NG_OK);
    ng_close(g);
    assert(ng_open(&r, "test.ng") == NG_OK);
    assert(ng_validate(r) == NG_OK);
    assert(ng_procedure_register(r, "addValues", procedure_add, NULL) == NG_OK);
    assert(ng_procedure_register(r, "nodeIdentity", procedure_node_id, NULL) == NG_OK);
    {
        FILE* procedure_output = tmpfile();
        int procedure_mutated = 0;
        char procedure_text[64] = {0};
        assert(procedure_output);
        assert(ng_query_execute(r,
                                "MATCH (a:Person) CALL addValues(id(a), 2) YIELD value AS total "
                                "RETURN total",
                                procedure_output,
                                &procedure_mutated) == NG_OK);
        assert(!procedure_mutated && fseek(procedure_output, 0, SEEK_SET) == 0);
        assert(fread(procedure_text, 1, sizeof(procedure_text) - 1, procedure_output) > 0);
        assert(!strcmp(procedure_text, "3\n"));
        fclose(procedure_output);
    }
    {
        FILE* list_output = tmpfile();
        int list_mutated = 0;
        char list_text[256] = {0};
        assert(list_output);
        assert(ng_query_execute(
                   r,
                   "WITH [1, 2, 3] AS xs RETURN xs[1], xs[-1], xs[1..3], size(xs), "
                   "head(xs), tail(xs), reverse(xs), xs + [4]",
                   list_output,
                   &list_mutated) == NG_OK);
        assert(!list_mutated && fseek(list_output, 0, SEEK_SET) == 0);
        assert(fread(list_text, 1, sizeof(list_text) - 1, list_output) > 0);
        assert(!strcmp(list_text,
                       "2\t3\t[2, 3]\t3\t1\t[2, 3]\t[3, 2, 1]\t[1, 2, 3, 4]\n"));
        fclose(list_output);
    }
    {
        FILE* scalar_output = tmpfile();
        int scalar_mutated = 0;
        char scalar_text[256] = {0};
        assert(scalar_output);
        assert(ng_query_execute(
                   r,
                   "WITH [1, 2, 3] AS xs RETURN toString(xs), size(\"abc\"), "
                   "coalesce(null, \"fallback\"), coalesce(null, xs[0])",
                   scalar_output,
                   &scalar_mutated) == NG_OK);
        assert(!scalar_mutated && fseek(scalar_output, 0, SEEK_SET) == 0);
        assert(fread(scalar_text, 1, sizeof(scalar_text) - 1, scalar_output) > 0);
        assert(!strcmp(scalar_text, "[1, 2, 3]\t3\tfallback\t1\n"));
        fclose(scalar_output);
    }
    {
        FILE* function_output = tmpfile();
        int function_mutated = 0;
        char function_text[128] = {0};
        assert(function_output);
        assert(ng_query_execute(
                   r,
                   "WITH 1 AS seed RETURN toLower(\" HeLLo \"), toUpper(\"hello\"), trim(\"  x  \"), "
                   "abs(-4), abs(1.5), toLower(null)",
                   function_output,
                   &function_mutated) == NG_OK);
        assert(!function_mutated && fseek(function_output, 0, SEEK_SET) == 0);
        assert(fread(function_text, 1, sizeof(function_text) - 1, function_output) > 0);
        assert(!strcmp(function_text, " hello \tHELLO\tx\t4\t1.5\tnull\n"));
        fclose(function_output);
    }
    {
        FILE* comprehension_output = tmpfile();
        int comprehension_mutated = 0;
        char comprehension_text[128] = {0};
        assert(comprehension_output);
        assert(ng_query_execute(
                   r,
                   "WITH [1, 2, 3] AS xs RETURN [x IN xs WHERE x > 1 | x * 2]",
                   comprehension_output,
                   &comprehension_mutated) == NG_OK);
        assert(!comprehension_mutated && fseek(comprehension_output, 0, SEEK_SET) == 0);
        assert(fread(comprehension_text, 1, sizeof(comprehension_text) - 1, comprehension_output) > 0);
        assert(!strcmp(comprehension_text, "[4, 6]\n"));
        fclose(comprehension_output);
    }
    {
        FILE* case_output = tmpfile();
        int case_mutated = 0;
        char case_text[128] = {0};
        assert(case_output);
        assert(ng_query_execute(
                   r,
                   "WITH 7 AS score RETURN CASE WHEN score >= 10 THEN \"high\" "
                   "WHEN score >= 5 THEN \"medium\" ELSE \"low\" END",
                   case_output,
                   &case_mutated) == NG_OK);
        assert(!case_mutated && fseek(case_output, 0, SEEK_SET) == 0);
        assert(fread(case_text, 1, sizeof(case_text) - 1, case_output) > 0);
        assert(!strcmp(case_text, "medium\n"));
        fclose(case_output);
        case_output = tmpfile();
        memset(case_text, 0, sizeof(case_text));
        assert(case_output);
        case_mutated = 0;
        assert(ng_query_execute(
                   r,
                   "WITH 2 AS score RETURN CASE score WHEN 1 THEN \"low\" "
                   "WHEN 2 THEN \"medium\" ELSE \"high\" END",
                   case_output,
                   &case_mutated) == NG_OK);
        assert(!case_mutated && fseek(case_output, 0, SEEK_SET) == 0);
        assert(fread(case_text, 1, sizeof(case_text) - 1, case_output) > 0);
        assert(!strcmp(case_text, "medium\n"));
        fclose(case_output);
    }
    {
        FILE* path_output = tmpfile();
        int path_mutated = 0;
        char path_text[128] = {0};
        assert(path_output);
        assert(ng_query_execute(
                   r,
                   "MATCH p=(a:Person)-[r:WORKS_AT]->(b) "
                   "RETURN size(nodes(p)), size(relationships(p)), nodes(p)[0], "
                   "relationships(p)[0]",
                   path_output,
                   &path_mutated) == NG_OK);
        assert(!path_mutated && fseek(path_output, 0, SEEK_SET) == 0);
        assert(fread(path_text, 1, sizeof(path_text) - 1, path_output) > 0);
        assert(!strcmp(path_text, "2\t1\t1\t1\n"));
        fclose(path_output);
        path_output = tmpfile();
        memset(path_text, 0, sizeof(path_text));
        assert(path_output);
        assert(ng_query_execute(
                   r,
                   "MATCH p=(a:Person)-[*1..2]->(b) "
                   "RETURN size(nodes(p)), size(relationships(p))",
                   path_output,
                   &path_mutated) == NG_OK);
        assert(!path_mutated && fseek(path_output, 0, SEEK_SET) == 0);
        assert(fread(path_text, 1, sizeof(path_text) - 1, path_output) > 0);
        assert(!strcmp(path_text, "2\t1\n3\t2\n"));
        fclose(path_output);
        path_output = tmpfile();
        memset(path_text, 0, sizeof(path_text));
        assert(path_output);
        assert(ng_query_execute(
                   r,
                   "MATCH p=(a:Person)-[r:WORKS_AT]->(b) RETURN p",
                   path_output,
                   &path_mutated) == NG_OK);
        assert(!path_mutated && fseek(path_output, 0, SEEK_SET) == 0);
        assert(fread(path_text, 1, sizeof(path_text) - 1, path_output) > 0);
        assert(!strcmp(path_text, "{nodes: [1, 2], relationships: [1]}\n"));
        fclose(path_output);
    }
    {
        FILE* merge_output = tmpfile();
        int merge_mutated = 0;
        char merge_text[128] = {0};
        assert(merge_output);
        assert(ng_query_execute(
                   r,
                   "WITH 1 AS seed MERGE (n:MergeHook {name: \"A\"}) "
                   "ON CREATE SET n.state = \"created\" RETURN n.state",
                   merge_output,
                   &merge_mutated) == NG_OK);
        assert(merge_mutated && fseek(merge_output, 0, SEEK_SET) == 0);
        assert(fread(merge_text, 1, sizeof(merge_text) - 1, merge_output) > 0);
        assert(!strcmp(merge_text, "created\n"));
        fclose(merge_output);
        merge_output = tmpfile();
        memset(merge_text, 0, sizeof(merge_text));
        assert(merge_output);
        merge_mutated = 0;
        assert(ng_query_execute(
                   r,
                   "WITH 1 AS seed MERGE (n:MergeHook {name: \"A\"}) "
                   "ON MATCH SET n.state = \"matched\" RETURN n.state",
                   merge_output,
                   &merge_mutated) == NG_OK);
        assert(merge_mutated && fseek(merge_output, 0, SEEK_SET) == 0);
        assert(fread(merge_text, 1, sizeof(merge_text) - 1, merge_output) > 0);
        assert(!strcmp(merge_text, "matched\n"));
        fclose(merge_output);
    }
    {
        FILE* procedure_output = tmpfile();
        int procedure_mutated = 0;
        char procedure_text[64] = {0};
        assert(procedure_output);
        assert(ng_query_execute(r,
                                "MATCH (a:Person) CALL nodeIdentity(a) YIELD nodeId AS id "
                                "RETURN id",
                                procedure_output,
                                &procedure_mutated) == NG_OK);
        assert(!procedure_mutated && fseek(procedure_output, 0, SEEK_SET) == 0);
        assert(fread(procedure_text, 1, sizeof(procedure_text) - 1, procedure_output) > 0);
        assert(!strcmp(procedure_text, "1\n"));
        fclose(procedure_output);
    }
    {
        FILE* procedure_output = tmpfile();
        int procedure_mutated = 0;
        char procedure_text[64] = {0};
        assert(procedure_output);
        assert(ng_query_execute(r,
                                "MATCH (a)-[rel:WORKS_AT]->(b) CALL nodeIdentity(rel) "
                                "YIELD nodeId AS id RETURN id",
                                procedure_output,
                                &procedure_mutated) == NG_OK);
        assert(!procedure_mutated && fseek(procedure_output, 0, SEEK_SET) == 0);
        assert(fread(procedure_text, 1, sizeof(procedure_text) - 1, procedure_output) > 0);
        assert(!strcmp(procedure_text, "1\n2\n"));
        fclose(procedure_output);
    }
    assert(ng_procedure_unregister(r, "addValues") == NG_OK);
    assert(ng_procedure_unregister(r, "nodeIdentity") == NG_OK);
    {
        FILE* batch_output = tmpfile();
        int batch_mutated = 0;
        char batch_text[128] = {0};
        assert(batch_output);
        assert(ng_query_execute(r,
                                "CREATE (first:Batch {name: \"one\"}); "
                                "CREATE (second:Batch {name: \"two\"}); "
                                "MATCH (n:Batch) RETURN n.name ORDER BY n.name",
                                batch_output,
                                &batch_mutated) == NG_OK);
        assert(batch_mutated && fseek(batch_output, 0, SEEK_SET) == 0);
        assert(fread(batch_text, 1, sizeof(batch_text) - 1, batch_output) > 0);
        assert(!strcmp(batch_text, "one\ntwo\n"));
        fclose(batch_output);
    }
    n = 0;
    assert(ng_node_relationships(r, a, NG_DIRECTION_OUTGOING, w, edge_count, &n) == NG_OK &&
           n == 1);
    n = 0;
    assert(ng_traverse(r, a, 0, node_count_cb, &n) == NG_OK && n == 2);
    n = 0;
    assert(ng_query_nodes(
               r, "MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    n = 0;
    assert(ng_query_nodes(r, "MATCH (n:Person) WHERE id(n) = 1 RETURN n", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    n = 0;
    assert(ng_query_nodes(r, "MATCH (n:Person) WHERE n.id = 1 RETURN n", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    n = 0;
    assert(ng_query_nodes(r, "MATCH (n) RETURN n LIMIT 1", match_count_cb, &n) == NG_OK && n == 1);
    n = 0;
    assert(ng_query_nodes(r, "MATCH (n:Person)-[:WORKS_AT]->(m) RETURN m", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    n = 0;
    assert(ng_query_nodes(
               r, "MATCH (n:Person)-[:WORKS_AT*1..2]->(m:Person) RETURN m", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    n = 0;
    assert(ng_query_nodes(r,
                          "MATCH (n)-[:WORKS_AT]->(m:Person) WHERE m.name = \"Alice\" RETURN n",
                          match_count_cb,
                          &n) == NG_OK &&
           n == 1);
    {
        FILE* f = fopen("projection.out", "wb");
        assert(f);
        assert(ng_query_print(r, "MATCH (n:Person) RETURN n.name", f) == NG_OK);
        assert(fclose(f) == 0);
        f = fopen("projection.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("projection.out", "projection.expected"));
        remove("projection.out");
        remove("projection.expected");
    }
    {
        FILE* f = fopen("columns.out", "wb");
        assert(f);
        assert(ng_query_print(r, "MATCH (n:Person)-[:WORKS_AT]->(m) RETURN n.name, m.id", f) ==
               NG_OK);
        assert(fclose(f) == 0);
        f = fopen("columns.expected", "wb");
        assert(f);
        fputs("Alice\t2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("columns.out", "columns.expected"));
        remove("columns.out");
        remove("columns.expected");
    }
    {
        FILE* f = fopen("multi.out", "wb");
        assert(f);
        assert(ng_query_print(r, "MATCH (n:Person)-[:WORKS_AT*2]->(m:Person) RETURN m.name", f) ==
               NG_OK);
        assert(fclose(f) == 0);
        f = fopen("multi.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("multi.out", "multi.expected"));
        remove("multi.out");
        remove("multi.expected");
    }
    {
        char plan[128];
        assert(ng_query_explain("MATCH (n:Person)-[:WORKS_AT*1..2]->(m) RETURN m LIMIT 1",
                                plan,
                                sizeof(plan)) == NG_OK &&
               strstr(plan, "Expand") != NULL);
    }
    assert(ng_query_nodes(r, "MATCH n RETURN n", match_count_cb, &n) == NG_PARSE_ERROR);
    assert(ng_query_nodes(r, "MATCH (n) RETURN m", match_count_cb, &n) == NG_PARSE_ERROR);
    n = 0;
    assert(ng_query_nodes(
               r, "MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n.name", match_count_cb, &n) ==
               NG_OK &&
           n == 1);
    assert(ng_query_nodes(r, "MATCH (n)-[:WORKS_AT*1..65]->(m) RETURN m", match_count_cb, &n) ==
           NG_PARSE_ERROR);
    ng_close(r);
    remove("test.ng");
    {
        ng_symbol_id rel;
        ng_symbol_id weight;
        ng_symbol_id person;
        ng_node_id c, d, iso;
        ng_relationship_id rid, rid_ab, rid_bc, rid_ca, rid_cd;
        ng_node_id shortest[8];
        ng_node_score scores[8];
        ng_node_metric metrics[8];
        ng_node_component comps[8];
        ng_node_id order[8];
        size_t count, i;
        uint64_t u;
        double sum;
        assert(ng_create(&g, "analytics.ng") == NG_OK);
        assert(ng_symbol(g, "R", &rel) == NG_OK);
        assert(ng_symbol(g, "weight", &weight) == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_node_create(g, &person, 1, &a) == NG_OK);
        assert(ng_node_create(g, &person, 1, &b) == NG_OK);
        assert(ng_node_create(g, &person, 1, &c) == NG_OK);
        assert(ng_node_create(g, 0, 0, &d) == NG_OK);
        assert(ng_node_create(g, 0, 0, &iso) == NG_OK);
        assert(ng_relationship_create(g, a, rel, b, &rid_ab) == NG_OK);
        assert(ng_relationship_create(g, b, rel, c, &rid_bc) == NG_OK);
        assert(ng_relationship_create(g, c, rel, a, &rid_ca) == NG_OK);
        assert(ng_relationship_create(g, c, rel, d, &rid_cd) == NG_OK);
        memset(&v, 0, sizeof(v));
        v.type = NG_VALUE_INT64;
        v.as.integer = 5;
        assert(ng_relationship_set(g, rid_ab, weight, &v) == NG_OK);
        v.as.integer = 2;
        assert(ng_relationship_set(g, rid_bc, weight, &v) == NG_OK);
        v.as.integer = 10;
        assert(ng_relationship_set(g, rid_ca, weight, &v) == NG_OK);
        v.as.integer = 1;
        assert(ng_relationship_set(g, rid_cd, weight, &v) == NG_OK);
        count = 0;
        assert(ng_dijkstra(g, a, c, NG_DIRECTION_OUTGOING, rel, weight, shortest, 8, &count, &sum) ==
                   NG_OK &&
               count == 3 && shortest[0] == a && shortest[1] == b && shortest[2] == c &&
               sum == 7.0);
        assert(ng_dijkstra(g, a, iso, NG_DIRECTION_OUTGOING, rel, weight, shortest, 8, &count, &sum) ==
               NG_NOT_FOUND &&
               count == 0);
        count = 0;
        assert(ng_a_star(g,
                         a,
                         c,
                         NG_DIRECTION_OUTGOING,
                         rel,
                         weight,
                         zero_heuristic,
                         NULL,
                         shortest,
                         8,
                         &count,
                         &sum) == NG_OK &&
               count == 3 && shortest[0] == a && shortest[1] == b && shortest[2] == c &&
               sum == 7.0);
        count = 0;
        assert(ng_bfs_path(g, a, d, NG_DIRECTION_OUTGOING, rel, shortest, 8, &count) == NG_OK &&
               count == 4 && shortest[0] == a && shortest[1] == b && shortest[2] == c &&
               shortest[3] == d);
        assert(ng_bfs_path(g, a, iso, NG_DIRECTION_OUTGOING, rel, shortest, 8, &count) ==
               NG_NOT_FOUND &&
               count == 0);
        {
            path_context paths = {0};
            assert(ng_enumerate_paths(g,
                                      a,
                                      d,
                                      NG_DIRECTION_OUTGOING,
                                      rel,
                                      4,
                                      4,
                                      path_count_cb,
                                      &paths,
                                      &count) == NG_OK);
            assert(count == 1 && paths.count == 1 && paths.length == 4 && paths.first == a &&
                   paths.last == d);
        }
        count = 0;
        assert(ng_degree_centrality(g, NG_DIRECTION_OUTGOING, rel, scores, 8, &count) == NG_OK &&
               count == 5);
        assert(scores[0].node == a && scores[0].score == 1.0 && scores[1].score == 1.0 &&
               scores[2].score == 2.0 && scores[3].score == 0.0 && scores[4].score == 0.0);
        assert(ng_degree_centrality(g, NG_DIRECTION_INCOMING, rel, scores, 8, &count) == NG_OK);
        assert(scores[0].score == 1.0 && scores[1].score == 1.0 && scores[2].score == 1.0 &&
               scores[3].score == 1.0 && scores[4].score == 0.0);
        assert(ng_degree_centrality(g, NG_DIRECTION_EITHER, rel, scores, 8, &count) == NG_OK);
        assert(scores[0].score == 2.0 && scores[1].score == 2.0 && scores[2].score == 3.0 &&
               scores[3].score == 1.0 && scores[4].score == 0.0);
        assert(ng_degree_centrality(g, NG_DIRECTION_EITHER, 999999, scores, 8, &count) ==
               NG_NOT_FOUND);
        assert(ng_degree_centrality(g, NG_DIRECTION_EITHER, rel, 0, 0, &count) == NG_LIMIT &&
               count == 5);
        assert(ng_pagerank(g, rel, 0.85, 25, scores, 8, &count) == NG_OK && count == 5);
        sum = 0.0;
        for (i = 0; i < count; i++) {
            assert(scores[i].score > 0.0);
            sum += scores[i].score;
        }
        assert(absd(sum - 1.0) < 0.000001);
        assert(ng_eigenvector_centrality(g, NG_DIRECTION_EITHER, rel, 25, scores, 8, &count) ==
                   NG_OK &&
               count == 5 && scores[0].score > 0.0 && scores[1].score > 0.0 &&
               scores[2].score > scores[4].score);
        assert(ng_closeness_centrality(g, NG_DIRECTION_EITHER, rel, scores, 8, &count) == NG_OK &&
               count == 5 && scores[0].score > 0.0 && scores[2].score > scores[4].score);
        assert(ng_harmonic_centrality(g, NG_DIRECTION_EITHER, rel, scores, 8, &count) == NG_OK &&
               count == 5 && scores[0].score > 0.0 && scores[2].score > scores[4].score);
        {
            double embedding[15];
            size_t embedding_count = 0;
            assert(ng_fastrp(g, NG_DIRECTION_EITHER, rel, 2, 3, 7, embedding, 15, &embedding_count) ==
                       NG_OK &&
                   embedding_count == 5 &&
                   (embedding[0] != 0.0 || embedding[1] != 0.0 || embedding[2] != 0.0));
        }
        {
            const double features[10] = {1.0, 0.0, 0.0, 1.0, 1.0,
                                         1.0, 0.5, 0.5, 0.2, 0.8};
            double embedding[15];
            size_t embedding_count = 0;
            assert(ng_graphsage(g, NG_DIRECTION_EITHER, rel, 2, 2, 3, features, 7,
                                embedding, 15, &embedding_count) == NG_OK &&
                   embedding_count == 5 &&
                   (embedding[0] != 0.0 || embedding[1] != 0.0 || embedding[2] != 0.0));
        }
        {
            const double features[10] = {1.0, 0.0, 0.0, 1.0, 1.0,
                                         1.0, 0.5, 0.5, 0.2, 0.8};
            double embedding[15], loaded_embedding[15];
            ng_graphsage_config config = {2, 2, 3, 1, 1, 42};
            ng_graphsage_model* model = NULL;
            ng_graphsage_model* loaded = NULL;
            ng_vector_score nearest[2];
            size_t node_count = 0, nearest_count = 0;
            assert(ng_graphsage_model_create(&config, &model) == NG_OK);
            assert(ng_graphsage_model_infer(model, g, NG_DIRECTION_EITHER, rel, features,
                                            embedding, 15, &node_count) == NG_OK &&
                   node_count == 5);
            assert(ng_graphsage_model_infer(model, g, NG_DIRECTION_EITHER, rel, features,
                                            loaded_embedding, 15, &node_count) == NG_OK);
            for (i = 0; i < 15; i++)
                assert(absd(embedding[i] - loaded_embedding[i]) < 0.0000001);
            assert(ng_graphsage_model_save(model, "graphsage.model") == NG_OK);
            assert(ng_graphsage_model_load("graphsage.model", &loaded) == NG_OK);
            assert(ng_graphsage_model_infer(loaded, g, NG_DIRECTION_EITHER, rel, features,
                                            loaded_embedding, 15, &node_count) == NG_OK);
            for (i = 0; i < 15; i++)
                assert(absd(embedding[i] - loaded_embedding[i]) < 0.0000001);
            assert(ng_vector_search_cosine(embedding, 5, 3, embedding, 2, nearest, 2,
                                           &nearest_count) == NG_OK &&
                   nearest_count == 2 && nearest[0].index == 0 && nearest[0].score > 0.99);
            {
                ng_vector_index* vector_index = NULL;
                ng_vector_index* loaded_index = NULL;
                ng_vector_index* tuned_index = NULL;
                ng_vector_score index_hits[2], approx_hits[2], loaded_hits[2];
                ng_vector_hnsw_config hnsw_config = {4, 6, 6};
                size_t index_count = 0, loaded_count = 0;
                assert(ng_vector_index_create(embedding, 5, 3, &vector_index) == NG_OK);
                assert(ng_vector_index_create_hnsw(embedding, 5, 3, &hnsw_config,
                                                   &tuned_index) == NG_OK);
                assert(ng_vector_index_search_cosine(vector_index, embedding, 2,
                                                     index_hits, 2, &index_count) == NG_OK &&
                       index_count == 2 && index_hits[0].index == 0 &&
                       index_hits[0].score > 0.99);
                assert(ng_vector_index_search_approx_cosine(vector_index, embedding, 2, 4,
                                                            approx_hits, 2,
                                                            &index_count) == NG_OK &&
                       index_count == 2 && approx_hits[0].index == 0 &&
                       approx_hits[0].score > 0.99);
                assert(ng_vector_index_save(vector_index, "vectors.ngv") == NG_OK);
                assert(ng_vector_index_load("vectors.ngv", &loaded_index) == NG_OK);
                assert(ng_vector_index_search_cosine(loaded_index, embedding, 2,
                                                     loaded_hits, 2, &loaded_count) == NG_OK &&
                       loaded_count == index_count);
                for (i = 0; i < loaded_count; i++) {
                    assert(loaded_hits[i].index == index_hits[i].index);
                    assert(absd(loaded_hits[i].score - index_hits[i].score) < 0.0000001);
                }
                assert(ng_vector_index_search_approx_cosine(loaded_index, embedding, 1, 2,
                                                            loaded_hits, 1,
                                                            &loaded_count) == NG_OK &&
                       loaded_count == 1 && loaded_hits[0].index == 0);
                assert(ng_vector_index_search_ann_cosine(loaded_index, embedding, 1, 2, 5,
                                                         loaded_hits, 1,
                                                         &loaded_count) == NG_OK &&
                       loaded_count == 1 && loaded_hits[0].index == 0);
                assert(ng_vector_index_search_hnsw_cosine(loaded_index, embedding, 1, 6,
                                                          loaded_hits, 1,
                                                          &loaded_count) == NG_OK &&
                       loaded_count == 1 && loaded_hits[0].index == 0);
                assert(ng_vector_index_search_hnsw_cosine(tuned_index, embedding, 1, 3,
                                                          loaded_hits, 1,
                                                          &loaded_count) == NG_OK &&
                       loaded_count == 1 && loaded_hits[0].index == 0);
                {
                    double full_recall = 0.0, partial_recall = 0.0, ann_recall = 0.0;
                    double hnsw_recall = 0.0;
                    assert(ng_test_vector_index_approx_recall(&full_recall,
                                                              &partial_recall,
                                                              &ann_recall,
                                                              &hnsw_recall) == NG_OK);
                    assert(full_recall == 1.0);
                    assert(partial_recall >= 0.0 && partial_recall <= 1.0);
                    assert(ann_recall >= 0.0 && ann_recall <= 1.0);
                    assert(hnsw_recall >= 0.0 && hnsw_recall <= 1.0);
                }
                assert(ng_vector_index_create(NULL, 5, 3, &loaded_index) ==
                       NG_INVALID_ARGUMENT);
                ng_vector_index_free(loaded_index);
                ng_vector_index_free(tuned_index);
                ng_vector_index_free(vector_index);
                remove("vectors.ngv");
            }
            ng_graphsage_model_free(loaded);
            ng_graphsage_model_free(model);
            remove("graphsage.model");
            {
                double max_gradient_delta = 1.0;
                assert(ng_test_graphsage_single_layer_mse_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 2, 1, 0,
                           NG_GRAPHSAGE_LOSS_MSE, &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 2, 1, 0,
                           NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 3, 1, 0,
                           NG_GRAPHSAGE_LOSS_MSE, &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 3, 1, 1,
                           NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 3, 0, 1,
                           NG_GRAPHSAGE_LOSS_MSE, &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_finite_difference_gradient_check(
                           g, NG_DIRECTION_EITHER, rel, 3, 1, 1,
                           NG_GRAPHSAGE_LOSS_SOFTMAX_CROSS_ENTROPY,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.00001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_evaluation_equivalence(
                           g, NG_DIRECTION_EITHER, rel, NG_GRAPHSAGE_LOSS_MSE,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.0000001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_evaluation_equivalence(
                           g, NG_DIRECTION_EITHER, rel,
                           NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.0000001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_evaluation_equivalence(
                           g, NG_DIRECTION_EITHER, rel,
                           NG_GRAPHSAGE_LOSS_SOFTMAX_CROSS_ENTROPY,
                           &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.0000001);
                max_gradient_delta = 1.0;
                assert(ng_test_graphsage_cached_training_equivalence(
                           g, NG_DIRECTION_EITHER, rel, &max_gradient_delta) == NG_OK);
                assert(max_gradient_delta < 0.0000001);
            }
            assert(ng_graphsage_model_create(&config, &model) == NG_OK);
            {
                const double targets[15] = {0.2, 0.1, 0.0, 0.1, 0.2,
                                            0.0, 0.2, 0.1, 0.1, 0.0,
                                            0.2, 0.1, 0.0, 0.1, 0.2};
                double loss = -1.0;
                assert(ng_graphsage_model_train(model, g, NG_DIRECTION_EITHER, rel, features,
                                                targets, 1, 0.01, &loss) == NG_OK &&
                       loss >= 0.0);
            }
            {
                const double targets[15] = {0.2, 0.1, 0.0, 0.1, 0.2,
                                            0.0, 0.2, 0.1, 0.1, 0.0,
                                            0.2, 0.1, 0.0, 0.1, 0.2};
                ng_graphsage_training_options options = {1, 0.01, 2, 0.2, 9,
                                                        NG_GRAPHSAGE_LOSS_MSE};
                ng_graphsage_training_report report;
                assert(ng_graphsage_model_train_ex(model, g, NG_DIRECTION_EITHER, rel,
                                                   features, targets, &options, &report) == NG_OK &&
                       report.training_samples == 4 && report.validation_samples == 1 &&
                       report.training_loss >= 0.0 && report.validation_loss >= 0.0 &&
                       report.training_accuracy == 0.0 && report.validation_accuracy == 0.0);
                options.loss = NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY;
                assert(ng_graphsage_model_train_ex(model, g, NG_DIRECTION_EITHER, rel,
                                                   features, targets, &options, &report) == NG_OK &&
                       report.training_loss >= 0.0 &&
                       report.training_accuracy >= 0.0 && report.training_accuracy <= 1.0 &&
                       report.training_precision >= 0.0 && report.training_precision <= 1.0 &&
                       report.training_recall >= 0.0 && report.training_recall <= 1.0);
                {
                    double train_history[4] = {-1.0, -1.0, -1.0, -1.0};
                    double validation_history[4] = {-1.0, -1.0, -1.0, -1.0};
                    size_t validation_rows[2] = {99, 99};
                    ng_graphsage_training_diagnostics diagnostics;
                    memset(&diagnostics, 0, sizeof(diagnostics));
                    diagnostics.epoch_training_losses = train_history;
                    diagnostics.epoch_validation_losses = validation_history;
                    diagnostics.epoch_capacity = 4;
                    diagnostics.validation_rows = validation_rows;
                    diagnostics.validation_row_capacity = 2;
                    options.epochs = 3;
                    options.loss = NG_GRAPHSAGE_LOSS_MSE;
                    assert(ng_graphsage_model_train_ex_diagnostics(
                               model, g, NG_DIRECTION_EITHER, rel, features, targets,
                               &options, &report, &diagnostics) == NG_OK);
                    assert(diagnostics.epoch_count == 3 && diagnostics.epochs_run == 3);
                    assert(!diagnostics.converged);
                    assert(diagnostics.validation_start == 4 &&
                           diagnostics.validation_row_count == 1 &&
                           diagnostics.validation_seed == options.seed);
                    assert(diagnostics.batch_count == 6 &&
                           diagnostics.cached_forward_reuses == diagnostics.batch_count &&
                           diagnostics.sampled_neighbor_count > 0 &&
                           diagnostics.gradient_buffer_bytes > 0 &&
                           diagnostics.subgraph_node_references > 0 &&
                           diagnostics.subgraph_edge_references > 0);
                    assert(diagnostics.subgraph_node_references <
                           diagnostics.batch_count * 5 * 2 * config.layers);
                    assert(validation_rows[0] == 4 && validation_rows[1] == 99);
                    assert(train_history[0] >= 0.0 && train_history[1] >= 0.0 &&
                           train_history[2] >= 0.0 && train_history[3] == -1.0);
                    assert(validation_history[0] >= 0.0 && validation_history[1] >= 0.0 &&
                           validation_history[2] >= 0.0 && validation_history[3] == -1.0);
                    memset(&diagnostics, 0, sizeof(diagnostics));
                    diagnostics.epoch_training_losses = train_history;
                    diagnostics.epoch_capacity = 4;
                    diagnostics.convergence_tolerance = 1000.0;
                    assert(ng_graphsage_model_train_ex_diagnostics(
                               model, g, NG_DIRECTION_EITHER, rel, features, targets,
                               &options, &report, &diagnostics) == NG_OK);
                    assert(diagnostics.converged && diagnostics.epochs_run == 2 &&
                           diagnostics.epoch_count == 2 &&
                           diagnostics.convergence_delta >= 0.0);
                }
                {
                    const double class_targets[15] = {1.0, 0.0, 0.0,
                                                      0.0, 1.0, 0.0,
                                                      0.0, 0.0, 1.0,
                                                      1.0, 0.0, 0.0,
                                                      0.0, 1.0, 0.0};
                    const double bad_class_targets[15] = {1.0, 0.0, 0.0,
                                                          0.0, 1.0, 0.0,
                                                          0.0, 0.0, 1.0,
                                                          1.0, 0.0, 0.0,
                                                          0.0, 0.5, 0.0};
                    options.epochs = 1;
                    options.loss = NG_GRAPHSAGE_LOSS_SOFTMAX_CROSS_ENTROPY;
                    assert(ng_graphsage_model_train_ex(model, g, NG_DIRECTION_EITHER, rel,
                                                       features, class_targets, &options,
                                                       &report) == NG_OK &&
                           report.training_loss >= 0.0 &&
                           report.training_accuracy >= 0.0 && report.training_accuracy <= 1.0 &&
                           report.training_precision >= 0.0 && report.training_precision <= 1.0 &&
                           report.training_recall >= 0.0 && report.training_recall <= 1.0);
                    assert(ng_graphsage_model_train_ex(model, g, NG_DIRECTION_EITHER, rel,
                                                       features, bad_class_targets, &options,
                                                       &report) == NG_INVALID_ARGUMENT);
                    {
                        double probabilities[15], confidence[5];
                        size_t classes[5];
                        size_t prediction_count = 0;
                        assert(ng_graphsage_model_predict_probabilities(
                                   model, g, NG_DIRECTION_EITHER, rel, features,
                                   probabilities, 15, &prediction_count) == NG_OK &&
                               prediction_count == 5);
                        for (i = 0; i < prediction_count; i++) {
                            double row_sum = probabilities[i * 3] +
                                             probabilities[i * 3 + 1] +
                                             probabilities[i * 3 + 2];
                            assert(absd(row_sum - 1.0) < 0.000001);
                        }
                        assert(ng_graphsage_model_predict_classes(
                                   model, g, NG_DIRECTION_EITHER, rel, features,
                                   classes, confidence, 5, &prediction_count) == NG_OK &&
                               prediction_count == 5);
                        for (i = 0; i < prediction_count; i++) {
                            double best = probabilities[i * 3 + classes[i]];
                            assert(classes[i] < 3);
                            assert(absd(best - confidence[i]) < 0.000001);
                        }
                        assert(ng_graphsage_model_predict_classes(
                                   model, g, NG_DIRECTION_EITHER, rel, features,
                                   classes, confidence, 4, &prediction_count) == NG_LIMIT &&
                               prediction_count == 5);
                    }
                }
            }
            ng_graphsage_model_free(model);
            assert(ng_vector_search_cosine(embedding, 5, 3, embedding, 2, nearest, 1,
                                           &nearest_count) == NG_INVALID_ARGUMENT);
        }
        {
            double embedding[15];
            size_t embedding_count = 0;
            assert(ng_node2vec(g, NG_DIRECTION_OUTGOING, rel, 1.0, 1.0, 2, 4, 3, 7,
                               embedding, 15, &embedding_count) == NG_OK &&
                   embedding_count == 5 &&
                   (embedding[0] != 0.0 || embedding[1] != 0.0 || embedding[2] != 0.0));
        }
        assert(ng_weakly_connected_components(g, rel, comps, 8, &count) == NG_OK && count == 5);
        assert(comps[0].component == 0 && comps[1].component == 0 && comps[2].component == 0 &&
               comps[3].component == 0 && comps[4].component == 1);
        assert(ng_label_propagation(g, NG_DIRECTION_EITHER, rel, 10, comps, 8, &count) == NG_OK &&
               count == 5 && comps[0].component == comps[1].component &&
               comps[1].component == comps[2].component && comps[2].component == comps[3].component &&
               comps[4].component != comps[0].component);
        assert(ng_louvain(g, rel, 10, comps, 8, &count) == NG_OK && count == 5);
        {
            ng_node_id points[8];
            ng_relationship_id bridges[8];
            size_t point_count = 0, bridge_count = 0;
            assert(ng_articulation_points(g, rel, points, 8, &point_count) == NG_OK &&
                   point_count == 1 && points[0] == c);
        assert(ng_bridges(g, rel, bridges, 8, &bridge_count) == NG_OK && bridge_count == 1 &&
                   bridges[0] == rid_cd);
            assert(ng_minimum_spanning_tree(g, rel, weight, bridges, 8, &bridge_count, &sum) ==
                       NG_OK &&
                   bridge_count == 3 && sum == 8.0);
            assert(ng_max_flow(g, a, d, rel, weight, &sum) == NG_OK && sum == 1.0);
        }
        {
            ng_link_score similar[2];
            assert(ng_knn(g, a, NG_DIRECTION_EITHER, rel, 2, similar, 2, &count) == NG_OK &&
                   count == 2 && similar[0].source == a && similar[1].source == a &&
                   similar[0].score >= similar[1].score);
            assert(ng_knn_filtered(g, a, NG_DIRECTION_EITHER, rel, person, 2, similar, 2, &count) ==
                       NG_OK &&
                   count == 2 && similar[0].target != d && similar[1].target != d);
        }
        assert(ng_strongly_connected_components(g, rel, comps, 8, &count) == NG_OK && count == 5);
        assert(comps[0].component == 0 && comps[1].component == 0 && comps[2].component == 0 &&
               comps[3].component == 1 && comps[4].component == 2);
        assert(ng_triangle_count(g, rel, metrics, 8, &count) == NG_OK && count == 5);
        assert(metrics[0].value == 1 && metrics[1].value == 1 && metrics[2].value == 1 &&
               metrics[3].value == 0 && metrics[4].value == 0);
        assert(ng_local_clustering_coefficient(g, rel, scores, 8, &count) == NG_OK && count == 5);
        assert(scores[0].score == 1.0 && scores[1].score == 1.0 && scores[2].score > 0.333 &&
               scores[2].score < 0.334 && scores[3].score == 0.0 && scores[4].score == 0.0);
        assert(ng_common_neighbors(g, a, c, rel, &u) == NG_OK && u == 1);
        assert(ng_total_neighbors(g, a, c, rel, &u) == NG_OK && u == 4);
        assert(ng_preferential_attachment(g, a, c, rel, &u) == NG_OK && u == 6);
        assert(ng_adamic_adar(g, a, b, rel, &sum) == NG_OK && sum > 0.91 && sum < 0.92);
        assert(ng_resource_allocation(g, a, b, rel, &sum) == NG_OK && sum > 0.32 && sum < 0.34);
        assert(ng_topological_sort(g, rel, order, 8, &count) == NG_EXISTS && count == 5);
        ng_close(g);
        remove("analytics.ng");
        assert(ng_create(&g, "dag.ng") == NG_OK);
        assert(ng_symbol(g, "R", &rel) == NG_OK);
        assert(ng_node_create(g, 0, 0, &a) == NG_OK);
        assert(ng_node_create(g, 0, 0, &b) == NG_OK);
        assert(ng_node_create(g, 0, 0, &c) == NG_OK);
        assert(ng_node_create(g, 0, 0, &d) == NG_OK);
        assert(ng_relationship_create(g, a, rel, b, &rid) == NG_OK);
        assert(ng_relationship_create(g, a, rel, c, &rid) == NG_OK);
        assert(ng_relationship_create(g, b, rel, d, &rid) == NG_OK);
        assert(ng_relationship_create(g, c, rel, d, &rid) == NG_OK);
        assert(ng_topological_sort(g, rel, order, 8, &count) == NG_OK && count == 4);
        assert(order[0] == a && order[3] == d);
        ng_close(g);
        remove("dag.ng");
    }
    write_import_files();
    {
        size_t fail;
        uint64_t mask = 0;
        int saw_failure = 0, saw_success = 0;
        for (fail = 0; fail < 10000; fail++) {
            ng_import_diagnostic d;
            size_t accepted = 999;
            ng_status st;
            ng_node_id old;
            uint64_t attempt_mask;
            fixture(&g, &old);
            d.line = d.column = 999;
            d.status = NG_INVALID_ARGUMENT;
            ng_test_fail_after(fail);
            st = ng_import_property_graph(g, "nodes.tsv", "rels.tsv", 0, &accepted, &d);
            attempt_mask = ng_test_import_stage_mask();
            mask |= attempt_mask;
            ng_test_fail_reset();
            if (st == NG_OK) {
                assert(accepted == 3);
                assert(ng_test_import_current_stage() == NG_TEST_IMPORT_COMPLETE);
                assert(ng_validate(g) == NG_OK);
                saw_success = 1;
                ng_close(g);
                break;
            }
            assert(st == NG_OOM && accepted == 0 && d.status == NG_OOM && d.line == 0 &&
                   d.column == 0);
            saw_failure = 1;
            assert(ng_node_count(g) == 1 && ng_relationship_count(g) == 0 &&
                   ng_validate(g) == NG_OK);
            assert(ng_node_get(g, old, &(ng_node){0}) == NG_OK);
            accepted = 999;
            memset(&d, 0, sizeof d);
            assert(ng_import_property_graph(g, "nodes.tsv", "rels.tsv", 0, &accepted, &d) == NG_OK);
            assert(ng_validate(g) == NG_OK);
            ng_close(g);
        }
        assert(saw_failure && saw_success);
        assert(mask & (1u << NG_TEST_IMPORT_SNAPSHOT));
        assert(mask & (1u << NG_TEST_IMPORT_PARSE_NODES));
        assert(mask & (1u << NG_TEST_IMPORT_SYMBOL));
        assert(mask & (1u << NG_TEST_IMPORT_NODE));
        assert(mask & (1u << NG_TEST_IMPORT_LABEL));
        assert(mask & (1u << NG_TEST_IMPORT_NODE_PROPERTY));
        assert(mask & (1u << NG_TEST_IMPORT_PARSE_RELATIONSHIPS));
        assert(mask & (1u << NG_TEST_IMPORT_RELATIONSHIP));
        assert(mask & (1u << NG_TEST_IMPORT_RELATIONSHIP_PROPERTY));
        assert(mask & (1u << NG_TEST_IMPORT_COMPLETE));
    }

    {
        ng_graph *o, *q;
        ng_symbol_id la, lb, lc, k1, k2, k3, k4, rt;
        ng_node_id x, y;
        ng_relationship_id re;
        ng_value pv;
        size_t before;
        assert(ng_create(&o, "order1.ng") == NG_OK);
        assert(ng_symbol(o, "A", &la) == NG_OK);
        assert(ng_symbol(o, "B", &lb) == NG_OK);
        assert(ng_symbol(o, "C", &lc) == NG_OK);
        assert(ng_symbol(o, "k1", &k1) == NG_OK);
        assert(ng_symbol(o, "k2", &k2) == NG_OK);
        assert(ng_symbol(o, "k3", &k3) == NG_OK);
        assert(ng_symbol(o, "k4", &k4) == NG_OK);
        assert(ng_symbol(o, "R", &rt) == NG_OK);
        assert(ng_node_create(o, (ng_symbol_id[]){lc, la, lb}, 3, &x) == NG_OK);
        assert(ng_node_create(o, 0, 0, &y) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "x";
        assert(ng_node_set(o, x, k4, &pv) == NG_OK);
        pv.as.string = "y";
        assert(ng_node_set(o, x, k2, &pv) == NG_OK);
        pv.as.string = "z";
        assert(ng_node_set(o, x, k1, &pv) == NG_OK);
        pv.as.string = "w";
        assert(ng_node_set(o, x, k3, &pv) == NG_OK);
        assert(ng_relationship_create(o, x, rt, y, &re) == NG_OK);
        assert(ng_export_property_graph(o, "order-n1.tsv", "order-r1.tsv") == NG_OK);
        before = ng_node_count(o);
        assert(ng_export_property_graph(o, "order-n2.tsv", "order-r2.tsv") == NG_OK);
        assert(same_file("order-n1.tsv", "order-n2.tsv") &&
               same_file("order-r1.tsv", "order-r2.tsv"));
        assert(ng_node_count(o) == before && ng_validate(o) == NG_OK);
        ng_test_fail_after(0);
        assert(ng_export_property_graph(o, "order-n3.tsv", "order-r3.tsv") == NG_OOM);
        ng_test_fail_reset();
        assert(ng_export_property_graph(o, "order-n3.tsv", "order-r3.tsv") == NG_OK);
        ng_close(o);
        remove("order1.ng");
        remove("order-n1.tsv");
        remove("order-r1.tsv");
        remove("order-n2.tsv");
        remove("order-r2.tsv");
        remove("order-n3.tsv");
        remove("order-r3.tsv");
        (void)q;
    }
    {
        FILE* f = fopen("bad.csv", "wb");
        assert(f);
        fputs("a,R,b\nbad,row\n", f);
        assert(fclose(f) == 0);
        assert(ng_create(&g, "badcsv.ng") == NG_OK);
        assert(ng_import_triples_csv(g, "bad.csv", 0, &n) == NG_PARSE_ERROR);
        assert(n == 0 && ng_node_count(g) == 0 && ng_relationship_count(g) == 0);
        ng_close(g);
        remove("bad.csv");
        remove("badcsv.ng");
    }
    {
        FILE* f;
        assert(ng_create(&g, "tail.ng") == NG_OK);
        assert(ng_save(g) == NG_OK);
        ng_close(g);
        f = fopen("tail.ng", "ab");
        assert(f);
        assert(fputc('x', f) != EOF);
        assert(fclose(f) == 0);
        r = 0;
        assert(ng_open(&r, "tail.ng") == NG_CORRUPT);
        remove("tail.ng");
    }
    {
        ng_graph *o, *h;
        ng_symbol_id person, bin, flag, score, ratio, reltype, relkey, hbin, hscore;
        ng_node_id x, y;
        ng_relationship_id re;
        ng_value pv, got;
        unsigned char bytes[] = {0, 1, 255};
        double dv = 2.5;
        size_t matches = 0;
        assert(ng_create(&o, "typed.ng") == NG_OK);
        assert(ng_symbol(o, "Person", &person) == NG_OK);
        assert(ng_symbol(o, "bin", &bin) == NG_OK);
        assert(ng_symbol(o, "flag", &flag) == NG_OK);
        assert(ng_symbol(o, "score", &score) == NG_OK);
        assert(ng_symbol(o, "ratio", &ratio) == NG_OK);
        assert(ng_symbol(o, "KNOWS", &reltype) == NG_OK);
        assert(ng_symbol(o, "since", &relkey) == NG_OK);
        assert(ng_node_create(o, &person, 1, &x) == NG_OK);
        assert(ng_node_create(o, 0, 0, &y) == NG_OK);
        pv.type = NG_VALUE_BYTES;
        pv.length = sizeof(bytes);
        pv.as.bytes = bytes;
        assert(ng_node_set(o, x, bin, &pv) == NG_OK);
        pv.type = NG_VALUE_BOOL;
        pv.length = 0;
        pv.as.boolean = 1;
        assert(ng_node_set(o, x, flag, &pv) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.as.integer = 42;
        assert(ng_node_set(o, x, score, &pv) == NG_OK);
        pv.type = NG_VALUE_DOUBLE;
        memcpy(&pv.as.real, &dv, 8);
        assert(ng_node_set(o, x, ratio, &pv) == NG_OK);
        assert(ng_relationship_create(o, x, reltype, y, &re) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.as.integer = 2026;
        assert(ng_relationship_set(o, re, relkey, &pv) == NG_OK);
        assert(ng_export_property_graph(o, "typed-n.tsv", "typed-r.tsv") == NG_OK);
        assert(ng_create(&h, "typed2.ng") == NG_OK);
        assert(ng_import_property_graph(h, "typed-n.tsv", "typed-r.tsv", 0, &n, 0) == NG_OK);
        assert(n == 3 && ng_validate(h) == NG_OK);
        assert(ng_symbol(h, "bin", &hbin) == NG_OK);
        assert(ng_symbol(h, "score", &hscore) == NG_OK);
        pv.type = NG_VALUE_BYTES;
        pv.length = sizeof(bytes);
        pv.as.bytes = bytes;
        assert(ng_find_nodes(h, 0, hbin, &pv, match_count_cb, &matches) == NG_OK && matches == 1);
        assert(ng_node_property(h, x, hscore, &got) == NG_OK && got.type == NG_VALUE_INT64 &&
               got.as.integer == 42);
        ng_close(h);
        ng_close(o);
        remove("typed.ng");
        remove("typed2.ng");
        remove("typed-n.tsv");
        remove("typed-r.tsv");
    }
    {
        ng_transaction* tx;
        ng_graph* tg;
        ng_node_index* ix;
        ng_symbol_id label, key;
        ng_node_id x;
        ng_value pv;
        size_t matches = 0;
        assert(ng_create(&g, "tx.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &label) == NG_OK);
        assert(ng_symbol(g, "name", &key) == NG_OK);
        assert(ng_node_create(g, &label, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, x, key, &pv) == NG_OK);
        assert(ng_node_index_build(g, label, key, &ix) == NG_OK);
        assert(ng_node_index_find(ix, &pv, match_count_cb, &matches) == NG_OK && matches == 1);
        ng_node_index_free(ix);
        assert(ng_node_unset(g, x, key) == NG_OK);
        assert(ng_node_property(g, x, key, &pv) == NG_NOT_FOUND);
        assert(ng_transaction_begin(g, &tx) == NG_OK);
        tg = ng_transaction_graph(tx);
        assert(tg);
        assert(ng_node_create(tg, &label, 1, &a) == NG_OK);
        ng_transaction_rollback(tx);
        assert(ng_node_count(g) == 1);
        assert(ng_transaction_begin(g, &tx) == NG_OK);
        tg = ng_transaction_graph(tx);
        assert(ng_node_create(tg, &label, 1, &a) == NG_OK);
        assert(ng_transaction_commit(tx) == NG_OK);
        assert(ng_node_count(g) == 2 && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("tx.ng");
    }
    {
        ng_symbol_id person, name;
        int mutated = 99;
        assert(ng_create(&g, "querytx-required.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, person, name) ==
               NG_OK);
        assert(query_tmp(g,
                         "CREATE (a:Person {name: \"A\"})-[:KNOWS]->(b:Person) RETURN a",
                         &mutated) == NG_NOT_FOUND);
        assert(!mutated && ng_node_count(g) == 0 && ng_relationship_count(g) == 0 &&
               ng_validate(g) == NG_OK);
        assert(query_tmp(g,
                         "CREATE (a:Person {name: \"A\"})-[:KNOWS]->(b:Person {name: \"B\"}) "
                         "RETURN a.name, b.name",
                         &mutated) == NG_OK);
        assert(mutated && ng_node_count(g) == 2 && ng_relationship_count(g) == 1 &&
               ng_validate(g) == NG_OK);
        ng_close(g);
        remove("querytx-required.ng");
    }
    {
        ng_symbol_id person, name;
        int mutated = 99;
        assert(ng_create(&g, "querytx-comma-required.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, person, name) ==
               NG_OK);
        assert(query_tmp(g, "CREATE (a:Person {name: \"A\"}), (b:Person) RETURN a, b", &mutated) ==
               NG_NOT_FOUND);
        assert(!mutated && ng_node_count(g) == 0 && ng_relationship_count(g) == 0 &&
               ng_validate(g) == NG_OK);
        ng_close(g);
        remove("querytx-comma-required.ng");
    }
    {
        ng_symbol_id person, required, name;
        ng_node_id x;
        ng_value pv;
        int mutated = 99;
        assert(ng_create(&g, "querytx-with-required.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "Required", &required) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, required, name) ==
               NG_OK);
        assert(query_tmp(g,
                         "MATCH (a:Person) WITH a CREATE (a)-[:KNOWS]->(b:Person {name: \"B\"}), "
                         "(bad:Required) RETURN a, b",
                         &mutated) == NG_NOT_FOUND);
        assert(!mutated && ng_node_count(g) == 1 && ng_relationship_count(g) == 0 &&
               ng_validate(g) == NG_OK);
        ng_close(g);
        remove("querytx-with-required.ng");
    }
    {
        size_t fail;
        int saw_failure = 0, saw_success = 0, mutated = 0;
        assert(ng_create(&g, "querytx-create-oom.ng") == NG_OK);
        for (fail = 0; fail < 10000; fail++) {
            ng_test_fail_after(fail);
            mutated = 99;
            ng_status st = query_tmp(
                g,
                "CREATE (a:Person {name: \"A\"})-[:KNOWS]->(b:Person {name: \"B\"})-[:LIKES "
                "{since: 2026}]->(c:Person {name: \"C\"}) RETURN a.name, b.name, c.name",
                &mutated);
            ng_test_fail_reset();
            if (st == NG_OK) {
                assert(mutated && ng_node_count(g) == 3 && ng_relationship_count(g) == 2 &&
                       ng_validate(g) == NG_OK);
                saw_success = 1;
                break;
            }
            assert(st == NG_OOM && !mutated && ng_node_count(g) == 0 &&
                   ng_relationship_count(g) == 0 && ng_validate(g) == NG_OK);
            saw_failure = 1;
        }
        assert(saw_failure && saw_success);
        ng_close(g);
        remove("querytx-create-oom.ng");
    }
    {
        ng_symbol_id person, thing, name;
        ng_node_id id;
        ng_value pv;
        size_t i, fail;
        int saw_failure = 0, saw_success = 0, mutated = 0;
        assert(ng_create(&g, "querytx-rel-oom.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "Thing", &thing) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        for (i = 0; i < 3; i++) {
            assert(ng_node_create(g, &person, 1, &id) == NG_OK);
            pv.type = NG_VALUE_INT64;
            pv.as.integer = (int64_t)i;
            assert(ng_node_set(g, id, name, &pv) == NG_OK);
            assert(ng_node_create(g, &thing, 1, &id) == NG_OK);
            pv.as.integer = (int64_t)i;
            assert(ng_node_set(g, id, name, &pv) == NG_OK);
        }
        for (fail = 0; fail < 10000; fail++) {
            ng_test_fail_after(fail);
            mutated = 99;
            ng_status st =
                query_tmp(g,
                          "MATCH (a:Person) MATCH (b:Thing) CREATE (a)-[:KNOWS]->(b) RETURN a, b",
                          &mutated);
            ng_test_fail_reset();
            if (st == NG_OK) {
                assert(mutated && ng_relationship_count(g) == 9 && ng_validate(g) == NG_OK);
                saw_success = 1;
                break;
            }
            assert(st == NG_OOM && !mutated && ng_relationship_count(g) == 0 &&
                   ng_node_count(g) == 6 && ng_validate(g) == NG_OK);
            saw_failure = 1;
        }
        assert(saw_failure && saw_success);
        ng_close(g);
        remove("querytx-rel-oom.ng");
    }
    {
        ng_symbol_id person, name, flag;
        ng_node_id x, y;
        ng_value pv, got;
        int mutated = 99;
        assert(ng_create(&g, "querytx-set.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_symbol(g, "flag", &flag) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.as.string = "B";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_OK);
        assert(query_tmp(g, "MATCH (n:Person) SET n.name = \"Same\" RETURN n.name", &mutated) ==
               NG_EXISTS);
        assert(!mutated && ng_node_count(g) == 2 && ng_validate(g) == NG_OK);
        assert(ng_node_property(g, x, name, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 1 && !memcmp(got.as.string, "A", 1));
        assert(ng_node_property(g, y, name, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 1 && !memcmp(got.as.string, "B", 1));
        assert(ng_node_property(g, x, flag, &got) == NG_NOT_FOUND &&
               ng_node_property(g, y, flag, &got) == NG_NOT_FOUND);
        assert(query_tmp(g, "MATCH (n:Person) SET n.flag = \"ok\" RETURN n.flag", &mutated) ==
               NG_OK);
        assert(mutated && ng_node_property(g, x, flag, &got) == NG_OK &&
               ng_node_property(g, y, flag, &got) == NG_OK);
        ng_close(g);
        remove("querytx-set.ng");
    }
    {
        ng_symbol_id person, name, city, flag, knows, weight;
        ng_node_id x, y, z;
        ng_relationship_id rel;
        ng_value pv, got;
        FILE* f;
        int mutated = 99;
        assert(ng_create(&g, "write-broad.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_symbol(g, "city", &city) == NG_OK);
        assert(ng_symbol(g, "flag", &flag) == NG_OK);
        assert(ng_symbol(g, "KNOWS", &knows) == NG_OK);
        assert(ng_symbol(g, "weight", &weight) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.as.string = "B";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_relationship_create(g, x, knows, y, &rel) == NG_OK);
        assert(query_params_file(
                   g,
                   "MATCH (a:Person)-[r:KNOWS]->(b:Person) SET a.city = \"Berlin\", b.city = "
                   "\"Paris\", r.weight = 2 + 3 RETURN a.city, b.city, r.weight",
                   NULL,
                   0,
                   "write-broad.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("write-broad.expected", "wb");
        assert(f);
        fputs("Berlin\tParis\t5\n", f);
        assert(fclose(f) == 0);
        assert(same_file("write-broad.out", "write-broad.expected"));
        assert(ng_node_property(g, x, city, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 6 && !memcmp(got.as.string, "Berlin", 6));
        assert(ng_node_property(g, y, city, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 5 && !memcmp(got.as.string, "Paris", 5));
        assert(ng_relationship_property(g, rel, weight, &got) == NG_OK &&
               got.type == NG_VALUE_INT64 && got.as.integer == 5);
        mutated = 99;
        assert(query_tmp(g,
                         "MATCH (a:Person) WHERE a.name = \"A\" SET a.flag = \"ok\", RETURN a.flag",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_property(g, x, flag, &got) == NG_NOT_FOUND &&
               ng_validate(g) == NG_OK);
        assert(ng_node_create(g, &person, 1, &z) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "C";
        assert(ng_node_set(g, z, name, &pv) == NG_OK);
        assert(ng_relationship_create(g, x, knows, z, &rel) == NG_OK);
        mutated = 99;
        assert(query_tmp(g,
                         "MATCH (a:Person)-[r:KNOWS]->(b:Person) WHERE b.name = \"C\" DELETE r, b",
                         &mutated) == NG_OK &&
               mutated);
        assert(ng_node_get(g, z, &(ng_node){0}) == NG_NOT_FOUND &&
               ng_relationship_get(g, rel, &(ng_relationship){0}) == NG_NOT_FOUND &&
               ng_node_get(g, x, &(ng_node){0}) == NG_OK);
        ng_close(g);
        remove("write-broad.ng");
        remove("write-broad.out");
        remove("write-broad.expected");
    }
    {
        ng_symbol_id person, name, flag;
        ng_node_id x, y;
        ng_value pv, got;
        int mutated = 99;
        assert(ng_create(&g, "write-rollback.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_symbol(g, "flag", &flag) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.as.string = "B";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_OK);
        assert(
            query_tmp(g,
                      "MATCH (a:Person) SET a.flag = \"changed\", a.name = \"Same\" RETURN a.name",
                      &mutated) == NG_EXISTS);
        assert(!mutated && ng_node_property(g, x, flag, &got) == NG_NOT_FOUND &&
               ng_node_property(g, y, flag, &got) == NG_NOT_FOUND);
        assert(ng_node_property(g, x, name, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 1 && !memcmp(got.as.string, "A", 1));
        assert(ng_node_property(g, y, name, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 1 && !memcmp(got.as.string, "B", 1));
        ng_close(g);
        remove("write-rollback.ng");
    }
    {
        ng_symbol_id person, name;
        ng_node_id x, y;
        ng_value pv;
        int mutated = 0;
        assert(ng_create(&g, "querytx-merge.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.as.string = "B";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (a:Person) MATCH (b:Person) WHERE a.name = \"A\" AND b.name = "
                         "\"B\" CREATE (a)-[:KNOWS]->(b) RETURN a.name, b.name",
                         &mutated) == NG_OK);
        assert(mutated && ng_relationship_count(g) == 1);
        mutated = 99;
        assert(query_tmp(g,
                         "MATCH (a:Person) MATCH (b:Person) WHERE a.name = \"A\" AND b.name = "
                         "\"B\" MERGE (a)-[:KNOWS]->(b) RETURN a.name, b.name",
                         &mutated) == NG_OK);
        assert(!mutated && ng_relationship_count(g) == 1 && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("querytx-merge.ng");
    }
    {
        ng_symbol_id person, required, name;
        FILE* f;
        int mutated = 99;
        size_t before;
        assert(ng_create(&g, "merge-list.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "Required", &required) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(query_params_file(
                   g,
                   "MERGE (a:Person {name: \"A\"}), (b:Person {name: \"B\"}) RETURN a.name, b.name",
                   NULL,
                   0,
                   "merge-list.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("merge-list.expected", "wb");
        assert(f);
        fputs("A\tB\n", f);
        assert(fclose(f) == 0);
        assert(same_file("merge-list.out", "merge-list.expected"));
        assert(ng_node_count(g) == 2);
        mutated = 99;
        assert(query_params_file(
                   g,
                   "MERGE (a:Person {name: \"A\"}), (b:Person {name: \"B\"}) RETURN a.name, b.name",
                   NULL,
                   0,
                   "merge-list.out",
                   &mutated) == NG_OK &&
               !mutated);
        assert(same_file("merge-list.out", "merge-list.expected") && ng_node_count(g) == 2);
        assert(query_params_file(g,
                                 "MERGE (a:Person {name: \"A\"}), (b:Person {name: \"B\"}), "
                                 "(a)-[r:KNOWS {since: 1}]->(b), (b)-[:KNOWS]->(c:Person {name: "
                                 "\"C\"}) RETURN a.name, b.name, c.name, r.since",
                                 NULL,
                                 0,
                                 "merge-list.out",
                                 &mutated) == NG_OK &&
               mutated);
        f = fopen("merge-list.expected", "wb");
        assert(f);
        fputs("A\tB\tC\t1\n", f);
        assert(fclose(f) == 0);
        assert(same_file("merge-list.out", "merge-list.expected"));
        assert(ng_node_count(g) == 3 && ng_relationship_count(g) == 2);
        mutated = 99;
        assert(query_params_file(g,
                                 "MERGE (a:Person {name: \"A\"}), (b:Person {name: \"B\"}), "
                                 "(a)-[r:KNOWS {since: 1}]->(b), (b)-[:KNOWS]->(c:Person {name: "
                                 "\"C\"}) RETURN a.name, b.name, c.name, r.since",
                                 NULL,
                                 0,
                                 "merge-list.out",
                                 &mutated) == NG_OK &&
               !mutated);
        assert(ng_node_count(g) == 3 && ng_relationship_count(g) == 2);
        assert(query_params_file(
                   g,
                   "MERGE (ra:Person {name: \"RA\"}), (rb:Person {name: \"RB\"}), (ra)<-[rel:LIKES "
                   "{since: 2}]-(rb) RETURN ra.name, rel.since, rb.name",
                   NULL,
                   0,
                   "merge-list.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("merge-list.expected", "wb");
        assert(f);
        fputs("RA\t2\tRB\n", f);
        assert(fclose(f) == 0);
        assert(same_file("merge-list.out", "merge-list.expected"));
        assert(
            query_params_file(g,
                              "MATCH (a:Person) WHERE a.name = \"A\" MERGE (a)-[:KNOWS]->(d:Person "
                              "{name: \"D\"}), (d)-[:KNOWS]->(a) RETURN d.name",
                              NULL,
                              0,
                              "merge-list.out",
                              &mutated) == NG_OK &&
            mutated);
        f = fopen("merge-list.expected", "wb");
        assert(f);
        fputs("D\n", f);
        assert(fclose(f) == 0);
        assert(same_file("merge-list.out", "merge-list.expected"));
        assert(query_tmp(g, "MERGE (bad:Person {name: \"Bad\"}), RETURN bad", &mutated) ==
               NG_PARSE_ERROR);
        assert(query_tmp(g, "MERGE (bad:Person),, (alsoBad:Person)", &mutated) == NG_PARSE_ERROR);
        before = ng_node_count(g);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, required, name) ==
               NG_OK);
        mutated = 99;
        assert(query_tmp(g,
                         "MERGE (ok:Person {name: \"WillRollback\"}), (bad:Required) RETURN ok",
                         &mutated) == NG_NOT_FOUND);
        assert(!mutated && ng_node_count(g) == before && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("merge-list.ng");
        remove("merge-list.out");
        remove("merge-list.expected");
    }
    {
        FILE* f;
        ng_symbol_id person, name, age, nick;
        ng_node_id x, y;
        ng_value pv;
        ng_parameter ps[4];
        int mutated = 99;
        size_t before;
        assert(ng_create(&g, "params.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_symbol(g, "age", &age) == NG_OK);
        assert(ng_symbol(g, "nickname", &nick) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.length = 0;
        pv.as.integer = 30;
        assert(ng_node_set(g, x, age, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 3;
        pv.as.string = "Bob";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.as.integer = 17;
        assert(ng_node_set(g, y, age, &pv) == NG_OK);
        ps[0].name = "name";
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 5;
        ps[0].value.as.string = "Alice";
        ps[1].name = "unused";
        ps[1].value.type = NG_VALUE_INT64;
        ps[1].value.as.integer = 99;
        mutated = 99;
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = $name RETURN a.name",
                                 ps,
                                 2,
                                 "param.out",
                                 &mutated) == NG_OK &&
               !mutated);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "age";
        ps[0].value.type = NG_VALUE_INT64;
        ps[0].value.as.integer = 18;
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.age >= $age RETURN a.name",
                                 ps,
                                 1,
                                 "param.out",
                                 &mutated) == NG_OK);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "maybe";
        ps[0].value.type = NG_VALUE_NULL;
        ps[0].value.length = 0;
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WITH $maybe AS v, a WHERE v IS NULL RETURN a.name LIMIT 1",
                   ps,
                   1,
                   "param.out",
                   &mutated) == NG_OK);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 1;
        ps[0].value.as.string = "x";
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WITH $maybe AS v, a WHERE v IS NOT NULL RETURN a.name LIMIT 1",
                   ps,
                   1,
                   "param.out",
                   &mutated) == NG_OK);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "name";
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 4;
        ps[0].value.as.string = "Cara";
        ps[1].name = "age";
        ps[1].value.type = NG_VALUE_INT64;
        ps[1].value.as.integer = 22;
        mutated = 99;
        assert(query_params_file(g,
                                 "CREATE (c:Person {name: $name, age: $age}) RETURN c.name, c.age",
                                 ps,
                                 2,
                                 "param.out",
                                 &mutated) == NG_OK &&
               mutated);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("Cara\t22\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "left";
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 5;
        ps[0].value.as.string = "Alice";
        ps[1].name = "right";
        ps[1].value.type = NG_VALUE_STRING;
        ps[1].value.length = 3;
        ps[1].value.as.string = "Bob";
        ps[2].name = "since";
        ps[2].value.type = NG_VALUE_INT64;
        ps[2].value.as.integer = 2026;
        assert(
            query_params_file(g,
                              "MATCH (a:Person) MATCH (b:Person) WHERE a.name = $left AND b.name = "
                              "$right CREATE (a)-[r:KNOWS {since: $since}]->(b) RETURN r.since",
                              ps,
                              3,
                              "param.out",
                              &mutated) == NG_OK);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("2026\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "name";
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 5;
        ps[0].value.as.string = "Alice";
        ps[1].name = "age";
        ps[1].value.type = NG_VALUE_INT64;
        ps[1].value.as.integer = 31;
        assert(
            query_params_file(g,
                              "MATCH (a:Person) WHERE a.name = $name SET a.age = $age RETURN a.age",
                              ps,
                              2,
                              "param.out",
                              &mutated) == NG_OK);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("31\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        ps[0].name = "name";
        ps[0].value.type = NG_VALUE_STRING;
        ps[0].value.length = 5;
        ps[0].value.as.string = "Alice";
        assert(query_params_file(g,
                                 "MATCH (a:Person) WITH $name AS wanted, a RETURN wanted LIMIT 1",
                                 ps,
                                 1,
                                 "param.out",
                                 &mutated) == NG_OK);
        f = fopen("param.expected", "wb");
        assert(f);
        fputs("Alice\n", f);
        assert(fclose(f) == 0);
        assert(same_file("param.out", "param.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WHERE a.name = $name OR a.nickname = $name RETURN a.name",
                   ps,
                   1,
                   "param.out",
                   &mutated) == NG_OK);
        assert(same_file("param.out", "param.expected"));
        assert(query_tmp_params(
                   g, "MATCH (a:Person) WHERE a.name = $missing RETURN a", ps, 1, &mutated) ==
               NG_NOT_FOUND);
        assert(query_tmp_params(
                   g, "MATCH (a:Person) WHERE a.name = $1bad RETURN a", ps, 1, &mutated) ==
               NG_PARSE_ERROR);
        before = ng_node_count(g);
        assert(query_tmp_params(
                   g,
                   "CREATE (a:Person {name: $name}), (b:Person {name: $missing}) RETURN a, b",
                   ps,
                   1,
                   &mutated) == NG_NOT_FOUND);
        assert(ng_node_count(g) == before && ng_validate(g) == NG_OK);
        assert(query_tmp(g, "CREATE (o:Person {name: \"Old\"}) RETURN o.name", &mutated) == NG_OK);
        remove("param.out");
        remove("param.expected");
        ng_close(g);
        remove("params.ng");
    }
    {
        FILE* f;
        ng_symbol_id person, name, age, city, score, summary;
        ng_node_id ids[4];
        ng_value pv;
        double d;
        ng_parameter ps[1];
        int mutated = 99;
        size_t before;
        assert(ng_create(&g, "aggregates.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "Summary", &summary) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_symbol(g, "age", &age) == NG_OK);
        assert(ng_symbol(g, "city", &city) == NG_OK);
        assert(ng_symbol(g, "score", &score) == NG_OK);
        assert(ng_node_create(g, &person, 1, &ids[0]) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "A";
        assert(ng_node_set(g, ids[0], name, &pv) == NG_OK);
        pv.as.string = "X";
        assert(ng_node_set(g, ids[0], city, &pv) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.as.integer = 10;
        assert(ng_node_set(g, ids[0], age, &pv) == NG_OK);
        pv.type = NG_VALUE_DOUBLE;
        d = 1.5;
        memcpy(&pv.as.real, &d, 8);
        assert(ng_node_set(g, ids[0], score, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &ids[1]) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "B";
        assert(ng_node_set(g, ids[1], name, &pv) == NG_OK);
        pv.as.string = "X";
        assert(ng_node_set(g, ids[1], city, &pv) == NG_OK);
        pv.type = NG_VALUE_INT64;
        pv.as.integer = 20;
        assert(ng_node_set(g, ids[1], age, &pv) == NG_OK);
        pv.type = NG_VALUE_DOUBLE;
        d = 2.25;
        memcpy(&pv.as.real, &d, 8);
        assert(ng_node_set(g, ids[1], score, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &ids[2]) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "C";
        assert(ng_node_set(g, ids[2], name, &pv) == NG_OK);
        pv.as.string = "Y";
        assert(ng_node_set(g, ids[2], city, &pv) == NG_OK);
        assert(ng_node_create(g, &person, 1, &ids[3]) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 1;
        pv.as.string = "D";
        assert(ng_node_set(g, ids[3], name, &pv) == NG_OK);
        pv.as.string = "Y";
        assert(ng_node_set(g, ids[3], city, &pv) == NG_OK);
        pv.type = NG_VALUE_NULL;
        pv.length = 0;
        assert(ng_node_set(g, ids[3], age, &pv) == NG_OK);
        assert(query_params_file(
                   g, "MATCH (a:Person) RETURN count(*)", NULL, 0, "agg.out", &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("4\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN count(a.age), sum(a.age)",
                                 NULL,
                                 0,
                                 "agg.out",
                                 &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("2\t30\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Missing) RETURN count(*), count(a.age), sum(a.age), collect(a.name)",
                   NULL,
                   0,
                   "agg.out",
                   &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("0\t0\tnull\t[]\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(
                   g, "MATCH (a:Person) RETURN sum(a.score)", NULL, 0, "agg.out", &mutated) ==
               NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("3.75\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(
                   g, "MATCH (a:Person) RETURN collect(a.name)", NULL, 0, "agg.out", &mutated) ==
               NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("[A, B, C, D]\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) RETURN count(DISTINCT a.city), collect(DISTINCT a.city)",
                   NULL,
                   0,
                   "agg.out",
                   &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("2\t[X, Y]\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(
                   g, "MATCH (a:Person) RETURN a.city, count(a)", NULL, 0, "agg.out", &mutated) ==
               NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("X\t2\nY\t2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.city, a.age, count(a)",
                                 NULL,
                                 0,
                                 "agg.out",
                                 &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("X\t10\t1\nX\t20\t1\nY\tnull\t2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WITH a.city AS city, count(a) AS people WHERE "
                                 "people > 1 RETURN city, people",
                                 NULL,
                                 0,
                                 "agg.out",
                                 &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("X\t2\nY\t2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN count(a), sum(a.age), collect(a.name)",
                                 NULL,
                                 0,
                                 "agg.out",
                                 &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("4\t30\t[A, B, C, D]\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        ps[0].name = "n";
        ps[0].value.type = NG_VALUE_INT64;
        ps[0].value.length = 0;
        ps[0].value.as.integer = 5;
        assert(query_params_file(
                   g, "MATCH (a:Person) RETURN sum($n)", ps, 1, "agg.out", &mutated) == NG_OK);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("20\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        before = ng_node_count(g);
        assert(query_params_file(g,
                                 "MATCH (a:Person) WITH count(a) AS people WHERE people > 3 CREATE "
                                 "(s:Summary {name: \"ok\"}) RETURN people",
                                 NULL,
                                 0,
                                 "agg.out",
                                 &mutated) == NG_OK &&
               mutated);
        f = fopen("agg.expected", "wb");
        assert(f);
        fputs("4\n", f);
        assert(fclose(f) == 0);
        assert(same_file("agg.out", "agg.expected"));
        assert(ng_node_count(g) == before + 1 && ng_validate(g) == NG_OK);
        assert(query_tmp_params(g, "MATCH (a:Person) RETURN sum(a.name)", NULL, 0, &mutated) ==
               NG_PARSE_ERROR);
        assert(query_tmp_params(g, "MATCH (a:Person) RETURN count()", NULL, 0, &mutated) ==
               NG_PARSE_ERROR);
        assert(query_tmp_params(g, "MATCH (a:Person) RETURN a.age + count(a)", NULL, 0, &mutated) ==
               NG_PARSE_ERROR);
        remove("agg.out");
        remove("agg.expected");
        ng_close(g);
        remove("aggregates.ng");
    }
    {
        FILE* f;
        int mutated = 99;
        assert(ng_create(&g, "optional.ng") == NG_OK);
        assert(query_tmp(g,
                         "CREATE (a:Person {name: \"A\", active: true})-[:KNOWS {since: "
                         "2020}]->(b:Person {name: \"B\", age: 20, active: true})",
                         &mutated) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (a:Person) WHERE a.name = \"A\" WITH a CREATE (a)-[:KNOWS {since: "
                         "2021}]->(d:Person {name: \"D\", age: 15, active: false}) RETURN a",
                         &mutated) == NG_OK);
        assert(query_tmp(g, "CREATE (c:Person {name: \"C\", active: true})", &mutated) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (d:Person) WHERE d.name = \"D\" WITH d MATCH (a:Person) WHERE "
                         "a.name = \"A\" CREATE (d)-[:KNOWS {since: 2022}]->(a) RETURN d",
                         &mutated) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (b:Person) WHERE b.name = \"B\" WITH b CREATE "
                         "(b)-[:WORKS_AT]->(co:Company {name: \"Acme\"}) RETURN b",
                         &mutated) == NG_OK);
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b:Person) RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tB\nA\tD\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b:Person) RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("C\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                                 "(a)-[r:LIKES]->(b) RETURN a.name, r, b",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("C\tnull\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)<-[:KNOWS]-(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("D\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"B\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]-(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b:Person {active: true}) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("B\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH (a)-[:KNOWS "
                                 "{since: 2021}]->(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("D\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b:Person) WHERE b.age >= 18 RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tB\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b:Person) WHERE b.age >= 30 RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH (a)-[:KNOWS]->(b:Person) "
                   "OPTIONAL MATCH (b)-[:WORKS_AT]->(c) RETURN a.name, b.name, c.name",
                   NULL,
                   0,
                   "opt.out",
                   &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tB\tAcme\nA\tD\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" WITH a OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(b) RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tB\nA\tD\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) RETURN "
                                 "count(*), count(b), collect(b.name)",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("5\t3\t[B, D, A]\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) WITH a, b "
                                 "WHERE b IS NULL RETURN a.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("B\nC\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) WITH a, b "
                                 "WHERE b IS NOT NULL RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tB\nA\tD\nD\tA\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) MATCH "
                                 "(b)-[:WORKS_AT]->(c) RETURN a.name, c.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\tAcme\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" WITH a MATCH "
                                 "(a)-[:KNOWS*2]->(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("A\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS*1..2]->(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("B\nD\nA\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS*1..2]->(b:Person) RETURN a.name, b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("C\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"A\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS*1..2 {since: 2021}]->(b:Person) RETURN b.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("D\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_tmp(g,
                         "MATCH (a:Person) WITH a MATCH (a)-[r:KNOWS*1..2]->(b) RETURN r",
                         &mutated) == NG_PARSE_ERROR);
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WHERE a.name = \"A\" WITH a SET a.tag = \"hot\" RETURN a.tag",
                   NULL,
                   0,
                   "opt.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("hot\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        {
            size_t before_nodes = ng_node_count(g), before_rels = ng_relationship_count(g);
            assert(query_tmp(g,
                             "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                             "(a)-[:KNOWS]->(b) SET b.tag = \"bad\" RETURN a.name",
                             &mutated) == NG_PARSE_ERROR);
            assert(ng_node_count(g) == before_nodes && ng_relationship_count(g) == before_rels &&
                   ng_validate(g) == NG_OK);
        }
        {
            size_t before_nodes = ng_node_count(g), before_rels = ng_relationship_count(g);
            assert(query_tmp(g,
                             "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                             "(a)-[:KNOWS]->(b) DELETE b",
                             &mutated) == NG_PARSE_ERROR);
            assert(ng_node_count(g) == before_nodes && ng_relationship_count(g) == before_rels &&
                   ng_validate(g) == NG_OK);
        }
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"C\" WITH a MERGE (a)-[r:KNOWS "
                                 "{since: 2023}]->(e:Person {name: \"E\"}) RETURN e.name, r.since",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK &&
               mutated);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("E\t2023\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) WHERE a.name = \"C\" WITH a MATCH (e:Person) WHERE e.name = "
                   "\"E\" MERGE (a)-[r:KNOWS {since: 2023}]->(e) RETURN e.name, r.since",
                   NULL,
                   0,
                   "opt.out",
                   &mutated) == NG_OK &&
               !mutated);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_tmp(g,
                         "MATCH (a:Person) WHERE a.name = \"C\" WITH a MATCH (e:Person) WHERE "
                         "e.name = \"E\" MATCH (a)-[r:KNOWS]->(e) WITH r DELETE r",
                         &mutated) == NG_OK &&
               mutated);
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.name = \"C\" OPTIONAL MATCH "
                                 "(a)-[:KNOWS]->(e:Person {name: \"E\"}) RETURN e.name",
                                 NULL,
                                 0,
                                 "opt.out",
                                 &mutated) == NG_OK);
        f = fopen("opt.expected", "wb");
        assert(f);
        fputs("null\n", f);
        assert(fclose(f) == 0);
        assert(same_file("opt.out", "opt.expected"));
        assert(query_tmp(g, "MATCH (a:Person) OPTIONAL (a)-[:KNOWS]->(b) RETURN a, b", &mutated) ==
               NG_PARSE_ERROR);
        assert(query_tmp(g, "MATCH (a:Person) WHERE a.name = \"A\" RETURN a.name", &mutated) ==
               NG_OK);
        remove("opt.out");
        remove("opt.expected");
        ng_close(g);
        remove("optional.ng");
    }
    {
        FILE* f;
        double d;
        ng_parameter ps[1];
        ng_symbol_id score;
        ng_value dv;
        int mutated = 99;
        assert(ng_create(&g, "orderby.ng") == NG_OK);
        assert(query_tmp(g,
                         "CREATE (a:Person {name: \"A\", age: 30, city: "
                         "\"X\"})-[:KNOWS]->(b:Person {name: \"B\", age: 20, city: \"X\"})",
                         &mutated) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (a:Person) WHERE a.name = \"A\" WITH a CREATE "
                         "(a)-[:KNOWS]->(c:Person {name: \"C\", city: \"Y\"}) RETURN a",
                         &mutated) == NG_OK);
        assert(query_tmp(g,
                         "MATCH (b:Person) WHERE b.name = \"B\" WITH b MATCH (c:Person) WHERE "
                         "c.name = \"C\" CREATE (b)-[:KNOWS]->(c) RETURN b",
                         &mutated) == NG_OK);
        assert(query_tmp(g, "CREATE (d:Person {name: \"D\", age: 10, city: \"Z\"})", &mutated) ==
               NG_OK);
        assert(ng_symbol(g, "score", &score) == NG_OK);
        dv.type = NG_VALUE_DOUBLE;
        dv.length = 0;
        d = 2.5;
        memcpy(&dv.as.real, &d, 8);
        assert(ng_node_set(g, 1, score, &dv) == NG_OK);
        d = 1.5;
        memcpy(&dv.as.real, &d, 8);
        assert(ng_node_set(g, 2, score, &dv) == NG_OK);
        d = 3.25;
        memcpy(&dv.as.real, &d, 8);
        assert(ng_node_set(g, 3, score, &dv) == NG_OK);
        d = 1.25;
        memcpy(&dv.as.real, &d, 8);
        assert(ng_node_set(g, 4, score, &dv) == NG_OK);
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name ORDER BY a.name ASC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("A\nB\nC\nD\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name, a.age ORDER BY a.age DESC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("C\tnull\nA\t30\nB\t20\nD\t10\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(
                   g,
                   "MATCH (a:Person) RETURN a.city, a.name, a.age ORDER BY a.city ASC, a.age DESC",
                   NULL,
                   0,
                   "order.out",
                   &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("X\tA\t30\nX\tB\t20\nY\tC\tnull\nZ\tD\t10\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(
            query_params_file(
                g,
                "MATCH (a:Person) RETURN a.name AS name, a.age AS age ORDER BY age DESC, name ASC",
                NULL,
                0,
                "order.out",
                &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("C\tnull\nA\t30\nB\t20\nD\t10\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.age IS NOT NULL RETURN a.name, a.age + "
                                 "1 AS next ORDER BY a.age + 1 ASC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("D\t11\nB\t21\nA\t31\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) WITH a.name AS name, a.age AS age ORDER BY age "
                                 "DESC RETURN name, age",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("C\tnull\nA\t30\nB\t20\nD\t10\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) RETURN a.name, "
                                 "b.name ORDER BY a.name ASC, b.name DESC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("A\tC\nA\tB\nB\tC\nC\tnull\nD\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name, a.age ORDER BY a.age ASC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("D\t10\nB\t20\nA\t30\nC\tnull\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.city, count(a) AS people ORDER BY "
                                 "people DESC, a.city ASC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("X\t2\nY\t1\nZ\t1\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(
            query_params_file(g,
                              "MATCH (a:Person) RETURN DISTINCT a.city AS city ORDER BY city DESC",
                              NULL,
                              0,
                              "order.out",
                              &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("Z\nY\nX\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name ORDER BY a.name SKIP 1 LIMIT 2",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("B\nC\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name, a.score ORDER BY a.score ASC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("D\t1.25\nB\t1.5\nA\t2.5\nC\t3.25\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_params_file(g,
                                 "MATCH (a:Person) RETURN a.name ORDER BY a.name DESC",
                                 NULL,
                                 0,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("D\nC\nB\nA\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        ps[0].name = "offset";
        ps[0].value.type = NG_VALUE_INT64;
        ps[0].value.length = 0;
        ps[0].value.as.integer = 5;
        assert(query_params_file(g,
                                 "MATCH (a:Person) WHERE a.age IS NOT NULL RETURN a.name ORDER BY "
                                 "a.age + $offset ASC",
                                 ps,
                                 1,
                                 "order.out",
                                 &mutated) == NG_OK);
        f = fopen("order.expected", "wb");
        assert(f);
        fputs("D\nB\nA\n", f);
        assert(fclose(f) == 0);
        assert(same_file("order.out", "order.expected"));
        assert(query_tmp(g,
                         "MATCH (a:Person) WITH a.name AS name ORDER BY a.age RETURN name",
                         &mutated) == NG_PARSE_ERROR);
        assert(query_tmp(g, "MATCH (a:Person) RETURN a.name ORDER BY a.name ASCENDING", &mutated) ==
               NG_PARSE_ERROR);
        remove("order.out");
        remove("order.expected");
        ng_close(g);
        remove("orderby.ng");
    }
    {
        ng_node_index* ix;
        ng_symbol_id label, key, out_label, out_key;
        ng_node_id x;
        ng_value pv;
        size_t matches = 0;
        assert(ng_create(&g, "indexmeta.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &label) == NG_OK);
        assert(ng_symbol(g, "email", &key) == NG_OK);
        assert(ng_node_create(g, &label, 1, &x) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 13;
        pv.as.string = "a@example.com";
        assert(ng_node_set(g, x, key, &pv) == NG_OK);
        assert(ng_node_index_create(g, label, key) == NG_OK);
        assert(ng_node_index_create(g, label, key) == NG_EXISTS);
        assert(ng_node_index_count(g) == 1);
        assert(ng_save(g) == NG_OK);
        ng_close(g);
        assert(ng_open(&g, "indexmeta.ng") == NG_OK);
        assert(ng_node_index_count(g) == 1);
        assert(ng_node_index_get(g, 0, &out_label, &out_key) == NG_OK && out_label == label &&
               out_key == key);
        assert(ng_node_index_build(g, out_label, out_key, &ix) == NG_OK);
        assert(ng_node_index_find(ix, &pv, match_count_cb, &matches) == NG_OK && matches == 1);
        ng_node_index_free(ix);
        assert(ng_node_index_drop(g, label, key) == NG_OK);
        assert(ng_node_index_count(g) == 0);
        ng_close(g);
        remove("indexmeta.ng");
    }
    {
        ng_symbol_id person, other, name;
        ng_node_id x, y, z, first, second;
        ng_value pv;
        assert(ng_create(&g, "constraints.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "Other", &other) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        assert(ng_node_create(g, &other, 1, &z) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        assert(ng_require_node_property(g, person, name, &first) == NG_NOT_FOUND && first == y);
        assert(ng_unique_node_property(g, person, name, &first, &second) == NG_OK);
        assert(ng_require_node_property(g, other, name, &first) == NG_NOT_FOUND && first == z);
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_unique_node_property(g, person, name, &first, &second) == NG_EXISTS &&
               first == x && second == y);
        pv.length = 3;
        pv.as.string = "Bob";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_require_node_property(g, person, name, &first) == NG_OK && first == 0);
        assert(ng_unique_node_property(g, person, name, &first, &second) == NG_OK && first == 0 &&
               second == 0);
        ng_close(g);
        remove("constraints.ng");
    }
    {
        ng_symbol_id person, name, label, key;
        ng_node_id x, y;
        ng_value pv;
        ng_node_constraint_kind kind;
        assert(ng_create(&g, "schema.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_create(g, &person, 1, &x) == NG_OK);
        assert(ng_node_create(g, &person, 1, &y) == NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, x, name, &pv) == NG_OK);
        pv.length = 3;
        pv.as.string = "Bob";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, person, name) ==
               NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_EXISTS);
        assert(ng_node_constraint_count(g) == 2);
        assert(ng_save(g) == NG_OK);
        ng_close(g);
        assert(ng_open(&g, "schema.ng") == NG_OK);
        assert(ng_node_constraint_count(g) == 2);
        assert(ng_node_constraint_get(g, 0, &kind, &label, &key) == NG_OK &&
               kind == NG_NODE_CONSTRAINT_REQUIRED_PROPERTY && label == person && key == name);
        assert(ng_node_constraint_get(g, 1, &kind, &label, &key) == NG_OK &&
               kind == NG_NODE_CONSTRAINT_UNIQUE_PROPERTY && label == person && key == name);
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, y, name, &pv) == NG_EXISTS);
        assert(ng_node_unset(g, x, name) == NG_NOT_FOUND);
        pv.type = NG_VALUE_NULL;
        pv.length = 0;
        assert(ng_node_set(g, x, name, &pv) == NG_NOT_FOUND);
        assert(ng_node_constraint_drop(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_OK);
        pv.type = NG_VALUE_STRING;
        pv.length = 5;
        pv.as.string = "Alice";
        assert(ng_node_set(g, y, name, &pv) == NG_OK);
        assert(ng_save(g) == NG_OK);
        ng_close(g);
        remove("schema.ng");
    }
    {
        ng_symbol_id person, name;
        ng_node_id x, y;
        ng_property props[1];
        ng_value got;
        assert(ng_create(&g, "create-props.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, person, name) ==
               NG_OK);
        assert(ng_node_create_with_properties(g, &person, 1, 0, 0, &x) == NG_NOT_FOUND);
        assert(ng_node_count(g) == 0);
        props[0].key = name;
        props[0].value.type = NG_VALUE_STRING;
        props[0].value.length = 5;
        props[0].value.as.string = "Alice";
        assert(ng_node_create_with_properties(g, &person, 1, props, 1, &x) == NG_OK);
        assert(ng_node_property(g, x, name, &got) == NG_OK && got.type == NG_VALUE_STRING &&
               got.length == 5 && !memcmp(got.as.string, "Alice", 5));
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_UNIQUE_PROPERTY, person, name) ==
               NG_OK);
        assert(ng_node_create_with_properties(g, &person, 1, props, 1, &y) == NG_EXISTS);
        assert(ng_node_count(g) == 1);
        props[0].value.length = 3;
        props[0].value.as.string = "Bob";
        assert(ng_node_create_with_properties(g, &person, 1, props, 1, &y) == NG_OK);
        assert(ng_node_count(g) == 2 && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("create-props.ng");
    }
    {
        FILE *nf = fopen("constraint-nodes.tsv", "wb"), *rf = fopen("constraint-rels.tsv", "wb");
        ng_symbol_id person, name;
        assert(nf && rf);
        fputs("node\ta\tPerson\t\n", nf);
        assert(fclose(nf) == 0);
        assert(fclose(rf) == 0);
        assert(ng_create(&g, "constraint-import.ng") == NG_OK);
        assert(ng_symbol(g, "Person", &person) == NG_OK);
        assert(ng_symbol(g, "name", &name) == NG_OK);
        assert(ng_node_constraint_create(g, NG_NODE_CONSTRAINT_REQUIRED_PROPERTY, person, name) ==
               NG_OK);
        assert(ng_import_property_graph(
                   g, "constraint-nodes.tsv", "constraint-rels.tsv", 0, &n, 0) == NG_NOT_FOUND);
        assert(n == 0 && ng_node_count(g) == 0 && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("constraint-import.ng");
        remove("constraint-nodes.tsv");
        remove("constraint-rels.tsv");
    }
    {
        FILE* f;
        assert(ng_create(&g, "backup.ng") == NG_OK);
        assert(ng_symbol(g, "P", &p) == NG_OK);
        assert(ng_node_create(g, &p, 1, &a) == NG_OK);
        f = fopen("guard-n.tsv.nautylusbak", "wb");
        assert(f);
        fputs("backup\n", f);
        assert(fclose(f) == 0);
        assert(ng_export_property_graph(g, "guard-n.tsv", "guard-r.tsv") == NG_EXISTS);
        ng_close(g);
        remove("backup.ng");
        remove("guard-n.tsv.nautylusbak");
        remove("guard-r.tsv.nautylusbak");
        remove("guard-n.tsv");
        remove("guard-r.tsv");
    }
    {
        FILE* f = fopen("pipe.tsv", "wb");
        assert(f);
        fputs("alice\tKNOWS\tbob\n", f);
        assert(fclose(f) == 0);
        remove("pipe.ng");
        assert(system(NAUTYLUS_CLI " create pipe.ng > pipe-create.out") == 0);
        assert(system(NAUTYLUS_CLI " open pipe.ng > pipe-open.out") == 0);
        assert(system(NAUTYLUS_CLI " store pipe.ng pipe.tsv > pipe-import.out") == 0);
        assert(system(NAUTYLUS_CLI " analyze pipe.ng > pipe-analyze.out") == 0);
        assert(system(NAUTYLUS_CLI " analyse pipe.ng > pipe-analyse.out") == 0);
        assert(system(NAUTYLUS_CLI " export pipe.ng - > pipe-out.tsv") == 0);
        assert(system(NAUTYLUS_CLI
                      " search pipe.ng 'MATCH (n) RETURN n LIMIT 1' > pipe-search.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query pipe.ng 'MATCH (n) RETURN n LIMIT 1' > pipe-query.out") == 0);
        assert(system(NAUTYLUS_CLI " explain 'MATCH (n) RETURN n LIMIT 1' > pipe-explain.out") ==
               0);
        assert(same_file("pipe.tsv", "pipe-out.tsv"));
        remove("pipe.ng");
        remove("pipe.tsv");
        remove("pipe-out.tsv");
        remove("pipe-create.out");
        remove("pipe-open.out");
        remove("pipe-import.out");
        remove("pipe-analyze.out");
        remove("pipe-analyse.out");
        remove("pipe-search.out");
        remove("pipe-query.out");
        remove("pipe-explain.out");
    }
    {
        FILE* f = fopen("nosave.tsv", "wb");
        assert(f);
        fputs("alice\tKNOWS\tbob\n", f);
        assert(fclose(f) == 0);
        remove("nosave.ng");
        assert(system(NAUTYLUS_CLI " create nosave.ng > nosave-create.out") == 0);
        assert(system(NAUTYLUS_CLI " import nosave.ng nosave.tsv > nosave-import.out") == 0);
        assert(system(NAUTYLUS_CLI " stats nosave.ng > nosave-stats.out") == 0);
        f = fopen("nosave-stats.expected", "wb");
        assert(f);
        fputs("nodes: 0\nrelationships: 0\nsymbols: 0\n", f);
        assert(fclose(f) == 0);
        assert(same_file("nosave-stats.out", "nosave-stats.expected"));
        remove("nosave.ng");
        remove("nosave.tsv");
        remove("nosave-create.out");
        remove("nosave-import.out");
        remove("nosave-stats.out");
        remove("nosave-stats.expected");
    }
    {
        FILE* f = fopen("csv.csv", "wb");
        assert(f);
        fputs("\"ali,ce\",KNOWS,\"bo\"\"b\"\n", f);
        assert(fclose(f) == 0);
        remove("csv.ng");
        assert(system(NAUTYLUS_CLI " create csv.ng > csv-create.out") == 0);
        assert(system(NAUTYLUS_CLI " store-csv csv.ng csv.csv > csv-import.out") == 0);
        assert(system(NAUTYLUS_CLI " export csv.ng - > csv-out.tsv") == 0);
        f = fopen("csv-expected.tsv", "wb");
        assert(f);
        fputs("ali,ce\tKNOWS\tbo\"b\n", f);
        assert(fclose(f) == 0);
        assert(same_file("csv-expected.tsv", "csv-out.tsv"));
        remove("csv.ng");
        remove("csv.csv");
        remove("csv-out.tsv");
        remove("csv-expected.tsv");
        remove("csv-create.out");
        remove("csv-import.out");
    }
    {
        assert(system(NAUTYLUS_CLI " help > help.out") == 0);
        assert(system(NAUTYLUS_CLI " --version > version.out") == 0);
        remove("help.out");
        remove("version.out");
    }
    {
        remove("bench.ng");
        assert(system(NAUTYLUS_CLI " bench bench.ng 128 > bench.out") == 0);
        remove("bench.ng");
        remove("bench.out");
    }
    {
        FILE *nf = fopen("cli-nodes.tsv", "wb"), *rf = fopen("cli-rels.tsv", "wb");
        assert(nf && rf);
        fputs("node\ta\tCli\tname=s:416c696365\nnode\tb\tCli\tname=s:426f62\nnode\tc\tCli\tname=s:"
              "4361726c\n",
              nf);
        fputs("relationship\tr\ta\tKNOWS\tb\tsince=i:2026\nrelationship\tr2\tb\tKNOWS\tc\tsince=i:"
              "2027\n",
              rf);
        assert(fclose(nf) == 0);
        assert(fclose(rf) == 0);
        remove("cli.ng");
        remove("cli2.ng");
        assert(system(NAUTYLUS_CLI " create cli.ng > cli-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " store-ng cli.ng cli-nodes.tsv cli-rels.tsv > cli-import.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " constraint-require cli.ng Cli name > cli-constraint-require.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " constraint-unique cli.ng Cli name > cli-constraint-unique.out") == 0);
        assert(system(NAUTYLUS_CLI " constraints cli.ng > cli-constraints.out") == 0);
        {
            FILE* ef = fopen("cli-constraints.expected", "wb");
            assert(ef);
            fputs("required Cli name\nunique Cli name\n", ef);
            assert(fclose(ef) == 0);
            assert(same_file("cli-constraints.out", "cli-constraints.expected"));
            remove("cli-constraints.expected");
        }
        assert(system(NAUTYLUS_CLI " index-create cli.ng Cli name > cli-index-create.out") == 0);
        assert(system(NAUTYLUS_CLI " indexes cli.ng > cli-indexes.out") == 0);
        {
            FILE* ef = fopen("cli-indexes.expected", "wb");
            assert(ef);
            fputs("node Cli name\n", ef);
            assert(fclose(ef) == 0);
            assert(same_file("cli-indexes.out", "cli-indexes.expected"));
            remove("cli-indexes.expected");
        }
        assert(system(NAUTYLUS_CLI " validate cli.ng > cli-validate.out") == 0);
        assert(system(NAUTYLUS_CLI " stats cli.ng > cli-stats.out") == 0);
        assert(system(NAUTYLUS_CLI " search cli.ng 'MATCH (n:Cli) WHERE n.name = \"Alice\" RETURN "
                                   "n.name LIMIT 1' > cli-search.out") == 0);
        assert(system(NAUTYLUS_CLI " search cli.ng 'MATCH (n:Cli)-[:KNOWS*2]->(m:Cli) RETURN "
                                   "n.name, m.name LIMIT 1' > cli-multi.out") == 0);
        {
            FILE* ef = fopen("cli-multi.expected", "wb");
            assert(ef);
            fputs("Alice	Carl\n", ef);
            assert(fclose(ef) == 0);
            assert(same_file("cli-multi.out", "cli-multi.expected"));
            remove("cli-multi.expected");
        }
        assert(system(NAUTYLUS_CLI " export-ng cli.ng cli-export-nodes.tsv cli-export-rels.tsv") ==
               0);
        assert(system(NAUTYLUS_CLI " create cli2.ng > cli2-create.out") == 0);
        assert(
            system(
                NAUTYLUS_CLI
                " store-ng cli2.ng cli-export-nodes.tsv cli-export-rels.tsv > cli2-import.out") ==
            0);
        assert(system(NAUTYLUS_CLI " validate cli2.ng > cli2-validate.out") == 0);
        remove("cli.ng");
        remove("cli2.ng");
        remove("cli-nodes.tsv");
        remove("cli-rels.tsv");
        remove("cli-export-nodes.tsv");
        remove("cli-export-rels.tsv");
        remove("cli-create.out");
        remove("cli-import.out");
        remove("cli-constraint-require.out");
        remove("cli-constraint-unique.out");
        remove("cli-constraints.out");
        remove("cli-index-create.out");
        remove("cli-indexes.out");
        remove("cli-validate.out");
        remove("cli-stats.out");
        remove("cli-search.out");
        remove("cli-multi.out");
        remove("cli2-create.out");
        remove("cli2-import.out");
        remove("cli2-validate.out");
    }
    {
        FILE *nf = fopen("bool-nodes.tsv", "wb"), *rf = fopen("bool-rels.tsv", "wb"), *ef;
        assert(nf && rf);
        fputs("node\ta\tPerson\tname=s:416c696365\nnode\tb\tPerson\tname=s:"
              "426f62\nnode\tc\tPerson\tname=s:4361726c\n",
              nf);
        fputs("relationship\tr\ta\tKNOWS\tb\tsince=i:2026\n", rf);
        assert(fclose(nf) == 0);
        assert(fclose(rf) == 0);
        remove("bool.ng");
        assert(system(NAUTYLUS_CLI " create bool.ng > bool-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " store-ng bool.ng bool-nodes.tsv bool-rels.tsv > bool-store.out") == 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" OR "
                                   "n.name = \"Alice\" RETURN n.name' > bool-or.out") == 0);
        ef = fopen("bool-or.expected", "wb");
        assert(ef);
        fputs("Alice\nBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-or.out", "bool-or.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person {name: \"Alice\"}) RETURN "
                                   "person.name' > bool-node-map.out") == 0);
        ef = fopen("bool-node-map.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-node-map.out", "bool-node-map.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person {name: \"Alice\"}) WHERE person.name = "
                      "\"Bob\" RETURN person.name' > bool-node-map-where.out") == 0);
        ef = fopen("bool-node-map-where.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-node-map-where.out", "bool-node-map-where.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (:Person {name: \"Alice\"}) MATCH (friend:Person "
                      "{name: \"Bob\"}) RETURN friend.name LIMIT 1' > bool-anon-node.out") == 0);
        ef = fopen("bool-anon-node.expected", "wb");
        assert(ef);
        fputs("Bob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-anon-node.out", "bool-anon-node.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person {name \"Alice\"}) RETURN person.name' "
                      "> bool-bad-node-map.out 2> bool-bad-node-map.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name IN [\"Bob\", "
                                   "\"Alice\"] RETURN n.name' > bool-in.out") == 0);
        ef = fopen("bool-in.expected", "wb");
        assert(ef);
        fputs("Alice\nBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-in.out", "bool-in.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) WHERE (n.name = \"Bob\" OR n.name = "
                      "\"Alice\") AND n.name = \"Alice\" RETURN n.name' > bool-paren.out") == 0);
        ef = fopen("bool-paren.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-paren.out", "bool-paren.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE NOT (n.name = \"Bob\" "
                                   "OR n.name = \"Alice\") RETURN n.name' > bool-not.out") == 0);
        ef = fopen("bool-not.expected", "wb");
        assert(ef);
        fputs("Carl\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-not.out", "bool-not.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE NOT RETURN n.name' > "
                                   "bool-badnot.out 2> bool-badnot.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.nickname IS "
                                   "NULL RETURN person.name' > bool-is-null.out") == 0);
        ef = fopen("bool-is-null.expected", "wb");
        assert(ef);
        fputs("Alice\nBob\nCarl\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-is-null.out", "bool-is-null.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name IS NOT "
                                   "NULL RETURN person.name' > bool-is-not-null.out") == 0);
        assert(same_file("bool-is-not-null.out", "bool-is-null.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person) WHERE person.name IS EMPTY RETURN "
                      "person.name' > bool-bad-is-null.out 2> bool-bad-is-null.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) WHERE (n.name = \"Bob\" OR n.name = "
                      "\"Alice\" RETURN n.name' > bool-badparen.out 2> bool-badparen.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name <> \"Carl\" "
                                   "RETURN n.name' > bool-ne.out") == 0);
        ef = fopen("bool-ne.expected", "wb");
        assert(ef);
        fputs("Alice\nBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-ne.out", "bool-ne.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name > \"Bob\" RETURN "
                                   "n.name' > bool-gt.out") == 0);
        ef = fopen("bool-gt.expected", "wb");
        assert(ef);
        fputs("Carl\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-gt.out", "bool-gt.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " query bool.ng 'MATCH (n:Person) WHERE id(n) >= 2 RETURN n.name' > bool-ge.out") ==
            0);
        ef = fopen("bool-ge.expected", "wb");
        assert(ef);
        fputs("Bob\nCarl\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-ge.out", "bool-ge.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name != \"Bob\" "
                                   "RETURN n.name' > bool-badcmp.out 2> bool-badcmp.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name ORDER BY n.name "
                                   "DESC SKIP 1 LIMIT 1' > bool-order.out") == 0);
        ef = fopen("bool-order.expected", "wb");
        assert(ef);
        fputs("Bob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-order.out", "bool-order.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name AS name ORDER "
                                   "BY n.name LIMIT 1' > bool-alias.out") == 0);
        ef = fopen("bool-alias.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-alias.out", "bool-alias.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name AS ORDER BY "
                                   "n.name' > bool-badalias.out 2> bool-badalias.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[]->(m) RETURN m.name ORDER "
                                   "BY m.name' > bool-multinode-order.out") == 0);
        ef = fopen("bool-multinode-order.expected", "wb");
        assert(ef);
        fputs("Bob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-multinode-order.out", "bool-multinode-order.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2026}]->(m:Person) WHERE "
                      "r.since >= 2020 RETURN n.name, r.since, m.name' > bool-relprop.out") == 0);
        ef = fopen("bool-relprop.expected", "wb");
        assert(ef);
        fputs("Alice\t2026\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relprop.out", "bool-relprop.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person {name: \"Alice\"})-[knows:KNOWS "
                      "{since: 2026}]->(friend:Person {name: \"Bob\"}) RETURN person.name, "
                      "knows.since, friend.name' > bool-rel-node-map.out") == 0);
        assert(same_file("bool-rel-node-map.out", "bool-relprop.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person)-[knows:KNOWS]->(friend:Person) WHERE "
                      "knows.missing IS NULL AND knows.since IS NOT NULL RETURN person.name, "
                      "friend.name' > bool-rel-is-null.out") == 0);
        ef = fopen("bool-rel-is-null.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-rel-is-null.out", "bool-rel-is-null.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: "
                      "2025}]->(m:Person) RETURN n.name' > bool-relprop-empty.out") == 0);
        ef = fopen("bool-relprop-empty.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relprop-empty.out", "bool-relprop-empty.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE r.since = 2026 RETURN "
                                   "n.name' > bool-badrelvar.out 2> bool-badrelvar.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2026}]->(m:Person) SET "
                      "r.strength = 7 RETURN r.strength' > bool-relset.out") == 0);
        ef = fopen("bool-relset.expected", "wb");
        assert(ef);
        fputs("7\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relset.out", "bool-relset.expected"));
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE r.strength = "
                      "7 RETURN n.name, m.name' > bool-relset-search.out") == 0);
        ef = fopen("bool-relset-search.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relset-search.out", "bool-relset-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)-[r:KNOWS*1..2]->(m:Person) "
                      "SET r.bad = 1' > bool-badrelset.out 2> bool-badrelset.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE "
                                   "r.strength = 7 DELETE r' > bool-reldelete.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE r.strength = "
                      "7 RETURN n.name, m.name' > bool-reldelete-search.out") == 0);
        ef = fopen("bool-reldelete-search.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-reldelete-search.out", "bool-reldelete-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS*1..2]->(m:Person) "
                                   "DELETE r' > bool-badreldel.out 2> bool-badreldel.err") != 0);
        assert(
            system(NAUTYLUS_CLI
                   " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name "
                   "= \"Alice\" AND friend.name = \"Bob\" MERGE (person)-[knows:KNOWS {since: "
                   "2030}]->(friend) RETURN knows.since' > bool-relmerge.out") == 0);
        ef = fopen("bool-relmerge.expected", "wb");
        assert(ef);
        fputs("2030\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relmerge.out", "bool-relmerge.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name "
                   "= \"Alice\" AND friend.name = \"Bob\" MERGE (person)-[knows:KNOWS {since: "
                   "2030}]->(friend) RETURN knows.since' > bool-relmerge-again.out") == 0);
        assert(same_file("bool-relmerge-again.out", "bool-relmerge.expected"));
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2030}]->(m:Person) "
                      "RETURN r.since' > bool-relmerge-search.out") == 0);
        assert(same_file("bool-relmerge-search.out", "bool-relmerge.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person)-[knows:KNOWS {since: "
                                   "2030}]->(friend:Person) SET knows.weight = 9 RETURN "
                                   "knows.weight' > bool-relset-var.out") == 0);
        ef = fopen("bool-relset-var.expected", "wb");
        assert(ef);
        fputs("9\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relset-var.out", "bool-relset-var.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (person:Person)-[knows:KNOWS {since: "
                                   "2030}]->(friend:Person) WHERE knows.weight = 9 RETURN "
                                   "person.name, friend.name' > bool-relset-var-search.out") == 0);
        ef = fopen("bool-relset-var-search.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-relset-var-search.out", "bool-relset-var-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" MERGE "
                      "(n)-[r:KNOWS]->(m)' > bool-badrelmerge.out 2> bool-badrelmerge.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)<-[r:KNOWS {since: 2030}]-(m:Person) WHERE "
                      "n.name = \"Bob\" RETURN m.name, r.since, n.name' > bool-incoming.out") == 0);
        ef = fopen("bool-incoming.expected", "wb");
        assert(ef);
        fputs("Alice\t2030\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-incoming.out", "bool-incoming.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2030}]-(m:Person) WHERE "
                      "n.name = \"Bob\" RETURN n.name, m.name' > bool-undirected.out") == 0);
        ef = fopen("bool-undirected.expected", "wb");
        assert(ef);
        fputs("Bob\tAlice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-undirected.out", "bool-undirected.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)<-[r:KNOWS]->(m:Person) RETURN "
                                   "n.name' > bool-baddir.out 2> bool-baddir.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) MATCH (m:Person) WHERE n.name = \"Alice\" "
                      "AND m.name = \"Bob\" RETURN n.name, m.name' > bool-multimatch.out") == 0);
        ef = fopen("bool-multimatch.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-multimatch.out", "bool-multimatch.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) MATCH (m:Person) RETURN "
                                   "m.name SKIP 1 LIMIT 1' > bool-multimatch-skip.out") == 0);
        ef = fopen("bool-multimatch-skip.expected", "wb");
        assert(ef);
        fputs("Bob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-multimatch-skip.out", "bool-multimatch-skip.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) MATCH (x:Person) WHERE x.name = "
                      "\"Bob\" RETURN n.name, x.name LIMIT 1' > bool-varmatch.out") == 0);
        ef = fopen("bool-varmatch.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-varmatch.out", "bool-varmatch.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) MATCH (x:Person) RETURN "
                      "z.name' > bool-badmultimatch.out 2> bool-badmultimatch.err") != 0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" AND "
                                   "n.name = \"Alice\" RETURN n.name' > bool-and.out") == 0);
        ef = fopen("bool-and.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-and.out", "bool-and.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'CREATE (person:Person {name: \"Dana\", age: "
                                   "30}) RETURN person.name' > bool-create-node.out") == 0);
        ef = fopen("bool-create-node.expected", "wb");
        assert(ef);
        fputs("Dana\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-create-node.out", "bool-create-node.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Dana\" "
                                   "RETURN n.name' > bool-create-node-search.out") == 0);
        assert(same_file("bool-create-node-search.out", "bool-create-node.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name "
                   "= \"Alice\" AND friend.name = \"Dana\" CREATE (person)-[edge:KNOWS]->(friend) "
                   "RETURN person.name, friend.name' > bool-create-rel.out") == 0);
        ef = fopen("bool-create-rel.expected", "wb");
        assert(ef);
        fputs("Alice\tDana\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-create-rel.out", "bool-create-rel.expected"));
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (n:Person)-[:KNOWS]->(m:Person) WHERE m.name = "
                      "\"Dana\" RETURN n.name, m.name' > bool-create-rel-search.out") == 0);
        assert(same_file("bool-create-rel-search.out", "bool-create-rel.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name "
                   "= \"Alice\" AND friend.name = \"Dana\" CREATE (person)<-[edge:KNOWS]-(friend) "
                   "RETURN person.name, friend.name' > bool-create-rel-reverse.out") == 0);
        assert(same_file("bool-create-rel-reverse.out", "bool-create-rel.expected"));
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (n:Person)-[:KNOWS]->(m:Person) WHERE n.name = "
                      "\"Dana\" RETURN m.name, n.name' > bool-create-rel-reverse-search.out") == 0);
        assert(same_file("bool-create-rel-reverse-search.out", "bool-create-rel.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name = "
                "\"Alice\" AND friend.name = \"Dana\" MERGE (person)<-[edge:KNOWS {since: "
                "2040}]-(friend) RETURN person.name, friend.name' > bool-merge-rel-reverse.out") ==
            0);
        assert(same_file("bool-merge-rel-reverse.out", "bool-create-rel.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE "
                      "person.name = \"Alice\" AND friend.name = \"Dana\" MERGE "
                      "(person)<-[edge:KNOWS {since: 2040}]-(friend) RETURN person.name, "
                      "friend.name' > bool-merge-rel-reverse-again.out") == 0);
        assert(same_file("bool-merge-rel-reverse-again.out", "bool-create-rel.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " search bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2040}]->(m:Person) WHERE "
                "n.name = \"Dana\" RETURN m.name, n.name' > bool-merge-rel-reverse-search.out") ==
            0);
        assert(same_file("bool-merge-rel-reverse-search.out", "bool-create-rel.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name "
                   "= \"Alice\" AND friend.name = \"Dana\" CREATE (n)-[:KNOWS]->(friend)' > "
                   "bool-badcreaterel-var.out 2> bool-badcreaterel-var.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person)-[edge:KNOWS]->(friend:Person) WHERE "
                      "friend.name = \"Dana\" DELETE edge' > bool-reldelete-var.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " search bool.ng 'MATCH (person:Person)-[edge:KNOWS]->(friend:Person) WHERE "
                      "friend.name = \"Dana\" RETURN person.name, friend.name' > "
                      "bool-reldelete-var-search.out") == 0);
        ef = fopen("bool-reldelete-var-search.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-reldelete-var-search.out", "bool-reldelete-var-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" CREATE "
                      "(n)-[:KNOWS]->(m)' > bool-badcreaterel.out 2> bool-badcreaterel.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'MATCH (person:Person) WHERE person.name = \"Alice\" SET "
                      "person.city = \"Berlin\" RETURN person.city' > bool-set.out") == 0);
        ef = fopen("bool-set.expected", "wb");
        assert(ef);
        fputs("Berlin\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-set.out", "bool-set.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.city = \"Berlin\" "
                                   "RETURN n.name' > bool-set-search.out") == 0);
        ef = fopen("bool-set-search.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-set-search.out", "bool-set-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MERGE (person:Person {name: \"Alice\"}) RETURN "
                                   "person.name' > bool-merge-existing.out") == 0);
        ef = fopen("bool-merge-existing.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-merge-existing.out", "bool-merge-existing.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" "
                                   "RETURN n.name' > bool-merge-existing-search.out") == 0);
        assert(same_file("bool-merge-existing-search.out", "bool-merge-existing.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MERGE (person:Person {name: \"Eve\"}) RETURN "
                                   "person.name' > bool-merge-new.out") == 0);
        ef = fopen("bool-merge-new.expected", "wb");
        assert(ef);
        fputs("Eve\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-merge-new.out", "bool-merge-new.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name = "
                                   "\"Bob\" DELETE person' > bool-delete.out") == 0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" "
                                   "RETURN n.name' > bool-delete-search.out") == 0);
        ef = fopen("bool-delete-search.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-delete-search.out", "bool-delete-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'CREATE (n:Person {name \"Bad\"}) RETURN "
                                   "n.name' > bool-badcreate.out 2> bool-badcreate.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query bool.ng 'CREATE (n:Person {name: \"BadTail\"}) RETURN x.name' > "
                      "bool-badcreate-tail.out 2> bool-badcreate-tail.err") != 0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"BadTail\" "
                                   "RETURN n.name' > bool-badcreate-tail-search.out") == 0);
        ef = fopen("bool-badcreate-tail-search.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("bool-badcreate-tail-search.out", "bool-badcreate-tail-search.expected"));
        remove("bool.ng");
        remove("bool-nodes.tsv");
        remove("bool-rels.tsv");
        remove("bool-create.out");
        remove("bool-store.out");
        remove("bool-or.out");
        remove("bool-or.expected");
        remove("bool-node-map.out");
        remove("bool-node-map.expected");
        remove("bool-node-map-where.out");
        remove("bool-node-map-where.expected");
        remove("bool-anon-node.out");
        remove("bool-anon-node.expected");
        remove("bool-bad-node-map.out");
        remove("bool-bad-node-map.err");
        remove("bool-in.out");
        remove("bool-in.expected");
        remove("bool-paren.out");
        remove("bool-paren.expected");
        remove("bool-not.out");
        remove("bool-not.expected");
        remove("bool-badnot.out");
        remove("bool-badnot.err");
        remove("bool-is-null.out");
        remove("bool-is-null.expected");
        remove("bool-is-not-null.out");
        remove("bool-bad-is-null.out");
        remove("bool-bad-is-null.err");
        remove("bool-badparen.out");
        remove("bool-badparen.err");
        remove("bool-ne.out");
        remove("bool-ne.expected");
        remove("bool-gt.out");
        remove("bool-gt.expected");
        remove("bool-ge.out");
        remove("bool-ge.expected");
        remove("bool-badcmp.out");
        remove("bool-badcmp.err");
        remove("bool-order.out");
        remove("bool-order.expected");
        remove("bool-alias.out");
        remove("bool-alias.expected");
        remove("bool-badalias.out");
        remove("bool-badalias.err");
        remove("bool-multinode-order.out");
        remove("bool-multinode-order.expected");
        remove("bool-relprop.out");
        remove("bool-relprop.expected");
        remove("bool-rel-node-map.out");
        remove("bool-rel-is-null.out");
        remove("bool-rel-is-null.expected");
        remove("bool-relprop-empty.out");
        remove("bool-relprop-empty.expected");
        remove("bool-badrelvar.out");
        remove("bool-badrelvar.err");
        remove("bool-relset.out");
        remove("bool-relset.expected");
        remove("bool-relset-search.out");
        remove("bool-relset-search.expected");
        remove("bool-badrelset.out");
        remove("bool-badrelset.err");
        remove("bool-reldelete.out");
        remove("bool-reldelete-search.out");
        remove("bool-reldelete-search.expected");
        remove("bool-badreldel.out");
        remove("bool-badreldel.err");
        remove("bool-relmerge.out");
        remove("bool-relmerge.expected");
        remove("bool-relmerge-again.out");
        remove("bool-relmerge-search.out");
        remove("bool-badrelmerge.out");
        remove("bool-badrelmerge.err");
        remove("bool-relset-var.out");
        remove("bool-relset-var.expected");
        remove("bool-relset-var-search.out");
        remove("bool-relset-var-search.expected");
        remove("bool-incoming.out");
        remove("bool-incoming.expected");
        remove("bool-undirected.out");
        remove("bool-undirected.expected");
        remove("bool-baddir.out");
        remove("bool-baddir.err");
        remove("bool-varmatch.out");
        remove("bool-varmatch.expected");
        remove("bool-multimatch.out");
        remove("bool-multimatch.expected");
        remove("bool-multimatch-skip.out");
        remove("bool-multimatch-skip.expected");
        remove("bool-badmultimatch.out");
        remove("bool-badmultimatch.err");
        remove("bool-and.out");
        remove("bool-and.expected");
        remove("bool-create-node.out");
        remove("bool-create-node.expected");
        remove("bool-create-node-search.out");
        remove("bool-create-rel.out");
        remove("bool-create-rel.expected");
        remove("bool-create-rel-search.out");
        remove("bool-create-rel-reverse.out");
        remove("bool-create-rel-reverse-search.out");
        remove("bool-merge-rel-reverse.out");
        remove("bool-merge-rel-reverse-again.out");
        remove("bool-merge-rel-reverse-search.out");
        remove("bool-badcreaterel-var.out");
        remove("bool-badcreaterel-var.err");
        remove("bool-reldelete-var.out");
        remove("bool-reldelete-var-search.out");
        remove("bool-reldelete-var-search.expected");
        remove("bool-badcreaterel.out");
        remove("bool-badcreaterel.err");
        remove("bool-set.out");
        remove("bool-set.expected");
        remove("bool-set-search.out");
        remove("bool-set-search.expected");
        remove("bool-merge-existing.out");
        remove("bool-merge-existing.expected");
        remove("bool-merge-existing-search.out");
        remove("bool-merge-new.out");
        remove("bool-merge-new.expected");
        remove("bool-delete.out");
        remove("bool-delete-search.out");
        remove("bool-delete-search.expected");
        remove("bool-badcreate.out");
        remove("bool-badcreate.err");
        remove("bool-badcreate-tail.out");
        remove("bool-badcreate-tail.err");
        remove("bool-badcreate-tail-search.out");
        remove("bool-badcreate-tail-search.expected");
    }
    {
        FILE *nf = fopen("chain-nodes.tsv", "wb"), *rf = fopen("chain-rels.tsv", "wb"), *ef;
        assert(nf && rf);
        fputs("node\ta\tPerson\tname=s:416c696365;age=i:40\nnode\tb\tPerson\tname=s:"
              "426f62\nnode\tc\tPerson\tname=s:4361726c\nnode\tx\tPerson\tname=s:"
              "586176696572\nnode\td\tPerson\tname=s:44616e61\n",
              nf);
        fputs("relationship\tr1\ta\tKNOWS\tb\tsince=i:1\nrelationship\tr2\ta\tKNOWS\tx\tsince=i:"
              "2\nrelationship\tr3\tb\tWORKS_WITH\tc\tweight=i:3\nrelationship\tr4\tx\tWORKS_"
              "WITH\td\tweight=i:4\n",
              rf);
        assert(fclose(nf) == 0);
        assert(fclose(rf) == 0);
        remove("chain.ng");
        assert(system(NAUTYLUS_CLI " create chain.ng > chain-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " store-ng chain.ng chain-nodes.tsv chain-rels.tsv > chain-store.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[r1:KNOWS]->(b)-[r2:WORKS_WITH]->(c) "
                      "RETURN a.name, b.name, c.name' > chain-path.out") == 0);
        ef = fopen("chain-path.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\tCarl\nAlice\tXavier\tDana\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-path.out", "chain-path.expected"));
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[r1:KNOWS]->(b)-[r2:WORKS_WITH]->(c) "
                      "WHERE a.name = \"Alice\" AND b.name = \"Bob\" AND r2.weight = 3 RETURN "
                      "a.name, r1.since, b.name, r2.weight, c.name' > chain-where.out") == 0);
        ef = fopen("chain-where.expected", "wb");
        assert(ef);
        fputs("Alice\t1\tBob\t3\tCarl\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-where.out", "chain-where.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " query chain.ng 'MATCH (a:Person {name: \"Alice\"})-[r1:KNOWS]->(b) MATCH "
                "(b)-[r2:WORKS_WITH]->(c) RETURN a.name, b.name, c.name' > chain-multimatch.out") ==
            0);
        assert(same_file("chain-multimatch.out", "chain-path.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query chain.ng 'MATCH (a:Person {name: "
                   "\"Alice\"})-[:KNOWS]->()-[:WORKS_WITH]->(c) RETURN c.name' > chain-anon.out") ==
            0);
        ef = fopen("chain-anon.expected", "wb");
        assert(ef);
        fputs("Carl\nDana\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-anon.out", "chain-anon.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1, \"hello\", 2 + 3 "
                   "* 4, (2 + 3) * 4, a.age + 1, -a.age + 50 LIMIT 1' > chain-return-expr.out") ==
            0);
        ef = fopen("chain-return-expr.expected", "wb");
        assert(ef);
        fputs("1\thello\t14\t20\t41\t10\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-return-expr.out", "chain-return-expr.expected"));
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN "
                      "DISTINCT a.name' > chain-distinct-one.out") == 0);
        ef = fopen("chain-distinct-one.expected", "wb");
        assert(ef);
        fputs("Alice\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-distinct-one.out", "chain-distinct-one.expected"));
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN "
                      "DISTINCT a.name, b.name' > chain-distinct-two.out") == 0);
        ef = fopen("chain-distinct-two.expected", "wb");
        assert(ef);
        fputs("Alice\tBob\nAlice\tXavier\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-distinct-two.out", "chain-distinct-two.expected"));
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN "
                      "DISTINCT a.age + 1 SKIP 1' > chain-distinct-skip.out") == 0);
        ef = fopen("chain-distinct-skip.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-distinct-skip.out", "chain-distinct-skip.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person) RETURN DISTINCT' > "
                                   "chain-bad-distinct.out 2> chain-bad-distinct.err") != 0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1 / "
                                   "0' > chain-bad-div.out 2> chain-bad-div.err") != 0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1 + "
                                   "\"x\"' > chain-bad-add.out 2> chain-bad-add.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[:LIKES]->(b)-[:WORKS_WITH]->(c) RETURN "
                      "a.name, c.name' > chain-empty.out") == 0);
        ef = fopen("chain-empty.expected", "wb");
        assert(ef);
        assert(fclose(ef) == 0);
        assert(same_file("chain-empty.out", "chain-empty.expected"));
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN "
                      "z.name' > chain-bad-var.out 2> chain-bad-var.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query chain.ng 'MATCH (a:Person:Employee)-[:KNOWS]->(b) "
                      "RETURN a.name' > chain-bad-label.out 2> chain-bad-label.err") != 0);
        remove("chain.ng");
        remove("chain-nodes.tsv");
        remove("chain-rels.tsv");
        remove("chain-create.out");
        remove("chain-store.out");
        remove("chain-path.out");
        remove("chain-path.expected");
        remove("chain-where.out");
        remove("chain-where.expected");
        remove("chain-multimatch.out");
        remove("chain-anon.out");
        remove("chain-anon.expected");
        remove("chain-return-expr.out");
        remove("chain-return-expr.expected");
        remove("chain-distinct-one.out");
        remove("chain-distinct-one.expected");
        remove("chain-distinct-two.out");
        remove("chain-distinct-two.expected");
        remove("chain-distinct-skip.out");
        remove("chain-distinct-skip.expected");
        remove("chain-bad-distinct.out");
        remove("chain-bad-distinct.err");
        remove("chain-bad-div.out");
        remove("chain-bad-div.err");
        remove("chain-bad-add.out");
        remove("chain-bad-add.err");
        remove("chain-empty.out");
        remove("chain-empty.expected");
        remove("chain-bad-var.out");
        remove("chain-bad-var.err");
        remove("chain-bad-label.out");
        remove("chain-bad-label.err");
    }
    {
        FILE* ef;
        remove("fullcreate.ng");
        assert(system(NAUTYLUS_CLI " create fullcreate.ng > fullcreate-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (a:Person {name: "
                      "\"Joe\"})-[r:KNOWS {since: 2026}]->(b:Person {name: "
                      "\"Bob\"}) RETURN a, b, r.since' > fullcreate-forward.out") == 0);
        ef = fopen("fullcreate-forward.expected", "wb");
        assert(ef);
        fputs("1\t2\t2026\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-forward.out", "fullcreate-forward.expected"));
        assert(system(NAUTYLUS_CLI
                      " search fullcreate.ng 'MATCH (a:Person)-[r:KNOWS {since: 2026}]->(b:Person) "
                      "RETURN a.name, r.since, b.name' > fullcreate-forward-search.out") == 0);
        ef = fopen("fullcreate-forward-search.expected", "wb");
        assert(ef);
        fputs("Joe\t2026\tBob\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-forward-search.out", "fullcreate-forward-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'MATCH (a:Person)-[:KNOWS]->(b:Person) WHERE "
                      "a.name = \"Joe\" RETURN a.name, b.name' --format json > "
                      "fullcreate-json.out") == 0);
        ef = fopen("fullcreate-json.expected", "wb");
        assert(ef);
        fputs("{\"columns\":[\"a.name\",\"b.name\"],\"rows\":[[\"Joe\",\"Bob\"]],\"row_count\":1}\n",
              ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-json.out", "fullcreate-json.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (a:Person {name: "
                      "\"A\"})<-[r:KNOWS {since: 2027}]-(b:Person {name: \"B\"}) "
                      "RETURN a.name, r.since, b.name' > fullcreate-reverse.out") == 0);
        ef = fopen("fullcreate-reverse.expected", "wb");
        assert(ef);
        fputs("A\t2027\tB\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-reverse.out", "fullcreate-reverse.expected"));
        assert(system(NAUTYLUS_CLI
                      " search fullcreate.ng 'MATCH (b:Person)-[r:KNOWS {since: 2027}]->(a:Person) "
                      "RETURN b.name, r.since, a.name' > fullcreate-reverse-search.out") == 0);
        ef = fopen("fullcreate-reverse-search.expected", "wb");
        assert(ef);
        fputs("B\t2027\tA\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-reverse-search.out", "fullcreate-reverse-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (a:Person {name: \"One\"})-[:KNOWS]->(b:Person "
                      "{name: \"Two\"})<-[:LIKES]-(c:Person {name: \"Three\"}) RETURN a.name, "
                      "b.name, c.name' > fullcreate-chain.out") == 0);
        ef = fopen("fullcreate-chain.expected", "wb");
        assert(ef);
        fputs("One\tTwo\tThree\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-chain.out", "fullcreate-chain.expected"));
        assert(system(NAUTYLUS_CLI
                      " search fullcreate.ng 'MATCH "
                      "(a:Person)-[:KNOWS]->(b:Person)<-[:LIKES]-(c:Person) WHERE a.name = \"One\" "
                      "RETURN a.name, b.name, c.name' > fullcreate-chain-search.out") == 0);
        assert(same_file("fullcreate-chain-search.out", "fullcreate-chain.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (x:Person {name: \"X\"}), (y:Person {name: "
                      "\"Y\"}) RETURN x.name, y.name' > fullcreate-comma-nodes.out") == 0);
        ef = fopen("fullcreate-comma-nodes.expected", "wb");
        assert(ef);
        fputs("X\tY\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-comma-nodes.out", "fullcreate-comma-nodes.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " query fullcreate.ng 'CREATE (a:Person {name: \"AA\"}), (b:Person {name: "
                "\"BB\"}), (a)-[:KNOWS {since: 1}]->(b), (b)-[:KNOWS {since: 2}]->(c:Person {name: "
                "\"CC\"}) RETURN a.name, b.name, c.name' > fullcreate-comma-shared.out") == 0);
        ef = fopen("fullcreate-comma-shared.expected", "wb");
        assert(ef);
        fputs("AA\tBB\tCC\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-comma-shared.out", "fullcreate-comma-shared.expected"));
        assert(system(NAUTYLUS_CLI
                      " search fullcreate.ng 'MATCH "
                      "(a:Person)-[:KNOWS]->(b:Person)-[:KNOWS]->(c:Person) WHERE a.name = \"AA\" "
                      "RETURN a.name, b.name, c.name' > fullcreate-comma-shared-search.out") == 0);
        assert(same_file("fullcreate-comma-shared-search.out", "fullcreate-comma-shared.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (ra:Person {name: \"RA\"}), (rb:Person {name: "
                      "\"RB\"}), (ra)<-[rel:KNOWS {since: 2030}]-(rb) RETURN ra.name, rel.since, "
                      "rb.name' > fullcreate-comma-reverse.out") == 0);
        ef = fopen("fullcreate-comma-reverse.expected", "wb");
        assert(ef);
        fputs("RA\t2030\tRB\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-comma-reverse.out", "fullcreate-comma-reverse.expected"));
        assert(
            system(
                NAUTYLUS_CLI
                " search fullcreate.ng 'MATCH (rb:Person)-[rel:KNOWS {since: 2030}]->(ra:Person) "
                "RETURN rb.name, rel.since, ra.name' > fullcreate-comma-reverse-search.out") == 0);
        ef = fopen("fullcreate-comma-reverse-search.expected", "wb");
        assert(ef);
        fputs("RB\t2030\tRA\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("fullcreate-comma-reverse-search.out",
                         "fullcreate-comma-reverse-search.expected"));
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (a:Person {name: "
                      "\"Bad\"})-[r]->(b:Person {name: \"MissingType\"}) RETURN "
                      "a.name' > fullcreate-bad-rel.out 2> fullcreate-bad-rel.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (bad:Person), RETURN bad' > "
                      "fullcreate-bad-comma-return.out 2> fullcreate-bad-comma-return.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " query fullcreate.ng 'CREATE (bad:Person),, (alsoBad:Person)' > "
                      "fullcreate-bad-comma-empty.out 2> fullcreate-bad-comma-empty.err") != 0);
        remove("fullcreate.ng");
        remove("fullcreate-create.out");
        remove("fullcreate-forward.out");
        remove("fullcreate-forward.expected");
        remove("fullcreate-forward-search.out");
        remove("fullcreate-forward-search.expected");
        remove("fullcreate-json.out");
        remove("fullcreate-json.expected");
        remove("fullcreate-reverse.out");
        remove("fullcreate-reverse.expected");
        remove("fullcreate-reverse-search.out");
        remove("fullcreate-reverse-search.expected");
        remove("fullcreate-chain.out");
        remove("fullcreate-chain.expected");
        remove("fullcreate-chain-search.out");
        remove("fullcreate-comma-nodes.out");
        remove("fullcreate-comma-nodes.expected");
        remove("fullcreate-comma-shared.out");
        remove("fullcreate-comma-shared.expected");
        remove("fullcreate-comma-shared-search.out");
        remove("fullcreate-comma-reverse.out");
        remove("fullcreate-comma-reverse.expected");
        remove("fullcreate-comma-reverse-search.out");
        remove("fullcreate-comma-reverse-search.expected");
        remove("fullcreate-bad-rel.out");
        remove("fullcreate-bad-rel.err");
        remove("fullcreate-bad-comma-return.out");
        remove("fullcreate-bad-comma-return.err");
        remove("fullcreate-bad-comma-empty.out");
        remove("fullcreate-bad-comma-empty.err");
    }
    {
        FILE* ef;
        remove("with.ng");
        assert(system(NAUTYLUS_CLI " create with.ng > with-create.out") == 0);
        assert(
            system(NAUTYLUS_CLI
                   " query with.ng 'CREATE (a:Person {name: \"A\", age: 20})-[:KNOWS]->(b:Person "
                   "{name: \"B\", age: 17}), (a)-[:KNOWS]->(c:Person {name: \"C\", age: 30}), "
                   "(d:Person {name: \"A\", age: 40})' > with-seed.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query with.ng 'MATCH (a:Person) WITH a MATCH "
                      "(a)-[:KNOWS]->(b) RETURN a.name, b.name' > with-basic.out") == 0);
        ef = fopen("with-basic.expected", "wb");
        assert(ef);
        fputs("A\tB\nA\tC\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-basic.out", "with-basic.expected"));
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH a.name AS name RETURN "
                                   "name' > with-alias.out") == 0);
        ef = fopen("with-alias.expected", "wb");
        assert(ef);
        fputs("A\nB\nC\nA\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-alias.out", "with-alias.expected"));
        assert(system(NAUTYLUS_CLI
                      " query with.ng 'MATCH (a:Person) WITH a.name AS name, 2 + 3 AS five, a.age "
                      "+ 1 AS next RETURN name, five, next' > with-expr.out") == 0);
        ef = fopen("with-expr.expected", "wb");
        assert(ef);
        fputs("A\t5\t21\nB\t5\t18\nC\t5\t31\nA\t5\t41\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-expr.out", "with-expr.expected"));
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH DISTINCT a.name AS name "
                                   "RETURN name' > with-distinct.out") == 0);
        ef = fopen("with-distinct.expected", "wb");
        assert(ef);
        fputs("A\nB\nC\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-distinct.out", "with-distinct.expected"));
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH a WHERE a.age >= 18 "
                                   "RETURN a.name' > with-where.out") == 0);
        ef = fopen("with-where.expected", "wb");
        assert(ef);
        fputs("A\nC\nA\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-where.out", "with-where.expected"));
        assert(system(NAUTYLUS_CLI
                      " query with.ng 'MATCH (a:Person) WITH a.name AS name, a.age AS age WITH "
                      "name, age + 1 AS next RETURN name, next' > with-chain.out") == 0);
        ef = fopen("with-chain.expected", "wb");
        assert(ef);
        fputs("A\t21\nB\t18\nC\t31\nA\t41\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-chain.out", "with-chain.expected"));
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH a.name AS name RETURN a' "
                                   "> with-scope-bad.out 2> with-scope-bad.err") != 0);
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH a.name RETURN a.name' > "
                                   "with-alias-bad.out 2> with-alias-bad.err") != 0);
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WITH missing RETURN missing' "
                                   "> with-unknown-bad.out 2> with-unknown-bad.err") != 0);
        assert(
            system(
                NAUTYLUS_CLI
                " query with.ng 'MATCH (a:Person) WITH a WHERE a.name = \"B\" CREATE "
                "(a)-[:LIKES]->(e:Person {name: \"E\"}) RETURN a.name, e.name' > with-write.out") ==
            0);
        ef = fopen("with-write.expected", "wb");
        assert(ef);
        fputs("B\tE\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-write.out", "with-write.expected"));
        assert(system(NAUTYLUS_CLI " search with.ng 'MATCH (a:Person)-[:LIKES]->(e:Person) RETURN "
                                   "a.name, e.name' > with-write-search.out") == 0);
        assert(same_file("with-write-search.out", "with-write.expected"));
        assert(system(NAUTYLUS_CLI " query with.ng 'MATCH (a:Person) WHERE a.name = \"A\" RETURN "
                                   "a.name' > with-existing.out") == 0);
        ef = fopen("with-existing.expected", "wb");
        assert(ef);
        fputs("A\nA\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("with-existing.out", "with-existing.expected"));
        remove("with.ng");
        remove("with-create.out");
        remove("with-seed.out");
        remove("with-basic.out");
        remove("with-basic.expected");
        remove("with-alias.out");
        remove("with-alias.expected");
        remove("with-expr.out");
        remove("with-expr.expected");
        remove("with-distinct.out");
        remove("with-distinct.expected");
        remove("with-where.out");
        remove("with-where.expected");
        remove("with-chain.out");
        remove("with-chain.expected");
        remove("with-scope-bad.out");
        remove("with-scope-bad.err");
        remove("with-alias-bad.out");
        remove("with-alias-bad.err");
        remove("with-unknown-bad.out");
        remove("with-unknown-bad.err");
        remove("with-write.out");
        remove("with-write.expected");
        remove("with-write-search.out");
        remove("with-existing.out");
        remove("with-existing.expected");
    }
    {
        ng_graph* rw;
        ng_symbol_id rtype;
        ng_node_id a, b, c, path1[8], path2[8];
        ng_relationship_id rid;
        ng_random_walk_options opts;
        size_t n1, n2;
        FILE* qout;
        int mutated;
        assert(ng_create(&rw, "random-walk.ng") == NG_OK);
        assert(ng_symbol(rw, "R", &rtype) == NG_OK);
        assert(ng_node_create(rw, 0, 0, &a) == NG_OK);
        assert(ng_node_create(rw, 0, 0, &b) == NG_OK);
        assert(ng_node_create(rw, 0, 0, &c) == NG_OK);
        assert(ng_relationship_create(rw, a, rtype, b, &rid) == NG_OK);
        assert(ng_relationship_create(rw, b, rtype, c, &rid) == NG_OK);
        opts.direction = NG_DIRECTION_OUTGOING;
        opts.type = rtype;
        opts.max_steps = 4;
        opts.seed = 7;
        assert(ng_random_walk(rw, a, &opts, path1, 8, &n1) == NG_OK);
        assert(n1 == 3 && path1[0] == a && path1[1] == b && path1[2] == c);
        assert(ng_random_walk(rw, a, &opts, path2, 8, &n2) == NG_OK && n2 == n1);
        assert(memcmp(path1, path2, n1 * sizeof(*path1)) == 0);
        opts.direction = NG_DIRECTION_INCOMING;
        assert(ng_random_walk(rw, c, &opts, path2, 8, &n2) == NG_OK && n2 == 3 && path2[1] == b &&
               path2[2] == a);
        opts.direction = NG_DIRECTION_EITHER;
        opts.type = 0;
        opts.max_steps = 1;
        assert(ng_random_walk(rw, b, &opts, path2, 8, &n2) == NG_OK && n2 == 2);
        assert(ng_random_walk(rw, a, &opts, path2, 1, &n2) == NG_LIMIT && n2 == 2);
        assert(ng_random_walk(rw, 999999, &opts, path2, 8, &n2) == NG_NOT_FOUND);
        qout = tmpfile();
        assert(qout);
        assert(ng_query_execute(rw,
                                "MATCH (a) CALL randomWalk(a, 2, 7) YIELD node RETURN node",
                                qout,
                                &mutated) == NG_OK);
        assert(ng_query_execute(
                   rw, "MATCH (a) CALL randomWalk(a, 2) YIELD node RETURN node", qout, &mutated) ==
               NG_OK);
        assert(ng_query_execute(
                   rw, "MATCH (a) CALL randomWalk(a) YIELD node RETURN node", qout, &mutated) ==
               NG_PARSE_ERROR);
        fclose(qout);
        ng_close(rw);
        remove("random-walk.ng");
    }
    {
        FILE* ef;
        remove("remove.ng");
        assert(system(NAUTYLUS_CLI " create remove.ng > remove-create.out") == 0);
        assert(
            system(
                NAUTYLUS_CLI
                " query remove.ng 'CREATE (n:Person {name: \"A\", age: 30})' > remove-seed.out") ==
            0);
        assert(system(NAUTYLUS_CLI
                      " query remove.ng 'MATCH (n:Person) REMOVE n.name, n:Person' > remove.out") ==
               0);
        assert(system(NAUTYLUS_CLI
                      " search remove.ng 'MATCH (n) RETURN n.name' > remove-absent.out") == 0);
        ef = fopen("remove-absent.expected", "wb");
        assert(ef);
        fputs("null\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("remove-absent.out", "remove-absent.expected"));
        assert(system(NAUTYLUS_CLI
                      " query remove.ng 'MATCH (n) REMOVE n.missing' > remove-missing.out") == 0);
        assert(system(NAUTYLUS_CLI " query remove.ng 'MATCH (n) REMOVE n.age, missing.value' > "
                                   "remove-rollback.out 2> remove-rollback.err") != 0);
        assert(system(NAUTYLUS_CLI
                      " search remove.ng 'MATCH (n) RETURN n.age' > remove-rollback-check.out") ==
               0);
        ef = fopen("remove-rollback-check.expected", "wb");
        assert(ef);
        fputs("30\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("remove-rollback-check.out", "remove-rollback-check.expected"));
        remove("remove.ng");
        remove("remove-create.out");
        remove("remove-seed.out");
        remove("remove.out");
        remove("remove-absent.out");
        remove("remove-absent.expected");
        remove("remove-missing.out");
        remove("remove-rollback.out");
        remove("remove-rollback.err");
        remove("remove-rollback-check.out");
        remove("remove-rollback-check.expected");
        remove("detach.ng");
        assert(system(NAUTYLUS_CLI " create detach.ng > detach-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query detach.ng 'CREATE (a:Person {name: \"A\"})-[:KNOWS]->(b:Person "
                      "{name: \"B\"})' > detach-seed.out") == 0);
        assert(
            system(
                NAUTYLUS_CLI
                " query detach.ng 'MATCH (a:Person {name: \"A\"}) DETACH DELETE a' > detach.out") ==
            0);
        assert(system(NAUTYLUS_CLI " stats detach.ng > detach-stats.out") == 0);
        ef = fopen("detach-stats.expected", "wb");
        assert(ef);
        fputs("nodes: 1\nrelationships: 0\nsymbols: 3\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("detach-stats.out", "detach-stats.expected"));
        assert(system(NAUTYLUS_CLI " query detach.ng 'MATCH (b:Person) DETACH DELETE b, missing' > "
                                   "detach-rollback.out 2> detach-rollback.err") != 0);
        assert(system(NAUTYLUS_CLI " stats detach.ng > detach-rollback-stats.out") == 0);
        assert(same_file("detach-rollback-stats.out", "detach-stats.expected"));
        remove("detach.ng");
        remove("detach-create.out");
        remove("detach-seed.out");
        remove("detach.out");
        remove("detach-stats.out");
        remove("detach-stats.expected");
        remove("detach-rollback.out");
        remove("detach-rollback.err");
        remove("detach-rollback-stats.out");
    }
    {
        FILE* ef;
        remove("mapset.ng");
        assert(system(NAUTYLUS_CLI " create mapset.ng > mapset-create.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query mapset.ng 'CREATE (n:Person {name: \"A\", age: "
                      "30})-[:KNOWS {since: 2020}]->(m:Person)' > mapset-seed.out") == 0);
        assert(system(NAUTYLUS_CLI
                      " query mapset.ng 'MATCH (n:Person {name: \"A\"}) SET n += {city: "
                      "\"Berlin\", age: 31} RETURN n.name, n.age, n.city' > mapset-merge.out") ==
               0);
        ef = fopen("mapset-merge.expected", "wb");
        assert(ef);
        fputs("A\t31\tBerlin\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("mapset-merge.out", "mapset-merge.expected"));
        assert(
            system(NAUTYLUS_CLI
                   " query mapset.ng 'MATCH (n:Person {name: \"A\"}) SET n = {name: \"B\", code: "
                   "\"...\"} RETURN n.name, n.age, n.city, n.code' > mapset-replace.out") == 0);
        ef = fopen("mapset-replace.expected", "wb");
        assert(ef);
        fputs("B\tnull\tnull\t...\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("mapset-replace.out", "mapset-replace.expected"));
        assert(system(NAUTYLUS_CLI " query mapset.ng 'MATCH (n:Person {name: \"B\"}) SET n += "
                                   "{name: null} RETURN n.name' > mapset-null.out") == 0);
        ef = fopen("mapset-null.expected", "wb");
        assert(ef);
        fputs("null\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("mapset-null.out", "mapset-null.expected"));
        assert(system(NAUTYLUS_CLI
                      " query mapset.ng 'MATCH (n:Person) SET n += {}' > mapset-empty.out") == 0);
        assert(
            system(NAUTYLUS_CLI
                   " query mapset.ng 'MATCH (n:Person {name: \"B\"}) SET n += {city: \"Paris\"}, "
                   "missing.value = 1' > mapset-rollback.out 2> mapset-rollback.err") != 0);
        assert(
            system(
                NAUTYLUS_CLI
                " search mapset.ng 'MATCH (n:Person) RETURN n.city' > mapset-rollback-check.out") ==
            0);
        ef = fopen("mapset-rollback-check.expected", "wb");
        assert(ef);
        fputs("null\nnull\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("mapset-rollback-check.out", "mapset-rollback-check.expected"));
        assert(system(NAUTYLUS_CLI
                      " query mapset.ng 'MATCH (a:Person)-[r:KNOWS]->(b:Person) SET r += {since: "
                      "2024, weight: 1.5} RETURN r.since, r.weight' > mapset-rel.out") == 0);
        ef = fopen("mapset-rel.expected", "wb");
        assert(ef);
        fputs("2024\t1.5\n", ef);
        assert(fclose(ef) == 0);
        assert(same_file("mapset-rel.out", "mapset-rel.expected"));
        remove("mapset.ng");
        remove("mapset-create.out");
        remove("mapset-seed.out");
        remove("mapset-merge.out");
        remove("mapset-merge.expected");
        remove("mapset-replace.out");
        remove("mapset-replace.expected");
        remove("mapset-null.out");
        remove("mapset-null.expected");
        remove("mapset-empty.out");
        remove("mapset-rollback.out");
        remove("mapset-rollback.err");
        remove("mapset-rollback-check.out");
        remove("mapset-rollback-check.expected");
        remove("mapset-rel.out");
        remove("mapset-rel.expected");
    }
    /* Core UNWIND and MERGE-map cases adapted from
     * cypher/openCypher/tck/features/clauses/unwind/Unwind1.feature and merge scenarios. */
    {
        ng_value items[3];
        ng_value_list list;
        ng_parameter param;
        FILE* f;
        int mutated = 99;
        size_t before;
        assert(ng_create(&g, "unwind.ng") == NG_OK);
        assert(query_params_file(
                   g, "UNWIND [1, 2, 3] AS x RETURN x", NULL, 0, "unwind.out", &mutated) == NG_OK);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("1\n2\n3\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(g, "UNWIND [] AS x RETURN x", NULL, 0, "unwind.out", &mutated) ==
               NG_OK);
        f = fopen("unwind.expected", "wb");
        assert(f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(g, "UNWIND null AS x RETURN x", NULL, 0, "unwind.out", &mutated) ==
               NG_OK);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(
                   g, "UNWIND [1, 2] AS x WITH x RETURN x + 10", NULL, 0, "unwind.out", &mutated) ==
               NG_OK);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("11\n12\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(
            query_params_file(g,
                              "UNWIND [1, 2] AS x CREATE (n:Value) SET n.value = x RETURN n.value",
                              NULL,
                              0,
                              "unwind.out",
                              &mutated) == NG_OK &&
            mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("1\n2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(g,
                                 "UNWIND [2, 3] AS x CREATE (n:Computed {value: x + 10, doubled: x "
                                 "* 2}) RETURN n.value, n.doubled",
                                 NULL,
                                 0,
                                 "unwind.out",
                                 &mutated) == NG_OK &&
               mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("12\t4\n13\t6\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(
                   g,
                   "UNWIND [1, 2] AS x CREATE (a:Computed {value: x})-[r:LINK {weight: x + "
                   "1}]->(b:Computed {value: x * 2}) RETURN a.value, r.weight, b.value",
                   NULL,
                   0,
                   "unwind.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("1\t2\t2\n2\t3\t4\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(
                   g,
                   "UNWIND [2, 3] AS x MERGE (n:MergeComputed {value: x + 10}) RETURN n.value",
                   NULL,
                   0,
                   "unwind.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("12\n13\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(
                   g,
                   "UNWIND [1, 2] AS x MERGE (a:MergeComputed {value: x})-[r:MERGE_LINK {weight: x "
                   "+ 1}]->(b:MergeComputed {value: x * 2}) RETURN a.value, r.weight, b.value",
                   NULL,
                   0,
                   "unwind.out",
                   &mutated) == NG_OK &&
               mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("1\t2\t2\n2\t3\t4\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_tmp(g, "CREATE (source:MergeSource {score: 40})", &mutated) == NG_OK);
        param.name = "offset";
        param.value.type = NG_VALUE_INT64;
        param.value.length = 0;
        param.value.as.integer = 5;
        assert(
            query_params_file(g,
                              "MATCH (source:MergeSource) UNWIND [1, 2] AS x MERGE (n:MergeParam "
                              "{value: source.score + x + $offset}) RETURN n.value",
                              &param,
                              1,
                              "unwind.out",
                              &mutated) == NG_OK &&
            mutated);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("46\n47\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        memset(items, 0, sizeof(items));
        items[0].type = NG_VALUE_INT64;
        items[0].as.integer = 4;
        items[1].type = NG_VALUE_INT64;
        items[1].as.integer = 5;
        items[2].type = NG_VALUE_INT64;
        items[2].as.integer = 6;
        list.count = 3;
        list.items = items;
        param.name = "items";
        param.value.type = NG_VALUE_LIST;
        param.value.length = 3;
        param.value.as.list = &list;
        assert(query_params_file(
                   g, "UNWIND $items AS x RETURN x", &param, 1, "unwind.out", &mutated) == NG_OK);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("4\n5\n6\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_params_file(g,
                                 "UNWIND [1, 2] AS x MATCH (n:Value) RETURN x, n.value",
                                 NULL,
                                 0,
                                 "unwind.out",
                                 &mutated) == NG_OK);
        f = fopen("unwind.expected", "wb");
        assert(f);
        fputs("1\t1\n1\t2\n2\t1\n2\t2\n", f);
        assert(fclose(f) == 0);
        assert(same_file("unwind.out", "unwind.expected"));
        assert(query_tmp(g, "UNWIND [1,] AS x RETURN x", &mutated) == NG_PARSE_ERROR);
        assert(query_tmp(g, "UNWIND missing AS x RETURN x", &mutated) == NG_PARSE_ERROR);
        assert(query_tmp(g, "UNWIND $missing AS x RETURN x", &mutated) == NG_NOT_FOUND);
        before = ng_node_count(g);
        mutated = 99;
        assert(query_tmp(g,
                         "UNWIND [7, 8] AS x CREATE (n:Rollback {value: x + 1}) SET missing.value "
                         "= 1 RETURN n",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(g) == before && ng_validate(g) == NG_OK);
        before = ng_node_count(g);
        mutated = 99;
        assert(query_tmp(g,
                         "UNWIND [7, 8] AS x MERGE (n:RollbackMerge {value: x + 1}) SET "
                         "missing.value = 1 RETURN n",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(g) == before && ng_validate(g) == NG_OK);
        param.name = "offset";
        param.value.type = NG_VALUE_INT64;
        param.value.length = 0;
        param.value.as.integer = 5;
        before = ng_node_count(g);
        mutated = 99;
        assert(query_tmp_params(g,
                                "UNWIND [9] AS x MERGE (n:RollbackParam {value: x + $offset}) SET "
                                "missing.value = 1 RETURN n",
                                &param,
                                1,
                                &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(g) == before && ng_validate(g) == NG_OK);
        ng_close(g);
        remove("unwind.ng");
        remove("unwind.out");
        remove("unwind.expected");
    }
    {
        ng_graph* map_graph = NULL;
        ng_graph* reopened = NULL;
        FILE* map_output;
        int mutated = 0;
        size_t before;

        assert(ng_create(&map_graph, "nested-map.ng") == NG_OK);
        assert(query_tmp(map_graph,
                         "CREATE (n:Map {profile: {name: \"Joe\", metrics: {score: 7}}}) "
                         "RETURN n.profile",
                         &mutated) == NG_OK);
        assert(mutated && ng_node_count(map_graph) == 1);
        map_output = fopen("nested-map.out", "wb");
        assert(map_output);
        assert(ng_query_execute(
                   map_graph,
                   "MATCH (n:Map) WITH n.profile AS profile RETURN profile, profile.metrics",
                   map_output,
                   &mutated) == NG_OK);
        assert(fclose(map_output) == 0);
        map_output = fopen("nested-map.out", "rb");
        assert(map_output);
        {
            char output[256] = {0};
            assert(fread(output, 1, sizeof(output) - 1, map_output) > 0);
            assert(!strcmp(output, "{name: Joe, metrics: {score: 7}}\t{score: 7}\n"));
        }
        assert(fclose(map_output) == 0);

        assert(query_tmp(map_graph,
                         "MATCH (n:Map) SET n += {profile: {name: \"Berta\", active: true}} "
                         "WITH n.profile AS profile RETURN profile.name",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "UNWIND [1, 2] AS x CREATE (n:RowMap {payload: {value: x, total: x + x}}) "
                         "RETURN n.payload",
                         &mutated) == NG_OK);
        assert(
            query_tmp(map_graph,
                      "UNWIND [3] AS x MERGE (n:MergeMap {payload: {value: x}}) RETURN n.payload",
                      &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "WITH {outer: {value: 3}} AS data WITH data.outer AS inner "
                         "RETURN inner.value",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:Map) RETURN n.profile AS value UNION MATCH (n:MergeMap) "
                         "RETURN n.payload AS value",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:Map) RETURN n.profile UNION ALL MATCH (n:Map) "
                         "RETURN n.profile",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:Map) RETURN n.profile UNION DISTINCT MATCH (n:Map) "
                         "RETURN n.profile",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:MissingLabel) RETURN n.name UNION MATCH (n:Map) "
                         "RETURN n.name",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:Map) RETURN n.name AS left UNION MATCH (n:Map) "
                         "RETURN n.name AS right",
                         &mutated) == NG_PARSE_ERROR);
        assert(query_tmp(map_graph,
                         "UNWIND [1] AS value RETURN value UNION UNWIND [\"one\"] AS value "
                         "RETURN value",
                         &mutated) == NG_PARSE_ERROR);
        assert(query_tmp(map_graph,
                         "UNWIND [1] AS value RETURN value AS value UNION UNWIND [1.5] AS value "
                         "RETURN value AS value",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "UNWIND [null] AS value RETURN value AS value UNION UNWIND [\"one\"] "
                         "AS value RETURN value AS value",
                         &mutated) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:MissingOne) RETURN n.name AS value UNION MATCH (n:MissingTwo) "
                         "RETURN n.name AS value",
                         &mutated) == NG_OK);
        before = ng_node_count(map_graph);
        assert(query_tmp(map_graph,
                         "CREATE (emptyOne:UnionWrite) UNION CREATE (emptyTwo:UnionWrite)",
                         &mutated) == NG_OK);
        assert(mutated && ng_node_count(map_graph) == before + 2);
        before = ng_node_count(map_graph);
        mutated = 99;
        assert(query_tmp(map_graph,
                         "CREATE (emptyRollback:UnionWrite) UNION MATCH (n:Map) RETURN "
                         "n.name AS value",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(map_graph) == before && ng_validate(map_graph) == NG_OK);
        assert(query_tmp(map_graph,
                         "MATCH (n:Map) RETURN n.profile UNION MATCH (n:Map) "
                         "RETURN n.profile, n.payload",
                         &mutated) == NG_PARSE_ERROR);

        before = ng_node_count(map_graph);
        assert(query_tmp(map_graph,
                         "CREATE (one:UnionWrite {name: \"one\"}) RETURN one.name AS name UNION "
                         "CREATE (two:UnionWrite {name: \"two\"}) RETURN two.name AS name",
                         &mutated) == NG_OK);
        assert(mutated && ng_node_count(map_graph) == before + 2);
        before = ng_node_count(map_graph);
        mutated = 99;
        assert(query_tmp(map_graph,
                         "CREATE (firstSchema:UnionWrite {name: \"first\"}) WITH "
                         "firstSchema.name AS left RETURN left UNION CREATE "
                         "(secondSchema:UnionWrite {name: \"second\"}) WITH "
                         "secondSchema.name AS right RETURN right",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(map_graph) == before && ng_validate(map_graph) == NG_OK);
        before = ng_node_count(map_graph);
        mutated = 99;
        assert(query_tmp(map_graph,
                         "CREATE (rollback:UnionWrite {name: \"rollback\"}) RETURN "
                         "rollback.name UNION MATCH (n:Map) SET missing.value = 1 RETURN n",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(map_graph) == before && ng_validate(map_graph) == NG_OK);

        before = ng_node_count(map_graph);
        mutated = 99;
        assert(query_tmp(map_graph,
                         "UNWIND [8, 9] AS x CREATE (n:RollbackMap {payload: {value: x}}) "
                         "SET missing.value = 1 RETURN n",
                         &mutated) == NG_PARSE_ERROR);
        assert(!mutated && ng_node_count(map_graph) == before && ng_validate(map_graph) == NG_OK);

        assert(ng_save(map_graph) == NG_OK);
        assert(ng_open(&reopened, "nested-map.ng") == NG_OK);
        map_output = fopen("nested-map.out", "wb");
        assert(map_output);
        assert(ng_query_execute(reopened,
                                "MATCH (n:Map) WITH n.profile AS profile RETURN profile",
                                map_output,
                                &mutated) == NG_OK);
        assert(fclose(map_output) == 0);
        map_output = fopen("nested-map.out", "rb");
        assert(map_output);
        {
            char output[32] = {0};
            assert(fread(output, 1, sizeof(output) - 1, map_output) > 0);
            assert(!strcmp(output, "{name: Berta, active: true}\n"));
        }
        assert(fclose(map_output) == 0);
        ng_close(reopened);
        ng_close(map_graph);
        remove("nested-map.ng");
        remove("nested-map.out");
    }
    remove_import_files();
    remove("test.ng");
    puts("ok");
    return 0;
}
