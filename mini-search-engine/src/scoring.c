#include "scoring.h"
#include <math.h>
#include <string.h>

void scorer_init(Scorer *s, int32_t total_docs, double avgdl) {
    s->total_docs = total_docs;
    s->avg_doc_length = avgdl;
}

double score_tfidf(int32_t term_freq, int32_t doc_freq, int32_t total_docs) {
    if (term_freq <= 0 || doc_freq <= 0 || total_docs <= 0) return 0.0;

    double tf = log(1.0 + (double)term_freq);
    double idf = log(((double)total_docs + 1.0) / ((double)doc_freq + 0.5));

    if (idf < 0.0) idf = 0.0;
    return tf * idf;
}

double score_bm25(int32_t term_freq, int32_t doc_freq, int32_t total_docs,
                  int32_t doc_length, double avgdl, double k1, double b) {
    if (term_freq <= 0 || doc_freq <= 0 || total_docs <= 0 || avgdl <= 0.0)
        return 0.0;

    double idf = log(((double)total_docs - (double)doc_freq + 0.5) /
                     ((double)doc_freq + 0.5));
    if (idf < 0.0) idf = 0.0;

    double len_norm = 1.0 - b + b * ((double)doc_length / avgdl);
    double tf_norm = ((double)term_freq * (k1 + 1.0)) /
                     ((double)term_freq + k1 * len_norm);

    return idf * tf_norm;
}

double score_vector_space(const double *query_vec, const double *doc_vec, int32_t dims) {
    if (!query_vec || !doc_vec || dims <= 0) return 0.0;

    double dot = 0.0, q_norm = 0.0, d_norm = 0.0;
    int32_t i;

    for (i = 0; i < dims; i++) {
        dot += query_vec[i] * doc_vec[i];
        q_norm += query_vec[i] * query_vec[i];
        d_norm += doc_vec[i] * doc_vec[i];
    }

    q_norm = sqrt(q_norm);
    d_norm = sqrt(d_norm);

    if (q_norm == 0.0 || d_norm == 0.0) return 0.0;
    return dot / (q_norm * d_norm);
}

double score_combined(const double *scores, int32_t num_scores) {
    if (!scores || num_scores <= 0) return 0.0;

    double sum = 0.0;
    int32_t i;
    for (i = 0; i < num_scores; i++)
        sum += scores[i];

    return sum / (double)num_scores;
}

double score_normalize_by_length(double raw_score, int32_t doc_length, double avgdl) {
    if (doc_length <= 0 || avgdl <= 0.0) return raw_score;

    double pivot = avgdl;
    double norm = 1.0 / sqrt(1.0 + ((double)doc_length - pivot) / pivot);

    if (norm < 0.1) norm = 0.1;
    if (norm > 2.0) norm = 2.0;
    return raw_score * norm;
}

double score_tfidf_raw_count(int32_t term_freq, int32_t doc_freq, int32_t total_docs) {
    if (term_freq <= 0 || doc_freq <= 0 || total_docs <= 0) return 0.0;

    double tf = (double)term_freq;
    double idf = log(((double)total_docs + 1.0) / ((double)doc_freq + 0.5));

    if (idf < 0.0) idf = 0.0;
    return tf * idf;
}

double score_tfidf_augmented(int32_t term_freq, int32_t doc_freq, int32_t total_docs,
                             int32_t max_tf_in_doc) {
    if (term_freq <= 0 || doc_freq <= 0 || total_docs <= 0 || max_tf_in_doc <= 0)
        return 0.0;

    double tf = 0.5 + 0.5 * ((double)term_freq / (double)max_tf_in_doc);
    double idf = log(((double)total_docs + 1.0) / ((double)doc_freq + 0.5));

    if (idf < 0.0) idf = 0.0;
    return tf * idf;
}

