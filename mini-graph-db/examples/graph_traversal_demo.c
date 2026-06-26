#include "property_graph.h"
#include "graph_traversal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double friendship_weight(Edge *e) {
    const char *w = NULL;
    for (int i = 0; i < e->property_count; i++) {
        if (strcmp(e->properties[i].key, "closeness") == 0)
            w = e->properties[i].value;
    }
    return w ? atof(w) : 1.0;
}

int main(void) {
    printf("=== Graph Traversal Demo: Social Network ===\n\n");

    PropertyGraph *g = graph_create();

    char *names[] = {
        "Alice", "Bob", "Carol", "Dave", "Eve",
        "Frank", "Grace", "Heidi", "Ivan", "Judy"
    };

    for (int i = 0; i < 10; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Person");
        graph_node_set_property(n, "name", names[i]);
    }

    Node *alice = graph_get_node(g, 1);
    Node *bob   = graph_get_node(g, 2);

    int friendships[15][3] = {
        {1, 2, 3}, {1, 3, 5}, {1, 4, 2}, {1, 5, 1},
        {2, 3, 4}, {2, 6, 6}, {3, 6, 3}, {3, 7, 7},
        {4, 7, 2}, {5, 8, 8}, {6, 8, 1}, {6, 9, 5},
        {7, 9, 4}, {8, 10, 2}, {9, 10, 3}
    };

    for (int i = 0; i < 15; i++) {
        char wbuf[16];
        snprintf(wbuf, sizeof(wbuf), "%.1f", (double)friendships[i][2]);
        Edge *e = graph_create_edge(g, friendships[i][0],
                                    friendships[i][1], "KNOWS", false);
        graph_edge_set_property(e, "closeness", wbuf);
    }

    printf("Graph: %d nodes, %d edges\n\n",
           graph_node_count(g), graph_edge_count(g));

    printf("--- BFS from Alice to all ---\n");
    PathResult bfs = traverse_bfs(g, 1, -1);
    printf("BFS visit order (%d nodes): ", bfs.length);
    for (int i = 0; i < bfs.length; i++) {
        Node *n = graph_get_node(g, bfs.node_ids[i]);
        printf("%s ", n ? graph_node_get_property(n, "name") : "?");
    }
    printf("\n\n");

    printf("--- DFS from Alice to all ---\n");
    PathResult dfs = traverse_dfs(g, 1, -1);
    printf("DFS visit order (%d nodes): ", dfs.length);
    for (int i = 0; i < dfs.length; i++) {
        Node *n = graph_get_node(g, dfs.node_ids[i]);
        printf("%s ", n ? graph_node_get_property(n, "name") : "?");
    }
    printf("\n\n");

    printf("--- Shortest path: Alice -> Bob ---\n");
    PathResult sp = traverse_shortest_path(g, 1, 2);
    printf("Shortest path: ");
    for (int i = 0; i < sp.length; i++) {
        Node *n = graph_get_node(g, sp.node_ids[i]);
        printf("%s", n ? graph_node_get_property(n, "name") : "?");
        if (i < sp.length - 1) printf(" -> ");
    }
    printf("  (length=%d)\n\n", sp.length);

    printf("--- Dijkstra: Alice -> Bob (weighted by closeness) ---\n");
    PathResult dij = traverse_dijkstra(g, 1, 2, friendship_weight);
    printf("Dijkstra path: ");
    for (int i = 0; i < dij.length; i++) {
        Node *n = graph_get_node(g, dij.node_ids[i]);
        printf("%s", n ? graph_node_get_property(n, "name") : "?");
        if (i < dij.length - 1) printf(" -> ");
    }
    printf("  (total_weight=%.2f)\n\n", dij.total_weight);

    printf("--- All paths: Alice -> Bob (max depth 4) ---\n");
    PathResult all = traverse_all_paths(g, 1, 2, 4);
    path_result_print(&all);

    graph_destroy(g);
    return 0;
}
