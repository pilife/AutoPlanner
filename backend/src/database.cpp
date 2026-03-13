#include "database.h"
#include "auth.h"
#include <stdexcept>
#include <ctime>
#include <sstream>
#include <iomanip>

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    initTables();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::initTables() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            provider    TEXT NOT NULL,
            provider_id TEXT NOT NULL,
            email       TEXT NOT NULL,
            name        TEXT DEFAULT '',
            avatar_url  TEXT DEFAULT '',
            created_at  TEXT NOT NULL,
            UNIQUE(provider, provider_id)
        );

        CREATE TABLE IF NOT EXISTS sessions (
            token      TEXT PRIMARY KEY,
            user_id    INTEGER NOT NULL,
            created_at TEXT NOT NULL,
            expires_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS tasks (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id         INTEGER NOT NULL DEFAULT 0,
            parent_id       INTEGER DEFAULT 0,
            title           TEXT NOT NULL,
            description     TEXT DEFAULT '',
            priority        INTEGER DEFAULT 3,
            estimated_minutes INTEGER DEFAULT 30,
            actual_minutes  INTEGER DEFAULT 0,
            category        TEXT DEFAULT '',
            status          TEXT DEFAULT 'todo',
            due_date        TEXT DEFAULT '',
            created_at      TEXT NOT NULL,
            updated_at      TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS plans (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id    INTEGER NOT NULL DEFAULT 0,
            type       TEXT NOT NULL,
            date       TEXT NOT NULL,
            items_json TEXT DEFAULT '[]',
            reviewed   INTEGER DEFAULT 0,
            created_at TEXT NOT NULL,
            UNIQUE(user_id, type, date)
        );

        CREATE TABLE IF NOT EXISTS weekly_summaries (
            id                 INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id            INTEGER NOT NULL DEFAULT 0,
            week_date          TEXT NOT NULL,
            total_planned      INTEGER DEFAULT 0,
            total_completed    INTEGER DEFAULT 0,
            total_actual       INTEGER DEFAULT 0,
            tasks_planned      INTEGER DEFAULT 0,
            tasks_completed    INTEGER DEFAULT 0,
            tasks_carried_over INTEGER DEFAULT 0,
            category_breakdown TEXT DEFAULT '{}',
            completed_tasks    TEXT DEFAULT '[]',
            incomplete_tasks   TEXT DEFAULT '[]',
            created_at         TEXT NOT NULL,
            UNIQUE(user_id, week_date)
        );

        CREATE TABLE IF NOT EXISTS productivity_logs (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id    INTEGER NOT NULL DEFAULT 0,
            task_id    INTEGER NOT NULL,
            start_time TEXT NOT NULL,
            end_time   TEXT DEFAULT '',
            notes      TEXT DEFAULT '',
            FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_tasks_user ON tasks(user_id);
        CREATE INDEX IF NOT EXISTS idx_plans_user ON plans(user_id);
        CREATE INDEX IF NOT EXISTS idx_summaries_user ON weekly_summaries(user_id);
        CREATE INDEX IF NOT EXISTS idx_logs_user ON productivity_logs(user_id);
    )");
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg);
    }
}

void Database::query(const std::string& sql,
                     const std::function<void(sqlite3_stmt*)>& rowCallback) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("SQL prepare error: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rowCallback(stmt);
    }
    sqlite3_finalize(stmt);
}

