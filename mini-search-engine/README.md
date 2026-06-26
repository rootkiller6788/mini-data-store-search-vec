# mini-search-engine — Full-Text Search Engine (C99)

> Reference: Manning, Raghavan, Schutze. *Introduction to Information Retrieval*
>            Witten, Moffat, Bell. *Managing Gigabytes*
>            Lucene Internals, Elasticsearch

## Module Status: COMPLETE ✅

| Level | Status | Details |
|-------|--------|---------|
| **L1** Definitions | **Complete** | All core structs/typedefs/enums defined (10 structs, 6 enums, 100+ API decls) |
| **L2** Core Concepts | **Complete** | Inverted Index, TF-IDF, BM25, Query Parsing, Porter Stemming, LM for IR |
| **L3** Engineering Structures | **Complete** | Hash-based dictionary, posting lists, analysis chain, AST evaluation |
| **L4** Standards/Theorems | **Complete** | Entropy bound, MAP/NDCG/MRR eval metrics, Golomb optimality, Dirichlet prior |
| **L5** Algorithms/Methods | **Complete** | VByte, Simple9, Elias Gamma/Delta, Golomb, Levenshtein, PageRank, MMR, Rocchio |
| **L6** Canonical Problems | **Complete** | End-to-end search engine: index → query → rank (3 demo examples + test suite) |
| **L7** Applications | **Complete** | Boolean query search, phrase search, faceted search, spell correction |
| **L8** Advanced Topics | **Complete** | Two-stage LM smoothing (Zhai & Lafferty), Pivoted normalization |
| **L9** Industry Frontiers | **Partial** | Documented: neural IR, learned sparse retrieval, dense vector search |
| **Lines** include/ + src/ | **3,446** | Exceeds 3,000 minimum |
| **Tests** | **86 passed, 0 failed** | `make test` |
| **Warnings** | **0** | Clean build with -Wall -Wextra |

## Introduction

A C99 educational full-text search engine using only libc + libm. Implements:
- **Inverted Index** — term→posting list with hash-based dictionary
- **Text Analysis Chain** — tokenization, lowercase, stop words, Porter stemming
- **Query Parser** — recursive descent, generates AST with AND/OR/NOT/PHRASE nodes
- **Scoring** — TF-IDF, BM25, Vector Space, length normalization, sigmoid boost
- **Index Compression** — VByte, Simple9, Elias Gamma/Delta, Golomb/Rice coding
- **Ranking & Evaluation** — PageRank, MMR, Rocchio feedback, MAP, NDCG, MRR
- **Language Modeling** — Dirichlet, JM, Absolute Discounting, Two-Stage smoothing
- **Spell Correction** — Levenshtein, Damerau-Levenshtein, Soundex, N-gram Jaccard

## Directory Structure

```
mini-search-engine/
├── include/                     # 9 header files (614 lines)
│   ├── inverted_index.h         # Inverted index + posting list API
│   ├── tokenizer.h              # Tokenizer, analyzer, filter flags
│   ├── scoring.h                # TF-IDF, BM25, vector space, decay functions
│   ├── query_parser.h           # Query AST, evaluation, top-K
│   ├── search_engine.h          # High-level engine coordinator
│   ├── index_compression.h      # VByte, Simple9, Elias codes, Golomb
│   ├── ranking.h                # PageRank, MMR, Rocchio, MAP, NDCG
│   ├── language_model.h         # LM for IR, smoothing methods
│   └── spell_correction.h       # Edit distance, Soundex, fuzzy matching
├── src/                         # 9 implementation files (2,832 lines)
│   ├── inverted_index.c         # 351 lines — djb2 hash, linear probing, merge ops
│   ├── tokenizer.c              # 325 lines — tokenizer types, Porter stemmer step 1b1
│   ├── scoring.c                # 210 lines — 14 scoring functions
│   ├── query_parser.c           # 360 lines — recursive descent, AST eval
│   ├── search_engine.c          # 168 lines — engine coordinator
│   ├── index_compression.c      # 419 lines — 5 compression codecs + bit I/O
│   ├── ranking.c                # 399 lines — 7 evaluation metrics + 3 ranking algorithms
│   ├── language_model.c         # 284 lines — LM vocabulary, 4 smoothing methods
│   └── spell_correction.c       # 316 lines — 2 edit distances, Soundex, n-gram
├── tests/
│   └── test_runner.c            # 505 lines — 86 assert-based tests
├── examples/
│   ├── index_demo.c             # Build index + term stats
│   ├── scoring_demo.c           # TF-IDF vs BM25 comparison
│   └── boolean_query_demo.c     # Boolean + phrase search
├── demos/
│   ├── mini-inverted-index/     # Inverted index deep dive
│   └── mini-text-search/        # Full-text search deep dive
├── docs/
│   ├── course-alignment.md      # 9-school curriculum mapping
│   └── search-engine-architecture.md  # Architecture documentation
├── Makefile
└── README.md
```

