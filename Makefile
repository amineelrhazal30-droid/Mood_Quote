CC      = gcc
CFLAGS  = -Wall -Wextra -I. -Wno-implicit-fallthrough
TARGET  = mood_tracker
TEST    = mood_test

# ── Platform detection ────────────────────────────────────────────────
UNAME := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(UNAME), Windows_NT)
    TARGET  := mood_tracker.exe
    TEST    := mood_test.exe
    CFLAGS  += -DCURL_STATICLIB
    LIBS     = -lcurl -lws2_32 -lwldap32 -lcrypt32 -lnormaliz
    CLEAN_CMD = del /f /q
else
    LIBS     = -lcurl
    CLEAN_CMD = rm -f
endif

SRCS_CORE  = core.c cJSON.c sqlite3.c
SRCS_MAIN  = main.c $(SRCS_CORE)
SRCS_TEST  = test_main.c $(SRCS_CORE)

# ── Default target: build the app ─────────────────────────────────────
all: $(TARGET)

$(TARGET): $(SRCS_MAIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "[+] Build OK -> $(TARGET)"

# ── Test target: compile and run unit tests ───────────────────────────
$(TEST): $(SRCS_TEST)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test: $(TEST)
	./$(TEST)

# ── Clean build artifacts ─────────────────────────────────────────────
clean:
	$(CLEAN_CMD) $(TARGET) $(TEST) response.txt supa_log.txt 2>/dev/null || true

.PHONY: all test clean
