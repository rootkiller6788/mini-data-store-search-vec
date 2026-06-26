#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include <stdint.h>
#include "inverted_index.h"
#include "tokenizer.h"
#include "scoring.h"
#include "query_parser.h"

#define MAX_DOCS         128
#define MAX_DOC_CONTENT  2048

typedef struct {
    InvertedIndex   index;
    Analyzer        analyzer;
    Scorer          scorer;
    int32_t         num_docs;
    int32_t         total_doc_length;
    char            doc_titles[MAX_DOCS][MAX_TITLE_LEN];
    int32_t        *doc_lengths;
    int32_t         doc_lengths_cap;
} SearchEngine;

void      engine_init(SearchEngine *engine);
void      engine_index_document(SearchEngine *engine, int32_t doc_id,
                                const char *title, const char *content);
int32_t   engine_search(SearchEngine *engine, const char *query,
                        SearchResult *results, int32_t max_results);
void      engine_print_results(const SearchResult *results, int32_t count);
void      engine_free(SearchEngine *engine);

void      engine_batch_index(SearchEngine *engine, const char **titles,
                             const char **contents, int32_t count);
int32_t   engine_search_ex(SearchEngine *engine, const char *query,
                           const char *filter_term, int32_t exclude_filter,
                           SearchResult *results, int32_t max_results);
int32_t   engine_get_doc_count(const SearchEngine *engine);
double    engine_get_avgdl(const SearchEngine *engine);
const char *engine_get_doc_title(const SearchEngine *engine, int32_t doc_id);
void      engine_print_stats(const SearchEngine *engine);

#endif
