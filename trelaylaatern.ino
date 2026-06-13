/*
 * ======================================================================================
 * Project: icinga-lighthouse (IMPROVED)
 * Description: LilyGO T-Relay 4-Channel Monitor for Icinga2 API
 * Updates: Security Fixes (TLS/Auth), Memory Optimization (Chunked HTML), Reliability
 * ======================================================================================
 *
 * --- SIMULATION MODE (Uncomment to use with Wokwi Simulator) ---
 * // #define SIMULATION_MODE
 * 
 * Instructions:
 * 1. Install "Wokwi for VS Code" extension.
 * 2. Uncomment the line above (#define SIMULATION_MODE).
 * 3. Ensure "Wokwi IoT Gateway" is running on your PC.
 * 4. Press F1 -> "Wokwi: Start Simulation".
 * 
 * Note: In simulation, use your PC's IP address for Icinga, not localhost!
 */

// --- SIMULATION MODE ---
// To run in Podman/Docker (Linux Simulation):
// This block allows compiling the .ino file as a C++ Linux app.
#ifdef LINUX_SIM
  #include "MockESP.h"
  // REMOVED DEFINITIONS FROM HERE TO AVOID REDEFINITION ERROR
#else
  #include <WiFi.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <ArduinoJson.h>
  #include <Preferences.h>
  #include <Ethernet.h>            // WIZnet W5500 (T-Relay W5500 shield H671)
  #include <SPI.h>
  #include "soc/soc.h"
  #include "soc/rtc_cntl_reg.h"
  
  // Configuration for Real ESP32
  // ... (Wokwi or Physical)
#endif    

// --- PIN DEFINITIONS ---
#define RELAY_1_PIN 21
#define RELAY_2_PIN 19
#define RELAY_3_PIN 18
#define RELAY_4_PIN 5
#define STATUS_LED_PIN 25

// --- W5500 Ethernet (LilyGo T-Relay-4 W5500 shield H671) ---
// Pinout per LilyGo pin_config.h / ESPHome yaml. None clash with relays/LED.
#define ETH_W5500_SCLK 22
#define ETH_W5500_MISO 13
#define ETH_W5500_MOSI 26
#define ETH_W5500_CS   27
#define ETH_W5500_RST  23
#define ETH_W5500_INT  36

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
  String lbl_rchk;
  String lbl_thr;
  String lbl_init;
  String lbl_rint;
  String lbl_rdur;
  String lbl_eth;
  String eth_auto;
  String eth_off;
  String btn_save;
  String msg_saved;
};
LangText txt; // Global object holding current texts

// --- CONFIGURATION ---
String wifi_ssid = "";
String wifi_pass = "";

// Data source: Icinga DB Web JSON API (NOT the Icinga2 core API).
// We ask Icinga *Web* for unhandled problems, so acknowledged / in-downtime /
// flapping objects are filtered out server-side (is_acknowledged=n, etc.).
// http:// uses a plain socket; https:// uses TLS (insecure / no cert check).
// limit=1 keeps the JSON tiny — we only need "is there any problem?".
String icinga_url_svc = "http://192.168.1.100:8080/icingadb/services?service.state.soft_state=2&service.state.is_acknowledged=n&service.state.in_downtime=n&service.state.is_flapping=n&limit=1";
String icinga_url_host = "http://192.168.1.100:8080/icingadb/hosts?host.state.soft_state=1&host.state.is_acknowledged=n&host.state.in_downtime=n&host.state.is_flapping=n&limit=1";

String icinga_user = "admin";   // Icinga Web login (not the icinga2 API user)
String icinga_pass = "admin";
String web_user = "admin";
String web_pass = "admin";
String system_lang = "en";
String tls_fingerprint = ""; // Stores SHA1 fingerprint (reserved; TLS is insecure for now)

// Ethernet (W5500): auto-used when the shield is detected, unless disabled here.
bool eth_disabled = false;   // panel toggle: force WiFi even if a W5500 is present

// Timings
unsigned long poll_interval_ms = 30000;       // normal poll cadence
unsigned long recheck_interval_ms = 10000;    // fast re-poll while confirming a fresh problem
unsigned long init_alarm_duration_ms = 30000;
unsigned long reminder_interval_ms = 300000;
unsigned long reminder_duration_ms = 15000;
unsigned long watchdog_timeout_ms = 60000;

