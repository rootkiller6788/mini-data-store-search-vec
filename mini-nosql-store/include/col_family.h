#ifndef COL_FAMILY_H
#define COL_FAMILY_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define CF_MAX_ROW_KEY     64
#define CF_MAX_FAMILY      32
#define CF_MAX_QUALIFIER   64
#define CF_MAX_VALUE_LEN  256
#define CF_MAX_VERSIONS     5
#define CF_MAX_COLUMNS     16
#define CF_MAX_TABLETS     16
#define CF_TABLET_ROWS    512

typedef struct cell_t {
    char    row_key[CF_MAX_ROW_KEY];
    char    col_family[CF_MAX_FAMILY];
    char    col_qualifier[CF_MAX_QUALIFIER];
    int64_t timestamp;
    char    value[CF_MAX_VALUE_LEN];
    struct cell_t *next;
} Cell;

typedef struct column_family_t {
    char         name[CF_MAX_FAMILY];
    char         columns[CF_MAX_COLUMNS][CF_MAX_QUALIFIER];
    int          column_count;
    Cell        *cells;
    int          cell_count;
    int          max_versions;
    struct column_family_t *next;
} ColumnFamily;

typedef struct tablet_t {
    char          start_row[CF_MAX_ROW_KEY];
    char          end_row[CF_MAX_ROW_KEY];
    ColumnFamily *families;
    int           family_count;
    struct tablet_t *next;
} Tablet;

typedef struct bigtable_store_t {
    Tablet *tablets;
    int     tablet_count;
    char    table_name[CF_MAX_FAMILY];
} BigtableStore;

BigtableStore *col_store_create(const char *table_name);
void           col_store_destroy(BigtableStore *store);

int  col_put(BigtableStore *store, const char *row_key,
             const char *col_family, const char *col_qualifier,
             int64_t timestamp, const char *value);
int  col_get(BigtableStore *store, const char *row_key,
             const char *col_family, const char *col_qualifier,
             Cell **results, int max_results);
int  col_get_latest(BigtableStore *store, const char *row_key,
                    const char *col_family, const char *col_qualifier,
                    char *value_out, size_t max_len);
int  col_scan(BigtableStore *store, const char *start_row, const char *end_row,
              const char *col_family, Cell **results, int max_results);
int  col_delete(BigtableStore *store, const char *row_key,
                const char *col_family, const char *col_qualifier);

ColumnFamily *col_get_family(BigtableStore *store, const char *name);
Tablet       *col_find_tablet(BigtableStore *store, const char *row_key);
int           col_split_tablet(BigtableStore *store, const char *row_key);
void          col_free_cell_results(Cell *results, int count);

#endif
