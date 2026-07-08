#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "interp.h"
#include "utils.h"

/* ---------- values ---------- */

typedef struct Object Object;

typedef enum { V_INT, V_STR, V_OBJ } VType;

typedef struct Value {
    VType t;
    int i;
    char *s;      // owned when t == V_STR
    Object *o;    // shared reference when t == V_OBJ
} Value;

typedef struct Var { char *name; Value v; } Var;

typedef struct Scope {
    Var *vars; int n;
    struct Scope *parent; // lookup falls through to parent (globals)
} Scope;

typedef struct Function {
    char *name;
    char **params; int nparams;
    Statement **body;
} Function;

typedef struct Class {
    char *name;
    Statement **field_inits; int nfields;   // let statements in class body
    Function *methods; int nmethods;
} Class;

struct Object {
    Class *cls;
    Scope fields;
};

static Scope globals = { NULL, 0, NULL };
static Scope *current = &globals;

static Function *functions = NULL; static int functions_n = 0;
static Class *classes = NULL; static int classes_n = 0;

static int returning = 0;
static Value return_value;

static Value make_int(int i) { Value v; v.t = V_INT; v.i = i; v.s = NULL; v.o = NULL; return v; }
static Value make_str(const char *s) { Value v; v.t = V_STR; v.i = 0; v.s = strdup_safe(s ? s : ""); v.o = NULL; return v; }
static Value make_obj(Object *o) { Value v; v.t = V_OBJ; v.i = 0; v.s = NULL; v.o = o; return v; }

static Value value_copy(Value v) {
    if (v.t == V_STR) return make_str(v.s);
    return v;
}

static void value_free(Value *v) {
    if (v->t == V_STR && v->s) { free(v->s); v->s = NULL; }
}

/* ---------- variables ---------- */

static void scope_set(Scope *sc, const char *name, Value v) {
    for (int i = 0; i < sc->n; i++) if (strcmp(sc->vars[i].name, name) == 0) {
        value_free(&sc->vars[i].v);
        sc->vars[i].v = v;
        return;
    }
    sc->vars = realloc(sc->vars, (sc->n + 1) * sizeof(Var));
    sc->vars[sc->n].name = strdup_safe(name);
    sc->vars[sc->n].v = v;
    sc->n++;
}

static Var *scope_find(Scope *sc, const char *name) {
    for (; sc; sc = sc->parent)
        for (int i = 0; i < sc->n; i++)
            if (strcmp(sc->vars[i].name, name) == 0) return &sc->vars[i];
    return NULL;
}

// set in the scope where the variable already exists, else current scope
static void var_assign(const char *name, Value v) {
    for (Scope *sc = current; sc; sc = sc->parent)
        for (int i = 0; i < sc->n; i++)
            if (strcmp(sc->vars[i].name, name) == 0) {
                value_free(&sc->vars[i].v);
                sc->vars[i].v = v;
                return;
            }
    scope_set(current, name, v);
}

/* ---------- functions & classes ---------- */

static Function *find_function(const char *name) {
    for (int i = 0; i < functions_n; i++)
        if (strcmp(functions[i].name, name) == 0) return &functions[i];
    return NULL;
}

static Class *find_class(const char *name) {
    for (int i = 0; i < classes_n; i++)
        if (strcmp(classes[i].name, name) == 0) return &classes[i];
    return NULL;
}

static void parse_params(const char *list, char ***out_params, int *out_n) {
    *out_params = NULL; *out_n = 0;
    if (!list) return;
    char *copy = strdup_safe(list);
    char *tok = strtok(copy, ",");
    while (tok) {
        char *t = trim(tok);
        if (*t) {
            *out_params = realloc(*out_params, (*out_n + 1) * sizeof(char*));
            (*out_params)[(*out_n)++] = strdup_safe(t);
        }
        tok = strtok(NULL, ",");
    }
    free(copy);
}

