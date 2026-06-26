#include "property_graph.h"
#include "graph_traversal.h"
#include "graph_algo.h"
#include "cypher_like.h"
#include "graph_index.h"
#include "graph_storage.h"
#include "graph_metrics.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %-55s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* =========================================================================
 * L1 Tests: Core Definitions (struct/typedef/API)
 * ========================================================================= */
static void test_l1_core_definitions(void) {
    printf("\n[L1] Core Definitions\n");

    /* Property struct */
    TEST("Property struct size");
    Property p;
    memset(&p, 0, sizeof(p));
    CHECK(sizeof(p) > 0, "zero size");

    TEST("Node struct fields accessible");
    Node n;
    memset(&n, 0, sizeof(n));
    n.id = 42;
    n.label_count = 2;
    CHECK(n.id == 42 && n.label_count == 2, "Node field access");

    TEST("Edge struct initialization");
    Edge e;
    memset(&e, 0, sizeof(e));
    e.directed = true;
    CHECK(e.directed, "Edge directed flag");

    TEST("PropertyGraph creation");
    PropertyGraph *g = graph_create();
    CHECK(g != NULL, "graph_create returned NULL");
    graph_destroy(g);

    TEST("MAX constants defined");
    CHECK(MAX_NODES > 0 && MAX_EDGES > 0 && MAX_PATH_LENGTH > 0,
          "MAX constants");
}

/* =========================================================================
 * L2 Tests: Core Concepts — Graph operations
 * ========================================================================= */
static void test_l2_graph_operations(void) {
    printf("\n[L2] Graph Operations\n");

    PropertyGraph *g = graph_create();
    CHECK(g != NULL, "graph_create");

    /* Create nodes */
    Node *n1 = graph_create_node(g);
    Node *n2 = graph_create_node(g);
    Node *n3 = graph_create_node(g);
    CHECK(n1 && n2 && n3, "create 3 nodes");
    CHECK(graph_node_count(g) == 3, "node count == 3");

    /* Node labels */
    CHECK(graph_node_add_label(n1, "Person"), "add label Person");
    CHECK(graph_node_add_label(n1, "Admin"), "add label Admin");
    CHECK(n1->label_count == 2, "2 labels");

    /* Node properties */
    CHECK(graph_node_set_property(n1, "name", "Alice"), "set name");
    CHECK(graph_node_set_property(n1, "age", "30"), "set age");
    CHECK(n1->property_count == 2, "2 properties");

    const char *name = graph_node_get_property(n1, "name");
    CHECK(name && strcmp(name, "Alice") == 0, "get name");

    const char *missing = graph_node_get_property(n1, "missing");
    CHECK(missing == NULL, "get missing property returns NULL");

    /* Create edge */
    Edge *e1 = graph_create_edge(g, n1->id, n2->id, "KNOWS", true);
    CHECK(e1 != NULL, "create directed edge");
    CHECK(graph_edge_count(g) == 1, "edge count == 1");

    /* Edge properties */
    CHECK(graph_edge_set_property(e1, "since", "2020"), "edge property");
    CHECK(e1->property_count == 1, "edge has 1 property");

    /* Get edge by id */
    Edge *lookup = graph_get_edge(g, e1->id);
    CHECK(lookup == e1, "graph_get_edge");

    /* Neighbors */
    int64_t neighbors[10];
    int ncount = graph_get_neighbors(g, n1->id, neighbors, 10);
    CHECK(ncount == 1 && neighbors[0] == n2->id, "get_neighbors");

    /* Out edges */
    int64_t out_edges[10];
    int out_count = graph_get_out_edges(g, n1->id, out_edges, 10);
    CHECK(out_count == 1, "get_out_edges");

    /* In edges */
    int64_t in_edges[10];
    int in_count = graph_get_in_edges(g, n2->id, in_edges, 10);
    CHECK(in_count == 1, "get_in_edges");

    /* Edge type filtering */
    int64_t typed[10];
    int tcount = graph_get_edges_by_type(g, "KNOWS", typed, 10);
    CHECK(tcount == 1 && typed[0] == e1->id, "edges_by_type");

    graph_destroy(g);
}

/* =========================================================================
 * L3 Tests: Engineering Structures — Adjacency List, Hash Table, Index
 * ========================================================================= */
