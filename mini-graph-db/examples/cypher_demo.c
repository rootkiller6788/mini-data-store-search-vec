#include "property_graph.h"
#include "cypher_like.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== Cypher-like Query Demo ===\n\n");

    PropertyGraph *g = graph_create();

    for (int i = 1; i <= 10; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Person");
        char name[32];
        snprintf(name, sizeof(name), "Person%d", i);
        graph_node_set_property(n, "name", name);
        if (i <= 4) graph_node_add_label(n, "Admin");
        if (i >= 7) graph_node_add_label(n, "Member");
    }

    graph_create_edge(g, 1, 2, "KNOWS", true);
    graph_create_edge(g, 1, 3, "KNOWS", true);
    graph_create_edge(g, 2, 4, "KNOWS", true);
    graph_create_edge(g, 3, 5, "KNOWS", true);
    graph_create_edge(g, 4, 6, "KNOWS", true);
    graph_create_edge(g, 5, 7, "FOLLOWS", true);
    graph_create_edge(g, 6, 8, "KNOWS", true);
    graph_create_edge(g, 7, 9, "FOLLOWS", true);
    graph_create_edge(g, 8, 10, "KNOWS", true);
    graph_create_edge(g, 9, 1, "KNOWS", true);
    graph_create_edge(g, 1, 5, "KNOWS", true);
    graph_create_edge(g, 2, 6, "KNOWS", true);
    graph_create_edge(g, 3, 7, "FOLLOWS", true);

    printf("Graph: %d nodes, %d edges\n\n",
           graph_node_count(g), graph_edge_count(g));

    printf("--- Parse and Match: (a:Person)-[:KNOWS]->(b) ---\n");
    QueryPattern pattern;
    if (cypher_parse("(a:Person)-[:KNOWS]->(b)", &pattern)) {
        cypher_pattern_print(&pattern);
        QueryResult result = cypher_match(g, &pattern);
        cypher_print_results(&result);
    } else {
        printf("Parse failed\n");
    }

    printf("\n--- Parse and Match: (a:Person)-[:KNOWS]->(b:Admin) ---\n");
    QueryPattern pattern2;
    if (cypher_parse("(a:Person)-[:KNOWS]->(b:Admin)", &pattern2)) {
        cypher_pattern_print(&pattern2);
        QueryResult result2 = cypher_match(g, &pattern2);
        cypher_print_results(&result2);
    } else {
        printf("Parse failed\n");
    }

    printf("\n--- Parse and Match: (a:Person)-[:FOLLOWS]->(b) ---\n");
    QueryPattern pattern3;
    if (cypher_parse("(a:Person)-[:FOLLOWS]->(b)", &pattern3)) {
        cypher_pattern_print(&pattern3);
        QueryResult result3 = cypher_match(g, &pattern3);
        cypher_print_results(&result3);
    } else {
        printf("Parse failed\n");
    }

    printf("\n--- All people (single node match) ---\n");
    QueryPattern pattern4;
    if (cypher_parse("(a:Person)", &pattern4)) {
        cypher_pattern_print(&pattern4);
        QueryResult result4 = cypher_match(g, &pattern4);
        cypher_print_results(&result4);
    }

    graph_destroy(g);
    return 0;
}