// Confirmation threshold: a problem must be seen this many consecutive polls
// before the siren fires. Debounces transient/false positives. Configurable.
int confirm_threshold = 3;

// Business hours: when enabled, the siren is muted outside the window. Alerts
// are still detected and shown in the UI — only the relay is suppressed. The
// current time comes from the HTTP "Date" header of Icinga's replies (works
// over both WiFi and Ethernet, no NTP needed).
bool bh_enabled       = false;  // restrict siren to business hours
int  bh_start         = 6;      // local hour, inclusive (0-23)
int  bh_end           = 18;     // local hour, exclusive (0-23)
bool bh_weekdays_only = true;   // Mon-Fri only (skip Sat/Sun)
int  tz_offset        = 0;      // hours added to UTC for local time (e.g. +2)

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
int alarm_confirm_count = 0;       // consecutive polls that saw a problem
String last_next_check = "";       // next_check hint from Icinga (for the UI)
bool eth_present = false;          // W5500 chip detected on SPI at boot
bool eth_active = false;           // Ethernet has an IP (updated from net events)

// Current time, parsed from the HTTP "Date" header (UTC).
bool time_valid = false;
int cur_utc_hour = 0;
int cur_utc_min = 0;
int cur_wday = 0;                  // 0=Sun .. 6=Sat (UTC)

enum AlarmState { STATE_IDLE, STATE_INITIAL_ALARM, STATE_COOLDOWN, STATE_REMINDER_ALARM };
AlarmState current_state = STATE_IDLE;
unsigned long state_start_time = 0;

// Declarations
void loadSettings();
void setLanguage(); 
void setupWiFi();
void setupNetwork();
bool networkUp();
String localIPStr();
void handleRoot();
void handleSave();
void handleToggle();
void checkIcinga();
bool queryIcingaEndpoint(String url, String typeName);
void captureHttpDate(String d);
bool alertsAllowedNow();
String localTimeStr();
void updateRelayLogic();
void ensureWiFiConnection();
void updateStatusLED();
String getUptimeStr();