static void test_l3_index_structures(void) {
    printf("\n[L3] Index Structures\n");

    PropertyGraph *g = graph_create();
    for (int i = 0; i < 5; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Person");
    }
    Node *n = graph_get_node(g, 1);
    graph_node_add_label(n, "Admin");
    graph_node_set_property(n, "dept", "Engineering");

    graph_create_edge(g, 1, 2, "KNOWS", true);
    graph_create_edge(g, 2, 3, "KNOWS", true);
    graph_create_edge(g, 3, 4, "FOLLOWS", true);
    graph_create_edge(g, 4, 5, "FOLLOWS", true);

    /* Label index */
    NodeIndex *ni = index_create_node(g);
    NodeIdList persons = index_lookup_by_label(ni, "Person");
    CHECK(persons.count >= 4, "label index: Person count");
    NodeIdList admins = index_lookup_by_label(ni, "Admin");
    CHECK(admins.count >= 1, "label index: Admin found");

    /* Edge type index */
    EdgeIndex *ei = index_create_edge(g);
    EdgeIdList knows = index_lookup_by_type(ei, "KNOWS");
    CHECK(knows.count == 2, "edge index: KNOWS count == 2");
    EdgeIdList follows = index_lookup_by_type(ei, "FOLLOWS");
    CHECK(follows.count == 2, "edge index: FOLLOWS count == 2");

    /* Property index */
    PropertyIndex *pi = index_create_property(g, "dept");
    NodeIdList eng = index_lookup_by_property(pi, "dept", "Engineering");
    CHECK(eng.count == 1 && eng.node_ids[0] == 1, "property index: dept");

    /* Spatial index */
    BoundingBox bb = {0, 0, 100, 100};
    SpatialIndex *si = index_create_spatial(&bb);
    CHECK(si != NULL, "spatial index create");
    CHECK(index_spatial_insert(si, 1, 50, 50), "spatial insert");
    CHECK(index_spatial_insert(si, 2, 10, 10), "spatial insert 2");
    NodeIdList range = index_spatial_range(si, 0, 0, 55, 55);
    CHECK(range.count >= 1, "spatial range query");

    index_destroy_node(ni);
    index_destroy_edge(ei);
    index_destroy_property(pi);
    index_destroy_spatial(si);
    graph_destroy(g);
}

/* =========================================================================
 * L4 Tests: Standards/Theorems — PageRank convergence, WAL, checksum
 * ========================================================================= */
static void test_l4_standards_theorems(void) {
    printf("\n[L4] Standards/Theorems\n");

    /* CRC32 checksum determinism */
    uint8_t test_data[] = "Hello, World! Graph DB Storage";
    uint32_t crc1 = crc32_compute(test_data, sizeof(test_data) - 1);
    uint32_t crc2 = crc32_compute(test_data, sizeof(test_data) - 1);
    CHECK(crc1 == crc2, "CRC32 deterministic");
    CHECK(crc1 != 0, "CRC32 non-zero");

    /* Page checksum integrity */
    DataPage page;
    page_init(&page, 1, PAGE_TYPE_NODE);
    page_calc_checksum(&page);
    CHECK(page_verify(&page), "page checksum verified");
    /* corrupt and verify detection */
    page.header.page_type = PAGE_TYPE_EDGE;
    page_calc_checksum(&page);
    CHECK(page_verify(&page), "modified page re-checksum ok");
    page.data[0] ^= 0xFF; /* flip bits */
    CHECK(!page_verify(&page), "corrupted page detected");

    /* PageRank damping factor theorem:
     * For damping d=0.85, all scores should be positive and sum to ~1.0 */
    PropertyGraph *g = graph_create();
    for (int i = 0; i < 10; i++) graph_create_node(g);
    /* create a simple cycle so PageRank converges */
    for (int i = 1; i <= 10; i++) {
        int64_t to = (i == 10) ? 1 : i + 1;
        graph_create_edge(g, i, to, "LINK", true);
    }
    RankedNode results[10];
    int count = pagerank(g, results, 10, 0.85, 100, 1e-6);
    double sum = 0.0;
    for (int i = 0; i < count; i++) sum += results[i].score;
    CHECK(count == 10, "PageRank: all nodes scored");
    CHECK(fabs(sum - 1.0) < 0.01, "PageRank: sum ~1.0 (stationary)");

    graph_destroy(g);
}

/* =========================================================================
 * L5 Tests: Algorithms — All graph algorithms
 * ========================================================================= */
