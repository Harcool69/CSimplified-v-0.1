#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "interp.h"
#include "utils.h"

typedef struct Var { char *name; int is_string; double val; char *str; } Var;
typedef struct FunctionDef { char *name; char **params; int param_count; Statement **body; } FunctionDef;
typedef struct ClassDef { char *name; Statement **body; } ClassDef;
typedef struct SavedVar { char *name; int existed; int is_string; double val; char *str; } SavedVar;

static Var *vars = NULL; static int vars_n = 0;
static FunctionDef *functions = NULL; static int functions_n = 0;
static ClassDef *classes = NULL; static int classes_n = 0;
static int current_return_active = 0;
static int current_return_is_string = 0;
static double current_return_val = 0;
static char *current_return_str = NULL;

static int eval_expr(const char *expr, int *is_string, char **out_str, double *out_val);
static int exec_block(Statement **body);
static int exec_stmt(Statement *s);

static void format_number(double value, char *buf, size_t size) {
    if (fabs(value - round(value)) < 0.000000001) snprintf(buf, size, "%.0f", value);
    else snprintf(buf, size, "%.10g", value);
}

static void clear_return_state(void) {
    if (current_return_str) { free(current_return_str); current_return_str = NULL; }
    current_return_active = 0;
    current_return_is_string = 0;
    current_return_val = 0;
}

static char *make_prefixed_name(const char *prefix, const char *name) {
    if (!prefix || !name) return strdup_safe(name ? name : "");
    size_t len = strlen(prefix) + 1 + strlen(name) + 1;
    char *out = malloc(len);
    snprintf(out, len, "%s.%s", prefix, name);
    return out;
}

static void var_set(const char *name, int is_string, const char *str_val, double number_val) {
    for (int i=0;i<vars_n;i++) if (strcmp(vars[i].name,name)==0) {
        if (vars[i].is_string) free(vars[i].str);
        vars[i].is_string = is_string;
        vars[i].val = number_val;
        if (is_string) vars[i].str = strdup_safe(str_val ? str_val : "");
        else vars[i].str = NULL;
        return;
    }
    vars = realloc(vars, (vars_n+1)*sizeof(Var));
    vars[vars_n].name = strdup_safe(name);
    vars[vars_n].is_string = is_string;
    vars[vars_n].val = number_val;
    vars[vars_n].str = is_string ? strdup_safe(str_val ? str_val : "") : NULL;
    vars_n++;
}

static void var_set_prefixed(const char *prefix, const char *name, int is_string, const char *str_val, double number_val) {
    char *full = make_prefixed_name(prefix, name);
    var_set(full, is_string, str_val, number_val);
    free(full);
}

