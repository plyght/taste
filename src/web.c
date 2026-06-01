#define _POSIX_C_SOURCE 200809L
#include "taste.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void send_all(int fd, const char *body) {
    size_t n = strlen(body);
    while (n) {
        ssize_t w = send(fd, body, n, 0);
        if (w <= 0) return;
        body += w;
        n -= (size_t)w;
    }
}

static void html_escape(FILE *f, const char *s) {
    for (; s && *s; s++) {
        if (*s == '&') fputs("&amp;", f);
        else if (*s == '<') fputs("&lt;", f);
        else if (*s == '>') fputs("&gt;", f);
        else if (*s == '"') fputs("&quot;", f);
        else fputc(*s, f);
    }
}

static void url_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static char *param(char *query, const char *key) {
    size_t klen = strlen(key);
    char *p = query;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char *v = p + klen + 1;
            char *end = strchr(v, '&');
            if (end) *end = 0;
            url_decode(v);
            return v;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return "";
}

static char *recommend_html(TasteDB *tdb, const char *a, const char *b, const char *c, const char *mode) {
    char seed_ids[256] = "0";
    const char *seeds[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        int id = db_artist_id(tdb, seeds[i]);
        char tmp[32];
        snprintf(tmp, sizeof(tmp), ",%d", id);
        strncat(seed_ids, tmp, sizeof(seed_ids) - strlen(seed_ids) - 1);
    }
    sqlite3_stmt *st;
    char sql[1024];
    snprintf(sql, sizeof(sql), "SELECT ar.name,sum(e.weight)+count(DISTINCT e.source_id)*0.75,COALESCE(group_concat(ee.evidence,'; '),'') FROM edges e JOIN artists ar ON ar.id=e.target_id LEFT JOIN edge_evidence ee ON ee.edge_id=e.id WHERE e.source_id IN(%s) AND e.target_id NOT IN(%s) GROUP BY e.target_id ORDER BY 2 DESC LIMIT 10", seed_ids, seed_ids);
    sqlite3_prepare_v2(tdb->db, sql, -1, &st, NULL);
    FILE *f = tmpfile();
    fputs("<section class='results'>", f);
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        double score = sqlite3_column_double(st, 1);
        const char *ev = (const char *)sqlite3_column_text(st, 2);
        if (n == 0) {
            fputs("<article class='hero'><p class='eyebrow'>Recommended artist</p><h2>", f);
            html_escape(f, name);
            fprintf(f, "</h2><p>Score %.2f using %s mode.</p><details><summary>Why?</summary><p>", score, mode && *mode ? mode : "safe");
            html_escape(f, ev);
            fputs("</p></details><form method='POST' action='/feedback'><input type='hidden' name='artist' value='", f);
            html_escape(f, name);
            fputs("'><button name='rating' value='good'>yes</button><button name='rating' value='bad'>no</button><button name='rating' value='unknown'>I do not know</button><button name='rating' value='too_obvious'>too obvious</button><button name='rating' value='too_far'>too far</button><button name='rating' value='already_know'>already know it</button></form></article>", f);
        } else {
            fputs("<li><strong>", f);
            html_escape(f, name);
            fprintf(f, "</strong> %.2f <span>", score);
            html_escape(f, ev);
            fputs("</span></li>", f);
        }
        n++;
    }
    if (n > 1) fputs("<h3>Alternates</h3></section>", f);
    if (!n) fputs("<p>No recommendations yet. Import a pack or expand artists first.</p></section>", f);
    sqlite3_finalize(st);
    long len;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *out = calloc((size_t)len + 1, 1);
    fread(out, 1, (size_t)len, f);
    fclose(f);
    return out;
}

static char *page(TasteDB *tdb, char *query) {
    char *a = param(query, "a");
    char *b = param(query, "b");
    char *c = param(query, "c");
    char *mode = param(query, "mode");
    FILE *f = tmpfile();
    fputs("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Taste</title><style>body{font-family:ui-sans-serif,system-ui;margin:0;background:#f6f1e8;color:#17130f}main{max-width:880px;margin:0 auto;padding:56px 24px}.card,.hero{background:#fffaf0;border:1px solid #251a1022;border-radius:24px;padding:24px;box-shadow:0 18px 50px #27140012}h1{font-size:64px;letter-spacing:-.06em;margin:0 0 8px}h2{font-size:48px;letter-spacing:-.05em;margin:0}input,select,button{font:inherit;border-radius:999px;border:1px solid #251a1033;padding:12px 16px;background:white}button{background:#17130f;color:white;cursor:pointer}form.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.eyebrow{text-transform:uppercase;letter-spacing:.16em;font-size:12px;color:#7a4b2b}.results{margin-top:24px}li{margin:12px 0}span{color:#6b6258;display:block}@media(max-width:760px){form.grid{grid-template-columns:1fr}h1{font-size:44px}}</style></head><body><main><h1>Taste</h1><p>Enter three artists. Get one graph-backed recommendation.</p><section class='card'><form class='grid' method='GET'><input name='a' placeholder='Cocteau Twins' value='", f);
    html_escape(f, a);
    fputs("'><input name='b' placeholder='Slowdive' value='", f);
    html_escape(f, b);
    fputs("'><input name='c' placeholder='My Bloody Valentine' value='", f);
    html_escape(f, c);
    fputs("'><select name='mode'><option>safe</option><option>adjacent</option><option>deep-cut</option><option>influence</option><option>descendant</option></select><button>Recommend</button></form></section>", f);
    if (*a && *b && *c) {
        char *rec = recommend_html(tdb, a, b, c, mode);
        fputs(rec, f);
        free(rec);
    }
    fputs("</main></body></html>", f);
    long len;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *out = calloc((size_t)len + 1, 1);
    fread(out, 1, (size_t)len, f);
    fclose(f);
    return out;
}

static char *feedback_response(TasteDB *tdb, char *body) {
    char *artist = param(body, "artist");
    char *rating = param(body, "rating");
    db_record_feedback(tdb, "web", artist, rating);
    return strdup_safe("HTTP/1.1 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n");
}

int serve_web(TasteDB *tdb, const char *host, int port) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) return 1;
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host ? host : "127.0.0.1");
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(server, 16) < 0) {
        perror("serve");
        close(server);
        return 1;
    }
    printf("tasted listening on http://%s:%d\n", host ? host : "127.0.0.1", port);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        char req[16384] = {0};
        ssize_t n = recv(client, req, sizeof(req) - 1, 0);
        if (n > 0) {
            char *response = NULL;
            if (strncmp(req, "POST /feedback", 14) == 0) {
                char *body = strstr(req, "\r\n\r\n");
                response = feedback_response(tdb, body ? body + 4 : "");
            } else {
                char *path = strchr(req, ' ');
                char *query = "";
                if (path) {
                    path++;
                    char *end = strchr(path, ' ');
                    if (end) *end = 0;
                    query = strchr(path, '?');
                    if (query) *query++ = 0;
                    else query = "";
                }
                response = page(tdb, query);
            }
            send_all(client, response);
            free(response);
        }
        close(client);
    }
    close(server);
    return 0;
}