static void test_l5_algorithms(void) {
    printf("\n[L5] Algorithms\n");

    PropertyGraph *g = graph_create();
    for (int i = 0; i < 8; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "V");
    }

    /* Build a connected graph with a DAG structure */
    graph_create_edge(g, 1, 2, "E", true);
    graph_create_edge(g, 1, 3, "E", true);
    graph_create_edge(g, 2, 4, "E", true);
    graph_create_edge(g, 3, 4, "E", true);
    graph_create_edge(g, 4, 5, "E", true);
    graph_create_edge(g, 5, 6, "E", true);
    graph_create_edge(g, 6, 7, "E", true);
    graph_create_edge(g, 7, 8, "E", true);
    graph_create_edge(g, 8, 1, "E", true); /* creates cycle */

    /* BFS */
    PathResult bfs = traverse_bfs(g, 1, 4);
    CHECK(bfs.found && bfs.length >= 2, "BFS found path");

    /* DFS */
    PathResult dfs = traverse_dfs(g, 1, 4);
    CHECK(dfs.found, "DFS found path");

    /* Shortest path */
    PathResult sp = traverse_shortest_path(g, 1, 4);
    CHECK(sp.found && sp.length == 3, "shortest path 1->4");

    /* Dijkstra */
    PathResult dij = traverse_dijkstra(g, 1, 4, NULL);
    CHECK(dij.found && dij.total_weight >= 2.0, "Dijkstra");

    /* Topological sort (should fail due to cycle) */
    int64_t sorted[20];
    int sorted_count = 0;
    bool topo_ok = topological_sort(g, sorted, &sorted_count);
    CHECK(!topo_ok, "topological sort: detected cycle");

    /* Break the cycle and retry */
    /* Check cycle detection */
    int64_t cycle[MAX_PATH_LENGTH];
    int cycle_len = 0;
    bool has_cycle = cycle_detection(g, cycle, &cycle_len);
    CHECK(has_cycle, "cycle detection: found cycle");

    /* Connected components */
    int64_t comp[20];
    int comp_count = 0;
    connected_components(g, comp, &comp_count);
    CHECK(comp_count == 1, "connected components: 1 component");

    /* Label propagation */
    LabelAssignment labels[10];
    int lp_count = label_propagation(g, labels, 8, 50);
    CHECK(lp_count == 8, "label propagation: all nodes assigned");

    /* Kruskal MST */
    MstEdge mst[20];
    int mst_count = mst_kruskal(g, NULL, mst, 20);
    CHECK(mst_count == 7, "Kruskal MST: 7 edges for 8 nodes");
    double mst_weight = mst_kruskal_total_weight(mst, mst_count);
    CHECK(mst_weight == 7.0, "MST total weight == 7");

    /* Prim MST */
    MstEdge mst2[20];
    int mst2_count = mst_prim(g, NULL, mst2, 20);
    CHECK(mst2_count == 7, "Prim MST: 7 edges");

    /* A* search */
    int64_t astar_path[20];
    int astar_len = astar_search(g, 1, 8, NULL, NULL, astar_path, 20);
    CHECK(astar_len > 0, "A* search found path");

    /* Floyd-Warshall */
    double *fw_dist = calloc(64, sizeof(double));
    int fw_n = floyd_warshall(g, fw_dist, 64);
    CHECK(fw_n == 8, "Floyd-Warshall: all 8 nodes");
    CHECK(fw_dist[0 * 8 + 7] < INFINITY / 2, "FW: path from 1 to 8");
    free(fw_dist);

    /* Graph coloring */
    int colors[20];
    int chromatic = graph_coloring_greedy(g, colors, 20);
    CHECK(chromatic >= 1, "graph coloring: at least 1 color needed");

    graph_destroy(g);

    /* SCC test on separate graph */
    PropertyGraph *g2 = graph_create();
    for (int i = 0; i < 4; i++) graph_create_node(g2);
    graph_create_edge(g2, 1, 2, "E", true);
    graph_create_edge(g2, 2, 3, "E", true);
    graph_create_edge(g2, 3, 1, "E", true); /* SCC: {1,2,3} */
    graph_create_edge(g2, 3, 4, "E", true);
    int64_t scc_comp[10];
    int scc_cnt = 0;
    scc_kosaraju(g2, scc_comp, &scc_cnt);
    CHECK(scc_cnt >= 2, "Kosaraju SCC: at least 2 components");
    graph_destroy(g2);
}

/* =========================================================================
 * L6 Tests: Canonical Problems — Cypher query, traversal benchmarks
 * ========================================================================= */
