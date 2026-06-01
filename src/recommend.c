#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double mode_factor(const char *mode, const char *rel) {
    if (!mode) return 1.0;
    if (strcmp(mode, "safe") == 0) return strstr(rel, "associated") || strstr(rel, "label") ? 1.25 : 1.0;
    if (strcmp(mode, "deep-cut") == 0) return strstr(rel, "genre") ? 0.9 : 1.15;
    if (strcmp(mode, "adjacent") == 0) return strstr(rel, "genre") ? 1.2 : 1.0;
    if (strcmp(mode, "influence") == 0 || strcmp(mode, "descendant") == 0) return 1.0;
    return 1.0;
}

int recommend_artists(TasteDB *tdb, char **seeds, int seed_count, const char *mode, int limit, bool json) {
    char seed_ids[512] = "0";
    for (int i = 0; i < seed_count; i++) {
        int id = db_artist_id(tdb, seeds[i]);
        char tmp[32];
        snprintf(tmp, sizeof(tmp), ",%d", id);
        strncat(seed_ids, tmp, sizeof(seed_ids) - strlen(seed_ids) - 1);
    }
    sqlite3_stmt *st;
    char sql[2048];
    snprintf(sql, sizeof(sql), "SELECT a.name,e.relationship,e.weight,COALESCE(group_concat(ee.evidence,'; '),''),count(DISTINCT e.source_id) FROM edges e JOIN artists a ON a.id=e.target_id LEFT JOIN edge_evidence ee ON ee.edge_id=e.id WHERE e.source_id IN(%s) AND e.target_id NOT IN(%s) GROUP BY e.target_id,e.relationship ORDER BY sum(e.weight) DESC LIMIT 200", seed_ids, seed_ids);
    sqlite3_prepare_v2(tdb->db, sql, -1, &st, NULL);
    Recommendation *recs = calloc(200, sizeof(Recommendation));
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < 200) {
        const char *name = (const char *)sqlite3_column_text(st, 0), *rel = (const char *)sqlite3_column_text(st, 1), *ev = (const char *)sqlite3_column_text(st, 3);
        double weight = sqlite3_column_double(st, 2);
        int coverage = sqlite3_column_int(st, 4);
        double score = weight * mode_factor(mode, rel) + coverage * 0.75;
        int found = -1;
        for (int i = 0; i < n; i++) if (strcmp(recs[i].artist, name) == 0) found = i;
        if (found >= 0) {
            recs[found].score += score;
        } else {
            recs[n].artist = strdup_safe(name);
            recs[n].score = score;
            recs[n].explanation = strdup_safe(ev);
            n++;
        }
    }
    sqlite3_finalize(st);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (recs[j].score > recs[i].score) {
        Recommendation t = recs[i]; recs[i] = recs[j]; recs[j] = t;
    }
    if (json) printf("[\n");
    int out = n < limit ? n : limit;
    for (int i = 0; i < out; i++) {
        if (json) printf("  {\"artist\":\"%s\",\"score\":%.3f,\"explanation\":\"%s\"}%s\n", recs[i].artist, recs[i].score, recs[i].explanation, i + 1 == out ? "" : ",");
        else printf("%d. %s %.2f\n   %s\n", i + 1, recs[i].artist, recs[i].score, recs[i].explanation);
    }
    if (json) printf("]\n");
    for (int i = 0; i < n; i++) { free(recs[i].artist); free(recs[i].explanation); }
    free(recs);
    return out ? 0 : 1;
}

int explain_edge(TasteDB *tdb, const char *source, const char *target, bool json) {
    int sid = db_artist_id(tdb, source), tid = db_artist_id(tdb, target);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT e.relationship,e.weight,ee.evidence,ee.provenance FROM edges e LEFT JOIN edge_evidence ee ON ee.edge_id=e.id WHERE e.source_id=? AND e.target_id=? ORDER BY e.weight DESC", -1, &st, NULL);
    sqlite3_bind_int(st, 1, sid);
    sqlite3_bind_int(st, 2, tid);
    int n = 0;
    if (json) printf("[\n");
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *rel = (const char *)sqlite3_column_text(st, 0), *ev = (const char *)sqlite3_column_text(st, 2), *prov = (const char *)sqlite3_column_text(st, 3);
        double w = sqlite3_column_double(st, 1);
        if (json) printf("%s  {\"relationship\":\"%s\",\"weight\":%.2f,\"evidence\":\"%s\",\"provenance\":\"%s\"}\n", n ? ",\n" : "", rel, w, ev ? ev : "", prov ? prov : "");
        else printf("%s -> %s: %s %.2f (%s)\n", source, target, rel, w, ev ? ev : "");
        n++;
    }
    if (json) printf("]\n");
    sqlite3_finalize(st);
    return n ? 0 : 1;
}

int inspect_graph(TasteDB *tdb, const char *artist, bool json) {
    int id = db_artist_id(tdb, artist);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT b.name,e.relationship,e.weight,COALESCE(group_concat(ee.evidence,'; '),'') FROM edges e JOIN artists b ON b.id=e.target_id LEFT JOIN edge_evidence ee ON ee.edge_id=e.id WHERE e.source_id=? GROUP BY b.name,e.relationship,e.weight ORDER BY e.weight DESC,b.name", -1, &st, NULL);
    sqlite3_bind_int(st, 1, id);
    int n = 0;
    if (json) printf("[\n");
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0), *rel = (const char *)sqlite3_column_text(st, 1), *ev = (const char *)sqlite3_column_text(st, 3);
        double w = sqlite3_column_double(st, 2);
        if (json) printf("%s  {\"artist\":\"%s\",\"relationship\":\"%s\",\"weight\":%.2f,\"evidence\":\"%s\"}\n", n ? ",\n" : "", name, rel, w, ev);
        else printf("%s %.2f %s %s\n", name, w, rel, ev);
        n++;
    }
    if (json) printf("]\n");
    sqlite3_finalize(st);
    return n ? 0 : 1;
}
