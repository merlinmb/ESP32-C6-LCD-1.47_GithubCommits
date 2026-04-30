#include "github_api.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>

#define GRID_WEEKS 53
#define GRID_DAYS 7

static const char *GRAPHQL_URL = "https://api.github.com/graphql";

struct ParsedContributionDay {
    int32_t days_since_epoch;
    uint16_t count;
    uint8_t level;
    uint8_t day;
    uint8_t month;
    bool has_date;
};

uint8_t contribution_level(uint16_t count) {
    if (count == 0)  return 0;
    if (count <= 3)  return 1;
    if (count <= 6)  return 2;
    if (count <= 9)  return 3;
    return 4;
}

// Parse a uint16 from a JSON fragment — searches for "key":NNN
static uint16_t parse_uint16_field(const String &body, const char *key, int start) {
    String search = String("\"") + key + "\":";
    int idx = body.indexOf(search, start);
    if (idx < 0) return 0;
    idx += search.length();
    // skip possible opening quote (string-encoded numbers)
    if (body.charAt(idx) == '"') idx++;
    int end = idx;
    while (end < (int)body.length() && isDigit(body.charAt(end))) end++;
    return (uint16_t)body.substring(idx, end).toInt();
}

static int32_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static bool parse_date_field(const String &body, const char *key, int start,
                             int32_t &days_since_epoch, uint8_t &month, uint8_t &day) {
    String search = String("\"") + key + "\":\"";
    int idx = body.indexOf(search, start);
    if (idx < 0) return false;
    idx += search.length();
    if (idx + 9 >= (int)body.length()) return false;

    int year = body.substring(idx, idx + 4).toInt();
    int parsed_month = body.substring(idx + 5, idx + 7).toInt();
    int parsed_day = body.substring(idx + 8, idx + 10).toInt();
    if (year <= 0 || parsed_month < 1 || parsed_month > 12 || parsed_day < 1 || parsed_day > 31) return false;

    month = (uint8_t)parsed_month;
    day = (uint8_t)parsed_day;
    days_since_epoch = days_from_civil(year, (unsigned)parsed_month, (unsigned)parsed_day);
    return true;
}

static uint8_t weekday_sun0(int32_t days_since_epoch) {
    int weekday = (days_since_epoch + 4) % 7;
    if (weekday < 0) weekday += 7;
    return (uint8_t)weekday;
}

static void compute_streak_and_busiest(GithubData &data) {
    data.busiest_day_count = 0;
    data.busiest_day_day = 0;
    data.busiest_day_month = 0;

    if (data.anchor_week_start_days == 0 || data.latest_data_day_days == 0) return;

    for (int w = 0; w < (int)data.week_count; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            uint16_t count = data.days[w][d].count;
            if (count <= data.busiest_day_count) continue;

            int32_t day_days = data.anchor_week_start_days - ((GRID_WEEKS - 1 - w) * GRID_DAYS) + d;
            if (day_days > data.latest_data_day_days) continue;

            data.busiest_day_count = count;

            time_t seconds = (time_t)day_days * 86400;
            struct tm tm_value;
            gmtime_r(&seconds, &tm_value);
            data.busiest_day_day = (uint8_t)tm_value.tm_mday;
            data.busiest_day_month = (uint8_t)(tm_value.tm_mon + 1);
        }
    }
}