static void register_function(Function **arr, int *n, const char *name, const char *params, Statement **body) {
    *arr = realloc(*arr, (*n + 1) * sizeof(Function));
    Function *f = &(*arr)[*n];
    f->name = strdup_safe(name);
    parse_params(params, &f->params, &f->nparams);
    f->body = body;
    (*n)++;
}

static void register_class(const char *name, Statement **body) {
    classes = realloc(classes, (classes_n + 1) * sizeof(Class));
    Class *c = &classes[classes_n++];
    c->name = strdup_safe(name);
    c->field_inits = NULL; c->nfields = 0;
    c->methods = NULL; c->nmethods = 0;
    for (int i = 0; body && body[i]; i++) {
        Statement *s = body[i];
        if (s->type == STMT_DEF) {
            register_function(&c->methods, &c->nmethods, s->name, s->expr, s->body);
        } else if (s->type == STMT_LET) {
            c->field_inits = realloc(c->field_inits, (c->nfields + 1) * sizeof(Statement*));
            c->field_inits[c->nfields++] = s;
        }
    }
}

static Function *find_method(Class *c, const char *name) {
    for (int i = 0; c && i < c->nmethods; i++)
        if (strcmp(c->methods[i].name, name) == 0) return &c->methods[i];
    return NULL;
}

/* ---------- input ---------- */

static Value read_input_value(void) {
    char buf[8192];
    if (!fgets(buf, sizeof(buf), stdin)) return make_str("");
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    if (len == 0) return make_str("");
    if (is_number(buf)) return make_int(atoi(buf));
    return make_str(buf);
}

/* ---------- expression normalization ----------
 * Maps English operator words to symbols so the evaluator only
 * deals with symbolic operators:
 *   plus -> +   minus -> -   times/multiply/multiplied -> *
 *   divided/divide -> /   mod/modulo -> %   by -> (skipped)
 *   is greater than -> >   is less than -> <   is equal to -> ==
 *   is not equal to -> !=
 */
static char *normalize_expr(const char *expr) {
    char *copy = strdup_safe(expr);
    char *words[128]; int n = 0;
    char *tok = strtok(copy, " \t");
    while (tok && n < 128) { words[n++] = tok; tok = strtok(NULL, " \t"); }

    size_t cap = strlen(expr) * 2 + 16;
    char *out = malloc(cap);
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        char lower[256]; int k = 0;
        for (char *c = words[i]; *c && k < 255; c++, k++) lower[k] = tolower((unsigned char)*c);
        lower[k] = '\0';
        const char *rep = NULL;
        int skip = 0;
        if (strcmp(lower, "plus") == 0) rep = "+";
        else if (strcmp(lower, "minus") == 0) rep = "-";
        else if (strcmp(lower, "times") == 0 || strcmp(lower, "multiply") == 0 || strcmp(lower, "multiplied") == 0) rep = "*";
        else if (strcmp(lower, "divided") == 0 || strcmp(lower, "divide") == 0) rep = "/";
        else if (strcmp(lower, "mod") == 0 || strcmp(lower, "modulo") == 0) rep = "%";
        else if (strcmp(lower, "by") == 0) skip = 1;
        else if (strcmp(lower, "is") == 0 && i + 2 < n) {
            if (strcmp(words[i+1], "greater") == 0 && strcmp(words[i+2], "than") == 0) { rep = ">"; i += 2; }
            else if (strcmp(words[i+1], "less") == 0 && strcmp(words[i+2], "than") == 0) { rep = "<"; i += 2; }
            else if (strcmp(words[i+1], "equal") == 0 && strcmp(words[i+2], "to") == 0) { rep = "=="; i += 2; }
            else if (i + 3 < n && strcmp(words[i+1], "not") == 0 && strcmp(words[i+2], "equal") == 0 && strcmp(words[i+3], "to") == 0) { rep = "!="; i += 3; }
        }
        if (skip) continue;
        const char *add = rep ? rep : words[i];
        size_t need = strlen(out) + strlen(add) + 2;
        if (need > cap) { cap = need * 2; out = realloc(out, cap); }
        if (out[0]) strcat(out, " ");
        strcat(out, add);
    }
    free(copy);
    return out;
}