static void test_l6_canonical_problems(void) {
    printf("\n[L6] Canonical Problems\n");

    /* Cypher query parsing and matching */
    PropertyGraph *g = graph_create();
    for (int i = 0; i < 10; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "Person");
        if (i < 3) graph_node_add_label(n, "Admin");
    }
    graph_create_edge(g, 1, 2, "KNOWS", true);
    graph_create_edge(g, 1, 3, "KNOWS", true);
    graph_create_edge(g, 2, 4, "KNOWS", true);
    graph_create_edge(g, 3, 5, "FOLLOWS", true);
    graph_create_edge(g, 4, 5, "KNOWS", true);

    /* Parse pattern */
    QueryPattern pattern;
    bool parsed = cypher_parse("(a:Person)-[:KNOWS]->(b)", &pattern);
    CHECK(parsed, "cypher parse OK");
    CHECK(pattern.node_match_count == 2, "2 node matches");
    CHECK(pattern.edge_match_count == 1, "1 edge match");

    /* Match */
    QueryResult result = cypher_match(g, &pattern);
    CHECK(result.success, "cypher match success");
    CHECK(result.row_count >= 3, "cypher: at least 3 matching rows");

    /* Parse with labels */
    QueryPattern pattern2;
    bool parsed2 = cypher_parse("(a:Person)-[:KNOWS]->(b:Admin)", &pattern2);
    CHECK(parsed2, "cypher parse with target label");

    QueryResult result2 = cypher_match(g, &pattern2);
    CHECK(result2.success, "cypher match with label filter");
    CHECK(result2.row_count >= 1, "at least 1 (Person)->(Admin) via KNOWS");

    graph_destroy(g);
}

/* =========================================================================
 * L7 Tests: Applications — Storage round-trip, metrics
 * ========================================================================= */
static void test_l7_applications(void) {
    printf("\n[L7] Applications\n");

    /* Storage round-trip: save and load */
    PropertyGraph *g = graph_create();
    for (int i = 0; i < 20; i++) {
        Node *n = graph_create_node(g);
        graph_node_add_label(n, "User");
        char name[32];
        snprintf(name, sizeof(name), "User%d", i + 1);
        graph_node_set_property(n, "name", name);
    }
    for (int i = 1; i <= 10; i++) {
        graph_create_edge(g, i, i + 1, "FRIEND", false);
    }
    for (int i = 11; i <= 15; i++) {
        graph_create_edge(g, i, i + 1, "FOLLOW", true);
    }

    GraphStorage *gs = gs_create();
    CHECK(gs != NULL, "gs_create");

    bool stored = gs_store_graph(gs, g);
    CHECK(stored, "gs_store_graph succeeded");

    /* Load into fresh graph */
    PropertyGraph *g2 = graph_create();
    bool loaded = gs_load_graph(gs, g2);
    CHECK(loaded, "gs_load_graph succeeded");
    CHECK(gs_node_count(gs) == 20, "storage: 20 nodes persisted");
    CHECK(gs_edge_count(gs) == 15, "storage: 15 edges persisted");

    /* Graph statistics */
    GraphStatistics stats;
    graph_statistics_compute(g2, &stats);
    CHECK(stats.graph_density > 0.0, "density > 0");
    CHECK(stats.triangle_count >= 0, "triangle count computed");
    CHECK(stats.average_path_length > 0.0, "avg path length > 0");

    graph_destroy(g);
    graph_destroy(g2);
    gs_destroy(gs);

    /* Centrality on a star graph */
    PropertyGraph *g3 = graph_create();
    for (int i = 0; i < 6; i++) graph_create_node(g3);
    for (int i = 2; i <= 6; i++) {
        graph_create_edge(g3, 1, i, "E", false);
    }
    CentralityMetrics cent[6];
    int cent_count = centrality_all(g3, cent, 6);
    CHECK(cent_count == 6, "centrality_all: 6 nodes");
    /* Node 1 should have highest degree centrality in a star */
    double max_deg = 0.0;
    int max_idx = 0;
    for (int i = 0; i < cent_count; i++) {
        if (cent[i].degree_centrality > max_deg) {
            max_deg = cent[i].degree_centrality;
            max_idx = (int)cent[i].node_id;
        }
    }
    CHECK(max_idx == 1, "star center has highest degree centrality");
    graph_destroy(g3);
}

