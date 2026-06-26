#include "inverted_index.h"
#include "tokenizer.h"
#include "scoring.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static char *g_docs[5] = {
    "The database system stores data efficiently for fast retrieval and querying",
    "Database management systems are essential for modern data applications",
    "A file system organizes files in directories not a database",
    "The search engine uses an inverted index for fast lookup of database content",
    "Database database database search search search engine engine index"
};

static int32_t g_doc_lengths[5] = { 0 };

static void normalize_tokens(char *src, char *dst, int32_t max) {
    char buf[MAX_TOKEN_TEXT];
    const char *p = src;
    int32_t out = 0;
    dst[0] = '\0';

    while (*p && out < max - 1) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;
        int32_t ti = 0;
        while (*p && *p != ' ' && *p != '\t' && ti < MAX_TOKEN_TEXT - 1) {
            buf[ti++] = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
            p++;
        }
        buf[ti] = '\0';
        if (ti > 0) {
            if (out > 0) { dst[out++] = ' '; }
            int32_t k;
            for (k = 0; buf[k] && out < max - 1; k++)
                dst[out++] = buf[k];
        }
    }
    dst[out] = '\0';
}

int main(void) {
    Analyzer analyzer;
    analyzer_init(&analyzer, TOKENIZER_STANDARD,
                  FILTER_LOWERCASE | FILTER_STOP_WORDS | FILTER_STEMMER);

    InvertedIndex idx;
    index_init(&idx);

    int32_t i, total_len = 0;
    for (i = 0; i < 5; i++) {
        Token tokens[MAX_TOKENS];
        int32_t tc = analyzer_analyze(&analyzer, g_docs[i], tokens, MAX_TOKENS);
        g_doc_lengths[i] = tc;
        total_len += tc;

        const char *token_ptrs[MAX_TOKENS];
        int32_t k;
        for (k = 0; k < tc; k++)
            token_ptrs[k] = tokens[k].text;
        index_add_doc(&idx, i, token_ptrs, &tc);
    }
    idx.total_docs = 5;

    double avgdl = (double)total_len / 5.0;

    Scorer scorer;
    scorer_init(&scorer, 5, avgdl);

    printf("=== Scoring Comparison Demo ===\n\n");
    printf("Average document length: %.2f tokens\n\n", avgdl);

    printf("%-12s %-12s %-12s %-12s %-8s\n",
           "Document", "Length", "TF-IDF", "BM25", "Combined");
    printf("%s\n",
           "----------------------------------------------------------------");

    const char *terms[] = { "database", "search", "system", "index", "data" };
    int32_t num_terms = 5;
    int32_t t;

    for (t = 0; t < num_terms; t++) {
        printf("\n--- Query term: '%s' ---\n", terms[t]);
        const PostingList *pl = index_search_term(&idx, terms[t]);
        if (!pl) {
            printf("  Term not found in any document.\n");
            continue;
        }

        printf("%-12s %-12s %-12s %-12s %-8s\n",
               "Document", "Length", "TF-IDF", "BM25", "Combined");
        printf("%s\n",
               "----------------------------------------------------------------");

        for (i = 0; i < 5; i++) {
            int32_t tf = 0;
            int32_t k;
            for (k = 0; k < pl->num_docs; k++) {
                if (pl->postings[k].doc_id == i) {
                    tf = pl->postings[k].term_freq;
                    break;
                }
            }

            double tfidf = score_tfidf(tf, pl->doc_freq, 5);
            double bm25 = score_bm25(tf, pl->doc_freq, 5,
                                     g_doc_lengths[i], avgdl, 1.2, 0.75);
            double scores[] = { tfidf, bm25 };
            double combined = score_combined(scores, 2);

            printf("doc_%-8d %-12d %-12.4f %-12.4f %-8.4f\n",
                   i, g_doc_lengths[i], tfidf, bm25, combined);
        }
    }

    printf("\n--- Ranking differences for 'database' ---\n");
    const PostingList *pl = index_search_term(&idx, "database");
    if (pl) {
        printf("TF-IDF ranking:      ");
        double tfidf_scores[5] = {0};
        for (i = 0; i < pl->num_docs; i++) {
            int32_t d = pl->postings[i].doc_id;
            tfidf_scores[d] = score_tfidf(pl->postings[i].term_freq,
                                           pl->doc_freq, 5);
        }
        int32_t order[5] = {0,1,2,3,4};
        int32_t a, b;
        for (a = 0; a < 4; a++)
            for (b = a+1; b < 5; b++)
                if (tfidf_scores[order[a]] < tfidf_scores[order[b]]) {
                    int32_t tmp = order[a]; order[a] = order[b]; order[b] = tmp;
                }
        for (i = 0; i < 5; i++)
            printf("doc_%d(%.4f) ", order[i], tfidf_scores[order[i]]);
        printf("\n");

        printf("BM25 ranking:        ");
        double bm25_scores[5] = {0};
        for (i = 0; i < pl->num_docs; i++) {
            int32_t d = pl->postings[i].doc_id;
            bm25_scores[d] = score_bm25(pl->postings[i].term_freq,
                                         pl->doc_freq, 5,
                                         g_doc_lengths[d], avgdl, 1.2, 0.75);
        }
        int32_t order2[5] = {0,1,2,3,4};
        for (a = 0; a < 4; a++)
            for (b = a+1; b < 5; b++)
                if (bm25_scores[order2[a]] < bm25_scores[order2[b]]) {
                    int32_t tmp = order2[a]; order2[a] = order2[b]; order2[b] = tmp;
                }
        for (i = 0; i < 5; i++)
            printf("doc_%d(%.4f) ", order2[i], bm25_scores[order2[i]]);
        printf("\n");
    }

    index_free(&idx);
    return 0;
}
