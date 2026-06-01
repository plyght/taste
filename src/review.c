#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *body = calloc((size_t)n + 1, 1);
    fread(body, 1, (size_t)n, f);
    fclose(f);
    return body;
}

static char *json_string(const char *object, const char *key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = strstr(object, needle);
    if (!p) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    char *end = strchr(p, '"');
    if (!end) return NULL;
    size_t n = (size_t)(end - p);
    char *out = calloc(n + 1, 1);
    memcpy(out, p, n);
    return out;
}

static double json_number(const char *object, const char *key, double fallback) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = strstr(object, needle);
    if (!p) return fallback;
    p = strchr(p + strlen(needle), ':');
    if (!p) return fallback;
    return strtod(p + 1, NULL);
}

int edge_candidates(TasteDB *tdb, int limit, bool json) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT e.id,a.name,b.name,e.relationship,e.weight,COALESCE(group_concat(ee.evidence,'; '),'') FROM edges e JOIN artists a ON a.id=e.source_id JOIN artists b ON b.id=e.target_id LEFT JOIN edge_evidence ee ON ee.edge_id=e.id LEFT JOIN edge_reviews er ON er.edge_id=e.id WHERE er.id IS NULL GROUP BY e.id,a.name,b.name,e.relationship,e.weight ORDER BY e.weight ASC,a.name,b.name LIMIT ?", -1, &st, NULL);
    sqlite3_bind_int(st, 1, limit);
    int n = 0;
    if (json) printf("[\n");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        const char *source = (const char *)sqlite3_column_text(st, 1);
        const char *target = (const char *)sqlite3_column_text(st, 2);
        const char *rel = (const char *)sqlite3_column_text(st, 3);
        double weight = sqlite3_column_double(st, 4);
        const char *evidence = (const char *)sqlite3_column_text(st, 5);
        if (json) {
            printf("%s  {\"id\":%d,\"source_artist\":", n ? ",\n" : "", id);
            print_json_string(stdout, source);
            printf(",\"target_artist\":");
            print_json_string(stdout, target);
            printf(",\"relationship\":");
            print_json_string(stdout, rel);
            printf(",\"weight\":%.2f,\"evidence\":", weight);
            print_json_string(stdout, evidence);
            printf("}\n");
        } else {
            printf("%d %s -> %s %s %.2f %s\n", id, source, target, rel, weight, evidence);
        }
        n++;
    }
    if (json) printf("]\n");
    sqlite3_finalize(st);
    return 0;
}

static int edge_id(TasteDB *tdb, const char *source, const char *target) {
    int sid = db_artist_id(tdb, source), tid = db_artist_id(tdb, target);
    sqlite3_stmt *st;
    int id = 0;
    sqlite3_prepare_v2(tdb->db, "SELECT id FROM edges WHERE source_id=? AND target_id=? ORDER BY weight DESC LIMIT 1", -1, &st, NULL);
    sqlite3_bind_int(st, 1, sid);
    sqlite3_bind_int(st, 2, tid);
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return id;
}

static void apply_review(TasteDB *tdb, int eid, const char *verdict, double confidence, const char *relationship, const char *rationale) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "INSERT INTO edge_reviews(edge_id,verdict,confidence,relationship,rationale) VALUES(?,?,?,?,?)", -1, &st, NULL);
    sqlite3_bind_int(st, 1, eid);
    sqlite3_bind_text(st, 2, verdict, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 3, confidence);
    sqlite3_bind_text(st, 4, relationship, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, rationale, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    if (strcmp(verdict, "reject") == 0 || strcmp(verdict, "bad") == 0) {
        sqlite3_prepare_v2(tdb->db, "UPDATE edges SET weight=weight*0.1 WHERE id=?", -1, &st, NULL);
        sqlite3_bind_int(st, 1, eid);
        sqlite3_step(st);
        sqlite3_finalize(st);
    } else if (strcmp(verdict, "keep") == 0 && relationship && relationship[0]) {
        sqlite3_prepare_v2(tdb->db, "UPDATE edges SET relationship=? WHERE id=?", -1, &st, NULL);
        sqlite3_bind_text(st, 1, relationship, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, eid);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
}

int edge_review_file(TasteDB *tdb, const char *path) {
    char *body = read_file(path);
    if (!body) return 1;
    int applied = 0, rejected = 0;
    char *p = body;
    while ((p = strchr(p, '{'))) {
        char *end = strchr(p, '}');
        if (!end) break;
        *end = 0;
        char *source = json_string(p, "source_artist");
        char *target = json_string(p, "target_artist");
        char *verdict = json_string(p, "verdict");
        char *relationship = json_string(p, "relationship");
        char *rationale = json_string(p, "rationale");
        if (!rationale) rationale = json_string(p, "evidence");
        double confidence = json_number(p, "confidence", 0.0);
        int eid = source && target ? edge_id(tdb, source, target) : 0;
        if (eid && verdict) {
            apply_review(tdb, eid, verdict, confidence, relationship ? relationship : "", rationale ? rationale : "");
            applied++;
        } else {
            rejected++;
        }
        free(source);
        free(target);
        free(verdict);
        free(relationship);
        free(rationale);
        p = end + 1;
    }
    printf("applied %d reviews, rejected %d reviews\n", applied, rejected);
    free(body);
    return rejected ? 1 : 0;
}
