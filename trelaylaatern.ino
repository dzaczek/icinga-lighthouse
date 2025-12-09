/*
 * ======================================================================================
 * Project: icinga-lighthouse
 * Repository: https://github.com/dzaczek/icinga-lighthouse
 * Description: LilyGO T-Relay 4-Channel Monitor for Icinga2 API
 * Version: 4.1 (International / Dual Monitor / Stable)
 * ======================================================================================
 * FEATURES:
 * - Architecture: Dictionary-based Multi-language system (English/Polish).
 * - Logic: Dual Monitor (Hosts + Services) via Icinga2 API.
 * - Stability: Safe Mode (No bootloops), Brownout disabled, Heap JSON.
 * - Hardware: Status LED Heartbeat, Manual Test Mode.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "soc/soc.h"             
#include "soc/rtc_cntl_reg.h"    

// --- PIN DEFINITIONS ---
#define RELAY_1_PIN 21 
#define RELAY_2_PIN 19 
#define RELAY_3_PIN 18 
#define RELAY_4_PIN 5 
#define STATUS_LED_PIN 25 

#define RELAY_ON  HIGH  
#define RELAY_OFF LOW   

Preferences preferences;
WebServer server(80);

// --- LANGUAGE DICTIONARY STRUCT ---
struct LangText {
  String title;
  String st_ok;
  String st_err;
  String st_warn;
  String st_man;
  String t_test;
  String btn_siren;
  String btn_end;
  String sec_net;
  String sec_api;
  String sec_time;
  String lbl_poll;
  String lbl_init;
  String lbl_rint;
  String lbl_rdur;
  String btn_save;
  String msg_saved;
};
LangText txt; // Global object holding current texts

// --- CONFIGURATION ---
String wifi_ssid = "";
String wifi_pass = "";

// Default: Service Critical (2) and Host Down (not UP/0)
String icinga_url_svc = "https://192.168.1.100:5665/v1/objects/services?filter=service.state==2"; 
String icinga_url_host = "https://192.168.1.100:5665/v1/objects/hosts?filter=host.state!=0"; 

String icinga_user = "root";
String icinga_pass = "icinga";
String web_user = "admin";
String web_pass = "admin";
String system_lang = "en"; // Default to English

// Timings
unsigned long poll_interval_ms = 5000;        
unsigned long init_alarm_duration_ms = 30000; 
unsigned long reminder_interval_ms = 300000;   
unsigned long reminder_duration_ms = 15000;    
unsigned long watchdog_timeout_ms = 60000;     

// State
unsigned long last_poll_time = 0;
unsigned long last_successful_data_time = 0;
unsigned long last_wifi_check_time = 0;

// LED & Manual
unsigned long last_blink_time = 0;
bool led_state = false;
unsigned long last_manual_action_time = 0;
bool manual_override_active = false;

// Status
String last_connection_status = "STARTUP";
String last_icinga_object_name = "None";
bool icinga_reachable = false; 
bool is_alarm_active = false;      
bool is_network_error = false;     
bool wifi_connected_mode = false;  

enum AlarmState { STATE_IDLE, STATE_INITIAL_ALARM, STATE_COOLDOWN, STATE_REMINDER_ALARM };
AlarmState current_state = STATE_IDLE;
unsigned long state_start_time = 0;

// Declarations
void loadSettings();
void setLanguage(); // Dictionary loader
void setupWiFi();
void handleRoot();
void handleSave();
void handleToggle();
void checkIcinga();
bool queryIcingaEndpoint(String url, String typeName);
void updateRelayLogic();
void ensureWiFiConnection();
void updateStatusLED();
String getUptimeStr();

void setup() {
  // DISABLE BROWNOUT DETECTOR (Fix for bootloops on power spikes)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- icinga-lighthouse v4.1 Booting... ---");
  Serial.println("Repo: https://github.com/dzaczek/icinga-lighthouse");

  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  digitalWrite(RELAY_1_PIN, RELAY_OFF);
  digitalWrite(RELAY_2_PIN, RELAY_OFF);
  digitalWrite(RELAY_3_PIN, RELAY_OFF);
  digitalWrite(RELAY_4_PIN, RELAY_OFF);
  digitalWrite(STATUS_LED_PIN, LOW);

  loadSettings(); 
  setupWiFi();

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/toggle", handleToggle);
  server.begin();
  
  last_successful_data_time = millis(); 
}

void loop() {
  server.handleClient();
  unsigned long current_millis = millis();
  updateStatusLED();

  if (manual_override_active) {
    if (current_millis - last_manual_action_time > 60000) {
      manual_override_active = false;
    }
  }

  if (wifi_connected_mode) {
    ensureWiFiConnection();
    if (WiFi.status() == WL_CONNECTED) {
       if (current_millis - last_poll_time >= poll_interval_ms) {
         last_poll_time = current_millis;
         checkIcinga();
       }
    } else {
       last_connection_status = "No WiFi";
       icinga_reachable = false;
    }
  } else {
    is_network_error = true;
    last_connection_status = "AP Mode";
    icinga_reachable = false;
  }

  // Soft Watchdog (Logic only)
  if (millis() - last_successful_data_time > watchdog_timeout_ms) {
    if (!is_network_error) {
      is_network_error = true;
      last_connection_status = "Data Timeout";
      is_alarm_active = false; 
      icinga_reachable = false; 
    }
  }

  updateRelayLogic();
}

// --- DICTIONARY LOGIC ---
void setLanguage() {
  if (system_lang == "pl") {
    txt.title = "icinga-lighthouse";
    txt.st_ok = "SYSTEM OK";
    txt.st_err = "ALARM KRYTYCZNY";
    txt.st_warn = "BŁĄD POŁĄCZENIA / API";
    txt.st_man = "TRYB RĘCZNY (TEST)";
    txt.t_test = "Test Przekaźników";
    txt.btn_siren = "Przełącz Syrenę";
    txt.btn_end = "ZAKONCZ TEST";
    txt.sec_net = "Sieć WiFi";
    txt.sec_api = "Konfiguracja API Icinga2";
    txt.sec_time = "Czasy i Logika";
    txt.lbl_poll = "Interwał sprawdzania (s)";
    txt.lbl_init = "Czas trwania alarmu (s)";
    txt.lbl_rint = "Przypomnienie co (min)";
    txt.lbl_rdur = "Czas przypomnienia (s)";
    txt.btn_save = "ZAPISZ I RESTARTUJ";
    txt.msg_saved = "Zapisano! Restart urzadzenia...";
  } else {
    // Default English
    txt.title = "icinga-lighthouse";
    txt.st_ok = "SYSTEM OK";
    txt.st_err = "CRITICAL ALARM";
    txt.st_warn = "CONNECTION / API ERROR";
    txt.st_man = "MANUAL MODE (TEST)";
    txt.t_test = "Relay Test";
    txt.btn_siren = "Toggle Siren";
    txt.btn_end = "END TEST";
    txt.sec_net = "WiFi Network";
    txt.sec_api = "Icinga2 API Configuration";
    txt.sec_time = "Timing & Logic";
    txt.lbl_poll = "Poll Interval (s)";
    txt.lbl_init = "Alarm Duration (s)";
    txt.lbl_rint = "Reminder Interval (min)";
    txt.lbl_rdur = "Reminder Duration (s)";
    txt.btn_save = "SAVE AND RESTART";
    txt.msg_saved = "Saved! Rebooting device...";
  }
}

void updateStatusLED() {
  unsigned long now = millis();
  unsigned long interval = icinga_reachable ? 1000 : 200;
  if (now - last_blink_time > interval) {
    last_blink_time = now;
    led_state = !led_state; 
    digitalWrite(STATUS_LED_PIN, led_state ? HIGH : LOW);
  }
}

void checkIcinga() {
  // Check Services first
  bool service_alarm = queryIcingaEndpoint(icinga_url_svc, "Service");
  
  // Check Hosts
  bool host_alarm = false;
  if (!service_alarm && icinga_url_host.length() > 5) {
      host_alarm = queryIcingaEndpoint(icinga_url_host, "Host");
  }
  
  bool total_alarm = service_alarm || host_alarm;
  
  if (total_alarm != is_alarm_active) {
      is_alarm_active = total_alarm;
      if (!total_alarm) last_icinga_object_name = "None";
  }
}

bool queryIcingaEndpoint(String url, String typeName) {
  if (url == "") return false;

  WiFiClientSecure client;
  client.setInsecure(); 
  client.setTimeout(3); 

  HTTPClient http;
  http.useHTTP10(true); 
  http.setTimeout(3000); 
  
  if (!http.begin(client, url)) {
    last_connection_status = "Conn Fail (" + typeName + ")";
    icinga_reachable = false; 
    return false; 
  }

  http.setAuthorization(icinga_user.c_str(), icinga_pass.c_str());
  http.addHeader("Accept", "application/json");

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    icinga_reachable = true; 
    last_successful_data_time = millis();
    is_network_error = false;
    last_connection_status = "OK (200)";

    Stream& stream = http.getStream();
    StaticJsonDocument<200> filter;
    filter["results"][0]["name"] = true; 
    filter["results"][0]["attrs"]["display_name"] = true; 
    
    // HEAP Allocation for JSON
    DynamicJsonDocument doc(4096); 
    DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    http.end(); 

    if (!error) {
      JsonArray results = doc["results"].as<JsonArray>();
      if (results.size() > 0) {
          const char* name = results[0]["attrs"]["display_name"] | results[0]["name"] | "Unknown";
          last_icinga_object_name = typeName + ": " + String(name);
          return true; // ALARM
      }
    }
  } else {
    last_connection_status = "HTTP " + String(httpCode);
    icinga_reachable = false; 
    http.end();
  }
  return false;
}

void setupWiFi() {
  if (wifi_ssid == "") {
    WiFi.softAP("icinga-lighthouse-cfg", "admin123");
    wifi_connected_mode = false;
    return;
  }
  
  // WiFi Stability Settings
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_11dBm); 
  
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected_mode = true;
    last_connection_status = "WiFi Connected";
  } else {
    WiFi.softAP("icinga-lighthouse-cfg", "admin123");
    wifi_connected_mode = false;
    last_connection_status = "WiFi Fail";
  }
}

void updateRelayLogic() {
  if (manual_override_active) return; 

  digitalWrite(RELAY_2_PIN, RELAY_OFF);
  digitalWrite(RELAY_3_PIN, RELAY_OFF);
  digitalWrite(RELAY_4_PIN, RELAY_OFF);

  if (is_network_error) {
    if (digitalRead(RELAY_1_PIN) == RELAY_ON) digitalWrite(RELAY_1_PIN, RELAY_OFF);
    return; 
  }

  unsigned long now = millis();
  unsigned long elapsed = now - state_start_time;

  switch (current_state) {
    case STATE_IDLE:
      if (is_alarm_active) {
        digitalWrite(RELAY_1_PIN, RELAY_ON);
        current_state = STATE_INITIAL_ALARM;
        state_start_time = now;
      }
      break;
    case STATE_INITIAL_ALARM:
      if (!is_alarm_active) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_IDLE;
      } else if (elapsed >= init_alarm_duration_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_COOLDOWN;
        state_start_time = now;
      }
      break;
    case STATE_COOLDOWN:
      if (!is_alarm_active) {
        current_state = STATE_IDLE;
      } else if (elapsed >= reminder_interval_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_ON);
        current_state = STATE_REMINDER_ALARM;
        state_start_time = now;
      }
      break;
    case STATE_REMINDER_ALARM:
      if (!is_alarm_active) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_IDLE;
      } else if (elapsed >= reminder_duration_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_COOLDOWN;
        state_start_time = now;
      }
      break;
  }
}

void handleSave() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) return server.requestAuthentication();

  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("wpass");
  new_ssid.trim(); new_pass.trim();

  preferences.putString("ssid", new_ssid);
  preferences.putString("wpass", new_pass);
  preferences.putString("iurl_s", server.arg("iurl_s"));
  preferences.putString("iurl_h", server.arg("iurl_h"));
  preferences.putString("iuser", server.arg("iuser"));
  preferences.putString("ipass", server.arg("ipass"));
  preferences.putString("lang", server.arg("lang")); 
  
  unsigned long p_sec = server.arg("poll").toInt(); if(p_sec < 1) p_sec = 1;
  preferences.putULong("poll", p_sec * 1000);
  unsigned long init_sec = server.arg("init").toInt(); preferences.putULong("init", init_sec * 1000);
  unsigned long rint_min = server.arg("rint").toInt(); preferences.putULong("rint", rint_min * 60 * 1000);
  unsigned long rdur_sec = server.arg("rdur").toInt(); preferences.putULong("rdur", rdur_sec * 1000);

  system_lang = server.arg("lang");
  setLanguage();

  server.send(200, "text/html", "<h1>" + txt.msg_saved + "</h1>");
  delay(1000);
  ESP.restart();
}

void handleToggle() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) return server.requestAuthentication();
  int r = server.arg("r").toInt();
  if (r == 0) {
      manual_override_active = false;
      server.sendHeader("Location", "/"); server.send(303);
      return;
  }
  manual_override_active = true;
  last_manual_action_time = millis();
  int pin = -1;
  if (r == 1) pin = RELAY_1_PIN;
  else if (r == 2) pin = RELAY_2_PIN;
  else if (r == 3) pin = RELAY_3_PIN;
  else if (r == 4) pin = RELAY_4_PIN;
  if (pin != -1) {
    int state = digitalRead(pin);
    digitalWrite(pin, !state);
  }
  server.sendHeader("Location", "/"); server.send(303);
}

void ensureWiFiConnection() {
  if (!wifi_connected_mode) return;
  if (millis() - last_wifi_check_time > 5000) {
    last_wifi_check_time = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(); WiFi.reconnect();
    }
  }
}

void handleRoot() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) return server.requestAuthentication();

  String s = "<!DOCTYPE html><html><head>";
  s += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  s += "<title>" + txt.title + "</title>";
  s += "<style>body{font-family:Segoe UI,sans-serif;padding:10px;background:#f4f4f9;color:#333;}";
  s += ".status{padding:15px;border-radius:5px;margin-bottom:15px;color:white;text-align:center;font-weight:bold;}";
  s += ".ok{background:#28a745;} .err{background:#dc3545;} .warn{background:#ffc107;color:black;}";
  s += "button{padding:10px;width:100%;margin:5px 0;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer;}";
  s += "button:hover{background:#0056b3;} input,select{width:100%;padding:8px;margin:5px 0;box-sizing:border-box;border:1px solid #ccc;}";
  s += ".group{background:white;padding:15px;margin-bottom:15px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.05);}";
  s += ".head-link{font-size:12px;color:#666;text-decoration:none;}</style></head><body>";
  
  s += "<h2>" + txt.title + "</h2>";
  s += "<a href='https://github.com/dzaczek/icinga-lighthouse' class='head-link' target='_blank'>GitHub: dzaczek/icinga-lighthouse</a><br><br>";

  if (manual_override_active) s += "<div class='status warn'>" + txt.st_man + "</div>";
  else if (!icinga_reachable) s += "<div class='status warn'>" + txt.st_warn + "</div>";
  else if (is_alarm_active) s += "<div class='status err'>" + txt.st_err + ": " + last_icinga_object_name + "</div>";
  else s += "<div class='status ok'>" + txt.st_ok + "</div>";
  
  s += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  s += "<p>Info: " + last_connection_status + "</p>";

  s += "<div class='group'><h3>" + txt.t_test + "</h3>";
  s += "<button onclick=\"location.href='/toggle?r=1'\">" + txt.btn_siren + "</button>";
  if (manual_override_active) s += "<button style='background:#dc3545' onclick=\"location.href='/toggle?r=0'\">" + txt.btn_end + "</button>";
  s += "</div>";

  s += "<form action='/save' method='POST'>";
  
  s += "<div class='group'><h3>" + txt.sec_net + "</h3>";
  s += "<label>SSID:</label><input type='text' name='ssid' value='" + wifi_ssid + "'>";
  s += "<label>Pass:</label><input type='password' name='wpass' value='" + wifi_pass + "'>";
  s += "</div>";

  s += "<div class='group'><h3>" + txt.sec_api + "</h3>";
  s += "<label>URL Services (Critical):</label><input type='text' name='iurl_s' value='" + icinga_url_svc + "'>";
  s += "<label>URL Hosts (Down):</label><input type='text' name='iurl_h' value='" + icinga_url_host + "'>";
  s += "<label>API User:</label><input type='text' name='iuser' value='" + icinga_user + "'>";
  s += "<label>API Pass:</label><input type='password' name='ipass' value='" + icinga_pass + "'>";
  s += "</div>";

  s += "<div class='group'><h3>" + txt.sec_time + "</h3>";
  s += "<label>" + txt.lbl_poll + ":</label><input type='number' name='poll' value='" + String(poll_interval_ms / 1000) + "'>";
  s += "<label>" + txt.lbl_init + ":</label><input type='number' name='init' value='" + String(init_alarm_duration_ms / 1000) + "'>";
  s += "<label>" + txt.lbl_rint + ":</label><input type='number' name='rint' value='" + String(reminder_interval_ms / 60000) + "'>";
  s += "<label>" + txt.lbl_rdur + ":</label><input type='number' name='rdur' value='" + String(reminder_duration_ms / 1000) + "'>";
  s += "</div>";

  s += "<div class='group'><h3>Language / Język</h3>";
  s += "<select name='lang'>";
  s += "<option value='en' " + String(system_lang == "en" ? "selected" : "") + ">English</option>";
  s += "<option value='pl' " + String(system_lang == "pl" ? "selected" : "") + ">Polski</option>";
  s += "</select></div>";

  s += "<button type='submit'>" + txt.btn_save + "</button></form></body></html>";
  
  server.send(200, "text/html", s);
}

void loadSettings() {
  preferences.begin("trelay_cfg", false);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_pass = preferences.getString("wpass", "");
  icinga_url_svc = preferences.getString("iurl_s", icinga_url_svc);
  icinga_url_host = preferences.getString("iurl_h", icinga_url_host);
  icinga_user = preferences.getString("iuser", icinga_user);
  icinga_pass = preferences.getString("ipass", icinga_pass);
  web_user = preferences.getString("wu", web_user);
  web_pass = preferences.getString("wp", web_pass);
  system_lang = preferences.getString("lang", "en"); 
  
  poll_interval_ms = preferences.getULong("poll", poll_interval_ms);
  init_alarm_duration_ms = preferences.getULong("init", init_alarm_duration_ms);
  reminder_interval_ms = preferences.getULong("rint", reminder_interval_ms);
  reminder_duration_ms = preferences.getULong("rdur", reminder_duration_ms);

  setLanguage(); 
}

String getUptimeStr() {
  return String(millis() / 1000 / 60) + " min";
}