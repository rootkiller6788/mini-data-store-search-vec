#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <stdint.h>
#include <stddef.h>

#define MAX_POSITIONS        32
#define MAX_TERM_LEN         64
#define HASH_MAP_SIZE        2048
#define POSTING_LIST_INIT_CAP 8

typedef struct {
    int32_t doc_id;
    int32_t term_freq;
    int32_t positions[MAX_POSITIONS];
    int32_t num_positions;
} PostingEntry;

typedef struct {
    PostingEntry *postings;
    int32_t num_docs;
    int32_t doc_freq;
    int32_t capacity;
} PostingList;

typedef struct {
    char         term[MAX_TERM_LEN];
    PostingList  list;
    int32_t      occupied;
} HashEntry;

typedef struct {
    HashEntry entries[HASH_MAP_SIZE];
    int32_t   num_terms;
    int32_t   total_docs;
} InvertedIndex;

void          index_init(InvertedIndex *index);
void          index_build(InvertedIndex *index, char **documents, int32_t num_docs);
void          index_add_doc(InvertedIndex *index, int32_t doc_id, const char **tokens,
                            const int32_t *token_count);
const PostingList *index_search_term(const InvertedIndex *index, const char *term);
void          index_print_term_stats(const InvertedIndex *index, const char *term);
void          index_free(InvertedIndex *index);

PostingList *posting_list_create(void);
void          posting_list_free(PostingList *pl);
void          posting_list_add(PostingList *pl, int32_t doc_id, int32_t position);
PostingList *posting_list_intersect(const PostingList *a, const PostingList *b);
PostingList *posting_list_union(const PostingList *a, const PostingList *b);
PostingList *posting_list_exclude(const PostingList *a, const PostingList *b);

#endif
