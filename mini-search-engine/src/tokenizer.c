#include "tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *g_stop_word_list[] = {
    "a", "an", "the", "and", "or", "but", "in", "on", "at", "to",
    "for", "of", "with", "by", "from", "is", "was", "are", "were",
    "be", "been", "being", "have", "has", "had", "do", "does", "did",
    "will", "would", "shall", "should", "may", "might", "must", "can",
    "could", "it", "its", "he", "she", "they", "we", "you", "i",
    "this", "that", "these", "those", "am", "not", "no", "if",
    "so", "as", "than", "too", "very", "just", "about", "into",
    "over", "after", "before", "between", "under", "again", "further",
    "then", "once", "here", "there", "all", "each", "every", "both",
    "few", "more", "most", "other", "some", "such", "only", "own",
    "same", "up", "out", "off", "down", "his", "her", "my", "our",
    "their", "me", "him", "us", "them", "what", "which", "who",
    "whom", "when", "where", "why", "how"
};
#define NUM_BUILTIN_STOP_WORDS \
    (sizeof(g_stop_word_list) / sizeof(g_stop_word_list[0]))

void tokenizer_init(Tokenizer *t, TokenizerType type) {
    t->type = type;
    t->ngram_min = 2;
    t->ngram_max = 3;
}

static int32_t is_delim(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static int32_t is_punct(char c) {
    return (c == '.' || c == ',' || c == ';' || c == ':' ||
            c == '!' || c == '?' || c == '(' || c == ')' ||
            c == '[' || c == ']' || c == '{' || c == '}' ||
            c == '\"' || c == '\'');
}

int32_t tokenizer_split(const Tokenizer *t, const char *text,
                        Token *tokens, int32_t max_tokens) {
    if (!text || !tokens || max_tokens <= 0) return 0;

    int32_t count = 0, pos = 0;
    const char *p = text;

    while (*p && count < max_tokens) {
        while (*p && (is_delim(*p) || (t->type == TOKENIZER_STANDARD && is_punct(*p))))
            p++;
        if (!*p) break;

        Token *tok = &tokens[count];
        tok->start_offset = (int32_t)(p - text);
        tok->position = pos;

        int32_t ti = 0;
        while (*p && !is_delim(*p) &&
               !(t->type == TOKENIZER_STANDARD && is_punct(*p)) &&
               ti < MAX_TOKEN_TEXT - 1) {
            tok->text[ti++] = *p;
            p++;
        }
        tok->text[ti] = '\0';

        if (ti > 0) {
            if (t->type == TOKENIZER_NGRAM) {
                int32_t n, s;
                char ngram_buf[MAX_TOKENS][MAX_TOKEN_TEXT];
                int32_t ng_count = 0;
                int32_t len = ti;

                for (n = t->ngram_min; n <= t->ngram_max && n <= len; n++) {
                    for (s = 0; s <= len - n && ng_count < MAX_TOKENS && count < max_tokens; s++) {
                        int32_t k;
                        for (k = 0; k < n; k++)
                            ngram_buf[ng_count][k] = tok->text[s + k];
                        ngram_buf[ng_count][n] = '\0';

                        if (ng_count == 0 && n == t->ngram_min && s == 0) {
                            memcpy(tok->text, ngram_buf[ng_count], (size_t)n + 1);
                            tok->position = pos;
                            ng_count++;
                        } else if (count + ng_count < max_tokens) {
                            Token *nt = &tokens[count + ng_count];
                            memcpy(nt->text, ngram_buf[ng_count], (size_t)n + 1);
                            nt->position = pos;
                            nt->start_offset = tok->start_offset + s;
                            ng_count++;
                        }
                    }
                }
                count += ng_count;
                pos++;
            } else {
                count++;
                pos++;
            }
        }
    }
    return count;
}

static void to_lowercase(char *s) {
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

int32_t is_stop_word(const char *word, const Analyzer *a) {
    int32_t i;
    for (i = 0; i < a->num_stop_words; i++) {
        if (strcmp(word, a->stop_words[i]) == 0)
            return 1;
    }
    for (i = 0; i < (int32_t)NUM_BUILTIN_STOP_WORDS; i++) {
        if (strcmp(word, g_stop_word_list[i]) == 0)
            return 1;
    }
    return 0;
}

void analyzer_init(Analyzer *a, TokenizerType type, int32_t filters) {
    tokenizer_init(&a->tokenizer, type);
    a->filters = filters;
    a->num_stop_words = 0;
}

int32_t analyzer_analyze(Analyzer *a, const char *text,
                         Token *tokens, int32_t max_tokens) {
    Token raw[MAX_TOKENS];
    int32_t total = tokenizer_split(&a->tokenizer, text, raw, MAX_TOKENS);
    if (total == 0) return 0;

    if (a->filters & FILTER_LOWERCASE) {
        int32_t i;
        for (i = 0; i < total; i++)
            to_lowercase(raw[i].text);
    }

    int32_t kept = total;
    Token *work = raw;
    Token filtered[MAX_TOKENS];

    if (a->filters & FILTER_STOP_WORDS) {
        kept = 0;
        int32_t i;
        for (i = 0; i < total; i++) {
            if (!is_stop_word(work[i].text, a)) {
                filtered[kept++] = work[i];
            }
        }
        work = filtered;
    }

    if (a->filters & FILTER_STEMMER) {
        int32_t i;
        for (i = 0; i < kept; i++)
            porter_stem(work[i].text);
    }

    int32_t out = kept;
    if (out > max_tokens) out = max_tokens;
    int32_t i;
    for (i = 0; i < out; i++)
        tokens[i] = work[i];

    return out;
}

static int32_t word_has_vowel(const char *word, int32_t len) {
    int32_t i;
    for (i = 0; i < len; i++) {
        char c = (char)tolower((unsigned char)word[i]);
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
            return 1;
        if (c == 'y' && i > 0) return 1;
    }
    return 0;
}

static int32_t measure_m(const char *word, int32_t len) {
    int32_t n = 0, i = 0, in_vc = 0;
    if (len < 2) return 0;
    while (i < len) {
        char c = (char)tolower((unsigned char)word[i]);
        int32_t is_v = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
                        (c == 'y' && i > 0));
        if (!in_vc) {
            if (is_v) in_vc = 1;
        } else {
            if (!is_v) { n++; in_vc = 0; }
        }
        i++;
    }
    return n;
}

static int32_t ends_with(const char *str, int32_t len, const char *suffix) {
    int32_t slen = (int32_t)strlen(suffix);
    if (len < slen) return 0;
    return strncmp(str + len - slen, suffix, (size_t)slen) == 0;
}

/* Check if word ends with double consonant (*d rule in Porter) */
static int32_t ends_with_double_consonant(const char *word, int32_t len) {
    if (len < 2) return 0;
    char c1 = (char)tolower((unsigned char)word[len - 1]);
    char c2 = (char)tolower((unsigned char)word[len - 2]);
    if (c1 != c2) return 0;
    /* Must be a consonant */
    if (c1 == 'a' || c1 == 'e' || c1 == 'i' || c1 == 'o' || c1 == 'u') return 0;
    if (c1 == 'y' && len > 2) return 0; /* y can be vowel after position 0 */
    return 1;
}

/* Check if word ends with CVC (*o rule) where second C is not w,x,y */
static int32_t ends_with_cvc(const char *word, int32_t len) {
    if (len < 3) return 0;
    char c3 = (char)tolower((unsigned char)word[len - 1]);
    char c2 = (char)tolower((unsigned char)word[len - 2]);
    char c1 = (char)tolower((unsigned char)word[len - 3]);

    /* Check c3 is consonant (not w,x,y) */
    if (c3 == 'a' || c3 == 'e' || c3 == 'i' || c3 == 'o' || c3 == 'u') return 0;
    if (c3 == 'w' || c3 == 'x' || c3 == 'y') return 0;

    /* Check c2 is vowel */
    if (c2 != 'a' && c2 != 'e' && c2 != 'i' && c2 != 'o' && c2 != 'u') {
        if (c2 != 'y' || len <= 3) return 0;
    }

    /* Check c1 is consonant */
    if (c1 == 'a' || c1 == 'e' || c1 == 'i' || c1 == 'o' || c1 == 'u') return 0;
    if (c1 == 'y' && (len - 3) > 0) return 0;

    return 1;
}

/* Porter step 1b1: applied after removing -ed or -ing */
static int32_t porter_step_1b1(char *word, int32_t len) {
    if (ends_with(word, len, "at") || ends_with(word, len, "bl") ||
        ends_with(word, len, "iz")) {
        word[len] = 'e';
        word[len + 1] = '\0';
        return len + 1;
    }
    /* *d rule: double consonant at end (not l,s,z) → remove last */
    if (ends_with_double_consonant(word, len)) {
        char last = (char)tolower((unsigned char)word[len - 1]);
        if (last != 'l' && last != 's' && last != 'z') {
            word[len - 1] = '\0';
            return len - 1;
        }
    }
    /* *o rule: m=1 and ends with CVC → add 'e' */
    if (measure_m(word, len) == 1 && ends_with_cvc(word, len)) {
        word[len] = 'e';
        word[len + 1] = '\0';
        return len + 1;
    }
    return len;
}

int32_t porter_stem(char *word) {
    int32_t len = (int32_t)strlen(word);
    if (len < 3) return len;

    if (ends_with(word, len, "sses")) {
        word[len - 2] = '\0';
        return len - 2;
    }
    if (ends_with(word, len, "ies")) {
        word[len - 3] = 'i';
        word[len - 2] = '\0';
        return len - 2;
    }
    if (ends_with(word, len, "ss")) return len;
    if (ends_with(word, len, "s") && len > 2) {
        word[len - 1] = '\0';
        len--;
    }

    if (ends_with(word, len, "eed")) {
        if (measure_m(word, len) > 0) {
            word[len - 1] = '\0';
            len--;
        }
    } else if (ends_with(word, len, "ed") && word_has_vowel(word, len - 2)) {
        word[len - 2] = '\0';
        len -= 2;
        len = porter_step_1b1(word, len);
    } else if (ends_with(word, len, "ing") && word_has_vowel(word, len - 3)) {
        word[len - 3] = '\0';
        len -= 3;
        len = porter_step_1b1(word, len);
    }

    if (ends_with(word, len, "ational")) {
        word[len - 5] = 'e'; word[len - 4] = '\0'; len -= 4;
    } else if (ends_with(word, len, "tional")) {
        word[len - 2] = '\0'; len -= 2;
    } else if (ends_with(word, len, "enci")) {
        word[len - 1] = 'e'; len -= 0;
    } else if (ends_with(word, len, "anci")) {
        word[len - 1] = 'e'; len -= 0;
    } else if (ends_with(word, len, "izer")) {
        word[len - 1] = '\0'; len--;
    } else if (ends_with(word, len, "ment")) {
        word[len - 4] = '\0'; len -= 4;
    } else if (ends_with(word, len, "ness")) {
        word[len - 4] = '\0'; len -= 4;
    } else if (ends_with(word, len, "ity")) {
        word[len - 3] = '\0'; len -= 3;
    } else if (ends_with(word, len, "ence")) {
        word[len - 4] = '\0'; len -= 4;
    } else if (ends_with(word, len, "ance")) {
        word[len - 4] = '\0'; len -= 4;
    }

    (void)word_has_vowel;
    (void)ends_with;
    return len;
}
