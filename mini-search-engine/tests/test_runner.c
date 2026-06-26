#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "inverted_index.h"
#include "tokenizer.h"
#include "scoring.h"
#include "query_parser.h"
#include "search_engine.h"
#include "index_compression.h"
#include "ranking.h"
#include "language_model.h"
#include "spell_correction.h"

static int32_t g_tests_passed = 0;
static int32_t g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { g_tests_passed++; } \
    else { \
        g_tests_failed++; \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __FILE__, __LINE__, msg); \
    } \
} while(0)

#define TEST_DOUBLE_EQ(a, b, eps, msg) do { \
    if (fabs((a) - (b)) < (eps)) { g_tests_passed++; } \
    else { \
        g_tests_failed++; \
        fprintf(stderr, "FAIL: %s (line %d): %s (%.6f vs %.6f)\n", \
                __FILE__, __LINE__, msg, a, b); \
    } \
} while(0)

/* File scope for variant generation callback (C99 compatible) */
static char variant_buf[1024][64];
static int32_t vcount = 0;

static void collect_variant(const char *v, void *ctx) {
    (void)ctx;
    if (vcount < 1024) {
        strncpy(variant_buf[vcount], v, 63);
        variant_buf[vcount][63] = '\0';
        vcount++;
    }
}

/* ===== Inverted Index Tests (L1-L5) ===== */
static void test_inverted_index(void) {
    InvertedIndex idx;
    index_init(&idx);
    TEST_ASSERT(idx.num_terms == 0, "index_init zero terms");
    TEST_ASSERT(idx.total_docs == 0, "index_init zero docs");

    const char *tokens1[] = {"hello", "world", "hello"};
    int32_t tc = 3;
    index_add_doc(&idx, 0, tokens1, &tc);

    const char *tokens2[] = {"hello", "search", "engine"};
    tc = 3;
    index_add_doc(&idx, 1, tokens2, &tc);

    idx.total_docs = 2;
    TEST_ASSERT(idx.num_terms >= 3, "index_add_doc terms created");

    const PostingList *pl = index_search_term(&idx, "hello");
    TEST_ASSERT(pl != NULL, "search_term hello found");
    TEST_ASSERT(pl->doc_freq == 2, "hello in 2 docs");

    pl = index_search_term(&idx, "nonexistent");
    TEST_ASSERT(pl == NULL, "search_term miss");

    /* Test intersection */
    pl = index_search_term(&idx, "hello");
    const PostingList *pl2 = index_search_term(&idx, "world");
    TEST_ASSERT(pl != NULL && pl2 != NULL, "both terms exist");
    if (pl && pl2) {
        PostingList *inter = posting_list_intersect(pl, pl2);
        TEST_ASSERT(inter != NULL, "intersect creates list");
        if (inter) {
            TEST_ASSERT(inter->num_docs == 1, "intersect hello+world = 1 doc");
            posting_list_free(inter);
        }
    }

    /* Test union */
    if (pl) {
        /* Use a term that exists only in doc 1 */
        const PostingList *ple = index_search_term(&idx, "engine");
        if (ple) {
            PostingList *uni = posting_list_union(pl, ple);
            TEST_ASSERT(uni != NULL, "union creates list");
            if (uni) {
                TEST_ASSERT(uni->num_docs == 2, "union hello+engine = 2 docs");
                posting_list_free(uni);
            }
        }
    }

    index_free(&idx);
}

/* ===== Tokenizer Tests (L5) ===== */
static void test_tokenizer(void) {
    Analyzer a;
    analyzer_init(&a, TOKENIZER_STANDARD,
                  FILTER_LOWERCASE | FILTER_STOP_WORDS | FILTER_STEMMER);

    Token tokens[MAX_TOKENS];
    int32_t n = analyzer_analyze(&a, "The database systems are running efficiently",
                                  tokens, MAX_TOKENS);
    TEST_ASSERT(n > 0, "analyzer returns tokens");
    TEST_ASSERT(n < 10, "stop words removed");

    /* Check stemming: "running" -> "run" */
    int32_t found_run = 0;
    int32_t i;
    for (i = 0; i < n; i++) {
        if (strcmp(tokens[i].text, "run") == 0) found_run = 1;
    }
    TEST_ASSERT(found_run, "porter stemmer running->run");

    /* Test N-gram tokenizer */
    Tokenizer ng;
    tokenizer_init(&ng, TOKENIZER_NGRAM);
    Token ngrams[MAX_TOKENS];
    int32_t ng_count = tokenizer_split(&ng, "hello", ngrams, MAX_TOKENS);
    TEST_ASSERT(ng_count > 1, "ngram creates multiple tokens");

    /* Test stop word */
    TEST_ASSERT(is_stop_word("the", &a), "is_stop_word the");
    TEST_ASSERT(!is_stop_word("database", &a), "!is_stop_word database");
}

