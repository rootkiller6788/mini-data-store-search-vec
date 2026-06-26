#include "search_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void engine_init(SearchEngine *engine) {
    index_init(&engine->index);
    analyzer_init(&engine->analyzer, TOKENIZER_STANDARD,
                  FILTER_LOWERCASE | FILTER_STOP_WORDS | FILTER_STEMMER);
    engine->num_docs = 0;
    engine->total_doc_length = 0;
    engine->doc_lengths_cap = MAX_DOCS;
    engine->doc_lengths = (int32_t *)calloc((size_t)MAX_DOCS, sizeof(int32_t));
    scorer_init(&engine->scorer, 0, 1.0);
}

void engine_index_document(SearchEngine *engine, int32_t doc_id,
                           const char *title, const char *content) {
    if (!engine || doc_id < 0) return;
    if (doc_id >= engine->doc_lengths_cap) {
        int32_t new_cap = engine->doc_lengths_cap * 2;
        if (doc_id >= new_cap) new_cap = doc_id + 16;
        engine->doc_lengths = (int32_t *)realloc(engine->doc_lengths,
            (size_t)new_cap * sizeof(int32_t));
        int32_t i;
        for (i = engine->doc_lengths_cap; i < new_cap; i++)
            engine->doc_lengths[i] = 0;
        engine->doc_lengths_cap = new_cap;
    }

    if (title && doc_id < MAX_DOCS) {
        strncpy(engine->doc_titles[doc_id], title, MAX_TITLE_LEN - 1);
        engine->doc_titles[doc_id][MAX_TITLE_LEN - 1] = '\0';
    }

    Token tokens[MAX_TOKENS];
    int32_t tc = analyzer_analyze(&engine->analyzer, content, tokens, MAX_TOKENS);

    const char *token_ptrs[MAX_TOKENS];
    int32_t i;
    for (i = 0; i < tc; i++)
        token_ptrs[i] = tokens[i].text;

    index_add_doc(&engine->index, doc_id, token_ptrs, &tc);

    if (doc_id >= engine->num_docs)
        engine->num_docs = doc_id + 1;
    engine->doc_lengths[doc_id] = tc;
    engine->total_doc_length += tc;

    engine->index.total_docs = engine->num_docs;
    engine->scorer.total_docs = engine->num_docs;
    if (engine->num_docs > 0)
        engine->scorer.avg_doc_length =
            (double)engine->total_doc_length / (double)engine->num_docs;
}

int32_t engine_search(SearchEngine *engine, const char *query,
                      SearchResult *results, int32_t max_results) {
    if (!engine || !query || !results || max_results <= 0) return 0;

    Token query_tokens[MAX_TOKENS];
    int32_t query_tc = analyzer_analyze(&engine->analyzer, query,
                                         query_tokens, MAX_TOKENS);

    if (query_tc == 0) return 0;

    char query_terms_buf[MAX_TOKENS * MAX_TOKEN_TEXT + MAX_TOKENS];
    query_terms_buf[0] = '\0';
    int32_t i;
    for (i = 0; i < query_tc; i++) {
        if (i > 0) strcat(query_terms_buf, " ");
        strcat(query_terms_buf, query_tokens[i].text);
    }

    PostingList *combined = query_boolean_search(&engine->index, query_terms_buf);
    if (!combined) return 0;

    int32_t result_count = query_result_top_k(
        results, max_results, combined, &engine->index,
        &engine->scorer, engine->doc_titles, engine->doc_lengths);

    posting_list_free(combined);
    return result_count;
}

void engine_print_results(const SearchResult *results, int32_t count) {
    if (!results || count <= 0) {
        printf("No results found.\n");
        return;
    }
    printf("Found %d result(s):\n", count);
    int32_t i;
    for (i = 0; i < count; i++) {
        printf("  %d. doc_id=%d, score=%.4f, title=\"%s\"\n",
               i + 1, results[i].doc_id, results[i].score,
               results[i].doc_title);
    }
}

void engine_free(SearchEngine *engine) {
    if (!engine) return;
    index_free(&engine->index);
    free(engine->doc_lengths);
    engine->doc_lengths = NULL;
    engine->doc_lengths_cap = 0;
    engine->num_docs = 0;
    engine->total_doc_length = 0;
}

void engine_batch_index(SearchEngine *engine, const char **titles,
                        const char **contents, int32_t count) {
    if (!engine || !titles || !contents || count <= 0) return;
    int32_t i;
    for (i = 0; i < count; i++)
        engine_index_document(engine, i, titles[i], contents[i]);
}

int32_t engine_search_ex(SearchEngine *engine, const char *query,
                         const char *filter_term, int32_t exclude_filter,
                         SearchResult *results, int32_t max_results) {
    if (!engine || !query || !results || max_results <= 0) return 0;

    PostingList *base = query_boolean_search(&engine->index, query);
    if (!base) return 0;

    PostingList *filtered = base;
    if (filter_term) {
        const PostingList *fl = index_search_term(&engine->index, filter_term);
        if (fl) {
            if (exclude_filter)
                filtered = posting_list_exclude(base, fl);
            else
                filtered = posting_list_intersect(base, fl);
            posting_list_free(base);
        }
    }

    int32_t result_count = query_result_top_k(
        results, max_results, filtered, &engine->index,
        &engine->scorer, engine->doc_titles, engine->doc_lengths);

    if (filter_term) posting_list_free(filtered);
    return result_count;
}

int32_t engine_get_doc_count(const SearchEngine *engine) {
    return engine ? engine->num_docs : 0;
}

double engine_get_avgdl(const SearchEngine *engine) {
    return engine ? engine->scorer.avg_doc_length : 0.0;
}

const char *engine_get_doc_title(const SearchEngine *engine, int32_t doc_id) {
    if (!engine || doc_id < 0 || doc_id >= MAX_DOCS) return NULL;
    return engine->doc_titles[doc_id];
}

void engine_print_stats(const SearchEngine *engine) {
    if (!engine) return;
    printf("SearchEngine Stats:\n");
    printf("  Total documents: %d\n", engine->num_docs);
    printf("  Total terms in index: %d\n", engine->index.num_terms);
    printf("  Total document length: %d\n", engine->total_doc_length);
    printf("  Average document length: %.2f\n", engine->scorer.avg_doc_length);
}

