#include <iostream>
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
    seedIfEmpty(db);
    httplib::Server server;

    // CORS middleware — allow requests from the Vite dev server
    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    registerRoutes(server, db);

    std::cout << "AutoPlanner backend running on http://localhost:" << port << std::endl;
    server.listen("0.0.0.0", port);

    return 0;
}