std::string Database::now() {
    auto t = std::time(nullptr);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// ── Users ──────────────────────────────────────────────────────────────

User Database::getOrCreateUser(const std::string& provider, const std::string& providerId,
                                const std::string& email, const std::string& name,
                                const std::string& avatarUrl) {
    // Try to find existing user
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, provider, provider_id, email, name, avatar_url, created_at "
        "FROM users WHERE provider = ? AND provider_id = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, providerId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = sqlite3_column_int(stmt, 0);
        u.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.provider_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u.avatar_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        sqlite3_finalize(stmt);

        // Update name/email/avatar if changed
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(db_,
            "UPDATE users SET email = ?, name = ?, avatar_url = ? WHERE id = ?",
            -1, &upd, nullptr);
        sqlite3_bind_text(upd, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 3, avatarUrl.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(upd, 4, u.id);
        sqlite3_step(upd);
        sqlite3_finalize(upd);

        u.email = email;
        u.name = name;
        u.avatar_url = avatarUrl;
        return u;
    }
    sqlite3_finalize(stmt);

    // Create new user
    std::string ts = now();
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO users (provider, provider_id, email, name, avatar_url, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        -1, &ins, nullptr);
    sqlite3_bind_text(ins, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, providerId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, avatarUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, ts.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(ins) != SQLITE_DONE) {
        sqlite3_finalize(ins);
        throw std::runtime_error("Failed to create user");
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(ins);

    User u;
    u.id = id;
    u.provider = provider;
    u.provider_id = providerId;
    u.email = email;
    u.name = name;
    u.avatar_url = avatarUrl;
    u.created_at = ts;
    return u;
}

std::optional<User> Database::getUserById(int id) {
    std::optional<User> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, provider, provider_id, email, name, avatar_url, created_at "
        "FROM users WHERE id = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = sqlite3_column_int(stmt, 0);
        u.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.provider_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u.avatar_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        result = u;
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── Sessions ───────────────────────────────────────────────────────────

std::string Database::createSession(int userId) {
    std::string token = generateSessionToken();
    std::string ts = now();

    // Expires in 30 days
    auto t = std::time(nullptr);
    t += 30 * 24 * 60 * 60;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    std::string expiresAt = oss.str();

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO sessions (token, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, userId);
    sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, expiresAt.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to create session");
    }
    sqlite3_finalize(stmt);
    return token;
}

std::optional<int> Database::validateSession(const std::string& token) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT user_id, expires_at FROM sessions WHERE token = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<int> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        std::string expiresAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string current = now();
        if (current < expiresAt) {
            result = userId;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void Database::deleteSession(const std::string& token) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM sessions WHERE token = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ── Tasks ──────────────────────────────────────────────────────────────

static Task rowToTask(sqlite3_stmt* stmt) {
    Task t;
    t.id                = sqlite3_column_int(stmt, 0);
    // column 1 is user_id, skip it for the Task struct
    t.parent_id         = sqlite3_column_int(stmt, 2);
    t.title             = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    t.description       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    t.priority          = sqlite3_column_int(stmt, 5);
    t.estimated_minutes = sqlite3_column_int(stmt, 6);
    t.actual_minutes    = sqlite3_column_int(stmt, 7);
    t.category          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    t.status            = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    t.due_date          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    t.created_at        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    t.updated_at        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    return t;
}

Task Database::createTask(int userId, const Task& task) {
    std::string ts = now();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tasks "
        "(user_id, parent_id, title, description, priority, estimated_minutes, actual_minutes, "
        "category, status, due_date, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, task.parent_id);
    sqlite3_bind_text(stmt, 3, task.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, task.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, task.priority);
    sqlite3_bind_int(stmt, 6, task.estimated_minutes);
    sqlite3_bind_int(stmt, 7, task.actual_minutes);
    sqlite3_bind_text(stmt, 8, task.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, task.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, task.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, ts.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to create task");
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);

    Task result = task;
    result.id = id;
    result.created_at = ts;
    result.updated_at = ts;
    return result;
}

std::optional<Task> Database::getTask(int userId, int id) {
    std::optional<Task> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT * FROM tasks WHERE id = ? AND user_id = ?",
                        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, userId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToTask(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Task> Database::getAllTasks(int userId, const std::string& status,
                                        const std::string& category) {
    std::vector<Task> tasks;
    std::string sql = "SELECT * FROM tasks WHERE user_id = ?";
    if (!status.empty())   sql += " AND status = '" + status + "'";
    if (!category.empty()) sql += " AND category = '" + category + "'";
    sql += " ORDER BY priority ASC, due_date ASC";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tasks.push_back(rowToTask(stmt));
    }
    sqlite3_finalize(stmt);
    return tasks;
}

bool Database::updateTask(int userId, int id, const Task& task) {
    std::string ts = now();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE tasks SET "
        "parent_id=?, title=?, description=?, priority=?, estimated_minutes=?, "
        "actual_minutes=?, category=?, status=?, due_date=?, updated_at=? "
        "WHERE id=? AND user_id=?";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, task.parent_id);
    sqlite3_bind_text(stmt, 2, task.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, task.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, task.priority);
    sqlite3_bind_int(stmt, 5, task.estimated_minutes);
    sqlite3_bind_int(stmt, 6, task.actual_minutes);
    sqlite3_bind_text(stmt, 7, task.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, task.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, task.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, id);
    sqlite3_bind_int(stmt, 12, userId);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteTask(int userId, int id) {
    auto task = getTask(userId, id);
    int parentId = task ? task->parent_id : 0;

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM tasks WHERE id = ? AND user_id = ?",
                        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);

    if (ok) {
        // Delete children recursively
        std::vector<int> childIds;
        sqlite3_stmt* q = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT id FROM tasks WHERE parent_id = ? AND user_id = ?",
            -1, &q, nullptr);
        sqlite3_bind_int(q, 1, id);
        sqlite3_bind_int(q, 2, userId);
        while (sqlite3_step(q) == SQLITE_ROW) {
            childIds.push_back(sqlite3_column_int(q, 0));
        }
        sqlite3_finalize(q);

        for (int cid : childIds) {
            deleteTask(userId, cid);
        }
        if (parentId > 0) recalcEstimate(parentId);
    }
    return ok;
}

void Database::recalcEstimate(int taskId) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COUNT(*), COALESCE(SUM(estimated_minutes), 0) "
        "FROM tasks WHERE parent_id = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, taskId);

    int childCount = 0, total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        childCount = sqlite3_column_int(stmt, 0);
        total = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    if (childCount == 0) return;

    sqlite3_stmt* upd = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE tasks SET estimated_minutes = ?, updated_at = ? WHERE id = ?",
        -1, &upd, nullptr);
    std::string ts = now();
    sqlite3_bind_int(upd, 1, total);
    sqlite3_bind_text(upd, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(upd, 3, taskId);
    sqlite3_step(upd);
    sqlite3_finalize(upd);

    // Recurse up — we need to find parent without user_id filter since
    // recalcEstimate is called internally after a user-scoped operation
    sqlite3_stmt* pq = nullptr;
    sqlite3_prepare_v2(db_, "SELECT parent_id FROM tasks WHERE id = ?", -1, &pq, nullptr);
    sqlite3_bind_int(pq, 1, taskId);
    int parentId = 0;
    if (sqlite3_step(pq) == SQLITE_ROW) {
        parentId = sqlite3_column_int(pq, 0);
    }
    sqlite3_finalize(pq);

    if (parentId > 0) {
        recalcEstimate(parentId);
    }
}

// ── Plans ──────────────────────────────────────────────────────────────

Plan Database::createPlan(int userId, const Plan& plan) {
    std::string ts = now();
    nlohmann::json itemsJson = plan.items;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO plans (user_id, type, date, items_json, created_at) "
                      "VALUES (?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, plan.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, plan.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, itemsJson.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, ts.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to create plan");
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);

    Plan result = plan;
    result.id = id;
    result.created_at = ts;
    return result;
}