bool github_fetch(const char *username, const char *token, GithubData &data) {
    data.valid = false;
    data.week_count = 0;
    data.total_contributions = 0;
    data.busiest_day_count = 0;
    data.busiest_day_day = 0;
    data.busiest_day_month = 0;
    data.current_month_commits = 0;
    data.current_month = 0;
    data.anchor_week_start_days = 0;
    data.latest_data_day_days = 0;
    memset(data.days, 0, sizeof(data.days));

    if (!username || username[0] == '\0') {
        Serial.println("[GitHub] Missing username");
        return false;
    }

    if (!token || token[0] == '\0') {
        Serial.println("[GitHub] Missing PAT; skipping GraphQL request");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // no cert verification - acceptable for this use case
    client.setTimeout(12); // 12-second TCP connect + TLS handshake timeout (seconds)

    HTTPClient http;
    http.setTimeout(10000); // 10-second inter-byte read timeout (ms)
    http.setConnectTimeout(12000); // 12-second connect timeout (ms)
    if (!http.begin(client, GRAPHQL_URL)) return false;

    http.addHeader("Content-Type", "application/json");
    String auth = String("Bearer ") + token;
    http.addHeader("Authorization", auth);

    // Compact GraphQL query as a JSON POST body
    String query = String("{\"query\":\"{user(login:\\\"") + username +
        "\\\"){contributionsCollection{contributionCalendar{"
        "totalContributions weeks{contributionDays{"
        "contributionCount date}}}}}}\"}" ;

    int code = http.POST(query);

    if (code != 200) {
        Serial.printf("[GitHub] HTTP %d\n", code);
        String body = http.getString();
        if (body.length() > 0) {
            Serial.printf("[GitHub] Error body: %s\n", body.c_str());
        }
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    if (body.length() > 131072) {
        Serial.printf("[GitHub] Response too large (%d bytes), aborting\n", body.length());
        return false;
    }

    Serial.printf("[GitHub] Response length: %d\n", body.length());

    // Parse totalContributions
    data.total_contributions = parse_uint16_field(body, "totalContributions", 0);

    // Parse weeks/days — static: safe because do_fetch() ensures only one task runs at a time
    static ParsedContributionDay parsed_days[GRID_WEEKS * GRID_DAYS];
    int parsed_day_count = 0;
    int parsed_week_count = 0;
    int32_t latest_day = INT32_MIN;

    int pos = 0;
    while (parsed_week_count < GRID_WEEKS && parsed_day_count < GRID_WEEKS * GRID_DAYS) {
        int week_start = body.indexOf("\"contributionDays\":[", pos);
        if (week_start < 0) break;
        pos = week_start + 20;

        for (int d = 0; d < 7; d++) {
            int day_start = body.indexOf("{", pos);
            if (day_start < 0) break;
            int day_end = body.indexOf("}", day_start);
            if (day_end < 0) break;
            String day_obj = body.substring(day_start, day_end + 1);
            uint16_t count = parse_uint16_field(day_obj, "contributionCount", 0);

            int32_t days_since_epoch = 0;
            uint8_t day = 0;
            uint8_t month = 0;
            bool has_date = parse_date_field(day_obj, "date", 0, days_since_epoch, month, day);
            if (has_date && days_since_epoch > latest_day) latest_day = days_since_epoch;

            parsed_days[parsed_day_count].days_since_epoch = days_since_epoch;
            parsed_days[parsed_day_count].count = count;
            parsed_days[parsed_day_count].level = contribution_level(count);
            parsed_days[parsed_day_count].day = day;
            parsed_days[parsed_day_count].month = month;
            parsed_days[parsed_day_count].has_date = has_date;
            parsed_day_count++;
            pos = day_end + 1;
        }
        parsed_week_count++;
    }

    if (parsed_week_count == 0) {
        Serial.println("[GitHub] Parse failed - no weeks found");
        return false;
    }

    if (latest_day != INT32_MIN) {
        data.anchor_week_start_days = latest_day - weekday_sun0(latest_day);
        data.latest_data_day_days = latest_day;
        data.week_count = GRID_WEEKS;

        for (int i = 0; i < parsed_day_count; i++) {
            if (!parsed_days[i].has_date) continue;

            uint8_t weekday = weekday_sun0(parsed_days[i].days_since_epoch);
            int32_t week_start_days = parsed_days[i].days_since_epoch - weekday;
            int32_t delta_days = data.anchor_week_start_days - week_start_days;
            if (delta_days < 0 || (delta_days % 7) != 0) continue;

            int week_diff = (int)(delta_days / 7);
            if (week_diff >= GRID_WEEKS) continue;

            int target_week = (GRID_WEEKS - 1) - week_diff;
            data.days[target_week][weekday].count = parsed_days[i].count;
            data.days[target_week][weekday].level = parsed_days[i].level;
        }
    } else {
        data.week_count = parsed_week_count;
        for (int w = 0; w < parsed_week_count; w++) {
            for (int d = 0; d < GRID_DAYS; d++) {
                int idx = w * GRID_DAYS + d;
                if (idx >= parsed_day_count) break;
                data.days[w][d].count = parsed_days[idx].count;
                data.days[w][d].level = parsed_days[idx].level;
            }
        }
    }

    // Compute current month and sum its commits from parsed data (has date info)
    if (latest_day != INT32_MIN) {
        time_t secs = (time_t)latest_day * 86400;
        struct tm tm_now;
        gmtime_r(&secs, &tm_now);
        data.current_month = (uint8_t)(tm_now.tm_mon + 1);
        for (int i = 0; i < parsed_day_count; i++) {
            if (parsed_days[i].has_date && parsed_days[i].month == data.current_month)
                data.current_month_commits += parsed_days[i].count;
        }
    }

    compute_streak_and_busiest(data);
    data.valid = true;
    Serial.printf("[GitHub] OK - %d weeks, %d contributions, month(%d) commits %d, busiest %d\n",
                  data.week_count, data.total_contributions,
                  data.current_month, data.current_month_commits, data.busiest_day_count);
    return true;
}
