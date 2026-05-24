# 🎯 Mood Tracker — Daily Mood & AI Quote Generator

> A C-based CLI app that captures your daily mood, fetches a personalized motivational quote via the **Gemini AI API**, saves it locally in **SQLite**, and syncs it to **Supabase** (cloud).

---

## 👥 Team Members

| Name | Role | LinkedIn |
|------|------|----------|
| Prénom Nom 1 | Backend C / SQLite | [linkedin.com/in/...](https://linkedin.com/in/) |
| Prénom Nom 2 | API Integration / Gemini | [linkedin.com/in/...](https://linkedin.com/in/) |
| Prénom Nom 3 | Supabase / Deployment | [linkedin.com/in/...](https://linkedin.com/in/) |

> _Groupe X — ESISA 2025–2026_

---

## 🏗️ Architecture

```
┌─────────────┐      stdin       ┌──────────────┐
│    User     │ ──── mood ────► │    main.c    │
└─────────────┘                  └──────┬───────┘
                                        │
                     ┌──────────────────┼──────────────────┐
                     ▼                  ▼                   ▼
             ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
             │  Gemini API  │  │  SQLite DB   │  │  Supabase    │
             │  (REST/JSON) │  │  (local)     │  │  (cloud)     │
             └──────┬───────┘  └──────────────┘  └──────────────┘
                    │
             ┌──────▼───────┐
             │  cJSON       │  JSON parsing library (bundled)
             └──────────────┘
```

**Key modules:**

| File | Role |
|------|------|
| `main.c` | Entry point: reads env vars, user input, orchestrates calls |
| `core.c` / `core.h` | Testable helpers: sanitize, payload builder, JSON parser, SQLite, cloud sync |
| `test_main.c` | Unit tests (runs without API keys) |
| `cJSON.c/.h` | Bundled JSON parser |
| `sqlite3.c/.h` | Bundled SQLite engine |

---

## ⚙️ Prerequisites

**Linux / macOS**
```bash
sudo apt-get install libcurl4-openssl-dev   # Debian/Ubuntu
brew install curl                            # macOS
```

**Windows (MinGW)**
- Requires MinGW-w64 with static libcurl

---

## 🔐 Configuration

Copy the example env file and fill in your keys:

```bash
cp .env.example .env
# Edit .env with your actual keys
```

Then load the variables before running:

```bash
# Linux / macOS
export $(grep -v '^#' .env | xargs)

# Windows PowerShell
Get-Content .env | ForEach-Object { if ($_ -notmatch '^#') { $k,$v = $_ -split '=',2; [System.Environment]::SetEnvironmentVariable($k,$v) } }
```

**Required variables:**

| Variable | Description |
|----------|-------------|
| `GEMINI_API_KEY` | Google Gemini API key (get at aistudio.google.com) |
| `SUPABASE_URL` | Your Supabase table endpoint |
| `SUPABASE_ANON_KEY` | Your Supabase anon key |

---

## 🔨 Build & Run

```bash
# Build the app
make

# Run
./mood_tracker         # Linux/macOS
mood_tracker.exe       # Windows
```

---

## ✅ Tests

Unit tests run **without any API key** — they test sanitization, JSON parsing, payload building, and SQLite round-trips locally.

```bash
make test
```

Expected output:
```
╔══════════════════════════════════╗
║   Mood Tracker — Unit Tests      ║
╚══════════════════════════════════╝

=== sanitize_string ===
  [PASS] single quote replaced
  [PASS] double quotes replaced
  ...

Results: 17 passed, 0 failed
```

---

## 🚀 Deployment

This is a desktop CLI app, distributed via **Itch.io**.

**Live demo:** [your-username.itch.io/mood-tracker](https://itch.io) ← _replace with real link_

To publish a new release:
1. `make` on the target OS
2. Zip the binary + `README.md`
3. Upload the zip to your Itch.io project page

---

## 📁 Project Structure

```
Mood_Quote/
├── main.c                   Entry point
├── core.c                   Business logic (testable)
├── core.h                   Shared declarations
├── test_main.c              Unit tests
├── cJSON.c / cJSON.h        JSON parser (bundled)
├── sqlite3.c / sqlite3.h    SQLite engine (bundled)
├── Makefile                 Cross-platform build
├── .env.example             Environment variable template
├── .gitignore               Protects secrets and build output
├── .github/
│   └── workflows/
│       └── ci.yml           GitHub Actions CI pipeline
└── docs/
    └── ARCHITECTURE.md      Full technical documentation
```

---

## 📄 License

Educational project — ESISA 2025–2026.  
Instructor: Prof. Chafik Boulealam (ch.boulealam@esisa.ac.ma)
