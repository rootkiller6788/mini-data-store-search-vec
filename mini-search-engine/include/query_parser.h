#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include <stdint.h>
#include "inverted_index.h"
#include "scoring.h"

#define MAX_RESULTS   128
#define MAX_TITLE_LEN 256

typedef enum {
    NODE_TERM,
    NODE_AND,
    NODE_OR,
    NODE_NOT,
    NODE_PHRASE,
    NODE_PREFIX
} QueryNodeType;

typedef struct QueryNode {
    QueryNodeType    type;
    char             term[MAX_TERM_LEN];
    struct QueryNode *left;
    struct QueryNode *right;
    PostingList     *result_list;
} QueryNode;

typedef struct {
    int32_t doc_id;
    double  score;
    char    doc_title[MAX_TITLE_LEN];
} SearchResult;

QueryNode *query_parse(const char *query_str);
void       query_free_node(QueryNode *node);

PostingList *query_evaluate(QueryNode *node, const InvertedIndex *index);

PostingList *query_phrase_search(const InvertedIndex *index,
                                 const char *term_a, const char *term_b,
                                 int32_t proximity);

PostingList *query_boolean_search(const InvertedIndex *index,
                                  const char *query_str);

int32_t      query_result_top_k(SearchResult *results, int32_t max_results,
                                const PostingList *list, const InvertedIndex *index,
                                const Scorer *scorer,
                                char (*doc_titles)[MAX_TITLE_LEN],
                                const int32_t *doc_lengths);

#endif
