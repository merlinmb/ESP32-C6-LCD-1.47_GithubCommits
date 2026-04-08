#include "github_api.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>

static const char *GRAPHQL_URL = "https://api.github.com/graphql";

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

static void compute_streak_and_busiest(GithubData &data) {
    data.busiest_day_count = 0;
    data.current_streak = 0;

    // Scan backwards week by week, day by day for streak
    bool in_streak = true;
    for (int w = (int)data.week_count - 1; w >= 0; w--) {
        for (int d = 6; d >= 0; d--) {
            uint16_t c = data.days[w][d].count;
            if (c > data.busiest_day_count) data.busiest_day_count = c;
            if (in_streak) {
                if (c > 0) data.current_streak++;
                else       in_streak = false;
            }
        }
    }
}

bool github_fetch(const char *username, const char *token, GithubData &data) {
    WiFiClientSecure client;
    client.setInsecure(); // no cert verification - acceptable for this use case

    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, GRAPHQL_URL)) return false;

    http.addHeader("Content-Type", "application/json");
    String auth = String("Bearer ") + token;
    http.addHeader("Authorization", auth);

    // Compact GraphQL query as a JSON POST body
    String query = String("{\"query\":\"{user(login:\\\"") + username +
        "\\\"){contributionsCollection{contributionCalendar{"
        "totalContributions weeks{contributionDays{"
        "contributionCount}}}}}}\"}" ;

    lv_timer_handler();
    int code = http.POST(query);
    lv_timer_handler();

    if (code != 200) {
        Serial.printf("[GitHub] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    lv_timer_handler();

    Serial.printf("[GitHub] Response length: %d\n", body.length());

    // Parse totalContributions
    data.total_contributions = parse_uint16_field(body, "totalContributions", 0);

    // Parse weeks/days
    memset(data.days, 0, sizeof(data.days));
    data.week_count = 0;

    int pos = 0;
    while (data.week_count < 53) {
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
            data.days[data.week_count][d].count = count;
            data.days[data.week_count][d].level = contribution_level(count);
            pos = day_end + 1;
        }
        data.week_count++;
    }

    if (data.week_count == 0) {
        Serial.println("[GitHub] Parse failed - no weeks found");
        return false;
    }

    compute_streak_and_busiest(data);
    data.valid = true;
    Serial.printf("[GitHub] OK - %d weeks, %d contributions, streak %d, busiest %d\n",
                  data.week_count, data.total_contributions,
                  data.current_streak, data.busiest_day_count);
    return true;
}
