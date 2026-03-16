#pragma once

#include <string>
#include <vector>
#include <functional>
#include <variant>
#include <stdexcept>

// Parameter type for prepared statements
struct Param {
    enum Type { INT, TEXT } type;
    int intVal = 0;
    std::string textVal;

    static Param Int(int v) { Param p; p.type = INT; p.intVal = v; return p; }
    static Param Text(const std::string& v) { Param p; p.type = TEXT; p.textVal = v; return p; }
};

// Abstract row reader — allows backend-agnostic column access
class Row {
public:
    virtual ~Row() = default;
    virtual int getInt(int col) const = 0;
    virtual std::string getText(int col) const = 0;
};

using RowCallback = std::function<void(const Row&)>;

// Abstract database backend interface
class DbBackend {
public:
    virtual ~DbBackend() = default;

    // Execute DDL/DML with no parameters (for schema setup)
    virtual void exec(const std::string& sql) = 0;

    // Execute a parameterized SELECT, calling callback for each row
    virtual void query(const std::string& sql, const std::vector<Param>& params,
                       RowCallback callback) = 0;

    // Execute a parameterized DML, return rows affected
    virtual int execute(const std::string& sql, const std::vector<Param>& params) = 0;

    // Execute INSERT and return the generated ID
    virtual int insertReturningId(const std::string& sql, const std::vector<Param>& params) = 0;

    // Transaction support
    virtual void beginTransaction() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;
};