/* ===== Scoring Tests (L5) ===== */
static void test_scoring(void) {
    Scorer scorer;
    scorer_init(&scorer, 100, 50.0);

    double tfidf = score_tfidf(3, 10, 100);
    TEST_ASSERT(tfidf > 0.0, "tfidf positive");
    TEST_ASSERT(tfidf < 5.0, "tfidf bounded");

    double bm25 = score_bm25(3, 10, 100, 55, 50.0, 1.2, 0.75);
    TEST_ASSERT(bm25 > 0.0, "bm25 positive");
    TEST_ASSERT(bm25 < 10.0, "bm25 bounded");

    /* Vector space: cosine similarity */
    double qv[] = {1.0, 2.0, 0.0};
    double dv[] = {2.0, 1.0, 3.0};
    double cos_sim = score_vector_space(qv, dv, 3);
    TEST_ASSERT(cos_sim > 0.0, "cosine positive");
    TEST_ASSERT(cos_sim <= 1.0, "cosine <= 1");

    /* Cosine of identical vectors should be 1.0 */
    double qv2[] = {1.0, 2.0, 3.0};
    double dv2[] = {1.0, 2.0, 3.0};
    double cos2 = score_vector_space(qv2, dv2, 3);
    TEST_DOUBLE_EQ(cos2, 1.0, 1e-6, "cosine identical = 1");

    /* Sigmoiq boost */
    double boosted = sigmoid_boost(5.0, 3.0, 1.0);
    TEST_ASSERT(boosted > 0.0, "sigmoid positive");

    /* Decay function */
    double decay = decay_function_gauss(5.0, 0.0, 10.0, 0.0, 0.5);
    TEST_ASSERT(decay >= 0.0, "gauss decay >= 0");
}

/* ===== Query Parser Tests (L5) ===== */
static void test_query_parser(void) {
    /* Build a small index */
    InvertedIndex idx;
    index_init(&idx);

    const char *tokens1[] = {"apple", "banana", "orange"};
    int32_t tc = 3;
    index_add_doc(&idx, 0, tokens1, &tc);

    const char *tokens2[] = {"apple", "grape", "orange"};
    tc = 3;
    index_add_doc(&idx, 1, tokens2, &tc);

    const char *tokens3[] = {"banana", "grape", "kiwi"};
    tc = 3;
    index_add_doc(&idx, 2, tokens3, &tc);
    idx.total_docs = 3;

    /* Test AND query */
    PostingList *result = query_boolean_search(&idx, "apple AND orange");
    TEST_ASSERT(result != NULL, "AND query returns result");
    if (result) {
        TEST_ASSERT(result->num_docs == 2, "apple AND orange = 2 docs");
        posting_list_free(result);
    }

    /* Test OR query */
    result = query_boolean_search(&idx, "apple OR kiwi");
    TEST_ASSERT(result != NULL, "OR query returns result");
    if (result) {
        TEST_ASSERT(result->num_docs >= 2, "apple OR kiwi >= 2 docs");
        posting_list_free(result);
    }

    /* Test NOT query */
    result = query_boolean_search(&idx, "apple NOT banana");
    TEST_ASSERT(result != NULL, "NOT query returns result");
    if (result) {
        /* doc 1 has apple but not banana */
        TEST_ASSERT(result->num_docs >= 1, "apple NOT banana >= 1 doc");
        posting_list_free(result);
    }

    /* Test parse tree */
    QueryNode *tree = query_parse("apple AND orange");
    TEST_ASSERT(tree != NULL, "parse tree created");
    if (tree) {
        TEST_ASSERT(tree->type == NODE_AND, "root is AND node");
        query_free_node(tree);
    }

    index_free(&idx);
}

