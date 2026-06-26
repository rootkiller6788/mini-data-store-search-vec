#include "inverted_index.h"
#include "tokenizer.h"
#include <stdio.h>
#include <string.h>

static char *g_docs[10] = {
    "The database system stores data efficiently for retrieval",
    "Database management systems are essential for modern applications",
    "A file system organizes files in a directory structure",
    "The search engine uses an inverted index for fast lookup",
    "Index structures improve database query performance",
    "Inverted indexes are the core of full-text search systems",
    "Data storage and retrieval are fundamental to database design",
    "Modern search engines process millions of queries per second",
    "The indexing pipeline includes tokenization and stemming",
    "Database search requires efficient index data structures"
};

int main(void) {
    int32_t i;

    printf("=== Inverted Index Demo ===\n\n");
    printf("Building inverted index from %d documents:\n", 10);
    for (i = 0; i < 10; i++)
        printf("  [doc_%d] %s\n", i, g_docs[i]);
    printf("\n");

    InvertedIndex idx;
    index_init(&idx);
    index_build(&idx, g_docs, 10);

    printf("Index built: %d terms, %d total docs\n",
           idx.num_terms, idx.total_docs);

    printf("\n--- Searching for term: \"database\" ---\n");
    index_print_term_stats(&idx, "database");

    printf("\n--- Searching for term: \"search\" ---\n");
    index_print_term_stats(&idx, "search");

    printf("\n--- Searching for term: \"index\" ---\n");
    index_print_term_stats(&idx, "index");

    printf("\n--- Searching for term: \"system\" ---\n");
    index_print_term_stats(&idx, "system");

    printf("\n--- Searching for term: \"data\" ---\n");
    index_print_term_stats(&idx, "data");

    printf("\n--- Searching for nonexistent term: \"quantum\" ---\n");
    index_print_term_stats(&idx, "quantum");

    printf("\n--- Showing all terms in index ---\n");
    for (i = 0; i < HASH_MAP_SIZE; i++) {
        if (idx.entries[i].occupied) {
            printf("  '%s' -> %d docs\n",
                   idx.entries[i].term, idx.entries[i].list.doc_freq);
        }
    }

    index_free(&idx);
    return 0;
}
