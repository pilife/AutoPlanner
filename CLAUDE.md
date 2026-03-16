# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AutoPlanner is a productivity planner for knowledge workers. It manages tasks and generates daily/weekly plans. The architecture is a C++ REST API backend + React (Vite) frontend, with Microsoft OAuth authentication and per-user data isolation. AI-driven analytics planned for future phases.

## Architecture

- **Backend** (`backend/`): C++17 REST API using cpp-httplib, nlohmann/json, SQLite3/Azure SQL, and OpenSSL (Linux). Built with Bazel (dev) and CMake (Docker).
  - `WORKSPACE` / `BUILD` — Bazel build config
  - `CMakeLists.txt` — CMake build config (used in Docker, `-DUSE_AZURE_SQL=ON` for ODBC)
  - `src/db_backend.h` — Abstract database interface (Param, Row, DbBackend)
  - `src/sqlite_backend.h/.cpp` — SQLite implementation of DbBackend
  - `src/azuresql_backend.h/.cpp` — Azure SQL ODBC implementation (compiled with `HAS_AZURE_SQL`)
  - `src/database.h/.cpp` — High-level Database class using DbBackend; switches SQLite/Azure SQL via `AZURE_SQL_CONNECTION_STRING` env var
  - `src/models.h` — Data structures (User, Task, Plan, PlanItem, WeeklySummary, ProductivityLog) with JSON serialization
  - `src/auth.h/.cpp` — Microsoft token verification (MS Graph with SSL, JWT decode fallback), session management, `requireAuth` wrapper
  - `src/routes.h/.cpp` — HTTP route handlers (all under `/api/`); all data routes wrapped with `requireAuth`
  - `src/main.cpp` — Server entry point (default port 8080)
  - `src/seed.h/.cpp` — Demo data (only runs if `AUTOPLANNER_SEED=1` env var is set)

- **Frontend** (`frontend/`): React 19 + Vite + React Router + MSAL.js. Proxies `/api` calls to the backend.
  - `src/auth/authConfig.js` — MSAL configuration (client ID from `VITE_MICROSOFT_CLIENT_ID` env var)
  - `src/auth/AuthContext.jsx` — React auth context (login, logout, session persistence)
  - `src/auth/LoginPage.jsx` — Microsoft sign-in page
  - `src/api.js` — API client (injects `Authorization: Bearer <token>` header, handles 401)
  - `src/components/` — TaskList, TaskForm, PlanView, SummaryView, ReviewModal

## Authentication

- **Provider**: Microsoft OAuth (both personal MSA and work/school Azure AD accounts)
- **Flow**: MSAL.js popup → access token + ID token → backend `/api/auth/login` → session token stored in localStorage
- **Backend verification**: MS Graph `/me` call (with OpenSSL on Linux) or JWT ID token decode (fallback on Windows without SSL)
- **Session**: Random 64-char hex token stored in `sessions` table, expires in 30 days
- **Data isolation**: All tables have `user_id` column; all queries filtered by authenticated user

### Azure App Registration
- **Client ID**: Configured via `VITE_MICROSOFT_CLIENT_ID` env var (frontend)
- **Redirect URIs** (SPA type): `http://localhost:3000` (dev), `https://autoplanner-pilife.azurewebsites.net` (prod)
- **Supported account types**: Accounts in any organizational directory and personal Microsoft accounts

## Build & Run

### Backend (C++)
```bash
cd backend
bazel build //:autoplanner
bazel run //:autoplanner        # Runs the server on port 8080
```
On Linux with OpenSSL: enables MS Graph token verification. On Windows without OpenSSL: falls back to JWT decode.

### Database Backend
The backend supports two database backends, selected at runtime:
- **SQLite** (default): used when `AZURE_SQL_CONNECTION_STRING` is not set. File-based, great for local dev.
- **Azure SQL**: used when `AZURE_SQL_CONNECTION_STRING` env var is set. Requires ODBC Driver 18. Built with `-DUSE_AZURE_SQL=ON` (CMake) or `-DHAS_AZURE_SQL` (Bazel copts on Linux).

Connection string (Entra Managed Identity, no password): `Driver={ODBC Driver 18 for SQL Server};Server=tcp:autoplanner.database.windows.net,1433;Database=autoplanner;Encrypt=yes;TrustServerCertificate=no;Connection Timeout=30;Authentication=ActiveDirectoryMsi`

### Frontend (React)
```bash
cd frontend
npm install
npm run dev     # Dev server on http://localhost:3000
npm run build   # Production build to dist/
```
Requires `frontend/.env.local` with `VITE_MICROSOFT_CLIENT_ID=<client-id>`.

## Deployment

- **Hosting**: Azure Web App for Containers (`autoplanner-pilife.azurewebsites.net`)
- **Container image**: `ghcr.io/pilife/autoplanner:latest`
- **CI/CD**: GitHub Actions (`.github/workflows/deploy.yml`) builds Docker image on push to `main`, pushes to GHCR, triggers Azure webhook to pull new image
- **GitHub repo**: `pilife/AutoPlanner`
- **GitHub Actions secrets** (in `pilife/AutoPlanner`):
  - `VITE_MICROSOFT_CLIENT_ID` — baked into Docker image at build time (frontend OAuth client ID)
  - `AZURE_SQL_CONNECTION_STRING` — baked into Docker image at build time (database connection)
  - `AZURE_WEBAPP_WEBHOOK_URL` — triggers Azure to pull new image after push
- **Azure App Service config**:
  - System-assigned Managed Identity enabled (for Entra auth to Azure SQL)
  - Database user `[autoplanner-pilife]` created from external provider with `db_owner` role
  - Entra admin: `frankzhang5513@gmail.com`

## API Endpoints

### Auth (unauthenticated)
| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/auth/login` | Exchange Microsoft token for session (`{provider, token, id_token}`) |
| POST | `/api/auth/logout` | Delete session |
| GET | `/api/auth/me` | Get current user info |

### Data (require `Authorization: Bearer <session_token>`)
| Method | Path | Description |
|--------|------|-------------|
| GET/POST | `/api/tasks` | List (with `?status=`/`?category=` filters) / Create |
| GET/PUT/DELETE | `/api/tasks/:id` | Read / Update (partial) / Delete |
| GET | `/api/plans?type=daily&date=YYYY-MM-DD` | Get plan |
| POST | `/api/plans` | Save plan |
| POST | `/api/plans/generate-weekly` | Generate weekly plan from all incomplete leaf tasks |
| POST | `/api/plans/generate-daily` | Distribute weekly tasks across remaining weekdays |
| GET | `/api/plans/unreviewed?before=YYYY-MM-DD` | Get unreviewed daily plans |
| POST | `/api/plans/review` | Review daily plan, update task statuses, regenerate |
| POST/GET | `/api/summaries/generate` / `/api/summaries` | Generate / Get weekly summaries |
| POST/GET | `/api/productivity` / `/api/productivity/:taskId` | Create / Get productivity logs |
| GET | `/api/categories` | Distinct task categories |

## Key Conventions

- Task priority: 1 (Critical) to 5 (Minimal), sorted ascending (1 = most important)
- Task status flow: `todo` -> `in_progress` -> `done`
- Plans are keyed by (user_id, type, date) with UNIQUE constraint — saving overwrites
- Plan items store `task_id`, `scheduled_time` (HH:MM), `duration_minutes`
- Weekly summaries keyed by (user_id, week_date) with UNIQUE constraint
- SQLite database file `autoplanner.db` is created in the working directory
- CORS allows `Authorization` header; permissive (`*`) origin for development
- All data is scoped to the authenticated user — no cross-user data access
