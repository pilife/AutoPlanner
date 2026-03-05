# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AutoPlanner is a productivity planner for knowledge workers. It manages tasks and generates daily/weekly plans. The architecture is a C++ REST API backend + React (Vite) frontend, with AI-driven analytics planned for future phases.

## Architecture

- **Backend** (`backend/`): C++17 REST API using cpp-httplib, nlohmann/json, and SQLite3. Built with Bazel — external deps declared in `WORKSPACE` via `http_archive`.
  - `WORKSPACE` — External dependency declarations (cpp-httplib, nlohmann/json, SQLite3)
  - `BUILD` — Build target for the `autoplanner` binary
  - `src/models.h` — Data structures (Task, Plan, PlanItem, ProductivityLog) with JSON serialization
  - `src/database.h/.cpp` — SQLite wrapper with typed CRUD methods
  - `src/routes.h/.cpp` — HTTP route handlers (all under `/api/`)
  - `src/main.cpp` — Server entry point (default port 8080)

- **Frontend** (`frontend/`): React 19 + Vite + React Router. Proxies `/api` calls to the backend.
  - `src/api.js` — API client (all fetch calls to backend)
  - `src/components/` — TaskList, TaskForm, PlanView

## Build & Run

### Backend (C++)
```bash
cd backend
bazel build //:autoplanner
bazel run //:autoplanner        # Runs the server
```
The binary is at `bazel-bin/autoplanner`. The server starts on port 8080. Pass a port number as argv[1] to override.

### Frontend (React)
```bash
cd frontend
npm install
npm run dev     # Dev server on http://localhost:3000
npm run build   # Production build to dist/
```

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET/POST | `/api/tasks` | List (with `?status=`/`?category=` filters) / Create |
| GET/PUT/DELETE | `/api/tasks/:id` | Read / Update (partial) / Delete |
| GET | `/api/plans?type=daily&date=YYYY-MM-DD` | Get plan |
| POST | `/api/plans` | Save plan |
| POST | `/api/plans/generate` | Auto-generate plan from tasks (stub — AI logic TBD) |
| GET/POST | `/api/productivity/:taskId` / `/api/productivity` | Logs |
| GET | `/api/categories` | Distinct task categories |

## Key Conventions

- Task priority: 1 (Critical) to 5 (Minimal), sorted ascending (1 = most important)
- Task status flow: `todo` -> `in_progress` -> `done`
- Plans are keyed by (type, date) with UNIQUE constraint — saving overwrites
- Plan items store `task_id`, `scheduled_time` (HH:MM), `duration_minutes`
- The generate endpoint is a stub: it schedules all todo tasks sequentially 09:00-18:00 by priority. AI logic will replace this.
- SQLite database file `autoplanner.db` is created in the working directory
- CORS is permissive (`*`) for development
