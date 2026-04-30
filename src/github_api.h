#pragma once
#include <Arduino.h>

struct ContributionDay {
    uint16_t count;
    uint8_t  level; // 0-4
};

struct GithubData {
    ContributionDay days[53][7]; // normalized [week][weekday 0=Sun], current week at index 52
    uint8_t         week_count;  // displayed weeks after normalization
    uint16_t        total_contributions;
    uint16_t        busiest_day_count;
    uint8_t         busiest_day_day;
    uint8_t         busiest_day_month;
    uint16_t        current_month_commits; // total commits in the current calendar month
    uint8_t         current_month;         // 1-12 month of latest_data_day_days
    int32_t         anchor_week_start_days; // days since Unix epoch for the current week's Sunday
    int32_t         latest_data_day_days;   // latest actual contribution day returned by GitHub
    bool            valid;       // false until first successful fetch
};

// count -> level using GitHub thresholds
uint8_t contribution_level(uint16_t count);

// Fetch contributions for username using token. Populates data in-place.
// Returns true on success. Calls lv_timer_handler() periodically during fetch.
bool github_fetch(const char *username, const char *token, GithubData &data);