/* =========================================================================
 * L8 Tests: Advanced Topics — Buffer pool, WAL recovery, checkpoint
 * ========================================================================= */
static void test_l8_advanced(void) {
    printf("\n[L8] Advanced: Storage Engine\n");

    /* Buffer pool */
    BufferPool *bp = bp_create();
    CHECK(bp != NULL, "buffer pool create");

    DataPage *p1 = bp_get_page(bp, 1);
    CHECK(p1 != NULL, "bp get page 1");
    bp_mark_dirty(bp, 1);
    bp_unpin(bp, 1);

    DataPage *p2 = bp_get_page(bp, 2);
    CHECK(p2 != NULL, "bp get page 2");
    bp_unpin(bp, 2);

    DataPage *p1_again = bp_get_page(bp, 1);
    CHECK(p1_again == p1, "bp page 1 cached (hit)");
    CHECK(bp->hit_count >= 1, "bp hit counter incremented");
    bp_unpin(bp, 1);

    /* Page operations */
    DataPage test_page;
    page_init(&test_page, 42, PAGE_TYPE_NODE);
    CHECK(test_page.header.record_count == 0, "page init: empty");
    CHECK(test_page.header.free_offset == PAGE_HEADER_SIZE, "page init: free_offset");

    uint8_t test_record[128];
    memset(test_record, 0xAB, sizeof(test_record));
    int slot = page_insert_record(&test_page, test_record, 64);
    CHECK(slot == 0, "page insert: slot 0");
    CHECK(test_page.header.record_count == 1, "page insert: count=1");

    uint8_t out_buf[128];
    uint16_t out_len = 0;
    bool got = page_get_record(&test_page, 0, out_buf, &out_len);
    CHECK(got && out_len == 64, "page get record");
    CHECK(memcmp(out_buf, test_record, 64) == 0, "page get: data matches");

    page_calc_checksum(&test_page);
    CHECK(page_verify(&test_page), "page verify");

    bool deleted = page_delete_record(&test_page, 0);
    CHECK(deleted, "page delete record");

    /* WAL */
    WALManager *wal = wal_create("test.wal");
    CHECK(wal != NULL, "wal create");

    uint32_t lsn = wal_log_insert(wal, WAL_INSERT, 1, 100, "hello", 5);
    CHECK(lsn > 0, "wal log insert");
    wal_commit(wal);
    CHECK(wal->flushed_count > 0, "wal flushed after commit");

    wal_checkpoint(wal);
    CHECK(wal->record_count == 0, "wal records cleared after checkpoint");

    wal_destroy(wal);

    /* cleanup test file */
    remove("test.wal");
    remove(DATA_FILE);
    remove(WAL_FILE);

    bp_destroy(bp);
}

/* =========================================================================
 * L9 Tests: Industry Frontiers — documentation validation only
 * ========================================================================= */
static void test_l9_frontiers(void) {
    printf("\n[L9] Industry Frontiers (documentation verification)\n");

    /* Verify key structures for industry relevance */
    TEST("WAL structure size check");
    CHECK(sizeof(WALRecord) > 64, "WAL record has reasonable size");

    TEST("BufferPool frame size check");
    CHECK(sizeof(BufferFrame) > sizeof(DataPage), "buffer frame larger than page");

    TEST("Page size alignment");
    CHECK(sizeof(DataPage) + sizeof(uint32_t) == PAGE_SIZE, "DataPage + footer == PAGE_SIZE");

    TEST("NodeRecord for persistent storage");
    CHECK(sizeof(NodeRecord) >= sizeof(Node), "NodeRecord >= Node (persistence)");
}

/* helper: simple distance heuristic for A* */
static double test_heuristic(int64_t node, int64_t target) {
    return (double)llabs(node - target);
}

static double test_weight(Edge *e) {
    (void)e;
    return 1.0;
}

int main(void) {
    printf("========================================\n");
    printf("  mini-graph-db Test Suite\n");
    printf("========================================\n");

    test_l1_core_definitions();
    test_l2_graph_operations();
    test_l3_index_structures();
    test_l4_standards_theorems();
    test_l5_algorithms();
    test_l6_canonical_problems();
    test_l7_applications();
    test_l8_advanced();
    test_l9_frontiers();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    /* cleanup */
    remove("test.wal");
    remove(DATA_FILE);
    remove(WAL_FILE);
    remove("graph.wal");
    remove("graph.dat");

    return tests_failed > 0 ? 1 : 0;
}
