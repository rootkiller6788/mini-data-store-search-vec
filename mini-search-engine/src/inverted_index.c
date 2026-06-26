#include "inverted_index.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t hash_string(const char *str) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        h = ((h << 5) + h) + c;
    return h;
}

void index_init(InvertedIndex *index) {
    int32_t i;
    index->num_terms = 0;
    index->total_docs = 0;
    for (i = 0; i < HASH_MAP_SIZE; i++) {
        index->entries[i].occupied = 0;
        index->entries[i].term[0] = '\0';
        index->entries[i].list.postings = NULL;
        index->entries[i].list.num_docs = 0;
        index->entries[i].list.doc_freq = 0;
        index->entries[i].list.capacity = 0;
    }
}

static HashEntry *index_find_or_create(InvertedIndex *index, const char *term) {
    uint32_t h = hash_string(term);
    int32_t i, slot;

    for (i = 0; i < HASH_MAP_SIZE; i++) {
        slot = (int32_t)((h + i) % HASH_MAP_SIZE);
        if (!index->entries[slot].occupied) {
            strncpy(index->entries[slot].term, term, MAX_TERM_LEN - 1);
            index->entries[slot].term[MAX_TERM_LEN - 1] = '\0';
            index->entries[slot].occupied = 1;
            index->num_terms++;
            return &index->entries[slot];
        }
        if (index->entries[slot].occupied &&
            strcmp(index->entries[slot].term, term) == 0) {
            return &index->entries[slot];
        }
    }
    return NULL;
}

static const HashEntry *index_find(const InvertedIndex *index, const char *term) {
    uint32_t h = hash_string(term);
    int32_t i, slot;

    for (i = 0; i < HASH_MAP_SIZE; i++) {
        slot = (int32_t)((h + i) % HASH_MAP_SIZE);
        if (!index->entries[slot].occupied)
            return NULL;
        if (index->entries[slot].occupied &&
            strcmp(index->entries[slot].term, term) == 0) {
            return &index->entries[slot];
        }
    }
    return NULL;
}

PostingList *posting_list_create(void) {
    PostingList *pl = (PostingList *)malloc(sizeof(PostingList));
    if (!pl) return NULL;
    pl->capacity = POSTING_LIST_INIT_CAP;
    pl->num_docs = 0;
    pl->doc_freq = 0;
    pl->postings = (PostingEntry *)calloc((size_t)pl->capacity, sizeof(PostingEntry));
    if (!pl->postings) {
        free(pl);
        return NULL;
    }
    return pl;
}

void posting_list_free(PostingList *pl) {
    if (!pl) return;
    free(pl->postings);
    free(pl);
}

void posting_list_add(PostingList *pl, int32_t doc_id, int32_t position) {
    int32_t i;

    for (i = 0; i < pl->num_docs; i++) {
        if (pl->postings[i].doc_id == doc_id) {
            PostingEntry *pe = &pl->postings[i];
            pe->term_freq++;
            if (pe->num_positions < MAX_POSITIONS) {
                pe->positions[pe->num_positions++] = position;
            }
            return;
        }
    }

    if (pl->num_docs >= pl->capacity) {
        int32_t new_cap = pl->capacity > 0 ? pl->capacity * 2 : POSTING_LIST_INIT_CAP;
        PostingEntry *new_posts = (PostingEntry *)realloc(pl->postings,
            (size_t)new_cap * sizeof(PostingEntry));
        if (!new_posts) return;
        pl->postings = new_posts;
        pl->capacity = new_cap;
    }

    PostingEntry *pe = &pl->postings[pl->num_docs];
    pe->doc_id = doc_id;
    pe->term_freq = 1;
    pe->num_positions = 1;
    pe->positions[0] = position;
    pl->num_docs++;
    pl->doc_freq++;
}

void index_build(InvertedIndex *index, char **documents, int32_t num_docs) {
    index_init(index);
    index->total_docs = num_docs;

    int32_t d;
    for (d = 0; d < num_docs; d++) {
        const char *doc = documents[d];
        const char *p = doc;
        int32_t pos = 0;
        char token_buf[MAX_TERM_LEN];

        while (*p) {
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                p++;
            if (!*p) break;

            int32_t ti = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' &&
                   ti < MAX_TERM_LEN - 1) {
                if (*p >= 'A' && *p <= 'Z')
                    token_buf[ti++] = (char)(*p + 32);
                else
                    token_buf[ti++] = *p;
                p++;
            }
            token_buf[ti] = '\0';

            if (ti > 0) {
                HashEntry *he = index_find_or_create(index, token_buf);
                if (he) {
                    posting_list_add(&he->list, d, pos);
                }
                pos++;
            }
        }
    }
}

void index_add_doc(InvertedIndex *index, int32_t doc_id, const char **tokens,
                   const int32_t *token_count) {
    int32_t i, count = *token_count;
    for (i = 0; i < count; i++) {
        char lower[MAX_TERM_LEN];
        const char *src = tokens[i];
        int32_t j = 0;
        while (src[j] && j < MAX_TERM_LEN - 1) {
            lower[j] = (char)((src[j] >= 'A' && src[j] <= 'Z') ? src[j] + 32 : src[j]);
            j++;
        }
        lower[j] = '\0';

        HashEntry *he = index_find_or_create(index, lower);
        if (he) {
            posting_list_add(&he->list, doc_id, i);
        }
    }
}

const PostingList *index_search_term(const InvertedIndex *index, const char *term) {
    char lower[MAX_TERM_LEN];
    int32_t j = 0;
    while (term[j] && j < MAX_TERM_LEN - 1) {
        lower[j] = (char)((term[j] >= 'A' && term[j] <= 'Z') ? term[j] + 32 : term[j]);
        j++;
    }
    lower[j] = '\0';

    const HashEntry *he = index_find(index, lower);
    if (he)
        return &he->list;
    return NULL;
}

