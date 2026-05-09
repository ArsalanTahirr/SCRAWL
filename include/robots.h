#ifndef ROBOTS_H
#define ROBOTS_H

/* ------------------------------------------------------------------ */
/* robots.txt parser                                                    */
/*                                                                      *
 * Fetches and parses /robots.txt for a given origin, honouring the    *
 * directives for our user-agent string and the wildcard "*" agent.    *
 * On any fetch/parse error the rules are treated as "allow all".      *
 * ------------------------------------------------------------------ */

/* Opaque handle returned by robots_fetch() */
typedef struct robots_rules robots_rules_t;

/*
 * robots_fetch – download and parse robots.txt for `origin`
 * (e.g. "https://example.com" — no trailing slash).
 *
 * `user_agent` is the value we send in the User-Agent header AND
 * match against "User-agent:" directives (case-insensitive prefix).
 *
 * Returns a heap-allocated rules handle on success or NULL on error
 * (treat NULL as allow-all).  Caller must call robots_free().
 */
robots_rules_t *robots_fetch(const char *origin, const char *user_agent);

/*
 * robots_allowed – return 1 if crawling `path` is allowed, 0 if not.
 * `path` must start with '/'.  Always returns 1 if rules is NULL.
 */
int robots_allowed(const robots_rules_t *rules, const char *path);

void robots_free(robots_rules_t *rules);

#endif /* ROBOTS_H */
