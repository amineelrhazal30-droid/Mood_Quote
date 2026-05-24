/*
 * test_main.c — Unit tests for Mood Tracker core functions.
 *
 * Uses a minimal hand-rolled test framework (no external dependencies).
 * Tests are grouped by function.  The test binary exits with code 0 if
 * all tests pass, 1 if any fail.
 *
 * Compile:
 *   make test           (via Makefile)
 *   ./mood_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "core.h"

/* ── Tiny test framework ──────────────────────────────────────────── */

static int g_passed = 0;
static int g_failed = 0;

/* Variant for stack char arrays (avoids -Waddress on literal arrays) */
#define ASSERT_EQ_STR(actual, expected, label)                       \
    do {                                                             \
        const char *_a = (actual);                                   \
        if (_a && strcmp(_a, (expected)) == 0) {                     \
            printf("  [PASS] %s\n", (label));                       \
            g_passed++;                                             \
        } else {                                                     \
            printf("  [FAIL] %s\n"                                  \
                   "         expected: \"%s\"\n"                    \
                   "         got:      \"%s\"\n",                   \
                   (label), (expected), _a ? _a : "(null)");        \
            g_failed++;                                             \
        }                                                            \
    } while (0)

#define ASSERT_NOT_NULL(val, label)                                  \
    do {                                                             \
        if ((val) != NULL) {                                         \
            printf("  [PASS] %s\n", (label));                       \
            g_passed++;                                             \
        } else {                                                     \
            printf("  [FAIL] %s — expected non-NULL\n", (label));   \
            g_failed++;                                             \
        }                                                            \
    } while (0)

#define ASSERT_NULL(val, label)                                      \
    do {                                                             \
        if ((val) == NULL) {                                         \
            printf("  [PASS] %s\n", (label));                       \
            g_passed++;                                             \
        } else {                                                     \
            printf("  [FAIL] %s — expected NULL, got \"%s\"\n",     \
                   (label), (char *)(val));                          \
            g_failed++;                                             \
        }                                                            \
    } while (0)

#define ASSERT_EQ_INT(actual, expected, label)                       \
    do {                                                             \
        if ((actual) == (expected)) {                                \
            printf("  [PASS] %s\n", (label));                       \
            g_passed++;                                             \
        } else {                                                     \
            printf("  [FAIL] %s — expected %d, got %d\n",           \
                   (label), (expected), (actual));                   \
            g_failed++;                                             \
        }                                                            \
    } while (0)

/* ── Test: sanitize_string ────────────────────────────────────────── */
static void test_sanitize(void)
{
    printf("\n=== sanitize_string ===\n");

    /* Single quotes become backticks */
    char s1[] = "it's fine";
    sanitize_string(s1);
    ASSERT_EQ_STR(s1, "it`s fine", "single quote replaced");

    /* Double quotes become backticks */
    char s2[] = "say \"hello\"";
    sanitize_string(s2);
    ASSERT_EQ_STR(s2, "say `hello`", "double quotes replaced");

    /* Mixed: both types */
    char s3[] = "it's a \"test\"";
    sanitize_string(s3);
    ASSERT_EQ_STR(s3, "it`s a `test`", "mixed quotes replaced");

    /* No quotes: string unchanged */
    char s4[] = "perfectly safe string";
    sanitize_string(s4);
    ASSERT_EQ_STR(s4, "perfectly safe string", "no-op on clean string");

    /* Empty string: no crash */
    char s5[] = "";
    sanitize_string(s5);
    ASSERT_EQ_STR(s5, "", "empty string no crash");

    /* NULL: no crash */
    sanitize_string(NULL);
    printf("  [PASS] NULL pointer no crash\n");
    g_passed++;
}

