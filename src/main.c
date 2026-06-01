#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static void usage(void) {
    puts("taste [--db graph.sqlite] command ...");
    puts("commands: recommend, explain, graph inspect, feedback, vault import|export|validate|build-db, pack add|validate, graph build, import wikipedia|wikidata, expand, edges candidates|evidence|review, serve");
    puts("importer examples: bun importers/wikidata.ts --artist \"Cocteau Twins\" --out vault && taste vault import vault");
}

static int has_arg(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) if (strcmp(argv[i], flag) == 0) return i;
    return 0;
}

static const char *arg_value(int argc, char **argv, const char *flag) {
    int index = has_arg(argc, argv, flag);
    if (!index || index + 1 >= argc) return NULL;
    return argv[index + 1];
}

static char *shell_quote(const char *value) {
    size_t extra = 3;
    for (const char *p = value; *p; p++) extra += *p == '\'' ? 4 : 1;
    char *out = calloc(extra, 1);
    char *w = out;
    *w++ = '\'';
    for (const char *p = value; *p; p++) {
        if (*p == '\'') {
            memcpy(w, "'\\''", 4);
            w += 4;
        } else {
            *w++ = *p;
        }
    }
    *w++ = '\'';
    *w = 0;
    return out;
}

static int run_importer(TasteDB *tdb, const char *source, const char *artist, const char *out) {
    char *qsource = shell_quote(source);
    char *qartist = shell_quote(artist);
    char *qout = shell_quote(out);
    size_t len = strlen(qsource) + strlen(qartist) + strlen(qout) + 128;
    char *cmd = calloc(len, 1);
    snprintf(cmd, len, "bun importers/%s.ts --artist %s --out %s", source, qartist, qout);
    int status = system(cmd);
    int rc = status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    if (!rc) rc = vault_import_path(tdb, out);
    free(qsource);
    free(qartist);
    free(qout);
    free(cmd);
    return rc;
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
    } else if (strcmp(argv[i], "edges") == 0 && i + 1 < argc) {
        if (strcmp(argv[i + 1], "candidates") == 0) {
            int limit = 50;
            const char *limit_arg = arg_value(argc, argv, "--limit");
            if (limit_arg) limit = atoi(limit_arg);
            rc = edge_candidates(&tdb, limit, has_arg(argc, argv, "--json"));
        } else if (strcmp(argv[i + 1], "evidence") == 0) {
            const char *source = arg_value(argc, argv, "--source");
            const char *target = arg_value(argc, argv, "--target");
            if (!source || !target) {
                fprintf(stderr, "missing --source or --target\n");
                rc = 1;
            } else {
                rc = explain_edge(&tdb, source, target, has_arg(argc, argv, "--json"));
            }
        } else if (strcmp(argv[i + 1], "review") == 0) {
            const char *file = arg_value(argc, argv, "--file");
            if (!file) {
                fprintf(stderr, "missing --file\n");
                rc = 1;
            } else {
                rc = edge_review_file(&tdb, file);
            }
        } else {
            usage();
            rc = 1;
        }
    } else if (strcmp(argv[i], "feedback") == 0 && i + 3 < argc) {
        const char *rating = "unknown";
        int r = has_arg(argc, argv, "--rating");
        if (r && r + 1 < argc) rating = argv[r + 1];
        rc = db_record_feedback(&tdb, argv[i + 1], argv[i + 2], rating);
    } else if (strcmp(argv[i], "serve") == 0) {
        const char *host = arg_value(argc, argv, "--host");
        const char *port_arg = arg_value(argc, argv, "--port");
        rc = serve_web(&tdb, host ? host : "127.0.0.1", port_arg ? atoi(port_arg) : 8765);
    } else if (strcmp(argv[i], "vault") == 0 && i + 2 < argc) {
        if (strcmp(argv[i + 1], "import") == 0 || strcmp(argv[i + 1], "build-db") == 0) rc = vault_import_path(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "export") == 0) rc = vault_export(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "validate") == 0) rc = vault_validate_path(argv[i + 2]);
        else if (strcmp(argv[i + 1], "diff") == 0) rc = vault_diff(&tdb, argv[i + 2]);
        else usage();
    } else if (strcmp(argv[i], "pack") == 0 && i + 2 < argc) {
        if (strcmp(argv[i + 1], "add") == 0) rc = vault_import_path(&tdb, argv[i + 2]);
        else if (strcmp(argv[i + 1], "validate") == 0) rc = vault_validate_path(argv[i + 2]);
        else usage();
    } else if (strcmp(argv[i], "import") == 0 && i + 1 < argc) {
        const char *artist = arg_value(argc, argv, "--artist");
        const char *out = arg_value(argc, argv, "--out");
        if (!out) out = "vault";
        if (!artist) {
            fprintf(stderr, "missing --artist\n");
            rc = 1;
        } else if (strcmp(argv[i + 1], "wikidata") == 0 || strcmp(argv[i + 1], "wikipedia") == 0) {
            rc = run_importer(&tdb, argv[i + 1], artist, out);
        } else {
            usage();
            rc = 1;
        }
    } else if (strcmp(argv[i], "expand") == 0 && i + 1 < argc) {
        const char *out = arg_value(argc, argv, "--out");
        if (!out) out = "vault";
        rc = run_importer(&tdb, "wikidata", argv[i + 1], out);
        if (!rc) rc = run_importer(&tdb, "wikipedia", argv[i + 1], out);
    } else {
        usage();
        rc = 1;
    }
    db_close(&tdb);
    return rc;
}
