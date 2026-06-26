#ifndef DOCUMENT_STORE_H
#define DOCUMENT_STORE_H

#include <stdint.h>
#include <stddef.h>

#define DOC_MAX_BSON_SIZE  4096
#define DOC_MAX_ID_LEN      32
#define DOC_MAX_COLLECTION  64
#define DOC_MAX_FIELDS      32
#define DOC_MAX_FIELD_LEN   64
#define DOC_MAX_VALUE_LEN  256
#define DOC_MAX_INDEXES     16
#define DOC_INDEX_BUCKETS   256

typedef struct document_t {
    char   collection[DOC_MAX_COLLECTION];
    char   doc_id[DOC_MAX_ID_LEN];
    uint8_t bson_data[DOC_MAX_BSON_SIZE];
    size_t  bson_len;
    struct document_t *next;
} Document;

typedef struct doc_index_entry_t {
    char    field[DOC_MAX_FIELD_LEN];
    char    value[DOC_MAX_VALUE_LEN];
    char    doc_ids[64][DOC_MAX_ID_LEN];
    int     doc_id_count;
    struct doc_index_entry_t *next;
} DocIndexEntry;

typedef struct doc_index_t {
    DocIndexEntry *buckets[DOC_INDEX_BUCKETS];
    int            index_count;
} DocIndex;

typedef struct doc_collection_t {
    char       name[DOC_MAX_COLLECTION];
    Document  *documents;
    int        doc_count;
    DocIndex   indexes[DOC_MAX_INDEXES];
    int        index_count;
    struct doc_collection_t *next;
} DocCollection;

typedef struct document_store_t {
    DocCollection *collections;
    int            collection_count;
} DocumentStore;

DocumentStore *doc_store_create(void);
void           doc_store_destroy(DocumentStore *store);

int  doc_insert(DocumentStore *store, const char *collection,
                const char *doc_id, const uint8_t *bson_data, size_t bson_len);
Document *doc_find(DocumentStore *store, const char *collection, const char *doc_id);
int  doc_find_query(DocumentStore *store, const char *collection,
                    const char *field, const char *value,
                    Document **results, int max_results);
int  doc_find_range(DocumentStore *store, const char *collection,
                    const char *field, const char *min_val, const char *max_val,
                    Document **results, int max_results);
int  doc_update(DocumentStore *store, const char *collection, const char *doc_id,
                const uint8_t *bson_data, size_t bson_len);
int  doc_delete(DocumentStore *store, const char *collection, const char *doc_id);

int  doc_create_index(DocumentStore *store, const char *collection, const char *field);
int  doc_drop_index(DocumentStore *store, const char *collection, const char *field);
int  doc_rebuild_index(DocumentStore *store, const char *collection, const char *field);

DocCollection *doc_get_collection(DocumentStore *store, const char *name);
int  doc_collection_count(DocumentStore *store);
void doc_query_free(Document *results, int count);

#endif