/* ---------- recursive-descent expression evaluator ----------
 * grammar:
 *   expr       := comparison
 *   comparison := additive (( > | < | >= | <= | == | != ) additive)?
 *   additive   := term (( + | - ) term)*
 *   term       := unary (( * | / | % ) unary)*
 *   unary      := - unary | primary
 *   primary    := number | "string" | ( expr ) | input
 *              | call name [with args] | new ClassName
 *              | name[(args)] | name.field | name.method(args)
 */

typedef struct { const char *p; } Parser;

static Value eval_expr_str(const char *expr);
static Value parse_comparison(Parser *ps);
static Value call_function(Function *f, Object *self, Value *args, int nargs);

static void skip_ws(Parser *ps) { while (*ps->p && isspace((unsigned char)*ps->p)) ps->p++; }

static int peek(Parser *ps) { skip_ws(ps); return (unsigned char)*ps->p; }

static int match_ch(Parser *ps, char c) {
    skip_ws(ps);
    if (*ps->p == c) { ps->p++; return 1; }
    return 0;
}

static int read_ident(Parser *ps, char *buf, int cap) {
    skip_ws(ps);
    int k = 0;
    while (*ps->p && (isalnum((unsigned char)*ps->p) || *ps->p == '_') && k < cap - 1)
        buf[k++] = *ps->p++;
    buf[k] = '\0';
    return k > 0;
}

static int value_truthy(Value v) {
    if (v.t == V_INT) return v.i != 0;
    if (v.t == V_STR) return v.s && v.s[0];
    return v.o != NULL;
}

static int value_as_int(Value v) {
    if (v.t == V_INT) return v.i;
    if (v.t == V_STR) return atoi(v.s ? v.s : "0");
    return 0;
}

static Value parse_args(Parser *ps, Value *args, int cap, int *out_n, char close_ch) {
    *out_n = 0;
    skip_ws(ps);
    if (close_ch && peek(ps) == (unsigned char)close_ch) return make_int(0);
    while (*ps->p) {
        if (*out_n >= cap) break;
        args[(*out_n)++] = parse_comparison(ps);
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (starts_with(ps->p, "and ")) { ps->p += 4; continue; }
        break;
    }
    return make_int(0);
}

static Value eval_call_named(Parser *ps, const char *name) {
    // args come either as (a, b) or "with a, b" or nothing
    Value args[32]; int nargs = 0;
    skip_ws(ps);
    if (*ps->p == '(') {
        ps->p++;
        parse_args(ps, args, 32, &nargs, ')');
        match_ch(ps, ')');
    } else if (starts_with(ps->p, "with ")) {
        ps->p += 5;
        parse_args(ps, args, 32, &nargs, 0);
    }
    Function *f = find_function(name);
    Value r = f ? call_function(f, NULL, args, nargs) : make_int(0);
    for (int i = 0; i < nargs; i++) value_free(&args[i]);
    return r;
}

static Value eval_member(Parser *ps, Value base) {
    // handles chains: obj.field, obj.method(args), obj.method with args
    while (peek(ps) == '.') {
        ps->p++;
        char member[128];
        if (!read_ident(ps, member, sizeof(member))) break;
        if (base.t != V_OBJ || !base.o) { value_free(&base); return make_int(0); }
        Object *obj = base.o;
        skip_ws(ps);
        if (*ps->p == '(' || starts_with(ps->p, "with ")) {
            Value args[32]; int nargs = 0;
            if (*ps->p == '(') {
                ps->p++;
                parse_args(ps, args, 32, &nargs, ')');
                match_ch(ps, ')');
            } else {
                ps->p += 5;
                parse_args(ps, args, 32, &nargs, 0);
            }
            Function *m = find_method(obj->cls, member);
            Value r = m ? call_function(m, obj, args, nargs) : make_int(0);
            for (int i = 0; i < nargs; i++) value_free(&args[i]);
            base = r;
        } else {
            Var *fv = scope_find(&obj->fields, member);
            if (fv) {
                base = value_copy(fv->v);
            } else {
                Function *m = find_method(obj->cls, member);
                base = m ? call_function(m, obj, NULL, 0) : make_int(0);
            }
        }
    }
    return base;
}

