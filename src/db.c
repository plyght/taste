#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strdup_safe(const char *s) {
    if (!s) return strdup("");
    return strdup(s);
}

char *slugify(const char *s) {
    size_t n = strlen(s);
    char *out = calloc(n + 1, 1);
    size_t j = 0;
    bool dash = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
            dash = false;
        } else if (!dash && j > 0) {
            out[j++] = '-';
            dash = true;
        }
    }
    if (j > 0 && out[j - 1] == '-') j--;
    out[j] = 0;
    return out;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s\n", err ? err : "sqlite error");
        sqlite3_free(err);
    }
    return rc == SQLITE_OK ? 0 : 1;
}

int db_open(TasteDB *tdb, const char *path) {
    tdb->path = path;
    if (sqlite3_open(path, &tdb->db) != SQLITE_OK) return 1;
    return db_init(tdb);
}

void db_close(TasteDB *tdb) {
    if (tdb->db) sqlite3_close(tdb->db);
}

int db_init(TasteDB *tdb) {
    return exec_sql(tdb->db,
                    "PRAGMA foreign_keys=ON;"
                    "CREATE TABLE IF NOT EXISTS artists(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE,slug TEXT NOT NULL UNIQUE,wikidata TEXT,musicbrainz TEXT);"
                    "CREATE TABLE IF NOT EXISTS artist_aliases(artist_id INTEGER,alias TEXT,UNIQUE(artist_id,alias));"
                    "CREATE TABLE IF NOT EXISTS artist_sources(artist_id INTEGER,source TEXT,UNIQUE(artist_id,source));"
                    "CREATE TABLE IF NOT EXISTS artist_facts(artist_id INTEGER,kind TEXT,value TEXT,source TEXT,UNIQUE(artist_id,kind,value));"
                    "CREATE TABLE IF NOT EXISTS edges(id INTEGER PRIMARY KEY,source_id INTEGER,target_id INTEGER,relationship TEXT,weight REAL,UNIQUE(source_id,target_id,relationship));"
                    "CREATE TABLE IF NOT EXISTS edge_evidence(edge_id INTEGER,evidence TEXT,provenance TEXT,UNIQUE(edge_id,evidence));"
                    "CREATE TABLE IF NOT EXISTS recommendation_feedback(id INTEGER PRIMARY KEY,seed_key TEXT,target_id INTEGER,rating TEXT,created_at TEXT DEFAULT CURRENT_TIMESTAMP);"
                    "CREATE TABLE IF NOT EXISTS raw_source_documents(id INTEGER PRIMARY KEY,source TEXT,key TEXT,body TEXT,created_at TEXT DEFAULT CURRENT_TIMESTAMP);"
                    "CREATE TABLE IF NOT EXISTS edge_reviews(id INTEGER PRIMARY KEY,edge_id INTEGER,verdict TEXT,confidence REAL,relationship TEXT,rationale TEXT,created_at TEXT DEFAULT CURRENT_TIMESTAMP);");
}

int db_artist_id(TasteDB *tdb, const char *name) {
    char *slug = slugify(name);
    sqlite3_stmt *st;
    int id = 0;
    sqlite3_prepare_v2(tdb->db, "SELECT id FROM artists WHERE slug=? OR name=?", -1, &st, NULL);
    sqlite3_bind_text(st, 1, slug, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    free(slug);
    return id;
}

static void insert_fact(TasteDB *tdb, int id, const char *kind, const char *value, const char *source) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "INSERT OR IGNORE INTO artist_facts(artist_id,kind,value,source) VALUES(?,?,?,?)", -1, &st, NULL);
    sqlite3_bind_int(st, 1, id);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, source, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

