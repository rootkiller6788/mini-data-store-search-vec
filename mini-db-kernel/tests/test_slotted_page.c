#include "slotted_page.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) { bool _ok = true; do
#define END_TEST(name) while(0); \
    if (_ok) { printf("  PASS %s\n", name); tests_passed++; } \
    else { printf("  FAIL %s\n", name); tests_failed++; } }

#define CHECK(cond, msg) if (!(cond)) { printf("    ! %s\n", msg); _ok = false; }

void test_sp_init(void) {
    TEST("sp_init_page") {
        SlottedPage page;
        sp_init_page(&page, 42);
        CHECK(page.header.page_id == 42, "page_id should be 42");
        CHECK(page.header.slot_count == 0, "slot_count should be 0");
        CHECK(page.header.tuple_count == 0, "tuple_count should be 0");
        CHECK(sp_get_free_space(&page) == SP_PAGE_SIZE - SP_HEADER_SIZE,
              "free space should be page_size - header_size");
    } END_TEST("sp_init_page");
}

void test_sp_insert_get(void) {
    TEST("sp_insert_tuple and sp_get_tuple") {
        SlottedPage page;
        sp_init_page(&page, 1);
        uint8_t data[] = "hello world";
        int32_t slot;
        CHECK(sp_insert_tuple(&page, data, 12, &slot), "insert should succeed");
        CHECK(slot == 0, "first slot should be 0");
        CHECK(page.header.tuple_count == 1, "tuple_count should be 1");

        uint8_t out[64];
        int32_t out_len;
        CHECK(sp_get_tuple(&page, 0, out, &out_len), "get should succeed");
        CHECK(out_len == 12, "length should be 12");
        CHECK(memcmp(out, data, 12) == 0, "data should match");
    } END_TEST("sp_insert_tuple and sp_get_tuple");
}

void test_sp_multiple_inserts(void) {
    TEST("multiple inserts") {
        SlottedPage page;
        sp_init_page(&page, 2);
        for (int32_t i = 0; i < 50; i++) {
            uint8_t buf[16];
            memset(buf, (uint8_t)(i + 1), sizeof(buf));
            int32_t slot;
            CHECK(sp_insert_tuple(&page, buf, 16, &slot), "insert should succeed");
            CHECK(slot == i, "slot index should match");
        }
        CHECK(page.header.tuple_count == 50, "should have 50 tuples");

        /* verify each tuple */
        for (int32_t i = 0; i < 50; i++) {
            uint8_t out[16];
            int32_t out_len;
            CHECK(sp_get_tuple(&page, i, out, &out_len), "get should succeed");
            CHECK(out_len == 16, "length should be 16");
            CHECK(out[0] == (uint8_t)(i + 1), "first byte should match");
        }
    } END_TEST("multiple inserts");
}

void test_sp_delete(void) {
    TEST("sp_delete_tuple") {
        SlottedPage page;
        sp_init_page(&page, 3);
        uint8_t data[] = "test data";
        int32_t slot;
        sp_insert_tuple(&page, data, 10, &slot);

        CHECK(sp_delete_tuple(&page, 0), "delete should succeed");
        CHECK(page.header.tuple_count == 0, "tuple_count should be 0 after delete");

        uint8_t out[64];
        int32_t out_len;
        CHECK(!sp_get_tuple(&page, 0, out, &out_len),
              "get on deleted tuple should fail");
    } END_TEST("sp_delete_tuple");
}

void test_sp_update_in_place(void) {
    TEST("sp_update_tuple (in-place)") {
        SlottedPage page;
        sp_init_page(&page, 4);
        uint8_t orig[] = "original data here";
        int32_t slot;
        sp_insert_tuple(&page, orig, 18, &slot);

        uint8_t new_data[] = "updated";
        CHECK(sp_update_tuple(&page, 0, new_data, 8), "update should succeed");

        uint8_t out[64];
        int32_t out_len;
        CHECK(sp_get_tuple(&page, 0, out, &out_len), "get after update should succeed");
        CHECK(out_len == 8, "length should be new length");
        CHECK(memcmp(out, new_data, 8) == 0, "data should be updated");
    } END_TEST("sp_update_tuple (in-place)");
}