static Object *instantiate(Class *c) {
    Object *obj = calloc(1, sizeof(Object));
    obj->cls = c;
    Scope *saved = current;
    current = &obj->fields;
    obj->fields.parent = NULL;
    for (int i = 0; c && i < c->nfields; i++) {
        Statement *s = c->field_inits[i];
        obj->fields.parent = saved; // field initializers may read outer vars
        Value v = eval_expr_str(s->expr);
        obj->fields.parent = NULL;
        scope_set(&obj->fields, s->name, v);
    }
    current = saved;
    return obj;
}

static Value parse_primary(Parser *ps) {
    skip_ws(ps);
    if (*ps->p == '(') {
        ps->p++;
        Value v = parse_comparison(ps);
        match_ch(ps, ')');
        return eval_member(ps, v);
    }
    if (*ps->p == '"') {
        ps->p++;
        const char *start = ps->p;
        while (*ps->p && *ps->p != '"') ps->p++;
        char *s = malloc(ps->p - start + 1);
        memcpy(s, start, ps->p - start);
        s[ps->p - start] = '\0';
        if (*ps->p == '"') ps->p++;
        Value v; v.t = V_STR; v.i = 0; v.s = s; v.o = NULL;
        return v;
    }
    if (isdigit((unsigned char)*ps->p) ||
        ((*ps->p == '-' || *ps->p == '+') && isdigit((unsigned char)ps->p[1]))) {
        char *end;
        long n = strtol(ps->p, &end, 10);
        ps->p = end;
        return make_int((int)n);
    }
    char ident[128];
    if (read_ident(ps, ident, sizeof(ident))) {
        if (strcmp(ident, "input") == 0) return read_input_value();
        if (strcmp(ident, "call") == 0) {
            char name[128];
            if (!read_ident(ps, name, sizeof(name))) return make_int(0);
            // method call: call obj.method ...
            Var *maybe_obj = scope_find(current, name);
            if (peek(ps) == '.' && maybe_obj && maybe_obj->v.t == V_OBJ)
                return eval_member(ps, value_copy(maybe_obj->v));
            return eval_call_named(ps, name);
        }
        if (strcmp(ident, "new") == 0) {
            char cname[128];
            if (!read_ident(ps, cname, sizeof(cname))) return make_int(0);
            Class *c = find_class(cname);
            if (!c) return make_int(0);
            Value v = make_obj(instantiate(c));
            return eval_member(ps, v);
        }
        skip_ws(ps);
        if (*ps->p == '(') {
            // f(args) style call
            if (find_function(ident)) return eval_call_named(ps, ident);
        }
        Var *var = scope_find(current, ident);
        Value v = var ? value_copy(var->v) : make_int(0);
        return eval_member(ps, v);
    }
    // unknown character: skip it to avoid infinite loops
    if (*ps->p) ps->p++;
    return make_int(0);
}

static Value parse_unary(Parser *ps) {
    skip_ws(ps);
    if (*ps->p == '-') {
        ps->p++;
        Value v = parse_unary(ps);
        int n = value_as_int(v);
        value_free(&v);
        return make_int(-n);
    }
    return parse_primary(ps);
}

static Value parse_term(Parser *ps) {
    Value left = parse_unary(ps);
    for (;;) {
        skip_ws(ps);
        char op = *ps->p;
        if (op != '*' && op != '/' && op != '%') break;
        ps->p++;
        Value right = parse_unary(ps);
        int a = value_as_int(left), b = value_as_int(right);
        value_free(&left); value_free(&right);
        int r = 0;
        if (op == '*') r = a * b;
        else if (op == '/') r = (b == 0 ? 0 : a / b);
        else r = (b == 0 ? 0 : a % b);
        left = make_int(r);
    }
    return left;
}

