#include "sql_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *sql;
    int         pos;
    int         len;
} Tokenizer;

static void tok_skip_whitespace(Tokenizer *t) {
    while (t->pos < t->len && isspace((unsigned char)t->sql[t->pos]))
        t->pos++;
}

static void tok_skip_ws_and_comma(Tokenizer *t) {
    tok_skip_whitespace(t);
    if (t->pos < t->len && t->sql[t->pos] == ',') {
        t->pos++;
        tok_skip_whitespace(t);
    }
}

static int tok_match_word(Tokenizer *t, const char *word) {
    int save = t->pos;
    tok_skip_whitespace(t);
    size_t wlen = strlen(word);
    if (t->pos + wlen <= (size_t)t->len) {
        int match = 1;
        for (size_t i = 0; i < wlen; i++) {
            if (toupper((unsigned char)t->sql[t->pos + i]) != toupper((unsigned char)word[i])) {
                match = 0; break;
            }
        }
        if (match && (t->pos + wlen >= (size_t)t->len ||
            !isalnum((unsigned char)t->sql[t->pos + wlen]) && t->sql[t->pos + wlen] != '_')) {
            t->pos += (int)wlen;
            return 1;
        }
    }
    t->pos = save;
    return 0;
}

static int tok_read_identifier(Tokenizer *t, char *out, int max_len) {
    tok_skip_whitespace(t);
    int start = t->pos;
    while (t->pos < t->len &&
           (isalnum((unsigned char)t->sql[t->pos]) || t->sql[t->pos] == '_')) {
        t->pos++;
    }
    int n = t->pos - start;
    if (n == 0 || n >= max_len) return 0;
    memcpy(out, t->sql + start, n);
    out[n] = '\0';
    return 1;
}

static int tok_read_value(Tokenizer *t, char *out, int max_len) {
    tok_skip_whitespace(t);
    if (t->pos < t->len && t->sql[t->pos] == '\'') {
        t->pos++;
        int start = t->pos;
        while (t->pos < t->len && t->sql[t->pos] != '\'') t->pos++;
        int n = t->pos - start;
        if (t->pos < t->len) t->pos++;
        if (n > 0 && n < max_len) {
            memcpy(out, t->sql + start, n);
            out[n] = '\0';
            return 1;
        }
        return 0;
    }
    int start = t->pos;
    if (t->sql[t->pos] == '-' || isdigit((unsigned char)t->sql[t->pos])) {
        if (t->sql[t->pos] == '-') t->pos++;
        while (t->pos < t->len && isdigit((unsigned char)t->sql[t->pos])) t->pos++;
    } else {
        while (t->pos < t->len &&
               (isalnum((unsigned char)t->sql[t->pos]) || t->sql[t->pos] == '_' || t->sql[t->pos] == '.')) {
            t->pos++;
        }
    }
    int n = t->pos - start;
    if (n == 0 || n >= max_len) return 0;
    memcpy(out, t->sql + start, n);
    out[n] = '\0';
    return 1;
}

static SQLCmpOp tok_parse_cmp_op(Tokenizer *t) {
    tok_skip_whitespace(t);
    if (t->pos + 1 < t->len) {
        char c1 = t->sql[t->pos], c2 = t->sql[t->pos + 1];
        if (c1 == '!' && c2 == '=') { t->pos += 2; return SQL_CMP_NE; }
        if (c1 == '<' && c2 == '=') { t->pos += 2; return SQL_CMP_LE; }
        if (c1 == '>' && c2 == '=') { t->pos += 2; return SQL_CMP_GE; }
    }
    if (t->pos < t->len) {
        switch (t->sql[t->pos]) {
            case '=': t->pos++; return SQL_CMP_EQ;
            case '<': t->pos++; return SQL_CMP_LT;
            case '>': t->pos++; return SQL_CMP_GT;
        }
    }
    return SQL_CMP_NONE;
}