/* ── Test: build_gemini_payload ───────────────────────────────────── */
static void test_build_payload(void)
{
    printf("\n=== build_gemini_payload ===\n");

    /* Normal mood produces a non-empty payload */
    char buf[1024];
    int rc = build_gemini_payload(buf, sizeof(buf), "happy");
    ASSERT_EQ_INT(rc, 0, "returns 0 on success");

    /* Payload contains the mood word */
    int contains_mood = (strstr(buf, "happy") != NULL);
    ASSERT_EQ_INT(contains_mood, 1, "payload contains mood word");

    /* Payload is valid JSON start */
    int starts_json = (buf[0] == '{');
    ASSERT_EQ_INT(starts_json, 1, "payload starts with '{'");

    /* Buffer too small returns -1 */
    char tiny[10];
    rc = build_gemini_payload(tiny, sizeof(tiny), "sad");
    ASSERT_EQ_INT(rc, -1, "returns -1 when buffer too small");

    /* NULL mood returns -1 */
    rc = build_gemini_payload(buf, sizeof(buf), NULL);
    ASSERT_EQ_INT(rc, -1, "returns -1 for NULL mood");
}

/* ── Test: parse_gemini_quote ─────────────────────────────────────── */
static void test_parse_gemini(void)
{
    printf("\n=== parse_gemini_quote ===\n");

    /* Well-formed Gemini response */
    const char *good_resp =
        "{"
        "  \"candidates\": [{"
        "    \"content\": {"
        "      \"parts\": [{"
        "        \"text\": \"{\\\"quote\\\": \\\"Keep going, you can do it.\\\"}\""
        "      }]"
        "    }"
        "  }]"
        "}";

    char *q = parse_gemini_quote(good_resp);
    ASSERT_NOT_NULL(q, "returns non-NULL for valid response");
    if (q) {
        ASSERT_EQ_STR(q, "Keep going, you can do it.", "correct quote extracted");
        free(q);
    }

    /* Missing candidates field */
    const char *bad_resp = "{\"error\": \"quota exceeded\"}";
    q = parse_gemini_quote(bad_resp);
    ASSERT_NULL(q, "returns NULL when candidates missing");
    free(q);

    /* Completely invalid JSON */
    q = parse_gemini_quote("not json at all !!!");
    ASSERT_NULL(q, "returns NULL for invalid JSON");
    free(q);

    /* NULL input */
    q = parse_gemini_quote(NULL);
    ASSERT_NULL(q, "returns NULL for NULL input");
    free(q);
}

/* ── Test: save_locally (SQLite round-trip) ───────────────────────── */
static void test_save_locally(void)
{
    printf("\n=== save_locally ===\n");

    const char *test_db = "/tmp/test_moods_tracker.db";
    /* Remove stale test DB */
    remove(test_db);

    /* Insert a row */
    int rc = save_locally(test_db, "happy", "Keep smiling!");
    ASSERT_EQ_INT(rc, 0, "insert returns 0 on success");

    /* Insert another row */
    rc = save_locally(test_db, "tired", "Rest and recharge.");
    ASSERT_EQ_INT(rc, 0, "second insert returns 0");

    /* Verify by reopening */
    sqlite3 *db;
    rc = sqlite3_open(test_db, &db);
    ASSERT_EQ_INT(rc, SQLITE_OK, "DB file readable after save");

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM moods;", -1, &stmt, NULL);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    ASSERT_EQ_INT(count, 2, "two rows stored in DB");
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* Verify mood content */
    sqlite3_open(test_db, &db);
    sqlite3_prepare_v2(db, "SELECT mood, quote FROM moods WHERE mood='happy';",
                       -1, &stmt, NULL);
    sqlite3_step(stmt);
    const char *mood  = (const char *)sqlite3_column_text(stmt, 0);
    const char *quote = (const char *)sqlite3_column_text(stmt, 1);
    ASSERT_EQ_STR(mood,  "happy",         "mood stored correctly");
    ASSERT_EQ_STR(quote, "Keep smiling!", "quote stored correctly");
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* Cleanup */
    remove(test_db);
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════╗\n");
    printf("║   Mood Tracker — Unit Tests      ║\n");
    printf("╚══════════════════════════════════╝\n");

    test_sanitize();
    test_build_payload();
    test_parse_gemini();
    test_save_locally();

    printf("\n──────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("──────────────────────────────────\n");

    return (g_failed == 0) ? 0 : 1;
}