void test_sp_compact(void) {
    TEST("sp_compact after deletes") {
        SlottedPage page;
        sp_init_page(&page, 5);
        for (int32_t i = 0; i < 10; i++) {
            uint8_t buf[8];
            memset(buf, (uint8_t)i, sizeof(buf));
            int32_t slot;
            sp_insert_tuple(&page, buf, 8, &slot);
        }
        /* delete even slots */
        sp_delete_tuple(&page, 0);
        sp_delete_tuple(&page, 2);
        sp_delete_tuple(&page, 4);
        sp_delete_tuple(&page, 6);
        sp_delete_tuple(&page, 8);
        CHECK(page.header.tuple_count == 5, "5 tuples after deletes");

        int32_t free_before = sp_get_free_space(&page);
        CHECK(sp_compact(&page), "compact should succeed");
        int32_t free_after = sp_get_free_space(&page);
        /* After compact, fragmented space should be reclaimed */
        CHECK(free_after >= free_before, "free space should not decrease after compact");

        /* verify active tuples still accessible */
        uint8_t out[8];
        int32_t out_len;
        CHECK(sp_get_tuple(&page, 1, out, &out_len), "slot 1 should still be valid");
        CHECK(sp_get_tuple(&page, 3, out, &out_len), "slot 3 should still be valid");
    } END_TEST("sp_compact after deletes");
}

static bool visitor_func(int32_t slot_id, const uint8_t *data, int32_t size, void *ctx) {
    int32_t *count = (int32_t *)ctx;
    (void)slot_id;
    (void)data;
    (void)size;
    (*count)++;
    return true;
}

void test_sp_iterate(void) {
    TEST("sp_iterate_tuples") {
        SlottedPage page;
        sp_init_page(&page, 6);
        for (int32_t i = 0; i < 5; i++) {
            uint8_t buf[4];
            int32_t slot;
            sp_insert_tuple(&page, buf, 4, &slot);
        }
        int32_t count = 0;
        int32_t visited = sp_iterate_tuples(&page, visitor_func, &count);
        CHECK(visited == 5, "should visit 5 tuples");
        CHECK(count == 5, "count should be 5");
    } END_TEST("sp_iterate_tuples");
}

void test_sp_copy(void) {
    TEST("sp_copy_page") {
        SlottedPage src, dst;
        sp_init_page(&src, 10);
        uint8_t data[] = "copy test";
        int32_t slot;
        sp_insert_tuple(&src, data, 10, &slot);

        CHECK(sp_copy_page(&src, &dst), "copy should succeed");
        CHECK(dst.header.page_id == 10, "page_id should match");
        CHECK(dst.header.tuple_count == 1, "tuple_count should match");

        uint8_t out[64];
        int32_t out_len;
        CHECK(sp_get_tuple(&dst, 0, out, &out_len), "get on copy should work");
        CHECK(memcmp(out, data, 10) == 0, "data should match");
    } END_TEST("sp_copy_page");
}

void test_sp_null(void) {
    TEST("null pointer safety") {
        CHECK(!sp_insert_tuple(NULL, (uint8_t*)"test", 4, NULL), "insert NULL page should fail");
        CHECK(!sp_get_tuple(NULL, 0, NULL, NULL), "get NULL page should fail");
        CHECK(!sp_delete_tuple(NULL, 0), "delete NULL page should fail");
        CHECK(!sp_update_tuple(NULL, 0, NULL, 0), "update NULL page should fail");
        CHECK(sp_get_free_space(NULL) == 0, "free space of NULL should be 0");
        CHECK(sp_get_used_space(NULL) == 0, "used space of NULL should be 0");
    } END_TEST("null pointer safety");
}

int main(void) {
    printf("=== Slotted Page Tests ===\n");
    test_sp_init();
    test_sp_insert_get();
    test_sp_multiple_inserts();
    test_sp_delete();
    test_sp_update_in_place();
    test_sp_compact();
    test_sp_iterate();
    test_sp_copy();
    test_sp_null();
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
