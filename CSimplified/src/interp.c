#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "interp.h"
#include "utils.h"

typedef struct Var { char *name; int is_string; int val; char *str; } Var;
static Var *vars = NULL; static int vars_n = 0;

static void var_set(const char *name, int is_string, const char *str_val, int int_val) {
    for (int i=0;i<vars_n;i++) if (strcmp(vars[i].name,name)==0) {
        if (vars[i].is_string) free(vars[i].str);
        vars[i].is_string = is_string;
        vars[i].val = int_val;
        if (is_string) vars[i].str = strdup_safe(str_val ? str_val : "");
        else vars[i].str = NULL;
        return;
    }
    vars = realloc(vars, (vars_n+1)*sizeof(Var));
    vars[vars_n].name = strdup_safe(name);
    vars[vars_n].is_string = is_string;
    vars[vars_n].val = int_val;
    vars[vars_n].str = is_string ? strdup_safe(str_val ? str_val : "") : NULL;
    vars_n++;
}

static int var_get(const char *name, int *out_val, char **out_str, int *out_is_string) {
    for (int i=0;i<vars_n;i++) if (strcmp(vars[i].name,name)==0) {
        *out_is_string = vars[i].is_string;
        if (vars[i].is_string) {
            *out_val = 0;
            *out_str = vars[i].str ? strdup_safe(vars[i].str) : strdup_safe("");
        } else {
            *out_val = vars[i].val;
            *out_str = NULL;
        }
        return 1;
    }
    return 0;
}

static int read_input_value(char **out_str, int *out_val, int *out_is_string) {
    char buf[8192];
    if (!fgets(buf, sizeof(buf), stdin)) {
        *out_str = strdup_safe("");
        *out_val = 0;
        *out_is_string = 1;
        return 0;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    if (len == 0) {
        *out_str = strdup_safe("");
        *out_val = 0;
        *out_is_string = 1;
        return 1;
    }
    if (is_number(buf)) {
        *out_str = NULL;
        *out_val = atoi(buf);
        *out_is_string = 0;
    } else {
        *out_str = strdup_safe(buf);
        *out_val = 0;
        *out_is_string = 1;
    }
    return 1;
}

static int resolve_value(const char *token, int *out_val) {
    char *tmp = strdup_safe(token);
    if (!tmp) return 0;
    trim(tmp);
    if (is_number(tmp)) {
        *out_val = atoi(tmp);
        free(tmp);
        return 1;
    }
    int is_str = 0; char *str = NULL; int val = 0;
    if (var_get(tmp, &val, &str, &is_str)) {
        if (is_str) {
            if (str) free(str);
            free(tmp);
            return 0;
        }
        *out_val = val;
        if (str) free(str);
        free(tmp);
        return 1;
    }
    *out_val = 0;
    free(tmp);
    return 1;
}

static int eval_arithmetic(const char *expr, int *out_val) {
    char *copy = strdup_safe(expr);
    if (!copy) return 0;
    trim(copy);
    if (copy[0] == '\0') { free(copy); return 0; }

    char *tokens[64]; int nt = 0;
    char *p = copy;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        if (strchr("+-*/%", *p)) {
            char opbuf[2] = { *p, '\0' };
            tokens[nt++] = strdup_safe(opbuf);
            p++;
            continue;
        }
        char *start = p;
        while (*p && !isspace((unsigned char)*p) && !strchr("+-*/%", *p)) p++;
        char save = *p;
        if (save) *p = '\0';
        tokens[nt++] = strdup_safe(start);
        if (save) *p = save;
    }

    int values[64]; char ops[64]; int nv = 0; int no = 0;
    for (int i = 0; i < nt; i++) {
        char *tok = tokens[i];
        if (tok[0] == '+' || tok[0] == '-' || tok[0] == '*' || tok[0] == '/' || tok[0] == '%') {
            char op = tok[0];
            int prec = (op == '*' || op == '/' || op == '%') ? 2 : 1;
            while (no > 0) {
                char prev = ops[no - 1];
                int prev_prec = (prev == '*' || prev == '/' || prev == '%') ? 2 : 1;
                if (prev_prec >= prec && nv >= 2) {
                    int b = values[--nv];
                    int a = values[--nv];
                    int result = 0;
                    switch (prev) {
                        case '+': result = a + b; break;
                        case '-': result = a - b; break;
                        case '*': result = a * b; break;
                        case '/': result = (b == 0 ? 0 : a / b); break;
                        case '%': result = (b == 0 ? 0 : a % b); break;
                    }
                    values[nv++] = result;
                    no--;
                } else {
                    break;
                }
            }
            ops[no++] = op;
        } else {
            int v = 0;
            if (!resolve_value(tok, &v)) {
                for (int j = 0; j < nt; j++) free(tokens[j]);
                free(copy);
                return 0;
            }
            values[nv++] = v;
        }
    }

    while (no > 0 && nv >= 2) {
        char op = ops[--no];
        int b = values[--nv];
        int a = values[--nv];
        int result = 0;
        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/': result = (b == 0 ? 0 : a / b); break;
            case '%': result = (b == 0 ? 0 : a % b); break;
        }
        values[nv++] = result;
    }

    for (int j = 0; j < nt; j++) free(tokens[j]);
    free(copy);
    if (nv >= 1) {
        *out_val = values[nv - 1];
        return 1;
    }
    return 0;
}

