#include "database.h"
#include "auth.h"
#include "sqlite_backend.h"
#ifdef HAS_AZURE_SQL
#include "azuresql_backend.h"
#endif
#include <stdexcept>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

Database::Database(const std::string& sqlitePath) {
    const char* connStr = std::getenv("AZURE_SQL_CONNECTION_STRING");
#ifdef HAS_AZURE_SQL
    if (connStr && std::strlen(connStr) > 0) {
        spdlog::info("[DB] Connecting to Azure SQL...");
        backend_ = std::make_unique<AzureSqlBackend>(connStr);
        useAzureSql_ = true;
        spdlog::info("[DB] Connected to Azure SQL");
    } else
#endif
    {
        (void)connStr;
        spdlog::info("[DB] Using SQLite: {}", sqlitePath);
        backend_ = std::make_unique<SqliteBackend>(sqlitePath);
    }
    spdlog::info("[DB] Initializing tables...");
    initTables();
    spdlog::info("[DB] Tables ready");
}

Database::~Database() = default;

void Database::initTables() {
    if (useAzureSql_) {
        // T-SQL schema with IDENTITY, NVARCHAR, BIT
        backend_->exec(R"(
            IF OBJECT_ID('dbo.users', 'U') IS NULL
            CREATE TABLE dbo.users (
                id          INT IDENTITY(1,1) PRIMARY KEY,
                provider    NVARCHAR(100)  NOT NULL,
                provider_id NVARCHAR(500) NOT NULL,
                email       NVARCHAR(500) NOT NULL,
                name        NVARCHAR(500) DEFAULT '',
                avatar_url  NVARCHAR(MAX) DEFAULT '',
                created_at  NVARCHAR(50)  NOT NULL,
                CONSTRAINT UQ_users_provider UNIQUE(provider, provider_id)
            )
        )");
        backend_->exec(R"(
            IF OBJECT_ID('dbo.sessions', 'U') IS NULL
            CREATE TABLE dbo.sessions (
                token      NVARCHAR(128) PRIMARY KEY,
                user_id    INT NOT NULL,
                created_at NVARCHAR(50) NOT NULL,
                expires_at NVARCHAR(50) NOT NULL,
                FOREIGN KEY (user_id) REFERENCES dbo.users(id) ON DELETE CASCADE
            )
        )");
        backend_->exec(R"(
            IF OBJECT_ID('dbo.tasks', 'U') IS NULL
            CREATE TABLE dbo.tasks (
                id                INT IDENTITY(1,1) PRIMARY KEY,
                user_id           INT NOT NULL DEFAULT 0,
                parent_id         INT DEFAULT 0,
                title             NVARCHAR(500) NOT NULL,
                description       NVARCHAR(MAX) DEFAULT '',
                priority          INT DEFAULT 3,
                estimated_minutes INT DEFAULT 30,
                actual_minutes    INT DEFAULT 0,
                category          NVARCHAR(100) DEFAULT '',
                status            NVARCHAR(20)  DEFAULT 'todo',
                due_date          NVARCHAR(10)  DEFAULT '',
                archived          BIT DEFAULT 0,
                created_at        NVARCHAR(50) NOT NULL,
                updated_at        NVARCHAR(50) NOT NULL
            )
        )");
        // Migration: add archived column to existing tables
        backend_->exec(R"(
            IF NOT EXISTS (SELECT 1 FROM sys.columns
                           WHERE object_id = OBJECT_ID('dbo.tasks') AND name = 'archived')
            ALTER TABLE dbo.tasks ADD archived BIT DEFAULT 0
        )");
        backend_->exec(R"(
            IF OBJECT_ID('dbo.plans', 'U') IS NULL
            CREATE TABLE dbo.plans (
                id         INT IDENTITY(1,1) PRIMARY KEY,
                user_id    INT NOT NULL DEFAULT 0,
                type       NVARCHAR(20) NOT NULL,
                date       NVARCHAR(10) NOT NULL,
                items_json NVARCHAR(MAX) DEFAULT '[]',
                reviewed   BIT DEFAULT 0,
                created_at NVARCHAR(50) NOT NULL,
                CONSTRAINT UQ_plans_user_type_date UNIQUE(user_id, type, date)
            )
        )");
        backend_->exec(R"(
            IF OBJECT_ID('dbo.weekly_summaries', 'U') IS NULL
            CREATE TABLE dbo.weekly_summaries (
                id                 INT IDENTITY(1,1) PRIMARY KEY,
                user_id            INT NOT NULL DEFAULT 0,
                week_date          NVARCHAR(10) NOT NULL,
                total_planned      INT DEFAULT 0,
                total_completed    INT DEFAULT 0,
                total_actual       INT DEFAULT 0,
                tasks_planned      INT DEFAULT 0,
                tasks_completed    INT DEFAULT 0,
                tasks_carried_over INT DEFAULT 0,
                category_breakdown NVARCHAR(MAX) DEFAULT '{}',
                completed_tasks    NVARCHAR(MAX) DEFAULT '[]',
                incomplete_tasks   NVARCHAR(MAX) DEFAULT '[]',
                created_at         NVARCHAR(50) NOT NULL,
                CONSTRAINT UQ_summaries_user_week UNIQUE(user_id, week_date)
            )
        )");
        backend_->exec(R"(
            IF OBJECT_ID('dbo.productivity_logs', 'U') IS NULL
            CREATE TABLE dbo.productivity_logs (
                id         INT IDENTITY(1,1) PRIMARY KEY,
                user_id    INT NOT NULL DEFAULT 0,
                task_id    INT NOT NULL,
                start_time NVARCHAR(50) NOT NULL,
                end_time   NVARCHAR(50) DEFAULT '',
                notes      NVARCHAR(MAX) DEFAULT '',
                FOREIGN KEY (task_id) REFERENCES dbo.tasks(id) ON DELETE CASCADE
            )
        )");
        // Indexes
        backend_->exec("IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = 'idx_tasks_user') CREATE INDEX idx_tasks_user ON dbo.tasks(user_id)");
        backend_->exec("IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = 'idx_plans_user') CREATE INDEX idx_plans_user ON dbo.plans(user_id)");
        backend_->exec("IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = 'idx_summaries_user') CREATE INDEX idx_summaries_user ON dbo.weekly_summaries(user_id)");
        backend_->exec("IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = 'idx_logs_user') CREATE INDEX idx_logs_user ON dbo.productivity_logs(user_id)");
    } else {
        // SQLite schema
        backend_->exec(R"(
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
                archived        INTEGER DEFAULT 0,
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

        // Migration: add archived column if it doesn't exist
        try {
            backend_->exec("ALTER TABLE tasks ADD COLUMN archived INTEGER DEFAULT 0");
        } catch (...) { /* column already exists */ }
    }
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

// ── SQL helpers for INSERT with OUTPUT (Azure SQL) vs plain INSERT (SQLite) ──

std::string Database::insertUserSql() const {
    if (useAzureSql_)
        return "INSERT INTO users (provider, provider_id, email, name, avatar_url, created_at) "
               "OUTPUT INSERTED.id VALUES (?, ?, ?, ?, ?, ?)";
    return "INSERT INTO users (provider, provider_id, email, name, avatar_url, created_at) "
           "VALUES (?, ?, ?, ?, ?, ?)";
}

std::string Database::insertSessionSql() const {
    return "INSERT INTO sessions (token, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)";
}

std::string Database::insertTaskSql() const {
    if (useAzureSql_)
        return "INSERT INTO tasks (user_id, parent_id, title, description, priority, "
               "estimated_minutes, actual_minutes, category, status, due_date, archived, created_at, updated_at) "
               "OUTPUT INSERTED.id VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    return "INSERT INTO tasks (user_id, parent_id, title, description, priority, "
           "estimated_minutes, actual_minutes, category, status, due_date, archived, created_at, updated_at) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
}

std::string Database::insertLogSql() const {
    if (useAzureSql_)
        return "INSERT INTO productivity_logs (user_id, task_id, start_time, end_time, notes) "
               "OUTPUT INSERTED.id VALUES (?, ?, ?, ?, ?)";
    return "INSERT INTO productivity_logs (user_id, task_id, start_time, end_time, notes) "
           "VALUES (?, ?, ?, ?, ?)";
}

// ── Helpers ────────────────────────────────────────────────────────────

static Task rowToTask(const Row& r) {
    Task t;
    t.id                = r.getInt(0);
    t.parent_id         = r.getInt(1);
    t.title             = r.getText(2);
    t.description       = r.getText(3);
    t.priority          = r.getInt(4);
    t.estimated_minutes = r.getInt(5);
    t.actual_minutes    = r.getInt(6);
    t.category          = r.getText(7);
    t.status            = r.getText(8);
    t.due_date          = r.getText(9);
    t.archived          = r.getInt(10);
    t.created_at        = r.getText(11);
    t.updated_at        = r.getText(12);
    return t;
}

static const char* TASK_COLS =
    "id, parent_id, title, description, priority, estimated_minutes, "
    "actual_minutes, category, status, due_date, archived, created_at, updated_at";

// ── Users ──────────────────────────────────────────────────────────────

User Database::getOrCreateUser(const std::string& provider, const std::string& providerId,
                                const std::string& email, const std::string& name,
                                const std::string& avatarUrl) {
    // Try to find existing user
    User u;
    bool found = false;
    backend_->query(
        "SELECT id, provider, provider_id, email, name, avatar_url, created_at "
        "FROM users WHERE provider = ? AND provider_id = ?",
        {Param::Text(provider), Param::Text(providerId)},
        [&](const Row& r) {
            u.id = r.getInt(0);
            u.provider = r.getText(1);
            u.provider_id = r.getText(2);
            u.email = r.getText(3);
            u.name = r.getText(4);
            u.avatar_url = r.getText(5);
            u.created_at = r.getText(6);
            found = true;
        });

    if (found) {
        backend_->execute(
            "UPDATE users SET email = ?, name = ?, avatar_url = ? WHERE id = ?",
            {Param::Text(email), Param::Text(name), Param::Text(avatarUrl), Param::Int(u.id)});
        u.email = email;
        u.name = name;
        u.avatar_url = avatarUrl;
        return u;
    }

    std::string ts = now();
    int id = backend_->insertReturningId(insertUserSql(),
        {Param::Text(provider), Param::Text(providerId), Param::Text(email),
         Param::Text(name), Param::Text(avatarUrl), Param::Text(ts)});

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
    backend_->query(
        "SELECT id, provider, provider_id, email, name, avatar_url, created_at "
        "FROM users WHERE id = ?",
        {Param::Int(id)},
        [&](const Row& r) {
            User u;
            u.id = r.getInt(0);
            u.provider = r.getText(1);
            u.provider_id = r.getText(2);
            u.email = r.getText(3);
            u.name = r.getText(4);
            u.avatar_url = r.getText(5);
            u.created_at = r.getText(6);
            result = u;
        });
    return result;
}

// ── Sessions ───────────────────────────────────────────────────────────

std::string Database::createSession(int userId) {
    std::string token = generateSessionToken();
    std::string ts = now();

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

    backend_->execute(insertSessionSql(),
        {Param::Text(token), Param::Int(userId), Param::Text(ts), Param::Text(expiresAt)});
    return token;
}

std::optional<int> Database::validateSession(const std::string& token) {
    std::optional<int> result;
    std::string current = now();
    backend_->query(
        "SELECT user_id, expires_at FROM sessions WHERE token = ?",
        {Param::Text(token)},
        [&](const Row& r) {
            int userId = r.getInt(0);
            std::string expiresAt = r.getText(1);
            if (current < expiresAt) {
                result = userId;
            }
        });
    return result;
}

void Database::deleteSession(const std::string& token) {
    backend_->execute("DELETE FROM sessions WHERE token = ?", {Param::Text(token)});
}

// ── Tasks ──────────────────────────────────────────────────────────────

Task Database::createTask(int userId, const Task& task) {
    std::string ts = now();
    int id = backend_->insertReturningId(insertTaskSql(),
        {Param::Int(userId), Param::Int(task.parent_id),
         Param::Text(task.title), Param::Text(task.description),
         Param::Int(task.priority), Param::Int(task.estimated_minutes),
         Param::Int(task.actual_minutes), Param::Text(task.category),
         Param::Text(task.status), Param::Text(task.due_date),
         Param::Int(task.archived),
         Param::Text(ts), Param::Text(ts)});

    Task result = task;
    result.id = id;
    result.created_at = ts;
    result.updated_at = ts;
    return result;
}

std::optional<Task> Database::getTask(int userId, int id) {
    std::optional<Task> result;
    std::string sql = std::string("SELECT ") + TASK_COLS +
                      " FROM tasks WHERE id = ? AND user_id = ?";
    backend_->query(sql, {Param::Int(id), Param::Int(userId)},
        [&](const Row& r) { result = rowToTask(r); });
    return result;
}

std::vector<Task> Database::getAllTasks(int userId, const std::string& status,
                                        const std::string& category,
                                        bool includeArchived) {
    std::vector<Task> tasks;
    std::string sql = std::string("SELECT ") + TASK_COLS + " FROM tasks WHERE user_id = ?";
    std::vector<Param> params = {Param::Int(userId)};

    if (!status.empty()) {
        sql += " AND status = ?";
        params.push_back(Param::Text(status));
    }
    if (!category.empty()) {
        sql += " AND category = ?";
        params.push_back(Param::Text(category));
    }
    if (!includeArchived) {
        sql += " AND archived = 0";
    }
    sql += " ORDER BY priority ASC, due_date ASC";

    backend_->query(sql, params, [&](const Row& r) { tasks.push_back(rowToTask(r)); });
    return tasks;
}

bool Database::updateTask(int userId, int id, const Task& task) {
    std::string ts = now();
    int affected = backend_->execute(
        "UPDATE tasks SET parent_id=?, title=?, description=?, priority=?, "
        "estimated_minutes=?, actual_minutes=?, category=?, status=?, due_date=?, archived=?, updated_at=? "
        "WHERE id=? AND user_id=?",
        {Param::Int(task.parent_id), Param::Text(task.title), Param::Text(task.description),
         Param::Int(task.priority), Param::Int(task.estimated_minutes),
         Param::Int(task.actual_minutes), Param::Text(task.category),
         Param::Text(task.status), Param::Text(task.due_date),
         Param::Int(task.archived), Param::Text(ts),
         Param::Int(id), Param::Int(userId)});
    return affected > 0;
}

bool Database::deleteTask(int userId, int id) {
    auto task = getTask(userId, id);
    int parentId = task ? task->parent_id : 0;

    backend_->beginTransaction();
    try {
        int affected = backend_->execute(
            "DELETE FROM tasks WHERE id = ? AND user_id = ?",
            {Param::Int(id), Param::Int(userId)});

        if (affected > 0) {
            // Delete children recursively
            std::vector<int> childIds;
            backend_->query(
                "SELECT id FROM tasks WHERE parent_id = ? AND user_id = ?",
                {Param::Int(id), Param::Int(userId)},
                [&](const Row& r) { childIds.push_back(r.getInt(0)); });

            for (int cid : childIds) {
                backend_->execute("DELETE FROM tasks WHERE id = ? AND user_id = ?",
                    {Param::Int(cid), Param::Int(userId)});
            }
        }
        backend_->commit();

        if (affected > 0 && parentId > 0) {
            recalcEstimate(parentId);
        }
        return affected > 0;
    } catch (...) {
        backend_->rollback();
        throw;
    }
}

void Database::recalcEstimate(int taskId) {
    int childCount = 0, total = 0;
    backend_->query(
        "SELECT COUNT(*), COALESCE(SUM(estimated_minutes), 0) FROM tasks WHERE parent_id = ?",
        {Param::Int(taskId)},
        [&](const Row& r) { childCount = r.getInt(0); total = r.getInt(1); });

    if (childCount == 0) return;

    std::string ts = now();
    backend_->execute(
        "UPDATE tasks SET estimated_minutes = ?, updated_at = ? WHERE id = ?",
        {Param::Int(total), Param::Text(ts), Param::Int(taskId)});

    int parentId = 0;
    backend_->query(
        "SELECT parent_id FROM tasks WHERE id = ?",
        {Param::Int(taskId)},
        [&](const Row& r) { parentId = r.getInt(0); });

    if (parentId > 0) {
        recalcEstimate(parentId);
    }
}

// ── Plans ──────────────────────────────────────────────────────────────

Plan Database::createPlan(int userId, const Plan& plan) {
    std::string ts = now();
    nlohmann::json itemsJson = plan.items;
    std::string itemsStr = itemsJson.dump();

    int id = 0;
    if (useAzureSql_) {
        // Azure SQL: UPDATE-then-INSERT (no INSERT OR REPLACE)
        int affected = backend_->execute(
            "UPDATE plans SET items_json = ?, reviewed = 0, created_at = ? "
            "WHERE user_id = ? AND type = ? AND date = ?",
            {Param::Text(itemsStr), Param::Text(ts),
             Param::Int(userId), Param::Text(plan.type), Param::Text(plan.date)});

        if (affected > 0) {
            backend_->query(
                "SELECT id FROM plans WHERE user_id = ? AND type = ? AND date = ?",
                {Param::Int(userId), Param::Text(plan.type), Param::Text(plan.date)},
                [&](const Row& r) { id = r.getInt(0); });
        } else {
            id = backend_->insertReturningId(
                "INSERT INTO plans (user_id, type, date, items_json, created_at) "
                "OUTPUT INSERTED.id VALUES (?, ?, ?, ?, ?)",
                {Param::Int(userId), Param::Text(plan.type), Param::Text(plan.date),
                 Param::Text(itemsStr), Param::Text(ts)});
        }
    } else {
        // SQLite: INSERT OR REPLACE
        id = backend_->insertReturningId(
            "INSERT OR REPLACE INTO plans (user_id, type, date, items_json, created_at) "
            "VALUES (?, ?, ?, ?, ?)",
            {Param::Int(userId), Param::Text(plan.type), Param::Text(plan.date),
             Param::Text(itemsStr), Param::Text(ts)});
    }

    Plan result = plan;
    result.id = id;
    result.created_at = ts;
    return result;
}

std::optional<Plan> Database::getPlanByTypeAndDate(int userId, const std::string& type,
                                                    const std::string& date) {
    std::optional<Plan> result;
    backend_->query(
        "SELECT id, type, date, items_json, reviewed, created_at FROM plans "
        "WHERE user_id = ? AND type = ? AND date = ?",
        {Param::Int(userId), Param::Text(type), Param::Text(date)},
        [&](const Row& r) {
            Plan p;
            p.id = r.getInt(0);
            p.type = r.getText(1);
            p.date = r.getText(2);
            auto itemsStr = r.getText(3);
            p.items = nlohmann::json::parse(itemsStr).get<std::vector<PlanItem>>();
            p.reviewed = r.getInt(4) != 0;
            p.created_at = r.getText(5);
            result = p;
        });
    return result;
}

bool Database::markPlanReviewed(int userId, int id) {
    int affected = backend_->execute(
        "UPDATE plans SET reviewed = 1 WHERE id = ? AND user_id = ?",
        {Param::Int(id), Param::Int(userId)});
    return affected > 0;
}

std::vector<Plan> Database::getUnreviewedDailyPlans(int userId,
                                                     const std::string& beforeDate,
                                                     const std::string& monday) {
    std::vector<Plan> plans;
    backend_->query(
        "SELECT id, type, date, items_json, reviewed, created_at FROM plans "
        "WHERE user_id = ? AND type = 'daily' AND reviewed = 0 AND date < ? AND date >= ? "
        "ORDER BY date ASC",
        {Param::Int(userId), Param::Text(beforeDate), Param::Text(monday)},
        [&](const Row& r) {
            Plan p;
            p.id = r.getInt(0);
            p.type = r.getText(1);
            p.date = r.getText(2);
            auto itemsStr = r.getText(3);
            p.items = nlohmann::json::parse(itemsStr).get<std::vector<PlanItem>>();
            p.reviewed = r.getInt(4) != 0;
            p.created_at = r.getText(5);
            plans.push_back(p);
        });
    return plans;
}

bool Database::deletePlan(int userId, int id) {
    int affected = backend_->execute(
        "DELETE FROM plans WHERE id = ? AND user_id = ?",
        {Param::Int(id), Param::Int(userId)});
    return affected > 0;
}

// ── Productivity Logs ──────────────────────────────────────────────────

ProductivityLog Database::createLog(int userId, const ProductivityLog& log) {
    int id = backend_->insertReturningId(insertLogSql(),
        {Param::Int(userId), Param::Int(log.task_id),
         Param::Text(log.start_time), Param::Text(log.end_time), Param::Text(log.notes)});

    ProductivityLog result = log;
    result.id = id;
    return result;
}

std::vector<ProductivityLog> Database::getLogsByTaskId(int userId, int taskId) {
    std::vector<ProductivityLog> logs;
    backend_->query(
        "SELECT id, task_id, start_time, end_time, notes "
        "FROM productivity_logs WHERE user_id = ? AND task_id = ? ORDER BY start_time DESC",
        {Param::Int(userId), Param::Int(taskId)},
        [&](const Row& r) {
            ProductivityLog l;
            l.id = r.getInt(0);
            l.task_id = r.getInt(1);
            l.start_time = r.getText(2);
            l.end_time = r.getText(3);
            l.notes = r.getText(4);
            logs.push_back(l);
        });
    return logs;
}

// ── Weekly Summaries ───────────────────────────────────────────────────

WeeklySummary Database::createSummary(int userId, const WeeklySummary& summary) {
    std::string ts = now();
    std::string catBreakdown = summary.category_breakdown.dump();
    std::string completedTasks = summary.completed_tasks.dump();
    std::string incompleteTasks = summary.incomplete_tasks.dump();

    int id = 0;
    if (useAzureSql_) {
        // Azure SQL: UPDATE-then-INSERT
        int affected = backend_->execute(
            "UPDATE weekly_summaries SET total_planned=?, total_completed=?, total_actual=?, "
            "tasks_planned=?, tasks_completed=?, tasks_carried_over=?, "
            "category_breakdown=?, completed_tasks=?, incomplete_tasks=?, created_at=? "
            "WHERE user_id = ? AND week_date = ?",
            {Param::Int(summary.total_planned), Param::Int(summary.total_completed),
             Param::Int(summary.total_actual), Param::Int(summary.tasks_planned),
             Param::Int(summary.tasks_completed), Param::Int(summary.tasks_carried_over),
             Param::Text(catBreakdown), Param::Text(completedTasks),
             Param::Text(incompleteTasks), Param::Text(ts),
             Param::Int(userId), Param::Text(summary.week_date)});

        if (affected > 0) {
            backend_->query(
                "SELECT id FROM weekly_summaries WHERE user_id = ? AND week_date = ?",
                {Param::Int(userId), Param::Text(summary.week_date)},
                [&](const Row& r) { id = r.getInt(0); });
        } else {
            id = backend_->insertReturningId(
                "INSERT INTO weekly_summaries (user_id, week_date, total_planned, total_completed, "
                "total_actual, tasks_planned, tasks_completed, tasks_carried_over, "
                "category_breakdown, completed_tasks, incomplete_tasks, created_at) "
                "OUTPUT INSERTED.id VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {Param::Int(userId), Param::Text(summary.week_date),
                 Param::Int(summary.total_planned), Param::Int(summary.total_completed),
                 Param::Int(summary.total_actual), Param::Int(summary.tasks_planned),
                 Param::Int(summary.tasks_completed), Param::Int(summary.tasks_carried_over),
                 Param::Text(catBreakdown), Param::Text(completedTasks),
                 Param::Text(incompleteTasks), Param::Text(ts)});
        }
    } else {
        // SQLite: INSERT OR REPLACE
        id = backend_->insertReturningId(
            "INSERT OR REPLACE INTO weekly_summaries "
            "(user_id, week_date, total_planned, total_completed, total_actual, "
            "tasks_planned, tasks_completed, tasks_carried_over, "
            "category_breakdown, completed_tasks, incomplete_tasks, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            {Param::Int(userId), Param::Text(summary.week_date),
             Param::Int(summary.total_planned), Param::Int(summary.total_completed),
             Param::Int(summary.total_actual), Param::Int(summary.tasks_planned),
             Param::Int(summary.tasks_completed), Param::Int(summary.tasks_carried_over),
             Param::Text(catBreakdown), Param::Text(completedTasks),
             Param::Text(incompleteTasks), Param::Text(ts)});
    }

    WeeklySummary result = summary;
    result.id = id;
    result.created_at = ts;
    return result;
}

std::optional<WeeklySummary> Database::getSummary(int userId, const std::string& weekDate) {
    std::optional<WeeklySummary> result;
    backend_->query(
        "SELECT id, week_date, total_planned, total_completed, total_actual, "
        "tasks_planned, tasks_completed, tasks_carried_over, "
        "category_breakdown, completed_tasks, incomplete_tasks, created_at "
        "FROM weekly_summaries WHERE user_id = ? AND week_date = ?",
        {Param::Int(userId), Param::Text(weekDate)},
        [&](const Row& r) {
            WeeklySummary s;
            s.id = r.getInt(0);
            s.week_date = r.getText(1);
            s.total_planned = r.getInt(2);
            s.total_completed = r.getInt(3);
            s.total_actual = r.getInt(4);
            s.tasks_planned = r.getInt(5);
            s.tasks_completed = r.getInt(6);
            s.tasks_carried_over = r.getInt(7);
            s.category_breakdown = nlohmann::json::parse(r.getText(8));
            s.completed_tasks = nlohmann::json::parse(r.getText(9));
            s.incomplete_tasks = nlohmann::json::parse(r.getText(10));
            s.created_at = r.getText(11);
            result = s;
        });
    return result;
}

std::vector<WeeklySummary> Database::getAllSummaries(int userId) {
    std::vector<WeeklySummary> summaries;
    backend_->query(
        "SELECT id, week_date, total_planned, total_completed, total_actual, "
        "tasks_planned, tasks_completed, tasks_carried_over, "
        "category_breakdown, completed_tasks, incomplete_tasks, created_at "
        "FROM weekly_summaries WHERE user_id = ? ORDER BY week_date DESC",
        {Param::Int(userId)},
        [&](const Row& r) {
            WeeklySummary s;
            s.id = r.getInt(0);
            s.week_date = r.getText(1);
            s.total_planned = r.getInt(2);
            s.total_completed = r.getInt(3);
            s.total_actual = r.getInt(4);
            s.tasks_planned = r.getInt(5);
            s.tasks_completed = r.getInt(6);
            s.tasks_carried_over = r.getInt(7);
            s.category_breakdown = nlohmann::json::parse(r.getText(8));
            s.completed_tasks = nlohmann::json::parse(r.getText(9));
            s.incomplete_tasks = nlohmann::json::parse(r.getText(10));
            s.created_at = r.getText(11);
            summaries.push_back(s);
        });
    return summaries;
}

// ── Categories ─────────────────────────────────────────────────────────

std::vector<std::string> Database::getCategories(int userId) {
    std::vector<std::string> cats;
    backend_->query(
        "SELECT DISTINCT category FROM tasks WHERE user_id = ? AND category <> '' ORDER BY category",
        {Param::Int(userId)},
        [&](const Row& r) { cats.push_back(r.getText(0)); });
    return cats;
}
