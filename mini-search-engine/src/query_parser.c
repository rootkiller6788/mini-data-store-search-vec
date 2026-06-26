#include "query_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static QueryNode *alloc_node(QueryNodeType type) {
    QueryNode *n = (QueryNode *)calloc(1, sizeof(QueryNode));
    if (n) n->type = type;
    return n;
}

void query_free_node(QueryNode *node) {
    if (!node) return;
    query_free_node(node->left);
    query_free_node(node->right);
    if (node->result_list) {
        posting_list_free(node->result_list);
        node->result_list = NULL;
    }
    free(node);
}

static void skip_ws(const char **s) {
    while (**s == ' ' || **s == '\t') (*s)++;
}

static int32_t is_keyword(const char *s, const char *kw) {
    size_t klen = strlen(kw);
    if (strncmp(s, kw, klen) != 0) return 0;
    char next = s[klen];
    if (next != '\0' && next != ' ' && next != '\t' && next != '(' && next != ')')
        return 0;
    return 1;
}

static QueryNode *parse_expr(const char **s);

static QueryNode *parse_atom(const char **s) {
    skip_ws(s);
    if (**s == '(') {
        (*s)++;
        QueryNode *inner = parse_expr(s);
        skip_ws(s);
        if (**s == ')') (*s)++;
        return inner;
    }
    if (**s == '\0') return NULL;

    char term[MAX_TERM_LEN];
    int32_t i = 0;
    while (**s && **s != ' ' && **s != '\t' && **s != '(' && **s != ')' &&
           i < MAX_TERM_LEN - 1) {
        if (**s >= 'A' && **s <= 'Z')
            term[i++] = (char)(**s + 32);
        else
            term[i++] = **s;
        (*s)++;
    }
    term[i] = '\0';

    if (i == 0) return NULL;

    QueryNode *n = alloc_node(NODE_TERM);
    if (n) {
        strncpy(n->term, term, MAX_TERM_LEN - 1);
        n->term[MAX_TERM_LEN - 1] = '\0';
    }
    return n;
}

static QueryNode *parse_prefix(const char **s) {
    skip_ws(s);
    const char *start = *s;

    if (is_keyword(start, "NOT") || is_keyword(start, "not")) {
        (*s) = start + 3;
        skip_ws(s);
        QueryNode *n = alloc_node(NODE_NOT);
        if (n) n->left = parse_prefix(s);
        return n;
    }

    QueryNode *atom = parse_atom(s);
    if (atom && atom->type == NODE_TERM) {
        int32_t len = (int32_t)strlen(atom->term);
        if (len > 0 && atom->term[len - 1] == '*') {
            atom->term[len - 1] = '\0';
            atom->type = NODE_PREFIX;
        }
    }

    if (**s == '~') {
        (*s)++;
        skip_ws(s);
        if (atom && atom->type == NODE_TERM) {
            QueryNode *phrase = alloc_node(NODE_PHRASE);
            if (phrase) {
                strncpy(phrase->term, atom->term, MAX_TERM_LEN - 1);
                phrase->term[MAX_TERM_LEN - 1] = '\0';
                QueryNode *right_atom = parse_atom(s);
                if (right_atom && right_atom->type == NODE_TERM) {
                    phrase->left = atom;
                    phrase->right = right_atom;
                    return phrase;
                }
                query_free_node(right_atom);
                query_free_node(phrase);
            }
        }
    }

    return atom;
}

static QueryNode *parse_and(const char **s) {
    QueryNode *left = parse_prefix(s);
    if (!left) return NULL;

    while (1) {
        skip_ws(s);
        if (is_keyword(*s, "AND") || is_keyword(*s, "and")) {
            (*s) += 3;
            QueryNode *right = parse_prefix(s);
            if (right) {
                QueryNode *n = alloc_node(NODE_AND);
                if (n) { n->left = left; n->right = right; left = n; }
            }
        } else if (**s != '\0' && **s != ')' &&
                   !is_keyword(*s, "OR") && !is_keyword(*s, "or")) {
            QueryNode *right = parse_prefix(s);
            if (right) {
                QueryNode *n = alloc_node(NODE_AND);
                if (n) { n->left = left; n->right = right; left = n; }
            } else {
                break;
            }
        } else {
            break;
        }
    }
    return left;
}

static QueryNode *parse_expr(const char **s) {
    QueryNode *left = parse_and(s);
    if (!left) return NULL;

    while (1) {
        skip_ws(s);
        if (is_keyword(*s, "OR") || is_keyword(*s, "or")) {
            (*s) += 2;
            QueryNode *right = parse_and(s);
            if (right) {
                QueryNode *n = alloc_node(NODE_OR);
                if (n) { n->left = left; n->right = right; left = n; }
            }
        } else {
            break;
        }
    }
    return left;
}

QueryNode *query_parse(const char *query_str) {
    if (!query_str || !*query_str) return NULL;
    const char *s = query_str;
    return parse_expr(&s);
}

