#include <WiFi.h>
#include "web_server.h"
#include <WebServer.h>
#include <Arduino.h>

static WebServer server(80);
static Config        *s_cfg      = nullptr;
static ConfigApplyFn  s_apply_fn = nullptr;

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
              ".btn-row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:16px}"
              ".btn-save,.btn-reboot,.btn-reset{border:none;border-radius:6px;padding:11px 6px;"
              "font-size:13px;font-weight:600;cursor:pointer;width:100%}"
              ".btn-save{background:#238636;color:#fff}"
              ".btn-save:hover{background:#2ea043}"
              ".btn-reboot{background:#1f6feb;color:#fff}"
              ".btn-reboot:hover{background:#388bfd}"
              ".btn-reset{background:#6e211a;color:#fff}"
              ".btn-reset:hover{background:#b91c1c}"
              "#toast{position:fixed;bottom:20px;left:50%;"
              "transform:translateX(-50%) translateY(20px);"
              "background:#1a7f37;color:#fff;padding:10px 20px;border-radius:8px;"
              "font-size:14px;opacity:0;transition:opacity .3s,transform .3s;"
              "pointer-events:none;z-index:999}"
              "</style></head><body>"
              "<h1>GitHub Monitor</h1>"
              "<div class='sub'>Device IP: ");
    html += ip;
    if (ap_mode) html += F(" <span class='warn'>&#9888; Setup mode — connect to GithubMonitor-Setup then visit 192.168.4.1</span>");
    html += F("</div><form id='cfg'>");

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
              "<label>LCD Brightness (%)</label>"
              "<div class='range-row'>"
              "<input type='range' name='brightness' min='1' max='100' value='");
    html += s_cfg->brightness;
    html += F("' oninput='this.nextElementSibling.textContent=this.value+\"%\"'>"
              "<span class='range-val'>");
    html += s_cfg->brightness;
    html += F("%</span></div>"
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

    // RGB LED card
    html += F("<div class='card'><h2>RGB LED</h2>"
              "<label>LED Brightness (%)</label>"
              "<div class='range-row'>"
              "<input type='range' name='rgb_bright' min='1' max='100' value='");
    html += s_cfg->rgb_brightness;
    html += F("' oninput='this.nextElementSibling.textContent=this.value+\"%\"'>"
              "<span class='range-val'>");
    html += s_cfg->rgb_brightness;
    html += F("%</span></div>"
              "<label>Min breath period ms (fastest, high streak)</label>"
              "<input type='number' name='rgb_pmin' min='400' max='4000' value='");
    html += s_cfg->rgb_period_min_ms;
    html += F("'><label>Max breath period ms (slowest, no streak)</label>"
              "<input type='number' name='rgb_pmax' min='2000' max='20000' value='");
    html += s_cfg->rgb_period_max_ms;
    html += F("'><label>Streak max (streak that achieves min period)</label>"
              "<input type='number' name='rgb_smax' min='1' max='365' value='");
    html += s_cfg->rgb_streak_max;
    html += F("'></div>");

    // MQTT card
    html += F("<div class='card'><h2>MQTT</h2>"
              "<label>Broker host / IP (leave empty to disable)</label>"
              "<input type='text' name='mqtt_host' value='");
    html += s_cfg->mqtt_broker;
    html += F("'><label>Broker port</label>"
              "<input type='number' name='mqtt_port' min='1' max='65535' value='");
    html += s_cfg->mqtt_port;
    html += F("'><label>Combined brightness topic &#8212; sets LCD &amp; LED (0&#8211;100)</label>"
              "<input type='text' name='mqtt_ctopic' value='");
    html += s_cfg->mqtt_combined_topic;
    html += F("'><label>LCD-only brightness topic (0&#8211;100)</label>"
              "<input type='text' name='mqtt_lcd' value='");
    html += s_cfg->mqtt_lcd_topic;
    html += F("'><label>LED-only brightness topic (0&#8211;100)</label>"
              "<input type='text' name='mqtt_ltopic' value='");
    html += s_cfg->mqtt_led_brightness_topic;
    html += F("'></div>");

    html += F("<div class='btn-row'>"
              "<button type='button' class='btn-save' id='btnSave'>Save</button>"
              "<button type='button' class='btn-reboot' id='btnReboot'>Reboot</button>"
              "<button type='button' class='btn-reset' id='btnReset'>Factory Reset</button>"
              "</div>"
              "</form>"
              "<div id='toast'></div>"
              "<script>"
              "function showToast(m,ok){"
              "var t=document.getElementById('toast');"
              "t.textContent=m;"
              "t.style.background=ok?'#1a7f37':'#b91c1c';"
              "t.style.opacity='1';"
              "t.style.transform='translateX(-50%) translateY(0)';"
              "setTimeout(function(){t.style.opacity='0';"
              "t.style.transform='translateX(-50%) translateY(20px)';},2800);}"
              "document.getElementById('btnSave').onclick=function(){"
              "var p=new URLSearchParams(new FormData(document.getElementById('cfg')));"
              "fetch('/save',{method:'POST',"
              "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
              "body:p.toString()})"
              ".then(function(r){return r.json();})"
              ".then(function(d){showToast(d.msg,d.ok);})"
              ".catch(function(){showToast('Save failed',false);});};"
              "document.getElementById('btnReboot').onclick=function(){"
              "if(!confirm('Reboot the device?'))return;"
              "fetch('/reboot',{method:'POST'})"
              ".then(function(){showToast('Rebooting...',true);});};"
              "document.getElementById('btnReset').onclick=function(){"
              "if(!confirm('This will erase ALL settings and reboot. Continue?'))return;"
              "fetch('/reset',{method:'POST'})"
              ".then(function(){showToast('Resetting...',true);});};"
              "</script></body></html>");

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
    if (server.hasArg("rgb_pmin")) s_cfg->rgb_period_min_ms = (uint16_t)server.arg("rgb_pmin").toInt();
    if (server.hasArg("rgb_pmax")) s_cfg->rgb_period_max_ms = (uint16_t)server.arg("rgb_pmax").toInt();
    if (server.hasArg("rgb_smax")) s_cfg->rgb_streak_max    = (uint8_t) server.arg("rgb_smax").toInt();
    if (server.hasArg("rgb_bright")) s_cfg->rgb_brightness  = (uint8_t) server.arg("rgb_bright").toInt();
    get_str("mqtt_host",    s_cfg->mqtt_broker,              sizeof(s_cfg->mqtt_broker));
    if (server.hasArg("mqtt_port")) s_cfg->mqtt_port        = (uint16_t)server.arg("mqtt_port").toInt();
    get_str("mqtt_ctopic", s_cfg->mqtt_combined_topic,      sizeof(s_cfg->mqtt_combined_topic));
    get_str("mqtt_lcd",    s_cfg->mqtt_lcd_topic,           sizeof(s_cfg->mqtt_lcd_topic));
    get_str("mqtt_ltopic", s_cfg->mqtt_led_brightness_topic,sizeof(s_cfg->mqtt_led_brightness_topic));

    config_save(*s_cfg);

    if (s_apply_fn) s_apply_fn(*s_cfg);

    server.send(200, "application/json",
        "{\"ok\":true,\"msg\":\"Saved. WiFi & GitHub changes need a reboot.\"}");
}

static void handle_reboot() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

static void handle_reset() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    config_reset();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

void web_server_start(Config &cfg, ConfigApplyFn on_apply) {
    s_cfg      = &cfg;
    s_apply_fn = on_apply;
    server.on("/",       HTTP_GET,  handle_root);
    server.on("/save",   HTTP_POST, handle_save);
    server.on("/reboot", HTTP_POST, handle_reboot);
    server.on("/reset",  HTTP_POST, handle_reset);
    server.begin();
    Serial.printf("[Web] Server started at http://%s/\n",
                  WiFi.localIP().toString().c_str());
}

void web_server_handle() {
    server.handleClient();
}