int sql_parse_select(const char *sql, SQLStmt *stmt) {
    memset(stmt, 0, sizeof(SQLStmt));
    stmt->type = SQL_SELECT;

    Tokenizer t = { sql, 0, (int)strlen(sql) };

    if (!tok_match_word(&t, "SELECT")) return 0;

    stmt->num_columns = 0;
    if (tok_match_word(&t, "*")) {
        strcpy(stmt->columns[0], "*");
        stmt->num_columns = 1;
    } else {
        do {
            if (stmt->num_columns >= SQL_MAX_COLUMNS) return 0;
            if (!tok_read_identifier(&t, stmt->columns[stmt->num_columns], SQL_MAX_NAME))
                return 0;
            stmt->num_columns++;
        } while (t.pos < t->len && t.sql[t.pos] == ',' &&
                 (t.pos++, tok_skip_whitespace(&t), 1));
    }

    if (!tok_match_word(&t, "FROM")) return 0;
    if (!tok_read_identifier(&t, stmt->table, SQL_MAX_NAME)) return 0;

    stmt->has_where = 0;
    stmt->has_order_by = 0;

    if (tok_match_word(&t, "WHERE")) {
        if (!tok_read_identifier(&t, stmt->where_clause.col.name, SQL_MAX_NAME))
            return 0;
        stmt->where_clause.op = tok_parse_cmp_op(&t);
        if (stmt->where_clause.op == SQL_CMP_NONE) return 0;
        char val[SQL_MAX_NAME];
        if (!tok_read_value(&t, val, SQL_MAX_NAME)) return 0;
        if (isdigit((unsigned char)val[0]) || val[0] == '-') {
            stmt->where_clause.int_val = atoi(val);
        } else {
            strcpy(stmt->where_clause.str_val, val);
        }
        stmt->has_where = 1;
    }

    if (tok_match_word(&t, "ORDER")) {
        if (!tok_match_word(&t, "BY")) return 0;
        if (!tok_read_identifier(&t, stmt->order_by, SQL_MAX_NAME)) return 0;
        stmt->has_order_by = 1;
    }

    return 1;
}

int sql_parse_create(const char *sql, SQLStmt *stmt) {
    memset(stmt, 0, sizeof(SQLStmt));
    stmt->type = SQL_CREATE_TABLE;

    Tokenizer t = { sql, 0, (int)strlen(sql) };

    if (!tok_match_word(&t, "CREATE")) return 0;
    if (!tok_match_word(&t, "TABLE")) return 0;

    if (!tok_read_identifier(&t, stmt->table, SQL_MAX_NAME)) return 0;

    if (t.pos >= t->len || t.sql[t.pos] != '(') return 0;
    t.pos++;

    stmt->num_col_defs = 0;
    do {
        tok_skip_whitespace(&t);
        if (t.pos >= t->len) return 0;
        if (t.sql[t.pos] == ')') { t.pos++; break; }

        ColumnDef *cd = &stmt->col_defs[stmt->num_col_defs];
        if (!tok_read_identifier(&t, cd->name, SQL_MAX_NAME)) return 0;

        if (tok_match_word(&t, "INT") || tok_match_word(&t, "INTEGER")) {
            cd->type = SQL_TYPE_INT;
        } else if (tok_match_word(&t, "VARCHAR")) {
            cd->type = SQL_TYPE_VARCHAR;
            if (t.pos < t->len && t.sql[t.pos] == '(') {
                t.pos++;
                char num[16];
                if (!tok_read_value(&t, num, 16)) return 0;
                cd->length = atoi(num);
                if (t.pos < t->len && t.sql[t.pos] == ')') t.pos++;
            }
        } else if (tok_match_word(&t, "TEXT")) {
            cd->type = SQL_TYPE_TEXT;
        } else {
            return 0;
        }

        stmt->num_col_defs++;
        if (stmt->num_col_defs >= SQL_MAX_COLUMNS) return 0;

        tok_skip_whitespace(&t);
        if (t.pos < t->len && t.sql[t.pos] == ',') t.pos++;
    } while (1);

    if (!tok_match_word(&t, ")")) {} /* closing paren may already be consumed */

    return 1;
}

int sql_parse_insert(const char *sql, SQLStmt *stmt) {
    memset(stmt, 0, sizeof(SQLStmt));
    stmt->type = SQL_INSERT;

    Tokenizer t = { sql, 0, (int)strlen(sql) };

    if (!tok_match_word(&t, "INSERT")) return 0;
    if (!tok_match_word(&t, "INTO")) return 0;

    if (!tok_read_identifier(&t, stmt->table, SQL_MAX_NAME)) return 0;

    if (!tok_match_word(&t, "VALUES")) return 0;

    if (t.pos >= t->len || t.sql[t.pos] != '(') return 0;
    t.pos++;

    stmt->num_values = 0;
    do {
        tok_skip_whitespace(&t);
        if (t.pos >= t->len) return 0;
        if (t.sql[t.pos] == ')') { t.pos++; break; }

        if (!tok_read_value(&t, stmt->values[stmt->num_values], SQL_MAX_NAME))
            return 0;
        stmt->num_values++;
        if (stmt->num_values >= SQL_MAX_COLUMNS) return 0;

        tok_skip_whitespace(&t);
        if (t.pos < t->len && t.sql[t.pos] == ',') t.pos++;
    } while (1);

    return 1;
}

