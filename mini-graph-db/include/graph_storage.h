#ifndef GRAPH_STORAGE_H
#define GRAPH_STORAGE_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================================================================
 * L3: Engineering Structures — Page-based Storage Engine
 *
 * Concepts:
 *   - Slotted Page Layout: variable-length records within fixed-size pages,
 *     tracked by a slot directory growing downward from page end.
 *   - Buffer Pool: in-memory cache of disk pages with Clock eviction policy.
 *   - Write-Ahead Log (WAL): sequential mutation log for durability and
 *     crash recovery (ARIES-inspired simplified model).
 *
 * L4: Standards/Theorems — ACID Durability via WAL
 *
 *   Write-Ahead Logging Theorem (Gray & Reuter):
 *     Before a database page is flushed to disk, its corresponding log
 *     record must be persisted to stable storage. This guarantees
 *     atomicity and durability in the presence of crashes.
 *
 *   Checksum Invariant:
 *     Each page carries a CRC32 checksum. On read, mismatch indicates
 *     torn page or bit rot. Corrupt pages are recovered via WAL replay.
 *
 * L8: Advanced Topics — Buffer Pool & Crash Recovery
 *   - Clock (Second-Chance) algorithm: approximates LRU with O(1) overhead
 *   - WAL-based recovery: REDO pass re-applies logged changes
 *   - Checkpoint: periodic sync truncating WAL, minimizing recovery time
 * ============================================================================ */

/* --- Constants ----------------------------------------------------------- */
#define PAGE_SIZE             4096
#define BUFFER_POOL_SIZE      64
#define MAX_PAGES             256
#define WAL_MAX_RECORDS       1024
#define WAL_FILE              "graph.wal"
#define DATA_FILE             "graph.dat"

/* --- Page Layout Structures --------------------------------------------- */

typedef enum {
    PAGE_TYPE_META       = 0,
    PAGE_TYPE_NODE       = 1,
    PAGE_TYPE_EDGE       = 2,
    PAGE_TYPE_ADJACENCY  = 3,
    PAGE_TYPE_INDEX      = 4,
} PageType;

#define PAGE_HEADER_SIZE 64
#define PAGE_FLAG_DIRTY     0x0001
#define PAGE_FLAG_CHECKPOINT 0x0002
#define PAGE_FLAG_FIXED      0x0004
#define PAGE_DATA_SIZE (PAGE_SIZE - PAGE_HEADER_SIZE - 4)

typedef struct {
    uint32_t checksum;
    uint32_t page_id;
    PageType page_type;
    uint16_t record_count;
    uint16_t free_offset;
    uint16_t slot_dir_offset;
    uint16_t flags;
    uint32_t lsn;
    uint32_t next_page;
    uint8_t  reserved[36];  /* padded to make PageHeader exactly 64 bytes */
} PageHeader;

typedef struct {
    uint16_t offset;
    uint16_t length;
    uint16_t flags;
    uint16_t pad;
} SlotEntry;
#define SLOT_EMPTY        0x0001
#define MAX_SLOTS         128

typedef struct {
    PageHeader header;
    uint8_t data[PAGE_DATA_SIZE];
} DataPage;

/* --- Buffer Pool Structures --------------------------------------------- */

typedef struct BufferFrame {
    uint32_t page_id;
    DataPage page;
    bool dirty;
    bool valid;
    int pin_count;
    uint8_t clock_ref;
} BufferFrame;

typedef struct {
    BufferFrame frames[BUFFER_POOL_SIZE];
    int clock_hand;
    int hit_count;
    int miss_count;
    int evict_count;
} BufferPool;

/* --- Write-Ahead Log Structures ----------------------------------------- */

typedef enum {
    WAL_INSERT   = 0,
    WAL_UPDATE   = 1,
    WAL_DELETE   = 2,
    WAL_CHECKPOINT = 3,
    WAL_COMMIT    = 4,
    WAL_ABORT     = 5,
} WALRecordType;