/* ===== Compression Tests (L5) ===== */
static void test_compression(void) {
    /* VByte roundtrip */
    uint32_t input[] = {1, 127, 128, 300, 1000000};
    uint8_t buf[256];
    int32_t enc_len = vbyte_encode(input, 5, buf, 256);
    TEST_ASSERT(enc_len > 0, "vbyte encode ok");

    uint32_t decoded[5] = {0};
    int32_t dec_count = vbyte_decode(buf, enc_len, decoded, 5);
    TEST_ASSERT(dec_count == 5, "vbyte decode count ok");
    TEST_ASSERT(decoded[0] == 1 && decoded[3] == 300, "vbyte roundtrip");

    /* Delta encoding roundtrip */
    uint32_t sorted[] = {10, 15, 20, 30, 100};
    uint32_t gaps[8] = {0};
    uint32_t recovered[8] = {0};
    int32_t dcount = delta_encode(sorted, 5, gaps, 8);
    TEST_ASSERT(dcount == 5, "delta encode count");
    dcount = delta_decode(gaps, 5, recovered, 8);
    TEST_ASSERT(dcount == 5, "delta decode count");
    TEST_ASSERT(recovered[4] == 100, "delta roundtrip");

    /* Simple9 roundtrip */
    uint32_t small_vals[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint32_t s9_out[32] = {0};
    uint32_t s9_back[32] = {0};
    int32_t s9_words = simple9_encode(small_vals, 10, s9_out, 32);
    TEST_ASSERT(s9_words > 0, "simple9 encode ok");
    int32_t s9_count = simple9_decode(s9_out, s9_words, s9_back, 32);
    TEST_ASSERT(s9_count == 10, "simple9 decode count");
    TEST_ASSERT(s9_back[0] == 1 && s9_back[9] == 10, "simple9 roundtrip");

    /* Elias gamma roundtrip */
    uint32_t gamma_in[] = {1, 5, 10, 100};
    uint8_t gamma_buf[128] = {0};
    uint32_t gamma_out[8] = {0};
    int32_t gbytes = elias_gamma_encode(gamma_in, 4, gamma_buf, 128);
    TEST_ASSERT(gbytes > 0, "gamma encode ok");
    int32_t gcount = elias_gamma_decode(gamma_buf, gbytes * 8, gamma_out, 8);
    TEST_ASSERT(gcount == 4, "gamma decode count");

    /* Elias delta roundtrip */
    uint32_t delta_in[] = {1, 20, 500};
    uint8_t delta_buf[128] = {0};
    uint32_t delta_out[8] = {0};
    int32_t dbytes = elias_delta_encode(delta_in, 3, delta_buf, 128);
    TEST_ASSERT(dbytes > 0, "delta encode ok");
    int32_t dout = elias_delta_decode(delta_buf, dbytes * 8, delta_out, 8);
    TEST_ASSERT(dout == 3, "delta decode count");

    /* Golomb roundtrip */
    uint32_t gmb_in[] = {0, 5, 10, 50};
    uint8_t gmb_buf[256] = {0};
    int32_t gmb_bytes = golomb_encode(gmb_in, 4, gmb_buf, 256, 3);
    TEST_ASSERT(gmb_bytes > 0, "golomb encode ok");
    uint32_t gmb_out[8] = {0};
    int32_t gmb_count = golomb_decode(gmb_buf, gmb_bytes, gmb_out, 8, 3);
    TEST_ASSERT(gmb_count == 4, "golomb decode count");
    TEST_ASSERT(gmb_out[3] == 50, "golomb roundtrip");
}

/* ===== Ranking Tests (L4-L5) ===== */
static void test_ranking(void) {
    /* Precision@K */
    int32_t rel[] = {1, 0, 1, 0, 1};
    double p3 = precision_at_k(rel, 3);
    TEST_DOUBLE_EQ(p3, 2.0/3.0, 0.01, "P@3 = 2/3");

    double p5 = precision_at_k(rel, 5);
    TEST_DOUBLE_EQ(p5, 3.0/5.0, 0.01, "P@5 = 3/5");

    /* Recall@K */
    double r5 = recall_at_k(rel, 5, 3);
    TEST_DOUBLE_EQ(r5, 1.0, 0.01, "R@5 = 1.0");

    /* MAP */
    int32_t rel_all[] = {1, 0, 1, 0, 1, 0, 0, 0, 1, 0};
    double ap = map_compute(rel_all, 10, 4);
    TEST_ASSERT(ap > 0.0 && ap <= 1.0, "MAP in (0,1]");

    /* NDCG */
    double gains[] = {3.0, 2.0, 3.0, 1.0, 0.0};
    double ndcg = ndcg_at_k(gains, 5, 3);
    TEST_ASSERT(ndcg >= 0.0 && ndcg <= 1.0, "NDCG in [0,1]");

    /* MRR */
    int32_t first_ranks[] = {1, 3, 2};
    double mrr = mrr_compute(first_ranks, 3);
    double expected_mrr = (1.0 + 1.0/3.0 + 1.0/2.0) / 3.0;
    TEST_DOUBLE_EQ(mrr, expected_mrr, 0.01, "MRR computation");

    /* MMR */
    double q_scores[] = {0.9, 0.8, 0.7, 0.6, 0.5};
    double sim[25] = {0};
    /* Set some similarities */
    int32_t k;
    for (k = 0; k < 25; k++) sim[k] = 0.0;
    sim[0 * 5 + 1] = 0.9; sim[1 * 5 + 0] = 0.9; /* docs 0,1 similar */
    sim[2 * 5 + 3] = 0.8; sim[3 * 5 + 2] = 0.8; /* docs 2,3 similar */

    int32_t ranked[5] = {0};
    int32_t mmr_count = mmr_rerank(q_scores, sim, 5, ranked, 3, 0.7);
    TEST_ASSERT(mmr_count == 3, "MMR selects 3 docs");
}

/* ===== Language Model Tests (L5) ===== */
static void test_language_model(void) {
    LanguageModel lm;
    lm_init(&lm);

    lm_add_term(&lm, "search", 100);
    lm_add_term(&lm, "engine", 80);
    lm_add_term(&lm, "database", 120);
    lm_add_term(&lm, "index", 60);
    lm_add_term(&lm, "search", 50); /* update */
    lm_build(&lm);

    TEST_ASSERT(lm.vocab_size == 4, "vocab has 4 terms");
    TEST_ASSERT(lm.total_terms == 410, "total terms = 410");

    double p_search = lm_collection_prob(&lm, "search");
    TEST_DOUBLE_EQ(p_search, 150.0/410.0, 0.01, "P(search|C)");

    /* Dirichlet smoothing */
    double p_dir = lm_prob_dirichlet(&lm, "search", 5, 100, 1000.0);
    TEST_ASSERT(p_dir > 0.0, "dirichlet > 0");
    TEST_ASSERT(p_dir < 1.0, "dirichlet < 1");

    /* JM smoothing */
    double p_jm = lm_prob_jelinek_mercer(&lm, "search", 5, 100, 0.5);
    TEST_ASSERT(p_jm > 0.0 && p_jm < 1.0, "JM in (0,1)");

    /* Absolute discounting */
    double p_abs = lm_prob_abs_discount(&lm, "search", 5, 100, 30, 0.5);
    TEST_ASSERT(p_abs >= 0.0, "abs discount >= 0");

    /* Two-stage smoothing */
    double p_ts = lm_prob_two_stage(&lm, "search", 5, 100, 1000.0, 0.1, 0.01);
    TEST_ASSERT(p_ts > 0.0 && p_ts < 1.0, "two-stage in (0,1)");

    /* Perplexity */
    const char *terms[] = {"search", "engine", "database"};
    int32_t doc_tf[12] = {0};
    doc_tf[0 * 4 + 0] = 5; /* doc0: search */
    doc_tf[0 * 4 + 1] = 3; /* doc0: engine */
    doc_tf[0 * 4 + 2] = 2; /* doc0: database */
    int32_t doc_len[1] = {10};
    lm.num_docs = 1;
    double pp = lm_perplexity(&lm, terms, 3, doc_tf, doc_len, 0, 1000.0);
    TEST_ASSERT(pp > 1.0, "perplexity > 1");

    lm_free(&lm);
}

/* ===== Spell Correction Tests (L5) ===== */
static void test_spell_correction(void) {
    /* Levenshtein distance */
    int32_t d = levenshtein_distance("kitten", "sitting");
    TEST_ASSERT(d == 3, "levenshtein kitten->sitting = 3");

    d = levenshtein_distance("", "abc");
    TEST_ASSERT(d == 3, "levenshtein empty->abc = 3");

    d = levenshtein_distance("abc", "abc");
    TEST_ASSERT(d == 0, "levenshtein identical = 0");

    d = levenshtein_distance(NULL, NULL);
    TEST_ASSERT(d == 0, "levenshtein NULL,NULL = 0");

    /* Damerau-Levenshtein */
    d = damerau_levenshtein("teh", "the");
    TEST_ASSERT(d == 1, "damerau teh->the = 1 (transposition)");

    d = damerau_levenshtein("hello", "helol");
    TEST_ASSERT(d == 1 || d == 2, "damerau helol nearby");

    /* Soundex */
    char code[5];
    soundex_encode("Robert", code);
    TEST_ASSERT(strcmp(code, "R163") == 0, "soundex Robert=R163");

    soundex_encode("Rupert", code);
    TEST_ASSERT(strcmp(code, "R163") == 0, "soundex Rupert=R163");

    soundex_encode("Washington", code);
    TEST_ASSERT(strcmp(code, "W252") == 0, "soundex Washington=W252");

    /* N-gram Jaccard */
    double sim = ngram_jaccard_similarity("hello", "hallo", 2);
    TEST_ASSERT(sim > 0.0 && sim < 1.0, "bigram jaccard in (0,1)");

    sim = ngram_jaccard_similarity("hello", "hello", 2);
    TEST_DOUBLE_EQ(sim, 1.0, 0.01, "bigram identical = 1");

    /* Spell correction */
    const char *dict[] = {"hello", "world", "help", "held", "shell"};
    SpellCandidate cands[8];
    int32_t n = spell_correct("helo", dict, 5, cands, 8, 2);
    TEST_ASSERT(n > 0, "spell_correct finds candidates");
    TEST_ASSERT(strcmp(cands[0].word, "hello") == 0 ||
                strcmp(cands[0].word, "help") == 0, "closest candidate");

    /* Variant generation callback test */
    vcount = 0;
    int32_t vn = spell_generate_variants("cat", collect_variant, NULL);
    TEST_ASSERT(vn > 50, "variants generated > 50 for 'cat'");
}

/* ===== Search Engine Integration Test (L6) ===== */
static void test_search_engine(void) {
    SearchEngine engine;
    engine_init(&engine);

    engine_index_document(&engine, 0, "Database Systems",
        "Database management systems are essential for modern applications");
    engine_index_document(&engine, 1, "Search Engines",
        "The search engine uses an inverted index for fast lookup");
    engine_index_document(&engine, 2, "Index Structures",
        "Index structures improve database query performance significantly");

    TEST_ASSERT(engine_get_doc_count(&engine) == 3, "3 docs indexed");
    TEST_ASSERT(engine_get_avgdl(&engine) > 0.0, "avgdl computed");

    const char *title = engine_get_doc_title(&engine, 0);
    TEST_ASSERT(title != NULL && strcmp(title, "Database Systems") == 0,
                "doc title retrieved");

    /* Search test */
    SearchResult results[10];
    int32_t n = engine_search(&engine, "database", results, 10);
    TEST_ASSERT(n > 0, "search finds results for database");

    /* Extended search */
    n = engine_search_ex(&engine, "database", "search", 0, results, 10);
    TEST_ASSERT(n >= 0, "search_ex runs");

    /* Test batch indexing */
    SearchEngine engine2;
    engine_init(&engine2);
    const char *titles[] = {"Doc A", "Doc B"};
    const char *contents[] = {"Content A here", "Content B there"};
    engine_batch_index(&engine2, titles, contents, 2);
    TEST_ASSERT(engine_get_doc_count(&engine2) == 2, "batch indexed 2 docs");
    engine_free(&engine2);

    engine_free(&engine);
}

int main(void) {
    printf("=== mini-search-engine Test Suite ===\n\n");

    printf("Testing inverted index...\n");
    test_inverted_index();

    printf("Testing tokenizer...\n");
    test_tokenizer();

    printf("Testing scoring...\n");
    test_scoring();

    printf("Testing query parser...\n");
    test_query_parser();

    printf("Testing compression...\n");
    test_compression();

    printf("Testing ranking...\n");
    test_ranking();

    printf("Testing language model...\n");
    test_language_model();

    printf("Testing spell correction...\n");
    test_spell_correction();

    printf("Testing search engine integration...\n");
    test_search_engine();

    printf("\n=== Results: %d passed, %d failed ===\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}