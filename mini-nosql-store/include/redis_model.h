#ifndef REDIS_MODEL_H
#define REDIS_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define REDIS_MAX_KEY_LEN   64
#define REDIS_MAX_VALUE_LEN 256
#define REDIS_MAX_FIELD_LEN 64
#define REDIS_SET_BUCKETS   128
#define REDIS_ZSET_MAXLVL   12

typedef struct redis_list_node_t {
    char  value[REDIS_MAX_VALUE_LEN];
    struct redis_list_node_t *prev;
    struct redis_list_node_t *next;
} RedisListNode;

typedef struct redis_list_t {
    RedisListNode *head;
    RedisListNode *tail;
    int            length;
    char           key[REDIS_MAX_KEY_LEN];
} RedisList;

typedef struct redis_set_entry_t {
    char  value[REDIS_MAX_VALUE_LEN];
    struct redis_set_entry_t *next;
} RedisSetEntry;

typedef struct redis_set_t {
    RedisSetEntry *buckets[REDIS_SET_BUCKETS];
    int            count;
    char           key[REDIS_MAX_KEY_LEN];
} RedisSet;

typedef struct redis_zset_node_t {
    char    member[REDIS_MAX_VALUE_LEN];
    double  score;
    struct redis_zset_node_t *forward[REDIS_ZSET_MAXLVL];
    int     span[REDIS_ZSET_MAXLVL];
    struct redis_zset_node_t *backward;
} RedisZSetNode;

typedef struct redis_zset_t {
    RedisZSetNode *header;
    RedisZSetNode *tail;
    unsigned long  length;
    int            level;
    char           key[REDIS_MAX_KEY_LEN];
} RedisZSet;

typedef struct redis_hash_entry_t {
    char  field[REDIS_MAX_FIELD_LEN];
    char  value[REDIS_MAX_VALUE_LEN];
    struct redis_hash_entry_t *next;
} RedisHashEntry;

typedef struct redis_hash_t {
    RedisHashEntry *buckets[REDIS_SET_BUCKETS];
    int             count;
    char            key[REDIS_MAX_KEY_LEN];
} RedisHash;

RedisList *redis_list_create(const char *key);
void       redis_list_destroy(RedisList *list);
int        redis_lpush(RedisList *list, const char *value);
int        redis_rpush(RedisList *list, const char *value);
char      *redis_lpop(RedisList *list);
char      *redis_rpop(RedisList *list);
int        redis_lrange(RedisList *list, int start, int stop,
                        char (*values)[REDIS_MAX_VALUE_LEN], int max_values);
int        redis_llen(RedisList *list);
int        redis_lindex(RedisList *list, int index, char *value_out, size_t max_len);

RedisSet *redis_set_create(const char *key);
void      redis_set_destroy(RedisSet *set);
int       redis_sadd(RedisSet *set, const char *value);
int       redis_srem(RedisSet *set, const char *value);
int       redis_sismember(RedisSet *set, const char *value);
int       redis_smembers(RedisSet *set, char (*values)[REDIS_MAX_VALUE_LEN], int max_values);
int       redis_scard(RedisSet *set);
int       redis_sunion(RedisSet *a, RedisSet *b,
                       char (*values)[REDIS_MAX_VALUE_LEN], int max_values);
int       redis_sinter(RedisSet *a, RedisSet *b,
                       char (*values)[REDIS_MAX_VALUE_LEN], int max_values);

RedisZSet *redis_zset_create(const char *key);
void       redis_zset_destroy(RedisZSet *zset);
int        redis_zadd(RedisZSet *zset, double score, const char *member);
int        redis_zrem(RedisZSet *zset, const char *member);
double     redis_zscore(RedisZSet *zset, const char *member);
int        redis_zrange(RedisZSet *zset, long start, long stop,
                        char (*members)[REDIS_MAX_VALUE_LEN], int max_members);
int        redis_zrangebyscore(RedisZSet *zset, double min, double max,
                               char (*members)[REDIS_MAX_VALUE_LEN], int max_members);
int        redis_zrank(RedisZSet *zset, const char *member);
int        redis_zcard(RedisZSet *zset);

RedisHash *redis_hash_create(const char *key);
void       redis_hash_destroy(RedisHash *hash);
int        redis_hset(RedisHash *hash, const char *field, const char *value);
int        redis_hget(RedisHash *hash, const char *field, char *value_out, size_t max_len);
int        redis_hdel(RedisHash *hash, const char *field);
int        redis_hexists(RedisHash *hash, const char *field);
int        redis_hkeys(RedisHash *hash, char (*keys)[REDIS_MAX_FIELD_LEN], int max_keys);
int        redis_hvals(RedisHash *hash, char (*values)[REDIS_MAX_VALUE_LEN], int max_vals);
int        redis_hlen(RedisHash *hash);
int        redis_hgetall(RedisHash *hash,
                         char (*fields)[REDIS_MAX_FIELD_LEN],
                         char (*values)[REDIS_MAX_VALUE_LEN], int max_pairs);

#endif
