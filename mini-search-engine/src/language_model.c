#include "language_model.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ===================================================================
 * language_model.c — Language Modeling for Information Retrieval
 *
 * Core idea (L2): Documents are ranked by P(D|Q) ∝ P(Q|D) * P(D).
 * P(Q|D) = ∏_{q∈Q} P(q|D) under the unigram assumption.
 *
 * Smoothing is essential: without it, any query term not in a
 * document gives P(q|D)=0 → P(Q|D)=0 for that document.
 * =================================================================== */

void lm_init(LanguageModel *lm) {
    if (!lm) return;
    lm->vocab = NULL;
    lm->vocab_size = 0;
    lm->vocab_capacity = 0;
    lm->total_terms = 0;
    lm->num_docs = 0;
}

static int32_t lm_find_term(const LanguageModel *lm, const char *term) {
    int32_t i;
    for (i = 0; i < lm->vocab_size; i++) {
        if (strcmp(lm->vocab[i].term, term) == 0)
            return i;
    }
    return -1;
}

void lm_add_term(LanguageModel *lm, const char *term, int32_t freq) {
    if (!lm || !term || freq <= 0) return;

    int32_t idx = lm_find_term(lm, term);
    if (idx >= 0) {
        lm->vocab[idx].collection_freq += freq;
    } else {
        if (lm->vocab_size >= lm->vocab_capacity) {
            int32_t new_cap = lm->vocab_capacity > 0 ?
                              lm->vocab_capacity * 2 : 1024;
            LmVocabEntry *new_vocab = (LmVocabEntry *)realloc(
                lm->vocab, (size_t)new_cap * sizeof(LmVocabEntry));
            if (!new_vocab) return;
            lm->vocab = new_vocab;
            lm->vocab_capacity = new_cap;
        }
        idx = lm->vocab_size++;
        strncpy(lm->vocab[idx].term, term, LM_MAX_TERM_LEN - 1);
        lm->vocab[idx].term[LM_MAX_TERM_LEN - 1] = '\0';
        lm->vocab[idx].collection_freq = freq;
        lm->vocab[idx].collection_prob = 0.0;
    }
    lm->total_terms += freq;
}

void lm_build(LanguageModel *lm) {
    if (!lm || lm->total_terms <= 0) return;
    int32_t i;
    for (i = 0; i < lm->vocab_size; i++) {
        lm->vocab[i].collection_prob =
            (double)lm->vocab[i].collection_freq / (double)lm->total_terms;
    }
}

double lm_collection_prob(const LanguageModel *lm, const char *term) {
    if (!lm || !term || lm->total_terms <= 0) return 0.0;
    int32_t idx = lm_find_term(lm, term);
    if (idx < 0) return 0.0;
    return lm->vocab[idx].collection_prob;
}

/* ===== Query-Likelihood Retrieval (L5) =====
 * P(Q|D) = ∏_{q∈Q} P(q|D)
 * Returns log-likelihood to avoid floating-point underflow.
 *
 * Uses Dirichlet smoothing with default mu=1000.
 * L4 Bayes: P(D|Q) = P(Q|D)*P(D)/P(Q). With uniform prior P(D)
 * and fixed P(Q), ranking by P(Q|D) is Bayes-optimal. */
double lm_query_log_likelihood(const LanguageModel *lm,
                               const char **query_terms, int32_t num_terms,
                               const int32_t *doc_term_freqs,
                               const int32_t *doc_lengths,
                               int32_t doc_id) {
    if (!lm || !query_terms || num_terms <= 0 || !doc_term_freqs ||
        !doc_lengths || doc_id < 0) return 0.0;

    double log_ll = 0.0;
    int32_t doc_len = doc_lengths[doc_id];
    double mu = 1000.0;
    int32_t i;

    for (i = 0; i < num_terms; i++) {
        /* Get P(w|C) from vocabulary */
        double p_coll = lm_collection_prob(lm, query_terms[i]);

        /* Get tf from the term-document matrix */
        int32_t term_idx = lm_find_term(lm, query_terms[i]);
        int32_t tf = 0;
        if (term_idx >= 0) {
            tf = doc_term_freqs[doc_id * lm->vocab_size + term_idx];
        }

        /* Dirichlet smoothed P(w|D) */
        double p_smooth = ((double)tf + mu * p_coll) /
                          ((double)doc_len + mu);

        if (p_smooth > 0.0)
            log_ll += log(p_smooth);
        else
            log_ll += log(1e-10); /* avoid log(0) → -inf */
    }

    return log_ll;
}

/* ===== L5: Dirichlet Prior Smoothing =====
 *
 * P(w|D) = (c(w,D) + mu * P(w|C)) / (|D| + mu)
 *
 * mu controls smoothing strength:
 *   mu → 0:  P(w|D) → P_ml(w|D) (no smoothing)
 *   mu → ∞:  P(w|D) → P(w|C) (full backoff)
 *
 * L4 Theorem: This is the posterior mean of a Dirichlet-multinomial
 * conjugate model. The Dirichlet prior is Dir(mu*P(w1|C),...).
 * Optimal mu ≈ 1000-2000 per Zhai & Lafferty (2004). */
double lm_prob_dirichlet(const LanguageModel *lm, const char *term,
                         int32_t term_freq_in_doc,
                         int32_t doc_length, double mu) {
    if (!lm || !term || doc_length < 0) return 0.0;
    if (mu <= 0.0) {
        if (doc_length <= 0) return 0.0;
        return (double)term_freq_in_doc / (double)doc_length;
    }

    double p_coll = lm_collection_prob(lm, term);

    double smoothed = ((double)term_freq_in_doc + mu * p_coll) /
                      ((double)doc_length + mu);
    return smoothed;
}