static Value parse_additive(Parser *ps) {
    Value left = parse_term(ps);
    for (;;) {
        skip_ws(ps);
        char op = *ps->p;
        if (op != '+' && op != '-') break;
        ps->p++;
        // string concatenation with +
        if (op == '+' && left.t == V_STR) {
            Value right = parse_term(ps);
            char buf[64];
            const char *rs;
            if (right.t == V_STR) rs = right.s ? right.s : "";
            else { snprintf(buf, sizeof(buf), "%d", value_as_int(right)); rs = buf; }
            char *joined = malloc(strlen(left.s ? left.s : "") + strlen(rs) + 1);
            strcpy(joined, left.s ? left.s : "");
            strcat(joined, rs);
            value_free(&left); value_free(&right);
            Value v; v.t = V_STR; v.i = 0; v.s = joined; v.o = NULL;
            left = v;
            continue;
        }
        Value right = parse_term(ps);
        int a = value_as_int(left), b = value_as_int(right);
        value_free(&left); value_free(&right);
        left = make_int(op == '+' ? a + b : a - b);
    }
    return left;
}

static Value parse_comparison(Parser *ps) {
    Value left = parse_additive(ps);
    skip_ws(ps);
    const char *p = ps->p;
    int op = 0; // 1:> 2:< 3:>= 4:<= 5:== 6:!=
    if (p[0] == '>' && p[1] == '=') { op = 3; ps->p += 2; }
    else if (p[0] == '<' && p[1] == '=') { op = 4; ps->p += 2; }
    else if (p[0] == '=' && p[1] == '=') { op = 5; ps->p += 2; }
    else if (p[0] == '!' && p[1] == '=') { op = 6; ps->p += 2; }
    else if (p[0] == '>') { op = 1; ps->p += 1; }
    else if (p[0] == '<') { op = 2; ps->p += 1; }
    if (!op) return left;
    Value right = parse_additive(ps);
    int r;
    if ((op == 5 || op == 6) && left.t == V_STR && right.t == V_STR) {
        int eq = strcmp(left.s ? left.s : "", right.s ? right.s : "") == 0;
        r = (op == 5) ? eq : !eq;
    } else {
        int a = value_as_int(left), b = value_as_int(right);
        switch (op) {
            case 1: r = a > b; break;
            case 2: r = a < b; break;
            case 3: r = a >= b; break;
            case 4: r = a <= b; break;
            case 5: r = a == b; break;
            default: r = a != b; break;
        }
    }
    value_free(&left); value_free(&right);
    return make_int(r);
}

static Value eval_expr_str(const char *expr) {
    if (!expr) return make_int(0);
    char *norm = normalize_expr(expr);
    Parser ps = { norm };
    Value v = parse_comparison(&ps);
    free(norm);
    return v;
}

/* ---------- execution ---------- */

static void exec_stmt(Statement *s);

static void exec_block(Statement **body) {
    if (!body) return;
    for (int i = 0; body[i]; i++) {
        exec_stmt(body[i]);
        if (returning) return;
    }
}

static Value call_function(Function *f, Object *self, Value *args, int nargs) {
    Scope local = { NULL, 0, &globals };
    Scope *saved = current;
    for (int i = 0; i < f->nparams; i++)
        scope_set(&local, f->params[i], i < nargs ? value_copy(args[i]) : make_int(0));
    if (self) {
        // fields resolve after locals but before globals
        Scope *fields = &self->fields;
        fields->parent = &globals;
        local.parent = fields;
        scope_set(&local, "self", make_obj(self));
    }
    current = &local;
    exec_block(f->body);
    current = saved;
    if (self) self->fields.parent = NULL;
    Value r = returning ? return_value : make_int(0);
    returning = 0;
    return_value = make_int(0);
    for (int i = 0; i < local.n; i++) { free(local.vars[i].name); value_free(&local.vars[i].v); }
    free(local.vars);
    return r;
}

static void print_value(Value v) {
    if (v.t == V_STR) printf("%s", v.s ? v.s : "");
    else if (v.t == V_OBJ) printf("<%s object>", v.o && v.o->cls ? v.o->cls->name : "null");
    else printf("%d", v.i);
}