static int var_get(const char *name, double *out_val, char **out_str, int *out_is_string) {
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

static int read_input_value(const char *prompt, char **out_str, double *out_val, int *out_is_string) {
    char buf[8192];
    if (prompt && prompt[0] != '\0') {
        printf("%s", prompt);
        fflush(stdout);
    }
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
    if (is_decimal_number(buf)) {
        *out_str = NULL;
        *out_val = atof(buf);
        *out_is_string = 0;
    } else {
        *out_str = strdup_safe(buf);
        *out_val = 0;
        *out_is_string = 1;
    }
    return 1;
}

static void function_add(Statement *s) {
    if (!s || !s->name) return;
    functions = realloc(functions, (functions_n + 1) * sizeof(FunctionDef));
    functions[functions_n].name = strdup_safe(s->name);
    functions[functions_n].body = s->body;
    functions[functions_n].param_count = 0;
    functions[functions_n].params = NULL;
    if (s->expr && s->expr[0] != '\0') {
        char *copy = strdup_safe(s->expr);
        char *token = strtok(copy, ",");
        while (token) {
            char *trimmed = trim(token);
            if (trimmed && trimmed[0] != '\0') {
                functions[functions_n].params = realloc(functions[functions_n].params, (functions[functions_n].param_count + 1) * sizeof(char*));
                functions[functions_n].params[functions[functions_n].param_count++] = strdup_safe(trimmed);
            }
            token = strtok(NULL, ",");
        }
        free(copy);
    }
    functions_n++;
}

static int function_get(const char *name, FunctionDef **out_fn) {
    for (int i=0;i<functions_n;i++) if (strcmp(functions[i].name, name) == 0) {
        if (out_fn) *out_fn = &functions[i];
        return 1;
    }
    return 0;
}

static void class_add(Statement *s) {
    if (!s || !s->name) return;
    classes = realloc(classes, (classes_n + 1) * sizeof(ClassDef));
    classes[classes_n].name = strdup_safe(s->name);
    classes[classes_n].body = s->body;
    classes_n++;
}

static int class_get(const char *name, ClassDef **out_cls) {
    for (int i=0;i<classes_n;i++) if (strcmp(classes[i].name, name) == 0) {
        if (out_cls) *out_cls = &classes[i];
        return 1;
    }
    return 0;
}

static int is_identifier_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

static int is_collection_literal(const char *s) {
    size_t len;
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    if (len < 2) return 0;
    if (s[0] == '[' && s[len - 1] == ']') return 1;
    if (s[0] == '{' && s[len - 1] == '}') return 1;
    if (s[0] == '(' && s[len - 1] == ')') {
        for (size_t i = 1; i + 1 < len; i++) {
            if (s[i] == ',') return 1;
        }
    }
    return 0;
}

static char *copy_trimmed_span(const char *s, size_t len) {
    while (len > 0 && isspace((unsigned char)*s)) {
        s++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *substring(const char *src, int start, int end) {
    if (start < 0 || end < start) return strdup_safe("");
    int len = end - start;
    char *out = malloc(len + 1);
    memcpy(out, src + start, len);
    out[len] = '\0';
    return out;
}

static char *render_collection_literal(const char *s) {
    size_t cap = strlen(s) + 64;
    char *out = malloc(cap);
    size_t n = 0;
    int in_string = 0;
    for (size_t i = 0; s[i]; ) {
        if (s[i] == '"') {
            in_string = !in_string;
            if (n + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[n++] = s[i++];
            continue;
        }
        if (!in_string && (isalpha((unsigned char)s[i]) || s[i] == '_')) {
            size_t start = i;
            while (s[i] && is_identifier_char(s[i])) i++;
            char *name = copy_trimmed_span(s + start, i - start);
            int is_str = 0; char *str = NULL; double val = 0;
            if (var_get(name, &val, &str, &is_str)) {
                char buf[64];
                const char *piece = NULL;
                char *owned = NULL;
                if (is_str) {
                    size_t need = strlen(str ? str : "") + 3;
                    owned = malloc(need);
                    snprintf(owned, need, "\"%s\"", str ? str : "");
                    piece = owned;
                } else {
                    format_number(val, buf, sizeof(buf));
                    piece = buf;
                }
                size_t need = n + strlen(piece) + 1;
                if (need > cap) { while (need > cap) cap *= 2; out = realloc(out, cap); }
                strcpy(out + n, piece);
                n += strlen(piece);
                if (owned) free(owned);
                if (str) free(str);
            } else {
                size_t need = n + strlen(name) + 1;
                if (need > cap) { while (need > cap) cap *= 2; out = realloc(out, cap); }
                strcpy(out + n, name);
                n += strlen(name);
            }
            free(name);
            continue;
        }
        if (n + 2 > cap) { cap *= 2; out = realloc(out, cap); }
        out[n++] = s[i++];
    }
    out[n] = '\0';
    return out;
}

static int parse_expression(const char *expr, int *pos, int *is_string, char **out_str, double *out_val);
static int parse_term(const char *expr, int *pos, int *is_string, char **out_str, double *out_val);
static int parse_factor(const char *expr, int *pos, int *is_string, char **out_str, double *out_val);

static int parse_factor(const char *expr, int *pos, int *is_string, char **out_str, double *out_val) {
    *is_string = 0; *out_str = NULL; *out_val = 0;
    while (expr[*pos] && isspace((unsigned char)expr[*pos])) (*pos)++;
    if (!expr[*pos]) return 0;
    if (expr[*pos] == '(') {
        (*pos)++;
        int ok = parse_expression(expr, pos, is_string, out_str, out_val);
        while (expr[*pos] && isspace((unsigned char)expr[*pos])) (*pos)++;
        if (expr[*pos] == ')') { (*pos)++; return ok; }
        return 0;
    }
    if (expr[*pos] == '"') {
        (*pos)++;
        int start = *pos;
        while (expr[*pos] && expr[*pos] != '"') (*pos)++;
        if (!expr[*pos]) return 0;
        *out_str = substring(expr, start, *pos);
        *is_string = 1;
        (*pos)++;
        return 1;
    }
    if (expr[*pos] == '+' || expr[*pos] == '-') {
        char op = expr[*pos];
        (*pos)++;
        int ok = parse_factor(expr, pos, is_string, out_str, out_val);
        if (!ok) return 0;
        if (op == '-') *out_val = -(*out_val);
        return 1;
    }
    if (isdigit((unsigned char)expr[*pos])) {
        int start = *pos;
        while (expr[*pos] && (isdigit((unsigned char)expr[*pos]) || expr[*pos] == '.')) (*pos)++;
        char *num = substring(expr, start, *pos);
        *out_val = atof(num);
        free(num);
        return 1;
    }
    if (isalpha((unsigned char)expr[*pos]) || expr[*pos] == '_') {
        int start = *pos;
        while (expr[*pos] && is_identifier_char(expr[*pos])) (*pos)++;
        char *name = substring(expr, start, *pos);
        while (expr[*pos] && isspace((unsigned char)expr[*pos])) (*pos)++;
        if (expr[*pos] == '(') {
            (*pos)++;
            int depth = 1;
            int arg_start = *pos;
            int arg_count = 0;
            char **arg_exprs = NULL;
            while (expr[*pos] && depth > 0) {
                if (expr[*pos] == '(') depth++;
                else if (expr[*pos] == ')') depth--;
                else if (expr[*pos] == ',' && depth == 1) {
                    char *arg = substring(expr, arg_start, *pos);
                    arg_exprs = realloc(arg_exprs, (arg_count + 1) * sizeof(char*));
                    arg_exprs[arg_count++] = arg;
                    arg_start = *pos + 1;
                }
                if (depth > 0) (*pos)++;
            }
            if (depth == 0) {
                if (arg_count > 0 || *pos > arg_start) {
                    char *arg = substring(expr, arg_start, *pos);
                    arg_exprs = realloc(arg_exprs, (arg_count + 1) * sizeof(char*));
                    arg_exprs[arg_count++] = arg;
                }
                (*pos)++;
            } else {
                for (int i=0;i<arg_count;i++) free(arg_exprs[i]);
                free(arg_exprs);
                free(name);
                return 0;
            }
            if (strcmp(name, "int") == 0 || strcmp(name, "float") == 0 || strcmp(name, "number") == 0 ||
                strcmp(name, "str") == 0 || strcmp(name, "len") == 0 || strcmp(name, "type") == 0 ||
                strcmp(name, "abs") == 0 || strcmp(name, "round") == 0) {
                int inner_is_string = 0; char *inner_str = NULL; double inner_val = 0;
                if (arg_count > 0) eval_expr(arg_exprs[0], &inner_is_string, &inner_str, &inner_val);
                if (strcmp(name, "str") == 0) {
                    *is_string = 1;
                    if (inner_is_string) *out_str = strdup_safe(inner_str ? inner_str : "");
                    else {
                        char buf[64];
                        format_number(inner_val, buf, sizeof(buf));
                        *out_str = strdup_safe(buf);
                    }
                } else if (strcmp(name, "type") == 0) {
                    *is_string = 1;
                    if (inner_is_string && inner_str && inner_str[0] == '[') *out_str = strdup_safe("list");
                    else if (inner_is_string && inner_str && inner_str[0] == '{') *out_str = strchr(inner_str, ':') ? strdup_safe("dict") : strdup_safe("set");
                    else if (inner_is_string && inner_str && inner_str[0] == '(') *out_str = strdup_safe("tuple");
                    else *out_str = strdup_safe(inner_is_string ? "str" : "number");
                } else if (strcmp(name, "int") == 0) {
                    *out_val = inner_is_string ? atoi(inner_str ? inner_str : "0") : (double)((long long)inner_val);
                } else if (strcmp(name, "float") == 0 || strcmp(name, "number") == 0) {
                    *out_val = inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val;
                } else if (strcmp(name, "len") == 0) {
                    *out_val = inner_is_string && inner_str ? (double)strlen(inner_str) : 0;
                } else if (strcmp(name, "abs") == 0) {
                    *out_val = fabs(inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val);
                } else {
                    *out_val = round(inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val);
                }
                if (inner_str) free(inner_str);
                free(name);
                for (int i=0;i<arg_count;i++) free(arg_exprs[i]);
                free(arg_exprs);
                return 1;
            }
            FunctionDef *fn = NULL;
            if (strcmp(name, "input") == 0) {
                char *prompt = NULL;
                if (arg_count > 0) {
                    int prompt_is_string = 0;
                    double prompt_val = 0;
                    eval_expr(arg_exprs[0], &prompt_is_string, &prompt, &prompt_val);
                    if (!prompt_is_string) {
                        char buf[64];
                        format_number(prompt_val, buf, sizeof(buf));
                        prompt = strdup_safe(buf);
                    }
                }
                int ok = read_input_value(prompt, out_str, out_val, is_string);
                if (prompt) free(prompt);
                free(name);
                for (int i=0;i<arg_count;i++) free(arg_exprs[i]);
                free(arg_exprs);
                return ok;
            }
            if (function_get(name, &fn)) {
                int old_arg_count = fn->param_count;
                SavedVar *saved = calloc(old_arg_count, sizeof(SavedVar));
                for (int i=0;i<old_arg_count;i++) {
                    saved[i].name = strdup_safe(fn->params[i]);
                    saved[i].existed = var_get(fn->params[i], &saved[i].val, &saved[i].str, &saved[i].is_string);
                }
                for (int i=0;i<arg_count && i<old_arg_count;i++) {
                    int arg_is_string = 0; char *arg_str = NULL; double arg_val = 0;
                    eval_expr(arg_exprs[i], &arg_is_string, &arg_str, &arg_val);
                    var_set(fn->params[i], arg_is_string, arg_str, arg_val);
                    if (arg_str) free(arg_str);
                }
                clear_return_state();
                int returned = exec_block(fn->body);
                if (current_return_active) {
                    *is_string = current_return_is_string;
                    if (current_return_is_string) {
                        *out_str = current_return_str ? strdup_safe(current_return_str) : strdup_safe("");
                        current_return_str = NULL;
                    } else {
                        *out_val = current_return_val;
                    }
                    current_return_active = 0;
                } else if (returned) {
                    *out_val = 0;
                }
                for (int i=0;i<old_arg_count;i++) {
                    if (saved[i].existed) {
                        var_set(saved[i].name, saved[i].is_string, saved[i].str, saved[i].val);
                    } else {
                        for (int j=0;j<vars_n;j++) if (strcmp(vars[j].name, saved[i].name) == 0) {
                            if (vars[j].is_string) free(vars[j].str);
                            memmove(&vars[j], &vars[j+1], (vars_n - j - 1) * sizeof(Var));
                            vars_n--;
                            break;
                        }
                    }
                    free(saved[i].name);
                    if (saved[i].str) free(saved[i].str);
                }
                free(saved);
                for (int i=0;i<arg_count;i++) free(arg_exprs[i]);
                free(arg_exprs);
                free(name);
                clear_return_state();
                return 1;
            }
            free(name);
            for (int i=0;i<arg_count;i++) free(arg_exprs[i]);
            free(arg_exprs);
            return 0;
        }
        double v = 0; int is_var_string = 0; char *var_str = NULL;
        if (var_get(name, &v, &var_str, &is_var_string)) {
            if (is_var_string) {
                *is_string = 1;
                *out_str = var_str;
                *out_val = 0;
            } else {
                *out_val = v;
                if (var_str) free(var_str);
            }
            free(name);
            return 1;
        }
        free(name);
        *out_val = 0;
        return 1;
    }
    return 0;
}

static int parse_term(const char *expr, int *pos, int *is_string, char **out_str, double *out_val) {
    int ok = parse_factor(expr, pos, is_string, out_str, out_val);
    if (!ok) return 0;
    while (1) {
        while (expr[*pos] && isspace((unsigned char)expr[*pos])) (*pos)++;
        char op = expr[*pos];
        if (op != '*' && op != '/' && op != '%') break;
        (*pos)++;
        int rhs_is_string = 0; char *rhs_str = NULL; double rhs_val = 0;
        int rhs_ok = parse_factor(expr, pos, &rhs_is_string, &rhs_str, &rhs_val);
        if (!rhs_ok) return 0;
        if (rhs_is_string || *is_string) {
            if (*out_str) free(*out_str);
            *out_str = NULL;
            *is_string = 0;
            *out_val = 0;
            return 0;
        }
        if (op == '*') *out_val *= rhs_val;
        else if (op == '/') *out_val = (rhs_val == 0 ? 0 : *out_val / rhs_val);
        else *out_val = (rhs_val == 0 ? 0 : fmod(*out_val, rhs_val));
        if (rhs_str) free(rhs_str);
    }
    return 1;
}

static int parse_expression(const char *expr, int *pos, int *is_string, char **out_str, double *out_val) {
    int ok = parse_term(expr, pos, is_string, out_str, out_val);
    if (!ok) return 0;
    while (1) {
        while (expr[*pos] && isspace((unsigned char)expr[*pos])) (*pos)++;
        char op = expr[*pos];
        if (op != '+' && op != '-') break;
        (*pos)++;
        int rhs_is_string = 0; char *rhs_str = NULL; double rhs_val = 0;
        int rhs_ok = parse_term(expr, pos, &rhs_is_string, &rhs_str, &rhs_val);
        if (!rhs_ok) return 0;
        if (rhs_is_string || *is_string) {
            if (*out_str) free(*out_str);
            *out_str = NULL;
            *is_string = 0;
            *out_val = 0;
            return 0;
        }
        if (op == '+') *out_val += rhs_val;
        else *out_val -= rhs_val;
        if (rhs_str) free(rhs_str);
    }
    return 1;
}

static const char *find_comparison_op(const char *expr, const char **op) {
    int depth = 0;
    int in_string = 0;
    for (const char *p = expr; p && *p; p++) {
        if (*p == '"') in_string = !in_string;
        if (in_string) continue;
        if (*p == '(' || *p == '[' || *p == '{') depth++;
        else if ((*p == ')' || *p == ']' || *p == '}') && depth > 0) depth--;
        if (depth != 0) continue;
        if (p[0] == '=' && p[1] == '=') { *op = "=="; return p; }
        if (p[0] == '!' && p[1] == '=') { *op = "!="; return p; }
        if (p[0] == '>' && p[1] == '=') { *op = ">="; return p; }
        if (p[0] == '<' && p[1] == '=') { *op = "<="; return p; }
        if (*p == '>') { *op = ">"; return p; }
        if (*p == '<') { *op = "<"; return p; }
    }
    return NULL;
}

static int eval_symbol_comparison(const char *expr, double *out_val) {
    const char *op = NULL;
    const char *where = find_comparison_op(expr, &op);
    if (!where) return 0;
    char *left = copy_trimmed_span(expr, (size_t)(where - expr));
    char *right = copy_trimmed_span(where + strlen(op), strlen(where + strlen(op)));
    int li = 0, ri = 0; double lv = 0, rv = 0;
    char *ls = NULL, *rs = NULL;
    eval_expr(left, &li, &ls, &lv);
    eval_expr(right, &ri, &rs, &rv);
    if (li || ri) {
        const char *a = li && ls ? ls : "";
        const char *b = ri && rs ? rs : "";
        int cmp = strcmp(a, b);
        if (strcmp(op, "==") == 0) *out_val = (cmp == 0);
        else if (strcmp(op, "!=") == 0) *out_val = (cmp != 0);
        else if (strcmp(op, ">") == 0) *out_val = (cmp > 0);
        else if (strcmp(op, "<") == 0) *out_val = (cmp < 0);
        else if (strcmp(op, ">=") == 0) *out_val = (cmp >= 0);
        else if (strcmp(op, "<=") == 0) *out_val = (cmp <= 0);
    } else {
        if (strcmp(op, "==") == 0) *out_val = (fabs(lv - rv) < 0.000000001);
        else if (strcmp(op, "!=") == 0) *out_val = (fabs(lv - rv) >= 0.000000001);
        else if (strcmp(op, ">") == 0) *out_val = (lv > rv);
        else if (strcmp(op, "<") == 0) *out_val = (lv < rv);
        else if (strcmp(op, ">=") == 0) *out_val = (lv >= rv);
        else if (strcmp(op, "<=") == 0) *out_val = (lv <= rv);
    }
    if (ls) free(ls);
    if (rs) free(rs);
    free(left);
    free(right);
    return 1;
}

static void free_word_expr(char **parts_arr, int p, char *norm, char *tmp0, char *tmp) {
    for (int i = 0; i < p; i++) free(parts_arr[i]);
    free(norm);
    free(tmp0);
    free(tmp);
}

static int eval_expr(const char *expr, int *is_string, char **out_str, double *out_val) {
    *is_string = 0; *out_str = NULL; *out_val = 0;
    if (!expr) return 0;
    char *trimmed = strdup_safe(expr);
    char *clean = trim(trimmed);
    if (strcmp(clean, "input") == 0) {
        int ok = read_input_value(NULL, out_str, out_val, is_string);
        free(trimmed);
        return ok;
    }
    if (starts_with(clean, "input(")) {
        char *open = strchr(clean, '(');
        char *close = strrchr(clean, ')');
        if (open && close && open < close) {
            char *after = close + 1;
            while (*after && isspace((unsigned char)*after)) after++;
            if (*after != '\0') goto skip_input_call;
            char *prompt = copy_trimmed_span(open + 1, (size_t)(close - open - 1));
            char *prompt_text = prompt;
            if (prompt[0] == '"' && prompt[strlen(prompt) - 1] == '"') {
                prompt[strlen(prompt) - 1] = '\0';
                prompt_text = prompt + 1;
            }
            int ok = read_input_value(prompt_text, out_str, out_val, is_string);
            free(prompt);
            free(trimmed);
            return ok;
        }
    }
skip_input_call:
    if (starts_with(clean, "int(") || starts_with(clean, "float(") || starts_with(clean, "number(") || starts_with(clean, "str(")) {
        char *open = strchr(clean, '(');
        char *close = strrchr(clean, ')');
        if (open && close && open < close) {
            char *after = close + 1;
            while (*after && isspace((unsigned char)*after)) after++;
            if (*after != '\0') goto skip_cast_call;
            char *inner = copy_trimmed_span(open + 1, (size_t)(close - open - 1));
            int inner_is_string = 0; char *inner_str = NULL; double inner_val = 0;
            eval_expr(inner, &inner_is_string, &inner_str, &inner_val);
            if (starts_with(clean, "str(")) {
                *is_string = 1;
                if (inner_is_string) *out_str = strdup_safe(inner_str ? inner_str : "");
                else {
                    char buf[64];
                    format_number(inner_val, buf, sizeof(buf));
                    *out_str = strdup_safe(buf);
                }
            } else if (starts_with(clean, "int(")) {
                *out_val = inner_is_string ? atoi(inner_str ? inner_str : "0") : (double)((long long)inner_val);
            } else {
                *out_val = inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val;
            }
            if (inner_str) free(inner_str);
            free(inner);
            free(trimmed);
            return 1;
        }
    }
skip_cast_call:
    if (starts_with(clean, "len(") || starts_with(clean, "type(") || starts_with(clean, "abs(") || starts_with(clean, "round(")) {
        char *open = strchr(clean, '(');
        char *close = strrchr(clean, ')');
        if (open && close && open < close) {
            char *after = close + 1;
            while (*after && isspace((unsigned char)*after)) after++;
            if (*after != '\0') goto skip_library_call;
            char *inner = copy_trimmed_span(open + 1, (size_t)(close - open - 1));
            int inner_is_string = 0; char *inner_str = NULL; double inner_val = 0;
            eval_expr(inner, &inner_is_string, &inner_str, &inner_val);
            if (starts_with(clean, "len(")) {
                *out_val = inner_is_string && inner_str ? (double)strlen(inner_str) : 0;
            } else if (starts_with(clean, "type(")) {
                *is_string = 1;
                if (inner_is_string && inner_str && inner_str[0] == '[') *out_str = strdup_safe("list");
                else if (inner_is_string && inner_str && inner_str[0] == '{') *out_str = strchr(inner_str, ':') ? strdup_safe("dict") : strdup_safe("set");
                else if (inner_is_string && inner_str && inner_str[0] == '(') *out_str = strdup_safe("tuple");
                else *out_str = strdup_safe(inner_is_string ? "str" : "number");
            } else if (starts_with(clean, "abs(")) {
                *out_val = fabs(inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val);
            } else {
                *out_val = round(inner_is_string ? atof(inner_str ? inner_str : "0") : inner_val);
            }
            if (inner_str) free(inner_str);
            free(inner);
            free(trimmed);
            return 1;
        }
    }
skip_library_call:
    if (is_collection_literal(clean)) {
        *is_string = 1;
        *out_str = render_collection_literal(clean);
        free(trimmed);
        return 1;
    }
    if (starts_with(clean, "new ")) {
        char *name = strdup_safe(clean + 4);
        char *paren = strchr(name, '(');
        if (paren && paren[1] == ')') {
            *paren = '\0';
            ClassDef *cls = NULL;
            if (class_get(name, &cls)) {
                *is_string = 1;
                size_t need = strlen(name) + 10;
                *out_str = malloc(need);
                snprintf(*out_str, need, "<%s object>", name);
                free(name);
                free(trimmed);
                return 1;
            }
        }
        free(name);
    }
    free(trimmed);
    if (eval_symbol_comparison(expr, out_val)) {
        *is_string = 0;
        *out_str = NULL;
        return 1;
    }
    const char *s = expr;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '"') {
        *is_string = 1;
        s++;
        char *end = strdup_safe(s);
        char *q = strchr(end, '"'); if (q) *q = '\0';
        *out_str = end;
        return 1;
    }
    int pos = 0;
    if (parse_expression(expr, &pos, is_string, out_str, out_val)) {
        while (expr[pos] && isspace((unsigned char)expr[pos])) pos++;
        if (expr[pos] == '\0') return 1;
    }

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
    size_t cap = 256; char *norm = malloc(cap);
    norm[0] = '\0';
    for (int i=0;i<p;i++) {
        size_t need = strlen(norm) + strlen(parts_arr[i]) + 2;
        if (need > cap) { cap = need * 2; norm = realloc(norm, cap); }
        if (strlen(norm) > 0) strcat(norm, " ");
        strcat(norm, parts_arr[i]);
    }
    char *tmp = strdup_safe(norm);
    char *parts[16]; int pc=0;
    char *t = strtok(tmp, " ");
    while (t && pc<16) { parts[pc++]=t; t=strtok(NULL," "); }
    if (pc==0) {
        for (int i=0;i<p;i++) free(parts_arr[i]);
        free(norm); free(tmp0); free(tmp);
        return 0;
    }
    if (pc==1 && is_decimal_number(parts[0])) {
        *out_val = atof(parts[0]);
        free_word_expr(parts_arr, p, norm, tmp0, tmp);
        return 1;
    }
    if (pc==1) {
        double v=0; int is_var_string=0; char *var_str=NULL;
        if (var_get(parts[0], &v, &var_str, &is_var_string)) {
            if (is_var_string) {
                *is_string = 1; *out_str = var_str; *out_val = 0;
            } else {
                *out_val = v;
                if (var_str) free(var_str);
            }
            free_word_expr(parts_arr, p, norm, tmp0, tmp);
            return 1;
        }
        *out_val = 0;
        free_word_expr(parts_arr, p, norm, tmp0, tmp);
        return 1;
    }
    if (pc==3) {
        double a=0,b=0; int is_a_string=0, is_b_string=0; char *a_str=NULL, *b_str=NULL;
        if (is_decimal_number(parts[0])) a=atof(parts[0]);
        else if (var_get(parts[0], &a, &a_str, &is_a_string)) {
            if (is_a_string) { if (a_str) free(a_str); a = 0; }
        }
        if (is_decimal_number(parts[2])) b=atof(parts[2]);
        else if (var_get(parts[2], &b, &b_str, &is_b_string)) {
            if (is_b_string) { if (b_str) free(b_str); b = 0; }
        }
        if (strcmp(parts[1], "+")==0) *out_val = a + b;
        else if (strcmp(parts[1], "-")==0) *out_val = a - b;
        else if (strcmp(parts[1], "*")==0) *out_val = a * b;
        else if (strcmp(parts[1], "/")==0) *out_val = (b==0?0:(a / b));
        else if (strcmp(parts[1], "%")==0) *out_val = (b==0?0:fmod(a, b));
        if (a_str) free(a_str);
        if (b_str) free(b_str);
        free_word_expr(parts_arr, p, norm, tmp0, tmp);
        return 1;
    }
    if (pc>=4 && strcmp(parts[1], "is")==0) {
        double a=0,b=0; int is_a_string=0, is_b_string=0; char *a_str=NULL, *b_str=NULL;
        if (is_decimal_number(parts[0])) a=atof(parts[0]);
        else if (var_get(parts[0], &a, &a_str, &is_a_string)) {
            if (is_a_string) { if (a_str) free(a_str); a = 0; }
        }
        if (strcmp(parts[2], "greater") == 0 && strcmp(parts[3], "than") == 0 && pc>=5) {
            if (is_decimal_number(parts[4])) b=atof(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a > b);
            if (a_str) free(a_str);
            if (b_str) free(b_str);
            free_word_expr(parts_arr, p, norm, tmp0, tmp);
            return 1;
        }
        if (strcmp(parts[2], "less") == 0 && strcmp(parts[3], "than") == 0 && pc>=5) {
            if (is_decimal_number(parts[4])) b=atof(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a < b);
            if (a_str) free(a_str);
            if (b_str) free(b_str);
            free_word_expr(parts_arr, p, norm, tmp0, tmp);
            return 1;
        }
        if (strcmp(parts[2], "equal") == 0 && strcmp(parts[3], "to") == 0 && pc>=5) {
            if (is_decimal_number(parts[4])) b=atof(parts[4]);
            else if (var_get(parts[4], &b, &b_str, &is_b_string)) {
                if (is_b_string) { if (b_str) free(b_str); b = 0; }
            }
            *out_val = (a == b);
            if (a_str) free(a_str);
            if (b_str) free(b_str);
            free_word_expr(parts_arr, p, norm, tmp0, tmp);
            return 1;
        }
    }
    if (is_decimal_number(parts[0])) {
        *out_val = atof(parts[0]);
        free_word_expr(parts_arr, p, norm, tmp0, tmp);
        return 1;
    }
    double v=0; int is_var_string=0; char *var_str=NULL; if (var_get(parts[0], &v, &var_str, &is_var_string)) {
        if (is_var_string) { *is_string = 1; *out_str = var_str; *out_val = 0; }
        else { *out_val = v; if (var_str) free(var_str); }
        free_word_expr(parts_arr, p, norm, tmp0, tmp);
        return 1;
    }
    free_word_expr(parts_arr, p, norm, tmp0, tmp);
    return 0;
}

static void exec_print_expr(const char *expr) {
    if (!expr) return;
    if (!strchr(expr, '"')) {
        int is_str = 0; char *str = NULL; double val = 0;
        if (eval_expr(expr, &is_str, &str, &val)) {
            if (is_str && str) {
                printf("%s\n", str);
                free(str);
            } else {
                char buf[64];
                format_number(val, buf, sizeof(buf));
                printf("%s\n", buf);
            }
            return;
        }
    }
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
        int is_str = 0; char *str = NULL; double val = 0;
        if (eval_expr(p, &is_str, &str, &val)) {
            if (is_str && str) {
                printf("%s", str);
                free(str);
            } else {
                char buf[64];
                format_number(val, buf, sizeof(buf));
                printf("%s", buf);
            }
        } else {
            printf("%s", p);
        }
        p = next;
    }
    putchar('\n');
    free(copy);
}

static int exec_block(Statement **body) {
    if (!body) return 0;
    int i=0; while (body[i]) { if (exec_stmt(body[i])) return 1; i++; }
    return 0;
}

static char *value_to_literal(int is_str, const char *str, double val) {
    if (is_str) {
        size_t need = strlen(str ? str : "") + 3;
        char *out = malloc(need);
        snprintf(out, need, "\"%s\"", str ? str : "");
        return out;
    }
    char buf[64];
    format_number(val, buf, sizeof(buf));
    return strdup_safe(buf);
}

static int find_top_level_comma(const char *s, int start, int end) {
    int depth = 0;
    int in_string = 0;
    for (int i = start; i < end; i++) {
        if (s[i] == '"') in_string = !in_string;
        if (in_string) continue;
        if (s[i] == '[' || s[i] == '{' || s[i] == '(') depth++;
        else if ((s[i] == ']' || s[i] == '}' || s[i] == ')') && depth > 0) depth--;
        else if (s[i] == ',' && depth == 0) return i;
    }
    return end;
}

static int collection_set_target(const char *target, const char *expr) {
    char *open = strchr(target, '[');
    char *close = strrchr(target, ']');
    if (!open || !close || close < open) return 0;
    char *var_name = copy_trimmed_span(target, (size_t)(open - target));
    char *key_expr = copy_trimmed_span(open + 1, (size_t)(close - open - 1));
    int col_is_str = 0; char *collection = NULL; double col_val = 0;
    if (!var_get(var_name, &col_val, &collection, &col_is_str) || !col_is_str || !collection) {
        free(var_name);
        free(key_expr);
        if (collection) free(collection);
        return 0;
    }
    int value_is_str = 0; char *value_str = NULL; double value_val = 0;
    eval_expr(expr, &value_is_str, &value_str, &value_val);
    char *literal = value_to_literal(value_is_str, value_str, value_val);
    if (value_str) free(value_str);

    size_t len = strlen(collection);
    if (len < 2) {
        free(var_name); free(key_expr); free(collection); free(literal);
        return 0;
    }
    char opener = collection[0];
    char closer = collection[len - 1];
    char *out = NULL;
    if (opener == '[' || opener == '(') {
        int key_is_str = 0; char *key_str = NULL; double key_val = 0;
        eval_expr(key_expr, &key_is_str, &key_str, &key_val);
        int wanted = (int)key_val;
        int idx = 0;
        int start = 1;
        int found_start = -1;
        int found_end = -1;
        while (start < (int)len - 1) {
            int end = find_top_level_comma(collection, start, (int)len - 1);
            if (idx == wanted) {
                found_start = start;
                found_end = end;
                break;
            }
            start = end + 1;
            idx++;
        }
        if (found_start >= 0) {
            while (found_start < found_end && isspace((unsigned char)collection[found_start])) found_start++;
            while (found_end > found_start && isspace((unsigned char)collection[found_end - 1])) found_end--;
            size_t need = (size_t)found_start + strlen(literal) + strlen(collection + found_end) + 1;
            out = malloc(need);
            memcpy(out, collection, (size_t)found_start);
            out[found_start] = '\0';
            strcat(out, literal);
            strcat(out, collection + found_end);
        }
        if (key_str) free(key_str);
    } else if (opener == '{') {
        int key_is_str = 0; char *key_str = NULL; double key_val = 0;
        eval_expr(key_expr, &key_is_str, &key_str, &key_val);
        char *key_lit = value_to_literal(key_is_str, key_str, key_val);
        size_t need = len + strlen(key_lit) + strlen(literal) + 6;
        out = malloc(need);
        if (len == 2) snprintf(out, need, "{%s: %s}", key_lit, literal);
        else {
            memcpy(out, collection, len - 1);
            out[len - 1] = '\0';
            strcat(out, ", ");
            strcat(out, key_lit);
            strcat(out, ": ");
            strcat(out, literal);
            strcat(out, "}");
        }
        if (key_str) free(key_str);
        free(key_lit);
    }
    int success = out != NULL;
    if (out) var_set(var_name, 1, out, 0);
    free(var_name);
    free(key_expr);
    free(collection);
    free(literal);
    if (out) free(out);
    (void)closer;
    return success;
}

static char *call_arg(const char *expr, int wanted) {
    char *open = strchr(expr, '(');
    char *close = strrchr(expr, ')');
    if (!open || !close || close < open) return NULL;
    int arg_start = (int)(open - expr + 1);
    int depth = 0;
    int in_string = 0;
    int index = 0;
    for (int i = arg_start; i <= (int)(close - expr); i++) {
        char c = expr[i];
        if (c == '"') in_string = !in_string;
        if (!in_string) {
            if (c == '[' || c == '{' || c == '(') depth++;
            else if ((c == ']' || c == '}' || c == ')') && depth > 0) depth--;
        }
        if ((c == ',' && depth == 0 && !in_string) || i == (int)(close - expr)) {
            if (index == wanted) return copy_trimmed_span(expr + arg_start, (size_t)(i - arg_start));
            arg_start = i + 1;
            index++;
        }
    }
    return NULL;
}

static int collection_append_or_remove(const char *expr, int remove_value) {
    char *name = call_arg(expr, 0);
    char *value_expr = call_arg(expr, 1);
    if (!name || !value_expr) {
        if (name) free(name);
        if (value_expr) free(value_expr);
        return 0;
    }
    int col_is_str = 0; char *collection = NULL; double col_val = 0;
    if (!var_get(name, &col_val, &collection, &col_is_str) || !col_is_str || !collection) {
        free(name); free(value_expr);
        if (collection) free(collection);
        return 0;
    }
    int value_is_str = 0; char *value_str = NULL; double value_val = 0;
    eval_expr(value_expr, &value_is_str, &value_str, &value_val);
    char *literal = value_to_literal(value_is_str, value_str, value_val);
    if (value_str) free(value_str);

    size_t len = strlen(collection);
    if (len < 2 || strchr(collection, ':')) {
        free(name); free(value_expr); free(collection); free(literal);
        return 0;
    }
    char opener = collection[0];
    char closer = collection[len - 1];
    char *out = NULL;
    if (!remove_value) {
        size_t need = len + strlen(literal) + 4;
        out = malloc(need);
        if (len == 2) snprintf(out, need, "%c%s%c", opener, literal, closer);
        else {
            memcpy(out, collection, len - 1);
            out[len - 1] = '\0';
            strcat(out, ", ");
            strcat(out, literal);
            size_t n = strlen(out);
            out[n] = closer;
            out[n + 1] = '\0';
        }
    } else {
        out = malloc(len + 1);
        size_t n = 0;
        out[n++] = opener;
        int start = 1;
        int wrote = 0;
        while (start < (int)len - 1) {
            int end = find_top_level_comma(collection, start, (int)len - 1);
            int item_start = start;
            int item_end = end;
            while (item_start < item_end && isspace((unsigned char)collection[item_start])) item_start++;
            while (item_end > item_start && isspace((unsigned char)collection[item_end - 1])) item_end--;
            int same = (strlen(literal) == (size_t)(item_end - item_start) && strncmp(collection + item_start, literal, (size_t)(item_end - item_start)) == 0);
            if (!same) {
                if (wrote) {
                    out[n++] = ',';
                    out[n++] = ' ';
                }
                memcpy(out + n, collection + item_start, (size_t)(item_end - item_start));
                n += (size_t)(item_end - item_start);
                wrote = 1;
            }
            start = end + 1;
        }
        out[n++] = closer;
        out[n] = '\0';
    }
    var_set(name, 1, out, 0);
    free(name);
    free(value_expr);
    free(collection);
    free(literal);
    free(out);
    return 1;
}

static void instantiate_class_fields(const char *instance_name, const char *expr) {
    char *clean = strdup_safe(expr ? expr : "");
    char *t = trim(clean);
    if (!starts_with(t, "new ")) {
        free(clean);
        return;
    }
    char *class_name = strdup_safe(t + 4);
    char *paren = strchr(class_name, '(');
    if (paren) *paren = '\0';
    char *class_trim = trim(class_name);
    ClassDef *cls = NULL;
    if (class_get(class_trim, &cls)) {
        for (int i = 0; cls->body && cls->body[i]; i++) {
            Statement *stmt = cls->body[i];
            if (stmt->type == STMT_LET) {
                int is_str = 0; char *str = NULL; double val = 0;
                eval_expr(stmt->expr, &is_str, &str, &val);
                var_set_prefixed(instance_name, stmt->name, is_str, str, val);
                if (str) free(str);
            }
        }
    }
    free(class_name);
    free(clean);
}

static int exec_stmt(Statement *s) {
    if (!s) return 0;
    if (s->type == STMT_PRINT) {
        exec_print_expr(s->expr);
    } else if (s->type == STMT_LET) {
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        var_set(s->name, is_str, str, val);
        instantiate_class_fields(s->name, s->expr);
        if (str) free(str);
    } else if (s->type == STMT_SET) {
        if (strchr(s->name, '[') && collection_set_target(s->name, s->expr)) return 0;
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        var_set(s->name, is_str, str, val);
        if (str) free(str);
    } else if (s->type == STMT_INPUT) {
        int is_str=0; char *str=NULL; double val=0;
        read_input_value(s->extra, &str, &val, &is_str);
        var_set(s->name, is_str, str, val);
        if (str) free(str);
    } else if (s->type == STMT_IF) {
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        if (val && exec_block(s->body)) {
            if (str) free(str);
            return 1;
        }
        if (str) free(str);
    } else if (s->type == STMT_WHILE) {
        int safety = 1000000;
        while (safety-- > 0) {
            int is_str=0; char *str=NULL; double val=0;
            eval_expr(s->expr, &is_str, &str, &val);
            if (str) free(str);
            if (!val) break;
            if (exec_block(s->body)) return 1;
        }
    } else if (s->type == STMT_DEF) {
        function_add(s);
    } else if (s->type == STMT_CLASS) {
        class_add(s);
    } else if (s->type == STMT_FOR) {
        double start_val = 0; double end_val = 0;
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val); start_val = val; if (str) free(str);
        eval_expr(s->extra, &is_str, &str, &val); end_val = val; if (str) free(str);
        double step = (start_val <= end_val) ? 1 : -1;
        for (double i = start_val; step > 0 ? (i <= end_val) : (i >= end_val); i += step) {
            var_set(s->name, 0, NULL, i);
            if (exec_block(s->body)) return 1;
        }
    } else if (s->type == STMT_RETURN) {
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        current_return_active = 1;
        current_return_is_string = is_str;
        current_return_val = val;
        if (is_str) {
            current_return_str = str;
        } else if (str) {
            free(str);
        }
        return 1;
    } else if (s->type == STMT_CALL) {
        if (starts_with(s->expr, "append(") || starts_with(s->expr, "add(")) {
            collection_append_or_remove(s->expr, 0);
            return 0;
        }
        if (starts_with(s->expr, "remove(")) {
            collection_append_or_remove(s->expr, 1);
            return 0;
        }
        int is_str=0; char *str=NULL; double val=0;
        eval_expr(s->expr, &is_str, &str, &val);
        if (str) free(str);
    }
    return 0;
}

void exec_statements(Statement **stmts) {
    if (!stmts) return;
    int i=0; while (stmts[i]) { exec_stmt(stmts[i]); i++; }
}
