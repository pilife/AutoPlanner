#include "seed.h"
#include <ctime>
#include <sstream>
#include <iomanip>

static std::string dateOffset(int daysFromToday) {
    auto t = std::time(nullptr);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    tm_buf.tm_mday += daysFromToday;
    std::mktime(&tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d");
    return oss.str();
}

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

void seedIfEmpty(Database& db) {
    // Seed uses user_id=0 (demo user, not a real authenticated user)
    const int demoUser = 0;

    auto existing = db.getAllTasks(demoUser, "", "");
    if (!existing.empty()) return;

    std::string today = dateOffset(0);
    std::string yesterday = dateOffset(-1);
    std::string dayBefore = dateOffset(-2);
    std::string tomorrow = dateOffset(1);
    std::string friday = dateOffset(4);
    std::string monday = getMondayOfWeek(today);

    // ── Root tasks (projects) ──────────────────────────────────────

    Task auth;
    auth.title = "User Authentication Service";
    auth.priority = 1;
    auth.category = "Coding";
    auth.due_date = friday;
    auto authSaved = db.createTask(demoUser, auth);

    Task dashboard;
    dashboard.title = "Analytics Dashboard";
    dashboard.priority = 2;
    dashboard.category = "Design";
    dashboard.due_date = friday;
    auto dashSaved = db.createTask(demoUser, dashboard);

    Task infra;
    infra.title = "Infrastructure Monitoring";
    infra.priority = 2;
    infra.category = "Monitor";
    infra.due_date = friday;
    auto infraSaved = db.createTask(demoUser, infra);

    // ── Auth subtasks ──────────────────────────────────────────────

    Task authDesign;
    authDesign.title = "Design auth API schema";
    authDesign.priority = 1;
    authDesign.parent_id = authSaved.id;
    authDesign.estimated_minutes = 120;
    authDesign.category = "Design";
    authDesign.status = "done";
    authDesign.actual_minutes = 90;
    auto authDesignSaved = db.createTask(demoUser, authDesign);

    Task authImpl;
    authImpl.title = "Implement OAuth2 flow";
    authImpl.priority = 1;
    authImpl.parent_id = authSaved.id;
    authImpl.estimated_minutes = 240;
    authImpl.category = "Coding";
    authImpl.status = "in_progress";
    auto authImplSaved = db.createTask(demoUser, authImpl);

    Task authTest;
    authTest.title = "Write auth integration tests";
    authTest.priority = 1;
    authTest.parent_id = authSaved.id;
    authTest.estimated_minutes = 180;
    authTest.category = "Test";
    auto authTestSaved = db.createTask(demoUser, authTest);

    Task authReview;
    authReview.title = "Code review auth module";
    authReview.priority = 2;
    authReview.parent_id = authSaved.id;
    authReview.estimated_minutes = 60;
    authReview.category = "Coding";
    auto authReviewSaved = db.createTask(demoUser, authReview);

    // ── Dashboard subtasks ─────────────────────────────────────────

    Task dashWireframe;
    dashWireframe.title = "Create dashboard wireframes";
    dashWireframe.priority = 2;
    dashWireframe.parent_id = dashSaved.id;
    dashWireframe.estimated_minutes = 180;
    dashWireframe.category = "Design";
    dashWireframe.status = "done";
    dashWireframe.actual_minutes = 200;
    auto dashWireframeSaved = db.createTask(demoUser, dashWireframe);

    Task dashCharts;
    dashCharts.title = "Implement chart components";
    dashCharts.priority = 2;
    dashCharts.parent_id = dashSaved.id;
    dashCharts.estimated_minutes = 300;
    dashCharts.category = "Coding";
    auto dashChartsSaved = db.createTask(demoUser, dashCharts);

    Task dashFilter;
    dashFilter.title = "Add date range filter";
    dashFilter.priority = 3;
    dashFilter.parent_id = dashSaved.id;
    dashFilter.estimated_minutes = 90;
    dashFilter.category = "Coding";
    auto dashFilterSaved = db.createTask(demoUser, dashFilter);

    // ── Infra subtasks ─────────────────────────────────────────────

    Task infraAlerts;
    infraAlerts.title = "Set up alerting rules";
    infraAlerts.priority = 2;
    infraAlerts.parent_id = infraSaved.id;
    infraAlerts.estimated_minutes = 120;
    infraAlerts.category = "Monitor";
    auto infraAlertsSaved = db.createTask(demoUser, infraAlerts);

    Task infraGrafana;
    infraGrafana.title = "Configure Grafana dashboards";
    infraGrafana.priority = 3;
    infraGrafana.parent_id = infraSaved.id;
    infraGrafana.estimated_minutes = 90;
    infraGrafana.category = "Monitor";
    auto infraGrafanaSaved = db.createTask(demoUser, infraGrafana);

    // ── Standalone task ────────────────────────────────────────────

    Task docs;
    docs.title = "Update API documentation";
    docs.priority = 4;
    docs.estimated_minutes = 60;
    docs.category = "Design";
    auto docsSaved = db.createTask(demoUser, docs);

    // Recalculate parent estimates
    db.recalcEstimate(authSaved.id);
    db.recalcEstimate(dashSaved.id);
    db.recalcEstimate(infraSaved.id);

    // ── Weekly plan ────────────────────────────────────────────────

    Plan weekly;
    weekly.type = "weekly";
    weekly.date = monday;
    weekly.items = {
        {authDesignSaved.id, "", 120},
        {authImplSaved.id, "", 240},
        {authTestSaved.id, "", 180},
        {authReviewSaved.id, "", 60},
        {dashWireframeSaved.id, "", 180},
        {dashChartsSaved.id, "", 300},
        {dashFilterSaved.id, "", 90},
        {infraAlertsSaved.id, "", 120},
        {infraGrafanaSaved.id, "", 90},
        {docsSaved.id, "", 60},
    };
    db.createPlan(demoUser, weekly);

    // ── Past daily plans (unreviewed) ──────────────────────────────

    Plan dayBeforePlan;
    dayBeforePlan.type = "daily";
    dayBeforePlan.date = dayBefore;
    dayBeforePlan.items = {
        {authDesignSaved.id, "", 120},
        {authImplSaved.id, "", 120},
        {dashWireframeSaved.id, "", 180},
    };
    db.createPlan(demoUser, dayBeforePlan);

    Plan yesterdayPlan;
    yesterdayPlan.type = "daily";
    yesterdayPlan.date = yesterday;
    yesterdayPlan.items = {
        {authImplSaved.id, "", 120},
        {dashChartsSaved.id, "", 150},
        {infraAlertsSaved.id, "", 120},
    };
    db.createPlan(demoUser, yesterdayPlan);

    Plan todayPlan;
    todayPlan.type = "daily";
    todayPlan.date = today;
    todayPlan.items = {
        {authTestSaved.id, "", 180},
        {dashChartsSaved.id, "", 150},
        {authReviewSaved.id, "", 60},
    };
    db.createPlan(demoUser, todayPlan);

    // ── Past week summary ──────────────────────────────────────────

    std::string lastMonday = dateOffset(-7);
    lastMonday = getMondayOfWeek(lastMonday);

    WeeklySummary pastSummary;
    pastSummary.week_date = lastMonday;
    pastSummary.total_planned = 960;
    pastSummary.total_completed = 780;
    pastSummary.total_actual = 820;
    pastSummary.tasks_planned = 8;
    pastSummary.tasks_completed = 6;
    pastSummary.tasks_carried_over = 2;
    pastSummary.category_breakdown = {
        {"Design", {{"planned", 240}, {"completed", 240}}},
        {"Coding", {{"planned", 420}, {"completed", 300}}},
        {"Test",   {{"planned", 180}, {"completed", 180}}},
        {"Monitor",{{"planned", 120}, {"completed", 60}}}
    };
    pastSummary.completed_tasks = {
        {{"task_id", 0}, {"title", "Design login flow"}, {"category", "Design"}, {"planned_minutes", 120}, {"actual_minutes", 100}, {"root_task_id", 100}, {"root_task_title", "User Portal"}},
        {{"task_id", 0}, {"title", "Design data model"}, {"category", "Design"}, {"planned_minutes", 120}, {"actual_minutes", 140}, {"root_task_id", 100}, {"root_task_title", "User Portal"}},
        {{"task_id", 0}, {"title", "Implement user CRUD"}, {"category", "Coding"}, {"planned_minutes", 180}, {"actual_minutes", 200}, {"root_task_id", 100}, {"root_task_title", "User Portal"}},
        {{"task_id", 0}, {"title", "Implement session mgmt"}, {"category", "Coding"}, {"planned_minutes", 120}, {"actual_minutes", 110}, {"root_task_id", 100}, {"root_task_title", "User Portal"}},
        {{"task_id", 0}, {"title", "Write unit tests"}, {"category", "Test"}, {"planned_minutes", 180}, {"actual_minutes", 190}, {"root_task_id", 101}, {"root_task_title", "CI/CD Pipeline"}},
        {{"task_id", 0}, {"title", "Setup health checks"}, {"category", "Monitor"}, {"planned_minutes", 60}, {"actual_minutes", 80}, {"root_task_id", 101}, {"root_task_title", "CI/CD Pipeline"}}
    };
    pastSummary.incomplete_tasks = {
        {{"task_id", 0}, {"title", "Implement rate limiting"}, {"category", "Coding"}, {"status", "in_progress"}, {"planned_minutes", 120}, {"root_task_id", 100}, {"root_task_title", "User Portal"}},
        {{"task_id", 0}, {"title", "Configure uptime alerts"}, {"category", "Monitor"}, {"status", "todo"}, {"planned_minutes", 60}, {"root_task_id", 101}, {"root_task_title", "CI/CD Pipeline"}}
    };
    db.createSummary(demoUser, pastSummary);
}
