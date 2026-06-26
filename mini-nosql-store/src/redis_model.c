#include "redis_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

RedisList *redis_list_create(const char *key) {
    RedisList *list = (RedisList *)calloc(1, sizeof(RedisList));
    if (!list) return NULL;
    if (key) { strncpy(list->key, key, REDIS_MAX_KEY_LEN - 1); }
    return list;
}

void redis_list_destroy(RedisList *list) {
    if (!list) return;
    RedisListNode *cur = list->head;
    while (cur) {
        RedisListNode *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    free(list);
}

int redis_lpush(RedisList *list, const char *value) {
    if (!list || !value) return -1;
    RedisListNode *node = (RedisListNode *)calloc(1, sizeof(RedisListNode));
    if (!node) return -1;
    strncpy(node->value, value, REDIS_MAX_VALUE_LEN - 1);
    node->value[REDIS_MAX_VALUE_LEN - 1] = '\0';
    node->next = list->head;
    if (list->head) list->head->prev = node;
    list->head = node;
    if (!list->tail) list->tail = node;
    list->length++;
    return list->length;
}

int redis_rpush(RedisList *list, const char *value) {
    if (!list || !value) return -1;
    RedisListNode *node = (RedisListNode *)calloc(1, sizeof(RedisListNode));
    if (!node) return -1;
    strncpy(node->value, value, REDIS_MAX_VALUE_LEN - 1);
    node->value[REDIS_MAX_VALUE_LEN - 1] = '\0';
    node->prev = list->tail;
    if (list->tail) list->tail->next = node;
    list->tail = node;
    if (!list->head) list->head = node;
    list->length++;
    return list->length;
}

char *redis_lpop(RedisList *list) {
    if (!list || !list->head) return NULL;
    RedisListNode *node = list->head;
    list->head = node->next;
    if (list->head) list->head->prev = NULL;
    else list->tail = NULL;
    char *val = (char *)malloc(REDIS_MAX_VALUE_LEN);
    if (val) { strncpy(val, node->value, REDIS_MAX_VALUE_LEN - 1); }
    free(node);
    list->length--;
    return val;
}

char *redis_rpop(RedisList *list) {
    if (!list || !list->tail) return NULL;
    RedisListNode *node = list->tail;
    list->tail = node->prev;
    if (list->tail) list->tail->next = NULL;
    else list->head = NULL;
    char *val = (char *)malloc(REDIS_MAX_VALUE_LEN);
    if (val) { strncpy(val, node->value, REDIS_MAX_VALUE_LEN - 1); }
    free(node);
    list->length--;
    return val;
}

int redis_lrange(RedisList *list, int start, int stop,
                 char (*values)[REDIS_MAX_VALUE_LEN], int max_values) {
    if (!list || !values) return 0;
    int len = list->length;
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;
    if (start > stop) return 0;

    RedisListNode *cur = list->head;
    int idx = 0;
    int found = 0;
    while (cur && found < max_values) {
        if (idx >= start && idx <= stop) {
            strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
            values[found][REDIS_MAX_VALUE_LEN - 1] = '\0';
            found++;
        }
        cur = cur->next;
        idx++;
    }
    return found;
}

int redis_llen(RedisList *list) {
    return list ? list->length : 0;
}

int redis_lindex(RedisList *list, int index, char *value_out, size_t max_len) {
    if (!list || !value_out) return -1;
    int len = list->length;
    if (index < 0) index = len + index;
    if (index < 0 || index >= len) return -2;
    RedisListNode *cur = list->head;
    for (int i = 0; i < index && cur; i++) cur = cur->next;
    if (cur) {
        strncpy(value_out, cur->value, max_len - 1);
        value_out[max_len - 1] = '\0';
        return 0;
    }
    return -2;
}

static unsigned int redis_set_hash(const char *s) {
    unsigned int h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;
    return h % REDIS_SET_BUCKETS;
}

RedisSet *redis_set_create(const char *key) {
    RedisSet *set = (RedisSet *)calloc(1, sizeof(RedisSet));
    if (!set) return NULL;
    if (key) { strncpy(set->key, key, REDIS_MAX_KEY_LEN - 1); }
    return set;
}

void redis_set_destroy(RedisSet *set) {
    if (!set) return;
    for (int i = 0; i < REDIS_SET_BUCKETS; i++) {
        RedisSetEntry *cur = set->buckets[i];
        while (cur) {
            RedisSetEntry *tmp = cur;
            cur = cur->next;
            free(tmp);
        }
    }
    free(set);
}

int redis_sadd(RedisSet *set, const char *value) {
    if (!set || !value) return -1;
    unsigned int h = redis_set_hash(value);
    RedisSetEntry *cur = set->buckets[h];
    while (cur) {
        if (strcmp(cur->value, value) == 0) return 0;
        cur = cur->next;
    }
    RedisSetEntry *entry = (RedisSetEntry *)calloc(1, sizeof(RedisSetEntry));
    if (!entry) return -1;
    strncpy(entry->value, value, REDIS_MAX_VALUE_LEN - 1);
    entry->value[REDIS_MAX_VALUE_LEN - 1] = '\0';
    entry->next = set->buckets[h];
    set->buckets[h] = entry;
    set->count++;
    return 1;
}

int redis_srem(RedisSet *set, const char *value) {
    if (!set || !value) return -1;
    unsigned int h = redis_set_hash(value);
    RedisSetEntry *cur = set->buckets[h];
    RedisSetEntry *prev = NULL;
    while (cur) {
        if (strcmp(cur->value, value) == 0) {
            if (prev) prev->next = cur->next;
            else set->buckets[h] = cur->next;
            free(cur);
            set->count--;
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

int redis_sismember(RedisSet *set, const char *value) {
    if (!set || !value) return 0;
    unsigned int h = redis_set_hash(value);
    RedisSetEntry *cur = set->buckets[h];
    while (cur) {
        if (strcmp(cur->value, value) == 0) return 1;
        cur = cur->next;
    }
    return 0;
}

int redis_smembers(RedisSet *set, char (*values)[REDIS_MAX_VALUE_LEN], int max_values) {
    if (!set || !values) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_values; i++) {
        RedisSetEntry *cur = set->buckets[i];
        while (cur && found < max_values) {
            strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
            values[found][REDIS_MAX_VALUE_LEN - 1] = '\0';
            found++;
            cur = cur->next;
        }
    }
    return found;
}

int redis_scard(RedisSet *set) {
    return set ? set->count : 0;
}

int redis_sunion(RedisSet *a, RedisSet *b,
                 char (*values)[REDIS_MAX_VALUE_LEN], int max_values) {
    if (!a || !b || !values) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_values; i++) {
        RedisSetEntry *cur = a->buckets[i];
        while (cur && found < max_values) {
            strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
            found++;
            cur = cur->next;
        }
    }
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_values; i++) {
        RedisSetEntry *cur = b->buckets[i];
        while (cur && found < max_values) {
            if (!redis_sismember(a, cur->value)) {
                strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
                found++;
            }
            cur = cur->next;
        }
    }
    return found;
}

int redis_sinter(RedisSet *a, RedisSet *b,
                 char (*values)[REDIS_MAX_VALUE_LEN], int max_values) {
    if (!a || !b || !values) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_values; i++) {
        RedisSetEntry *cur = a->buckets[i];
        while (cur && found < max_values) {
            if (redis_sismember(b, cur->value)) {
                strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
                found++;
            }
            cur = cur->next;
        }
    }
    return found;
}

