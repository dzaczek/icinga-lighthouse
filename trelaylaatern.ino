/**
 * LilyGO T-Relay 4-Channel Icinga Monitor (Production Version v2.3)
 * * Features:
 * - Icinga API Polling (Stream parsing for efficiency).
 * - Universal Logic: Works for Services (Critical) and Hosts (Down) based on API URL filter.
 * - Relay 1: Alarm Siren (Initial Alarm -> Silence -> Reminder).
 * - Relay 4: System Failure Indicator (Network/Data Watchdog).
 * - Watchdog: Hardware WDT + Logic WDT to ensure stability.
 * - Web Panel: Multi-language support (PL/EN), Status Dashboard.
 * - Persistent Storage: Settings saved in Flash (Preferences).
 * - COMPATIBILITY: ESP32 Core v3.0+
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// --- PIN DEFINITIONS (LilyGO T-Relay) ---
#define RELAY_1_PIN 21 // Alarm (Siren)
#define RELAY_2_PIN 19 // Spare
#define RELAY_3_PIN 18 // Spare
#define RELAY_4_PIN 5  // System Failure Indicator

// --- HARDWARE WATCHDOG CONFIG ---
#define WDT_TIMEOUT_SECONDS 15 

// ==========================================
// --- USER CONFIGURATION - RELAY LOGIC ---
// ==========================================
// LilyGO T-Relay typically uses Active HIGH logic.
// However, depending on your wiring (NO vs NC terminals):
//
// OPTION A: Wired to NO (Normally Open) - Recommended for alarms
//           Use: ON=HIGH, OFF=LOW
//
// OPTION B: Wired to NC (Normally Closed)
//           Use: ON=LOW, OFF=HIGH
//
// If the siren turns ON immediately at startup, SWAP these values!

#define RELAY_ON  HIGH  
#define RELAY_OFF LOW   

// ==========================================

// --- CONFIGURATION VARIABLES (Defaults) ---
Preferences preferences;
WebServer server(80);

// Network & Access
String wifi_ssid = "";
String wifi_pass = "";
// Default URL for Services (User can change to /objects/hosts?filter=host.state==1 for Host monitoring)
String icinga_url = "https://192.168.1.100:5665/v1/objects/services?filter=service.state==2"; 
String icinga_user = "root";
String icinga_pass = "icinga";
String web_user = "admin";
String web_pass = "admin";
String system_lang = "pl"; // "pl" or "en" for the Web Interface language

// Timing (milliseconds)
unsigned long poll_interval_ms = 2000;        // Polling frequency
unsigned long watchdog_timeout_ms = 60000;     // Data staleness limit
unsigned long init_alarm_duration_ms = 30000;  // Initial siren duration
unsigned long reminder_interval_ms = 300000;   // Reminder silence duration
unsigned long reminder_duration_ms = 15000;    // Reminder siren duration

// --- SYSTEM STATE VARIABLES ---
unsigned long last_poll_time = 0;
unsigned long last_successful_data_time = 0;
unsigned long last_wifi_check_time = 0;
unsigned long last_alarm_trigger_time = 0; // Timestamp of the last Relay 1 activation

String last_connection_status = "STARTUP"; // Text status for Web UI
String last_icinga_object_name = "None";   // Name of the object causing alarm

bool is_alarm_active = false;      // Does Icinga report a CRITICAL/DOWN state?
bool is_network_error = false;     // Is there a connection/data failure?
bool wifi_connected_mode = false;  // STA mode (true) or AP mode (false)

// Alarm State Machine
enum AlarmState {
  STATE_IDLE,             // No alarm
  STATE_INITIAL_ALARM,    // First loud alarm
  STATE_COOLDOWN,         // Waiting for reminder (Silence)
  STATE_REMINDER_ALARM    // Reminder alarm
};

AlarmState current_state = STATE_IDLE;
unsigned long state_start_time = 0;

// Function Declarations
void loadSettings();
void setupWiFi();
void handleRoot();
void handleSave();
void checkIcinga();
void updateRelayLogic();
void checkDataWatchdog();
void ensureWiFiConnection();
String getUptimeStr();
String formatTimeAgo(unsigned long timestamp);

void setup() {
  Serial.begin(115200);
  
  // --- HARDWARE WATCHDOG INIT (ESP32 Core v3.0 Compatible) ---
  // In v3.0, esp_task_wdt_init takes a config struct
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
      .idle_core_mask = (1 << 0), // Watch Core 0
      .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL); // Add current thread to WDT

  // Pin Configuration
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  
  // Initial state - Force OFF based on User Configuration
  digitalWrite(RELAY_1_PIN, RELAY_OFF);
  digitalWrite(RELAY_2_PIN, RELAY_OFF);
  digitalWrite(RELAY_3_PIN, RELAY_OFF);
  digitalWrite(RELAY_4_PIN, RELAY_OFF);

  loadSettings();
  setupWiFi();

  // Web Server Setup
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web Server started.");
  
  // Initialize watchdog timer
  last_successful_data_time = millis(); 
}

void loop() {
  // 1. Reset Hardware Watchdog (Kick the dog)
  esp_task_wdt_reset();

  server.handleClient();
  unsigned long current_millis = millis();

  // 2. WiFi & Polling Logic
  // Only attempt if we are in Client Mode (not AP)
  if (wifi_connected_mode) {
    ensureWiFiConnection();

    // Only poll Icinga if WiFi is actually connected
    if (WiFi.status() == WL_CONNECTED) {
       if (current_millis - last_poll_time >= poll_interval_ms) {
         last_poll_time = current_millis;
         checkIcinga();
       }
    } else {
       // WiFi physically down, treat as network error immediately or let watchdog catch it
       last_connection_status = (system_lang == "pl") ? "Brak WiFi" : "No WiFi";
    }
  } else {
    // We are in AP mode (Configuration mode), so monitoring is technically "failed/offline"
    is_network_error = true;
    last_connection_status = (system_lang == "pl") ? "Tryb AP (Konfiguracja)" : "AP Mode (Config)";
  }

  // 3. Data Watchdog (Fail-Safe) - Runs regardless of WiFi state
  checkDataWatchdog();

  // 4. Relay Logic - Runs regardless of WiFi state (to handle System Failure Relay)
  updateRelayLogic();
}

// --- RELAY LOGIC ENGINE ---
void updateRelayLogic() {
  // Relay 4: System Failure Status
  // Active if network error OR data is stale
  if (is_network_error) {
    digitalWrite(RELAY_4_PIN, RELAY_ON); 
  } else {
    digitalWrite(RELAY_4_PIN, RELAY_OFF);
  }

  // Relay 1: Main Alarm Siren
  // Fail-Safe: If network error, force silence on siren (to avoid endless screaming on disconnect)
  if (is_network_error) {
    // If we are currently ON, force OFF.
    if (digitalRead(RELAY_1_PIN) == RELAY_ON) {
       digitalWrite(RELAY_1_PIN, RELAY_OFF);
       Serial.println("[LOGIC] Network Error - Forcing Siren OFF.");
    }
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
        last_alarm_trigger_time = now; // Update statistic
        Serial.println("[ALARM] Triggered! Relay ON (Initial)");
      }
      break;

    case STATE_INITIAL_ALARM:
      if (!is_alarm_active) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_IDLE;
        Serial.println("[ALARM] Threat cleared. Relay OFF.");
      } else if (elapsed >= init_alarm_duration_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_COOLDOWN;
        state_start_time = now;
        Serial.println("[ALARM] Initial phase ended. Silence.");
      }
      break;

    case STATE_COOLDOWN:
      if (!is_alarm_active) {
        current_state = STATE_IDLE;
        Serial.println("[ALARM] Threat cleared during silence.");
      } else if (elapsed >= reminder_interval_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_ON);
        current_state = STATE_REMINDER_ALARM;
        state_start_time = now;
        last_alarm_trigger_time = now;
        Serial.println("[ALARM] Reminder! Relay ON.");
      }
      break;

    case STATE_REMINDER_ALARM:
      if (!is_alarm_active) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_IDLE;
        Serial.println("[ALARM] Threat cleared.");
      } else if (elapsed >= reminder_duration_ms) {
        digitalWrite(RELAY_1_PIN, RELAY_OFF);
        current_state = STATE_COOLDOWN;
        state_start_time = now;
        Serial.println("[ALARM] Reminder ended. Silence.");
      }
      break;
  }
}

// --- DATA WATCHDOG ---
void checkDataWatchdog() {
  if (millis() - last_successful_data_time > watchdog_timeout_ms) {
    if (!is_network_error) {
      Serial.println("[WATCHDOG] Data stale! Setting System Failure.");
      is_network_error = true;
      last_connection_status = (system_lang == "pl") ? "BŁĄD DANYCH (Timeout)" : "DATA ERROR (Timeout)";
      // Fail-safe: reset alarm logic state to prevent stuck siren
      is_alarm_active = false; 
    }
  }
}

// --- ICINGA COMMUNICATION ---
void checkIcinga() {
  // Use static to keep connection/objects alive if we implement keep-alive later
  WiFiClientSecure client;
  client.setInsecure(); // Ignore SSL certificates
  client.setTimeout(5); // TCP Timeout 5s

  HTTPClient http;
  http.useHTTP10(true); // Stream mode
  http.setTimeout(5000); // HTTP Timeout 5s
  
  if (!http.begin(client, icinga_url)) {
    Serial.println("[HTTP] Connection failed");
    last_connection_status = "Connect Fail";
    return;
  }

  http.setAuthorization(icinga_user.c_str(), icinga_pass.c_str());
  http.addHeader("Accept", "application/json");

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    Stream& stream = http.getStream();

    // Filter: We only care about the "results" array.
    // This works for both Hosts and Services.
    StaticJsonDocument<200> filter;
    filter["results"][0]["name"] = true; // Try to get the name of the object
    filter["results"][0]["attrs"]["display_name"] = true; // Or display name

    // Buffer: Enough for a few objects names
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));

    if (!error) {
      last_successful_data_time = millis();
      is_network_error = false;
      last_connection_status = "OK (200)";
      
      JsonArray results = doc["results"].as<JsonArray>();
      
      // LOGIC: If results array is not empty, it means the API Filter matched something.
      // E.g. filter=service.state==2 matched a critical service.
      bool new_state = (results.size() > 0);
      
      if (new_state) {
          // Get name of the first culprit for status
          const char* name = results[0]["attrs"]["display_name"] | results[0]["name"] | "Unknown Object";
          last_icinga_object_name = String(name);
      } else {
          last_icinga_object_name = "None";
      }
      
      if (new_state != is_alarm_active) {
         Serial.printf("[ICINGA] State Change: %s\n", new_state ? "CRITICAL" : "OK");
         is_alarm_active = new_state;
      }
    } else {
      Serial.print("[JSON] Parse Error: ");
      Serial.println(error.c_str());
      last_connection_status = "JSON Error";
    }
  } else {
    Serial.printf("[HTTP] Error Code: %d\n", httpCode);
    last_connection_status = "HTTP " + String(httpCode);
  }
  http.end();
}

// --- WIFI RECONNECTION MANAGER ---
void ensureWiFiConnection() {
  if (!wifi_connected_mode) return;
  
  if (millis() - last_wifi_check_time > 5000) {
    last_wifi_check_time = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lost connection. Reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
}

// --- WEB SERVER & CONFIG ---
void handleRoot() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) {
    return server.requestAuthentication();
  }

  // --- LANGUAGE STRINGS (WEB UI ONLY) ---
  bool is_pl = (system_lang == "pl");
  String t_title = is_pl ? "Panel Alarmowy LilyGO" : "LilyGO Alarm Panel";
  String t_status_head = is_pl ? "STATUS SYSTEMU" : "SYSTEM STATUS";
  String t_net_err = is_pl ? "AWARIA SIECI / DANYCH" : "NETWORK / DATA FAILURE";
  String t_crit = is_pl ? "ALARM KRYTYCZNY!" : "CRITICAL ALARM!";
  String t_ok = is_pl ? "SYSTEM OK (Czuwanie)" : "SYSTEM OK (Standby)";
  
  String t_tab_conn = is_pl ? "Stan Połączenia:" : "Connection State:";
  String t_tab_last = is_pl ? "Ostatni Sukces:" : "Last Success:";
  String t_tab_trig = is_pl ? "Ostatni Alarm (Start):" : "Last Alarm Trigger:";
  String t_tab_obj  = is_pl ? "Obiekt Alarmu:" : "Alarm Object:";
  String t_tab_upt  = is_pl ? "Czas Pracy:" : "Uptime:";

  String t_sec_wifi = is_pl ? "Konfiguracja WiFi" : "WiFi Configuration";
  String t_sec_api  = is_pl ? "Integracja Icinga" : "Icinga Integration";
  String t_sec_log  = is_pl ? "Logika i Czasy (ms)" : "Logic & Timings (ms)";
  String t_sec_acc  = is_pl ? "Dostęp i Język" : "Access & Language";
  
  String t_btn = is_pl ? "ZAPISZ I RESTARTUJ" : "SAVE AND RESTART";

  // --- HTML GENERATION ---
  String s = "<!DOCTYPE html><html lang='" + system_lang + "'><head>";
  s += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  s += "<title>" + t_title + "</title>";
  s += "<style>";
  s += "body{font-family:Segoe UI,sans-serif;margin:0;padding:10px;background:#f4f4f9;color:#333;}";
  s += "h2{border-bottom:3px solid #007bff;padding-bottom:10px;margin-top:0;}";
  s += ".status{padding:20px;border-radius:8px;margin-bottom:20px;font-weight:bold;color:white;text-align:center;font-size:1.2em;}";
  s += ".ok{background:#28a745;} .err{background:#dc3545;} .warn{background:#ffc107;color:black;}";
  s += "table{width:100%;background:white;border-radius:5px;margin-bottom:15px;border-collapse:collapse;}";
  s += "td{padding:10px;border-bottom:1px solid #eee;} td:first-child{font-weight:bold;width:40%;}";
  s += "input,select{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px;}";
  s += "button{width:100%;padding:15px;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer;font-size:16px;font-weight:bold;}";
  s += "button:hover{background:#0056b3;} label{font-weight:600;font-size:14px;display:block;margin-top:10px;}";
  s += ".group{background:white;padding:20px;margin-bottom:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.05);}";
  s += "</style></head><body>";
  
  s += "<h2>" + t_title + "</h2>";

  // MAIN STATUS BOX
  if (is_network_error) {
    s += "<div class='status err'>" + t_net_err + "</div>";
  } else if (is_alarm_active) {
    s += "<div class='status err'>" + t_crit + "<br><small>" + last_icinga_object_name + "</small></div>";
  } else {
    s += "<div class='status ok'>" + t_ok + "</div>";
  }
  
  // INFO TABLE
  s += "<div class='group'><h3>" + t_status_head + "</h3><table>";
  s += "<tr><td>" + t_tab_conn + "</td><td>" + last_connection_status + "</td></tr>";
  s += "<tr><td>" + t_tab_last + "</td><td>" + formatTimeAgo(last_successful_data_time) + "</td></tr>";
  s += "<tr><td>" + t_tab_obj + "</td><td>" + last_icinga_object_name + "</td></tr>";
  s += "<tr><td>" + t_tab_trig + "</td><td>" + (last_alarm_trigger_time > 0 ? formatTimeAgo(last_alarm_trigger_time) : "-") + "</td></tr>";
  s += "<tr><td>" + t_tab_upt + "</td><td>" + getUptimeStr() + "</td></tr>";
  s += "</table></div>";

  s += "<form action='/save' method='POST'>";
  
  s += "<div class='group'><h3>" + t_sec_wifi + "</h3>";
  s += "<label>SSID:</label><input type='text' name='ssid' value='" + wifi_ssid + "'>";
  s += "<label>Password:</label><input type='password' name='wpass' value='" + wifi_pass + "'></div>";
  
  s += "<div class='group'><h3>" + t_sec_api + "</h3>";
  s += "<label>URL (Filter):</label><input type='text' name='iurl' value='" + icinga_url + "'>";
  s += "<small style='color:#666'>Ex: .../services?filter=service.state==2 OR .../hosts?filter=host.state==1</small>";
  s += "<label>User:</label><input type='text' name='iuser' value='" + icinga_user + "'>";
  s += "<label>Password:</label><input type='password' name='ipass' value='" + icinga_pass + "'></div>";
  
  s += "<div class='group'><h3>" + t_sec_log + "</h3>";
  s += "<label>Poll Interval:</label><input type='number' name='poll' value='" + String(poll_interval_ms) + "'>";
  s += "<label>Watchdog Timeout:</label><input type='number' name='wd' value='" + String(watchdog_timeout_ms) + "'>";
  s += "<label>Init Alarm Duration:</label><input type='number' name='init' value='" + String(init_alarm_duration_ms) + "'>";
  s += "<label>Reminder Interval:</label><input type='number' name='rint' value='" + String(reminder_interval_ms) + "'>";
  s += "<label>Reminder Duration:</label><input type='number' name='rdur' value='" + String(reminder_duration_ms) + "'></div>";
  
  s += "<div class='group'><h3>" + t_sec_acc + "</h3>";
  s += "<label>Web Login:</label><input type='text' name='wu' value='" + web_user + "'>";
  s += "<label>Web Password:</label><input type='password' name='wp' value='" + web_pass + "'>";
  s += "<label>Language / Język:</label><select name='lang'>";
  s += "<option value='pl' " + String(system_lang == "pl" ? "selected" : "") + ">Polski</option>";
  s += "<option value='en' " + String(system_lang == "en" ? "selected" : "") + ">English</option>";
  s += "</select></div>";
  
  s += "<button type='submit'>" + t_btn + "</button>";
  s += "</form></body></html>";
  
  server.send(200, "text/html", s);
}

void handleSave() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) {
    return server.requestAuthentication();
  }

  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("wpass", server.arg("wpass"));
  preferences.putString("iurl", server.arg("iurl"));
  preferences.putString("iuser", server.arg("iuser"));
  preferences.putString("ipass", server.arg("ipass"));
  preferences.putString("wu", server.arg("wu"));
  preferences.putString("wp", server.arg("wp"));
  preferences.putString("lang", server.arg("lang"));

  // Input validation (clamping)
  unsigned long new_poll = server.arg("poll").toInt();
  if (new_poll < 1000) new_poll = 1000; // Minimum 1 sec

  preferences.putULong("poll", new_poll);
  preferences.putULong("wd", server.arg("wd").toInt());
  preferences.putULong("init", server.arg("init").toInt());
  preferences.putULong("rint", server.arg("rint").toInt());
  preferences.putULong("rdur", server.arg("rdur").toInt());

  String msg = (server.arg("lang") == "pl") 
    ? "<h1>Zapisano!</h1><p>Restart urzadzenia...</p>" 
    : "<h1>Saved!</h1><p>Rebooting device...</p>";

  server.send(200, "text/html", msg);
  
  delay(1000);
  ESP.restart();
}

void loadSettings() {
  preferences.begin("trelay_cfg", false);
  
  wifi_ssid = preferences.getString("ssid", "");
  wifi_pass = preferences.getString("wpass", "");
  
  icinga_url = preferences.getString("iurl", icinga_url);
  icinga_user = preferences.getString("iuser", icinga_user);
  icinga_pass = preferences.getString("ipass", icinga_pass);
  
  web_user = preferences.getString("wu", web_user);
  web_pass = preferences.getString("wp", web_pass);
  system_lang = preferences.getString("lang", "pl");

  poll_interval_ms = preferences.getULong("poll", poll_interval_ms);
  watchdog_timeout_ms = preferences.getULong("wd", watchdog_timeout_ms);
  init_alarm_duration_ms = preferences.getULong("init", init_alarm_duration_ms);
  reminder_interval_ms = preferences.getULong("rint", reminder_interval_ms);
  reminder_duration_ms = preferences.getULong("rdur", reminder_duration_ms);
}

void setupWiFi() {
  if (wifi_ssid == "") {
    Serial.println("No WiFi Config. Starting AP Mode.");
    WiFi.softAP("T-Relay-Config", "admin123");
    wifi_connected_mode = false;
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  Serial.print("Connecting to WiFi: "); Serial.println(wifi_ssid);

  int retries = 0;
  // Wait 15 seconds max
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    Serial.print(".");
    retries++;
    esp_task_wdt_reset(); // Keep watchdog happy during connect
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    wifi_connected_mode = true;
    last_connection_status = "WiFi Connected";
  } else {
    Serial.println("WiFi Fail. Starting Emergency AP.");
    WiFi.softAP("T-Relay-Config", "admin123");
    wifi_connected_mode = false;
    last_connection_status = "WiFi Failed -> AP Mode";
  }
}

// --- HELPER FUNCTIONS ---

String formatTimeAgo(unsigned long timestamp) {
  if (timestamp == 0) return "-";
  unsigned long diff = millis() - timestamp;
  unsigned long seconds = diff / 1000;
  
  String unit = (system_lang == "pl") ? " s temu" : " s ago";
  if (seconds > 60) {
    unit = (system_lang == "pl") ? " min temu" : " min ago";
    seconds /= 60;
  }
  return String(seconds) + unit;
}

String getUptimeStr() {
  unsigned long sec = millis() / 1000;
  unsigned long min = sec / 60;
  unsigned long hr = min / 60;
  
  String s = String(hr) + "h " + String(min % 60) + "m";
  return s;
}