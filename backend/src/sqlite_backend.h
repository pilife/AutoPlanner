#pragma once

#include "db_backend.h"
#include <sqlite3.h>
#include <string>

class SqliteRow : public Row {
public:
    explicit SqliteRow(sqlite3_stmt* stmt) : stmt_(stmt) {}
    int getInt(int col) const override { return sqlite3_column_int(stmt_, col); }
    std::string getText(int col) const override {
        auto p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
        return p ? p : "";
    }
private:
    sqlite3_stmt* stmt_;
};

class SqliteBackend : public DbBackend {
public:
    explicit SqliteBackend(const std::string& path);
    ~SqliteBackend() override;

    void exec(const std::string& sql) override;
    void query(const std::string& sql, const std::vector<Param>& params,
               RowCallback callback) override;
    int execute(const std::string& sql, const std::vector<Param>& params) override;
    int insertReturningId(const std::string& sql, const std::vector<Param>& params) override;
    void beginTransaction() override;
    void commit() override;
    void rollback() override;

private:
    sqlite3* db_ = nullptr;
    void bindParams(sqlite3_stmt* stmt, const std::vector<Param>& params);
};