std::optional<Plan> Database::getPlanByTypeAndDate(int userId, const std::string& type,
                                                    const std::string& date) {
    std::optional<Plan> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, type, date, items_json, reviewed, created_at FROM plans "
        "WHERE user_id = ? AND type = ? AND date = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, date.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Plan p;
        p.id = sqlite3_column_int(stmt, 0);
        p.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto itemsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        p.items = nlohmann::json::parse(itemsStr).get<std::vector<PlanItem>>();
        p.reviewed = sqlite3_column_int(stmt, 4) != 0;
        p.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        result = p;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::markPlanReviewed(int userId, int id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE plans SET reviewed = 1 WHERE id = ? AND user_id = ?",
                        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Plan> Database::getUnreviewedDailyPlans(int userId,
                                                     const std::string& beforeDate,
                                                     const std::string& monday) {
    std::vector<Plan> plans;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, type, date, items_json, reviewed, created_at FROM plans "
        "WHERE user_id = ? AND type = 'daily' AND reviewed = 0 AND date < ? AND date >= ? "
        "ORDER BY date ASC", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, beforeDate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, monday.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Plan p;
        p.id = sqlite3_column_int(stmt, 0);
        p.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto itemsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        p.items = nlohmann::json::parse(itemsStr).get<std::vector<PlanItem>>();
        p.reviewed = sqlite3_column_int(stmt, 4) != 0;
        p.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        plans.push_back(p);
    }
    sqlite3_finalize(stmt);
    return plans;
}

bool Database::deletePlan(int userId, int id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM plans WHERE id = ? AND user_id = ?",
                        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

// ── Productivity Logs ──────────────────────────────────────────────────

ProductivityLog Database::createLog(int userId, const ProductivityLog& log) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO productivity_logs "
        "(user_id, task_id, start_time, end_time, notes) VALUES (?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, log.task_id);
    sqlite3_bind_text(stmt, 3, log.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, log.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, log.notes.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to create log");
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);

    ProductivityLog result = log;
    result.id = id;
    return result;
}

std::vector<ProductivityLog> Database::getLogsByTaskId(int userId, int taskId) {
    std::vector<ProductivityLog> logs;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, task_id, start_time, end_time, notes "
        "FROM productivity_logs WHERE user_id = ? AND task_id = ? ORDER BY start_time DESC",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, taskId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ProductivityLog l;
        l.id         = sqlite3_column_int(stmt, 0);
        l.task_id    = sqlite3_column_int(stmt, 1);
        l.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        l.end_time   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        l.notes      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        logs.push_back(l);
    }
    sqlite3_finalize(stmt);
    return logs;
}

