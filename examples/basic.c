#include "nautylus.h"

#include <stdio.h>
#include <string.h>

#define NG_CHECK(expr)                                                                             \
    do {                                                                                           \
        status = (expr);                                                                           \
        if (status != NG_OK) {                                                                     \
            fprintf(stderr, "%s\n", ng_status_name(status));                                       \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)

int main(void) {
    ng_graph* g = 0;
    ng_status status = NG_OK;
    ng_symbol_id person = 0;
    ng_symbol_id knows = 0;
    ng_symbol_id name = 0;
    ng_node_id alice = 0;
    ng_node_id bob = 0;
    ng_relationship_id relationship = 0;
    ng_value value;

    NG_CHECK(ng_create(&g, "example.ng"));
    NG_CHECK(ng_symbol(g, "Person", &person));
    NG_CHECK(ng_symbol(g, "KNOWS", &knows));
    NG_CHECK(ng_symbol(g, "name", &name));

    NG_CHECK(ng_node_create(g, &person, 1, &alice));
    NG_CHECK(ng_node_create(g, &person, 1, &bob));
    NG_CHECK(ng_relationship_create(g, alice, knows, bob, &relationship));

    memset(&value, 0, sizeof(value));
    value.type = NG_VALUE_STRING;
    value.length = 5;
    value.as.string = "Alice";
    NG_CHECK(ng_node_set(g, alice, name, &value));

    NG_CHECK(ng_save(g));
    ng_close(g);
    return 0;

fail:
    ng_close(g);
    return 1;
}
