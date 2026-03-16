#include "sqlite_backend.h"
#include <stdexcept>

SqliteBackend::SqliteBackend(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
}

SqliteBackend::~SqliteBackend() {
    if (db_) sqlite3_close(db_);
}

void SqliteBackend::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQLite exec error: " + msg);
    }
}

void SqliteBackend::bindParams(sqlite3_stmt* stmt, const std::vector<Param>& params) {
    for (size_t i = 0; i < params.size(); i++) {
        int idx = static_cast<int>(i) + 1;
        switch (params[i].type) {
            case Param::INT:
                sqlite3_bind_int(stmt, idx, params[i].intVal);
                break;
            case Param::TEXT:
                sqlite3_bind_text(stmt, idx, params[i].textVal.c_str(), -1, SQLITE_TRANSIENT);
                break;
        }
    }
}

void SqliteBackend::query(const std::string& sql, const std::vector<Param>& params,
                           RowCallback callback) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("SQLite prepare error: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    bindParams(stmt, params);
    SqliteRow row(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        callback(row);
    }
    sqlite3_finalize(stmt);
}

int SqliteBackend::execute(const std::string& sql, const std::vector<Param>& params) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("SQLite prepare error: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    bindParams(stmt, params);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("SQLite execute error: " + msg);
    }
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changes;
}

int SqliteBackend::insertReturningId(const std::string& sql, const std::vector<Param>& params) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("SQLite prepare error: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
    bindParams(stmt, params);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("SQLite insert error: " + msg);
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);
    return id;
}

void SqliteBackend::beginTransaction() { exec("BEGIN TRANSACTION;"); }
void SqliteBackend::commit() { exec("COMMIT;"); }
void SqliteBackend::rollback() { exec("ROLLBACK;"); }