void setup() {
  // --- SIMULATION MODE ---
  #ifdef LINUX_SIM
    // No Brownout Detector in Sim
    Serial.println("--- VIRTUAL ESP32 RUNNING IN DOCKER ---");
  #else
    // DISABLE BROWNOUT DETECTOR
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  #endif 

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- icinga-lighthouse v5.1 (Icinga DB Web + Ethernet + Business Hours) Booting... ---");

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

  #ifdef LINUX_SIM
    // Override settings for the Docker test-env AFTER loadSettings()
    // (Preferences are not persisted in the Linux mock). Point at icingadb-web.
    wifi_ssid = "DOCKER_NET";
    wifi_pass = "";
    icinga_user = "admin";
    icinga_pass = "admin";
    icinga_url_svc = "http://icingaweb2:8080/icingadb/services?service.state.soft_state=2&service.state.is_acknowledged=n&service.state.in_downtime=n&service.state.is_flapping=n&limit=1";
    icinga_url_host = "http://icingaweb2:8080/icingadb/hosts?host.state.soft_state=1&host.state.is_acknowledged=n&host.state.in_downtime=n&host.state.is_flapping=n&limit=1";
    // Fast cadence so the demo reacts quickly.
    poll_interval_ms = 6000;
    recheck_interval_ms = 2000;
  #endif

  setupNetwork();

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

#ifndef LINUX_SIM
  // Keep the Ethernet lease alive and track cable plug/unplug at runtime.
  if (eth_present) {
    static unsigned long last_eth_check = 0;
    if (current_millis - last_eth_check > 3000) {
      last_eth_check = current_millis;
      Ethernet.maintain();
      eth_active = (Ethernet.linkStatus() == LinkON) &&
                   (Ethernet.localIP() != IPAddress(0, 0, 0, 0));
    }
  }
#endif

  if (manual_override_active) {
    if (current_millis - last_manual_action_time > 60000) {
      manual_override_active = false;
    }
  }

  bool ap_mode = (!eth_active && !wifi_connected_mode);
  if (!ap_mode) {
    if (wifi_connected_mode && !eth_active) ensureWiFiConnection();
    if (networkUp()) {
       // Adaptive cadence: while a fresh problem is still being confirmed
       // (count between 1 and threshold-1) poll fast, so we don't wait a full
       // poll_interval for the next confirmation. Otherwise use the normal rate.
       bool confirming = (alarm_confirm_count > 0 && alarm_confirm_count < confirm_threshold);
       unsigned long effective_interval = confirming ? recheck_interval_ms : poll_interval_ms;
       if (current_millis - last_poll_time >= effective_interval) {
         last_poll_time = current_millis;
         checkIcinga();
       }
    } else {
       last_connection_status = eth_present ? "ETH no link" : "No WiFi";
       icinga_reachable = false;
    }
  } else {
    is_network_error = true;
    last_connection_status = "AP Mode";
    icinga_reachable = false;
  }

  // Soft Watchdog
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
    txt.sec_api = "Konfiguracja Icinga DB Web";
    txt.sec_time = "Czasy i Logika";
    txt.lbl_poll = "Interwał sprawdzania (s)";
    txt.lbl_rchk = "Szybki recheck przy alercie (s)";
    txt.lbl_thr = "Próg potwierdzeń (ile z rzędu)";
    txt.lbl_init = "Czas trwania alarmu (s)";
    txt.lbl_rint = "Przypomnienie co (min)";
    txt.lbl_rdur = "Czas przypomnienia (s)";
    txt.lbl_eth = "Tryb Ethernet";
    txt.eth_auto = "Auto (użyj jeśli wykryty)";
    txt.eth_off = "Wyłączony (wymuś WiFi)";
    txt.btn_save = "ZAPISZ I RESTARTUJ";
    txt.msg_saved = "Zapisano! Restart urzadzenia...";
  } else {
    txt.title = "icinga-lighthouse";
    txt.st_ok = "SYSTEM OK";
    txt.st_err = "CRITICAL ALARM";
    txt.st_warn = "CONNECTION / API ERROR";
    txt.st_man = "MANUAL MODE (TEST)";
    txt.t_test = "Relay Test";
    txt.btn_siren = "Toggle Siren";
    txt.btn_end = "END TEST";
    txt.sec_net = "WiFi Network";
    txt.sec_api = "Icinga DB Web Configuration";
    txt.sec_time = "Timing & Logic";
    txt.lbl_poll = "Poll Interval (s)";
    txt.lbl_rchk = "Fast recheck on problem (s)";
    txt.lbl_thr = "Confirm threshold (consecutive)";
    txt.lbl_init = "Alarm Duration (s)";
    txt.lbl_rint = "Reminder Interval (min)";
    txt.lbl_rdur = "Reminder Duration (s)";
    txt.lbl_eth = "Ethernet mode";
    txt.eth_auto = "Auto (use if detected)";
    txt.eth_off = "Disabled (force WiFi)";
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
  bool service_alarm = queryIcingaEndpoint(icinga_url_svc, "Service");

  bool host_alarm = false;
  if (!service_alarm && icinga_url_host.length() > 5) {
      host_alarm = queryIcingaEndpoint(icinga_url_host, "Host");
  }

  bool problem = service_alarm || host_alarm;

  if (problem) {
    if (alarm_confirm_count < confirm_threshold) alarm_confirm_count++;
  } else {
    alarm_confirm_count = 0;
    last_icinga_object_name = "None";
    last_next_check = "";
  }

  // The siren only arms once the problem has been confirmed N polls in a row.
  is_alarm_active = (alarm_confirm_count >= confirm_threshold);

  Serial.println("[checkIcinga] problem=" + String(problem ? 1 : 0) +
                 " confirm=" + String(alarm_confirm_count) + "/" + String(confirm_threshold) +
                 " alarm=" + String(is_alarm_active ? 1 : 0));
}

// --- Time & business hours (time is taken from the HTTP "Date" header) -----

// Parse an IMF-fixdate value, e.g. "Sat, 13 Jun 2026 07:30:00 GMT".
void captureHttpDate(String d) {
  d.trim();
  if (d.startsWith("Date:")) { d = d.substring(5); d.trim(); }
  if (d.length() < 25) return;
  const char* days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  String wd = d.substring(0, 3);
  int w = -1; for (int i = 0; i < 7; i++) if (wd == days[i]) w = i;
  if (w < 0) return;
  int hh = d.substring(17, 19).toInt();
  int mm = d.substring(20, 22).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return;
  cur_wday = w; cur_utc_hour = hh; cur_utc_min = mm; time_valid = true;
}

