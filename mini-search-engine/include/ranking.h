#ifndef RANKING_H
#define RANKING_H

#include <stdint.h>

/*
 * ranking.h — Search Result Ranking and Evaluation Metrics
 *
 * Reference: Manning, Raghavan, Schutze. "Introduction to IR" (2008), Ch. 8, 21
 *            Page et al. "The PageRank Citation Ranking" (1999)
 *            Carbonell & Goldstein. "MMR for Text Summarization" (1998)
 *            Rocchio. "Relevance Feedback in Information Retrieval" (1971)
 *
 * Knowledge Coverage:
 *   L5: PageRank (iterative), MMR, Rocchio feedback
 *   L4: MAP, NDCG, Precision/Recall@K, MRR (evaluation theorems)
 *   L3: Ranked retrieval pipeline, diversification strategy
 */

#define MAX_RESULTS_K 256

/* ===== L4: IR Evaluation Metrics ===== */

/* MAP (Mean Average Precision): average of precision at each relevant
 * document position, averaged across queries.
 * Formula: MAP = (1/|Q|) * sum_q AvgP(q)
 *   where AvgP = (1/rel_q) * sum_{k: rel(k)=1} P@k
 * Theorem (L4): MAP is a single-value summary of the precision-recall
 * curve; it assumes binary relevance and uniform user patience. */
double map_compute(const int32_t *relevant_at_k, int32_t num_results,
                   int32_t total_relevant);

/* NDCG@K (Normalized Discounted Cumulative Gain):
 * Formula: DCG@K = sum_{i=1}^K (2^{rel_i} - 1) / log2(i+1)
 *          NDCG@K = DCG@K / IDCG@K
 * where IDCG is the ideal DCG (perfect ranking).
 * Theorem (L4): NDCG handles graded relevance; the log discount models
 * user behavior where higher-ranked results are more likely to be examined. */
double ndcg_at_k(const double *relevance_scores, int32_t num_results, int32_t k);

/* Precision@K: proportion of top-K results that are relevant.
 * Formula: P@K = (1/K) * sum_{i=1}^K rel_i, where rel_i ∈ {0,1}
 * Theorem (L4): P@K ignores recall; useful when user only examines first K. */
double precision_at_k(const int32_t *relevant, int32_t k);

/* Recall@K: proportion of all relevant documents found in top-K.
 * Formula: R@K = (1/num_relevant) * sum_{i=1}^K rel_i */
double recall_at_k(const int32_t *relevant, int32_t k, int32_t total_relevant);

/* MRR (Mean Reciprocal Rank): average of reciprocal ranks for the first
 * correct answer. Formula: MRR = (1/|Q|) * sum_q (1/rank_q)
 * Widely used in QA and recommendation evaluation. */
double mrr_compute(const int32_t *first_relevant_ranks, int32_t num_queries);

/* ===== L5: Ranking Algorithms ===== */

/* PageRank (simplified iterative): computes stationary distribution of
 * a random walk on the document graph. Uses power iteration method.
 * Formula: PR(p_i) = (1-d)/N + d * sum_{p_j ∈ M(p_i)} PR(p_j)/L(p_j)
 * where d=0.85 (damping), L = out-degree, M = in-link set.
 * Converges when |PR_new - PR_old| < epsilon.
 * Theorem (L4): Perron-Frobenius guarantees convergence for stochastic
 * matrices with damping factor d < 1. */
void pagerank_compute(double *ranks, const int32_t *out_degree,
                      const int32_t *in_links, const int32_t *in_link_offsets,
                      int32_t num_nodes, double damping, int32_t max_iters,
                      double epsilon);

/* MMR (Maximal Marginal Relevance): greedy re-ranking to balance
 * relevance and novelty. Iteratively selects the document that maximizes:
 *   MMR = argmax_{d_i ∈ R\S} [ λ * sim_1(d_i, Q) - (1-λ) * max_{d_j ∈ S} sim_2(d_i, d_j) ]
 * where sim_1 = query relevance, sim_2 = inter-document similarity.
 * Theorem (L4): MMR is NP-hard to optimize exactly; greedy gives
 * a (1-1/e) approximation under submodularity (Carbonell 1998). */
int32_t mmr_rerank(const double *query_scores, const double *sim_matrix,
                   int32_t num_docs, int32_t *ranked_indices,
                   int32_t top_k, double lambda);

/* Rocchio Relevance Feedback: reformulates the query vector by
 * incorporating relevance judgments.
 * Formula: Q_new = α * Q_orig + β * (1/|D_rel|) * sum_{d∈D_rel} d
 *                              - γ * (1/|D_nonrel|) * sum_{d∈D_nonrel} d
 * Typical values: α=1.0, β=0.75, γ=0.15 (ide dec-hi)
 * Theorem (L4): Rocchio is optimal under the assumption of spherical,
 * equal-variance class distributions (MRS Ch.9). */
void rocchio_update(double *query_vec, const double **relevant_docs,
                    const double **nonrelevant_docs,
                    int32_t num_rel, int32_t num_nonrel,
                    int32_t dims, double alpha, double beta, double gamma);

#endif
