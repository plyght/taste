#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    puts("taste [--db graph.sqlite] command ...");
    puts("commands: recommend, explain, graph inspect, feedback, vault import|export|validate|build-db, pack add|validate, graph build, import wikipedia|wikidata, expand");
    puts("importer examples: bun importers/wikidata.ts --artist \"Cocteau Twins\" --out vault && taste vault import vault");
}

static int has_arg(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) if (strcmp(argv[i], flag) == 0) return i;
    return 0;
}

int main(int argc, char **argv) {
    const char *db_path = "graph.sqlite";
    int i = 1;
    if (argc > 2 && strcmp(argv[i], "--db") == 0) {
        db_path = argv[i + 1];
        i += 2;
    }
    if (i >= argc) { usage(); return 1; }
    TasteDB tdb = {0};
    if (db_open(&tdb, db_path)) return 1;
    int rc = 0;
    if (strcmp(argv[i], "recommend") == 0) {
        char *seeds[16];
        int seed_count = 0, limit = 10;
        const char *mode = "safe";
        bool json = has_arg(argc, argv, "--json");
        for (int j = i + 1; j < argc; j++) {
            if (strcmp(argv[j], "--mode") == 0 && j + 1 < argc) mode = argv[++j];
            else if (strcmp(argv[j], "--limit") == 0 && j + 1 < argc) limit = atoi(argv[++j]);
            else if (strcmp(argv[j], "--json") == 0) {}
            else seeds[seed_count++] = argv[j];
        }
        rc = recommend_artists(&tdb, seeds, seed_count, mode, limit, json);
    } else if (strcmp(argv[i], "explain") == 0 && i + 2 < argc) {
        rc = explain_edge(&tdb, argv[i + 1], argv[i + 2], has_arg(argc, argv, "--json"));
    } else if (strcmp(argv[i], "graph") == 0 && i + 1 < argc && strcmp(argv[i + 1], "inspect") == 0 && i + 2 < argc) {
        rc = inspect_graph(&tdb, argv[i + 2], has_arg(argc, argv, "--json"));
    } else if (strcmp(argv[i], "graph") == 0 && i + 1 < argc && strcmp(argv[i + 1], "build") == 0) {
        puts("graph is built during vault and pack imports");
    } else if (strcmp(argv[i], "feedback") == 0 && i + 3 < argc) {
        const char *rating = "unknown";
        int r = has_arg(argc, argv, "--rating");
        if (r && r + 1 < argc) rating = argv[r + 1];
        rc = db_record_feedback(&tdb, argv[i + 1], argv[i + 2], rating);
    } else if (strcmp(argv[i], "vault") == 0 && i + 2 < argc) {
        if (strcmp(argv[i + 1], "import") == 0 || strcmp(argv[i + 1], "build-db") == 0) rc = vault_import_path(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "export") == 0) rc = vault_export(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "validate") == 0) rc = vault_validate_path(argv[i + 2]);
        else usage();
    } else if (strcmp(argv[i], "pack") == 0 && i + 2 < argc) {
        if (strcmp(argv[i + 1], "add") == 0) rc = vault_import_path(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "validate") == 0) rc = vault_validate_path(argv[i + 2]);
        else usage();
    } else if (strcmp(argv[i], "import") == 0 && i + 1 < argc) {
        puts("network importers are not included in milestone 1; use vault import or pack add");
    } else if (strcmp(argv[i], "expand") == 0) {
        puts("lazy network expansion is not included in milestone 1; use pack add for local expansion");
    } else {
        usage();
        rc = 1;
    }
    db_close(&tdb);
    return rc;
}