// True if the siren may sound now. Fails open until the time is known, so a
// fresh boot never silently swallows alerts.
bool alertsAllowedNow() {
  if (!bh_enabled) return true;
  if (!time_valid) return true;
  int h = cur_utc_hour + tz_offset;
  int w = cur_wday;
  while (h >= 24) { h -= 24; w = (w + 1) % 7; }
  while (h < 0)   { h += 24; w = (w + 6) % 7; }
  if (bh_weekdays_only && (w == 0 || w == 6)) return false;   // Sun/Sat
  if (bh_start <= bh_end) return (h >= bh_start && h < bh_end);
  return (h >= bh_start || h < bh_end);                       // spans midnight
}

// Local time as "Www HH:MM" for the UI ("--" if unknown).
String localTimeStr() {
  if (!time_valid) return "--";
  int h = cur_utc_hour + tz_offset;
  int w = cur_wday;
  while (h >= 24) { h -= 24; w = (w + 1) % 7; }
  while (h < 0)   { h += 24; w = (w + 6) % 7; }
  const char* days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  char buf[12];
  snprintf(buf, sizeof(buf), "%s %02d:%02d", days[w], h, cur_utc_min);
  return String(buf);
}

// --- Icinga DB Web query (shared parser + WiFi/HTTPClient & Ethernet paths) ---
//
// The URL already carries the state + "not handled" filters, so any element of
// the returned array is a real, unmuted problem. Response is a TOP-LEVEL ARRAY:
//   [ { "name":.., "display_name":.., "host":{"display_name":..},
//       "state":{"soft_state":2,"next_check":"2026-..+00:00",..} }, .. ]

// Parses the problem array from a body stream. Fills last_icinga_object_name /
// last_next_check; returns true if at least one problem is present. Keeps only
// the few fields we need (objects are huge), so memory stays small.
bool applyProblemJson(Stream& stream, String typeName) {
  StaticJsonDocument<256> filter;
  filter[0]["name"] = true;
  filter[0]["display_name"] = true;
  filter[0]["host"]["display_name"] = true;
  filter[0]["state"]["next_check"] = true;

  DynamicJsonDocument doc(3072);
  DeserializationError error =
      deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  if (error) { last_connection_status = "JSON err (" + typeName + ")"; return false; }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;

  JsonObject o = arr[0];
  const char* dn = o["display_name"] | o["name"] | "Unknown";
  const char* hn = o["host"]["display_name"] | "";
  String label = String(dn);
  if (typeName == "Service" && hn[0] != '\0') label = String(hn) + "!" + String(dn);
  last_icinga_object_name = typeName + ": " + label;
  last_next_check = String((const char*)(o["state"]["next_check"] | ""));
  return true;
}

#ifndef LINUX_SIM
// HTTP GET over the W5500 (the WIZnet EthernetClient has its own TCP stack and
// can't go through HTTPClient). http:// only — icingadb-web is plain HTTP.
bool queryViaEthernet(String url, String typeName) {
  if (!url.startsWith("http://")) { last_connection_status = "ETH needs http"; return false; }
  String rest = url.substring(7);
  int slash = rest.indexOf('/');
  String hostport = (slash < 0) ? rest : rest.substring(0, slash);
  String path     = (slash < 0) ? "/"  : rest.substring(slash);
  int colon = hostport.indexOf(':');
  String host = (colon < 0) ? hostport : hostport.substring(0, colon);
  int port    = (colon < 0) ? 80       : hostport.substring(colon + 1).toInt();

  EthernetClient client;
  client.setTimeout(4000);
  if (!client.connect(host.c_str(), port)) {
    last_connection_status = "ETH conn fail"; icinga_reachable = false; return false;
  }

  client.print(String("GET ") + path + " HTTP/1.0\r\n");
  client.print("Host: " + host + "\r\n");
  client.print("Authorization: Basic " + base64Encode(icinga_user + ":" + icinga_pass) + "\r\n");
  client.print("Accept: application/json\r\n");
  client.print("Connection: close\r\n\r\n");

  String status = client.readStringUntil('\n');     // "HTTP/1.0 200 OK"
  int code = 0; { int sp = status.indexOf(' '); if (sp > 0) code = status.substring(sp + 1).toInt(); }

  while (client.connected()) {                        // skip headers to blank line
    String line = client.readStringUntil('\n');
    if (line.length() == 0 || line == "\r") break;
    if (line.startsWith("Date:")) captureHttpDate(line);
  }

  if (code != 200) {
    last_connection_status = "ETH HTTP " + String(code);
    icinga_reachable = false; client.stop(); return false;
  }
  icinga_reachable = true; last_successful_data_time = millis();
  is_network_error = false; last_connection_status = "OK (200/eth)";
  bool res = applyProblemJson(client, typeName);
  client.stop();
  return res;
}
#endif

