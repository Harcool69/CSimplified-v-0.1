#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "utils.h"

static char **read_lines(const char *path, int *out_n) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t cap = 64; int n = 0;
    char **lines = malloc(cap * sizeof(char*));
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        char *s = strdup_safe(buf);
        char *trimmed = trim(s);
        if (trimmed != s) {
            memmove(s, trimmed, strlen(trimmed) + 1);
        }
        if (n >= (int)cap) { cap *= 2; lines = realloc(lines, cap * sizeof(char*)); }
        lines[n++] = s;
    }
    fclose(f);
    *out_n = n;
    return lines;
}

static Statement *make_stmt(StmtType t) {
    Statement *s = calloc(1, sizeof(Statement));
    s->type = t;
    return s;
}

static Statement **parse_block(char **lines, int n, int *idx) {
    size_t cap = 8; int count = 0;
    Statement **list = malloc(cap * sizeof(Statement*));
    while (*idx < n) {
        char *line = lines[*idx];
        (*idx)++;
        if (!line || line[0]=='#' || line[0]=='\0') continue;
        if (starts_with(line, "end")) break;
        if (starts_with(line, "print ")) {
            Statement *st = make_stmt(STMT_PRINT);
            st->expr = strdup_safe(line + 6);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "input ")) {
            Statement *st = make_stmt(STMT_INPUT);
            char *p = line + 6;
            char *quote = strchr(p, '"');
            if (quote) {
                *quote = '\0';
                st->name = strdup_safe(trim(p));
                st->extra = strdup_safe(quote + 1);
                char *end = strrchr(st->extra, '"');
                if (end) *end = '\0';
            } else {
                st->name = strdup_safe(trim(p));
            }
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "let ")) {
            char *p = line + 4;
            char *be = strstr(p, " be ");
            if (be) {
                *be = '\0';
                Statement *st = make_stmt(STMT_LET);
                st->name = strdup_safe(trim(p));
                st->expr = strdup_safe(trim(be + 4));
                if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                list[count++] = st;
                continue;
            }
        }
        if (starts_with(line, "set ")) {
            char *p = line + 4;
            char *to = strstr(p, " to ");
            if (to) {
                *to = '\0';
                Statement *st = make_stmt(STMT_SET);
                st->name = strdup_safe(trim(p));
                st->expr = strdup_safe(trim(to + 4));
                if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                list[count++] = st;
                continue;
            }
        }
        if (starts_with(line, "append ") || starts_with(line, "add ")) {
            int offset = starts_with(line, "append ") ? 7 : 4;
            char *p = line + offset;
            char *to = strstr(p, " to ");
            if (to) {
                *to = '\0';
                char *value = trim(p);
                char *name = trim(to + 4);
                Statement *st = make_stmt(STMT_CALL);
                size_t need = strlen(name) + strlen(value) + 12;
                st->expr = malloc(need);
                snprintf(st->expr, need, "append(%s, %s)", name, value);
                if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                list[count++] = st;
                continue;
            }
        }
        if (starts_with(line, "remove ")) {
            char *p = line + 7;
            char *from = strstr(p, " from ");
            if (from) {
                *from = '\0';
                char *value = trim(p);
                char *name = trim(from + 6);
                Statement *st = make_stmt(STMT_CALL);
                size_t need = strlen(name) + strlen(value) + 12;
                st->expr = malloc(need);
                snprintf(st->expr, need, "remove(%s, %s)", name, value);
                if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                list[count++] = st;
                continue;
            }
        }
        size_t line_len = strlen(line);
        if (line_len > 2 && line[line_len - 1] == '+' && line[line_len - 2] == '+') {
            line[line_len - 2] = '\0';
            Statement *st = make_stmt(STMT_SET);
            st->name = strdup_safe(trim(line));
            size_t need = strlen(st->name) + 5;
            st->expr = malloc(need);
            snprintf(st->expr, need, "%s + 1", st->name);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (line_len > 2 && line[line_len - 1] == '-' && line[line_len - 2] == '-') {
            line[line_len - 2] = '\0';
            Statement *st = make_stmt(STMT_SET);
            st->name = strdup_safe(trim(line));
            size_t need = strlen(st->name) + 5;
            st->expr = malloc(need);
            snprintf(st->expr, need, "%s - 1", st->name);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        char *aug = strstr(line, "+=");
        if (!aug) aug = strstr(line, "-=");
        if (!aug) aug = strstr(line, "*=");
        if (!aug) aug = strstr(line, "/=");
        if (aug) {
            char op = aug[0];
            *aug = '\0';
            Statement *st = make_stmt(STMT_SET);
            st->name = strdup_safe(trim(line));
            char *rhs = trim(aug + 2);
            size_t need = strlen(st->name) + strlen(rhs) + 6;
            st->expr = malloc(need);
            snprintf(st->expr, need, "%s %c %s", st->name, op, rhs);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        char *eq = strchr(line, '=');
        if (eq && !(eq > line && (eq[-1] == '=' || eq[-1] == '!' || eq[-1] == '<' || eq[-1] == '>')) && eq[1] != '=') {
            *eq = '\0';
            Statement *st = make_stmt(STMT_SET);
            st->name = strdup_safe(trim(line));
            st->expr = strdup_safe(trim(eq + 1));
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "def ")) {
            char *p = line + 4;
            char *open = strchr(p, '(');
            char *close = strrchr(p, ')');
            if (open && close && open < close) {
                *open = '\0';
                *close = '\0';
                Statement *st = make_stmt(STMT_DEF);
                st->name = strdup_safe(trim(p));
                st->expr = strdup_safe(trim(open + 1));
                st->body = parse_block(lines, n, idx);
                if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                list[count++] = st;
                continue;
            }
        }
        if (starts_with(line, "class ")) {
            char *p = line + 6;
            Statement *st = make_stmt(STMT_CLASS);
            st->name = strdup_safe(trim(p));
            st->body = parse_block(lines, n, idx);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "for ")) {
            char *p = line + 4;
            char *from = strstr(p, " from ");
            if (from) {
                *from = '\0';
                char *loop_var = trim(p);
                char *start_expr = trim(from + 6);
                char *to = strstr(start_expr, " to ");
                if (to) {
                    *to = '\0';
                    char *end_expr = trim(to + 4);
                    char *do_ = strstr(end_expr, " do");
                    if (do_) {
                        *do_ = '\0';
                        Statement *st = make_stmt(STMT_FOR);
                        st->name = strdup_safe(loop_var);
                        st->expr = strdup_safe(trim(start_expr));
                        st->extra = strdup_safe(trim(end_expr));
                        st->body = parse_block(lines, n, idx);
                        if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
                        list[count++] = st;
                        continue;
                    }
                }
            }
        }
        if (starts_with(line, "return ")) {
            Statement *st = make_stmt(STMT_RETURN);
            st->expr = strdup_safe(trim(line + 7));
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "call ")) {
            Statement *st = make_stmt(STMT_CALL);
            st->expr = strdup_safe(trim(line + 5));
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "if ") && strstr(line, " then")) {
            char *cond = strdup_safe(line + 3);
            char *then = strstr(cond, " then");
            if (then) *then = '\0';
            Statement *st = make_stmt(STMT_IF);
            st->expr = strdup_safe(trim(cond));
            free(cond);
            st->body = parse_block(lines, n, idx);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "while ") && strstr(line, " do")) {
            char *cond = strdup_safe(line + 6);
            char *do_ = strstr(cond, " do"); if (do_) *do_='\0';
            Statement *st = make_stmt(STMT_WHILE);
            st->expr = strdup_safe(trim(cond));
            free(cond);
            st->body = parse_block(lines, n, idx);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        Statement *st = make_stmt(STMT_PRINT);
        st->expr = strdup_safe(line);
        if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
        list[count++] = st;
    }
    list = realloc(list, (count+1)*sizeof(Statement*));
    list[count] = NULL;
    return list;
}

Statement **parse_file(const char *path, int *out_count) {
    int n = 0; char **lines = read_lines(path, &n);
    if (!lines) return NULL;
    int idx = 0;
    Statement **top = parse_block(lines, n, &idx);
    int c = 0; while (top && top[c]) c++;
    *out_count = c;
    for (int i=0;i<n;i++) free(lines[i]);
    free(lines);
    return top;
}

static void free_stmt(Statement *s) {
    if (!s) return;
    if (s->name) free(s->name);
    if (s->expr) free(s->expr);
    if (s->extra) free(s->extra);
    if (s->body) {
        int i=0; while (s->body[i]) { free_stmt(s->body[i]); i++; }
        free(s->body);
    }
    free(s);
}

void free_statements(Statement **stmts, int n) {
    if (!stmts) return;
    int i=0; while (stmts[i]) { free_stmt(stmts[i]); i++; }
    free(stmts);
}
