/*
 * =============================================
 *   MOOD TRACKER — main entry point
 * =============================================
 *
 * All business logic lives in core.c / core.h.
 * This file only handles user I/O and orchestration.
 *
 * REQUIRED environment variables (set in a .env file or export):
 *   GEMINI_API_KEY     — your Google Gemini API key
 *   SUPABASE_URL       — your Supabase table REST endpoint
 *   SUPABASE_ANON_KEY  — your Supabase anon/publishable key
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "core.h"

/* ── Internal: fetch Gemini quote over HTTP ───────────────────────── */

/* libcurl write callback: accumulate response into a heap buffer */
typedef struct { char *data; size_t len; } RespBuf;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *ud)
{
    size_t bytes = sz * nmemb;
    RespBuf *buf = (RespBuf *)ud;
    char *tmp = realloc(buf->data, buf->len + bytes + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

static char *fetch_gemini_quote(const char *mood, const char *api_key)
{
    char payload[1024];
    if (build_gemini_payload(payload, sizeof(payload), mood) != 0) {
        fprintf(stderr, "[!] Payload builder failed (mood too long?)\n");
        return NULL;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://generativelanguage.googleapis.com/v1beta/"
        "models/gemini-2.5-flash:generateContent?key=%s",
        api_key);

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    RespBuf resp = {NULL, 0};

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &resp);
    /* SSL verification is ENABLED (do not disable in production) */

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[!] Gemini request failed: %s\n",
                curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    char *quote = parse_gemini_quote(resp.data);
    free(resp.data);
    return quote;   /* caller must free() */
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(void)
{
    /* 1. Validate required environment variables upfront */
    const char *gemini_key = getenv("GEMINI_API_KEY");
    if (!gemini_key || gemini_key[0] == '\0') {
        fprintf(stderr,
            "[!] GEMINI_API_KEY is not set.\n"
            "    Export it or add it to your .env file.\n");
        return 1;
    }

    /* SUPABASE vars are checked lazily inside sync_cloud() */

    curl_global_init(CURL_GLOBAL_ALL);

    /* 2. Read mood from user */
    char mood[64];
    printf("============================\n");
    printf("   DAILY MOOD TRACKER       \n");
    printf("============================\n\n");
    printf("How are you feeling today? (one word)\n> ");
    fflush(stdout);

    if (fgets(mood, sizeof(mood), stdin) == NULL) {
        curl_global_cleanup();
        return 1;
    }
    mood[strcspn(mood, "\n")] = '\0';
    if (mood[0] == '\0') {
        fprintf(stderr, "[!] No mood entered.\n");
        curl_global_cleanup();
        return 1;
    }

    /* 3. Fetch AI quote */
    printf("[~] Asking AI for a quote...\n");
    char *quote = fetch_gemini_quote(mood, gemini_key);
    if (!quote) {
        curl_global_cleanup();
        return 1;
    }

    /* 4. Sanitize to avoid SQL/JSON injection */
    sanitize_string(quote);

    printf("\nYou feel : %s\n", mood);
    printf("Quote     : %s\n\n", quote);

    /* 5. Persist locally */
    save_locally("moods.db", mood, quote);

    /* 6. Sync to cloud (non-fatal if it fails) */
    sync_cloud(mood, quote, NULL, NULL);

    free(quote);
    curl_global_cleanup();
    return 0;
}