bool queryIcingaEndpoint(String url, String typeName) {
  if (url == "") return false;

#ifndef LINUX_SIM
  if (eth_active) return queryViaEthernet(url, typeName);
#endif

  // WiFi / sim path via HTTPClient. WiFiClientSecure derives from WiFiClient,
  // so a base pointer drives either transport (TLS picked via the vtable).
  bool https = url.startsWith("https");
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  WiFiClient* client;
  if (https) { secureClient.setInsecure(); secureClient.setTimeout(5); client = &secureClient; }
  else       { client = &plainClient; }

  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(4000);

  if (!http.begin(*client, url)) {
    last_connection_status = "Conn Fail (" + typeName + ")";
    icinga_reachable = false;
    return false;
  }
  http.setAuthorization(icinga_user.c_str(), icinga_pass.c_str());
  http.addHeader("Accept", "application/json");
  const char* dateHdr[] = { "Date" };
  http.collectHeaders(dateHdr, 1);

  int httpCode = http.GET();
  bool result = false;
  if (httpCode == HTTP_CODE_OK) {
    icinga_reachable = true;
    last_successful_data_time = millis();
    is_network_error = false;
    last_connection_status = "OK (200)";
    captureHttpDate(http.header("Date"));   // keep device clock fresh
    Stream& stream = http.getStream();
    result = applyProblemJson(stream, typeName);
  } else {
    last_connection_status = "HTTP " + String(httpCode);
    icinga_reachable = false;
  }
  http.end();
  return result;
}

