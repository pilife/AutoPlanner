#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct Task {
    int id = 0;
    int parent_id = 0;          // 0 means root (no parent)
    std::string title;
    std::string description;
    int priority = 3;           // 1 (highest) to 5 (lowest)
    int estimated_minutes = 30;
    int actual_minutes = 0;
    std::string category;
    std::string status = "todo"; // todo | in_progress | done
    std::string due_date;        // YYYY-MM-DD
    std::string created_at;
    std::string updated_at;
};

inline void to_json(nlohmann::json& j, const Task& t) {
    j = {
        {"id", t.id}, {"parent_id", t.parent_id},
        {"title", t.title}, {"description", t.description},
        {"priority", t.priority}, {"estimated_minutes", t.estimated_minutes},
        {"actual_minutes", t.actual_minutes}, {"category", t.category},
        {"status", t.status}, {"due_date", t.due_date},
        {"created_at", t.created_at}, {"updated_at", t.updated_at}
    };
}

inline void from_json(const nlohmann::json& j, Task& t) {
    if (j.contains("id"))                j.at("id").get_to(t.id);
    if (j.contains("parent_id"))         j.at("parent_id").get_to(t.parent_id);
    if (j.contains("title"))             j.at("title").get_to(t.title);
    if (j.contains("description"))       j.at("description").get_to(t.description);
    if (j.contains("priority"))          j.at("priority").get_to(t.priority);
    if (j.contains("estimated_minutes")) j.at("estimated_minutes").get_to(t.estimated_minutes);
    if (j.contains("actual_minutes"))    j.at("actual_minutes").get_to(t.actual_minutes);
    if (j.contains("category"))          j.at("category").get_to(t.category);
    if (j.contains("status"))            j.at("status").get_to(t.status);
    if (j.contains("due_date"))          j.at("due_date").get_to(t.due_date);
}

struct PlanItem {
    int task_id = 0;
    std::string scheduled_time;  // HH:MM
    int duration_minutes = 30;
};

inline void to_json(nlohmann::json& j, const PlanItem& p) {
    j = {{"task_id", p.task_id}, {"scheduled_time", p.scheduled_time},
         {"duration_minutes", p.duration_minutes}};
}

inline void from_json(const nlohmann::json& j, PlanItem& p) {
    if (j.contains("task_id"))          j.at("task_id").get_to(p.task_id);
    if (j.contains("scheduled_time"))   j.at("scheduled_time").get_to(p.scheduled_time);
    if (j.contains("duration_minutes")) j.at("duration_minutes").get_to(p.duration_minutes);
}

struct Plan {
    int id = 0;
    std::string type;   // "daily" or "weekly"
    std::string date;   // YYYY-MM-DD
    std::vector<PlanItem> items;
    bool reviewed = false;
    std::string created_at;
};

inline void to_json(nlohmann::json& j, const Plan& p) {
    j = {{"id", p.id}, {"type", p.type}, {"date", p.date},
         {"items", p.items}, {"reviewed", p.reviewed},
         {"created_at", p.created_at}};
}

inline void from_json(const nlohmann::json& j, Plan& p) {
    if (j.contains("id"))       j.at("id").get_to(p.id);
    if (j.contains("type"))     j.at("type").get_to(p.type);
    if (j.contains("date"))     j.at("date").get_to(p.date);
    if (j.contains("reviewed")) j.at("reviewed").get_to(p.reviewed);
    if (j.contains("items")) j.at("items").get_to(p.items);
}

struct WeeklySummary {
    int id = 0;
    std::string week_date;       // Monday of the week
    int total_planned = 0;       // total planned minutes
    int total_completed = 0;     // completed task minutes
    int total_actual = 0;        // actual minutes spent
    int tasks_planned = 0;
    int tasks_completed = 0;
    int tasks_carried_over = 0;  // tasks not finished
    nlohmann::json category_breakdown = nlohmann::json::object();
    nlohmann::json completed_tasks = nlohmann::json::array();
    nlohmann::json incomplete_tasks = nlohmann::json::array();
    std::string created_at;
};

inline void to_json(nlohmann::json& j, const WeeklySummary& s) {
    j = {
        {"id", s.id}, {"week_date", s.week_date},
        {"total_planned", s.total_planned}, {"total_completed", s.total_completed},
        {"total_actual", s.total_actual},
        {"tasks_planned", s.tasks_planned}, {"tasks_completed", s.tasks_completed},
        {"tasks_carried_over", s.tasks_carried_over},
        {"category_breakdown", s.category_breakdown},
        {"completed_tasks", s.completed_tasks},
        {"incomplete_tasks", s.incomplete_tasks},
        {"created_at", s.created_at}
    };
}

struct ProductivityLog {
    int id = 0;
    int task_id = 0;
    std::string start_time;  // ISO 8601
    std::string end_time;
    std::string notes;
};

inline void to_json(nlohmann::json& j, const ProductivityLog& l) {
    j = {{"id", l.id}, {"task_id", l.task_id}, {"start_time", l.start_time},
         {"end_time", l.end_time}, {"notes", l.notes}};
}

inline void from_json(const nlohmann::json& j, ProductivityLog& l) {
    if (j.contains("id"))         j.at("id").get_to(l.id);
    if (j.contains("task_id"))    j.at("task_id").get_to(l.task_id);
    if (j.contains("start_time")) j.at("start_time").get_to(l.start_time);
    if (j.contains("end_time"))   j.at("end_time").get_to(l.end_time);
    if (j.contains("notes"))      j.at("notes").get_to(l.notes);
}
