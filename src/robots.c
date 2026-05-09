#include "robots.h"
#include "fetch.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp — POSIX, not C11 */
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

/*
 * A single Disallow or Allow rule.
 * Allow rules take precedence over Disallow rules of the same length
 * (standard robots.txt convention).
 */
typedef struct rule_entry {
    char              *path;      /* prefix to match against */
    int                is_allow;  /* 1 = Allow, 0 = Disallow */
    struct rule_entry *next;
} rule_entry_t;

struct robots_rules {
    rule_entry_t *rules;   /* singly-linked list, order preserved */
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static char *str_tolower_dup(const char *s)
{
    char *d = strdup(s);
    if (d == NULL) return NULL;
    for (char *p = d; *p; p++) *p = (char)tolower((unsigned char)*p);
    return d;
}

/* Trim leading and trailing whitespace in-place */
static char *str_trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}

/* ------------------------------------------------------------------ */
/* Parser                                                               */
/* ------------------------------------------------------------------ */

/*
 * parse_robots – scan the robots.txt text and collect rules that apply
 * to `our_agent` (lowercased) or the wildcard "*".
 *
 * Rules are added for the currently-active agent block; we only keep
 * rules from blocks that match our agent or the catch-all "*".
 */
static robots_rules_t *parse_robots(const char *text, const char *our_agent)
{
    robots_rules_t *rr = malloc(sizeof(*rr));
    if (rr == NULL) return NULL;
    rr->rules = NULL;

    char *our_agent_lower = str_tolower_dup(our_agent);
    if (our_agent_lower == NULL) { free(rr); return NULL; }

    /*
     * We do a two-pass approach:
     *   1. Collect rules for our specific agent name.
     *   2. If none found, collect rules for "*".
     * This matches the spec: specific agent blocks take full priority.
     */
    int passes = 2;
    const char *agents_to_try[2] = { our_agent_lower, "*" };

    for (int pass = 0; pass < passes; pass++) {
        const char *target_agent = agents_to_try[pass];

        /* If pass 0 already found rules, skip the wildcard pass */
        if (pass == 1 && rr->rules != NULL) break;

        /* Walk through the text line by line */
        const char *line_start = text;
        int in_our_block = 0;

        while (*line_start != '\0') {
            /* Find end of line */
            const char *line_end = line_start;
            while (*line_end != '\0' && *line_end != '\n') line_end++;

            /* Copy line into a mutable buffer */
            size_t line_len = (size_t)(line_end - line_start);
            char  *line     = malloc(line_len + 1);
            if (line == NULL) goto done;
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';

            /* Strip inline comment */
            char *hash = strchr(line, '#');
            if (hash != NULL) *hash = '\0';

            char *trimmed = str_trim(line);

            if (strncasecmp(trimmed, "User-agent:", 11) == 0) {
                char *agent = str_trim(trimmed + 11);
                char *agent_lower = str_tolower_dup(agent);
                if (agent_lower != NULL) {
                    /* Check for exact match or prefix match for our agent */
                    in_our_block = (strcmp(agent_lower, target_agent) == 0) ||
                                   (pass == 0 &&
                                    strncmp(agent_lower, target_agent,
                                            strlen(target_agent)) == 0);
                    free(agent_lower);
                }
            } else if (in_our_block) {
                int   is_allow = 0;
                char *path     = NULL;

                if (strncasecmp(trimmed, "Allow:", 6) == 0) {
                    is_allow = 1;
                    path     = str_trim(trimmed + 6);
                } else if (strncasecmp(trimmed, "Disallow:", 9) == 0) {
                    is_allow = 0;
                    path     = str_trim(trimmed + 9);
                }

                if (path != NULL && path[0] != '\0') {
                    rule_entry_t *entry = malloc(sizeof(*entry));
                    if (entry == NULL) { free(line); goto done; }
                    entry->path     = strdup(path);
                    entry->is_allow = is_allow;
                    entry->next     = NULL;

                    if (entry->path == NULL) { free(entry); free(line); goto done; }

                    /* Append to end of list (preserve file order) */
                    if (rr->rules == NULL) {
                        rr->rules = entry;
                    } else {
                        rule_entry_t *tail = rr->rules;
                        while (tail->next != NULL) tail = tail->next;
                        tail->next = entry;
                    }
                }
            }

            free(line);
            line_start = (*line_end == '\n') ? line_end + 1 : line_end;
        }
    }

done:
    free(our_agent_lower);
    return rr;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

robots_rules_t *robots_fetch(const char *origin, const char *user_agent)
{
    /* Build the robots.txt URL */
    size_t url_len = strlen(origin) + sizeof("/robots.txt") + 1;
    char  *robots_url = malloc(url_len);
    if (robots_url == NULL) return NULL;
    snprintf(robots_url, url_len, "%s/robots.txt", origin);

    fetch_result_t result;
    int ok = fetch_page(robots_url, &result);
    free(robots_url);

    if (ok != 0) {
        /* Could not fetch — allow all */
        return NULL;
    }

    robots_rules_t *rules = parse_robots(result.data, user_agent);
    free_fetch_result(&result);
    return rules;
}

int robots_allowed(const robots_rules_t *rules, const char *path)
{
    /* NULL rules → allow all */
    if (rules == NULL || path == NULL) return 1;

    /*
     * Standard matching algorithm:
     * Walk all rules; find the one with the longest matching prefix.
     * If there is a tie between Allow and Disallow, Allow wins.
     */
    size_t best_len  = 0;
    int    best_perm = 1;   /* default: allowed */

    for (const rule_entry_t *r = rules->rules; r != NULL; r = r->next) {
        size_t rule_len = strlen(r->path);
        if (rule_len == 0) continue;   /* empty Disallow = allow all */

        if (strncmp(path, r->path, rule_len) == 0) {
            if (rule_len > best_len ||
                (rule_len == best_len && r->is_allow)) {
                best_len  = rule_len;
                best_perm = r->is_allow;
            }
        }
    }

    return best_perm;
}

void robots_free(robots_rules_t *rules)
{
    if (rules == NULL) return;
    rule_entry_t *entry = rules->rules;
    while (entry != NULL) {
        rule_entry_t *next = entry->next;
        free(entry->path);
        free(entry);
        entry = next;
    }
    free(rules);
}
