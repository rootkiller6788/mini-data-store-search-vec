#ifndef SCORING_H
#define SCORING_H

#include <stdint.h>
#include <math.h>
#include "inverted_index.h"

typedef struct {
    int32_t total_docs;
    double  avg_doc_length;
} Scorer;

void   scorer_init(Scorer *s, int32_t total_docs, double avgdl);

double score_tfidf(int32_t term_freq, int32_t doc_freq, int32_t total_docs);

double score_bm25(int32_t term_freq, int32_t doc_freq, int32_t total_docs,
                  int32_t doc_length, double avgdl, double k1, double b);

double score_vector_space(const double *query_vec, const double *doc_vec, int32_t dims);

double score_combined(const double *scores, int32_t num_scores);

double score_normalize_by_length(double raw_score, int32_t doc_length, double avgdl);

double score_tfidf_raw_count(int32_t term_freq, int32_t doc_freq, int32_t total_docs);

double score_tfidf_augmented(int32_t term_freq, int32_t doc_freq, int32_t total_docs,
                             int32_t max_tf_in_doc);

double score_idf_only(int32_t doc_freq, int32_t total_docs);

double score_bm25_multi_term(const int32_t *term_freqs, const int32_t *doc_freqs,
                             int32_t num_terms, int32_t total_docs,
                             int32_t doc_length, double avgdl, double k1, double b);

double score_bm25f_weighted(const double *term_freqs, const double *field_weights,
                            int32_t num_fields, int32_t doc_freq, int32_t total_docs,
                            int32_t doc_length, double avgdl, double k1, double b);

double score_weighted_sum(const double *scores, const double *weights, int32_t n);

double score_boolean_overlap(const PostingList *query_postings,
                             const PostingList *doc_postings);

double score_pivoted_normalization(double raw_score, int32_t doc_length, double avgdl,
                                    double pivot_slope);

double sigmoid_boost(double score, double midpoint, double steepness);

double decay_function_gauss(double field_value, double origin, double scale,
                             double offset, double decay);

#endif
