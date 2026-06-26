#include "cypher_like.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void skip_whitespace(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static bool match_char(const char **p, char c) {
    skip_whitespace(p);
    if (**p == c) { (*p)++; return true; }
    return false;
}

static bool parse_identifier(const char **p, char *buf, int buf_size) {
    skip_whitespace(p);
    const char *start = *p;
    if (!isalpha((unsigned char)**p) && **p != '_') return false;
    while (**p && (isalnum((unsigned char)**p) || **p == '_')) (*p)++;
    int len = (int)(*p - start);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    return len > 0;
}

static bool parse_string(const char **p, char *buf, int buf_size) {
    skip_whitespace(p);
    if (**p != '\"') {
        const char *start = *p;
        while (**p && isalnum((unsigned char)**p)) (*p)++;
        int len = (int)(*p - start);
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, start, (size_t)len);
        buf[len] = '\0';
        return len > 0;
    }
    (*p)++;
    const char *start = *p;
    while (**p && **p != '\"') (*p)++;
    int len = (int)(*p - start);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    if (**p == '\"') (*p)++;
    return true;
}

static bool parse_labels(const char **p, NodeMatch *nm) {
    skip_whitespace(p);
    if (**p != ':') return true;
    (*p)++;
    while (**p && **p != ')' && **p != '<' && **p != '-') {
        if (**p == ':') (*p)++;
        skip_whitespace(p);
        if (!isalpha((unsigned char)**p)) break;
        if (nm->label_count < MAX_NODE_LABELS) {
            parse_string(p, nm->labels[nm->label_count], MAX_LABEL_LEN);
            nm->label_count++;
        } else {
            while (**p && isalnum((unsigned char)**p)) (*p)++;
        }
        skip_whitespace(p);
    }
    return true;
}

static bool parse_edge_type(const char **p, EdgeMatch *em) {
    skip_whitespace(p);
    if (**p != ':') return false;
    (*p)++;
    return parse_string(p, em->type, MAX_EDGE_TYPE_LEN);
}

