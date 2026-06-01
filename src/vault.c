#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static void add_item(char ***arr, int *count, const char *v) {
    *arr = realloc(*arr, sizeof(char *) * (size_t)(*count + 1));
    (*arr)[*count] = strdup_safe(v);
    (*count)++;
}

static int parse_artist_file(const char *path, ArtistNote *note) {
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char line[2048];
    char section[64] = "";
    bool front = false;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (strcmp(s, "---") == 0) {
            front = !front;
            if (!front) break;
            continue;
        }
        if (!front) continue;
        if (strstr(s, ":") && s[strlen(s) - 1] != ':') {
            char *colon = strchr(s, ':');
            *colon = 0;
            char *key = trim(s), *value = trim(colon + 1);
            section[0] = 0;
            if (strcmp(key, "name") == 0) note->name = strdup_safe(value);
            else if (strcmp(key, "wikidata") == 0) note->wikidata = strdup_safe(value);
            else if (strcmp(key, "musicbrainz") == 0) note->musicbrainz = strdup_safe(value);
        } else if (strstr(s, ":") && s[strlen(s) - 1] == ':') {
            s[strlen(s) - 1] = 0;
            snprintf(section, sizeof(section), "%s", trim(s));
        } else if (strncmp(s, "- ", 2) == 0) {
            char *v = trim(s + 2);
            if (strcmp(section, "aliases") == 0) add_item(&note->aliases, &note->alias_count, v);
            else if (strcmp(section, "genres") == 0) add_item(&note->genres, &note->genre_count, v);
            else if (strcmp(section, "labels") == 0) add_item(&note->labels, &note->label_count, v);
            else if (strcmp(section, "associated") == 0) add_item(&note->associated, &note->associated_count, v);
        }
    }
    fclose(f);
    if (!note->name) return 1;
    note->slug = slugify(note->name);
    return 0;
}

static int import_artist_file(TasteDB *tdb, const char *path) {
    ArtistNote note = {0};
    if (parse_artist_file(path, &note)) return 1;
    int rc = db_upsert_artist(tdb, &note, path);
    free_artist_note(&note);
    return rc;
}

static int scan_artists(TasteDB *tdb, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return errno == ENOENT ? 0 : 1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        if (!strstr(ent->d_name, ".md")) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        rc |= import_artist_file(tdb, path);
    }
    closedir(d);
    return rc;
}

static void build_edges_from_facts(TasteDB *tdb) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT a.name,b.name,fa.kind,fa.value FROM artist_facts fa JOIN artist_facts fb ON fa.kind=fb.kind AND fa.value=fb.value AND fa.artist_id<fb.artist_id JOIN artists a ON a.id=fa.artist_id JOIN artists b ON b.id=fb.artist_id WHERE fa.kind IN('genre','label')", -1, &st, NULL);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *a = (const char *)sqlite3_column_text(st, 0), *b = (const char *)sqlite3_column_text(st, 1);
        const char *kind = (const char *)sqlite3_column_text(st, 2), *value = (const char *)sqlite3_column_text(st, 3);
        char ev[512];
        snprintf(ev, sizeof(ev), "shared %s: %s", kind, value);
        double w = strcmp(kind, "label") == 0 ? 1.4 : 1.0;
        db_add_edge(tdb, a, b, strcmp(kind, "label") == 0 ? "shared_label" : "shared_genre", w, ev, "vault");
        db_add_edge(tdb, b, a, strcmp(kind, "label") == 0 ? "shared_label" : "shared_genre", w, ev, "vault");
    }
    sqlite3_finalize(st);
    sqlite3_prepare_v2(tdb->db, "SELECT a.name,fa.value FROM artist_facts fa JOIN artists a ON a.id=fa.artist_id WHERE fa.kind='associated'", -1, &st, NULL);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *a = (const char *)sqlite3_column_text(st, 0), *b = (const char *)sqlite3_column_text(st, 1);
        if (db_artist_id(tdb, b)) {
            char ev[512];
            snprintf(ev, sizeof(ev), "associated act: %s", b);
            db_add_edge(tdb, a, b, "associated_act", 2.0, ev, "vault");
            db_add_edge(tdb, b, a, "associated_act", 2.0, ev, "vault");
        }
    }
    sqlite3_finalize(st);
}

