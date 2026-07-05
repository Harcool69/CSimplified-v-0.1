#ifndef CSIMPLIFIED_PARSER_H
#define CSIMPLIFIED_PARSER_H

typedef enum {STMT_PRINT, STMT_LET, STMT_SET, STMT_IF, STMT_WHILE, STMT_INPUT,
              STMT_DEF, STMT_RETURN, STMT_CLASS, STMT_EXPR} StmtType;

typedef struct Statement {
    StmtType type;
    char *name;       // for LET/SET/DEF/CLASS
    char *expr;       // expression, string, or param list for DEF
    struct Statement **body;
    int body_count;
} Statement;

// Parse file into array of top-level statements. Caller must free via free_statements
Statement **parse_file(const char *path, int *out_count);
void free_statements(Statement **stmts, int n);

#endif
