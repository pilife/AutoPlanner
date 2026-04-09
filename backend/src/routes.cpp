#include "routes.h"
#include "auth.h"
#include "blob_storage.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>

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

    // ── Version (unauthenticated) ─────────────────────────────────────

    server.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
#ifndef BUILD_COMMIT
#define BUILD_COMMIT "dev"
#endif
        jsonResponse(res, 200, {
            {"commit", BUILD_COMMIT},
            {"build_time", __DATE__ " " __TIME__}
        });
    });

    // ── Auth (unauthenticated) ────────────────────────────────────────

    server.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string provider = body.value("provider", "");
            std::string token = body.value("token", "");
            std::string idToken = body.value("id_token", "");

            if (provider.empty() || (token.empty() && idToken.empty())) {
                errorResponse(res, 400, "provider and token are required");
                return;
            }

            if (provider != "microsoft") {
                errorResponse(res, 400, "Only 'microsoft' provider is supported");
                return;
            }

            auto msUser = verifyMicrosoftToken(token, idToken);
            if (!msUser) {
                errorResponse(res, 401, "Invalid or expired token");
                return;
            }

            auto user = db.getOrCreateUser("microsoft", msUser->id,
                                            msUser->email, msUser->display_name, "");
            auto sessionToken = db.createSession(user.id);
            spdlog::info("[AUTH] Login: user={} (id={})", user.email, user.id);

            json result;
            result["session_token"] = sessionToken;
            result["user"] = user;
            jsonResponse(res, 200, result);
        } catch (const std::exception& e) {
            spdlog::error("[AUTH] Login failed: {}", e.what());
            errorResponse(res, 400, e.what());
        }
    });

    server.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
        auto userId = getAuthenticatedUserId(db, req);
        if (!userId) {
            errorResponse(res, 401, "Unauthorized");
            return;
        }
        // Extract token and delete session
        auto it = req.headers.find("Authorization");
        if (it != req.headers.end() && it->second.substr(0, 7) == "Bearer ") {
            db.deleteSession(it->second.substr(7));
        }
        jsonResponse(res, 200, {{"logged_out", true}});
    });

    server.Get("/api/auth/me", [&](const httplib::Request& req, httplib::Response& res) {
        auto userId = getAuthenticatedUserId(db, req);
        if (!userId) {
            errorResponse(res, 401, "Unauthorized");
            return;
        }
        auto user = db.getUserById(*userId);
        if (!user) {
            errorResponse(res, 401, "User not found");
            return;
        }
        jsonResponse(res, 200, *user);
    });

    // ── Blob Upload SAS ────────────────────────────────────────────────

    server.Post("/api/upload/sas", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            const char* account = std::getenv("AZURE_STORAGE_ACCOUNT");
            const char* key = std::getenv("AZURE_STORAGE_KEY");
            if (!account || !key || std::strlen(account) == 0 || std::strlen(key) == 0) {
                errorResponse(res, 500, "Blob storage not configured");
                return;
            }

            auto body = json::parse(req.body);
            std::string filename = body.value("filename", "image.png");
            std::string contentType = body.value("content_type", "image/png");

            std::string ext = blob_storage::getExtension(filename);
            std::string uuid = blob_storage::generateUuid();
            std::string blobPath = "user-" + std::to_string(userId) + "/" + uuid + "." + ext;
            std::string container = "images";

            // Upload SAS: create+write, 5 min
            std::string uploadUrl = blob_storage::generateBlobSasUrl(
                account, key, container, blobPath, "cw", 5);
            // Read SAS: read, 365 days
            std::string blobUrl = blob_storage::generateBlobSasUrl(
                account, key, container, blobPath, "r", 365 * 24 * 60);

            if (uploadUrl.empty() || blobUrl.empty()) {
                errorResponse(res, 500, "Failed to generate SAS token");
                return;
            }

            spdlog::info("[BLOB] SAS generated for user={} blob={}", userId, blobPath);
            jsonResponse(res, 200, {
                {"upload_url", uploadUrl},
                {"blob_url", blobUrl},
                {"content_type", contentType}
            });
        } catch (const std::exception& e) {
            spdlog::error("[BLOB] SAS generation failed: {}", e.what());
            errorResponse(res, 400, e.what());
        }
    }));

    // ── Tasks ──────────────────────────────────────────────────────────

    server.Get("/api/tasks", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        std::string status   = req.get_param_value("status");
        std::string category = req.get_param_value("category");
        auto tasks = db.getAllTasks(userId, status, category);
        jsonResponse(res, 200, tasks);
    }));

    server.Get(R"(/api/tasks/(\d+))", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        int id = std::stoi(req.matches[1]);
        auto task = db.getTask(userId, id);
        if (task) {
            jsonResponse(res, 200, *task);
        } else {
            errorResponse(res, 404, "Task not found");
        }
    }));

    server.Post("/api/tasks", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            Task t = body.get<Task>();
            if (t.title.empty()) {
                errorResponse(res, 400, "Title is required");
                return;
            }
            auto created = db.createTask(userId, t);
            if (created.parent_id > 0) {
                db.recalcEstimate(created.parent_id);
            }
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Put(R"(/api/tasks/(\d+))", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            int id = std::stoi(req.matches[1]);
            auto existing = db.getTask(userId, id);
            if (!existing) {
                errorResponse(res, 404, "Task not found");
                return;
            }
            auto body = json::parse(req.body);
            Task t = *existing;
            int oldParentId = t.parent_id;

            if (body.contains("parent_id"))         t.parent_id = body["parent_id"];
            if (body.contains("title"))             t.title = body["title"];
            if (body.contains("description"))       t.description = body["description"];
            if (body.contains("priority"))          t.priority = body["priority"];
            if (body.contains("estimated_minutes")) t.estimated_minutes = body["estimated_minutes"];
            if (body.contains("actual_minutes"))    t.actual_minutes = body["actual_minutes"];
            if (body.contains("category"))          t.category = body["category"];
            if (body.contains("status"))            t.status = body["status"];
            if (body.contains("due_date"))          t.due_date = body["due_date"];

            if (db.updateTask(userId, id, t)) {
                if (oldParentId > 0) db.recalcEstimate(oldParentId);
                if (t.parent_id > 0 && t.parent_id != oldParentId)
                    db.recalcEstimate(t.parent_id);
                db.recalcEstimate(id);

                auto updated = db.getTask(userId, id);
                jsonResponse(res, 200, *updated);
            } else {
                errorResponse(res, 500, "Failed to update task");
            }
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Delete(R"(/api/tasks/(\d+))", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        int id = std::stoi(req.matches[1]);
        if (db.deleteTask(userId, id)) {
            jsonResponse(res, 200, {{"deleted", true}});
        } else {
            errorResponse(res, 404, "Task not found");
        }
    }));

    // ── Plans ──────────────────────────────────────────────────────────

    server.Get("/api/plans", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        std::string type = req.get_param_value("type");
        std::string date = req.get_param_value("date");
        if (type.empty() || date.empty()) {
            errorResponse(res, 400, "Both 'type' and 'date' query params required");
            return;
        }
        std::string lookupDate = (type == "weekly") ? getMondayOfWeek(date) : date;
        auto plan = db.getPlanByTypeAndDate(userId, type, lookupDate);
        if (plan) {
            jsonResponse(res, 200, *plan);
        } else {
            jsonResponse(res, 200, json::object());
        }
    }));

    server.Post("/api/plans", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            Plan p = body.get<Plan>();
            if (p.type.empty() || p.date.empty()) {
                errorResponse(res, 400, "type and date are required");
                return;
            }
            if (p.type == "weekly") p.date = getMondayOfWeek(p.date);
            auto created = db.createPlan(userId, p);
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Post("/api/plans/generate-weekly", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);

            auto allTasks = db.getAllTasks(userId, "", "");
            std::vector<Task> leafTasks;
            for (const auto& t : allTasks) {
                bool hasChildren = false;
                for (const auto& other : allTasks) {
                    if (other.parent_id == t.id) {
                        hasChildren = true;
                        break;
                    }
                }
                if (!hasChildren) leafTasks.push_back(t);
            }

            std::sort(leafTasks.begin(), leafTasks.end(),
                [](const Task& a, const Task& b) {
                    // in_progress first, then by priority
                    bool aInProgress = (a.status == "in_progress");
                    bool bInProgress = (b.status == "in_progress");
                    if (aInProgress != bInProgress) return aInProgress > bInProgress;
                    return a.priority < b.priority;
                });

            std::vector<PlanItem> items;
            for (const auto& t : leafTasks) {
                // Only include in_progress tasks and done tasks from this week
                if (t.status == "in_progress") {
                    PlanItem item;
                    item.task_id = t.id;
                    item.scheduled_time = "";
                    item.duration_minutes = t.estimated_minutes;
                    items.push_back(item);
                } else if (t.status == "done" && !t.updated_at.empty() && t.updated_at.substr(0, 10) >= monday) {
                    PlanItem item;
                    item.task_id = t.id;
                    item.scheduled_time = "";
                    item.duration_minutes = t.estimated_minutes;
                    items.push_back(item);
                }
            }

            Plan plan;
            plan.type = "weekly";
            plan.date = monday;
            plan.items = items;
            auto saved = db.createPlan(userId, plan);

            json result = saved;
            result["generated"] = true;
            jsonResponse(res, 201, result);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Post("/api/plans/generate-daily", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
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

            auto weeklyPlan = db.getPlanByTypeAndDate(userId, "weekly", monday);
            if (!weeklyPlan || weeklyPlan->items.empty()) {
                errorResponse(res, 400, "No weekly plan found. Generate a weekly plan first.");
                return;
            }

            std::vector<PlanItem> incompleteItems;
            // Collect done task IDs and their existing daily plan assignments
            std::set<int> doneTaskIds;
            for (const auto& item : weeklyPlan->items) {
                auto task = db.getTask(userId, item.task_id);
                if (task && task->status == "done") {
                    doneTaskIds.insert(item.task_id);
                } else if (task) {
                    incompleteItems.push_back(item);
                }
            }

            auto remainingDays = getRemainingWeekDays(monday, startDate);
            int numDays = static_cast<int>(remainingDays.size());
            if (numDays == 0) {
                errorResponse(res, 400, "No remaining weekdays this week.");
                return;
            }

            // Preserve done tasks in their existing daily plans
            std::map<std::string, std::vector<PlanItem>> preservedItems; // date -> items
            std::set<int> assignedDoneIds;
            auto allWeekDays = getRemainingWeekDays(monday, monday);
            for (const auto& day : allWeekDays) {
                auto existingPlan = db.getPlanByTypeAndDate(userId, "daily", day);
                if (existingPlan) {
                    for (const auto& item : existingPlan->items) {
                        if (doneTaskIds.count(item.task_id)) {
                            preservedItems[day].push_back(item);
                            assignedDoneIds.insert(item.task_id);
                        }
                    }
                }
            }
            // Done tasks not yet in any daily plan — assign to today/first remaining day
            for (int taskId : doneTaskIds) {
                if (!assignedDoneIds.count(taskId)) {
                    auto it = std::find_if(weeklyPlan->items.begin(), weeklyPlan->items.end(),
                        [taskId](const PlanItem& pi) { return pi.task_id == taskId; });
                    PlanItem pi;
                    pi.task_id = taskId;
                    pi.scheduled_time = "";
                    pi.duration_minutes = (it != weeklyPlan->items.end()) ? it->duration_minutes : 30;
                    preservedItems[startDate].push_back(pi);
                }
            }

            int dailyCapacity = 480;
            int totalRemaining = 0;
            for (const auto& item : incompleteItems)
                totalRemaining += item.duration_minutes;
            int totalCapacity = numDays * dailyCapacity;

            std::vector<std::vector<PlanItem>> dailyItems(numDays);
            std::vector<int> dayMinutes(numDays, 0);

            // Pre-fill preserved done items for remaining days
            for (int i = 0; i < numDays; i++) {
                auto it = preservedItems.find(remainingDays[i]);
                if (it != preservedItems.end()) {
                    for (const auto& item : it->second) {
                        dailyItems[i].push_back(item);
                        dayMinutes[i] += item.duration_minutes;
                    }
                }
            }

            // Separate main tasks from side tasks (low priority + short duration)
            // Side tasks are interleaved so there's always something to switch to
            auto allTasksList = db.getAllTasks(userId, "", "");
            std::map<int, Task> taskById;
            for (const auto& t : allTasksList) taskById[t.id] = t;

            std::vector<PlanItem> mainItems, sideItems;
            for (const auto& item : incompleteItems) {
                auto it = taskById.find(item.task_id);
                int priority = (it != taskById.end()) ? it->second.priority : 3;
                // Side task: priority 4-5 and <= 60 minutes
                if (priority >= 4 && item.duration_minutes <= 60) {
                    sideItems.push_back(item);
                } else {
                    mainItems.push_back(item);
                }
            }

            int currentDay = 0;
            int mainCountInDay = 0;
            size_t sideIdx = 0;
            for (const auto& item : mainItems) {
                while (currentDay < numDays &&
                       dayMinutes[currentDay] + item.duration_minutes > dailyCapacity) {
                    currentDay++;
                    mainCountInDay = 0;
                }
                if (currentDay >= numDays) currentDay = numDays - 1;

                PlanItem scheduled;
                scheduled.task_id = item.task_id;
                scheduled.scheduled_time = "";
                scheduled.duration_minutes = item.duration_minutes;
                dailyItems[currentDay].push_back(scheduled);
                dayMinutes[currentDay] += item.duration_minutes;
                mainCountInDay++;

                // After every 2 main tasks, insert a side task if available and fits
                if (mainCountInDay % 2 == 0 && sideIdx < sideItems.size()) {
                    const auto& side = sideItems[sideIdx];
                    if (dayMinutes[currentDay] + side.duration_minutes <= dailyCapacity) {
                        PlanItem sideScheduled;
                        sideScheduled.task_id = side.task_id;
                        sideScheduled.scheduled_time = "";
                        sideScheduled.duration_minutes = side.duration_minutes;
                        dailyItems[currentDay].push_back(sideScheduled);
                        dayMinutes[currentDay] += side.duration_minutes;
                        sideIdx++;
                    }
                }

                if (currentDay < numDays - 1 &&
                    dayMinutes[currentDay] >= dailyCapacity) {
                    currentDay++;
                    mainCountInDay = 0;
                }
            }

            // Distribute remaining side tasks across days with capacity
            for (; sideIdx < sideItems.size(); sideIdx++) {
                const auto& side = sideItems[sideIdx];
                int bestDay = 0;
                for (int d = 1; d < numDays; d++) {
                    if (dayMinutes[d] < dayMinutes[bestDay]) bestDay = d;
                }
                PlanItem sideScheduled;
                sideScheduled.task_id = side.task_id;
                sideScheduled.scheduled_time = "";
                sideScheduled.duration_minutes = side.duration_minutes;
                dailyItems[bestDay].push_back(sideScheduled);
                dayMinutes[bestDay] += side.duration_minutes;
            }

            json plans = json::array();
            // Save remaining days (with both preserved done + new incomplete items)
            for (int i = 0; i < numDays; i++) {
                Plan daily;
                daily.type = "daily";
                daily.date = remainingDays[i];
                daily.items = dailyItems[i];
                auto saved = db.createPlan(userId, daily);
                plans.push_back(saved);
            }
            // Past days are not re-saved to preserve their reviewed status

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
    }));

    server.Get("/api/plans/unreviewed", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        std::string before = req.get_param_value("before");
        if (before.empty()) before = todayStr();
        std::string monday = getMondayOfWeek(before);
        auto plans = db.getUnreviewedDailyPlans(userId, before, monday);
        jsonResponse(res, 200, plans);
    }));

    server.Post("/api/plans/review", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            int planId = body.at("plan_id").get<int>();
            auto taskReviews = body.at("tasks");

            for (const auto& tr : taskReviews) {
                int taskId = tr.at("task_id").get<int>();
                std::string status = tr.at("status").get<std::string>();
                int actualMinutes = tr.value("actual_minutes", 0);

                auto task = db.getTask(userId, taskId);
                if (task) {
                    Task t = *task;
                    t.status = status;
                    t.actual_minutes = actualMinutes;
                    db.updateTask(userId, taskId, t);
                }
            }

            db.markPlanReviewed(userId, planId);

            std::string monday;
            if (body.contains("plan_date")) {
                monday = getMondayOfWeek(body["plan_date"].get<std::string>());
            } else {
                monday = getMondayOfWeek(todayStr());
            }

            std::string today = todayStr();
            std::string startDate = (today > monday) ? today : monday;
            auto weeklyPlan = db.getPlanByTypeAndDate(userId, "weekly", monday);

            json result;
            result["reviewed"] = true;

            if (weeklyPlan && !weeklyPlan->items.empty()) {
                std::vector<PlanItem> incompleteItems;
                for (const auto& item : weeklyPlan->items) {
                    auto task = db.getTask(userId, item.task_id);
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
                        auto saved = db.createPlan(userId, daily);
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
    }));

    // ── Weekly Summary ──────────────────────────────────────────────────

    server.Post("/api/summaries/generate", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            std::string date = body.value("date", "");
            if (date.empty()) {
                errorResponse(res, 400, "date is required");
                return;
            }
            std::string monday = getMondayOfWeek(date);

            auto weeklyPlan = db.getPlanByTypeAndDate(userId, "weekly", monday);
            if (!weeklyPlan || weeklyPlan->items.empty()) {
                errorResponse(res, 400, "No weekly plan found for this week.");
                return;
            }

            WeeklySummary summary;
            summary.week_date = monday;

            std::map<std::string, int> catCompleted;
            std::map<std::string, int> catPlanned;

            auto findRoot = [&](int taskId) -> std::pair<int, std::string> {
                auto t = db.getTask(userId, taskId);
                while (t && t->parent_id > 0) {
                    t = db.getTask(userId, t->parent_id);
                }
                return t ? std::make_pair(t->id, t->title)
                         : std::make_pair(taskId, std::string("Task #") + std::to_string(taskId));
            };

            // Build full path for a task (e.g. "Root > Parent > Task")
            auto getTaskPath = [&](int taskId) -> std::string {
                std::vector<std::string> parts;
                auto t = db.getTask(userId, taskId);
                while (t) {
                    parts.push_back(t->title);
                    t = (t->parent_id > 0) ? db.getTask(userId, t->parent_id) : std::nullopt;
                }
                std::reverse(parts.begin(), parts.end());
                std::string path;
                for (size_t i = 0; i < parts.size(); i++) {
                    if (i > 0) path += " > ";
                    path += parts[i];
                }
                return path;
            };

            // Build parent_id -> task mapping for hierarchy
            auto getParentId = [&](int taskId) -> int {
                auto t = db.getTask(userId, taskId);
                return t ? t->parent_id : 0;
            };

            for (const auto& item : weeklyPlan->items) {
                auto task = db.getTask(userId, item.task_id);
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
                        {"root_task_title", rootTitle},
                        {"parent_id", getParentId(task->id)},
                        {"path", getTaskPath(task->id)}
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
                        {"root_task_title", rootTitle},
                        {"parent_id", getParentId(task->id)},
                        {"path", getTaskPath(task->id)}
                    });
                }
            }

            for (const auto& [cat, planned] : catPlanned) {
                int completed = catCompleted.count(cat) ? catCompleted[cat] : 0;
                summary.category_breakdown[cat] = {
                    {"planned", planned},
                    {"completed", completed}
                };
            }

            auto saved = db.createSummary(userId, summary);
            jsonResponse(res, 201, saved);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Get("/api/summaries", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        std::string date = req.get_param_value("date");
        if (!date.empty()) {
            std::string monday = getMondayOfWeek(date);
            auto summary = db.getSummary(userId, monday);
            if (summary) {
                jsonResponse(res, 200, *summary);
            } else {
                jsonResponse(res, 200, json::object());
            }
        } else {
            auto all = db.getAllSummaries(userId);
            jsonResponse(res, 200, all);
        }
    }));

    // ── Productivity Logs ──────────────────────────────────────────────

    server.Post("/api/productivity", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        try {
            auto body = json::parse(req.body);
            ProductivityLog log = body.get<ProductivityLog>();
            auto created = db.createLog(userId, log);
            jsonResponse(res, 201, created);
        } catch (const std::exception& e) {
            errorResponse(res, 400, e.what());
        }
    }));

    server.Get(R"(/api/productivity/(\d+))", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        int taskId = std::stoi(req.matches[1]);
        auto logs = db.getLogsByTaskId(userId, taskId);
        jsonResponse(res, 200, logs);
    }));

    // ── Categories ─────────────────────────────────────────────────────

    server.Get("/api/categories", requireAuth(db,
        [&](const httplib::Request& req, httplib::Response& res, int userId) {
        auto cats = db.getCategories(userId);
        jsonResponse(res, 200, cats);
    }));
}