## Quick Start

### Build
```bash
make
```

### Run Tests
```bash
make test
```

### Run Demos
```bash
make run-index-demo        # Build index + query terms
make run-scoring-demo      # TF-IDF vs BM25 comparison
make run-boolean-demo      # AND/OR/NOT queries + phrase search
```

### Clean
```bash
make clean
```

## Knowledge Coverage (9 Levels)

### L1 — Core Definitions
| Definition | Location |
|-----------|----------|
| PostingEntry, PostingList, InvertedIndex | `inverted_index.h` |
| Token, Tokenizer, Analyzer, TokenizerType, AnalyzerFilter | `tokenizer.h` |
| Scorer, scoring function signatures | `scoring.h` |
| QueryNode, QueryNodeType, SearchResult | `query_parser.h` |
| SearchEngine | `search_engine.h` |
| LmVocabEntry, LanguageModel | `language_model.h` |
| SpellCandidate | `spell_correction.h` |

### L2 — Core Concepts
- Inverted Index: term→posting list mapping, hash-based dictionary
- Analysis Chain: Tokenize → Lowercase → Stop Words → Stem
- Boolean Retrieval: AND (intersect), OR (union), NOT (exclude)
- Ranked Retrieval: TF-IDF, BM25 probabilistic model, Vector Space
- Query Parsing: Recursive descent, AST construction and evaluation
- Language Modeling for IR: Query-likelihood, Bayesian smoothing
- Spell Correction: Edit distance, phonetic encoding, n-gram matching

### L3 — Engineering Structures
- Hash Map: djb2 hash, open addressing with linear probing (2048 buckets)
- Dynamic Posting Lists: realloc-based growth, sorted by doc_id
- Two-Pointer Merge: O(|A|+|B|) intersection, union, exclusion
- Analysis Pipeline: composable filter chain (bitmask flags)
- Query AST: recursive tree data structure, bottom-up evaluation

### L4 — Standards/Theorems
| Theorem | Implementation |
|---------|---------------|
| Information Entropy Lower Bound | `index_compression.c` — documented in VByte/Simple9 sections |
| Golomb Optimality for Geometric Distributions | `index_compression.c` — `golomb_optimal_k()` |
| Kraft-McMillan Inequality | `index_compression.c` — prefix code constraints |
| MAP (Mean Average Precision) | `ranking.c` — `map_compute()` |
| NDCG@K | `ranking.c` — `ndcg_at_k()` |
| MRR | `ranking.c` — `mrr_compute()` |
| Precision/Recall@K | `ranking.c` — `precision_at_k()`, `recall_at_k()` |
| Bayes Rule for LM retrieval | `language_model.c` — P(D\|Q) ∝ P(Q\|D)·P(D) |
| Dirichlet-Multinomial Conjugacy | `language_model.c` — `lm_prob_dirichlet()` |
| Shrinkage Estimation (JM) | `language_model.c` — `lm_prob_jelinek_mercer()` |
| Good-Turing Frequency Estimation | `language_model.c` — `lm_prob_abs_discount()` |
| Edit Distance Metric Properties | `spell_correction.c` — Levenshtein metric proof |
| Perron-Frobenius Theorem | `ranking.c` — PageRank convergence guarantee |

