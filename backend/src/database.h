#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <sqlite3.h>
#include "models.h"

class Database {
public:
    explicit Database(const std::string& path);
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
                                  const std::string& category = "");
    bool updateTask(int userId, int id, const Task& task);
    bool deleteTask(int userId, int id);

    // Recompute parent estimated_minutes as sum of children (recursive up)
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
    sqlite3* db_ = nullptr;

    void initTables();
    void exec(const std::string& sql);
    void query(const std::string& sql,
               const std::function<void(sqlite3_stmt*)>& rowCallback);

    std::string now();
};