static void eval_merge(QueryNode *node, const InvertedIndex *index) {
    if (!node || !index) return;
    if (node->result_list) return;

    eval_merge(node->left, index);
    eval_merge(node->right, index);

    switch (node->type) {
    case NODE_TERM: {
        const PostingList *pl = index_search_term(index, node->term);
        node->result_list = posting_list_create();
        if (pl && node->result_list) {
            int32_t i;
            for (i = 0; i < pl->num_docs; i++) {
                posting_list_add(node->result_list, pl->postings[i].doc_id, 0);
                node->result_list->postings[node->result_list->num_docs - 1].term_freq =
                    pl->postings[i].term_freq;
            }
        }
        break;
    }
    case NODE_PREFIX: {
        node->result_list = posting_list_create();
        if (!node->result_list) break;
        int32_t term_len = (int32_t)strlen(node->term);
        int32_t i;
        for (i = 0; i < HASH_MAP_SIZE; i++) {
            if (index->entries[i].occupied &&
                strncmp(index->entries[i].term, node->term, (size_t)term_len) == 0) {
                PostingList *tmp = posting_list_union(
                    node->result_list, &index->entries[i].list);
                posting_list_free(node->result_list);
                node->result_list = tmp;
            }
        }
        break;
    }
    case NODE_AND: {
        PostingList *la = node->left ? node->left->result_list : NULL;
        PostingList *lb = node->right ? node->right->result_list : NULL;
        node->result_list = posting_list_intersect(la, lb);
        break;
    }
    case NODE_OR: {
        PostingList *la = node->left ? node->left->result_list : NULL;
        PostingList *lb = node->right ? node->right->result_list : NULL;
        node->result_list = posting_list_union(la, lb);
        break;
    }
    case NODE_NOT: {
        PostingList *la = node->left ? node->left->result_list : NULL;
        PostingList *all = posting_list_create();
        if (all && index) {
            int32_t d;
            for (d = 0; d < index->total_docs; d++)
                posting_list_add(all, d, 0);
        }
        node->result_list = posting_list_exclude(all, la);
        posting_list_free(all);
        break;
    }
    case NODE_PHRASE: {
        node->result_list = posting_list_create();
        break;
    }
    }
}

static void cleanup_result_lists(QueryNode *node) {
    if (!node) return;
    cleanup_result_lists(node->left);
    cleanup_result_lists(node->right);
    if (node->result_list) {
        posting_list_free(node->result_list);
        node->result_list = NULL;
    }
}

PostingList *query_evaluate(QueryNode *node, const InvertedIndex *index) {
    if (!node || !index) return NULL;
    cleanup_result_lists(node);
    eval_merge(node, index);

    PostingList *result = NULL;
    if (node->result_list) {
        result = posting_list_create();
        if (result) {
            int32_t i;
            for (i = 0; i < node->result_list->num_docs; i++) {
                posting_list_add(result, node->result_list->postings[i].doc_id, 0);
                result->postings[result->num_docs - 1].term_freq =
                    node->result_list->postings[i].term_freq;
            }
        }
    }
    cleanup_result_lists(node);
    return result;
}

PostingList *query_phrase_search(const InvertedIndex *index,
                                 const char *term_a, const char *term_b,
                                 int32_t proximity) {
    const PostingList *pla = index_search_term(index, term_a);
    const PostingList *plb = index_search_term(index, term_b);
    if (!pla || !plb) return posting_list_create();

    PostingList *result = posting_list_create();
    if (!result) return NULL;

    int32_t i = 0, j = 0;
    while (i < pla->num_docs && j < plb->num_docs) {
        if (pla->postings[i].doc_id < plb->postings[j].doc_id) { i++; continue; }
        if (pla->postings[i].doc_id > plb->postings[j].doc_id) { j++; continue; }

        const PostingEntry *a = &pla->postings[i];
        const PostingEntry *b = &plb->postings[j];
        int32_t pi = 0, pj = 0;
        while (pi < a->num_positions && pj < b->num_positions) {
            if (a->positions[pi] + proximity >= b->positions[pj] &&
                b->positions[pj] >= a->positions[pi]) {
                posting_list_add(result, a->doc_id, 0);
                break;
            }
            if (a->positions[pi] + proximity < b->positions[pj]) pi++;
            else pj++;
        }
        i++; j++;
    }
    return result;
}

PostingList *query_boolean_search(const InvertedIndex *index,
                                  const char *query_str) {
    QueryNode *tree = query_parse(query_str);
    if (!tree) return posting_list_create();

    PostingList *result = query_evaluate(tree, index);
    query_free_node(tree);
    return result ? result : posting_list_create();
}

static int32_t cmp_results_desc(const void *a, const void *b) {
    const SearchResult *ra = (const SearchResult *)a;
    const SearchResult *rb = (const SearchResult *)b;
    if (ra->score > rb->score) return -1;
    if (ra->score < rb->score) return 1;
    return 0;
}

int32_t query_result_top_k(SearchResult *results, int32_t max_results,
                            const PostingList *list, const InvertedIndex *index,
                            const Scorer *scorer,
                            char (*doc_titles)[MAX_TITLE_LEN],
                            const int32_t *doc_lengths) {
    if (!list || !results || !index || !scorer) return 0;

    SearchResult tmp[MAX_RESULTS];
    int32_t count = 0, i;
    int32_t N = scorer->total_docs;
    double avgdl = scorer->avg_doc_length;

    for (i = 0; i < list->num_docs && count < MAX_RESULTS; i++) {
        int32_t doc_id = list->postings[i].doc_id;
        int32_t tf = list->postings[i].term_freq;
        int32_t df = list->doc_freq;
        int32_t dl = doc_lengths ? doc_lengths[doc_id] : 100;

        double s1 = score_tfidf(tf, df, N);
        double s2 = score_bm25(tf, df, N, dl, avgdl, 1.2, 0.75);
        double scores[] = { s1, s2 };
        double combined = score_combined(scores, 2);

        tmp[count].doc_id = doc_id;
        tmp[count].score = combined;
        if (doc_titles)
            strncpy(tmp[count].doc_title, doc_titles[doc_id], MAX_TITLE_LEN - 1);
        else
            tmp[count].doc_title[0] = '\0';
        tmp[count].doc_title[MAX_TITLE_LEN - 1] = '\0';
        count++;
    }

    qsort(tmp, (size_t)count, sizeof(SearchResult), cmp_results_desc);

    int32_t out = count < max_results ? count : max_results;
    for (i = 0; i < out; i++)
        results[i] = tmp[i];

    return out;
}