### L5 — Algorithms/Methods
| Algorithm | Implementation |
|-----------|---------------|
| VByte Encoding/Decoding | `index_compression.c` — 7-bit continuation |
| Delta/Gap Encoding | `index_compression.c` — sorted list gaps |
| Simple9 Word-Aligned Packing | `index_compression.c` — 4-bit selector + 28-bit data |
| Elias Gamma Coding | `index_compression.c` — universal code for small ints |
| Elias Delta Coding | `index_compression.c` — gamma-encoded length prefix |
| Golomb/Rice Coding | `index_compression.c` — unary quotient + binary remainder |
| TF-IDF (sublinear tf) | `scoring.c` — log(1+tf) × IDF |
| BM25 (Okapi) | `scoring.c` — k1=1.2, b=0.75 |
| Cosine Similarity (Vector Space) | `scoring.c` — dot product / (norm_q × norm_d) |
| Porter Stemmer (Step 1a/1b/1b1/3) | `tokenizer.c` — suffix stripping rules |
| Recursive Descent Parser | `query_parser.c` — expr → and_expr → atom |
| Phrase Search (positional) | `query_parser.c` — proximity check via positions |
| PageRank (Power Iteration) | `ranking.c` — damping, dangling nodes |
| MMR (Maximal Marginal Relevance) | `ranking.c` — greedy diversification |
| Rocchio Relevance Feedback | `ranking.c` — query vector reformulation |
| Levenshtein Edit Distance | `spell_correction.c` — 2-row DP, O(mn) time |
| Damerau-Levenshtein | `spell_correction.c` — transposition support |
| Soundex Phonetic Encoding | `spell_correction.c` — name→4-char code |
| N-gram Jaccard Similarity | `spell_correction.c` — boundary-padded n-grams |

### L6 — Canonical Problems
- **Boolean Search Engine**: `examples/boolean_query_demo.c` — AND/OR/NOT + phrase search
- **Ranked Retrieval**: `examples/scoring_demo.c` — TF-IDF vs BM25 comparison
- **Index Construction**: `examples/index_demo.c` — build and query inverted index
- **End-to-End Integration**: `tests/test_runner.c` — engine_init → index → search → verify

### L7 — Applications (≥2)
1. Boolean query search with configurable AND/OR/NOT operators
2. Phrase search with proximity constraints
3. Faceted search via `engine_search_ex()` with filter terms
4. Spell correction with edit-distance candidate ranking
5. Batch document indexing via `engine_batch_index()`

### L8 — Advanced Topics (≥1)
1. **Two-Stage Language Model Smoothing** (`lm_prob_two_stage`): Dirichlet + JM, per Zhai & Lafferty (SIGIR 2002)
2. **Pivoted Document Length Normalization** (`score_pivoted_normalization`): Singhal et al. (SIGIR 1996)
3. **BM25F Weighted Fields** (`score_bm25f_weighted`): multi-field scoring
4. **Sigmoid Score Boosting** (`sigmoid_boost`): non-linear relevance transformation
5. **Gaussian Decay Functions** (`decay_function_gauss`): time/location-based scoring

### L9 — Industry Frontiers (Documented)
- Neural IR: BERT-based dense retrieval (ColBERT, DPR)
- Learned Sparse Retrieval: SPLADE, DeepImpact
- Approximate Nearest Neighbor (ANN): HNSW, IVF-PQ for vector search
- Hybrid Search: sparse (BM25) + dense (embeddings) fusion
- AI-powered relevance: Learning-to-Rank (LambdaMART), LLM-based reranking

## Core Theorems (Formulae)

