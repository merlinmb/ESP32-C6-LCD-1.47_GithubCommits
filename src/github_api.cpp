#include "github_api.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define GRID_WEEKS 53
#define GRID_DAYS  7

static const char *GRAPHQL_URL = "https://api.github.com/graphql";
static const char *GRAPHQL_QUERY =
    "query($login:String!){user(login:$login){contributionsCollection{"
    "contributionCalendar{totalContributions weeks{contributionDays{"
    "contributionCount date}}}}}}";

// ── Contribution level ────────────────────────────────────────────────────────

uint8_t contribution_level(uint16_t count) {
    if (count == 0)  return 0;
    if (count <= 3)  return 1;
    if (count <= 6)  return 2;
    if (count <= 9)  return 3;
    return 4;
}

// ── Date math ─────────────────────────────────────────────────────────────────

static int32_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static uint8_t weekday_sun0(int32_t d) {
    int w = (int)((d + 4) % 7);
    if (w < 0) w += 7;
    return (uint8_t)w;
}

static bool parse_iso_date(const char *s, int32_t &days, uint8_t &month, uint8_t &day_out) {
    if (s[4] != '-' || s[7] != '-') return false;
    int y  = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    int mo = (s[5]-'0')*10  + (s[6]-'0');
    int d  = (s[8]-'0')*10  + (s[9]-'0');
    if (y <= 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    month   = (uint8_t)mo;
    day_out = (uint8_t)d;
    days    = days_from_civil(y, (unsigned)mo, (unsigned)d);
    return true;
}

static uint8_t parse_month_abbr(const char *s) {
    switch (s[0]) {
        case 'J': if (s[1]=='a') return 1;
                  if (s[2]=='n') return 6;
                  return 7;
        case 'F': return 2;
        case 'M': return s[2]=='r' ? 3 : 5;
        case 'A': return s[1]=='p' ? 4 : 8;
        case 'S': return 9;
        case 'O': return 10;
        case 'N': return 11;
        case 'D': return 12;
    }
    return 0;
}

static bool parse_http_date_days(const String &hdr, int32_t &days) {
    if ((int)hdr.length() < 29) return false;
    int comma = hdr.indexOf(',');
    if (comma < 0) return false;
    const char *p = hdr.c_str() + comma + 2;
    int d  = (p[0]-'0')*10 + (p[1]-'0');
    uint8_t mo = parse_month_abbr(p + 3);
    int y  = (p[7]-'0')*1000 + (p[8]-'0')*100 + (p[9]-'0')*10 + (p[10]-'0');
    if (y <= 0 || mo == 0 || d < 1 || d > 31) return false;
    days = days_from_civil(y, mo, (unsigned)d);
    return true;
}

static void trim_ascii_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src && isspace((unsigned char)*src)) src++;

    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;

    memcpy(dst, src, len);
    dst[len] = '\0';
}

// ── Static receive buffer ─────────────────────────────────────────────────────
// 48 KB covers the full GitHub GraphQL response for a year of contributions
// (~30-40 KB typical). Static so it never fragments the heap across fetches.

static const int BODY_BUF_SIZE = 49152;
static char s_body[BODY_BUF_SIZE];

// Minimal Stream wrapper that writes into s_body[].
class BufStream : public Stream {
public:
    int   _len = 0;
    bool  _overflow = false;

    size_t write(uint8_t c) override {
        if (_len >= BODY_BUF_SIZE - 1) { _overflow = true; return 1; }
        s_body[_len++] = (char)c;
        return 1;
    }
    size_t write(const uint8_t *buf, size_t size) override {
        for (size_t i = 0; i < size; i++) write(buf[i]);
        return size;
    }
    // Stream read side — not used, must be implemented
    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }
};

// ── Char-buffer helpers (zero-copy, no String allocation) ─────────────────────

static uint16_t buf_parse_uint16(const char *body, int body_len,
                                  const char *key, int start) {
    int klen = (int)strlen(key);
    for (int i = start; i <= body_len - klen; i++) {
        if (memcmp(body + i, key, klen) != 0) continue;
        int j = i + klen;
        if (j < body_len && body[j] == '"') j++; // skip optional quote
        if (j >= body_len || !isDigit(body[j])) continue;
        uint16_t v = 0;
        while (j < body_len && isDigit(body[j])) v = v * 10 + (body[j++] - '0');
        return v;
    }
    return 0;
}

// Find needle starting at *pos, advance *pos past it. Returns true if found.
static bool buf_find(const char *body, int body_len,
                     const char *needle, int needle_len, int &pos) {
    for (; pos <= body_len - needle_len; pos++) {
        if (memcmp(body + pos, needle, needle_len) == 0) {
            pos += needle_len;
            return true;
        }
    }
    return false;
}

static bool buf_contains(const char *body, int body_len, const char *needle) {
    int pos = 0;
    return buf_find(body, body_len, needle, (int)strlen(needle), pos);
}

static int buf_find_char(const char *body, int body_len, char ch, int start) {
    for (int i = start; i < body_len; i++) {
        if (body[i] == ch) return i;
    }
    return -1;
}