static int zset_random_level(void) {
    int lvl = 0;
    while (lvl < REDIS_ZSET_MAXLVL - 1 && (rand() % 4) == 0) lvl++;
    return lvl;
}

RedisZSet *redis_zset_create(const char *key) {
    RedisZSet *zset = (RedisZSet *)calloc(1, sizeof(RedisZSet));
    if (!zset) return NULL;
    if (key) { strncpy(zset->key, key, REDIS_MAX_KEY_LEN - 1); }
    zset->header = (RedisZSetNode *)calloc(1, sizeof(RedisZSetNode));
    if (!zset->header) { free(zset); return NULL; }
    zset->header->score = -INFINITY;
    for (int i = 0; i < REDIS_ZSET_MAXLVL; i++)
        zset->header->forward[i] = NULL;
    zset->level = 0;
    return zset;
}

void redis_zset_destroy(RedisZSet *zset) {
    if (!zset) return;
    RedisZSetNode *cur = zset->header->forward[0];
    while (cur) {
        RedisZSetNode *tmp = cur;
        cur = cur->forward[0];
        free(tmp);
    }
    free(zset->header);
    free(zset);
}

int redis_zadd(RedisZSet *zset, double score, const char *member) {
    if (!zset || !member) return -1;
    RedisZSetNode *update[REDIS_ZSET_MAXLVL] = {0};
    RedisZSetNode *cur = zset->header;
    for (int i = REDIS_ZSET_MAXLVL - 1; i >= 0; i--) {
        while (cur->forward[i] && cur->forward[i]->score < score)
            cur = cur->forward[i];
        update[i] = cur;
    }
    cur = cur->forward[0];
    if (cur && cur->score == score && strcmp(cur->member, member) == 0) {
        cur->score = score;
        return 0;
    }

    int lvl = zset_random_level();
    if (lvl > zset->level) {
        for (int i = zset->level + 1; i <= lvl; i++)
            update[i] = zset->header;
        zset->level = lvl;
    }

    RedisZSetNode *node = (RedisZSetNode *)calloc(1, sizeof(RedisZSetNode));
    if (!node) return -1;
    node->score = score;
    strncpy(node->member, member, REDIS_MAX_VALUE_LEN - 1);
    node->member[REDIS_MAX_VALUE_LEN - 1] = '\0';
    for (int i = 0; i <= lvl; i++) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }
    node->backward = (update[0] == zset->header) ? NULL : update[0];
    if (node->forward[0])
        node->forward[0]->backward = node;
    else
        zset->tail = node;
    zset->length++;
    return 1;
}

