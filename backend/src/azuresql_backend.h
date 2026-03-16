#pragma once

#ifdef HAS_AZURE_SQL

#include "db_backend.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

class OdbcRow : public Row {
public:
    explicit OdbcRow(SQLHSTMT stmt) : stmt_(stmt) {}
    int getInt(int col) const override {
        SQLINTEGER val = 0;
        SQLGetData(stmt_, static_cast<SQLUSMALLINT>(col + 1), SQL_C_SLONG, &val, sizeof(val), nullptr);
        return static_cast<int>(val);
    }
    std::string getText(int col) const override {
        char buf[4096] = {};
        SQLLEN len = 0;
        SQLGetData(stmt_, static_cast<SQLUSMALLINT>(col + 1), SQL_C_CHAR, buf, sizeof(buf), &len);
        if (len == SQL_NULL_DATA || len < 0) return "";
        return std::string(buf, std::min(static_cast<size_t>(len), sizeof(buf) - 1));
    }
private:
    SQLHSTMT stmt_;
};

class AzureSqlBackend : public DbBackend {
public:
    explicit AzureSqlBackend(const std::string& connectionString);
    ~AzureSqlBackend() override;

    void exec(const std::string& sql) override;
    void query(const std::string& sql, const std::vector<Param>& params,
               RowCallback callback) override;
    int execute(const std::string& sql, const std::vector<Param>& params) override;
    int insertReturningId(const std::string& sql, const std::vector<Param>& params) override;
    void beginTransaction() override;
    void commit() override;
    void rollback() override;

private:
    SQLHENV henv_ = SQL_NULL_HENV;
    SQLHDBC hdbc_ = SQL_NULL_HDBC;

    SQLHSTMT prepareAndBind(const std::string& sql, const std::vector<Param>& params);
    std::string getOdbcError(SQLSMALLINT handleType, SQLHANDLE handle);
    void checkReturn(SQLRETURN ret, SQLSMALLINT handleType, SQLHANDLE handle,
                     const std::string& context);
};

#endif // HAS_AZURE_SQL
