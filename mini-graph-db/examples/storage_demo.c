#include "property_graph.h"
#include "graph_storage.h"
#include "graph_metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("=== Storage Engine Demo: Graph Persistence ===\n\n");

    /* Build a graph in memory */
    PropertyGraph *g = graph_create();
    printf("Creating 10 nodes and 12 edges...\n");

    for (int i = 1; i <= 10; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Person");
        char name[32];
        snprintf(name, sizeof(name), "User%d", i);
        graph_node_set_property(n, "name", name);
    }

    graph_create_edge(g, 1, 2, "KNOWS", true);
    graph_create_edge(g, 1, 3, "KNOWS", true);
    graph_create_edge(g, 1, 4, "KNOWS", true);
    graph_create_edge(g, 2, 5, "KNOWS", true);
    graph_create_edge(g, 3, 5, "KNOWS", true);
    graph_create_edge(g, 4, 6, "KNOWS", true);
    graph_create_edge(g, 5, 7, "FOLLOWS", true);
    graph_create_edge(g, 6, 7, "FOLLOWS", true);
    graph_create_edge(g, 7, 8, "LIKES", true);
    graph_create_edge(g, 8, 9, "LIKES", true);
    graph_create_edge(g, 9, 10, "LIKES", true);
    graph_create_edge(g, 10, 1, "LIKES", true);

    printf("  Nodes: %d  Edges: %d\n\n", graph_node_count(g), graph_edge_count(g));

    /* Compute metrics before storage */
    printf("--- Graph Metrics (before storage) ---\n");
    GraphStatistics stats;
    graph_statistics_compute(g, &stats);
    graph_statistics_print(&stats);

    /* Store to disk */
    printf("\n--- Persisting to disk ---\n");
    GraphStorage *gs = gs_create();
    if (!gs) { printf("Failed to create storage\n"); graph_destroy(g); return 1; }

    bool stored = gs_store_graph(gs, g);
    printf("  Store result: %s\n", stored ? "SUCCESS" : "FAILED");
    printf("  Pages used: %d nodes, %d edges\n",
           gs_node_count(gs), gs_edge_count(gs));

    bp_print_stats(gs->pool);
    wal_print_info(gs->wal);

    /* Load from disk into a fresh graph */
    printf("\n--- Loading from disk ---\n");
    PropertyGraph *g2 = graph_create();
    bool loaded = gs_load_graph(gs, g2);
    printf("  Load result: %s\n", loaded ? "SUCCESS" : "FAILED");
    printf("  Loaded nodes: %d, edges: %d\n",
           graph_node_count(g2), graph_edge_count(g2));

    /* Verify a few nodes */
    for (int i = 1; i <= 3 && i <= graph_node_count(g2); i++) {
        Node *n = graph_get_node(g2, (int64_t)i);
        if (n) {
            const char *name = graph_node_get_property(n, "name");
            printf("  Node %d: %s  (labels: %d, props: %d)\n",
                   i, name ? name : "?", n->label_count, n->property_count);
        }
    }

    /* Compute metrics on loaded graph to verify data integrity */
    printf("\n--- Graph Metrics (after load) ---\n");
    GraphStatistics stats2;
    graph_statistics_compute(g2, &stats2);
    graph_statistics_print(&stats2);

    printf("\n--- Verifying round-trip integrity ---\n");
    printf("  Node count:  before=%d  after=%d  %s\n",
           graph_node_count(g), graph_node_count(g2),
           graph_node_count(g) == graph_node_count(g2) ? "OK" : "MISMATCH");
    printf("  Edge count:  before=%d  after=%d  %s\n",
           graph_edge_count(g), graph_edge_count(g2),
           graph_edge_count(g) == graph_edge_count(g2) ? "OK" : "MISMATCH");
    printf("  Density:     before=%.6f  after=%.6f  %s\n",
           stats.graph_density, stats2.graph_density,
           fabs(stats.graph_density - stats2.graph_density) < 0.001 ? "OK" : "MISMATCH");

    graph_destroy(g);
    graph_destroy(g2);
    gs_destroy(gs);

    /* cleanup */
    remove(DATA_FILE);
    remove(WAL_FILE);

    printf("\n=== Storage Engine Demo Complete ===\n");
    return 0;
}
