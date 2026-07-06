#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"

char *trim(char *s) {
    if (!s) return NULL;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

char *strdup_safe(const char *s) {
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (!d) return NULL;
    strcpy(d, s);
    return d;
}

int is_number(const char *s) {
    if (!s || *s == '\0') return 0;
    const char *p = s;
    if (*p == '-' || *p == '+') p++;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

int is_decimal_number(const char *s) {
    int saw_digit = 0;
    int saw_dot = 0;
    if (!s || *s == '\0') return 0;
    const char *p = s;
    if (*p == '-' || *p == '+') p++;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            saw_digit = 1;
        } else if (*p == '.' && !saw_dot) {
            saw_dot = 1;
        } else {
            return 0;
        }
        p++;
    }
    return saw_digit;
}
