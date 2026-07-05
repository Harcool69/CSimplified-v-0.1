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
            // trim returned a pointer past leading whitespace; shift chars back
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
            st->name = strdup_safe(trim(line + 6));
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "let ")) {
            // let <name> be <expr>
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
            // set <name> to <expr>
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
        if (starts_with(line, "def ")) {
            // def <name> with <params>   OR   def <name>
            char *p = line + 4;
            Statement *st = make_stmt(STMT_DEF);
            char *with = strstr(p, " with ");
            if (with) {
                *with = '\0';
                st->name = strdup_safe(trim(p));
                st->expr = strdup_safe(trim(with + 6));
            } else {
                st->name = strdup_safe(trim(p));
                st->expr = NULL;
            }
            st->body = parse_block(lines, n, idx);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "class ")) {
            // class <Name>
            Statement *st = make_stmt(STMT_CLASS);
            st->name = strdup_safe(trim(line + 6));
            st->body = parse_block(lines, n, idx);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (strcmp(line, "return") == 0 || starts_with(line, "return ")) {
            Statement *st = make_stmt(STMT_RETURN);
            if (line[6]) st->expr = strdup_safe(trim(line + 7));
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "call ")) {
            Statement *st = make_stmt(STMT_EXPR);
            st->expr = strdup_safe(line);
            if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
            list[count++] = st;
            continue;
        }
        if (starts_with(line, "if ") && strstr(line, " then")) {
            // if <cond> then
            char *cond = strdup_safe(line + 3);
            char *then = strstr(cond, " then");
            if (then) *then = '\0';
            Statement *st = make_stmt(STMT_IF);
            st->expr = strdup_safe(trim(cond));
            free(cond);
            // parse body
            st->body = parse_block(lines, n, idx);
            // count body
            int bc = 0; while (st->body && st->body[bc]) bc++;
            st->body_count = 0; // will compute differently in free
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
        // unknown/ignored line -> treat as print raw
        Statement *st = make_stmt(STMT_PRINT);
        st->expr = strdup_safe(line);
        if (count >= (int)cap) { cap*=2; list = realloc(list, cap*sizeof(Statement*)); }
        list[count++] = st;
    }
    // terminate with NULL for convenience
    list = realloc(list, (count+1)*sizeof(Statement*));
    list[count] = NULL;
    return list;
}

Statement **parse_file(const char *path, int *out_count) {
    int n = 0; char **lines = read_lines(path, &n);
    if (!lines) return NULL;
    int idx = 0;
    Statement **top = parse_block(lines, n, &idx);
    // compute count
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