/* ===== L5: Jelinek-Mercer Smoothing =====
 *
 * P(w|D) = (1-λ) * P_ml(w|D) + λ * P(w|C)
 *
 * λ ∈ [0,1]: 0 = ML only, 1 = collection only.
 * Typical λ: 0.1 for short queries, 0.7 for long documents.
 *
 * L4 Theorem: JM is a shrinkage estimator. The optimal λ minimizes
 * KL divergence between estimated and true distributions.
 * λ_opt ≈ 1 / (1 + |D| / μ_eff) where μ_eff is effective sample size. */
double lm_prob_jelinek_mercer(const LanguageModel *lm, const char *term,
                              int32_t term_freq_in_doc,
                              int32_t doc_length, double lambda) {
    if (!lm || !term || doc_length < 0) return 0.0;
    if (lambda < 0.0) lambda = 0.0;
    if (lambda > 1.0) lambda = 1.0;

    double p_ml = (doc_length > 0) ?
        (double)term_freq_in_doc / (double)doc_length : 0.0;

    double p_coll = lm_collection_prob(lm, term);

    return (1.0 - lambda) * p_ml + lambda * p_coll;
}

/* ===== L5: Absolute Discounting =====
 *
 * P(w|D) = max(c(w,D) - δ, 0) / |D| + σ * P(w|C)
 *
 * σ = δ * |D_unique| / |D| redistributes discounted mass.
 * Typical δ: 0.5-0.75.
 *
 * L4 Theorem: Motivated by Good-Turing estimation. For a word
 * observed r times, the expected true frequency ≈ r - E(N_{r+1})/E(N_r).
 * For small r, this discount is approximately 0.5 (Church & Gale 1991). */
double lm_prob_abs_discount(const LanguageModel *lm, const char *term,
                            int32_t term_freq_in_doc,
                            int32_t doc_length,
                            int32_t unique_terms_in_doc,
                            double delta) {
    if (!lm || !term || doc_length <= 0) return 0.0;
    if (delta < 0.0 || delta > 1.0) delta = 0.5;

    double discounted = (double)((term_freq_in_doc > (int32_t)delta) ?
        term_freq_in_doc - (int32_t)delta : 0);
    if (discounted < 0.0) discounted = 0.0;

    double p_disc = discounted / (double)doc_length;

    double sigma = (unique_terms_in_doc > 0 && doc_length > 0) ?
        delta * (double)unique_terms_in_doc / (double)doc_length : 0.0;

    double p_coll = lm_collection_prob(lm, term);

    return p_disc + sigma * p_coll;
}

/* ===== L8: Two-Stage Smoothing =====
 *
 * Stage 1 (Dirichlet): P'(w|D) = (c(w,D) + mu*P(w|C)) / (|D| + mu)
 * Stage 2 (JM):        P''(w|D) = (1-λ)*P'(w|D) + λ*P_bg(w)
 *
 * Advantages: Stage 1 handles data sparsity; Stage 2 allows
 * background model beyond the collection (e.g., query logs).
 *
 * Reference: Zhai & Lafferty. "Two-stage language models for
 * information retrieval" (SIGIR 2002). */
double lm_prob_two_stage(const LanguageModel *lm, const char *term,
                         int32_t term_freq_in_doc,
                         int32_t doc_length,
                         double mu, double lambda,
                         double bg_prob) {
    if (!lm || !term || doc_length < 0) return 0.0;
    if (mu <= 0.0) mu = 1000.0;
    if (lambda < 0.0) lambda = 0.0;
    if (lambda > 1.0) lambda = 1.0;

    double p_coll = lm_collection_prob(lm, term);

    /* Stage 1: Dirichlet */
    double p_stage1 = ((double)term_freq_in_doc + mu * p_coll) /
                      ((double)doc_length + mu);

    /* Stage 2: JM with background */
    double p_stage2 = (1.0 - lambda) * p_stage1 + lambda * bg_prob;

    return p_stage2;
}

/* ===== L4: Perplexity =====
 *
 * PP = 2^{ - (1/N) * Σ log₂ P(w_i|D) }
 *
 * Interpretation:
 *   PP ≈ 100 means the model is as confused as choosing uniformly
 *         among 100 equally likely options.
 *   PP = 1 means perfect prediction.
 *
 * Uses Dirichlet smoothing for P(w|D). */
double lm_perplexity(const LanguageModel *lm,
                     const char **terms, int32_t num_terms,
                     const int32_t *doc_term_freqs,
                     const int32_t *doc_lengths,
                     int32_t doc_id, double mu) {
    if (!lm || !terms || num_terms <= 0 || !doc_term_freqs ||
        !doc_lengths || doc_id < 0) return 0.0;

    double sum_log2 = 0.0;
    int32_t doc_len = doc_lengths[doc_id];
    int32_t i;

    for (i = 0; i < num_terms; i++) {
        double p_coll = lm_collection_prob(lm, terms[i]);

        int32_t idx = lm_find_term(lm, terms[i]);
        int32_t tf = 0;
        if (idx >= 0) {
            tf = doc_term_freqs[doc_id * lm->vocab_size + idx];
        }

        double p = ((double)tf + mu * p_coll) /
                   ((double)doc_len + mu);
        if (p <= 0.0) p = 1e-10;
        sum_log2 += log2(p);
    }

    double avg_log2 = sum_log2 / (double)num_terms;
    return pow(2.0, -avg_log2);
}

void lm_free(LanguageModel *lm) {
    if (!lm) return;
    free(lm->vocab);
    lm->vocab = NULL;
    lm->vocab_size = 0;
    lm->vocab_capacity = 0;
    lm->total_terms = 0;
    lm->num_docs = 0;
}