int redis_zrem(RedisZSet *zset, const char *member) {
    if (!zset || !member) return -1;
    RedisZSetNode *update[REDIS_ZSET_MAXLVL] = {0};
    RedisZSetNode *cur = zset->header;
    for (int i = REDIS_ZSET_MAXLVL - 1; i >= 0; i--) {
        while (cur->forward[i] && strcmp(cur->forward[i]->member, member) < 0)
            cur = cur->forward[i];
        update[i] = cur;
    }
    cur = cur->forward[0];
    if (cur && strcmp(cur->member, member) == 0) {
        for (int i = 0; i <= zset->level; i++) {
            if (update[i]->forward[i] != cur) break;
            update[i]->forward[i] = cur->forward[i];
        }
        if (cur->forward[0]) cur->forward[0]->backward = cur->backward;
        else zset->tail = cur->backward;
        while (zset->level > 0 && zset->header->forward[zset->level] == NULL)
            zset->level--;
        free(cur);
        zset->length--;
        return 1;
    }
    return 0;
}

double redis_zscore(RedisZSet *zset, const char *member) {
    if (!zset || !member) return -INFINITY;
    RedisZSetNode *cur = zset->header->forward[0];
    while (cur) {
        if (strcmp(cur->member, member) == 0) return cur->score;
        cur = cur->forward[0];
    }
    return -INFINITY;
}

int redis_zrange(RedisZSet *zset, long start, long stop,
                 char (*members)[REDIS_MAX_VALUE_LEN], int max_members) {
    if (!zset || !members) return 0;
    long len = (long)zset->length;
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;
    if (start > stop) return 0;

    RedisZSetNode *cur = zset->header->forward[0];
    long idx = 0;
    int found = 0;
    while (cur && idx <= stop && found < max_members) {
        if (idx >= start) {
            strncpy(members[found], cur->member, REDIS_MAX_VALUE_LEN - 1);
            members[found][REDIS_MAX_VALUE_LEN - 1] = '\0';
            found++;
        }
        cur = cur->forward[0];
        idx++;
    }
    return found;
}

int redis_zrangebyscore(RedisZSet *zset, double min, double max,
                        char (*members)[REDIS_MAX_VALUE_LEN], int max_members) {
    if (!zset || !members) return 0;
    RedisZSetNode *cur = zset->header->forward[0];
    int found = 0;
    while (cur && found < max_members) {
        if (cur->score >= min && cur->score <= max) {
            strncpy(members[found], cur->member, REDIS_MAX_VALUE_LEN - 1);
            members[found][REDIS_MAX_VALUE_LEN - 1] = '\0';
            found++;
        }
        cur = cur->forward[0];
    }
    return found;
}

int redis_zrank(RedisZSet *zset, const char *member) {
    if (!zset || !member) return -1;
    RedisZSetNode *cur = zset->header->forward[0];
    int rank = 0;
    while (cur) {
        if (strcmp(cur->member, member) == 0) return rank;
        cur = cur->forward[0];
        rank++;
    }
    return -1;
}

int redis_zcard(RedisZSet *zset) {
    return zset ? (int)zset->length : 0;
}

