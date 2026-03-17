#ifdef HAS_AZURE_SQL

#include "azuresql_backend.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// Truncate long strings for logging
static std::string truncate(const std::string& s, size_t maxLen = 80) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "...(" + std::to_string(s.size()) + " chars)";
}

static std::string paramsToString(const std::vector<Param>& params) {
    std::string result;
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) result += ", ";
        result += "?" + std::to_string(i + 1) + "=";
        switch (params[i].type) {
            case Param::INT:
                result += std::to_string(params[i].intVal);
                break;
            case Param::TEXT:
                result += "'" + truncate(params[i].textVal, 50) + "'[" +
                          std::to_string(params[i].textVal.size()) + "B]";
                break;
        }
    }
    return result;
}

AzureSqlBackend::AzureSqlBackend(const std::string& connectionString) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);
    SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);

    SQLRETURN ret = SQLDriverConnect(hdbc_, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(connectionString.c_str())),
        SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);

    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::string err = getOdbcError(SQL_HANDLE_DBC, hdbc_);
        throw std::runtime_error("ODBC connection failed: " + err);
    }

    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
}

AzureSqlBackend::~AzureSqlBackend() {
    if (hdbc_ != SQL_NULL_HDBC) {
        SQLDisconnect(hdbc_);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc_);
    }
    if (henv_ != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, henv_);
    }
}

std::string AzureSqlBackend::getOdbcError(SQLSMALLINT handleType, SQLHANDLE handle) {
    SQLCHAR state[6] = {}, msg[1024] = {};
    SQLINTEGER native = 0;
    SQLSMALLINT len = 0;
    SQLGetDiagRec(handleType, handle, 1, state, &native, msg, sizeof(msg), &len);
    return std::string(reinterpret_cast<char*>(state)) + ": " +
           std::string(reinterpret_cast<char*>(msg),
                       static_cast<size_t>(std::max(static_cast<SQLSMALLINT>(0), len)));
}

void AzureSqlBackend::checkReturn(SQLRETURN ret, SQLSMALLINT handleType, SQLHANDLE handle,
                                   const std::string& context) {
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(handleType, handle);
        throw std::runtime_error("ODBC " + context + ": " + err);
    }
}

SQLHSTMT AzureSqlBackend::prepareAndBind(const std::string& sql, const std::vector<Param>& params,
                                          std::vector<SQLLEN>& lenBuf) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);

    SQLRETURN ret = SQLPrepare(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())),
                                SQL_NTS);
    checkReturn(ret, SQL_HANDLE_STMT, hstmt, "prepare");

    // Pre-allocate length indicators so pointers remain valid until execution
    lenBuf.resize(params.size(), 0);

    for (size_t i = 0; i < params.size(); i++) {
        SQLUSMALLINT idx = static_cast<SQLUSMALLINT>(i + 1);
        switch (params[i].type) {
            case Param::INT: {
                auto* intPtr = const_cast<int*>(&params[i].intVal);
                lenBuf[i] = 0;
                SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_SLONG,
                                SQL_INTEGER, 0, 0, intPtr, 0, &lenBuf[i]);
                break;
            }
            case Param::TEXT: {
                auto* textPtr = const_cast<char*>(params[i].textVal.c_str());
                lenBuf[i] = static_cast<SQLLEN>(params[i].textVal.size());
                // Use SQL_WVARCHAR to match NVARCHAR columns on Azure SQL
                SQLULEN colSize = std::max(static_cast<SQLULEN>(lenBuf[i] + 1),
                                           static_cast<SQLULEN>(4000));
                SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_CHAR,
                                SQL_WVARCHAR, colSize, 0,
                                textPtr, lenBuf[i] + 1, &lenBuf[i]);
                break;
            }
        }
    }
    return hstmt;
}

void AzureSqlBackend::exec(const std::string& sql) {
    spdlog::debug("[SQL] exec: {}", truncate(sql, 120));
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    SQLRETURN ret = SQLExecDirect(hstmt,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        spdlog::error("[SQL] exec failed: {} | sql={}", err, truncate(sql, 200));
        throw std::runtime_error("ODBC exec error: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void AzureSqlBackend::query(const std::string& sql, const std::vector<Param>& params,
                             RowCallback callback) {
    spdlog::debug("[SQL] query: {} | params: {}", truncate(sql, 120), paramsToString(params));
    std::vector<SQLLEN> lenBuf;
    SQLHSTMT hstmt = prepareAndBind(sql, params, lenBuf);
    SQLRETURN ret = SQLExecute(hstmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        spdlog::error("[SQL] query failed: {} | sql={} | params: {}", err, truncate(sql, 200), paramsToString(params));
        throw std::runtime_error("ODBC query execute: " + err);
    }

    OdbcRow row(hstmt);
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        callback(row);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

int AzureSqlBackend::execute(const std::string& sql, const std::vector<Param>& params) {
    spdlog::debug("[SQL] execute: {} | params: {}", truncate(sql, 120), paramsToString(params));
    std::vector<SQLLEN> lenBuf;
    SQLHSTMT hstmt = prepareAndBind(sql, params, lenBuf);
    SQLRETURN ret = SQLExecute(hstmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        spdlog::error("[SQL] execute failed: {} | sql={} | params: {}", err, truncate(sql, 200), paramsToString(params));
        throw std::runtime_error("ODBC execute: " + err);
    }

    SQLLEN rowCount = 0;
    SQLRowCount(hstmt, &rowCount);
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return static_cast<int>(rowCount);
}

int AzureSqlBackend::insertReturningId(const std::string& sql, const std::vector<Param>& params) {
    spdlog::debug("[SQL] insert: {} | params: {}", truncate(sql, 120), paramsToString(params));
    std::vector<SQLLEN> lenBuf;
    SQLHSTMT hstmt = prepareAndBind(sql, params, lenBuf);
    SQLRETURN ret = SQLExecute(hstmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        spdlog::error("[SQL] insert failed: {} | sql={} | params: {}", err, truncate(sql, 200), paramsToString(params));
        throw std::runtime_error("ODBC insertReturningId: " + err);
    }

    int id = 0;
    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &id, sizeof(id), nullptr);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    spdlog::debug("[SQL] insert returned id={}", id);
    return id;
}

void AzureSqlBackend::beginTransaction() {
    spdlog::debug("[SQL] BEGIN TRANSACTION");
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
}

void AzureSqlBackend::commit() {
    spdlog::debug("[SQL] COMMIT");
    SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
}

void AzureSqlBackend::rollback() {
    spdlog::warn("[SQL] ROLLBACK");
    SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
}

#endif // HAS_AZURE_SQL
