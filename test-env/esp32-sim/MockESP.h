#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <atomic>
#include <cstdlib>

#include <httplib.h>

// Mock Arduino Types
class ArduinoString; // Forward decl

class ArduinoString : public std::string {
public:
    ArduinoString() : std::string() {}
    ArduinoString(const char* s) : std::string(s) {}
    ArduinoString(std::string s) : std::string(s) {}
    ArduinoString(int i) : std::string(std::to_string(i)) {}
    ArduinoString(long i) : std::string(std::to_string(i)) {}
    ArduinoString(unsigned long i) : std::string(std::to_string(i)) {}
    
    // Concatenation
    ArduinoString operator+(const char* rhs) { return ArduinoString(std::string(*this) + rhs); }
    ArduinoString operator+(const ArduinoString& rhs) { return ArduinoString(std::string(*this) + std::string(rhs)); }
    
    void trim() { 
         // simplistic trim
    }
    int toInt() { return std::stoi(*this); }
    
    // Simulating .toString() which is used on IPAddress in Arduino but here WiFi.localIP() returns String
    // Wait, WiFi.localIP() in MockESP returns String directly.
    // In Arduino, IPAddress has .toString().
    // So if WiFi.localIP() returns String, then we need .toString() on String?
    // No, standard Arduino String doesn't have toString(), it IS a string.
    // But the user code does: WiFi.localIP().toString()
    // This implies WiFi.localIP() returns an IPAddress object in real Arduino.
    // In our Mock, it returns String.
    // So we need to add toString() to our Mock String class to satisfy this call.
    ArduinoString toString() const { return *this; }
    
    // c_str() inherited from std::string
};

#define String ArduinoString

typedef bool boolean;
typedef unsigned char byte;

// CONSTANTS must be declared before use if they are used as default args or in initializers
// But macros #define work anywhere if included before usage.

// However, main problem was redefinition of variables because #include "trelaylaatern.ino" is essentially copy-paste.
// And we defined wifi_ssid in MockESP.h? No.
// Wait, the error says:
// trelaylaatern.ino:88:10: error: redefinition of 'ArduinoString wifi_ssid'
// trelaylaatern.ino:26:10: note: 'ArduinoString wifi_ssid' previously declared here
// Ah! In trelaylaatern.ino we have:
// #ifdef LINUX_SIM
//   String wifi_ssid = "DOCKER_NET";
// #else
// ...
// #endif
// AND THEN LATER at line 88:
// String wifi_ssid = "";
//
// We need to fix trelaylaatern.ino to not redeclare these if LINUX_SIM is defined, OR handle it better.

// Also missing definitions:
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define WL_CONNECTED 1
#define WIFI_STA 1
#define WIFI_POWER_11dBm 11
#define HTTP_GET 0
#define HTTP_POST 1 
#define HTTP_CODE_OK 200

// ESP object
class ESPMock {
public:
    void restart() { 
        std::cout << "[ESP] RESTARTING..." << std::endl;
        exit(0); 
    }
};
static ESPMock ESP;


// Mock Time
inline unsigned long millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Mock Serial
class SerialMock {
public:
    void begin(int baud) { std::cout << "[Serial] Begin " << baud << std::endl; }
    void println(const String& s) { std::cout << "[Serial] " << s << std::endl; }
    void println(int i) { std::cout << "[Serial] " << i << std::endl; }
    void print(const String& s) { std::cout << s; }
};
static SerialMock Serial;

// Mock GPIO
static std::map<int, int> pinStates;
inline void pinMode(int pin, int mode) { }
inline void digitalWrite(int pin, int val) {
    if (pinStates[pin] != val) {
        pinStates[pin] = val;
        // Specifically log Relays
        if (pin == 21 || pin == 19 || pin == 18 || pin == 5) {
            std::cout << "\033[1;33m[GPIO] RELAY Pin " << pin << " -> " << (val ? "ON" : "OFF") << "\033[0m" << std::endl;
        } else {
             std::cout << "[GPIO] Pin " << pin << " -> " << val << std::endl;
        }
    }
}
inline int digitalRead(int pin) { return pinStates[pin]; }

