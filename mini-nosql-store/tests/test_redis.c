#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "redis_model.h"

int main(void) {
    /* List tests */
    RedisList *list = redis_list_create("testlist");
    assert(list != NULL);
    assert(redis_lpush(list, "world") == 1);
    assert(redis_lpush(list, "hello") == 2);
    assert(redis_rpush(list, "extra") == 3);
    assert(redis_llen(list) == 3);

    char buf[256];
    assert(redis_lindex(list, 0, buf, sizeof(buf)) == 0); /* hello */
    assert(strcmp(buf, "hello") == 0);
    assert(redis_lindex(list, 1, buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "world") == 0);

    char *popped = redis_lpop(list);
    assert(popped != NULL);
    assert(strcmp(popped, "hello") == 0);
    free(popped);

    popped = redis_rpop(list);
    assert(popped != NULL);
    assert(strcmp(popped, "extra") == 0);
    free(popped);

    redis_list_destroy(list);

    /* Set tests */
    RedisSet *set = redis_set_create("testset");
    assert(redis_sadd(set, "a") == 1);
    assert(redis_sadd(set, "b") == 1);
    assert(redis_sadd(set, "a") == 0); /* duplicate */
    assert(redis_scard(set) == 2);
    assert(redis_sismember(set, "a") == 1);
    assert(redis_sismember(set, "c") == 0);
    assert(redis_srem(set, "a") == 1);
    assert(redis_scard(set) == 1);

    RedisSet *set2 = redis_set_create("testset2");
    redis_sadd(set2, "b");
    redis_sadd(set2, "c");
    char members[16][REDIS_MAX_VALUE_LEN];
    int n = redis_sunion(set, set2, members, 16);
    assert(n >= 2);

    n = redis_sinter(set, set2, members, 16);
    assert(n == 1);

    redis_set_destroy(set);
    redis_set_destroy(set2);

    /* ZSet tests */
    RedisZSet *zset = redis_zset_create("testzset");
    assert(redis_zadd(zset, 10.0, "alice") == 1);
    assert(redis_zadd(zset, 20.0, "bob") == 1);
    assert(redis_zadd(zset, 5.0, "charlie") == 1);
    assert(redis_zcard(zset) == 3);

    double score = redis_zscore(zset, "bob");
    assert(score == 20.0);

    assert(redis_zrank(zset, "charlie") == 0);
    assert(redis_zrank(zset, "alice") == 1);
    assert(redis_zrank(zset, "bob") == 2);

    char zmembers[16][REDIS_MAX_VALUE_LEN];
    n = redis_zrange(zset, 0, 2, zmembers, 16);
    assert(n == 3);
    assert(strcmp(zmembers[0], "charlie") == 0);

    n = redis_zrangebyscore(zset, 10.0, 20.0, zmembers, 16);
    assert(n == 2);

    redis_zset_destroy(zset);

    /* Hash tests */
    RedisHash *hash = redis_hash_create("testhash");
    assert(redis_hset(hash, "name", "alice") == 1);
    assert(redis_hset(hash, "age", "28") == 1);
    assert(redis_hlen(hash) == 2);
    assert(redis_hexists(hash, "name") == 1);

    assert(redis_hget(hash, "name", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "alice") == 0);

    char fields[16][REDIS_MAX_FIELD_LEN];
    char values[16][REDIS_MAX_VALUE_LEN];
    n = redis_hkeys(hash, fields, 16);
    assert(n == 2);
    n = redis_hvals(hash, values, 16);
    assert(n == 2);
    n = redis_hgetall(hash, fields, values, 16);
    assert(n == 2);

    assert(redis_hdel(hash, "age") == 1);
    assert(redis_hlen(hash) == 1);

    redis_hash_destroy(hash);

    printf("test_redis: PASSED\n");
    return 0;
}