void index_print_term_stats(const InvertedIndex *index, const char *term) {
    const PostingList *pl = index_search_term(index, term);
    if (!pl) {
        printf("term '%s': not found in index\n", term);
        return;
    }
    printf("term '%s': doc_freq=%d, posting_list_size=%d\n",
           term, pl->doc_freq, pl->num_docs);
    int32_t i;
    for (i = 0; i < pl->num_docs; i++) {
        const PostingEntry *pe = &pl->postings[i];
        printf("  doc_id=%d, tf=%d, positions=[", pe->doc_id, pe->term_freq);
        int32_t k;
        for (k = 0; k < pe->num_positions; k++) {
            printf("%s%d", k > 0 ? "," : "", pe->positions[k]);
        }
        printf("]\n");
    }
}

void index_free(InvertedIndex *index) {
    int32_t i;
    for (i = 0; i < HASH_MAP_SIZE; i++) {
        if (index->entries[i].occupied) {
            free(index->entries[i].list.postings);
            index->entries[i].list.postings = NULL;
            index->entries[i].list.capacity = 0;
            index->entries[i].list.num_docs = 0;
            index->entries[i].occupied = 0;
        }
    }
    index->num_terms = 0;
    index->total_docs = 0;
}

PostingList *posting_list_intersect(const PostingList *a, const PostingList *b) {
    if (!a || !b || a->num_docs == 0 || b->num_docs == 0) {
        PostingList *empty = posting_list_create();
        return empty;
    }
    PostingList *result = posting_list_create();
    if (!result) return NULL;

    int32_t i = 0, j = 0;
    while (i < a->num_docs && j < b->num_docs) {
        int32_t id_a = a->postings[i].doc_id;
        int32_t id_b = b->postings[j].doc_id;
        if (id_a < id_b) {
            i++;
        } else if (id_a > id_b) {
            j++;
        } else {
            PostingEntry e;
            e.doc_id = id_a;
            e.term_freq = a->postings[i].term_freq + b->postings[j].term_freq;
            e.num_positions = 0;
            if (e.term_freq > MAX_POSITIONS)
                e.term_freq = MAX_POSITIONS;
            if (result->num_docs >= result->capacity) {
                result->capacity *= 2;
                result->postings = (PostingEntry *)realloc(result->postings,
                    (size_t)result->capacity * sizeof(PostingEntry));
            }
            result->postings[result->num_docs++] = e;
            result->doc_freq++;
            i++;
            j++;
        }
    }
    return result;
}

PostingList *posting_list_union(const PostingList *a, const PostingList *b) {
    if (!a && !b) return posting_list_create();
    if (!a) {
        PostingList *copy = posting_list_create();
        if (copy && b) {
            int32_t i;
            for (i = 0; i < b->num_docs; i++) {
                posting_list_add(copy, b->postings[i].doc_id, 0);
                copy->postings[copy->num_docs - 1].term_freq = b->postings[i].term_freq;
            }
        }
        return copy;
    }
    if (!b) {
        PostingList *copy = posting_list_create();
        if (copy && a) {
            int32_t i;
            for (i = 0; i < a->num_docs; i++) {
                posting_list_add(copy, a->postings[i].doc_id, 0);
                copy->postings[copy->num_docs - 1].term_freq = a->postings[i].term_freq;
            }
        }
        return copy;
    }

    PostingList *result = posting_list_create();
    if (!result) return NULL;

    int32_t i = 0, j = 0;
    while (i < a->num_docs || j < b->num_docs) {
        int32_t doc_id;
        PostingEntry e;
        e.term_freq = 0;
        e.num_positions = 0;

        if (j >= b->num_docs || (i < a->num_docs && a->postings[i].doc_id < b->postings[j].doc_id)) {
            doc_id = a->postings[i].doc_id;
            e.term_freq = a->postings[i].term_freq;
            i++;
        } else if (i >= a->num_docs || (j < b->num_docs && b->postings[j].doc_id < a->postings[i].doc_id)) {
            doc_id = b->postings[j].doc_id;
            e.term_freq = b->postings[j].term_freq;
            j++;
        } else {
            doc_id = a->postings[i].doc_id;
            e.term_freq = a->postings[i].term_freq + b->postings[j].term_freq;
            i++;
            j++;
        }

        e.doc_id = doc_id;
        if (result->num_docs >= result->capacity) {
            result->capacity *= 2;
            result->postings = (PostingEntry *)realloc(result->postings,
                (size_t)result->capacity * sizeof(PostingEntry));
        }
        result->postings[result->num_docs++] = e;
        result->doc_freq++;
    }
    return result;
}

PostingList *posting_list_exclude(const PostingList *a, const PostingList *b) {
    if (!a) return posting_list_create();
    PostingList *result = posting_list_create();
    if (!result) return NULL;

    if (!b || b->num_docs == 0) {
        int32_t i;
        for (i = 0; i < a->num_docs; i++) {
            posting_list_add(result, a->postings[i].doc_id, 0);
            result->postings[result->num_docs - 1].term_freq = a->postings[i].term_freq;
        }
        return result;
    }

    int32_t i = 0, j = 0;
    while (i < a->num_docs) {
        int32_t id_a = a->postings[i].doc_id;
        while (j < b->num_docs && b->postings[j].doc_id < id_a) j++;
        if (j < b->num_docs && b->postings[j].doc_id == id_a) {
            i++;
            continue;
        }
        posting_list_add(result, id_a, 0);
        result->postings[result->num_docs - 1].term_freq = a->postings[i].term_freq;
        i++;
    }
    return result;
}
