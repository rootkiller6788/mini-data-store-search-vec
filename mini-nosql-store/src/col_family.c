#include "col_family.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

BigtableStore *col_store_create(const char *table_name) {
    BigtableStore *store = (BigtableStore *)calloc(1, sizeof(BigtableStore));
    if (!store) return NULL;
    if (table_name) {
        strncpy(store->table_name, table_name, CF_MAX_FAMILY - 1);
        store->table_name[CF_MAX_FAMILY - 1] = '\0';
    }
    Tablet *tablet = (Tablet *)calloc(1, sizeof(Tablet));
    if (!tablet) { free(store); return NULL; }
    strncpy(tablet->start_row, "", CF_MAX_ROW_KEY - 1);
    strncpy(tablet->end_row, "\xFF\xFF", CF_MAX_ROW_KEY - 1);
    tablet->next = NULL;
    store->tablets = tablet;
    store->tablet_count = 1;
    return store;
}

void col_store_destroy(BigtableStore *store) {
    if (!store) return;
    Tablet *t = store->tablets;
    while (t) {
        Tablet *tmp_tablet = t;
        ColumnFamily *cf = t->families;
        while (cf) {
            ColumnFamily *tmp_cf = cf;
            Cell *cell = cf->cells;
            while (cell) {
                Cell *tmp_cell = cell;
                cell = cell->next;
                free(tmp_cell);
            }
            cf = cf->next;
            free(tmp_cf);
        }
        t = t->next;
        free(tmp_tablet);
    }
    free(store);
}

static ColumnFamily *get_or_create_family(BigtableStore *store,
                                          Tablet *tablet, const char *name) {
    ColumnFamily *cf = tablet->families;
    while (cf) {
        if (strcmp(cf->name, name) == 0) return cf;
        cf = cf->next;
    }
    ColumnFamily *new_cf = (ColumnFamily *)calloc(1, sizeof(ColumnFamily));
    if (!new_cf) return NULL;
    strncpy(new_cf->name, name, CF_MAX_FAMILY - 1);
    new_cf->name[CF_MAX_FAMILY - 1] = '\0';
    new_cf->max_versions = CF_MAX_VERSIONS;
    new_cf->next = tablet->families;
    tablet->families = new_cf;
    tablet->family_count++;
    return new_cf;
}

Tablet *col_find_tablet(BigtableStore *store, const char *row_key) {
    if (!store || !row_key) return NULL;
    Tablet *t = store->tablets;
    while (t) {
        if (strcmp(row_key, t->start_row) >= 0 &&
            strcmp(row_key, t->end_row) <= 0)
            return t;
        t = t->next;
    }
    return store->tablets;
}

int col_put(BigtableStore *store, const char *row_key,
            const char *col_family, const char *col_qualifier,
            int64_t timestamp, const char *value) {
    if (!store || !row_key || !col_family || !value) return -1;
    Tablet *tablet = col_find_tablet(store, row_key);
    if (!tablet) return -1;
    ColumnFamily *cf = get_or_create_family(store, tablet, col_family);
    if (!cf) return -1;

    if (col_qualifier) {
        int found = 0;
        for (int i = 0; i < cf->column_count; i++) {
            if (strcmp(cf->columns[i], col_qualifier) == 0) {
                found = 1; break;
            }
        }
        if (!found && cf->column_count < CF_MAX_COLUMNS) {
            strncpy(cf->columns[cf->column_count], col_qualifier, CF_MAX_QUALIFIER - 1);
            cf->columns[cf->column_count][CF_MAX_QUALIFIER - 1] = '\0';
            cf->column_count++;
        }
    }

    Cell *cell = (Cell *)calloc(1, sizeof(Cell));
    if (!cell) return -1;
    strncpy(cell->row_key, row_key, CF_MAX_ROW_KEY - 1);
    strncpy(cell->col_family, col_family, CF_MAX_FAMILY - 1);
    if (col_qualifier) {
        strncpy(cell->col_qualifier, col_qualifier, CF_MAX_QUALIFIER - 1);
    }
    cell->timestamp = timestamp > 0 ? timestamp : (int64_t)time(NULL);
    strncpy(cell->value, value, CF_MAX_VALUE_LEN - 1);
    cell->value[CF_MAX_VALUE_LEN - 1] = '\0';
    cell->next = cf->cells;
    cf->cells = cell;
    cf->cell_count++;

    int version_count = 0;
    Cell *tmp = cf->cells;
    while (tmp) {
        if (strcmp(tmp->row_key, row_key) == 0 &&
            strcmp(tmp->col_family, col_family) == 0) {
            version_count++;
        }
        tmp = tmp->next;
    }
    if (version_count > cf->max_versions) {
        Cell *prev = NULL;
        Cell *c = cf->cells;
        Cell *oldest = NULL;
        Cell *oldest_prev = NULL;
        int64_t oldest_ts = INT64_MAX;
        while (c) {
            if (strcmp(c->row_key, row_key) == 0 &&
                strcmp(c->col_family, col_family) == 0 &&
                c->timestamp < oldest_ts) {
                oldest_ts = c->timestamp;
                oldest = c;
                oldest_prev = prev;
            }
            prev = c;
            c = c->next;
        }
        if (oldest) {
            if (oldest_prev) oldest_prev->next = oldest->next;
            else cf->cells = oldest->next;
            free(oldest);
            cf->cell_count--;
        }
    }

    return 0;
}