// ── Weekly Summaries ───────────────────────────────────────────────────

WeeklySummary Database::createSummary(int userId, const WeeklySummary& summary) {
    std::string ts = now();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO weekly_summaries "
        "(user_id, week_date, total_planned, total_completed, total_actual, "
        "tasks_planned, tasks_completed, tasks_carried_over, "
        "category_breakdown, completed_tasks, incomplete_tasks, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, summary.week_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, summary.total_planned);
    sqlite3_bind_int(stmt, 4, summary.total_completed);
    sqlite3_bind_int(stmt, 5, summary.total_actual);
    sqlite3_bind_int(stmt, 6, summary.tasks_planned);
    sqlite3_bind_int(stmt, 7, summary.tasks_completed);
    sqlite3_bind_int(stmt, 8, summary.tasks_carried_over);
    sqlite3_bind_text(stmt, 9, summary.category_breakdown.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, summary.completed_tasks.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, summary.incomplete_tasks.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, ts.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to create summary");
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);

    WeeklySummary result = summary;
    result.id = id;
    result.created_at = ts;
    return result;
}

std::optional<WeeklySummary> Database::getSummary(int userId, const std::string& weekDate) {
    std::optional<WeeklySummary> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, week_date, total_planned, total_completed, total_actual, "
        "tasks_planned, tasks_completed, tasks_carried_over, "
        "category_breakdown, completed_tasks, incomplete_tasks, created_at "
        "FROM weekly_summaries WHERE user_id = ? AND week_date = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, weekDate.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        WeeklySummary s;
        s.id = sqlite3_column_int(stmt, 0);
        s.week_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.total_planned = sqlite3_column_int(stmt, 2);
        s.total_completed = sqlite3_column_int(stmt, 3);
        s.total_actual = sqlite3_column_int(stmt, 4);
        s.tasks_planned = sqlite3_column_int(stmt, 5);
        s.tasks_completed = sqlite3_column_int(stmt, 6);
        s.tasks_carried_over = sqlite3_column_int(stmt, 7);
        s.category_breakdown = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        s.completed_tasks = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
        s.incomplete_tasks = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)));
        s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        result = s;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<WeeklySummary> Database::getAllSummaries(int userId) {
    std::vector<WeeklySummary> summaries;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, week_date, total_planned, total_completed, total_actual, "
        "tasks_planned, tasks_completed, tasks_carried_over, "
        "category_breakdown, completed_tasks, incomplete_tasks, created_at "
        "FROM weekly_summaries WHERE user_id = ? ORDER BY week_date DESC",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WeeklySummary s;
        s.id = sqlite3_column_int(stmt, 0);
        s.week_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.total_planned = sqlite3_column_int(stmt, 2);
        s.total_completed = sqlite3_column_int(stmt, 3);
        s.total_actual = sqlite3_column_int(stmt, 4);
        s.tasks_planned = sqlite3_column_int(stmt, 5);
        s.tasks_completed = sqlite3_column_int(stmt, 6);
        s.tasks_carried_over = sqlite3_column_int(stmt, 7);
        s.category_breakdown = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        s.completed_tasks = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
        s.incomplete_tasks = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)));
        s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        summaries.push_back(s);
    }
    sqlite3_finalize(stmt);
    return summaries;
}

// ── Categories ─────────────────────────────────────────────────────────

std::vector<std::string> Database::getCategories(int userId) {
    std::vector<std::string> cats;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT DISTINCT category FROM tasks WHERE user_id = ? AND category != '' ORDER BY category",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cats.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return cats;
}