void setupWiFi() {
  if (wifi_ssid == "") {
    WiFi.softAP("icinga-lighthouse-cfg", "admin123");
    wifi_connected_mode = false;
    return;
  }
  
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

// --- NETWORK: prefer W5500 Ethernet when present, else fall back to WiFi ---

#ifndef LINUX_SIM
// Minimal base64 (for the HTTP Basic auth header on the Ethernet path).
String base64Encode(String in) {
  static const char* t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out; int val = 0, bits = -6;
  for (int i = 0; i < (int)in.length(); i++) {
    val = (val << 8) + (unsigned char)in[i]; bits += 8;
    while (bits >= 0) { out += t[(val >> bits) & 0x3F]; bits -= 8; }
  }
  if (bits > -6) out += t[((val << 8) >> (bits + 8)) & 0x3F];
  while (out.length() % 4) out += '=';
  return out;
}

// Brings up the W5500 shield over SPI using the WIZnet Ethernet library
// (Arduino core 2.x has no SPI PHY support in its built-in ETH). Sets
// eth_present (chip detected) and eth_active (got a DHCP lease + link).
bool setupEthernet() {
  if (eth_disabled) { last_connection_status = "ETH disabled"; return false; }
  SPI.begin(ETH_W5500_SCLK, ETH_W5500_MISO, ETH_W5500_MOSI, ETH_W5500_CS);
  Ethernet.init(ETH_W5500_CS);

  // Locally-administered MAC derived from the ESP32 efuse.
  uint64_t id = ESP.getEfuseMac();
  uint8_t mac[6] = { 0x02, (uint8_t)(id >> 32), (uint8_t)(id >> 24),
                     (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)id };

  int ok = Ethernet.begin(mac, 8000, 4000);   // DHCP, 8s timeout
  eth_present = (Ethernet.hardwareStatus() != EthernetNoHardware);
  if (!eth_present) { last_connection_status = "No W5500"; return false; }

  eth_active = (ok == 1) && (Ethernet.linkStatus() != LinkOFF);
  last_connection_status = eth_active ? "ETH up"
                         : (Ethernet.linkStatus() == LinkOFF ? "ETH no link" : "ETH no DHCP");
  return eth_active;
}
#endif

void setupNetwork() {
#ifndef LINUX_SIM
  setupEthernet();                 // sets eth_present / eth_active
#endif
  if (!eth_active) setupWiFi();    // Ethernet not ready -> WiFi (or AP config mode)
}

bool networkUp() {
#ifdef LINUX_SIM
  return WiFi.status() == WL_CONNECTED;
#else
  return eth_active || (WiFi.status() == WL_CONNECTED);
#endif
}

String localIPStr() {
#ifndef LINUX_SIM
  if (eth_active) return Ethernet.localIP().toString();
#endif
  return WiFi.localIP().toString();
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

  if (!alertsAllowedNow()) {              // outside business hours: keep siren muted
    if (digitalRead(RELAY_1_PIN) == RELAY_ON) digitalWrite(RELAY_1_PIN, RELAY_OFF);
    current_state = STATE_IDLE;
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

// IMPROVED: Saves Web Credentials and TLS Fingerprint
void handleSave() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) return server.requestAuthentication();

  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("wpass");
  new_ssid.trim(); new_pass.trim();

  preferences.putString("ssid", new_ssid);
  // Passwords: only overwrite when a new value is given, so a blank field
  // (we never pre-fill passwords into the HTML) keeps the stored one.
  if (new_pass.length() > 0) preferences.putString("wpass", new_pass);
  preferences.putString("iurl_s", server.arg("iurl_s"));
  preferences.putString("iurl_h", server.arg("iurl_h"));
  preferences.putString("iuser", server.arg("iuser"));
  String n_ipass = server.arg("ipass"); n_ipass.trim();
  if (n_ipass.length() > 0) preferences.putString("ipass", n_ipass);
  preferences.putString("lang", server.arg("lang"));
  
  // FIX: Save Web User/Pass if changed
  String n_wu = server.arg("wu"); n_wu.trim();
  String n_wp = server.arg("wp"); n_wp.trim();
  if (n_wu.length() > 0 && n_wp.length() > 0) {
     preferences.putString("wu", n_wu);
     preferences.putString("wp", n_wp);
  }

  // NEW: Save Fingerprint
  String n_fing = server.arg("fing");
  n_fing.trim();
  preferences.putString("fing", n_fing);
  
  unsigned long p_sec = server.arg("poll").toInt();
  if(p_sec < 1) p_sec = 1;
  preferences.putULong("poll", p_sec * 1000);
  unsigned long rchk_sec = server.arg("rchk").toInt(); if (rchk_sec < 1) rchk_sec = 1;
  preferences.putULong("rchk", rchk_sec * 1000);
  int thr = server.arg("thr").toInt(); if (thr < 1) thr = 1;
  preferences.putInt("thr", thr);
  eth_disabled = (server.arg("ethdis").toInt() == 1);
  preferences.putInt("ethdis", eth_disabled ? 1 : 0);
  bh_enabled = (server.arg("bh_en").toInt() == 1);
  preferences.putInt("bh_en", bh_enabled ? 1 : 0);
  bh_start = constrain((int)server.arg("bh_s").toInt(), 0, 23);
  preferences.putInt("bh_s", bh_start);
  bh_end = constrain((int)server.arg("bh_e").toInt(), 0, 23);
  preferences.putInt("bh_e", bh_end);
  bh_weekdays_only = (server.arg("bh_wd").toInt() == 1);
  preferences.putInt("bh_wd", bh_weekdays_only ? 1 : 0);
  tz_offset = constrain((int)server.arg("tz").toInt(), -12, 14);
  preferences.putInt("tz", tz_offset);
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

// IMPROVED: Uses Chunked Transfer to avoid Heap Fragmentation
void handleRoot() {
  if (!server.authenticate(web_user.c_str(), web_pass.c_str())) return server.requestAuthentication();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  #define SEND_HTML(x) server.sendContent(x)

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
  SEND_HTML(s);

  SEND_HTML("<h2>" + txt.title + "</h2>");
  SEND_HTML("<a href='https://github.com/dzaczek/icinga-lighthouse' class='head-link' target='_blank'>GitHub: dzaczek/icinga-lighthouse</a><br><br>");

  if (manual_override_active) SEND_HTML("<div class='status warn'>" + txt.st_man + "</div>");
  else if (!icinga_reachable) SEND_HTML("<div class='status warn'>" + txt.st_warn + "</div>");
  else if (is_alarm_active && !alertsAllowedNow()) SEND_HTML("<div class='status warn'>" + txt.st_err + " (quiet hours, siren muted): " + last_icinga_object_name + "</div>");
  else if (is_alarm_active) SEND_HTML("<div class='status err'>" + txt.st_err + ": " + last_icinga_object_name + "</div>");
  else if (alarm_confirm_count > 0) SEND_HTML("<div class='status warn'>CONFIRMING " + String(alarm_confirm_count) + "/" + String(confirm_threshold) + ": " + last_icinga_object_name + "</div>");
  else SEND_HTML("<div class='status ok'>" + txt.st_ok + "</div>");

  String link_kind;
#ifdef LINUX_SIM
  link_kind = "WiFi (sim)";
#else
  link_kind = eth_active ? "Ethernet (W5500)" : (wifi_connected_mode ? "WiFi" : "AP config");
#endif
  SEND_HTML("<p>Link: " + link_kind + " &middot; IP: " + localIPStr() + "</p>");
  SEND_HTML("<p>Info: " + last_connection_status + "</p>");
  if (last_next_check.length() > 0) SEND_HTML("<p>Icinga next check: " + last_next_check + "</p>");
  String bh_info = bh_enabled
    ? (" &middot; siren " + String(bh_start) + "-" + String(bh_end) + "h" + (bh_weekdays_only ? " Mon-Fri" : " daily"))
    : " &middot; siren 24/7";
  SEND_HTML("<p>Device time: " + localTimeStr() + bh_info + "</p>");

  SEND_HTML("<div class='group'><h3>" + txt.t_test + "</h3>");
  SEND_HTML("<button onclick=\"location.href='/toggle?r=1'\">" + txt.btn_siren + "</button>");
  if (manual_override_active) SEND_HTML("<button style='background:#dc3545' onclick=\"location.href='/toggle?r=0'\">" + txt.btn_end + "</button>");
  SEND_HTML("</div>");

  SEND_HTML("<form action='/save' method='POST'>");
  
  s = "<div class='group'><h3>" + txt.sec_net + "</h3>";
  s += "<label>SSID:</label><input type='text' name='ssid' value='" + wifi_ssid + "'>";
  s += "<label>Pass:</label><input type='password' name='wpass' placeholder='(leave blank = unchanged)'>";
  s += "</div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>Ethernet (W5500)</h3>";
#ifdef LINUX_SIM
  s += "<small style='color:gray'>n/a in simulator</small>";
#else
  s += "<small style='color:gray'>Shield: " + String(eth_present ? "detected" : "not detected") +
       " &middot; Link: " + String(eth_active ? "up" : "down") + "</small>";
#endif
  s += "<label>" + txt.lbl_eth + ":</label><select name='ethdis'>";
  s += "<option value='0' " + String(!eth_disabled ? "selected" : "") + ">" + txt.eth_auto + "</option>";
  s += "<option value='1' " + String(eth_disabled ? "selected" : "") + ">" + txt.eth_off + "</option>";
  s += "</select></div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>Security / Admin</h3>";
  s += "<label>Web User:</label><input type='text' name='wu' value='" + web_user + "'>";
  s += "<label>Web Pass:</label><input type='password' name='wp' placeholder='(leave blank = unchanged)'>";
  s += "<label>TLS Fingerprint (SHA1):</label><input type='text' name='fing' placeholder='AA:BB:CC...' value='" + tls_fingerprint + "'>";
  s += "<small style='color:gray'>Leave empty for Insecure Mode (no validation)</small>";
  s += "</div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>" + txt.sec_api + "</h3>";
  s += "<small style='color:gray'>Icinga DB Web JSON API. Filters (is_acknowledged=n &amp; in_downtime=n &amp; is_flapping=n) keep muted problems out.</small>";
  s += "<label>URL Services (Critical):</label><input type='text' name='iurl_s' value='" + icinga_url_svc + "'>";
  s += "<label>URL Hosts (Down):</label><input type='text' name='iurl_h' value='" + icinga_url_host + "'>";
  s += "<label>Web User:</label><input type='text' name='iuser' value='" + icinga_user + "'>";
  s += "<label>Web Pass:</label><input type='password' name='ipass' placeholder='(leave blank = unchanged)'>";
  s += "</div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>" + txt.sec_time + "</h3>";
  s += "<label>" + txt.lbl_poll + ":</label><input type='number' name='poll' value='" + String(poll_interval_ms / 1000) + "'>";
  s += "<label>" + txt.lbl_rchk + ":</label><input type='number' name='rchk' value='" + String(recheck_interval_ms / 1000) + "'>";
  s += "<label>" + txt.lbl_thr + ":</label><input type='number' name='thr' min='1' value='" + String(confirm_threshold) + "'>";
  s += "<label>" + txt.lbl_init + ":</label><input type='number' name='init' value='" + String(init_alarm_duration_ms / 1000) + "'>";
  s += "<label>" + txt.lbl_rint + ":</label><input type='number' name='rint' value='" + String(reminder_interval_ms / 60000) + "'>";
  s += "<label>" + txt.lbl_rdur + ":</label><input type='number' name='rdur' value='" + String(reminder_duration_ms / 1000) + "'>";
  s += "</div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>Business Hours (siren mute window)</h3>";
  s += "<small style='color:gray'>Alerts are always detected and shown; outside the window the siren stays silent. Time comes from Icinga's HTTP Date header.</small>";
  s += "<label>Restrict siren to business hours:</label><select name='bh_en'>";
  s += "<option value='0' " + String(!bh_enabled ? "selected" : "") + ">Off (24/7)</option>";
  s += "<option value='1' " + String(bh_enabled ? "selected" : "") + ">On</option>";
  s += "</select>";
  s += "<label>Start hour (0-23, inclusive):</label><input type='number' name='bh_s' min='0' max='23' value='" + String(bh_start) + "'>";
  s += "<label>End hour (0-23, exclusive):</label><input type='number' name='bh_e' min='0' max='23' value='" + String(bh_end) + "'>";
  s += "<label>Days:</label><select name='bh_wd'>";
  s += "<option value='1' " + String(bh_weekdays_only ? "selected" : "") + ">Mon-Fri only</option>";
  s += "<option value='0' " + String(!bh_weekdays_only ? "selected" : "") + ">Every day</option>";
  s += "</select>";
  s += "<label>UTC offset for local time (hours):</label><input type='number' name='tz' min='-12' max='14' value='" + String(tz_offset) + "'>";
  s += "<small style='color:gray'>Device time now: " + localTimeStr() + "</small>";
  s += "</div>";
  SEND_HTML(s);

  s = "<div class='group'><h3>Language / Język</h3>";
  s += "<select name='lang'>";
  s += "<option value='en' " + String(system_lang == "en" ? "selected" : "") + ">English</option>";
  s += "<option value='pl' " + String(system_lang == "pl" ? "selected" : "") + ">Polski</option>";
  s += "</select></div>";
  SEND_HTML(s);

  SEND_HTML("<button type='submit'>" + txt.btn_save + "</button></form></body></html>");
  
  server.sendContent(""); 
}

// IMPROVED: Loads saved credentials and fingerprint
void loadSettings() {
  preferences.begin("trelay_cfg", false);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_pass = preferences.getString("wpass", "");
  icinga_url_svc = preferences.getString("iurl_s", icinga_url_svc);
  icinga_url_host = preferences.getString("iurl_h", icinga_url_host);
  icinga_user = preferences.getString("iuser", icinga_user);
  icinga_pass = preferences.getString("ipass", icinga_pass);
  
  // FIX: Actually load these
  web_user = preferences.getString("wu", web_user);
  web_pass = preferences.getString("wp", web_pass);
  
  // NEW: Load fingerprint
  tls_fingerprint = preferences.getString("fing", "");

  system_lang = preferences.getString("lang", "en"); 
  
  poll_interval_ms = preferences.getULong("poll", poll_interval_ms);
  recheck_interval_ms = preferences.getULong("rchk", recheck_interval_ms);
  confirm_threshold = preferences.getInt("thr", confirm_threshold);
  eth_disabled = (preferences.getInt("ethdis", eth_disabled ? 1 : 0) == 1);
  bh_enabled = (preferences.getInt("bh_en", bh_enabled ? 1 : 0) == 1);
  bh_start = preferences.getInt("bh_s", bh_start);
  bh_end = preferences.getInt("bh_e", bh_end);
  bh_weekdays_only = (preferences.getInt("bh_wd", bh_weekdays_only ? 1 : 0) == 1);
  tz_offset = preferences.getInt("tz", tz_offset);
  init_alarm_duration_ms = preferences.getULong("init", init_alarm_duration_ms);
  reminder_interval_ms = preferences.getULong("rint", reminder_interval_ms);
  reminder_duration_ms = preferences.getULong("rdur", reminder_duration_ms);

  setLanguage();
}

String getUptimeStr() {
  return String((unsigned long)(millis() / 1000 / 60)) + " min";
}