int sql_parse_drop(const char *sql, SQLStmt *stmt) {
    memset(stmt, 0, sizeof(SQLStmt));
    stmt->type = SQL_DROP_TABLE;

    Tokenizer t = { sql, 0, (int)strlen(sql) };

    if (!tok_match_word(&t, "DROP")) return 0;
    if (!tok_match_word(&t, "TABLE")) return 0;

    if (!tok_read_identifier(&t, stmt->table, SQL_MAX_NAME)) return 0;

    return 1;
}

int sql_parse(const char *sql, SQLStmt *stmt) {
    Tokenizer t = { sql, 0, (int)strlen(sql) };
    tok_skip_whitespace(&t);

    if (t.pos >= t.len) return 0;

    const char *p = sql + t.pos;
    if (strncasecmp(p, "SELECT", 6) == 0) return sql_parse_select(sql, stmt);
    if (strncasecmp(p, "INSERT", 6) == 0) return sql_parse_insert(sql, stmt);
    if (strncasecmp(p, "CREATE", 6) == 0) return sql_parse_create(sql, stmt);
    if (strncasecmp(p, "DROP", 4) == 0)   return sql_parse_drop(sql, stmt);

    stmt->type = SQL_ERROR;
    return 0;
}

static const char *type_name(SQLDataType t) {
    switch (t) {
        case SQL_TYPE_INT:     return "INT";
        case SQL_TYPE_VARCHAR: return "VARCHAR";
        case SQL_TYPE_TEXT:    return "TEXT";
        default:               return "UNKNOWN";
    }
}

static const char *cmp_op_name(SQLCmpOp op) {
    switch (op) {
        case SQL_CMP_EQ: return "=";
        case SQL_CMP_NE: return "!=";
        case SQL_CMP_LT: return "<";
        case SQL_CMP_LE: return "<=";
        case SQL_CMP_GT: return ">";
        case SQL_CMP_GE: return ">=";
        default:          return "?";
    }
}

void sql_print_stmt(const SQLStmt *stmt) {
    switch (stmt->type) {
    case SQL_SELECT:
        printf("SELECT ");
        for (int i = 0; i < stmt->num_columns; i++) {
            if (i > 0) printf(", ");
            printf("%s", stmt->columns[i]);
        }
        printf(" FROM %s", stmt->table);
        if (stmt->has_where) {
            printf(" WHERE %s %s ", stmt->where_clause.col.name,
                   cmp_op_name(stmt->where_clause.op));
            if (stmt->where_clause.op > SQL_CMP_NE)
                printf("%d", stmt->where_clause.int_val);
            else
                printf("'%s'", stmt->where_clause.str_val);
        }
        if (stmt->has_order_by)
            printf(" ORDER BY %s", stmt->order_by);
        printf("\n");
        break;
    case SQL_CREATE_TABLE:
        printf("CREATE TABLE %s (\n", stmt->table);
        for (int i = 0; i < stmt->num_col_defs; i++) {
            printf("  %s %s", stmt->col_defs[i].name,
                   type_name(stmt->col_defs[i].type));
            if (stmt->col_defs[i].type == SQL_TYPE_VARCHAR)
                printf("(%d)", stmt->col_defs[i].length);
            if (i < stmt->num_col_defs - 1) printf(",");
            printf("\n");
        }
        printf(");\n");
        break;
    case SQL_INSERT:
        printf("INSERT INTO %s VALUES (", stmt->table);
        for (int i = 0; i < stmt->num_values; i++) {
            if (i > 0) printf(", ");
            printf("%s", stmt->values[i]);
        }
        printf(");\n");
        break;
    case SQL_DROP_TABLE:
        printf("DROP TABLE %s;\n", stmt->table);
        break;
    default:
        printf("[ERROR: unknown statement]\n");
        break;
    }
}
