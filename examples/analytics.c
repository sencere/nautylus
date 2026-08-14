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

static ng_value double_value(double number) {
    ng_value value;
    memset(&value, 0, sizeof(value));
    value.type = NG_VALUE_DOUBLE;
    value.as.real = number;
    return value;
}

static double zero_heuristic(ng_node_id node, ng_node_id target, void* context) {
    (void)node;
    (void)target;
    (void)context;
    return 0.0;
}

static int print_path(const ng_node_id* path, size_t length, void* context) {
    size_t i;
    (void)context;
    printf("path:");
    for (i = 0; i < length; i++)
        printf(" %llu", (unsigned long long)path[i]);
    printf("\n");
    return 1;
}

int main(void) {
    ng_graph* g = NULL;
    ng_status status = NG_OK;
    ng_symbol_id place = 0, task = 0, route = 0, depends = 0, weight = 0, capacity = 0;
    ng_node_id a = 0, b = 0, c = 0, d = 0, e = 0, t1 = 0, t2 = 0, t3 = 0;
    ng_relationship_id r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0;
    ng_node_score scores[16];
    ng_node_metric metrics[16];
    ng_node_component components[16];
    ng_link_score links[8];
    ng_node_id path[8], articulation[8], topo[8], walk[8];
    ng_relationship_id bridges[8], mst[8];
    size_t count = 0;
    double distance = 0.0, flow = 0.0, total_weight = 0.0, score = 0.0;
    uint64_t metric = 0;
    ng_random_walk_options walk_options;
    ng_value value;

    NG_CHECK(ng_create(&g, "analytics-example.ng"));
    NG_CHECK(ng_symbol(g, "Place", &place));
    NG_CHECK(ng_symbol(g, "Task", &task));
    NG_CHECK(ng_symbol(g, "ROUTE", &route));
    NG_CHECK(ng_symbol(g, "DEPENDS_ON", &depends));
    NG_CHECK(ng_symbol(g, "weight", &weight));
    NG_CHECK(ng_symbol(g, "capacity", &capacity));

    NG_CHECK(ng_node_create(g, &place, 1, &a));
    NG_CHECK(ng_node_create(g, &place, 1, &b));
    NG_CHECK(ng_node_create(g, &place, 1, &c));
    NG_CHECK(ng_node_create(g, &place, 1, &d));
    NG_CHECK(ng_node_create(g, &place, 1, &e));
    NG_CHECK(ng_node_create(g, &task, 1, &t1));
    NG_CHECK(ng_node_create(g, &task, 1, &t2));
    NG_CHECK(ng_node_create(g, &task, 1, &t3));

    NG_CHECK(ng_relationship_create(g, a, route, b, &r1));
    NG_CHECK(ng_relationship_create(g, b, route, c, &r2));
    NG_CHECK(ng_relationship_create(g, a, route, d, &r3));
    NG_CHECK(ng_relationship_create(g, d, route, c, &r4));
    NG_CHECK(ng_relationship_create(g, c, route, e, &r5));
    value = double_value(1.0);
    NG_CHECK(ng_relationship_set(g, r1, weight, &value));
    value = double_value(2.0);
    NG_CHECK(ng_relationship_set(g, r2, weight, &value));
    value = double_value(3.0);
    NG_CHECK(ng_relationship_set(g, r3, weight, &value));
    value = double_value(1.0);
    NG_CHECK(ng_relationship_set(g, r4, weight, &value));
    value = double_value(1.0);
    NG_CHECK(ng_relationship_set(g, r5, weight, &value));
    value = double_value(4.0);
    NG_CHECK(ng_relationship_set(g, r1, capacity, &value));
    value = double_value(2.0);
    NG_CHECK(ng_relationship_set(g, r2, capacity, &value));
    value = double_value(3.0);
    NG_CHECK(ng_relationship_set(g, r3, capacity, &value));
    value = double_value(3.0);
    NG_CHECK(ng_relationship_set(g, r4, capacity, &value));
    value = double_value(5.0);
    NG_CHECK(ng_relationship_set(g, r5, capacity, &value));
    NG_CHECK(ng_relationship_create(g, t1, depends, t2, &r1));
    NG_CHECK(ng_relationship_create(g, t2, depends, t3, &r1));

    NG_CHECK(ng_degree_centrality(g, NG_DIRECTION_EITHER, route, scores, 16, &count));
    printf("degree rows: %zu\n", count);
    NG_CHECK(ng_pagerank(g, route, 0.85, 20, scores, 16, &count));
    printf("pagerank rows: %zu\n", count);
    NG_CHECK(ng_eigenvector_centrality(g, NG_DIRECTION_EITHER, route, 20, scores, 16, &count));
    NG_CHECK(ng_closeness_centrality(g, NG_DIRECTION_EITHER, route, scores, 16, &count));
    NG_CHECK(ng_harmonic_centrality(g, NG_DIRECTION_EITHER, route, scores, 16, &count));

    NG_CHECK(ng_bfs_path(g, a, e, NG_DIRECTION_OUTGOING, route, path, 8, &count));
    printf("bfs path length: %zu\n", count);
    NG_CHECK(ng_dijkstra(g, a, e, NG_DIRECTION_OUTGOING, route, weight, path, 8, &count,
                         &distance));
    printf("dijkstra distance: %.2f\n", distance);
    NG_CHECK(ng_a_star(g, a, e, NG_DIRECTION_OUTGOING, route, weight, zero_heuristic, NULL, path,
                       8, &count, &distance));
    NG_CHECK(ng_enumerate_paths(g, a, e, NG_DIRECTION_OUTGOING, route, 4, 8, print_path, NULL,
                                &count));

    walk_options.direction = NG_DIRECTION_OUTGOING;
    walk_options.type = route;
    walk_options.max_steps = 5;
    walk_options.seed = 42;
    NG_CHECK(ng_random_walk(g, a, &walk_options, walk, 8, &count));
    printf("random walk length: %zu\n", count);

    NG_CHECK(ng_weakly_connected_components(g, route, components, 16, &count));
    NG_CHECK(ng_strongly_connected_components(g, route, components, 16, &count));
    NG_CHECK(ng_label_propagation(g, NG_DIRECTION_EITHER, route, 5, components, 16, &count));
    NG_CHECK(ng_louvain(g, route, 5, components, 16, &count));
    NG_CHECK(ng_triangle_count(g, route, metrics, 16, &count));
    NG_CHECK(ng_local_clustering_coefficient(g, route, scores, 16, &count));
    NG_CHECK(ng_articulation_points(g, route, articulation, 8, &count));
    NG_CHECK(ng_bridges(g, route, bridges, 8, &count));
    NG_CHECK(ng_topological_sort(g, depends, topo, 8, &count));
    NG_CHECK(ng_minimum_spanning_tree(g, route, weight, mst, 8, &count, &total_weight));
    NG_CHECK(ng_max_flow(g, a, e, route, capacity, &flow));

    NG_CHECK(ng_common_neighbors(g, a, c, route, &metric));
    NG_CHECK(ng_preferential_attachment(g, a, c, route, &metric));
    NG_CHECK(ng_total_neighbors(g, a, c, route, &metric));
    NG_CHECK(ng_adamic_adar(g, a, c, route, &score));
    NG_CHECK(ng_resource_allocation(g, a, c, route, &score));
    NG_CHECK(ng_knn(g, a, NG_DIRECTION_EITHER, route, 3, links, 8, &count));
    NG_CHECK(ng_knn_filtered(g, a, NG_DIRECTION_EITHER, route, place, 3, links, 8, &count));

    printf("mst edges: %zu, mst weight: %.2f, max flow: %.2f\n", count, total_weight, flow);
    NG_CHECK(ng_save(g));
    ng_close(g);
    return 0;

fail:
    ng_close(g);
    return 1;
}