// Evaluate simple expressions: integers, variable names, or binary like "a plus b"
// Comparisons return 1/0. Strings are quoted.
static int eval_expr(const char *expr, int *is_string, char **out_str, int *out_val) {
    *is_string = 0; *out_str = NULL; *out_val = 0;
    if (!expr) return 0;
    char *trimmed = strdup_safe(expr);
    trim(trimmed);
    if (strcmp(trimmed, "input") == 0) {
        int ok = read_input_value(out_str, out_val, is_string);
        free(trimmed);
        return ok;
    }
    free(trimmed);
    const char *s = expr;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '"') {
        // string literal
        *is_string = 1;
        s++;
        char *end = strdup_safe(s);
        char *q = strchr(end, '"'); if (q) *q = '\0';
        *out_str = end;
        return 1;
    }
    if (strpbrk(expr, "+-*/%") != NULL) {
        int ok = eval_arithmetic(expr, out_val);
        if (ok) {
            return 1;
        }
    }

    // normalize tokens: map English words to symbols and lower-case
    char *tmp0 = strdup_safe(expr);
    char *tok = NULL;
    char *parts_arr[64]; int p = 0;
    tok = strtok(tmp0, " ");
    while (tok && p < 64) {
        char lower[256]; int ii=0;
        for (char *c = tok; *c && ii < 255; c++, ii++) lower[ii] = tolower((unsigned char)*c);
        lower[ii] = '\0';
        if (strcmp(lower, "plus") == 0) { parts_arr[p++] = strdup_safe("+"); }
        else if (strcmp(lower, "minus") == 0) { parts_arr[p++] = strdup_safe("-"); }
        else if (strcmp(lower, "multiply") == 0 || strcmp(lower, "times") == 0 || strcmp(lower, "multiplied") == 0) { parts_arr[p++] = strdup_safe("*"); }
        else if (strcmp(lower, "divided") == 0 || strcmp(lower, "divide") == 0) { parts_arr[p++] = strdup_safe("/"); }
        else if (strcmp(lower, "by") == 0) { /* skip */ }
        else if (strcmp(lower, "mod") == 0 || strcmp(lower, "modulo") == 0) { parts_arr[p++] = strdup_safe("%"); }
        else { parts_arr[p++] = strdup_safe(tok); }
        tok = strtok(NULL, " ");
    }
    // build normalized string
    size_t cap = 256; char *norm = malloc(cap);
    norm[0] = '\0';
    for (int i=0;i<p;i++) {
        size_t need = strlen(norm) + strlen(parts_arr[i]) + 2;
        if (need > cap) { cap = need * 2; norm = realloc(norm, cap); }
        if (strlen(norm) > 0) strcat(norm, " ");
        strcat(norm, parts_arr[i]);
    }
    // tokenize normalized string
    char *tmp = strdup_safe(norm);
    char *parts[16]; int pc=0;
    char *t = strtok(tmp, " ");
    while (t && pc<16) { parts[pc++]=t; t=strtok(NULL," "); }
    if (pc==0) {
        for (int i=0;i<p;i++) free(parts_arr[i]);
        free(norm); free(tmp0); free(tmp);
        return 0;
    }
    // single number
    if (pc==1 && is_number(parts[0])) { *out_val = atoi(parts[0]); for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1; }
    // single variable
    if (pc==1) {
        int v=0; int is_var_string=0; char *var_str=NULL;
        if (var_get(parts[0], &v, &var_str, &is_var_string)) {
            if (is_var_string) {
                *is_string = 1; *out_str = var_str; *out_val = 0;
            } else {
                *out_val = v;
                if (var_str) free(var_str);
            }
            for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
        } else {
            *out_val = 0; for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
        }
    }
    // binary ops: a op b (supports + - * / %)
    if (pc==3) {
        int a=0,b=0; int is_a_string=0, is_b_string=0; char *a_str=NULL, *b_str=NULL;
        if (is_number(parts[0])) a=atoi(parts[0]);
        else if (var_get(parts[0], &a, &a_str, &is_a_string)) {
            if (is_a_string) { if (a_str) free(a_str); a = 0; }
        }
        if (is_number(parts[2])) b=atoi(parts[2]);
        else if (var_get(parts[2], &b, &b_str, &is_b_string)) {
            if (is_b_string) { if (b_str) free(b_str); b = 0; }
        }
        if (strcmp(parts[1], "+")==0) *out_val = a + b;
        else if (strcmp(parts[1], "-")==0) *out_val = a - b;
        else if (strcmp(parts[1], "*")==0) *out_val = a * b;
        else if (strcmp(parts[1], "/")==0) *out_val = (b==0?0:(a / b));
        else if (strcmp(parts[1], "%")==0) *out_val = (b==0?0:(a % b));
        if (a_str) free(a_str); if (b_str) free(b_str);
        for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
    }
    // comparisons: keep English style (e.g., x is greater than 3)
    if (pc>=4 && strcmp(parts[1], "is")==0) {
        int a=0,b=0; int is_a_string=0, is_b_string=0; char *a_str=NULL, *b_str=NULL;
        if (is_number(parts[0])) a=atoi(parts[0]);
        else if (var_get(parts[0], &a, &a_str, &is_a_string)) {
            if (is_a_string) { if (a_str) free(a_str); a = 0; }
        }
        if (strcmp(parts[2], "greater") == 0 && strcmp(parts[3], "than") == 0 && pc>=5) {
            if (is_number(parts[4])) b=atoi(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a > b);
            if (a_str) free(a_str); if (b_str) free(b_str);
            for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
        }
        if (strcmp(parts[2], "less") == 0 && strcmp(parts[3], "than") == 0 && pc>=5) {
            if (is_number(parts[4])) b=atoi(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a < b);
            if (a_str) free(a_str); if (b_str) free(b_str);
            for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
        }
        if (strcmp(parts[2], "equal") == 0 && strcmp(parts[3], "to") == 0 && pc>=5) {
            if (is_number(parts[4])) b=atoi(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a == b);
            if (a_str) free(a_str); if (b_str) free(b_str);
            for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
        }
    }
    // fallback: number or variable
    if (is_number(parts[0])) { *out_val = atoi(parts[0]); for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1; }
    int v=0; int is_var_string=0; char *var_str=NULL; if (var_get(parts[0], &v, &var_str, &is_var_string)) {
        if (is_var_string) { *is_string = 1; *out_str = var_str; *out_val = 0; }
        else { *out_val = v; if (var_str) free(var_str); }
        for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp); return 1;
    }
    for (int i=0;i<p;i++) free(parts_arr[i]); free(norm); free(tmp0); free(tmp);
    return 0;
}
static void exec_stmt(Statement *s);

