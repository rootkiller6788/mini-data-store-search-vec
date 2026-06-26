#include "document_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static unsigned int doc_index_hash(const char *s) {
    unsigned int h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;
    return h % DOC_INDEX_BUCKETS;
}

DocumentStore *doc_store_create(void) {
    return (DocumentStore *)calloc(1, sizeof(DocumentStore));
}

static void doc_collection_free(DocCollection *coll) {
    if (!coll) return;
    Document *d = coll->documents;
    while (d) {
        Document *tmp = d;
        d = d->next;
        free(tmp);
    }
    for (int i = 0; i < coll->index_count; i++) {
        for (int j = 0; j < DOC_INDEX_BUCKETS; j++) {
            DocIndexEntry *e = coll->indexes[i].buckets[j];
            while (e) {
                DocIndexEntry *tmp = e;
                e = e->next;
                free(tmp);
            }
        }
    }
    free(coll);
}

void doc_store_destroy(DocumentStore *store) {
    if (!store) return;
    DocCollection *c = store->collections;
    while (c) {
        DocCollection *tmp = c;
        c = c->next;
        doc_collection_free(tmp);
    }
    free(store);
}

static DocCollection *get_or_create_collection(DocumentStore *store, const char *name) {
    DocCollection *c = store->collections;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    DocCollection *new_coll = (DocCollection *)calloc(1, sizeof(DocCollection));
    if (!new_coll) return NULL;
    strncpy(new_coll->name, name, DOC_MAX_COLLECTION - 1);
    new_coll->name[DOC_MAX_COLLECTION - 1] = '\0';
    new_coll->next = store->collections;
    store->collections = new_coll;
    store->collection_count++;
    return new_coll;
}

int doc_insert(DocumentStore *store, const char *collection,
               const char *doc_id, const uint8_t *bson_data, size_t bson_len) {
    if (!store || !collection || !doc_id || !bson_data) return -1;
    DocCollection *coll = get_or_create_collection(store, collection);
    if (!coll) return -1;

    Document *doc = (Document *)calloc(1, sizeof(Document));
    if (!doc) return -1;
    strncpy(doc->collection, collection, DOC_MAX_COLLECTION - 1);
    doc->collection[DOC_MAX_COLLECTION - 1] = '\0';
    strncpy(doc->doc_id, doc_id, DOC_MAX_ID_LEN - 1);
    doc->doc_id[DOC_MAX_ID_LEN - 1] = '\0';
    if (bson_len > DOC_MAX_BSON_SIZE) bson_len = DOC_MAX_BSON_SIZE;
    memcpy(doc->bson_data, bson_data, bson_len);
    doc->bson_len = bson_len;
    doc->next = coll->documents;
    coll->documents = doc;
    coll->doc_count++;
    return 0;
}

Document *doc_find(DocumentStore *store, const char *collection, const char *doc_id) {
    if (!store || !collection || !doc_id) return NULL;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll) return NULL;
    Document *d = coll->documents;
    while (d) {
        if (strcmp(d->doc_id, doc_id) == 0) return d;
        d = d->next;
    }
    return NULL;
}

static int bson_find_field(const uint8_t *bson, size_t len,
                           const char *field, char *value_out, size_t max_val) {
    size_t pos = 4;
    while (pos < len - 1) {
        uint8_t type = bson[pos];
        if (type == 0) break;
        pos++;
        const char *ekey = (const char *)(bson + pos);
        size_t ekey_len = strlen(ekey);
        if (strcmp(ekey, field) == 0) {
            pos += ekey_len + 1;
            switch (type) {
            case 0x02:
                {
                    uint32_t slen;
                    memcpy(&slen, bson + pos, 4);
                    pos += 4;
                    size_t sl = slen - 1;
                    if (sl > max_val - 1) sl = max_val - 1;
                    memcpy(value_out, bson + pos, sl);
                    value_out[sl] = '\0';
                    return 0;
                }
            case 0x10:
                {
                    int32_t iv;
                    memcpy(&iv, bson + pos, 4);
                    snprintf(value_out, max_val, "%d", iv);
                    return 0;
                }
            case 0x12:
                {
                    int64_t lv;
                    memcpy(&lv, bson + pos, 8);
                    snprintf(value_out, max_val, "%lld", (long long)lv);
                    return 0;
                }
            default:
                strncpy(value_out, "", max_val);
                return -3;
            }
        }
        pos += ekey_len + 1;
        switch (type) {
        case 0x02:
            { uint32_t slen; memcpy(&slen, bson + pos, 4); pos += 4 + slen; break; }
        case 0x10: pos += 4; break;
        case 0x12: pos += 8; break;
        case 0x08: pos += 1; break;
        case 0x01: pos += 8; break;
        case 0x09: pos += 8; break;
        default: pos += 4; break;
        }
    }
    return -2;
}

