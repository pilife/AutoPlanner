#ifdef HAS_AZURE_SQL

#include "azuresql_backend.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>

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

    // Enable autocommit by default
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

SQLHSTMT AzureSqlBackend::prepareAndBind(const std::string& sql, const std::vector<Param>& params) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);

    SQLRETURN ret = SQLPrepare(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())),
                                SQL_NTS);
    checkReturn(ret, SQL_HANDLE_STMT, hstmt, "prepare");

    for (size_t i = 0; i < params.size(); i++) {
        SQLUSMALLINT idx = static_cast<SQLUSMALLINT>(i + 1);
        switch (params[i].type) {
            case Param::INT: {
                // Need stable storage for the integer value during execution
                // We use SQLBindParameter with SQL_PARAM_INPUT
                auto* intPtr = const_cast<int*>(&params[i].intVal);
                SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_SLONG,
                                SQL_INTEGER, 0, 0, intPtr, 0, nullptr);
                break;
            }
            case Param::TEXT: {
                auto* textPtr = const_cast<char*>(params[i].textVal.c_str());
                SQLLEN textLen = static_cast<SQLLEN>(params[i].textVal.size());
                SQLLEN* lenPtr = const_cast<SQLLEN*>(&textLen);
                SQLBindParameter(hstmt, idx, SQL_PARAM_INPUT, SQL_C_CHAR,
                                SQL_VARCHAR, params[i].textVal.size() + 1, 0,
                                textPtr, textLen + 1, lenPtr);
                break;
            }
        }
    }
    return hstmt;
}

void AzureSqlBackend::exec(const std::string& sql) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
    SQLRETURN ret = SQLExecDirect(hstmt,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
    // Allow SQL_NO_DATA (e.g., for IF NOT EXISTS that does nothing)
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO && ret != SQL_NO_DATA) {
        std::string err = getOdbcError(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        throw std::runtime_error("ODBC exec error: " + err);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void AzureSqlBackend::query(const std::string& sql, const std::vector<Param>& params,
                             RowCallback callback) {
    SQLHSTMT hstmt = prepareAndBind(sql, params);
    SQLRETURN ret = SQLExecute(hstmt);
    checkReturn(ret, SQL_HANDLE_STMT, hstmt, "query execute");

    OdbcRow row(hstmt);
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        callback(row);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

int AzureSqlBackend::execute(const std::string& sql, const std::vector<Param>& params) {
    SQLHSTMT hstmt = prepareAndBind(sql, params);
    SQLRETURN ret = SQLExecute(hstmt);
    checkReturn(ret, SQL_HANDLE_STMT, hstmt, "execute");

    SQLLEN rowCount = 0;
    SQLRowCount(hstmt, &rowCount);
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return static_cast<int>(rowCount);
}

int AzureSqlBackend::insertReturningId(const std::string& sql, const std::vector<Param>& params) {
    // The SQL should contain OUTPUT INSERTED.id for Azure SQL
    SQLHSTMT hstmt = prepareAndBind(sql, params);
    SQLRETURN ret = SQLExecute(hstmt);
    checkReturn(ret, SQL_HANDLE_STMT, hstmt, "insertReturningId");

    int id = 0;
    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &id, sizeof(id), nullptr);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return id;
}

void AzureSqlBackend::beginTransaction() {
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
}

void AzureSqlBackend::commit() {
    SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
}

void AzureSqlBackend::rollback() {
    SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                      reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
}

#endif // HAS_AZURE_SQL
