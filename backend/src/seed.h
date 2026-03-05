#pragma once

#include "database.h"

// Populate the database with sample data if it's empty.
// Creates a realistic task hierarchy, a weekly plan, daily plans
// for past days (unreviewed), and today's plan.
void seedIfEmpty(Database& db);