// Print statement: quoted segments are printed literally; each unquoted
// whitespace-separated segment (keeping balanced parens together) is
// evaluated as an expression.
static void exec_print_expr(const char *expr) {
    if (!expr) { putchar('\n'); return; }
    const char *p = expr;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            const char *start = p;
            while (*p && *p != '"') p++;
            printf("%.*s", (int)(p - start), start);
            if (*p == '"') p++;
            continue;
        }
        // a "call"/"new" segment consumes the rest of the line
        if (starts_with(p, "call ") || starts_with(p, "new ")) {
            Value v = eval_expr_str(p);
            print_value(v);
            value_free(&v);
            break;
        }
        // a comparison consumes the whole unquoted run
        {
            const char *q = p;
            int cmp = 0;
            while (*q && *q != '"') {
                if (*q == '>' || *q == '<' || *q == '!' ||
                    (*q == '=' && q[1] == '=')) { cmp = 1; break; }
                if ((q == p || isspace((unsigned char)q[-1])) && starts_with(q, "is ")) { cmp = 1; break; }
                q++;
            }
            if (cmp) {
                q = p;
                while (*q && *q != '"') q++;
                char *seg = malloc(q - p + 1);
                memcpy(seg, p, q - p);
                seg[q - p] = '\0';
                Value v = eval_expr_str(seg);
                print_value(v);
                value_free(&v);
                free(seg);
                p = q;
                continue;
            }
        }
        const char *start = p;
        int depth = 0;
        while (*p && (depth > 0 || (!isspace((unsigned char)*p) && *p != '"'))) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            p++;
        }
        char *seg = malloc(p - start + 1);
        memcpy(seg, start, p - start);
        seg[p - start] = '\0';
        Value v = eval_expr_str(seg);
        print_value(v);
        value_free(&v);
        free(seg);
    }
    putchar('\n');
}

static void exec_stmt(Statement *s) {
    if (!s) return;
    switch (s->type) {
        case STMT_PRINT:
            exec_print_expr(s->expr);
            break;
        case STMT_LET:
        case STMT_SET: {
            char *dot = s->name ? strchr(s->name, '.') : NULL;
            Value v = eval_expr_str(s->expr);
            if (dot) {
                // set obj.field to <expr>
                char *obj_name = strdup_safe(s->name);
                obj_name[dot - s->name] = '\0';
                const char *field = dot + 1;
                Var *var = scope_find(current, obj_name);
                if (var && var->v.t == V_OBJ && var->v.o)
                    scope_set(&var->v.o->fields, field, v);
                else
                    value_free(&v);
                free(obj_name);
            } else {
                var_assign(s->name, v);
            }
            break;
        }
        case STMT_INPUT:
            var_assign(s->name, read_input_value());
            break;
        case STMT_IF: {
            Value v = eval_expr_str(s->expr);
            int t = value_truthy(v);
            value_free(&v);
            if (t) exec_block(s->body);
            break;
        }
        case STMT_WHILE: {
            int safety = 1000000; // avoid infinite loops
            while (safety-- > 0) {
                Value v = eval_expr_str(s->expr);
                int t = value_truthy(v);
                value_free(&v);
                if (!t) break;
                exec_block(s->body);
                if (returning) break;
            }
            break;
        }
        case STMT_DEF:
            register_function(&functions, &functions_n, s->name, s->expr, s->body);
            break;
        case STMT_CLASS:
            register_class(s->name, s->body);
            break;
        case STMT_RETURN:
            return_value = s->expr ? eval_expr_str(s->expr) : make_int(0);
            returning = 1;
            break;
        case STMT_EXPR: {
            Value v = eval_expr_str(s->expr);
            value_free(&v);
            break;
        }
    }
}

void exec_statements(Statement **stmts) {
    if (!stmts) return;
    return_value = make_int(0);
    for (int i = 0; stmts[i]; i++) {
        exec_stmt(stmts[i]);
        if (returning) break;
    }
}