int vault_import_path(TasteDB *tdb, const char *path) {
    char artists[4096];
    snprintf(artists, sizeof(artists), "%s/artists", path);
    int rc = scan_artists(tdb, artists);
    build_edges_from_facts(tdb);
    return rc;
}

int vault_validate_path(const char *path) {
    char artists[4096];
    snprintf(artists, sizeof(artists), "%s/artists", path);
    DIR *d = opendir(artists);
    if (!d) return 1;
    struct dirent *ent;
    int bad = 0, count = 0;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.' || !strstr(ent->d_name, ".md")) continue;
        char file[4096];
        snprintf(file, sizeof(file), "%s/%s", artists, ent->d_name);
        ArtistNote note = {0};
        if (parse_artist_file(file, &note)) {
            fprintf(stderr, "invalid artist note: %s\n", file);
            bad++;
        } else count++;
        free_artist_note(&note);
    }
    closedir(d);
    printf("validated %d artist notes\n", count);
    return bad ? 1 : 0;
}

int vault_diff(TasteDB *tdb, const char *path) {
    char artists[4096];
    snprintf(artists, sizeof(artists), "%s/artists", path);
    DIR *d = opendir(artists);
    if (!d) return 1;
    struct dirent *ent;
    int missing_db = 0, changed = 0, checked = 0;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.' || !strstr(ent->d_name, ".md")) continue;
        char file[4096];
        snprintf(file, sizeof(file), "%s/%s", artists, ent->d_name);
        ArtistNote note = {0};
        if (parse_artist_file(file, &note)) {
            fprintf(stderr, "invalid artist note: %s\n", file);
            changed++;
        } else {
            int id = db_artist_id(tdb, note.name);
            if (!id) {
                printf("missing-db artist %s\n", note.name);
                missing_db++;
            }
            checked++;
        }
        free_artist_note(&note);
    }
    closedir(d);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT name FROM artists ORDER BY name", -1, &st, NULL);
    int missing_vault = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        char *slug = slugify(name);
        char file[4096];
        snprintf(file, sizeof(file), "%s/%s.md", artists, slug);
        if (access(file, F_OK) != 0) {
            printf("missing-vault artist %s\n", name);
            missing_vault++;
        }
        free(slug);
    }
    sqlite3_finalize(st);
    printf("checked %d vault artists, missing-db %d, missing-vault %d, invalid %d\n", checked, missing_db, missing_vault, changed);
    return missing_db || missing_vault || changed ? 1 : 0;
}

int vault_export(TasteDB *tdb, const char *path) {
    char artists[4096];
    mkdir(path, 0755);
    snprintf(artists, sizeof(artists), "%s/artists", path);
    mkdir(artists, 0755);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(tdb->db, "SELECT name,slug,wikidata,musicbrainz FROM artists ORDER BY name", -1, &st, NULL);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0), *slug = (const char *)sqlite3_column_text(st, 1);
        const char *wikidata = (const char *)sqlite3_column_text(st, 2), *mb = (const char *)sqlite3_column_text(st, 3);
        char file[4096];
        snprintf(file, sizeof(file), "%s/%s.md", artists, slug);
        FILE *f = fopen(file, "w");
        if (!f) continue;
        fprintf(f, "---\ntype: artist\nname: %s\nwikidata: %s\nmusicbrainz: %s\naliases:\ngenres:\nlabels:\nassociated:\n---\n\n# %s\n", name, wikidata ? wikidata : "", mb ? mb : "", name);
        fclose(f);
    }
    sqlite3_finalize(st);
    return 0;
}