// Mock WiFi
class WiFiMock {
public:
    int status() { return WL_CONNECTED; }
    void disconnect(bool w) {}
    void disconnect() { disconnect(true); }
    void mode(int m) {}
    void setSleep(bool b) {}
    void setTxPower(int p) {}
    void begin(const char* ssid, const char* pass) {
        std::cout << "[WiFi] Connecting to " << ssid << "..." << std::endl;
        delay(500);
        std::cout << "[WiFi] Connected!" << std::endl;
    }
    void reconnect() {}
    void softAP(const char* ssid, const char* pass) {}
    String localIP() { return "127.0.0.1"; }
};
static WiFiMock WiFi;

// Mock Preferences
class Preferences {
public:
    std::map<String, String> store;
    void begin(const char* name, bool ro) {}
    void putString(const char* key, String val) { store[key] = val; }
    String getString(const char* key, String def) { return store.count(key) ? store[key] : def; }
    void putULong(const char* key, unsigned long val) { store[key] = std::to_string(val); }
    unsigned long getULong(const char* key, unsigned long def) { return store.count(key) ? std::stoul(store[key]) : def; }
};

// Mock WebServer (real HTTP server via cpp-httplib)
class WebServer {
public:
    explicit WebServer(int port) : port_(port) {}

    // Register GET handler (default)
    void on(const char* uri, void (*fn)()) {
        routes_.push_back(Route{uri, HTTP_GET, fn});
    }

    // Register handler with method
    void on(const char* uri, int method, void (*fn)()) {
        routes_.push_back(Route{uri, method, fn});
    }

    void begin() {
        // Build routes into the HTTP server
        for (const auto& r : routes_) {
            if (r.method == HTTP_POST) {
                http_.Post(r.path.c_str(), [this, r](const httplib::Request& req, httplib::Response& res) {
                    dispatch(req, res, r.fn);
                });
            } else {
                http_.Get(r.path.c_str(), [this, r](const httplib::Request& req, httplib::Response& res) {
                    dispatch(req, res, r.fn);
                });
            }
        }

        // Start listener thread
        running_ = true;
        thread_ = std::thread([this]() {
            http_.listen("0.0.0.0", port_);
        });
        thread_.detach();
    }

    // Arduino WebServer expects polling; in sim we run in a background thread.
    void handleClient() {}

    bool authenticate(const char* u, const char* p) {
        if (!current_req_) return false;
        const auto auth = current_req_->get_header_value("Authorization");
        if (auth.rfind("Basic ", 0) != 0) return false;
        const auto b64 = auth.substr(6);
        const auto decoded = base64_decode(b64);
        const auto pos = decoded.find(':');
        if (pos == std::string::npos) return false;
        const auto user = decoded.substr(0, pos);
        const auto pass = decoded.substr(pos + 1);
        return (user == u && pass == p);
    }

    void requestAuthentication() {
        sendHeader("WWW-Authenticate", "Basic realm=\"esp32-sim\"");
        send(401, "text/plain", "Unauthorized");
    }

    void setContentLength(int /*l*/) {}

    void send(int code) {
        std::lock_guard<std::mutex> lock(mu_);
        resp_status_ = code;
        resp_type_ = "text/plain";
        resp_body_.clear();
        finalized_ = true;
    }

    void send(int code, const char* type, const String& content) {
        std::lock_guard<std::mutex> lock(mu_);
        resp_status_ = code;
        resp_type_ = type ? type : "text/plain";
        resp_body_ = std::string(content);
        finalized_ = false; // may be followed by sendContent()
    }

    void sendHeader(const char* k, const char* v) {
        if (!k || !v) return;
        std::lock_guard<std::mutex> lock(mu_);
        headers_[k] = v;
    }

