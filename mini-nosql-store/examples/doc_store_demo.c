#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "document_store.h"

static size_t make_bson_doc(uint8_t *buf, size_t cap,
                            const char *name, const char *city, int age) {
    memset(buf, 0, cap);
    size_t pos = 4;

    buf[pos++] = 0x02;
    const char *k = "name";
    strcpy((char *)(buf + pos), k);
    pos += strlen(k) + 1;
    uint32_t nlen = (uint32_t)strlen(name) + 1;
    memcpy(buf + pos, &nlen, 4); pos += 4;
    strcpy((char *)(buf + pos), name);
    pos += nlen;

    buf[pos++] = 0x02;
    k = "city";
    strcpy((char *)(buf + pos), k);
    pos += strlen(k) + 1;
    nlen = (uint32_t)strlen(city) + 1;
    memcpy(buf + pos, &nlen, 4); pos += 4;
    strcpy((char *)(buf + pos), city);
    pos += nlen;

    buf[pos++] = 0x10;
    k = "age";
    strcpy((char *)(buf + pos), k);
    pos += strlen(k) + 1;
    int32_t age32 = (int32_t)age;
    memcpy(buf + pos, &age32, 4); pos += 4;

    buf[pos++] = 0x00;
    uint32_t total = (uint32_t)pos;
    memcpy(buf, &total, 4);

    return pos;
}

int main(void) {
    printf("=== mini-nosql Document Store Demo ===\n\n");

    DocumentStore *store = doc_store_create();
    if (!store) {
        fprintf(stderr, "Failed to create document store\n");
        return 1;
    }

    uint8_t bson[DOC_MAX_BSON_SIZE];
    size_t len;

    len = make_bson_doc(bson, sizeof(bson), "Alice", "Beijing", 28);
    doc_insert(store, "users", "u1", bson, len);

    len = make_bson_doc(bson, sizeof(bson), "Bob", "Shanghai", 35);
    doc_insert(store, "users", "u2", bson, len);

    len = make_bson_doc(bson, sizeof(bson), "Charlie", "Beijing", 42);
    doc_insert(store, "users", "u3", bson, len);

    len = make_bson_doc(bson, sizeof(bson), "Diana", "Shenzhen", 31);
    doc_insert(store, "users", "u4", bson, len);

    len = make_bson_doc(bson, sizeof(bson), "MacBook Pro", "Electronics", 0);
    doc_insert(store, "products", "p1", bson, len);

    len = make_bson_doc(bson, sizeof(bson), "Pixel Phone", "Electronics", 0);
    doc_insert(store, "products", "p2", bson, len);

    printf("Collections: %d\n", doc_collection_count(store));

    printf("\n--- Find user by doc_id ---\n");
    Document *doc = doc_find(store, "users", "u1");
    if (doc) printf("  Found doc: id=%s, bson_len=%zu\n", doc->doc_id, doc->bson_len);

    printf("\n--- Find documents by query: city='Beijing' ---\n");
    Document *results[16];
    int n = doc_find_query(store, "users", "city", "Beijing", results, 16);
    printf("  Found %d documents:\n", n);
    for (int i = 0; i < n; i++)
        printf("    doc_id=%s\n", results[i]->doc_id);

    printf("\n--- Range find: age between 30 and 45 ---\n");
    n = doc_find_range(store, "users", "age", "30", "45", results, 16);
    printf("  Found %d documents:\n", n);
    for (int i = 0; i < n; i++)
        printf("    doc_id=%s\n", results[i]->doc_id);

    printf("\n--- Update document ---\n");
    uint8_t new_bson[DOC_MAX_BSON_SIZE];
    size_t new_len = make_bson_doc(new_bson, sizeof(new_bson), "Alice Updated", "Hangzhou", 29);
    doc_update(store, "users", "u1", new_bson, new_len);
    doc = doc_find(store, "users", "u1");
    if (doc) printf("  Updated doc: id=%s, bson_len=%zu\n", doc->doc_id, doc->bson_len);

    printf("\n--- Delete document ---\n");
    doc_delete(store, "users", "u2");
    doc = doc_find(store, "users", "u2");
    printf("  u2 after delete: %s\n", doc ? "FOUND" : "NOT FOUND");

    printf("\n--- Create index on 'name' ---\n");
    doc_create_index(store, "users", "name");
    printf("  Index created for users.name\n");

    printf("\n--- Build index ---\n");
    doc_rebuild_index(store, "users", "name");
    printf("  Index rebuilt\n");

    printf("\n--- Find products by city='Electronics' ---\n");
    n = doc_find_query(store, "products", "city", "Electronics", results, 16);
    printf("  Found %d products:\n", n);
    for (int i = 0; i < n; i++)
        printf("    doc_id=%s\n", results[i]->doc_id);

    doc_store_destroy(store);
    printf("\nDocument store demo complete.\n");
    return 0;
}
