#include "nautylus.h"

#include <stdio.h>
#include <string.h>

#define NG_CHECK(expr)                                                                             \
    do {                                                                                           \
        status = (expr);                                                                           \
        if (status != NG_OK) {                                                                     \
            fprintf(stderr, "%s: %s\n", #expr, ng_status_name(status));                           \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)

static ng_value string_value(const char* text) {
    ng_value value;
    memset(&value, 0, sizeof(value));
    value.type = NG_VALUE_STRING;
    value.as.string = text;
    value.length = strlen(text);
    return value;
}

static ng_value int_value(int64_t number) {
    ng_value value;
    memset(&value, 0, sizeof(value));
    value.type = NG_VALUE_INT64;
    value.as.integer = number;
    return value;
}

int main(void) {
    ng_graph* g = NULL;
    ng_status status = NG_OK;
    int mutated = 0;
    ng_parameter parameters[2];

    NG_CHECK(ng_create(&g, "cypher-example.ng"));

    NG_CHECK(ng_query_execute(
        g,
        "CREATE (joe:Person {name: \"Joe\", age: 34, city: \"Berlin\"}),"
        "       (bob:Person {name: \"Bob\", age: 31, city: \"Berlin\"}),"
        "       (joe)-[:KNOWS {since: 2020}]->(bob)"
        "RETURN joe, bob",
        stdout, &mutated));

    parameters[0].name = "name";
    parameters[0].value = string_value("Ada");
    parameters[1].name = "age";
    parameters[1].value = int_value(37);
    NG_CHECK(ng_query_execute_params(
        g,
        "CREATE (p:Person {name: $name, age: $age, city: \"London\"}) RETURN p.name, p.age",
        parameters, 2, stdout, &mutated));

    NG_CHECK(ng_query_execute(
        g,
        "UNWIND [\"Cypher\", \"C API\", \"Analytics\"] AS topic "
        "CREATE (t:Topic {name: topic}) "
        "RETURN t.name "
        "ORDER BY t.name",
        stdout, &mutated));

    NG_CHECK(ng_query_print(
        g,
        "MATCH (a:Person) "
        "OPTIONAL MATCH (a)-[:KNOWS]->(b:Person) "
        "WITH a.name AS person, b.name AS knows "
        "RETURN person, knows "
        "ORDER BY person ASC, knows DESC",
        stdout));

    NG_CHECK(ng_query_print(
        g,
        "MATCH (a:Person) "
        "RETURN a.city, count(a) AS people, collect(a.name) AS names "
        "ORDER BY people DESC, a.city ASC",
        stdout));

    NG_CHECK(ng_query_execute(
        g,
        "MATCH (p:Person) WHERE p.name = \"Joe\" "
        "SET p += {score: 10, active: true} "
        "RETURN p.name, p.score, p.active",
        stdout, &mutated));

    NG_CHECK(ng_save(g));
    ng_close(g);
    return 0;

fail:
    ng_close(g);
    return 1;
}