| Theorem | Formula |
|---------|---------|
| TF-IDF | TF = ln(1+tf), IDF = log((N+1)/(df+0.5)), Score = TF×IDF |
| BM25 | IDF×tf(k1+1)/(tf+k1(1-b+b×\|d\|/avgdl)) |
| Cosine Similarity | (q·d) / (\|q\|×\|d\|) |
| NDCG@K | DCG@K / IDCG@K, DCG@K = Σ(2^rel_i-1)/log₂(i+1) |
| MAP | (1/\|Q\|) Σ_q (1/rel_q) Σ_{k:rel(k)=1} P@k |
| PageRank | PR(p) = (1-d)/N + d Σ PR(in)/L(in) |
| MMR | argmax[λ·sim₁(d,Q) - (1-λ)·max sim₂(d,S)] |
| Rocchio | Q' = αQ + β·centroid(D_rel) - γ·centroid(D_nonrel) |
| Dirichlet Smoothing | P(w\|D) = (c(w,D)+μ·P(w\|C))/(\|D\|+μ) |
| Jelinek-Mercer | P(w\|D) = (1-λ)·P_ml(w\|D) + λ·P(w\|C) |
| Golomb Code | n → unary(q) + k-bit(r), where n=q·2^k+r |

## 9-School Curriculum Mapping

| School | Key Courses | Module Coverage |
|--------|------------|-----------------|
| **MIT** | 6.004 Computation Structures | Hash maps, data structures |
| **Stanford** | CS 276 Information Retrieval | IR core: index, scoring, eval |
| **Berkeley** | CS 186 Database Systems | Index structures, query processing |
| **CMU** | 11-442 Search Engines | Full search pipeline |
| **Cambridge** | Part II: IR & NLP | Tokenization, stemming, LM |
| **ETH** | 263-5300 Information Retrieval | Probabilistic models, evaluation |
| **Tsinghua** | 信息检索导论 | 倒排索引, 布尔/排序检索, 中文扩展 |
| **Georgia Tech** | CS 7641 Machine Learning | Learning to Rank (L9 documented) |
| **UT Austin** | CS 380D Distributed Computing | Distributed IR (L9 documented) |

## API Summary

### Search Engine (High-Level)
```c
SearchEngine engine;
SearchResult results[10];

engine_init(&engine);
engine_index_document(&engine, 0, "Title", "content...");
int n = engine_search(&engine, "query", results, 10);
engine_print_results(results, n);
engine_free(&engine);
```

### Index Compression (New)
```c
uint32_t input[] = {1, 128, 1000000};
uint8_t buf[256];
int32_t len = vbyte_encode(input, 3, buf, 256);
// buf = {0x01, 0x80 0x01, 0xC0 0x84 0x3D} (7 bytes vs 12)
```

### Ranking & Evaluation (New)
```c
double ndcg = ndcg_at_k(relevance_scores, num_results, k);
double mrr  = mrr_compute(first_relevant_ranks, num_queries);
mmr_rerank(query_scores, sim_matrix, n, ranked, top_k, 0.7);
```

### Spell Correction (New)
```c
int32_t d = levenshtein_distance("kitten", "sitting");  // = 3
char code[5]; soundex_encode("Robert", code);  // "R163"
SpellCandidate cands[8];
int32_t n = spell_correct("helo", dict, 5, cands, 8, 2);
```

## Design Constraints

- **C99 Standard**: block-scope declarations, `//` comments
- **libc + libm only**: zero external dependencies
- **Fixed upper bounds**: macros for max terms, docs, posting list sizes
- **Explicit memory management**: every calloc/realloc has a corresponding free
- **No filler code**: every function implements an independent knowledge point

## References

- Manning, Raghavan, Schutze. *Introduction to Information Retrieval*. Cambridge, 2008
- Robertson & Zaragoza. *The Probabilistic Relevance Framework: BM25 and Beyond*, 2009
- Porter, M.F. *An Algorithm for Suffix Stripping*, Program 14(3), 1980
- Witten, Moffat, Bell. *Managing Gigabytes*, 1999
- Zobel & Moffat. *Inverted files for text search engines*, ACM CSUR 2006
- Zhai & Lafferty. *A Study of Smoothing Methods*, TOIS 2004
- Page, Brin et al. *The PageRank Citation Ranking*, 1999
- Carbonell & Goldstein. *MMR*, SIGIR 1998
- Damerau. *Spelling error correction*, CACM 1964
