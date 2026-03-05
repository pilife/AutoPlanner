#include "routes.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

using json = nlohmann::json;

static void jsonResponse(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

static void errorResponse(httplib::Response& res, int status, const std::string& msg) {
    jsonResponse(res, status, {{"error", msg}});
}

// Get Monday (YYYY-MM-DD) of the week containing the given date
static std::string getMondayOfWeek(const std::string& dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    std::mktime(&tm);
    int daysSinceMonday = (tm.tm_wday + 6) % 7;
    tm.tm_mday -= daysSinceMonday;
    std::mktime(&tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

// Get Mon-Fri date strings for a week starting at monday
static std::vector<std::string> getWeekDays(const std::string& monday) {
    std::vector<std::string> days;
    std::tm tm = {};
    std::istringstream ss(monday);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    std::mktime(&tm);
    for (int i = 0; i < 5; i++) {
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        days.push_back(oss.str());
        tm.tm_mday++;
        std::mktime(&tm);
    }
    return days;
}

static std::string todayStr() {
    auto t = std::time(nullptr);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d");
    return oss.str();
}

// Get remaining weekdays from startDate through Friday of that week
static std::vector<std::string> getRemainingWeekDays(const std::string& monday,
                                                      const std::string& startDate) {
    auto allDays = getWeekDays(monday);
    std::vector<std::string> remaining;
    for (const auto& d : allDays) {
        if (d >= startDate) remaining.push_back(d);
    }
    return remaining;
}

void registerRoutes(httplib::Server& server, Database& db) {

    // ── Tasks ──────────────────────────────────────────────────────────

    server.Get("/api/tasks", [&](const httplib::Request& req, httplib::Response& res) {
        std::string status   = req.get_param_value("status");
        std::string category = req.get_param_value("category");
        auto tasks = db.getAllTasks(status, category);
        jsonResponse(res, 200, tasks);
    });

    server.Get(R"(/api/tasks/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        auto task = db.getTask(id);
        if (task) {
            jsonResponse(res, 200, *task);
        } else {
            errorResponse(res, 404, "Task not found");
        }
    });

    server.Post("/api/tasks", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Task t = body.get<Task>();
            if (t.title.empty()) {
                errorResponse(res, 400, "Title is required");
                return;
            }
            auto created = db.createTask(t);
            // Recompute parent's estimated_minutes from children
            if (created.parent_id > 0) {
                db.recalcEstimate(created.parent_id);
            }
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    server.Put(R"(/api/tasks/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto existing = db.getTask(id);
            if (!existing) {
                errorResponse(res, 404, "Task not found");
                return;
            }
            auto body = json::parse(req.body);
            Task t = *existing;
            int oldParentId = t.parent_id;

            // Merge fields from body
            if (body.contains("parent_id"))         t.parent_id = body["parent_id"];
            if (body.contains("title"))             t.title = body["title"];
            if (body.contains("description"))       t.description = body["description"];
            if (body.contains("priority"))          t.priority = body["priority"];
            if (body.contains("estimated_minutes")) t.estimated_minutes = body["estimated_minutes"];
            if (body.contains("actual_minutes"))    t.actual_minutes = body["actual_minutes"];
            if (body.contains("category"))          t.category = body["category"];
            if (body.contains("status"))            t.status = body["status"];
            if (body.contains("due_date"))          t.due_date = body["due_date"];

            if (db.updateTask(id, t)) {
                // Recompute estimates for old and new parents
                if (oldParentId > 0) db.recalcEstimate(oldParentId);
                if (t.parent_id > 0 && t.parent_id != oldParentId)
                    db.recalcEstimate(t.parent_id);
                // Also recompute self if it has children
                db.recalcEstimate(id);

                auto updated = db.getTask(id);
                jsonResponse(res, 200, *updated);
            } else {
                errorResponse(res, 500, "Failed to update task");
            }
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    server.Delete(R"(/api/tasks/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        if (db.deleteTask(id)) {
            jsonResponse(res, 200, {{"deleted", true}});
        } else {
            errorResponse(res, 404, "Task not found");
        }
    });

    // ── Plans ──────────────────────────────────────────────────────────

    server.Get("/api/plans", [&](const httplib::Request& req, httplib::Response& res) {
        std::string type = req.get_param_value("type");
        std::string date = req.get_param_value("date");
        if (type.empty() || date.empty()) {
            errorResponse(res, 400, "Both 'type' and 'date' query params required");
            return;
        }
        // For weekly, normalize date to Monday
        std::string lookupDate = (type == "weekly") ? getMondayOfWeek(date) : date;
        auto plan = db.getPlanByTypeAndDate(type, lookupDate);
        if (plan) {
            jsonResponse(res, 200, *plan);
        } else {
            jsonResponse(res, 200, json::object());
        }
    });

    server.Post("/api/plans", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Plan p = body.get<Plan>();
            if (p.type.empty() || p.date.empty()) {
                errorResponse(res, 400, "type and date are required");
                return;
            }
            // Normalize weekly date to Monday
            if (p.type == "weekly") p.date = getMondayOfWeek(p.date);
            auto created = db.createPlan(p);
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    // Generate weekly plan: collect all incomplete leaf tasks sorted by priority
    server.Post("/api/plans/generate-weekly", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);

            // Get all tasks (including done ones for the weekly record)
            auto allTasks = db.getAllTasks("", "");
            // Filter to leaf tasks (no children)
            std::vector<Task> leafTasks;
            for (const auto& t : allTasks) {
                // Check if this task has children
                bool hasChildren = false;
                for (const auto& other : allTasks) {
                    if (other.parent_id == t.id) {
                        hasChildren = true;
                        break;
                    }
                }
                if (!hasChildren) leafTasks.push_back(t);
            }

            // Sort by priority (ascending = most important first)
            std::sort(leafTasks.begin(), leafTasks.end(),
                [](const Task& a, const Task& b) { return a.priority < b.priority; });

            std::vector<PlanItem> items;
            for (const auto& t : leafTasks) {
                PlanItem item;
                item.task_id = t.id;
                item.scheduled_time = ""; // weekly items don't have specific times
                item.duration_minutes = t.estimated_minutes;
                items.push_back(item);
            }

            Plan plan;
            plan.type = "weekly";
            plan.date = monday;
            plan.items = items;
            auto saved = db.createPlan(plan);

            json result = saved;
            result["generated"] = true;
            jsonResponse(res, 201, result);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    // Generate daily plans from weekly plan: distributes incomplete tasks
    // across remaining weekdays (from max(monday, today) through Friday).
    // Returns plans + warning if overloaded.
    server.Post("/api/plans/generate-daily", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);
            std::string today = todayStr();
            std::string startDate = (today > monday) ? today : monday;

            // Load the weekly plan
            auto weeklyPlan = db.getPlanByTypeAndDate("weekly", monday);
            if (!weeklyPlan || weeklyPlan->items.empty()) {
                errorResponse(res, 400, "No weekly plan found. Generate a weekly plan first.");
                return;
            }

            // Filter out completed tasks
            std::vector<PlanItem> incompleteItems;
            for (const auto& item : weeklyPlan->items) {
                auto task = db.getTask(item.task_id);
                if (task && task->status != "done") {
                    incompleteItems.push_back(item);
                }
            }

            auto remainingDays = getRemainingWeekDays(monday, startDate);
            int numDays = static_cast<int>(remainingDays.size());
            if (numDays == 0) {
                errorResponse(res, 400, "No remaining weekdays this week.");
                return;
            }

            int dailyCapacity = 480; // 8 hours per day
            int totalRemaining = 0;
            for (const auto& item : incompleteItems)
                totalRemaining += item.duration_minutes;
            int totalCapacity = numDays * dailyCapacity;

            // Distribute across remaining days
            std::vector<std::vector<PlanItem>> dailyItems(numDays);
            std::vector<int> dayMinutes(numDays, 0);
            int currentDay = 0;

            for (const auto& item : incompleteItems) {
                int startDay = currentDay;
                while (currentDay < numDays &&
                       dayMinutes[currentDay] + item.duration_minutes > dailyCapacity) {
                    currentDay++;
                }
                if (currentDay >= numDays) currentDay = numDays - 1; // overflow to last day

                PlanItem scheduled;
                scheduled.task_id = item.task_id;
                scheduled.scheduled_time = "";
                scheduled.duration_minutes = item.duration_minutes;
                dailyItems[currentDay].push_back(scheduled);

                dayMinutes[currentDay] += item.duration_minutes;
                if (currentDay < numDays - 1 &&
                    dayMinutes[currentDay] >= dailyCapacity) {
                    currentDay++;
                }
            }

            // Save daily plans (only for remaining days)
            json plans = json::array();
            for (int i = 0; i < numDays; i++) {
                Plan daily;
                daily.type = "daily";
                daily.date = remainingDays[i];
                daily.items = dailyItems[i];
                auto saved = db.createPlan(daily);
                plans.push_back(saved);
            }

            // Build response with warning info
            json result;
            result["plans"] = plans;
            result["remaining_minutes"] = totalRemaining;
            result["remaining_days"] = numDays;
            result["daily_capacity"] = dailyCapacity;
            result["total_capacity"] = totalCapacity;

            if (totalRemaining > totalCapacity) {
                result["warning"] = "Overloaded: " +
                    std::to_string(totalRemaining - totalCapacity) +
                    " minutes of work exceed remaining capacity. "
                    "Consider adjusting your weekly plan.";
            } else if (totalRemaining > static_cast<int>(totalCapacity * 0.9)) {
                result["warning"] = "Near capacity: workload is over 90% of "
                    "remaining capacity. Consider adjusting priorities.";
            }

            jsonResponse(res, 201, result);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    // Get unreviewed daily plans before a given date (within same week)
    server.Get("/api/plans/unreviewed", [&](const httplib::Request& req, httplib::Response& res) {
        std::string before = req.get_param_value("before");
        if (before.empty()) before = todayStr();
        std::string monday = getMondayOfWeek(before);
        auto plans = db.getUnreviewedDailyPlans(before, monday);
        jsonResponse(res, 200, plans);
    });

    // Review a daily plan: update task statuses and actual times,
    // mark plan reviewed, then regenerate remaining daily plans.
    server.Post("/api/plans/review", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            int planId = body.at("plan_id").get<int>();
            auto taskReviews = body.at("tasks");

            // Update each task
            for (const auto& tr : taskReviews) {
                int taskId = tr.at("task_id").get<int>();
                std::string status = tr.at("status").get<std::string>();
                int actualMinutes = tr.value("actual_minutes", 0);

                auto task = db.getTask(taskId);
                if (task) {
                    Task t = *task;
                    t.status = status;
                    t.actual_minutes = actualMinutes;
                    db.updateTask(taskId, t);
                }
            }

            // Mark plan as reviewed
            db.markPlanReviewed(planId);

            // Get the plan's date to determine week
            auto plan = db.getPlanByTypeAndDate("daily",
                body.value("plan_date", todayStr()));
            // Also try to get plan by id if plan_date not provided
            std::string monday;
            if (body.contains("plan_date")) {
                monday = getMondayOfWeek(body["plan_date"].get<std::string>());
            } else {
                monday = getMondayOfWeek(todayStr());
            }

            // Regenerate remaining daily plans from weekly
            std::string today = todayStr();
            std::string startDate = (today > monday) ? today : monday;
            auto weeklyPlan = db.getPlanByTypeAndDate("weekly", monday);

            json result;
            result["reviewed"] = true;

            if (weeklyPlan && !weeklyPlan->items.empty()) {
                // Filter incomplete tasks from weekly plan
                std::vector<PlanItem> incompleteItems;
                for (const auto& item : weeklyPlan->items) {
                    auto task = db.getTask(item.task_id);
                    if (task && task->status != "done") {
                        incompleteItems.push_back(item);
                    }
                }

                auto remainingDays = getRemainingWeekDays(monday, startDate);
                int numDays = static_cast<int>(remainingDays.size());
                int dailyCapacity = 480;
                int totalRemaining = 0;
                for (const auto& item : incompleteItems)
                    totalRemaining += item.duration_minutes;
                int totalCapacity = numDays * dailyCapacity;

                if (numDays > 0) {
                    std::vector<std::vector<PlanItem>> dailyItems(numDays);
                    std::vector<int> dayMinutes(numDays, 0);
                    int currentDay = 0;

                    for (const auto& item : incompleteItems) {
                        while (currentDay < numDays &&
                               dayMinutes[currentDay] + item.duration_minutes > dailyCapacity) {
                            currentDay++;
                        }
                        if (currentDay >= numDays) currentDay = numDays - 1;

                        PlanItem scheduled;
                        scheduled.task_id = item.task_id;
                        scheduled.scheduled_time = "";
                        scheduled.duration_minutes = item.duration_minutes;
                        dailyItems[currentDay].push_back(scheduled);
                        dayMinutes[currentDay] += item.duration_minutes;

                        if (currentDay < numDays - 1 &&
                            dayMinutes[currentDay] >= dailyCapacity) {
                            currentDay++;
                        }
                    }

                    json plans = json::array();
                    for (int i = 0; i < numDays; i++) {
                        Plan daily;
                        daily.type = "daily";
                        daily.date = remainingDays[i];
                        daily.items = dailyItems[i];
                        auto saved = db.createPlan(daily);
                        plans.push_back(saved);
                    }
                    result["plans"] = plans;
                }

                result["remaining_minutes"] = totalRemaining;
                result["total_capacity"] = numDays * dailyCapacity;

                if (totalRemaining > totalCapacity) {
                    result["warning"] = "Overloaded: " +
                        std::to_string(totalRemaining - totalCapacity) +
                        " minutes of work exceed remaining capacity. "
                        "Consider adjusting your weekly plan.";
                } else if (totalRemaining > static_cast<int>(totalCapacity * 0.9)) {
                    result["warning"] = "Near capacity: workload is over 90% of "
                        "remaining capacity. Consider adjusting priorities.";
                }
            }

            jsonResponse(res, 200, result);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    // ── Weekly Summary ──────────────────────────────────────────────────

    // Generate summary for a week from plan and task data
    server.Post("/api/summaries/generate", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);

            auto weeklyPlan = db.getPlanByTypeAndDate("weekly", monday);
            if (!weeklyPlan || weeklyPlan->items.empty()) {
                errorResponse(res, 400, "No weekly plan found for this week.");
                return;
            }

            WeeklySummary summary;
            summary.week_date = monday;

            std::map<std::string, int> catCompleted;
            std::map<std::string, int> catPlanned;

            // Helper to find root task
            auto findRoot = [&](int taskId) -> std::pair<int, std::string> {
                auto t = db.getTask(taskId);
                while (t && t->parent_id > 0) {
                    t = db.getTask(t->parent_id);
                }
                return t ? std::make_pair(t->id, t->title)
                         : std::make_pair(taskId, std::string("Task #") + std::to_string(taskId));
            };

            for (const auto& item : weeklyPlan->items) {
                auto task = db.getTask(item.task_id);
                if (!task) continue;

                auto [rootId, rootTitle] = findRoot(item.task_id);

                summary.tasks_planned++;
                summary.total_planned += item.duration_minutes;

                std::string cat = task->category.empty() ? "N/A" : task->category;
                catPlanned[cat] += item.duration_minutes;

                if (task->status == "done") {
                    summary.tasks_completed++;
                    summary.total_completed += item.duration_minutes;
                    summary.total_actual += task->actual_minutes > 0
                        ? task->actual_minutes : item.duration_minutes;
                    catCompleted[cat] += item.duration_minutes;

                    summary.completed_tasks.push_back({
                        {"task_id", task->id},
                        {"title", task->title},
                        {"category", cat},
                        {"planned_minutes", item.duration_minutes},
                        {"actual_minutes", task->actual_minutes},
                        {"root_task_id", rootId},
                        {"root_task_title", rootTitle}
                    });
                } else {
                    summary.tasks_carried_over++;
                    summary.incomplete_tasks.push_back({
                        {"task_id", task->id},
                        {"title", task->title},
                        {"category", cat},
                        {"status", task->status},
                        {"planned_minutes", item.duration_minutes},
                        {"root_task_id", rootId},
                        {"root_task_title", rootTitle}
                    });
                }
            }

            // Category breakdown
            for (const auto& [cat, planned] : catPlanned) {
                int completed = catCompleted.count(cat) ? catCompleted[cat] : 0;
                summary.category_breakdown[cat] = {
                    {"planned", planned},
                    {"completed", completed}
                };
            }

            auto saved = db.createSummary(summary);
            jsonResponse(res, 201, saved);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    // Get summary for a specific week
    server.Get("/api/summaries", [&](const httplib::Request& req, httplib::Response& res) {
        std::string date = req.get_param_value("date");
        if (!date.empty()) {
            std::string monday = getMondayOfWeek(date);
            auto summary = db.getSummary(monday);
            if (summary) {
                jsonResponse(res, 200, *summary);
            } else {
                jsonResponse(res, 200, json::object());
            }
        } else {
            auto all = db.getAllSummaries();
            jsonResponse(res, 200, all);
        }
    });

    // ── Productivity Logs ──────────────────────────────────────────────

    server.Post("/api/productivity", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            ProductivityLog log = body.get<ProductivityLog>();
            auto created = db.createLog(log);
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    });

    server.Get(R"(/api/productivity/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        int taskId = std::stoi(req.matches[1]);
        auto logs = db.getLogsByTaskId(taskId);
        jsonResponse(res, 200, logs);
    });

    // ── Categories ─────────────────────────────────────────────────────

    server.Get("/api/categories", [&](const httplib::Request& req, httplib::Response& res) {
        auto cats = db.getCategories();
        jsonResponse(res, 200, cats);
    });
}
