#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdint.h>

#define MAX_TOKEN_TEXT   64
#define MAX_TOKENS       512
#define MAX_STOP_WORDS   64

typedef enum {
    TOKENIZER_WHITESPACE,
    TOKENIZER_STANDARD,
    TOKENIZER_NGRAM
} TokenizerType;

typedef struct {
    char    text[MAX_TOKEN_TEXT];
    int32_t position;
    int32_t start_offset;
} Token;

typedef struct {
    TokenizerType type;
    int32_t       ngram_min;
    int32_t       ngram_max;
} Tokenizer;

typedef enum {
    FILTER_LOWERCASE  = 1 << 0,
    FILTER_STOP_WORDS = 1 << 1,
    FILTER_STEMMER    = 1 << 2
} AnalyzerFilter;

typedef struct {
    Tokenizer tokenizer;
    int32_t   filters;
    char      stop_words[MAX_STOP_WORDS][MAX_TOKEN_TEXT];
    int32_t   num_stop_words;
} Analyzer;

void    tokenizer_init(Tokenizer *t, TokenizerType type);
int32_t tokenizer_split(const Tokenizer *t, const char *text,
                        Token *tokens, int32_t max_tokens);

void    analyzer_init(Analyzer *a, TokenizerType type, int32_t filters);
int32_t analyzer_analyze(Analyzer *a, const char *text,
                         Token *tokens, int32_t max_tokens);

int32_t is_stop_word(const char *word, const Analyzer *a);
int32_t porter_stem(char *word);

#endif
