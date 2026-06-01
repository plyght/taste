#ifndef TASTE_H
#define TASTE_H

#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    sqlite3 *db;
    const char *path;
} TasteDB;

typedef struct {
    char *name;
    char *slug;
    char *wikidata;
    char *musicbrainz;
    char **aliases;
    int alias_count;
    char **genres;
    int genre_count;
    char **labels;
    int label_count;
    char **associated;
    int associated_count;
} ArtistNote;

typedef struct {
    char *artist;
    double score;
    char *explanation;
} Recommendation;

int db_open(TasteDB *tdb, const char *path);
void db_close(TasteDB *tdb);
int db_init(TasteDB *tdb);
int db_upsert_artist(TasteDB *tdb, ArtistNote *note, const char *source);
int db_add_edge(TasteDB *tdb, const char *source, const char *target, const char *rel, double weight, const char *evidence, const char *provenance);
int db_record_feedback(TasteDB *tdb, const char *seed_key, const char *artist, const char *rating);
int db_artist_id(TasteDB *tdb, const char *name);
char *slugify(const char *s);
char *strdup_safe(const char *s);
void free_artist_note(ArtistNote *note);
int vault_import_path(TasteDB *tdb, const char *path);
int vault_validate_path(const char *path);
int vault_export(TasteDB *tdb, const char *path);
int recommend_artists(TasteDB *tdb, char **seeds, int seed_count, const char *mode, int limit, bool json);
int explain_edge(TasteDB *tdb, const char *source, const char *target, bool json);
int inspect_graph(TasteDB *tdb, const char *artist, bool json);
int edge_candidates(TasteDB *tdb, int limit, bool json);
int edge_review_file(TasteDB *tdb, const char *path);
int serve_web(TasteDB *tdb, const char *host, int port);

#endif