bool cypher_parse(const char *query_string, QueryPattern *pattern) {
    memset(pattern, 0, sizeof(QueryPattern));
    const char *p = query_string;

    skip_whitespace(&p);

    if (!match_char(&p, '(')) return false;

    NodeMatch *nm1 = &pattern->node_matches[0];
    pattern->node_match_count = 1;
    parse_identifier(&p, nm1->variable, MAX_LABEL_LEN);
    parse_labels(&p, nm1);

    skip_whitespace(&p);
    if (*p == '{') {
        p++;
        skip_whitespace(&p);
        while (*p && *p != '}') {
            char key[MAX_KEY_LEN], value[MAX_VALUE_LEN];
            if (parse_identifier(&p, key, MAX_KEY_LEN)) {
                skip_whitespace(&p);
                if (*p == ':') {
                    p++;
                    if (parse_string(&p, value, MAX_VALUE_LEN)) {
                        if (nm1->filter_count < MAX_NODE_PROPERTIES) {
                            strncpy(nm1->property_filters[nm1->filter_count].key,
                                    key, MAX_KEY_LEN - 1);
                            strncpy(nm1->property_filters[nm1->filter_count].value,
                                    value, MAX_VALUE_LEN - 1);
                            nm1->filter_count++;
                        }
                    }
                }
            }
            skip_whitespace(&p);
            if (*p == ',') p++;
            skip_whitespace(&p);
        }
        if (*p == '}') p++;
    }

    if (!match_char(&p, ')')) return false;

    skip_whitespace(&p);
    if (*p == '-') {
        p++;
        if (*p == '[') {
            p++;
            EdgeMatch *em = &pattern->edge_matches[0];
            pattern->edge_match_count = 1;
            em->direction = DIR_RIGHT;
            parse_identifier(&p, em->variable, MAX_LABEL_LEN);
            if (parse_edge_type(&p, em)) {
                skip_whitespace(&p);
            }
            skip_whitespace(&p);
            if (*p == ']') p++;
            if (*p == '-') p++;
            if (*p == '>') { p++; em->direction = DIR_RIGHT; }
        } else {
            EdgeMatch *em = &pattern->edge_matches[0];
            pattern->edge_match_count = 1;
            em->direction = DIR_RIGHT;
            if (*p == '>') { p++; em->direction = DIR_RIGHT; }
        }
    } else if (*p == '<') {
        p++;
        if (*p == '-') {
            p++;
            if (*p == '[') {
                p++;
                EdgeMatch *em = &pattern->edge_matches[0];
                pattern->edge_match_count = 1;
                em->direction = DIR_LEFT;
                parse_identifier(&p, em->variable, MAX_LABEL_LEN);
                parse_edge_type(&p, em);
                skip_whitespace(&p);
                if (*p == ']') p++;
            } else {
                EdgeMatch *em = &pattern->edge_matches[0];
                pattern->edge_match_count = 1;
                em->direction = DIR_LEFT;
            }
        }
    }

    skip_whitespace(&p);

    if (!match_char(&p, '(')) return false;

    NodeMatch *nm2 = &pattern->node_matches[1];
    pattern->node_match_count = 2;
    parse_identifier(&p, nm2->variable, MAX_LABEL_LEN);
    parse_labels(&p, nm2);

    skip_whitespace(&p);
    if (*p == '{') {
        p++;
        skip_whitespace(&p);
        while (*p && *p != '}') {
            char key[MAX_KEY_LEN], value[MAX_VALUE_LEN];
            if (parse_identifier(&p, key, MAX_KEY_LEN)) {
                skip_whitespace(&p);
                if (*p == ':') {
                    p++;
                    if (parse_string(&p, value, MAX_VALUE_LEN)) {
                        if (nm2->filter_count < MAX_NODE_PROPERTIES) {
                            strncpy(nm2->property_filters[nm2->filter_count].key,
                                    key, MAX_KEY_LEN - 1);
                            strncpy(nm2->property_filters[nm2->filter_count].value,
                                    value, MAX_VALUE_LEN - 1);
                            nm2->filter_count++;
                        }
                    }
                }
            }
            skip_whitespace(&p);
            if (*p == ',') p++;
            skip_whitespace(&p);
        }
        if (*p == '}') p++;
    }

    if (!match_char(&p, ')')) return false;

    skip_whitespace(&p);
    if (*p != '\0') return false;

    return true;
}

static bool node_matches_filter(PropertyGraph *g, NodeMatch *nm, int64_t node_id) {
    Node *node = graph_get_node(g, node_id);
    if (!node) return false;

    for (int l = 0; l < nm->label_count; l++) {
        bool has_label = false;
        for (int j = 0; j < node->label_count; j++) {
            if (strcmp(node->labels[j], nm->labels[l]) == 0) {
                has_label = true;
                break;
            }
        }
        if (!has_label) return false;
    }

    for (int f = 0; f < nm->filter_count; f++) {
        const char *val = graph_node_get_property(node,
                            nm->property_filters[f].key);
        if (!val || strcmp(val, nm->property_filters[f].value) != 0)
            return false;
    }
    return true;
}

static bool edge_matches_filter(PropertyGraph *g, EdgeMatch *em, int64_t edge_id,
                                int64_t from_node, int64_t to_node) {
    Edge *edge = graph_get_edge(g, edge_id);
    if (!edge) return false;

    if (strlen(em->type) > 0 && strcmp(edge->type, em->type) != 0)
        return false;

    if (em->direction == DIR_RIGHT && edge->from_node != from_node)
        return false;
    if (em->direction == DIR_LEFT && edge->to_node != from_node)
        return false;

    return true;
}

