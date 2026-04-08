#pragma once
#include <lvgl.h>
#include "github_api.h"
#include "config.h"

// Build the grid screen. Call once after LVGL init.
lv_obj_t *display_grid_build(const char *username);

// Update cell colours and reassign animations from fresh data.
void display_grid_update(const GithubData &data, const Config &cfg);

// Stop all breathing animations (call before data refresh).
void display_grid_stop_animations();
