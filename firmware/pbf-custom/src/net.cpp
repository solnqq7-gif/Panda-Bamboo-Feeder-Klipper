#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "feeder.h"
#include "io.h"
#include "logbuf.h"
#include "motor.h"
#include "settings.h"
#include "stats.h"
#include "web_page.h"

namespace net {
namespace {

constexpr const char *kHostname = "pbf";
constexpr const char *kApPassword = "pbf12345";
constexpr uint32_t kStaTimeoutMs = 20000;       // then open the access point as a fallback
constexpr uint32_t kApOffAfterConnectMs = 60000;

WebServer server(80);
bool services_started = false;
bool ap_active = false;
bool was_connected = false;
uint32_t sta_start_ms = 0;
uint32_t ap_off_at_ms = 0;
String ap_ssid;

bool staConnected() { return WiFi.status() == WL_CONNECTED; }

// The stock firmware keeps the network in NVS namespace "app_nvs", blob "wifi_info":
// SSID at offset 0 (32 chars + NUL), password at offset 33 (64 chars).
bool importStockWifi() {
  Preferences p;
  if (!p.begin("app_nvs", true)) return false;
  size_t n = p.getBytesLength("wifi_info");
  uint8_t buf[300] = {0};
  if (n >= 97) p.getBytes("wifi_info", buf, min(n, sizeof(buf)));
  p.end();
  if (n < 97) return false;
  char ssid[33] = {0};
  char pass[65] = {0};
  memcpy(ssid, buf, 32);
  memcpy(pass, buf + 33, 64);
  if (!ssid[0]) return false;
  settings::setWifi(ssid, pass);
  logbuf::logf("wifi: imported network \"%s\" from the stock firmware settings", ssid);
  return true;
}

// Stock NVS blob "klp_cfg": port (u16, little endian) at offset 0, IP string at offset 2.
bool importStockPrinter() {
  Preferences p;
  if (!p.begin("app_nvs", true)) return false;
  size_t n = p.getBytesLength("klp_cfg");
  uint8_t buf[64] = {0};
  if (n >= 18) p.getBytes("klp_cfg", buf, min(n, sizeof(buf)));
  p.end();
  if (n < 18) return false;
  char ip[17] = {0};
  memcpy(ip, buf + 2, 16);
  uint16_t port = buf[0] | (buf[1] << 8);
  if (!ip[0]) return false;
  Settings &s = settings::get();
  s.printer_host = ip;
  if (port) s.printer_port = port;
  settings::save();
  logbuf::logf("printer: imported %s:%u from the stock firmware settings", ip, port);
  return true;
}

void sendJson(const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void handleStatus() {
  String o = "{";
  feeder::appendStatusJson(o);
  o += ",\"fw\":\"" PBF_FW_VERSION "\"";
  o += ",\"pressure\":" + String(settings::get().pressure_level);
  o += ",\"wifi_mode\":\"" + String(ap_active ? (staConnected() ? "sta+ap" : "ap") : "sta") + "\"";
  o += ",\"ip\":\"" + ipString() + "\"";
  o += ",\"rssi\":" + String(staConnected() ? WiFi.RSSI() : 0);
  o += ",\"uptime\":" + String(millis() / 1000);
  o += "}";
  sendJson(o);
}

void handleLog() {
  uint32_t since = server.arg("since").toInt();
  String o = "{\"lines\":";
  uint32_t next = logbuf::appendJsonSince(since, o);
  o += ",\"next\":" + String(next) + "}";
  sendJson(o);
}

void handleSettingsGet() {
  String o;
  settings::appendJson(o);
  sendJson(o);
}

void handleSettingsPost() {
  // Validate the entire form first so a rejected field cannot leave a partial update.
  for (int i = 0; i < server.args(); i++) {
    if (server.argName(i) == "plain") continue;
    if (!settings::validate(server.argName(i), server.arg(i))) {
      String error = String("{\"error\":\"") +
                     (settings::get().english ? "invalid setting name or value" : "設定名稱或數值無效") +
                     "\",\"field\":";
      logbuf::appendJsonString(error, server.argName(i).c_str());
      error += '}';
      server.send(400, "application/json", error);
      return;
    }
  }
  for (int i = 0; i < server.args(); i++) {
    if (server.argName(i) == "plain") continue;
    settings::set(server.argName(i), server.arg(i), false);
  }
  settings::save();
  motor::setCurrentMa(hw::pressureLevelToMa(settings::get().pressure_level), settings::get().hold_ma);
  logbuf::line("web: settings saved");
  handleSettingsGet();
}

void handleAction() {
  String cmd = server.arg("cmd");
  if (cmd == "hold") {  // repeated every 250 ms while the button is pressed: no log line
    feeder::cmdHold(server.arg("dir") != "retract");
    sendJson("{\"ok\":true}");
    return;
  }
  if (cmd == "hold_stop") {  // also sent when the page is hidden: log only if something stops,
    feeder::cmdHoldStop();   // and do not wake the feeder (a phone locking is not a user action)
    sendJson("{\"ok\":true}");
    return;
  }
  feeder::wakeScreen();
  logbuf::logf("web: action %s", cmd.c_str());
  if (cmd == "feed_dist") feeder::cmdFeedDistance(server.arg("mm").toFloat());
  else if (cmd == "unload_dist") feeder::cmdUnloadDistance(server.arg("mm").toFloat());
  else if (cmd == "load") feeder::cmdLoad();
  else if (cmd == "unload") feeder::cmdUnload();
  else if (cmd == "stop") feeder::cmdStop();
  else if (cmd == "free") feeder::cmdFree(true);
  else if (cmd == "grip") feeder::cmdFree(false);
  else if (cmd == "jog") feeder::cmdJog(server.arg("mm").toFloat());
  else if (cmd == "ptest") feeder::cmdPressureTest();
  else if (cmd == "btest") feeder::cmdBufferTest();
  else if (cmd == "fulltest") feeder::cmdFullTest();
  else if (cmd == "stats_clear") stats::clear();
  else if (cmd == "beep") io::testBeep();
  else if (cmd == "notify_test") feeder::cmdNotifyTest();
  else if (cmd == "reboot") {
    sendJson("{\"ok\":true}");
    delay(300);
    ESP.restart();
  } else {
    sendJson("{\"ok\":false}");
    return;
  }
  sendJson("{\"ok\":true}");
}

void handleWifi() {
  settings::setWifi(server.arg("ssid"), server.arg("pass"));
  logbuf::line("web: WiFi saved, rebooting");
  sendJson("{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "OK" : "FAIL");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    motor::release();
    logbuf::logf("update: receiving %s", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) logbuf::logf("update: begin failed (%s)", Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      logbuf::logf("update: write failed (%s)", Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) logbuf::logf("update: %u bytes written, rebooting", up.totalSize);
    else logbuf::logf("update: failed (%s)", Update.errorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    logbuf::line("update: aborted");
  }
}

void startServices() {
  if (services_started) return;
  services_started = true;
  if (MDNS.begin(kHostname)) MDNS.addService("http", "tcp", 80);

  ArduinoOTA.setHostname(kHostname);
  ArduinoOTA.onStart([]() {
    motor::release();
    logbuf::line("ota: update started");
  });
  ArduinoOTA.onEnd([]() { logbuf::line("ota: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { logbuf::logf("ota: error %u", (unsigned)e); });
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", kIndexHtml); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  logbuf::line("web: server started, http://pbf.local");
}

void startAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[16];
  snprintf(name, sizeof(name), "PBF-%02X%02X", mac[4], mac[5]);
  ap_ssid = name;
  WiFi.mode(settings::get().wifi_ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(ap_ssid.c_str(), kApPassword);
  ap_active = true;
  logbuf::logf("wifi: access point %s (password %s), http://%s", ap_ssid.c_str(), kApPassword,
               WiFi.softAPIP().toString().c_str());
}

}  // namespace

void begin() {
  if (settings::get().wifi_ssid.isEmpty()) importStockWifi();
  if (settings::get().printer_host.isEmpty()) importStockPrinter();
  WiFi.persistent(false);
  WiFi.setHostname(kHostname);
  const Settings &s = settings::get();
  if (s.wifi_ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // lower latency for the web page
    WiFi.begin(s.wifi_ssid.c_str(), s.wifi_pass.c_str());
    sta_start_ms = millis();
    logbuf::logf("wifi: connecting to \"%s\"", s.wifi_ssid.c_str());
  } else {
    startAp();
    startServices();
  }
}

void update() {
  bool connected = staConnected();
  if (connected && !was_connected) {
    logbuf::logf("wifi: connected, IP %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    startServices();
    if (ap_active) ap_off_at_ms = millis() + kApOffAfterConnectMs;
  } else if (!connected && was_connected) {
    logbuf::line("wifi: disconnected, retrying");
  }
  was_connected = connected;

  if (!connected && !ap_active && settings::get().wifi_ssid.length() &&
      millis() - sta_start_ms > kStaTimeoutMs) {
    logbuf::line("wifi: could not connect, opening the access point");
    startAp();
    startServices();
  }
  if (ap_active && connected && ap_off_at_ms && (int32_t)(millis() - ap_off_at_ms) > 0) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_active = false;
    ap_off_at_ms = 0;
    logbuf::line("wifi: access point closed");
  }
  if (services_started) {
    server.handleClient();
    ArduinoOTA.handle();
  }
}

String ipString() {
  if (staConnected()) return WiFi.localIP().toString();
  if (ap_active) return WiFi.softAPIP().toString();
  return "";
}

String shortStatus() {
  if (staConnected()) return WiFi.localIP().toString();
  if (ap_active) return "AP " + ap_ssid;
  return settings::get().wifi_ssid.length() ? "WiFi..." : "no WiFi";
}

}  // namespace net