int doc_find_query(DocumentStore *store, const char *collection,
                   const char *field, const char *value,
                   Document **results, int max_results) {
    if (!store || !collection || !field || !value || !results) return 0;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll) return 0;

    int found = 0;
    Document *d = coll->documents;
    while (d && found < max_results) {
        char field_val[DOC_MAX_VALUE_LEN] = {0};
        if (bson_find_field(d->bson_data, d->bson_len,
                            field, field_val, DOC_MAX_VALUE_LEN) == 0) {
            if (strcmp(field_val, value) == 0) {
                results[found++] = d;
            }
        }
        d = d->next;
    }
    return found;
}

int doc_find_range(DocumentStore *store, const char *collection,
                   const char *field, const char *min_val, const char *max_val,
                   Document **results, int max_results) {
    if (!store || !collection || !field || !results) return 0;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll) return 0;

    int found = 0;
    Document *d = coll->documents;
    while (d && found < max_results) {
        char field_val[DOC_MAX_VALUE_LEN] = {0};
        if (bson_find_field(d->bson_data, d->bson_len,
                            field, field_val, DOC_MAX_VALUE_LEN) == 0) {
            int ge_min = !min_val || strcmp(field_val, min_val) >= 0;
            int le_max = !max_val || strcmp(field_val, max_val) <= 0;
            if (ge_min && le_max) {
                results[found++] = d;
            }
        }
        d = d->next;
    }
    return found;
}

int doc_update(DocumentStore *store, const char *collection, const char *doc_id,
               const uint8_t *bson_data, size_t bson_len) {
    if (!store || !collection || !doc_id || !bson_data) return -1;
    Document *doc = doc_find(store, collection, doc_id);
    if (!doc) return -2;
    size_t len = bson_len < DOC_MAX_BSON_SIZE ? bson_len : DOC_MAX_BSON_SIZE;
    memcpy(doc->bson_data, bson_data, len);
    doc->bson_len = len;
    return 0;
}

int doc_delete(DocumentStore *store, const char *collection, const char *doc_id) {
    if (!store || !collection || !doc_id) return -1;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll) return -2;
    Document *d = coll->documents;
    Document *prev = NULL;
    while (d) {
        if (strcmp(d->doc_id, doc_id) == 0) {
            if (prev) prev->next = d->next;
            else coll->documents = d->next;
            free(d);
            coll->doc_count--;
            return 0;
        }
        prev = d;
        d = d->next;
    }
    return -2;
}

int doc_create_index(DocumentStore *store, const char *collection, const char *field) {
    if (!store || !collection || !field) return -1;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll || coll->index_count >= DOC_MAX_INDEXES) return -1;
    DocIndex *idx = &coll->indexes[coll->index_count++];
    (void)idx;
    return doc_rebuild_index(store, collection, field);
}

int doc_rebuild_index(DocumentStore *store, const char *collection, const char *field) {
    if (!store || !collection || !field) return -1;
    DocCollection *coll = doc_get_collection(store, collection);
    if (!coll) return -1;

    int idx_pos = -1;
    for (int i = 0; i < coll->index_count; i++) {
        DocIndexEntry *e = coll->indexes[i].buckets[0];
        while (e) { DocIndexEntry *t = e; e = e->next; free(t); }
        memset(&coll->indexes[i], 0, sizeof(DocIndex));

        if (idx_pos == -1) idx_pos = i;
    }

    Document *d = coll->documents;
    while (d) {
        char val[DOC_MAX_VALUE_LEN] = {0};
        if (bson_find_field(d->bson_data, d->bson_len,
                            field, val, DOC_MAX_VALUE_LEN) == 0) {
            unsigned int h = doc_index_hash(val);
            DocIndexEntry *entry = (DocIndexEntry *)calloc(1, sizeof(DocIndexEntry));
            if (entry) {
                strncpy(entry->field, field, DOC_MAX_FIELD_LEN - 1);
                strncpy(entry->value, val, DOC_MAX_VALUE_LEN - 1);
                if (entry->doc_id_count < 64) {
                    strncpy(entry->doc_ids[entry->doc_id_count++],
                            d->doc_id, DOC_MAX_ID_LEN - 1);
                }
                entry->next = coll->indexes[0].buckets[h];
                coll->indexes[0].buckets[h] = entry;
                coll->indexes[0].index_count++;
            }
        }
        d = d->next;
    }
    return 0;
}

int doc_drop_index(DocumentStore *store, const char *collection, const char *field) {
    (void)store; (void)collection; (void)field;
    return 0;
}

DocCollection *doc_get_collection(DocumentStore *store, const char *name) {
    if (!store || !name) return NULL;
    DocCollection *c = store->collections;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

int doc_collection_count(DocumentStore *store) {
    return store ? store->collection_count : -1;
}

void doc_query_free(Document *results, int count) {
    (void)results;
    (void)count;
}