typedef struct {
    uint32_t lsn;
    WALRecordType type;
    uint32_t page_id;
    uint16_t offset;
    uint16_t length;
    uint16_t old_length;
    uint64_t transaction_id;
    uint8_t data[512];
    uint16_t data_len;
} WALRecord;

typedef struct {
    WALRecord records[WAL_MAX_RECORDS];
    int record_count;
    int flushed_count;
    uint32_t next_lsn;
    uint64_t next_txn_id;
    FILE *log_file;
} WALManager;

/* --- Storage Engine ----------------------------------------------------- */

typedef struct {
    BufferPool *pool;
    WALManager *wal;
    uint32_t node_page_list;
    uint32_t edge_page_list;
    uint32_t adj_page_list;
    int total_nodes;
    int total_edges;
    bool recovery_mode;
} GraphStorage;

typedef struct {
    int64_t id;
    int32_t label_count;
    char labels[MAX_NODE_LABELS][MAX_LABEL_LEN];
    int32_t property_count;
    Property properties[MAX_NODE_PROPERTIES];
} NodeRecord;

typedef struct {
    int64_t id;
    char type[MAX_EDGE_TYPE_LEN];
    int64_t from_node;
    int64_t to_node;
    bool directed;
    int32_t property_count;
    Property properties[MAX_EDGE_PROPERTIES];
} EdgeRecord;

typedef struct {
    int64_t node_id;
    int64_t edge_id;
    int64_t neighbor_id;
} AdjacencyRecord;

/* --- API ---------------------------------------------------------------- */

/* Buffer Pool */
BufferPool *bp_create(void);
void bp_destroy(BufferPool *bp);
DataPage *bp_get_page(BufferPool *bp, uint32_t page_id);
void bp_mark_dirty(BufferPool *bp, uint32_t page_id);
void bp_unpin(BufferPool *bp, uint32_t page_id);
void bp_flush_all(BufferPool *bp, FILE *data_file);
void bp_print_stats(BufferPool *bp);

/* WAL Manager */
WALManager *wal_create(const char *log_path);
void wal_destroy(WALManager *wal);
uint32_t wal_log_insert(WALManager *wal, WALRecordType type,
                        uint32_t page_id, uint16_t offset,
                        const void *data, uint16_t len);
uint32_t wal_log_update(WALManager *wal, uint32_t page_id,
                        uint16_t offset, const void *old_data,
                        uint16_t old_len, const void *new_data, uint16_t new_len);
void wal_commit(WALManager *wal);
void wal_checkpoint(WALManager *wal);
int  wal_recover(WALManager *wal, BufferPool *bp, FILE *data_file);
void wal_print_info(WALManager *wal);

/* Page Operations */
uint32_t page_checksum(const DataPage *page);
bool  page_verify(const DataPage *page);
void  page_init(DataPage *page, uint32_t page_id, PageType type);
int   page_insert_record(DataPage *page, const void *record, uint16_t len);
bool  page_get_record(DataPage *page, int slot_idx, void *out_buf, uint16_t *out_len);
bool  page_delete_record(DataPage *page, int slot_idx);
void  page_calc_checksum(DataPage *page);

/* Graph Storage */
GraphStorage *gs_create(void);
void gs_destroy(GraphStorage *gs);
bool gs_store_graph(GraphStorage *gs, PropertyGraph *g);
bool gs_load_graph(GraphStorage *gs, PropertyGraph *g);
int  gs_node_count(GraphStorage *gs);
int  gs_edge_count(GraphStorage *gs);

/* Serialization */
int node_record_serialize(const Node *n, uint8_t *buf, int max_len);
int node_record_deserialize(const uint8_t *buf, int len, Node *n);
int edge_record_serialize(const Edge *e, uint8_t *buf, int max_len);
int edge_record_deserialize(const uint8_t *buf, int len, Edge *e);

/* Checksum */
uint32_t crc32_compute(const uint8_t *data, size_t len);

#endif
