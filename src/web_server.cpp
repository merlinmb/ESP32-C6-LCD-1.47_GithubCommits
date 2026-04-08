#include <WiFi.h>
#include "web_server.h"
#include <WebServer.h>
#include <Arduino.h>

static WebServer server(80);
static Config   *s_cfg = nullptr;

// Build config page HTML string with current values pre-filled
static String build_page() {
    bool ap_mode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    String ip = ap_mode ? "192.168.4.1" : WiFi.localIP().toString();

    String html;
    html.reserve(4096);

    html += F("<!DOCTYPE html><html lang='en'><head>"
              "<meta charset='UTF-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>GitHub Monitor Config</title>"
              "<style>"
              "*{box-sizing:border-box;margin:0;padding:0}"
              "body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;padding:20px}"
              "h1{color:#39d353;font-size:20px;margin-bottom:4px}"
              ".sub{color:#8b949e;font-size:13px;margin-bottom:24px}"
              ".warn{color:#d29922;font-size:12px;margin-left:8px}"
              ".card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;margin-bottom:16px}"
              ".card h2{color:#e6edf3;font-size:14px;margin-bottom:14px;border-bottom:1px solid #30363d;padding-bottom:8px}"
              "label{display:block;color:#8b949e;font-size:12px;margin-bottom:4px;margin-top:10px}"
              "label:first-of-type{margin-top:0}"
              "input[type=text],input[type=password],input[type=number]{"
              "width:100%;background:#0d1117;border:1px solid #30363d;border-radius:6px;"
              "color:#e6edf3;padding:8px 10px;font-size:14px;outline:none}"
              "input:focus{border-color:#39d353}"
              "input[type=range]{width:100%;accent-color:#39d353}"
              ".range-row{display:flex;align-items:center;gap:10px;margin-top:4px}"
              ".range-row input{flex:1}"
              ".range-val{color:#39d353;font-size:13px;min-width:40px;text-align:right}"
              "button{background:#238636;color:#fff;border:none;border-radius:6px;"
              "padding:11px 24px;font-size:15px;cursor:pointer;width:100%;margin-top:16px}"
              "button:hover{background:#2ea043}"
              "</style></head><body>"
              "<h1>GitHub Monitor</h1>"
              "<div class='sub'>Device IP: ");
    html += ip;
    if (ap_mode) html += F(" <span class='warn'>&#9888; Setup mode — connect to GithubMonitor-Setup then visit 192.168.4.1</span>");
    html += F("</div><form method='POST' action='/save'>");

    // WiFi card
    html += F("<div class='card'><h2>WiFi</h2>"
              "<label>SSID</label>"
              "<input type='text' name='wifi_ssid' value='");
    html += s_cfg->wifi_ssid;
    html += F("'><label>Password</label>"
              "<input type='password' name='wifi_pass' value='");
    html += s_cfg->wifi_password;
    html += F("'></div>");

    // GitHub card
    html += F("<div class='card'><h2>GitHub</h2>"
              "<label>Username</label>"
              "<input type='text' name='gh_user' value='");
    html += s_cfg->github_username;
    html += F("'><label>Personal Access Token (read:user scope)</label>"
              "<input type='password' name='gh_token' value='");
    html += s_cfg->github_token;
    html += F("'></div>");

    // Display card
    html += F("<div class='card'><h2>Display</h2>"
              "<label>Brightness (0&#8211;255)</label>"
              "<div class='range-row'>"
              "<input type='range' name='brightness' min='0' max='255' value='");
    html += s_cfg->brightness;
    html += F("' oninput='this.nextElementSibling.textContent=this.value'>"
              "<span class='range-val'>");
    html += s_cfg->brightness;
    html += F("</span></div>"
              "<label>Screen switch interval (seconds)</label>"
              "<input type='number' name='switch_sec' min='5' max='3600' value='");
    html += s_cfg->screen_switch_secs;
    html += F("'><label>Data refresh interval (minutes)</label>"
              "<input type='number' name='refresh_min' min='1' max='1440' value='");
    html += s_cfg->refresh_interval_min;
    html += F("'></div>");

    // Animation card
    html += F("<div class='card'><h2>Animation</h2>"
              "<label>Animate top % of active days</label>"
              "<div class='range-row'>"
              "<input type='range' name='anim_pct' min='1' max='100' value='");
    html += s_cfg->anim_top_pct;
    html += F("' oninput='this.nextElementSibling.textContent=this.value+\"%\"'>"
              "<span class='range-val'>");
    html += s_cfg->anim_top_pct;
    html += F("%</span></div>"
              "<label>Animation period (ms)</label>"
              "<input type='number' name='anim_ms' min='500' max='10000' value='");
    html += s_cfg->anim_period_ms;
    html += F("'></div>");

    html += F("<button type='submit'>Save &amp; Reboot</button>"
              "</form></body></html>");

    return html;
}

static void handle_root() {
    server.send(200, "text/html", build_page());
}

static void handle_save() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }

    auto get_str = [&](const char *key, char *dst, size_t len) {
        if (server.hasArg(key)) {
            String v = server.arg(key);
            strncpy(dst, v.c_str(), len - 1);
            dst[len - 1] = '\0';
        }
    };

    get_str("wifi_ssid", s_cfg->wifi_ssid,       sizeof(s_cfg->wifi_ssid));
    get_str("wifi_pass", s_cfg->wifi_password,    sizeof(s_cfg->wifi_password));
    get_str("gh_user",   s_cfg->github_username,  sizeof(s_cfg->github_username));
    get_str("gh_token",  s_cfg->github_token,     sizeof(s_cfg->github_token));

    if (server.hasArg("brightness"))  s_cfg->brightness           = (uint8_t) server.arg("brightness").toInt();
    if (server.hasArg("switch_sec"))  s_cfg->screen_switch_secs   = (uint16_t)server.arg("switch_sec").toInt();
    if (server.hasArg("refresh_min")) s_cfg->refresh_interval_min = (uint16_t)server.arg("refresh_min").toInt();
    if (server.hasArg("anim_pct"))    s_cfg->anim_top_pct         = (uint8_t) server.arg("anim_pct").toInt();
    if (server.hasArg("anim_ms"))     s_cfg->anim_period_ms       = (uint16_t)server.arg("anim_ms").toInt();

    config_save(*s_cfg);

    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<style>body{background:#0d1117;color:#39d353;font-family:sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0;flex-direction:column;gap:12px}"
        "h1{font-size:22px}p{color:#8b949e;font-size:14px}</style></head>"
        "<body><h1>Saved &#8212; rebooting&hellip;</h1>"
        "<p>The device will restart and connect with the new settings.</p>"
        "</body></html>");

    delay(1000);
    ESP.restart();
}

void web_server_start(Config &cfg) {
    s_cfg = &cfg;
    server.on("/",     HTTP_GET,  handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.begin();
    Serial.printf("[Web] Server started at http://%s/\n",
                  WiFi.localIP().toString().c_str());
}

void web_server_handle() {
    server.handleClient();
}
