#ifndef CORE_H
#define CORE_H

/*
 * core.h — Shared declarations for Mood Tracker helper functions.
 * Separating core logic from main() makes unit testing straightforward.
 */

#include <stddef.h>  /* size_t */

/* ── String utilities ─────────────────────────────────────────────── */

/**
 * sanitize_string: replace ' and " characters with ` in-place.
 * Prevents SQL injection and JSON breakage.
 */
void sanitize_string(char *s);

/* ── Payload builder ──────────────────────────────────────────────── */

/**
 * build_gemini_payload: write Gemini REST JSON body into buf.
 * Returns 0 on success, -1 if buf is too small.
 */
int build_gemini_payload(char *buf, size_t buf_size, const char *mood);

/* ── Gemini response parser ───────────────────────────────────────── */

/**
 * parse_gemini_quote: extract the quote string from a raw Gemini
 * JSON response buffer.  Returns a malloc'd string the caller must
 * free(), or NULL on failure.
 */
char *parse_gemini_quote(const char *response_buf);

/* ── SQLite persistence ───────────────────────────────────────────── */

/**
 * save_locally: open moods.db (path), create the table if needed,
 * and insert one row.  Returns 0 on success, non-zero on error.
 */
int save_locally(const char *db_path, const char *mood, const char *quote);

/* ── Cloud sync ───────────────────────────────────────────────────── */

/**
 * sync_cloud: POST mood+quote to Supabase.
 * supabase_url and supabase_key are read from the environment if NULL
 * is passed (normal operation).  Pass explicit values in tests.
 * Returns 0 on success, non-zero on curl error.
 */
int sync_cloud(const char *mood, const char *quote,
               const char *supabase_url, const char *supabase_key);

#endif /* CORE_H */