static bool buf_extract_json_string(const char *body, int body_len,
                                    const char *key, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';

    int pos = 0;
    if (!buf_find(body, body_len, key, (int)strlen(key), pos)) return false;

    size_t out_len = 0;
    while (pos < body_len && out_len + 1 < out_size) {
        char ch = body[pos++];
        if (ch == '\\' && pos < body_len) {
            out[out_len++] = body[pos++];
            continue;
        }
        if (ch == '"') break;
        out[out_len++] = ch;
    }
    out[out_len] = '\0';
    return out_len > 0;
}

// ── Parsed day scratch buffer ─────────────────────────────────────────────────

struct ParsedContributionDay {
    int32_t days_since_epoch;
    uint16_t count;
    uint8_t  level;
    uint8_t  month;
    bool     has_date;
};

static ParsedContributionDay s_parsed_days[GRID_WEEKS * GRID_DAYS];

// ── Busiest day ───────────────────────────────────────────────────────────────

static void compute_busiest(GithubData &data, int count) {
    data.busiest_day_count = 0;
    data.busiest_day_day   = 0;
    data.busiest_day_month = 0;
    for (int i = 0; i < count; i++) {
        if (!s_parsed_days[i].has_date) continue;
        if (s_parsed_days[i].count <= data.busiest_day_count) continue;
        if (s_parsed_days[i].days_since_epoch > data.latest_data_day_days) continue;
        data.busiest_day_count = s_parsed_days[i].count;
        time_t s = (time_t)s_parsed_days[i].days_since_epoch * 86400;
        struct tm t;
        gmtime_r(&s, &t);
        data.busiest_day_day   = (uint8_t)t.tm_mday;
        data.busiest_day_month = (uint8_t)(t.tm_mon + 1);
    }
}

// ── Main fetch ────────────────────────────────────────────────────────────────

