#include <iostream>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "database.h"
#include "routes.h"
#include "seed.h"

static void initLogging() {
    std::vector<spdlog::sink_ptr> sinks;

    // Console sink (always)
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
    sinks.push_back(consoleSink);

    // File sink (if LOG_FILE env var is set, or default in production)
    const char* logFile = std::getenv("LOG_FILE");
    if (logFile && std::strlen(logFile) > 0) {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        sinks.push_back(fileSink);
    }

    auto logger = std::make_shared<spdlog::logger>("autoplanner", sinks.begin(), sinks.end());

    // Log level from env: LOG_LEVEL=debug|info|warn|error (default: info)
    const char* levelEnv = std::getenv("LOG_LEVEL");
    std::string level = levelEnv ? levelEnv : "info";
    if (level == "debug")     logger->set_level(spdlog::level::debug);
    else if (level == "warn") logger->set_level(spdlog::level::warn);
    else if (level == "error") logger->set_level(spdlog::level::err);
    else                       logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
}

#ifndef BUILD_COMMIT
#define BUILD_COMMIT "dev"
#endif

int main(int argc, char* argv[]) {
    initLogging();

    spdlog::info("AutoPlanner commit={} built={}", BUILD_COMMIT, __DATE__ " " __TIME__);

    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    spdlog::info("Starting AutoPlanner...");

    Database* dbPtr = nullptr;
    try {
        dbPtr = new Database("autoplanner.db");
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize database: {}", e.what());
        return 1;
    }
    Database& db = *dbPtr;

    const char* seedEnv = std::getenv("AUTOPLANNER_SEED");
    if (seedEnv && std::string(seedEnv) == "1") {
        spdlog::info("Seeding demo data...");
        seedIfEmpty(db);
    }

    httplib::Server server;

    // CORS middleware
    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // API request logging
    server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        if (req.method == "OPTIONS") return; // skip preflight
        if (res.status >= 400) {
            spdlog::warn("[API] {} {} -> {} ({}B)", req.method, req.path,
                         res.status, res.body.size());
        } else {
            spdlog::info("[API] {} {} -> {} ({}B)", req.method, req.path,
                         res.status, res.body.size());
        }
    });

    registerRoutes(server, db);

    // Serve frontend static files (production build)
    std::string staticDir = "./static";
    if (argc > 2) staticDir = argv[2];
    if (server.set_mount_point("/", staticDir)) {
        spdlog::info("Serving static files from {}", staticDir);

        server.set_error_handler([&](const httplib::Request& req, httplib::Response& res) {
            if (res.status == 404 && req.path.substr(0, 4) != "/api") {
                std::ifstream ifs(staticDir + "/index.html");
                if (ifs) {
                    std::string html((std::istreambuf_iterator<char>(ifs)),
                                      std::istreambuf_iterator<char>());
                    res.set_content(html, "text/html");
                    res.status = 200;
                }
            }
        });
    } else {
        spdlog::info("Static directory not found ({}), running API-only mode", staticDir);
    }

    spdlog::info("AutoPlanner running on http://localhost:{}", port);
    server.listen("0.0.0.0", port);

    delete dbPtr;
    return 0;
}
