#include "inverted_index.h"
#include "tokenizer.h"
#include "query_parser.h"
#include "scoring.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static char *g_docs[10] = {
    "apple banana cherry date elderberry",
    "apple banana fig grape honeydew",
    "apple cherry kiwi lemon mango",
    "apple orange pear quince raspberry",
    "banana orange strawberry tangerine watermelon",
    "apple banana orange cherry strawberry",
    "orange pineapple coconut papaya guava",
    "apple banana orange grapefruit lime",
    "banana cherry orange blueberry blackberry",
    "apple orange peach plum apricot"
};

int main(void) {
    InvertedIndex idx;
    index_init(&idx);

    int32_t i;
    for (i = 0; i < 10; i++) {
        Token tokens[MAX_TOKENS];
        const char *p = g_docs[i];
        int32_t tc = 0, pos = 0;

        while (*p && tc < MAX_TOKENS) {
            while (*p == ' ') p++;
            if (!*p) break;
            int32_t ti = 0;
            while (*p && *p != ' ' && ti < MAX_TOKEN_TEXT - 1)
                tokens[tc].text[ti++] = (char)tolower((unsigned char)*p++);
            tokens[tc].text[ti] = '\0';
            tokens[tc].position = pos++;
            tc++;
        }

        const char *tptr[MAX_TOKENS];
        int32_t k;
        for (k = 0; k < tc; k++)
            tptr[k] = tokens[k].text;
        index_add_doc(&idx, i, tptr, &tc);
    }
    idx.total_docs = 10;

    printf("=== Boolean Query Demo ===\n\n");
    printf("Documents in index:\n");
    for (i = 0; i < 10; i++)
        printf("  doc_%d: %s\n", i, g_docs[i]);

    const char *queries[] = {
        "apple AND orange NOT banana",
        "apple AND orange",
        "apple OR banana",
        "apple AND cherry",
        "banana OR orange NOT apple",
        "apple AND banana AND orange",
        "apple pineapple",
        "apple OR banana OR orange"
    };
    int32_t num_queries = 8;

    for (i = 0; i < num_queries; i++) {
        printf("\n--- Query: \"%s\" ---\n", queries[i]);

        QueryNode *tree = query_parse(queries[i]);
        if (!tree) {
            printf("  Failed to parse query.\n");
            continue;
        }

        printf("  Parsed tree (root type=%d):\n", tree->type);

        PostingList *result = query_evaluate(tree, &idx);
        if (!result || result->num_docs == 0) {
            printf("  No matching documents.\n");
        } else {
            printf("  Matching documents (%d):\n", result->num_docs);
            int32_t k;
            for (k = 0; k < result->num_docs; k++) {
                printf("    doc_%d\n", result->postings[k].doc_id);
            }
        }

        posting_list_free(result);
        query_free_node(tree);
    }

    printf("\n--- Phrase Search Demo ---\n");
    {
        PostingList *phrase = query_phrase_search(&idx, "apple", "banana", 1);
        if (phrase && phrase->num_docs > 0) {
            printf("Documents with 'apple banana' (adjacent):\n");
            int32_t k;
            for (k = 0; k < phrase->num_docs; k++)
                printf("  doc_%d\n", phrase->postings[k].doc_id);
        } else {
            printf("No documents with adjacent 'apple banana'.\n");
        }
        posting_list_free(phrase);
    }

    {
        PostingList *phrase2 = query_phrase_search(&idx, "apple", "orange", 1);
        if (phrase2 && phrase2->num_docs > 0) {
            printf("Documents with 'apple orange' (adjacent):\n");
            int32_t k;
            for (k = 0; k < phrase2->num_docs; k++)
                printf("  doc_%d\n", phrase2->postings[k].doc_id);
        } else {
            printf("No documents with adjacent 'apple orange'.\n");
        }
        posting_list_free(phrase2);
    }

    index_free(&idx);
    return 0;
}
