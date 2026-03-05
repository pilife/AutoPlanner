#pragma once

#include <httplib.h>
#include "database.h"

void registerRoutes(httplib::Server& server, Database& db);