int db_upsert_artist(TasteDB *tdb, ArtistNote *note, const char *source) {
    char *slug = note->slug ? strdup_safe(note->slug) : slugify(note->name);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "INSERT INTO artists(name,slug,wikidata,musicbrainz) VALUES(?,?,?,?) ON CONFLICT(slug) DO UPDATE SET name=excluded.name,wikidata=COALESCE(excluded.wikidata,artists.wikidata),musicbrainz=COALESCE(excluded.musicbrainz,artists.musicbrainz)", -1, &st, NULL);
    sqlite3_bind_text(st, 1, note->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, note->wikidata, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, note->musicbrainz, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    int id = db_artist_id(tdb, note->name);
    sqlite3_prepare_v2(tdb->db, "INSERT OR IGNORE INTO artist_sources(artist_id,source) VALUES(?,?)", -1, &st, NULL);
    sqlite3_bind_int(st, 1, id);
    sqlite3_bind_text(st, 2, source, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    for (int i = 0; i < note->genre_count; i++) insert_fact(tdb, id, "genre", note->genres[i], source);
    for (int i = 0; i < note->label_count; i++) insert_fact(tdb, id, "label", note->labels[i], source);
    for (int i = 0; i < note->associated_count; i++) insert_fact(tdb, id, "associated", note->associated[i], source);
    for (int i = 0; i < note->alias_count; i++) {
        sqlite3_prepare_v2(tdb->db, "INSERT OR IGNORE INTO artist_aliases(artist_id,alias) VALUES(?,?)", -1, &st, NULL);
        sqlite3_bind_int(st, 1, id);
        sqlite3_bind_text(st, 2, note->aliases[i], -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    free(slug);
    return id ? 0 : 1;
}

int db_add_edge(TasteDB *tdb, const char *source, const char *target, const char *rel, double weight, const char *evidence, const char *provenance) {
    int sid = db_artist_id(tdb, source), tid = db_artist_id(tdb, target);
    if (!sid || !tid || sid == tid) return 1;
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "INSERT INTO edges(source_id,target_id,relationship,weight) VALUES(?,?,?,?) ON CONFLICT(source_id,target_id,relationship) DO UPDATE SET weight=max(weight,excluded.weight)", -1, &st, NULL);
    sqlite3_bind_int(st, 1, sid);
    sqlite3_bind_int(st, 2, tid);
    sqlite3_bind_text(st, 3, rel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 4, weight);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sqlite3_prepare_v2(tdb->db, "SELECT id FROM edges WHERE source_id=? AND target_id=? AND relationship=?", -1, &st, NULL);
    sqlite3_bind_int(st, 1, sid);
    sqlite3_bind_int(st, 2, tid);
    sqlite3_bind_text(st, 3, rel, -1, SQLITE_TRANSIENT);
    int eid = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    if (eid) {
        sqlite3_prepare_v2(tdb->db, "INSERT OR IGNORE INTO edge_evidence(edge_id,evidence,provenance) VALUES(?,?,?)", -1, &st, NULL);
        sqlite3_bind_int(st, 1, eid);
        sqlite3_bind_text(st, 2, evidence, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, provenance, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return 0;
}

int db_record_feedback(TasteDB *tdb, const char *seed_key, const char *artist, const char *rating) {
    int id = db_artist_id(tdb, artist);
    if (!id) return 1;
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "INSERT INTO recommendation_feedback(seed_key,target_id,rating) VALUES(?,?,?)", -1, &st, NULL);
    sqlite3_bind_text(st, 1, seed_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, id);
    sqlite3_bind_text(st, 3, rating, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : 1;
    sqlite3_finalize(st);
    return rc;
}

void free_artist_note(ArtistNote *note) {
    free(note->name); free(note->slug); free(note->wikidata); free(note->musicbrainz);
    for (int i = 0; i < note->alias_count; i++) free(note->aliases[i]);
    for (int i = 0; i < note->genre_count; i++) free(note->genres[i]);
    for (int i = 0; i < note->label_count; i++) free(note->labels[i]);
    for (int i = 0; i < note->associated_count; i++) free(note->associated[i]);
    free(note->aliases); free(note->genres); free(note->labels); free(note->associated);
}
