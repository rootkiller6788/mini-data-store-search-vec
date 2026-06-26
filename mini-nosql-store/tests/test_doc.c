#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "document_store.h"

static size_t make_bson(uint8_t *buf, const char *name, const char *city, int age) {
    memset(buf, 0, DOC_MAX_BSON_SIZE);
    size_t pos = 4;
    buf[pos++] = 0x02;
    const char *k = "name";
    strcpy((char *)(buf + pos), k); pos += strlen(k) + 1;
    uint32_t nlen = (uint32_t)strlen(name) + 1;
    memcpy(buf + pos, &nlen, 4); pos += 4;
    strcpy((char *)(buf + pos), name); pos += nlen;

    buf[pos++] = 0x02; k = "city";
    strcpy((char *)(buf + pos), k); pos += strlen(k) + 1;
    nlen = (uint32_t)strlen(city) + 1;
    memcpy(buf + pos, &nlen, 4); pos += 4;
    strcpy((char *)(buf + pos), city); pos += nlen;

    buf[pos++] = 0x10; k = "age";
    strcpy((char *)(buf + pos), k); pos += strlen(k) + 1;
    int32_t age32 = (int32_t)age;
    memcpy(buf + pos, &age32, 4); pos += 4;
    buf[pos++] = 0x00;
    uint32_t total = (uint32_t)pos;
    memcpy(buf, &total, 4);
    return pos;
}

int main(void) {
    DocumentStore *store = doc_store_create();
    assert(store != NULL);

    uint8_t bson[DOC_MAX_BSON_SIZE];
    size_t len;

    len = make_bson(bson, "Alice", "Beijing", 28);
    assert(doc_insert(store, "users", "u1", bson, len) == 0);
    len = make_bson(bson, "Bob", "Shanghai", 35);
    assert(doc_insert(store, "users", "u2", bson, len) == 0);

    Document *doc = doc_find(store, "users", "u1");
    assert(doc != NULL);
    assert(strcmp(doc->doc_id, "u1") == 0);

    Document *results[16];
    int n = doc_find_query(store, "users", "city", "Beijing", results, 16);
    assert(n == 1);

    n = doc_find_range(store, "users", "age", "30", "40", results, 16);
    assert(n == 1);

    /* Update test */
    len = make_bson(bson, "Alice New", "Hangzhou", 29);
    assert(doc_update(store, "users", "u1", bson, len) == 0);

    /* Delete test */
    assert(doc_delete(store, "users", "u2") == 0);
    assert(doc_find(store, "users", "u2") == NULL);

    assert(doc_get_collection(store, "users") != NULL);
    assert(doc_collection_count(store) >= 1);

    doc_store_destroy(store);
    printf("test_doc: PASSED\n");
    return 0;
}
