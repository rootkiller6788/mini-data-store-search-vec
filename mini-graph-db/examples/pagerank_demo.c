#include "property_graph.h"
#include "graph_algo.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== PageRank Demo: Web Graph ===\n\n");

    PropertyGraph *g = graph_create();

    for (int i = 1; i <= 6; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Page");
        char name[16];
        snprintf(name, sizeof(name), "Page%d", i);
        graph_node_set_property(n, "title", name);
    }

    int links[][2] = {
        {1, 2}, {1, 3}, {2, 3}, {2, 4},
        {3, 5}, {4, 5}, {4, 6}, {5, 6},
        {6, 1}, {5, 1}, {3, 6}
    };
    int link_count = sizeof(links) / sizeof(links[0]);

    for (int i = 0; i < link_count; i++) {
        graph_create_edge(g, links[i][0], links[i][1], "LINKS_TO", true);
    }

    printf("Web graph: %d pages, %d links\n\n",
           graph_node_count(g), graph_edge_count(g));

    printf("--- Out-degree of each page ---\n");
    for (int i = 1; i <= 6; i++) {
        int od = node_out_degree(g, i);
        int id = node_in_degree(g, i);
        printf("  Page%d: out=%d, in=%d\n", i, od, id);
    }
    printf("\n");

    pagerank_print_top(g, 6);
    printf("\n");

    printf("--- Connected Components ---\n");
    connected_components_print(g);
    printf("\n");

    printf("--- Cycle Detection ---\n");
    cycle_detection_print(g);
    printf("\n");

    graph_destroy(g);
    return 0;
}
