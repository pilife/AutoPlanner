#include "database.h"
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
        CREATE TABLE IF NOT EXISTS tasks (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
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
            type       TEXT NOT NULL,
            date       TEXT NOT NULL,
            items_json TEXT DEFAULT '[]',
            created_at TEXT NOT NULL,
            UNIQUE(type, date)
        );

        CREATE TABLE IF NOT EXISTS productivity_logs (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            task_id    INTEGER NOT NULL,
            start_time TEXT NOT NULL,
            end_time   TEXT DEFAULT '',
            notes      TEXT DEFAULT '',
            FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE
        );
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

// ── Tasks ──────────────────────────────────────────────────────────────

static Task rowToTask(sqlite3_stmt* stmt) {
    Task t;
    t.id                = sqlite3_column_int(stmt, 0);
    t.parent_id         = sqlite3_column_int(stmt, 1);
    t.title             = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    t.description       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    t.priority          = sqlite3_column_int(stmt, 4);
    t.estimated_minutes = sqlite3_column_int(stmt, 5);
    t.actual_minutes    = sqlite3_column_int(stmt, 6);
    t.category          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    t.status            = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    t.due_date          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    t.created_at        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    t.updated_at        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    return t;
}

Task Database::createTask(const Task& task) {
    std::string ts = now();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tasks "
        "(parent_id, title, description, priority, estimated_minutes, actual_minutes, "
        "category, status, due_date, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

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
    sqlite3_bind_text(stmt, 11, ts.c_str(), -1, SQLITE_TRANSIENT);

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

std::optional<Task> Database::getTask(int id) {
    std::optional<Task> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT * FROM tasks WHERE id = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToTask(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Task> Database::getAllTasks(const std::string& status,
                                        const std::string& category) {
    std::vector<Task> tasks;
    std::string sql = "SELECT * FROM tasks WHERE 1=1";
    if (!status.empty())   sql += " AND status = '" + status + "'";
    if (!category.empty()) sql += " AND category = '" + category + "'";
    sql += " ORDER BY priority ASC, due_date ASC";

    query(sql, [&](sqlite3_stmt* stmt) {
        tasks.push_back(rowToTask(stmt));
    });
    return tasks;
}

bool Database::updateTask(int id, const Task& task) {
    std::string ts = now();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE tasks SET "
        "parent_id=?, title=?, description=?, priority=?, estimated_minutes=?, "
        "actual_minutes=?, category=?, status=?, due_date=?, updated_at=? "
        "WHERE id=?";

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

    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteTask(int id) {
    // First get the parent_id before deleting
    auto task = getTask(id);
    int parentId = task ? task->parent_id : 0;

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM tasks WHERE id = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);

    // Also delete all descendant tasks
    if (ok) {
        // Delete children recursively (SQLite foreign key cascade won't help
        // since parent_id is not a foreign key constraint)
        std::vector<int> childIds;
        query("SELECT id FROM tasks WHERE parent_id = " + std::to_string(id),
              [&](sqlite3_stmt* s) { childIds.push_back(sqlite3_column_int(s, 0)); });
        for (int cid : childIds) {
            deleteTask(cid);
        }
        // Recalc ancestor estimates
        if (parentId > 0) recalcEstimate(parentId);
    }
    return ok;
}

void Database::recalcEstimate(int taskId) {
    // Sum estimated_minutes of direct children
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

    // Only override if this task actually has children
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

    // Recurse up to grandparent
    auto task = getTask(taskId);
    if (task && task->parent_id > 0) {
        recalcEstimate(task->parent_id);
    }
}

// ── Plans ──────────────────────────────────────────────────────────────

Plan Database::createPlan(const Plan& plan) {
    std::string ts = now();
    nlohmann::json itemsJson = plan.items;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO plans (type, date, items_json, created_at) "
                      "VALUES (?, ?, ?, ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, plan.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, plan.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, itemsJson.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ts.c_str(), -1, SQLITE_TRANSIENT);

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

std::optional<Plan> Database::getPlanByTypeAndDate(const std::string& type,
                                                    const std::string& date) {
    std::optional<Plan> result;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, type, date, items_json, created_at FROM plans "
        "WHERE type = ? AND date = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Plan p;
        p.id = sqlite3_column_int(stmt, 0);
        p.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto itemsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        p.items = nlohmann::json::parse(itemsStr).get<std::vector<PlanItem>>();
        p.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result = p;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::deletePlan(int id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM plans WHERE id = ?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

// ── Productivity Logs ──────────────────────────────────────────────────

ProductivityLog Database::createLog(const ProductivityLog& log) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO productivity_logs "
        "(task_id, start_time, end_time, notes) VALUES (?, ?, ?, ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, log.task_id);
    sqlite3_bind_text(stmt, 2, log.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, log.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, log.notes.c_str(), -1, SQLITE_TRANSIENT);

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

std::vector<ProductivityLog> Database::getLogsByTaskId(int taskId) {
    std::vector<ProductivityLog> logs;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id, task_id, start_time, end_time, notes "
        "FROM productivity_logs WHERE task_id = ? ORDER BY start_time DESC",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, taskId);

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

// ── Categories ─────────────────────────────────────────────────────────

std::vector<std::string> Database::getCategories() {
    std::vector<std::string> cats;
    query("SELECT DISTINCT category FROM tasks WHERE category != '' ORDER BY category",
          [&](sqlite3_stmt* stmt) {
              cats.emplace_back(
                  reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
          });
    return cats;
}
