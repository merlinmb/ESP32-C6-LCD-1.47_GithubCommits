#pragma once
#include <Arduino.h>

struct ContributionDay {
    uint16_t count;
    uint8_t  level; // 0-4
};

struct GithubData {
    ContributionDay days[53][7]; // [week][weekday 0=Sun]
    uint8_t         week_count;  // actual weeks returned (usually 53)
    uint16_t        total_contributions;
    uint16_t        busiest_day_count;
    uint16_t        current_streak;
    bool            valid;       // false until first successful fetch
};

// count -> level using GitHub thresholds
uint8_t contribution_level(uint16_t count);

// Fetch contributions for username using token. Populates data in-place.
// Returns true on success. Calls lv_timer_handler() periodically during fetch.
bool github_fetch(const char *username, const char *token, GithubData &data);
