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

    // Tasks
    Task createTask(const Task& task);
    std::optional<Task> getTask(int id);
    std::vector<Task> getAllTasks(const std::string& status = "",
                                  const std::string& category = "");
    bool updateTask(int id, const Task& task);
    bool deleteTask(int id);

    // Recompute parent estimated_minutes as sum of children (recursive up)
    void recalcEstimate(int taskId);

    // Plans
    Plan createPlan(const Plan& plan);
    std::optional<Plan> getPlanByTypeAndDate(const std::string& type,
                                             const std::string& date);
    bool deletePlan(int id);
    bool markPlanReviewed(int id);
    std::vector<Plan> getUnreviewedDailyPlans(const std::string& beforeDate,
                                               const std::string& monday);

    // Weekly summaries
    WeeklySummary createSummary(const WeeklySummary& summary);
    std::optional<WeeklySummary> getSummary(const std::string& weekDate);
    std::vector<WeeklySummary> getAllSummaries();

    // Productivity logs
    ProductivityLog createLog(const ProductivityLog& log);
    std::vector<ProductivityLog> getLogsByTaskId(int taskId);

    // Categories
    std::vector<std::string> getCategories();

private:
    sqlite3* db_ = nullptr;

    void initTables();
    void exec(const std::string& sql);
    void query(const std::string& sql,
               const std::function<void(sqlite3_stmt*)>& rowCallback);

    std::string now();
};