int col_get(BigtableStore *store, const char *row_key,
            const char *col_family, const char *col_qualifier,
            Cell **results, int max_results) {
    if (!store || !row_key || !col_family || !results) return 0;
    Tablet *tablet = col_find_tablet(store, row_key);
    if (!tablet) return 0;
    ColumnFamily *cf = get_or_create_family(store, tablet, col_family);
    if (!cf) return 0;

    int found = 0;
    Cell *cell = cf->cells;
    Cell *sorted = NULL;

    while (cell && found < max_results) {
        if (strcmp(cell->row_key, row_key) == 0) {
            if (!col_qualifier || strcmp(cell->col_qualifier, col_qualifier) == 0) {
                Cell *new_node = (Cell *)calloc(1, sizeof(Cell));
                if (new_node) {
                    memcpy(new_node, cell, sizeof(Cell));
                    new_node->next = sorted;
                    sorted = new_node;
                    found++;
                }
            }
        }
        cell = cell->next;
    }

    for (int i = 0; i < found; i++) {
        Cell *max = sorted;
        Cell *cur = sorted;
        int idx = 0;
        int max_idx = 0;
        while (cur) {
            if (idx > max_idx) { (void)max; }
            cur = cur->next;
            idx++;
        }
        results[i] = sorted;
        sorted = sorted->next;
    }
    return found;
}

int col_get_latest(BigtableStore *store, const char *row_key,
                   const char *col_family, const char *col_qualifier,
                   char *value_out, size_t max_len) {
    if (!store || !row_key || !col_family || !value_out) return -1;
    Tablet *tablet = col_find_tablet(store, row_key);
    if (!tablet) return -1;
    ColumnFamily *cf = get_or_create_family(store, tablet, col_family);
    if (!cf) return -1;

    Cell *latest = NULL;
    int64_t latest_ts = -1;
    Cell *cell = cf->cells;
    while (cell) {
        if (strcmp(cell->row_key, row_key) == 0 &&
            strcmp(cell->col_family, col_family) == 0) {
            if (!col_qualifier || strcmp(cell->col_qualifier, col_qualifier) == 0) {
                if (cell->timestamp > latest_ts) {
                    latest_ts = cell->timestamp;
                    latest = cell;
                }
            }
        }
        cell = cell->next;
    }
    if (latest) {
        strncpy(value_out, latest->value, max_len - 1);
        value_out[max_len - 1] = '\0';
        return 0;
    }
    return -2;
}

int col_scan(BigtableStore *store, const char *start_row, const char *end_row,
             const char *col_family, Cell **results, int max_results) {
    if (!store || !start_row || !end_row || !col_family || !results) return 0;
    Tablet *tablet = col_find_tablet(store, start_row);
    if (!tablet) return 0;
    ColumnFamily *cf = get_or_create_family(store, tablet, col_family);
    if (!cf) return 0;

    int found = 0;
    Cell *cell = cf->cells;
    while (cell && found < max_results) {
        if (strcmp(cell->row_key, start_row) >= 0 &&
            strcmp(cell->row_key, end_row) <= 0) {
            results[found++] = cell;
        }
        cell = cell->next;
    }
    return found;
}

int col_delete(BigtableStore *store, const char *row_key,
               const char *col_family, const char *col_qualifier) {
    return col_put(store, row_key, col_family, col_qualifier, 0, "__DELETED__");
}

ColumnFamily *col_get_family(BigtableStore *store, const char *name) {
    if (!store || !name) return NULL;
    Tablet *t = store->tablets;
    while (t) {
        ColumnFamily *cf = t->families;
        while (cf) {
            if (strcmp(cf->name, name) == 0) return cf;
            cf = cf->next;
        }
        t = t->next;
    }
    return NULL;
}

int col_split_tablet(BigtableStore *store, const char *row_key) {
    if (!store || !row_key) return -1;
    Tablet *tablet = col_find_tablet(store, row_key);
    if (!tablet) return -1;
    Tablet *new_tablet = (Tablet *)calloc(1, sizeof(Tablet));
    if (!new_tablet) return -1;
    strncpy(new_tablet->start_row, row_key, CF_MAX_ROW_KEY - 1);
    strncpy(new_tablet->end_row, tablet->end_row, CF_MAX_ROW_KEY - 1);
    strncpy(tablet->end_row, row_key, CF_MAX_ROW_KEY - 1);
    new_tablet->next = tablet->next;
    tablet->next = new_tablet;

    Cell *cell = new_tablet->families ? new_tablet->families->cells : NULL;
    Cell *prev = NULL;
    Cell *cur = cell;
    while (cur) {
        if (strcmp(cur->row_key, row_key) >= 0) {
            if (prev) prev->next = cur->next;
            else if (new_tablet->families) new_tablet->families->cells = cur->next;
        } else {
            prev = cur;
        }
        cur = cur ? cur->next : NULL;
    }

    store->tablet_count++;
    return 0;
}

void col_free_cell_results(Cell *results, int count) {
    (void)results;
    (void)count;
}
