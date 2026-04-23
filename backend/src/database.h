#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include "models.h"
#include "db_backend.h"

class Database {
public:
    // Reads AZURE_SQL_CONNECTION_STRING env var. If set, uses ODBC. Otherwise SQLite.
    explicit Database(const std::string& sqlitePath = "autoplanner.db");
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Users
    User getOrCreateUser(const std::string& provider, const std::string& providerId,
                         const std::string& email, const std::string& name,
                         const std::string& avatarUrl);
    std::optional<User> getUserById(int id);

    // Sessions
    std::string createSession(int userId);
    std::optional<int> validateSession(const std::string& token);
    void deleteSession(const std::string& token);

    // Tasks
    Task createTask(int userId, const Task& task);
    std::optional<Task> getTask(int userId, int id);
    std::vector<Task> getAllTasks(int userId, const std::string& status = "",
                                  const std::string& category = "",
                                  bool includeArchived = false);
    bool updateTask(int userId, int id, const Task& task);
    bool deleteTask(int userId, int id);

    void recalcEstimate(int taskId);

    // Plans
    Plan createPlan(int userId, const Plan& plan);
    std::optional<Plan> getPlanByTypeAndDate(int userId, const std::string& type,
                                             const std::string& date);
    bool deletePlan(int userId, int id);
    bool markPlanReviewed(int userId, int id);
    std::vector<Plan> getUnreviewedDailyPlans(int userId, const std::string& beforeDate,
                                               const std::string& monday);

    // Weekly summaries
    WeeklySummary createSummary(int userId, const WeeklySummary& summary);
    std::optional<WeeklySummary> getSummary(int userId, const std::string& weekDate);
    std::vector<WeeklySummary> getAllSummaries(int userId);

    // Productivity logs
    ProductivityLog createLog(int userId, const ProductivityLog& log);
    std::vector<ProductivityLog> getLogsByTaskId(int userId, int taskId);

    // Categories
    std::vector<std::string> getCategories(int userId);

private:
    std::unique_ptr<DbBackend> backend_;
    bool useAzureSql_ = false;

    void initTables();
    std::string now();

    // SQL helpers that differ between backends
    std::string insertTaskSql() const;
    std::string insertPlanSql() const;
    std::string insertSummarySql() const;
    std::string insertLogSql() const;
    std::string insertUserSql() const;
    std::string insertSessionSql() const;
};
