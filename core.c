/*
 * core.c — Business logic for the Mood Tracker.
 *
 * All functions here are independently testable; main() lives in main.c.
 *
 * Compile alongside main.c (production) or test_main.c (unit tests).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "sqlite3.h"
#include "cJSON.h"
#include "core.h"

/* ─────────────────────────────────────────────────────────────────────
 *  Internal libcurl write callback: accumulate response into a buffer.
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    char  *data;
    size_t len;
} ResponseBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    ResponseBuf *buf = (ResponseBuf *)userdata;

    char *tmp = realloc(buf->data, buf->len + bytes + 1);
    if (!tmp) return 0;          /* signal curl that we failed */

    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

/* ─────────────────────────────────────────────────────────────────────
 *  sanitize_string
 * ───────────────────────────────────────────────────────────────────── */
void sanitize_string(char *s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\'' || s[i] == '"') {
            s[i] = '`';
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────
 *  build_gemini_payload
 * ───────────────────────────────────────────────────────────────────── */
int build_gemini_payload(char *buf, size_t buf_size, const char *mood)
{
    if (!buf || !mood) return -1;

    int n = snprintf(buf, buf_size,
        "{\"contents\":[{\"parts\":[{\"text\": "
        "\"I feel %s. Give me ONE motivational quote, max 20 words. "
        "Reply ONLY with this JSON: {\\\"quote\\\": \\\"your quote here\\\"}"
        "\"}]}]}",
        mood);

    /* snprintf returns negative on encoding error, or >= buf_size if truncated */
    if (n < 0 || (size_t)n >= buf_size) return -1;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 *  parse_gemini_quote
 * ───────────────────────────────────────────────────────────────────── */
char *parse_gemini_quote(const char *response_buf)
{
    if (!response_buf) return NULL;

    cJSON *root = cJSON_Parse(response_buf);
    if (!root) {
        fprintf(stderr, "[!] Failed to parse Gemini JSON response.\n");
        return NULL;
    }

    char *result = NULL;

    cJSON *candidates  = cJSON_GetObjectItem(root, "candidates");
    cJSON *candidate   = cJSON_GetArrayItem(candidates, 0);
    cJSON *content     = cJSON_GetObjectItem(candidate, "content");
    cJSON *parts       = cJSON_GetObjectItem(content, "parts");
    cJSON *part        = cJSON_GetArrayItem(parts, 0);
    cJSON *text_item   = cJSON_GetObjectItem(part, "text");

    if (text_item && text_item->valuestring) {
        /* The model should reply with a JSON object like {"quote": "..."} */
        const char *json_start = strchr(text_item->valuestring, '{');
        if (json_start) {
            cJSON *inner = cJSON_Parse(json_start);
            if (inner) {
                cJSON *quote_item = cJSON_GetObjectItem(inner, "quote");
                if (quote_item && quote_item->valuestring) {
                    result = strdup(quote_item->valuestring);
                }
                cJSON_Delete(inner);
            }
        }
    }

    cJSON_Delete(root);

    if (!result) {
        fprintf(stderr, "[!] Could not extract 'quote' field from AI response.\n");
    }
    return result;
}

/* ─────────────────────────────────────────────────────────────────────
 *  save_locally
 * ───────────────────────────────────────────────────────────────────── */
int save_locally(const char *db_path, const char *mood, const char *quote)
{
    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[!] Cannot open database '%s': %s\n",
                db_path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS moods ("
        "  id    INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date  DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  mood  TEXT NOT NULL,"
        "  quote TEXT NOT NULL"
        ");",
        NULL, NULL, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "[!] Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO moods (mood, quote) VALUES (?, ?);", -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "[!] Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    sqlite3_bind_text(stmt, 1, mood,  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, quote, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[!] Failed to insert row: %s\n", sqlite3_errmsg(db));
    } else {
        printf("[+] Saved to local SQLite database (%s)\n", db_path);
        rc = SQLITE_OK;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return (rc == SQLITE_DONE || rc == SQLITE_OK) ? 0 : rc;
}

/* ─────────────────────────────────────────────────────────────────────
 *  sync_cloud
 * ───────────────────────────────────────────────────────────────────── */
int sync_cloud(const char *mood, const char *quote,
               const char *supabase_url, const char *supabase_key)
{
    /* Fall back to environment variables when not provided explicitly */
    if (!supabase_url)  supabase_url  = getenv("SUPABASE_URL");
    if (!supabase_key)  supabase_key  = getenv("SUPABASE_ANON_KEY");

    if (!supabase_url || !supabase_key) {
        fprintf(stderr,
            "[!] Cloud sync skipped: SUPABASE_URL or SUPABASE_ANON_KEY "
            "not set in environment.\n");
        return -1;
    }

    char payload[1024];
    snprintf(payload, sizeof(payload),
             "{\"mood\":\"%s\",\"quote\":\"%s\"}", mood, quote);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    ResponseBuf resp = {NULL, 0};

    char apikey_header[512], auth_header[512];
    snprintf(apikey_header, sizeof(apikey_header), "apikey: %s",       supabase_key);
    snprintf(auth_header,   sizeof(auth_header),   "Authorization: Bearer %s", supabase_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, apikey_header);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL,           supabase_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &resp);
    /* SSL verification MUST remain enabled in production */

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        printf("[+] Synced to Supabase Cloud\n");
    } else {
        fprintf(stderr, "[!] Cloud sync failed (curl error %d: %s)\n",
                res, curl_easy_strerror(res));
    }

    free(resp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? 0 : (int)res;
}