QueryResult cypher_match(PropertyGraph *g, QueryPattern *pattern) {
    QueryResult result;
    memset(&result, 0, sizeof(QueryResult));
    result.success = true;

    if (pattern->node_match_count < 1) {
        result.success = false;
        strcpy(result.error_message, "No node patterns");
        return result;
    }

    NodeMatch *nm_a = &pattern->node_matches[0];
    int n = graph_node_count(g);

    for (int i = 0; i < n && result.row_count < MAX_RESULT_ROWS; i++) {
        int64_t a_id = g->nodes[i].id;
        if (!node_matches_filter(g, nm_a, a_id)) continue;

        if (pattern->node_match_count == 1) {
            QueryRow *row = &result.rows[result.row_count];
            row->binding_count = 1;
            strncpy(row->bindings[0].var_name, nm_a->variable, MAX_LABEL_LEN - 1);
            row->bindings[0].node_id = a_id;
            result.row_count++;
            continue;
        }

        int64_t out_edges[MAX_NODES];
        int out_count = graph_get_out_edges(g, a_id, out_edges, MAX_NODES);

        NodeMatch *nm_b = &pattern->node_matches[1];
        EdgeMatch *em = (pattern->edge_match_count > 0) ?
                        &pattern->edge_matches[0] : NULL;

        for (int e = 0; e < out_count && result.row_count < MAX_RESULT_ROWS; e++) {
            Edge *edge = graph_get_edge(g, out_edges[e]);
            if (!edge) continue;

            int64_t b_id = edge->to_node;
            if (!node_matches_filter(g, nm_b, b_id)) continue;

            if (em) {
                if (strlen(em->type) > 0 &&
                    strcmp(edge->type, em->type) != 0) continue;
            }

            QueryRow *row = &result.rows[result.row_count];
            if (strlen(nm_a->variable) > 0) {
                strncpy(row->bindings[0].var_name, nm_a->variable, MAX_LABEL_LEN - 1);
                row->bindings[0].node_id = a_id;
                row->binding_count = 1;
            }
            if (strlen(nm_b->variable) > 0) {
                strncpy(row->bindings[row->binding_count].var_name,
                        nm_b->variable, MAX_LABEL_LEN - 1);
                row->bindings[row->binding_count].node_id = b_id;
                row->binding_count++;
            }
            if (em && strlen(em->variable) > 0) {
                strncpy(row->bindings[row->binding_count].var_name,
                        em->variable, MAX_LABEL_LEN - 1);
                row->bindings[row->binding_count].node_id = out_edges[e];
                row->binding_count++;
            }
            result.row_count++;
        }
    }
    return result;
}

void cypher_print_results(QueryResult *result) {
    if (!result->success) {
        printf("Query Error: %s\n", result->error_message);
        return;
    }
    printf("\n=== Query Results: %d rows ===\n", result->row_count);
    for (int r = 0; r < result->row_count; r++) {
        printf("  Row %d: ", r + 1);
        QueryRow *row = &result->rows[r];
        for (int b = 0; b < row->binding_count; b++) {
            printf("%s=%lld  ", row->bindings[b].var_name,
                   (long long)row->bindings[b].node_id);
        }
        printf("\n");
    }
}

void cypher_pattern_print(QueryPattern *pattern) {
    printf("Pattern: ");
    for (int i = 0; i < pattern->node_match_count; i++) {
        NodeMatch *nm = &pattern->node_matches[i];
        printf("(%s", nm->variable);
        for (int l = 0; l < nm->label_count; l++)
            printf(":%s", nm->labels[l]);
        printf(")");
        if (i < pattern->node_match_count - 1) {
            EdgeMatch *em = &pattern->edge_matches[i];
            printf("-");
            if (strlen(em->variable) > 0) printf("[%s", em->variable);
            if (strlen(em->type) > 0) printf(":%s", em->type);
            if (strlen(em->variable) > 0) printf("]");
            printf("->");
        }
    }
    printf("\n");
}