bool github_fetch(const char *username, const char *token, GithubData &data) {
    data.valid               = false;
    data.week_count          = 0;
    data.total_contributions = 0;
    data.busiest_day_count   = 0;
    data.busiest_day_day     = 0;
    data.busiest_day_month   = 0;
    data.current_month_commits = 0;
    data.current_month       = 0;
    data.anchor_week_start_days = 0;
    data.latest_data_day_days   = 0;
    memset(data.days, 0, sizeof(data.days));

    char username_buf[64];
    char token_buf[128];
    trim_ascii_copy(username_buf, sizeof(username_buf), username);
    trim_ascii_copy(token_buf, sizeof(token_buf), token);

    if (username_buf[0] == '\0') {
        Serial.println("[GitHub] Missing username");
        return false;
    }
    if (token_buf[0] == '\0') {
        Serial.println("[GitHub] Missing PAT; skipping GraphQL request");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12);

    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(12000);
    if (!http.begin(client, GRAPHQL_URL)) return false;

    const char *header_keys[] = {"Date"};
    http.collectHeaders(header_keys, 1);

    char auth_buf[256];
    snprintf(auth_buf, sizeof(auth_buf), "Bearer %s", token_buf);
    http.addHeader("Accept", "application/json");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP32-C6-GithubMonitor");
    http.addHeader("Authorization", auth_buf);

    char query_buf[320];
    snprintf(query_buf, sizeof(query_buf),
        "{\"query\":\"%s\",\"variables\":{\"login\":\"%s\"}}",
        GRAPHQL_QUERY, username_buf);

    int code = http.POST(query_buf);

    if (code != 200) {
        Serial.printf("[GitHub] HTTP %d\n", code);
        String error_body = http.getString();
        if (error_body.length() > 0)
            Serial.printf("[GitHub] Error body: %s\n", error_body.c_str());
        http.end();
        return false;
    }

    String server_date_header = http.header("Date");
    int32_t server_day = INT32_MAX;
    parse_http_date_days(server_date_header, server_day);

    // Receive body via writeToStream() which handles chunked encoding correctly.
    // s_body is static — no heap allocation, safe to reuse every fetch.
    BufStream buf;
    http.writeToStream(&buf);
    http.end();

    if (buf._overflow) {
        Serial.printf("[GitHub] Response overflow (>%d bytes)\n", BODY_BUF_SIZE);
        return false;
    }
    s_body[buf._len] = '\0';
    int body_len = buf._len;

    Serial.printf("[GitHub] Response length: %d bytes\n", body_len);

    if (buf_contains(s_body, body_len, "\"errors\":")) {
        char error_msg[160];
        if (buf_extract_json_string(s_body, body_len, "\"message\":\"",
                                    error_msg, sizeof(error_msg))) {
            Serial.printf("[GitHub] GraphQL error: %s\n", error_msg);
        } else {
            Serial.println("[GitHub] GraphQL returned errors");
        }
        return false;
    }

    // ── Parse body ────────────────────────────────────────────────────────────

    const char KEY_TC[]  = "\"totalContributions\":";
    const char KEY_CD[]  = "\"contributionDays\":[";
    const char KEY_CC[]  = "\"contributionCount\":";
    const char KEY_DT[]  = "\"date\":\"";

    int pos = 0;
    data.total_contributions = buf_parse_uint16(s_body, body_len, KEY_TC, 0);

    int parsed_day_count  = 0;
    int parsed_week_count = 0;
    int32_t latest_day     = INT32_MIN;
    int32_t latest_day_any = INT32_MIN;

    while (parsed_week_count < GRID_WEEKS &&
           parsed_day_count  < GRID_WEEKS * GRID_DAYS) {

        if (!buf_find(s_body, body_len, KEY_CD, sizeof(KEY_CD)-1, pos)) break;
        parsed_week_count++;

        for (int d = 0; d < GRID_DAYS; d++) {
            int day_start = buf_find_char(s_body, body_len, '{', pos);
            if (day_start < 0) goto done;
            int day_end = buf_find_char(s_body, body_len, '}', day_start + 1);
            if (day_end < 0) goto done;

            uint16_t count = buf_parse_uint16(s_body, day_end + 1, KEY_CC, day_start);

            // read date
            int32_t day_epoch = 0;
            uint8_t  day_month = 0;
            bool     has_date = false;
            int day_pos = day_start;
            if (buf_find(s_body, day_end + 1, KEY_DT, sizeof(KEY_DT)-1, day_pos)) {
                if (day_pos + 10 <= day_end + 1) {
                    uint8_t dummy;
                    has_date = parse_iso_date(s_body + day_pos, day_epoch, day_month, dummy);
                    if (has_date) {
                        if (day_epoch > latest_day_any) latest_day_any = day_epoch;
                        if (day_epoch <= server_day && day_epoch > latest_day)
                            latest_day = day_epoch;
                    }
                }
            }
            pos = day_end + 1;

            s_parsed_days[parsed_day_count].days_since_epoch = day_epoch;
            s_parsed_days[parsed_day_count].count    = count;
            s_parsed_days[parsed_day_count].level    = contribution_level(count);
            s_parsed_days[parsed_day_count].month    = day_month;
            s_parsed_days[parsed_day_count].has_date = has_date;
            parsed_day_count++;
        }
    }
    done:

    if (latest_day == INT32_MIN) latest_day = latest_day_any;

    if (parsed_week_count == 0) {
        Serial.println("[GitHub] Parse failed - no weeks found");
        return false;
    }

    // ── Map into grid ─────────────────────────────────────────────────────────
    if (latest_day != INT32_MIN) {
        data.anchor_week_start_days = latest_day - weekday_sun0(latest_day);
        data.latest_data_day_days   = latest_day;
        data.week_count             = GRID_WEEKS;

        for (int i = 0; i < parsed_day_count; i++) {
            if (!s_parsed_days[i].has_date) continue;
            uint8_t  wd    = weekday_sun0(s_parsed_days[i].days_since_epoch);
            int32_t  ws    = s_parsed_days[i].days_since_epoch - wd;
            int32_t  delta = data.anchor_week_start_days - ws;
            if (delta < 0 || (delta % 7) != 0) continue;
            int week_diff = (int)(delta / 7);
            if (week_diff >= GRID_WEEKS) continue;
            int tw = (GRID_WEEKS - 1) - week_diff;
            data.days[tw][wd].count = s_parsed_days[i].count;
            data.days[tw][wd].level = s_parsed_days[i].level;
        }
    } else {
        data.week_count = parsed_week_count;
        for (int w = 0; w < parsed_week_count; w++) {
            for (int d = 0; d < GRID_DAYS; d++) {
                int idx = w * GRID_DAYS + d;
                if (idx >= parsed_day_count) break;
                data.days[w][d].count = s_parsed_days[idx].count;
                data.days[w][d].level = s_parsed_days[idx].level;
            }
        }
    }

    // ── Current month ─────────────────────────────────────────────────────────
    if (latest_day != INT32_MIN) {
        time_t secs = (time_t)latest_day * 86400;
        struct tm tm_now;
        gmtime_r(&secs, &tm_now);
        data.current_month = (uint8_t)(tm_now.tm_mon + 1);
        int current_year = tm_now.tm_year + 1900;
        int next_month = data.current_month == 12 ? 1 : data.current_month + 1;
        int next_year = data.current_month == 12 ? current_year + 1 : current_year;
        int32_t month_start_days = days_from_civil(current_year, data.current_month, 1);
        int32_t next_month_start_days = days_from_civil(next_year, (unsigned)next_month, 1);
        for (int i = 0; i < parsed_day_count; i++) {
            if (s_parsed_days[i].has_date &&
                s_parsed_days[i].days_since_epoch >= month_start_days &&
                s_parsed_days[i].days_since_epoch < next_month_start_days)
                data.current_month_commits += s_parsed_days[i].count;
        }
    }

    compute_busiest(data, parsed_day_count);
    data.valid = true;
    Serial.printf("[GitHub] OK - %d weeks, %d contributions, month(%d) commits %d, busiest %d\n",
                  data.week_count, data.total_contributions,
                  data.current_month, data.current_month_commits, data.busiest_day_count);
    return true;
}
