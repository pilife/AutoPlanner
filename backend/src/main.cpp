#include <iostream>
#include <fstream>
#include <cstdlib>
#include <httplib.h>
#include "database.h"
#include "routes.h"
#include "seed.h"

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    Database db("autoplanner.db");
    // Only seed demo data if AUTOPLANNER_SEED=1 is set
    const char* seedEnv = std::getenv("AUTOPLANNER_SEED");
    if (seedEnv && std::string(seedEnv) == "1") {
        seedIfEmpty(db);
    }
    httplib::Server server;

    // CORS middleware — allow requests from the Vite dev server
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

    registerRoutes(server, db);

    // Serve frontend static files (production build)
    std::string staticDir = "./static";
    if (argc > 2) staticDir = argv[2];
    if (server.set_mount_point("/", staticDir)) {
        std::cout << "Serving static files from " << staticDir << std::endl;

        // SPA fallback: serve index.html for non-API, non-file routes
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
        std::cout << "Static directory not found (" << staticDir
                  << "), running API-only mode" << std::endl;
    }

    std::cout << "AutoPlanner running on http://localhost:" << port << std::endl;
    server.listen("0.0.0.0", port);

    return 0;
}