double score_idf_only(int32_t doc_freq, int32_t total_docs) {
    if (doc_freq <= 0 || total_docs <= 0) return 0.0;

    double idf = log(((double)total_docs + 1.0) / ((double)doc_freq + 0.5));
    return idf > 0.0 ? idf : 0.0;
}

double score_bm25_multi_term(const int32_t *term_freqs, const int32_t *doc_freqs,
                             int32_t num_terms, int32_t total_docs,
                             int32_t doc_length, double avgdl, double k1, double b) {
    if (!term_freqs || !doc_freqs || num_terms <= 0) return 0.0;

    double total = 0.0;
    int32_t i;

    for (i = 0; i < num_terms; i++) {
        total += score_bm25(term_freqs[i], doc_freqs[i], total_docs,
                            doc_length, avgdl, k1, b);
    }

    return total;
}

double score_bm25f_weighted(const double *term_freqs, const double *field_weights,
                            int32_t num_fields, int32_t doc_freq, int32_t total_docs,
                            int32_t doc_length, double avgdl, double k1, double b) {
    if (!term_freqs || !field_weights || num_fields <= 0) return 0.0;

    double weighted_tf = 0.0;
    int32_t i;

    for (i = 0; i < num_fields; i++)
        weighted_tf += term_freqs[i] * field_weights[i];

    if (weighted_tf <= 0.0 || doc_freq <= 0) return 0.0;

    double idf = log(((double)total_docs - (double)doc_freq + 0.5) /
                     ((double)doc_freq + 0.5));
    if (idf < 0.0) idf = 0.0;

    double len_norm = 1.0 - b + b * ((double)doc_length / avgdl);
    double tf_norm = (weighted_tf * (k1 + 1.0)) / (weighted_tf + k1 * len_norm);

    return idf * tf_norm;
}

double score_weighted_sum(const double *scores, const double *weights, int32_t n) {
    if (!scores || !weights || n <= 0) return 0.0;

    double sum = 0.0, weight_sum = 0.0;
    int32_t i;

    for (i = 0; i < n; i++) {
        sum += scores[i] * weights[i];
        weight_sum += weights[i];
    }

    if (weight_sum <= 0.0) return 0.0;
    return sum / weight_sum;
}

double score_boolean_overlap(const PostingList *query_postings,
                             const PostingList *doc_postings) {
    if (!query_postings || !doc_postings) return 0.0;
    if (query_postings->num_docs == 0) return 0.0;

    int32_t overlap = 0;
    int32_t i, j = 0;

    for (i = 0; i < query_postings->num_docs; i++) {
        int32_t target = query_postings->postings[i].doc_id;
        while (j < doc_postings->num_docs && doc_postings->postings[j].doc_id < target)
            j++;
        if (j < doc_postings->num_docs && doc_postings->postings[j].doc_id == target)
            overlap++;
    }

    return (double)overlap / (double)query_postings->num_docs;
}

double score_pivoted_normalization(double raw_score, int32_t doc_length, double avgdl,
                                    double pivot_slope) {
    if (doc_length <= 0 || avgdl <= 0.0) return raw_score;

    double old_norm = 1.0 / (1.0 - pivot_slope + pivot_slope * ((double)doc_length / avgdl));
    double new_norm = (double)doc_length / avgdl;

    if (old_norm < 0.01) old_norm = 0.01;
    if (new_norm < 0.01) new_norm = 0.01;

    return raw_score * old_norm / new_norm;
}

double sigmoid_boost(double score, double midpoint, double steepness) {
    if (midpoint <= 0.0) midpoint = 1.0;
    return score / (1.0 + exp(-steepness * (score - midpoint)));
}

double decay_function_gauss(double field_value, double origin, double scale,
                             double offset, double decay) {
    if (scale <= 0.0) return 1.0;

    double diff = field_value - origin;
    if (diff < 0.0) diff = -diff;

    double adjusted = fmax(0.0, diff - offset);
    double val = adjusted / scale;
    double score = exp(-(val * val) / 2.0);

    return score * (1.0 - decay) + decay;
}