static void exec_print_expr(const char *expr) {
    if (!expr) return;
    char *copy = strdup_safe(expr);
    if (!copy) return;
    char *p = copy;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;

        if (*p == '"') {
            char *start = p + 1;
            char *end = strchr(start, '"');
            if (end) {
                *end = '\0';
                printf("%s", start);
                p = end + 1;
            } else {
                printf("%s", start);
                break;
            }
            continue;
        }

        char *end = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        char *next = end;
        if (*end) {
            *end = '\0';
            next = end + 1;
        }

        int is_str = 0; char *str = NULL; int val = 0;
        if (eval_expr(p, &is_str, &str, &val)) {
            if (is_str && str) {
                printf("%s", str);
                free(str);
            } else {
                printf("%d", val);
            }
        } else {
            printf("%s", p);
        }

        p = next;
    }
    putchar('\n');
    free(copy);
}

static void exec_block(Statement **body) {
    if (!body) return;
    int i=0; while (body[i]) { exec_stmt(body[i]); i++; }
}

static void exec_stmt(Statement *s) {
    if (!s) return;
    if (s->type == STMT_PRINT) {
        exec_print_expr(s->expr);
    } else if (s->type == STMT_LET) {
        int is_str=0; char *str=NULL; int val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        var_set(s->name, is_str, str, val);
        if (str) free(str);
    } else if (s->type == STMT_SET) {
        int is_str=0; char *str=NULL; int val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        var_set(s->name, is_str, str, val);
        if (str) free(str);
    } else if (s->type == STMT_INPUT) {
        int is_str=0; char *str=NULL; int val=0;
        read_input_value(&str, &val, &is_str);
        var_set(s->name, is_str, str, val);
        if (str) free(str);
    } else if (s->type == STMT_IF) {
        int is_str=0; char *str=NULL; int val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        if (val) exec_block(s->body);
        if (str) free(str);
    } else if (s->type == STMT_WHILE) {
        int safety = 1000000; // avoid infinite loops
        while (safety-- > 0) {
            int is_str=0; char *str=NULL; int val=0;
            eval_expr(s->expr, &is_str, &str, &val);
            if (str) free(str);
            if (!val) break;
            exec_block(s->body);
        }
    }
}

void exec_statements(Statement **stmts) {
    if (!stmts) return;
    int i=0; while (stmts[i]) { exec_stmt(stmts[i]); i++; }
}
