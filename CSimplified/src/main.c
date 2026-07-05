#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "interp.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.csimpl>\n", argv[0]);
        return 1;
    }
    int n = 0;
    Statement **stmts = parse_file(argv[1], &n);
    if (!stmts) { fprintf(stderr, "Failed to parse %s\n", argv[1]); return 1; }
    exec_statements(stmts);
    free_statements(stmts, n);
    return 0;
}