    void sendContent(const String& content) {
        std::lock_guard<std::mutex> lock(mu_);
        resp_body_ += std::string(content);
    }

    String arg(const char* name) {
        if (!current_req_ || !name) return "";
        if (current_req_->has_param(name)) {
            return current_req_->get_param_value(name);
        }
        return "";
    }

private:
    struct Route {
        std::string path;
        int method;
        void (*fn)();
    };

    void dispatch(const httplib::Request& req, httplib::Response& res, void (*fn)()) {
        current_req_ = &req;
        {
            std::lock_guard<std::mutex> lock(mu_);
            resp_status_ = 200;
            resp_type_ = "text/plain";
            resp_body_.clear();
            headers_.clear();
            finalized_ = false;
        }

        if (fn) fn();

        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& kv : headers_) {
            res.set_header(kv.first, kv.second);
        }
        res.status = resp_status_;
        res.set_content(resp_body_, resp_type_);

        current_req_ = nullptr;
    }

    // Minimal Base64 decoder for Basic Auth
    static std::string base64_decode(const std::string& in) {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[static_cast<size_t>(chars[i])] = i;

        std::string out;
        int val = 0, valb = -8;
        for (unsigned char c : in) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    int port_;
    httplib::Server http_;
    std::vector<Route> routes_;

    std::mutex mu_;
    int resp_status_ = 200;
    std::string resp_type_ = "text/plain";
    std::string resp_body_;
    std::map<std::string, std::string> headers_;
    bool finalized_ = false;

    std::thread thread_;
    std::atomic<bool> running_{false};
    const httplib::Request* current_req_ = nullptr;
};
#define CONTENT_LENGTH_UNKNOWN 0

// Helper Stream for ArduinoJson
class Stream {
public:
    virtual int read() = 0;
    virtual ~Stream() {}
};

class StringStream : public Stream {
    std::string data;
    size_t pos = 0;
public:
    StringStream(std::string s) : data(s) {}
    int read() override {
        if (pos < data.size()) return (unsigned char)data[pos++];
        return -1;
    }
};

// Mock WiFiClientSecure
class WiFiClientSecure {
public:
    void setInsecure() {}
    void setTimeout(int s) {}
};

// Mock HTTPClient
class HTTPClient {
    String payload;
    StringStream* stream = nullptr;
public:
    void useHTTP10(bool b) {}
    void setTimeout(int ms) {}
    bool begin(WiFiClientSecure& client, String url) { 
        this->url = url; 
        return true; 
    }
    void setAuthorization(const char* u, const char* p) { user = u; pass = p; }
    void addHeader(String k, String v) {}
    int GET() {
        std::cout << "[HTTP] GET " << url << std::endl;
        
        CURL *curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            
            if (!user.empty()) {
                curl_easy_setopt(curl, CURLOPT_USERNAME, user.c_str());
                curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
            }

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            
            if(res != CURLE_OK) {
                std::cout << "[HTTP] Error: " << curl_easy_strerror(res) << std::endl;
                return 500;
            }
            
            payload = readBuffer;
            return 200; 
        }
        return 500;
    }
    
    Stream& getStream() {
        if (stream) delete stream;
        stream = new StringStream(payload);
        return *stream;
    }
    
    void end() {
        if (stream) { delete stream; stream = nullptr; }
    }
    
private:
    String url;
    String user, pass;
    
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};

// Mock specific ESP32 macros/functions
#define WRITE_PERI_REG(reg, val)
#define RTC_CNTL_BROWN_OUT_REG 0

// Include ArduinoJson (Header only)
// Note: In real world we would need to download it or expect it in include path.
// For the sake of this demo, we will try to include it.
// Since we are inside a docker container that runs `apk add nlohmann-json`? 
// No, ArduinoJson is not nlohmann-json.
// We must download ArduinoJson.h into the container.
// I'll add a curl command to the Dockerfile to fetch single header ArduinoJson.

#include <ArduinoJson.h>

