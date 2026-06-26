#include "query_exec.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int passed = 0, failed = 0;
#define TEST(n) { bool ok = true; do
#define END(n) while(0); if (ok) { printf("  PASS %s\n", n); passed++; } else { printf("  FAIL %s\n", n); failed++; } }
#define CHK(c, m) if (!(c)) { printf("    ! %s\n", m); ok = false; }

static TableSchema person_schema;
static uint8_t person_buf[4096];

static void init_test_data(void) {
    memset(&person_schema, 0, sizeof(person_schema));
    memset(person_buf, 0, sizeof(person_buf));
    person_schema.num_columns = 3;
    person_schema.tuple_size = 0;

    strcpy(person_schema.columns[0].name, "id");
    person_schema.columns[0].type = QE_TYPE_INT32;
    person_schema.columns[0].offset = person_schema.tuple_size;
    person_schema.columns[0].length = sizeof(int32_t);
    person_schema.tuple_size += sizeof(int32_t);

    strcpy(person_schema.columns[1].name, "age");
    person_schema.columns[1].type = QE_TYPE_INT32;
    person_schema.columns[1].offset = person_schema.tuple_size;
    person_schema.columns[1].length = sizeof(int32_t);
    person_schema.tuple_size += sizeof(int32_t);

    strcpy(person_schema.columns[2].name, "name");
    person_schema.columns[2].type = QE_TYPE_STRING;
    person_schema.columns[2].offset = person_schema.tuple_size;
    person_schema.columns[2].length = 20;
    person_schema.tuple_size += 20;

    int32_t ids[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int32_t ages[] = {25, 30, 22, 35, 28, 45, 19, 33, 27, 31};
    const char *names[] = {"Alice", "Bob", "Charlie", "Diana", "Eve",
                           "Frank", "Grace", "Henry", "Iris", "Jack"};

    uint8_t *buf = person_buf;
    for (int32_t i = 0; i < 10; i++) {
        int32_t off = i * person_schema.tuple_size;
        memcpy(buf + off, &ids[i], sizeof(int32_t));
        memcpy(buf + off + sizeof(int32_t), &ages[i], sizeof(int32_t));
        memcpy(buf + off + 2 * sizeof(int32_t), names[i], strlen(names[i]) + 1);
    }
}

static bool age_gt_30(const QETuple *t, const TableSchema *s) {
    int32_t age = qe_get_column_value_int32(t, s, 1);
    return age > 30;
}

int main(void) {
    printf("=== Query Execution Tests ===\n");
    init_test_data();

    TEST("seqscan") {
        QEOperator *scan = qe_seqscan_create((const uint8_t *)person_buf, 10,
                                              person_schema.tuple_size, &person_schema);
        CHK(scan != NULL, "seqscan should be created");
        scan->open(scan);
        int32_t count = 0;
        QETuple *t;
        while ((t = scan->next(scan))) {
            int32_t id = qe_get_column_value_int32(t, &person_schema, 0);
            CHK(id >= 1 && id <= 10, "id should be in range");
            count++;
            /* tuple freed by close */
        }
        CHK(count == 10, "should read 10 tuples");
        scan->close(scan);
        qe_destroy_operator(scan);
    } END("seqscan");

    TEST("filter (age > 30)") {
        QEOperator *scan = qe_seqscan_create((const uint8_t *)person_buf, 10,
                                              person_schema.tuple_size, &person_schema);
        QEOperator *filter = qe_filter_create(scan, &person_schema, age_gt_30);
        CHK(filter != NULL, "filter should be created");
        filter->open(filter);
        int32_t count = 0;
        QETuple *t;
        while ((t = filter->next(filter))) {
            int32_t age = qe_get_column_value_int32(t, &person_schema, 1);
            CHK(age > 30, "filtered age should be > 30");
            count++;
            /* tuple freed by close */
        }
        CHK(count > 0, "should have some tuples with age > 30");
        filter->close(filter);
        qe_destroy_operator(filter);
    } END("filter (age > 30)");

    TEST("project (id, age)") {
        QEOperator *scan = qe_seqscan_create((const uint8_t *)person_buf, 10,
                                              person_schema.tuple_size, &person_schema);
        TableSchema out_schema;
        memset(&out_schema, 0, sizeof(out_schema));
        out_schema.num_columns = 2;
        out_schema.tuple_size = 0;
        strcpy(out_schema.columns[0].name, "id");
        out_schema.columns[0].type = QE_TYPE_INT32;
        out_schema.columns[0].offset = out_schema.tuple_size;
        out_schema.columns[0].length = sizeof(int32_t);
        out_schema.tuple_size += sizeof(int32_t);
        strcpy(out_schema.columns[1].name, "age");
        out_schema.columns[1].type = QE_TYPE_INT32;
        out_schema.columns[1].offset = out_schema.tuple_size;
        out_schema.columns[1].length = sizeof(int32_t);
        out_schema.tuple_size += sizeof(int32_t);
        int32_t cols[] = {0, 1};
        QEOperator *proj = qe_project_create(scan, &person_schema, &out_schema, cols, 2);
        CHK(proj != NULL, "project should be created");
        proj->open(proj);
        QETuple *t = proj->next(proj);
        CHK(t != NULL, "should get at least one tuple");
        if (t) {
            int32_t id = qe_get_column_value_int32(t, &out_schema, 0);
            CHK(id == 1, "first id should be 1");
            /* tuple freed by close */
        }
        proj->close(proj);
        qe_destroy_operator(proj);
    } END("project (id, age)");

    TEST("limit operator") {
        QEOperator *scan = qe_seqscan_create((const uint8_t *)person_buf, 10,
                                              person_schema.tuple_size, &person_schema);
        QEOperator *limit = qe_limit_create(scan, 3);
        CHK(limit != NULL, "limit should be created");
        limit->open(limit);
        int32_t count = 0;
        QETuple *t;
        while ((t = limit->next(limit))) { count++; /* freed by close */ }
        CHK(count == 3, "should emit exactly 3 tuples");
        limit->close(limit);
        qe_destroy_operator(limit);
    } END("limit operator");

    TEST("compound: scan -> filter -> limit") {
        QEOperator *scan = qe_seqscan_create((const uint8_t *)person_buf, 10,
                                              person_schema.tuple_size, &person_schema);
        QEOperator *filter = qe_filter_create(scan, &person_schema, age_gt_30);
        QEOperator *limit = qe_limit_create(filter, 2);
        limit->open(limit);
        int32_t count = 0;
        QETuple *t;
        while ((t = limit->next(limit))) {
            int32_t age = qe_get_column_value_int32(t, &person_schema, 1);
            CHK(age > 30, "should satisfy filter");
            count++;
            /* tuple freed by close */
        }
        limit->close(limit);
        qe_destroy_operator(limit);
    } END("compound: scan -> filter -> limit");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}