RedisHash *redis_hash_create(const char *key) {
    RedisHash *hash = (RedisHash *)calloc(1, sizeof(RedisHash));
    if (!hash) return NULL;
    if (key) { strncpy(hash->key, key, REDIS_MAX_KEY_LEN - 1); }
    return hash;
}

void redis_hash_destroy(RedisHash *hash) {
    if (!hash) return;
    for (int i = 0; i < REDIS_SET_BUCKETS; i++) {
        RedisHashEntry *cur = hash->buckets[i];
        while (cur) {
            RedisHashEntry *tmp = cur;
            cur = cur->next;
            free(tmp);
        }
    }
    free(hash);
}

int redis_hset(RedisHash *hash, const char *field, const char *value) {
    if (!hash || !field || !value) return -1;
    unsigned int h = redis_set_hash(field);
    RedisHashEntry *cur = hash->buckets[h];
    while (cur) {
        if (strcmp(cur->field, field) == 0) {
            strncpy(cur->value, value, REDIS_MAX_VALUE_LEN - 1);
            cur->value[REDIS_MAX_VALUE_LEN - 1] = '\0';
            return 0;
        }
        cur = cur->next;
    }
    RedisHashEntry *entry = (RedisHashEntry *)calloc(1, sizeof(RedisHashEntry));
    if (!entry) return -1;
    strncpy(entry->field, field, REDIS_MAX_FIELD_LEN - 1);
    strncpy(entry->value, value, REDIS_MAX_VALUE_LEN - 1);
    entry->next = hash->buckets[h];
    hash->buckets[h] = entry;
    hash->count++;
    return 1;
}

int redis_hget(RedisHash *hash, const char *field, char *value_out, size_t max_len) {
    if (!hash || !field || !value_out) return -1;
    unsigned int h = redis_set_hash(field);
    RedisHashEntry *cur = hash->buckets[h];
    while (cur) {
        if (strcmp(cur->field, field) == 0) {
            strncpy(value_out, cur->value, max_len - 1);
            value_out[max_len - 1] = '\0';
            return 0;
        }
        cur = cur->next;
    }
    return -2;
}

int redis_hdel(RedisHash *hash, const char *field) {
    if (!hash || !field) return -1;
    unsigned int h = redis_set_hash(field);
    RedisHashEntry *cur = hash->buckets[h];
    RedisHashEntry *prev = NULL;
    while (cur) {
        if (strcmp(cur->field, field) == 0) {
            if (prev) prev->next = cur->next;
            else hash->buckets[h] = cur->next;
            free(cur);
            hash->count--;
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

int redis_hexists(RedisHash *hash, const char *field) {
    if (!hash || !field) return 0;
    unsigned int h = redis_set_hash(field);
    RedisHashEntry *cur = hash->buckets[h];
    while (cur) {
        if (strcmp(cur->field, field) == 0) return 1;
        cur = cur->next;
    }
    return 0;
}

int redis_hkeys(RedisHash *hash, char (*keys)[REDIS_MAX_FIELD_LEN], int max_keys) {
    if (!hash || !keys) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_keys; i++) {
        RedisHashEntry *cur = hash->buckets[i];
        while (cur && found < max_keys) {
            strncpy(keys[found], cur->field, REDIS_MAX_FIELD_LEN - 1);
            found++;
            cur = cur->next;
        }
    }
    return found;
}

int redis_hvals(RedisHash *hash, char (*values)[REDIS_MAX_VALUE_LEN], int max_vals) {
    if (!hash || !values) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_vals; i++) {
        RedisHashEntry *cur = hash->buckets[i];
        while (cur && found < max_vals) {
            strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
            found++;
            cur = cur->next;
        }
    }
    return found;
}

int redis_hlen(RedisHash *hash) {
    return hash ? hash->count : 0;
}

int redis_hgetall(RedisHash *hash,
                  char (*fields)[REDIS_MAX_FIELD_LEN],
                  char (*values)[REDIS_MAX_VALUE_LEN], int max_pairs) {
    if (!hash || !fields || !values) return 0;
    int found = 0;
    for (int i = 0; i < REDIS_SET_BUCKETS && found < max_pairs; i++) {
        RedisHashEntry *cur = hash->buckets[i];
        while (cur && found < max_pairs) {
            strncpy(fields[found], cur->field, REDIS_MAX_FIELD_LEN - 1);
            strncpy(values[found], cur->value, REDIS_MAX_VALUE_LEN - 1);
            found++;
            cur = cur->next;
        }
    }
    return found;
}
