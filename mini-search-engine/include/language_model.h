#ifndef LANGUAGE_MODEL_H
#define LANGUAGE_MODEL_H

#include <stdint.h>

/*
 * language_model.h — Language Modeling Approach to Information Retrieval
 *
 * Reference: Ponte & Croft. "A Language Modeling Approach to IR" (SIGIR 1998)
 *            Zhai & Lafferty. "A Study of Smoothing Methods" (TOIS 2004)
 *            Hiemstra. "Using Language Models for IR" (PhD Thesis, 2001)
 *
 * Knowledge Coverage:
 *   L5: Dirichlet, Jelinek-Mercer, Absolute Discounting smoothing
 *   L5: Query-likelihood retrieval model
 *   L4: Bayesian framework for parameter estimation
 *   L8: Two-stage smoothing (advanced smoothing strategy)
 */

#define LM_VOCAB_SIZE 16384
#define LM_MAX_TERM_LEN 64

/* Vocabulary entry for language model statistics */
typedef struct {
    char    term[LM_MAX_TERM_LEN];
    int32_t collection_freq;
    double  collection_prob;
} LmVocabEntry;

/* Per-document language model */
typedef struct {
    LmVocabEntry *vocab;
    int32_t       vocab_size;
    int32_t       vocab_capacity;
    int32_t       total_terms;       /* sum of all term frequencies */
    int32_t       num_docs;
} LanguageModel;

/* Initialize language model with collection statistics */
void lm_init(LanguageModel *lm);

/* Add or update a term's frequency in the collection */
void lm_add_term(LanguageModel *lm, const char *term, int32_t freq);

/* Build probability distribution after all terms added */
void lm_build(LanguageModel *lm);

/* Query-likelihood: P(Q|D) = ∏_{q∈Q} P(q|D)
 * Returns log-likelihood to avoid underflow.
 * L5: This is the core retrieval function in the LM approach.
 * Documents are ranked by P(D|Q) ∝ P(Q|D) * P(D) (Bayes rule). */
double lm_query_log_likelihood(const LanguageModel *lm,
                               const char **query_terms, int32_t num_terms,
                               const int32_t *doc_term_freqs,
                               const int32_t *doc_lengths,
                               int32_t doc_id);

/* ===== L5: Smoothing Methods ===== */

/* Dirichlet prior smoothing (Bayesian):
 * P(w|D) = (c(w,D) + μ * P(w|C)) / (|D| + μ)
 * where μ is the Dirichlet prior parameter (typically 1000-2000).
 * term is looked up in LM vocabulary for P(w|C).
 * L4 Theorem: Dirichlet smoothing is equivalent to MAP estimation
 * with a Dirichlet(μ*P(w1|C), ..., μ*P(wN|C)) prior. */
double lm_prob_dirichlet(const LanguageModel *lm, const char *term,
                         int32_t term_freq_in_doc,
                         int32_t doc_length, double mu);

/* Jelinek-Mercer smoothing (interpolation):
 * P(w|D) = (1-λ) * P_ml(w|D) + λ * P(w|C)
 * where λ ∈ [0,1] controls collection model weight.
 * term is looked up in LM vocabulary for P(w|C).
 * L4 Theorem: JM is a shrinkage estimator that balances
 * bias (from collection) and variance (from ML estimate). */
double lm_prob_jelinek_mercer(const LanguageModel *lm, const char *term,
                              int32_t term_freq_in_doc,
                              int32_t doc_length, double lambda);

/* Absolute discounting:
 * P(w|D) = max(c(w,D) - δ, 0) / |D| + σ * P(w|C)
 * where δ is the discount constant (typically 0.5-0.8) and
 * σ = δ * |D_unique| / |D| ensures proper probability mass.
 * term is looked up in LM vocabulary for P(w|C).
 * L4 Theorem: Absolute discounting is motivated by the
 * Good-Turing estimate; it subtracts a constant from observed counts. */
double lm_prob_abs_discount(const LanguageModel *lm, const char *term,
                            int32_t term_freq_in_doc,
                            int32_t doc_length,
                            int32_t unique_terms_in_doc,
                            double delta);

/* Two-stage smoothing (L8: Advanced):
 * Combines Dirichlet at stage 1 with JM at stage 2:
 * Stage 1: P'(w|D) = (c(w,D) + μ*P(w|C)) / (|D| + μ)
 * Stage 2: P''(w|D) = (1-λ)*P'(w|D) + λ*P_bg(w)
 * where P_bg is a background model (e.g., query-independent).
 * Reference: Zhai & Lafferty (2002) */
double lm_prob_two_stage(const LanguageModel *lm, const char *term,
                         int32_t term_freq_in_doc,
                         int32_t doc_length,
                         double mu, double lambda,
                         double bg_prob);

/* Compute collection probability P(w|C) for a term */
double lm_collection_prob(const LanguageModel *lm, const char *term);

/* Perplexity: 2^{-(1/N) * sum log2 P(w_i)}
 * Measures how well the model predicts the data.
 * Lower perplexity = better model fit. */
double lm_perplexity(const LanguageModel *lm,
                     const char **terms, int32_t num_terms,
                     const int32_t *doc_term_freqs,
                     const int32_t *doc_lengths,
                     int32_t doc_id, double mu);

/* Clean up language model */
void lm_free(LanguageModel *lm);

#endif
