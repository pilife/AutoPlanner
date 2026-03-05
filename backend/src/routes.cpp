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

            // Get all incomplete tasks
            auto allTasks = db.getAllTasks("", "");
            // Filter to leaf tasks (no children) that are not done
            std::vector<Task> leafTasks;
            for (const auto& t : allTasks) {
                if (t.status == "done") continue;
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

    // Generate daily plans from a weekly plan: distribute tasks across Mon-Fri
    server.Post("/api/plans/generate-daily", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);

            // Load the weekly plan
            auto weeklyPlan = db.getPlanByTypeAndDate("weekly", monday);
            if (!weeklyPlan || weeklyPlan->items.empty()) {
                errorResponse(res, 400, "No weekly plan found. Generate a weekly plan first.");
                return;
            }

            auto weekDays = getWeekDays(monday);
            int dailyCapacity = 480; // 8 hours per day in minutes

            // Distribute weekly items across 5 days
            std::vector<std::vector<PlanItem>> dailyItems(5);
            std::vector<int> dayMinutes(5, 0);
            int currentDay = 0;

            for (const auto& item : weeklyPlan->items) {
                // Find a day with enough capacity
                int startDay = currentDay;
                while (dayMinutes[currentDay] + item.duration_minutes > dailyCapacity) {
                    currentDay = (currentDay + 1) % 5;
                    if (currentDay == startDay) break; // all days full, force into current
                }

                // Assign a time slot
                int hour = 9 + (dayMinutes[currentDay] / 60);
                int minute = dayMinutes[currentDay] % 60;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);

                PlanItem scheduled;
                scheduled.task_id = item.task_id;
                scheduled.scheduled_time = buf;
                scheduled.duration_minutes = item.duration_minutes;
                dailyItems[currentDay].push_back(scheduled);

                dayMinutes[currentDay] += item.duration_minutes;
                if (dayMinutes[currentDay] >= dailyCapacity) {
                    currentDay = (currentDay + 1) % 5;
                }
            }

            // Save daily plans
            json result = json::array();
            for (int i = 0; i < 5; i++) {
                Plan daily;
                daily.type = "daily";
                daily.date = weekDays[i];
                daily.items = dailyItems[i];
                auto saved = db.createPlan(daily);
                result.push_back(saved);
            }

            jsonResponse(res, 201, result);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
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
