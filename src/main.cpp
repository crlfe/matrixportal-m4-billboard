#include <time.h>

#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <Adafruit_Protomatter.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <ArduinoJson.h>
#include <ArduinoMDNS.h>

// The WIFI driver provides some calls that block less than the high-level
// wrapper.
#include <utility/wifi_drv.h>

#include "Base64Encoder.hh"
#include "FixedBuffer.hh"

#include "gen-site.h"

// *** JSON configuration ***

JsonDocument config_json;
JsonDocument frames_json;

bool frames_saving = false;
unsigned long frames_saving_stamp = 0;

static float getRequestedGain() {
  return frames_json["gain"].is<float>() ? frames_json["gain"].as<float>()
                                         : 0.5;
}

static int getRotation() {
  return frames_json["rotation"].is<int>() ? frames_json["rotation"].as<int>()
                                           : 0;
}

static int getMorning() {
  return frames_json["morning"].is<int>() ? frames_json["morning"].as<int>()
                                          : 900;
}

static int getEvening() {
  return frames_json["evening"].is<int>() ? frames_json["evening"].as<int>()
                                          : 1600;
}

// *** LED Matrix ***

#define IMAGE_WIDTH (64)
#define IMAGE_HEIGHT (64)
uint8_t image_bin[IMAGE_HEIGHT][IMAGE_WIDTH][4];

bool image_saving = false;
unsigned long image_saving_stamp = 0;

bool image_showing = false;
unsigned long image_showing_stamp = 0;
unsigned long image_refresh_stamp = 0;

// BUG: The Protomatter library requires the pin arrays to be non-const.
Adafruit_Protomatter matrix(
    IMAGE_WIDTH,
    6,
    1,                                 // Number of RGB pin sets.
    (uint8_t[]){7, 8, 9, 10, 11, 12},  // RGB pins, six per set.
    4,  // Number of address pins is (N+1) and image height is 2**(N+1).
    (uint8_t[]){17, 18, 19, 20, 21},  // Address pins.
    14,                               // Clock pin.
    15,                               // Latch pin.
    16,                               // Output enable pin.
    false,                            // Double buffered.
    -2  // Two matrix panels in serpentine arrangement.
);

static void setupMatrix() {
  matrix.begin();
  matrix.fillScreen(0);
  matrix.show();
}

static void loopMatrix() {
  // DEBUG: Serial.printf("%lu: %d\n", millis(), (int)image_show);
  if (image_showing) {
    // TODO: High gain when insifficiently powered (like over USB from a laptop)
    // will cause voltage drop and system crashes. Consider always starting the
    // actual gain at zero and slowly increasing until we hit the requested
    // limit or see power ripples.
    float gain = getRequestedGain();

    // TODO: Automatic rotation based on the accelerometer would be cool.
    // TODO: Consider using the DMA to scan out lines and avoid the sleep-based
    // library.

    int rotation = getRotation();
    if (rotation == 90)
      matrix.setRotation(1);
    else if (rotation == 180)
      matrix.setRotation(2);
    else if (rotation == 270)
      matrix.setRotation(3);
    else
      matrix.setRotation(0);

    for (int y = 0; y < IMAGE_HEIGHT; y++) {
      for (int x = 0; x < IMAGE_WIDTH; x++) {
        const uint8_t* rgba = image_bin[y][x];
        uint8_t r = constrain(rgba[0] * gain, 0, 255);
        uint8_t g = constrain(rgba[1] * gain, 0, 255);
        uint8_t b = constrain(rgba[2] * gain, 0, 255);
        matrix.drawPixel(x, y, matrix.color565(r, g, b));
      }
    }
  } else {
    matrix.fillScreen(0);
  }

  matrix.show();
}

// *** SPI flash and USB mass storage device ***
// <https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/MassStorage/msc_external_flash/msc_external_flash.ino>

Adafruit_FlashTransport_QSPI flash_transport;
Adafruit_SPIFlash flash(&flash_transport);

Adafruit_USBD_MSC flash_usb;
bool flash_changed_flag = false;
unsigned long flash_changed_ms = 0;

FatVolume flash_fat;
bool flash_fat_ok = false;

static int32_t flashUsbRead(uint32_t lba, void* buffer, uint32_t bufsize) {
  return flash.readBlocks(lba, (uint8_t*)buffer, bufsize / 512) ? bufsize : -1;
}

static int32_t flashUsbWrite(uint32_t lba,
                               uint8_t* buffer,
                               uint32_t bufsize) {
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

static void flashUsbFlush() {
  flash.syncBlocks();
  flash_fat.cacheClear();

  flash_changed_flag = true;
  flash_changed_ms = millis();
}

static void setupFlash() {
  if (!flash.begin()) {
    // TODO: Need a fatal error logger
    while (true) {
    }
  }

  flash_usb.setID("Adafruit", "External Flash", "1.0");
  flash_usb.setCapacity(flash.pageSize() * flash.numPages() / 512, 512);
  flash_usb.setReadWriteCallback(flashUsbRead, flashUsbWrite,
                                 flashUsbFlush);
  flash_usb.setUnitReady(true);
  flash_usb.begin();

  // Force the host to refresh our USB devices.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

// *** WiFi and HTTP server ***

WiFiClass wifi;
WiFiUDP udp;
MDNS mdns(udp);
WiFiServer http_server(80);
String http_auth;

static int getHoursMinutes() {
  // TODO: Would be *really* nice to have automatic dawn and dusk values.

  unsigned long seconds = wifi.getTime();
  if (seconds > 0) {
    time_t t = seconds;
    tm td = {0};
    if (gmtime_r(&t, &td)) {
      return td.tm_hour * 100 + td.tm_min;
    }
  }
  return -1;
}

class HttpServerConnection {
 public:
  enum State {
    STATE_READING_REQUEST,
    STATE_READING_HEADERS,
    STATE_READING_BODY,
    STATE_WRITING_REPLY,
    STATE_CLOSE,
  } state;

  WiFiClient sock;
  unsigned long connection_begin_ms;
  unsigned long connection_change_ms;

  FixedBuffer<20480> data;

  const char* method;
  const char* resource;
  const char* version;
  const char* authorization;
  const char* content_type;
  unsigned long content_length;

  const uint8_t* reply_data;
  size_t reply_pos;
  size_t reply_len;

  void clear() {
    state = STATE_READING_REQUEST;
    sock.stop();
    connection_begin_ms = 0;
    connection_change_ms = 0;
    data.clear();
    method = NULL;
    resource = NULL;
    version = NULL;
    authorization = NULL;
    content_type = NULL;
    content_length = 0;
    reply_data = NULL;
    reply_pos = 0;
    reply_len = 0;
  }

  void begin(WiFiClient sock) {
    clear();
    this->sock = sock;
    unsigned long now = millis();
    connection_begin_ms = now;
    connection_change_ms = now;
  }

  void run() {
    // Check timeouts.
    unsigned long now = millis();
    if (sock) {
      if (now - connection_begin_ms > 15000) {
        Serial.printf("http connection timeout (body=%ld)\n", data.size());
        state = STATE_CLOSE;
      } else if (now - connection_change_ms > 1000) {
        Serial.printf("http connection idle timeout (body=%ld)\n", data.size());
        state = STATE_CLOSE;
      }
    }

    if (state == STATE_READING_REQUEST || state == STATE_READING_HEADERS) {
      int n = sock.available() ? sock.read(data.end(), data.remaining()) : 0;
      if (n <= 0) {
        return;
      }
      data.advanceEnd(n);

      connection_change_ms = now;

      // Scan the data for newlines, starting at the beginning of the latest
      // line.
      while (state == STATE_READING_REQUEST || state == STATE_READING_HEADERS) {
        char* line = consumeLine();
        if (!line) {
          // Have not received a complete line yet, wait for more data.
          break;
        }
        // DEBUG: Serial.printf("line: %s\n", line);

        if (line[0] == '\0') {
          // Found a blank line, process the end of headers.
          state = processHeadersDone();
          break;
        }

        // Found a non-empty line.
        if (state == STATE_READING_REQUEST) {
          state = processRequestLine(line);
        } else if (state == STATE_READING_HEADERS) {
          state = processHeaderLine(line);
        }
      }
    } else if (state == STATE_READING_BODY) {
      int avail = sock.available();
      int n = avail > 0
                  ? sock.read(data.end(), min((size_t)avail, data.remaining()))
                  : 0;
      if (n > 0) {
        data.advanceEnd(n);
        connection_change_ms = now;
      }

      if (data.size() >= content_length || !data.remaining()) {
        state = processBodyDone();
      }
    } else if (state == STATE_WRITING_REPLY) {
      // TODO: Would be nice to check sock.availableForWrite, but that seems to
      // be unimplemented. For the moment, throttling our write to 1K buffers
      // seems to avoid problems.
      size_t n =
          sock.write(reply_data + reply_pos, min(1024u, reply_len - reply_pos));
      if (n > 0) {
        connection_change_ms = now;
        reply_pos += n;
      }

      if (reply_pos >= reply_len || !sock.connected()) {
        state = STATE_CLOSE;
      }
    } else if (state == STATE_CLOSE) {
      sock.stop();
    }
  }

 private:
  char* consumeLine() {
    char* saved = reinterpret_cast<char*>(data.begin());
    for (size_t i = 0; i < data.size(); i++) {
      char a = data.get(i + 0);
      char b = data.get(i + 1);
      if (a == '\r' && b == '\n') {
        data.set(i, '\0');
        data.advanceBegin(i + 2);
        return saved;
      } else if (a == '\r' || a == '\n') {
        data.set(i, '\0');
        data.advanceBegin(i + 1);
        return saved;
      }
    }
    return nullptr;
  }

  State processRequestLine(char* line) {
    char* p0 = line;
    char* p1 = strchr(line, ' ');
    char* p2 = (p1 && *p1) ? strchr(p1 + 1, ' ') : NULL;

    if (!p1 || !p2) {
      return sendReplyStatus(400, "Bad Request", "");
    }

    *p1 = '\0';
    *p2 = '\0';

    method = p0;
    resource = p1 + 1;
    version = p2 + 1;

    return STATE_READING_HEADERS;
  }

  State processHeaderLine(char* line) {
    char* p0 = line;
    char* p1 = strchr(p0, ':');

    if (!p1) {
      return sendReplyStatus(400, "Bad Request", "");
    }

    // DEBUG: Serial.printf("header '%s' '%s'\n", p0, p1);

    *p1 = '\0';
    p1++;
    if (*p1 == ' ') {
      p1++;
    }

    if (strcasecmp(p0, "Content-Type") == 0) {
      content_type = p1;
    } else if (strcasecmp(p0, "Content-Length") == 0) {
      // Parse error is indicated by ULONG_MAX.
      unsigned long length = strtoul(p1, NULL, 10);
      if (length > data.remaining()) {
        return sendReplyStatus(413, "Content Too Large", "");
      }
      content_length = length;
    } else if (strcasecmp(p0, "Authorization") == 0) {
      authorization = p1;
    }

    return STATE_READING_HEADERS;
  }

  State processHeadersDone() {
    // DEBUG: Serial.printf("HTTP request %s %s %s\n", method, resource,
    // version);

    if (!method || !resource || !version) {
      return sendReplyStatus(400, "Bad Request", "");
    } else if (!authorization || !checkAuthorization(authorization)) {
      return sendReplyStatus(
          401, "Unauthorized",
          "WWW-Authenticate: Basic realm=\"billboard\", charset=\"UTF-8\"\r\n");
    } else if (strcmp(method, "GET") == 0) {
      if (strcmp(resource, "/api/image") == 0) {
        return sendReplyData(200, "OK", "application/octet-stream",
                             sizeof(image_bin), NULL, image_bin);
      } else if (strcmp(resource, "/api/gain") == 0) {
        JsonDocument message;
        message["value"] = getRequestedGain();
        return sendReplyJson(200, "OK", message);
      } else if (strcmp(resource, "/api/rotation") == 0) {
        JsonDocument message;
        message["rotation"] = getRotation();
        return sendReplyJson(200, "OK", message);
      } else if (strcmp(resource, "/api/time") == 0) {
        JsonDocument message;
        message["morning"] = getMorning();
        message["evening"] = getEvening();
        return sendReplyJson(200, "OK", message);
      } else if (strcmp(resource, "/api/now") == 0) {
        JsonDocument message;
        message["value"] = getHoursMinutes();
        return sendReplyJson(200, "OK", message);
      }

      const site_entry* file = findSiteFile(resource);
      if (!file) {
        // Try "${resource}/index.html"
        String index(resource);
        if (!index.endsWith("/")) {
          index += "/";
        }
        index += "index.html";
        file = findSiteFile(index.c_str());
      }
      if (!file) {
        file = findSiteFile("/index.html");
      }
      if (!file) {
        file = findSiteFile("/404.html");
      }

      if (file) {
        return sendReplyData(200, "OK", file->type, file->length,
                             file->encoding, site_data + file->offset);
      } else {
        return sendReplyStatus(404, "Not Found", "");
      }
    } else if (strcmp(method, "POST") == 0 &&
               (strcmp(resource, "/api/image") == 0 ||
                strcmp(resource, "/api/gain") == 0 ||
                strcmp(resource, "/api/rotation") == 0 ||
                strcmp(resource, "/api/time") == 0)) {
      return STATE_READING_BODY;
    } else {
      return sendReplyStatus(405, "Method Not Allowed", "");
    }
  }

  State processBodyDone() {
    const unsigned long now = millis();

    if (!method || !resource || !version) {
      return sendReplyStatus(400, "Bad Request", "");
    }

    if (strcmp(method, "POST") == 0 && strcmp(resource, "/api/image") == 0) {
      if (data.size() != sizeof(image_bin)) {
        Serial.printf(
            "http PUT image.bin failed (contentLength=%lu, body=%lu)\n",
            content_length, data.size());
        return sendReplyStatus(500, "Internal Server Error", "");
      }

      // DEBUG: Serial.printf("new upload at %lu\n", millis());

      memcpy(image_bin, data.begin(), sizeof(image_bin));

      // Save the image if we make it through the next while without crashing.
      image_saving = true;
      image_saving_stamp = now;

      // Always display the newly-updated image for a while.
      image_showing = true;
      image_showing_stamp = now;
      image_refresh_stamp = now - 60000u;  // Refresh immediately.

      return sendReplyStatus(200, "OK", "");
    }

    if (strcmp(method, "POST") == 0 &&
        (strcmp(resource, "/api/gain") == 0 ||
         strcmp(resource, "/api/rotation") == 0 ||
         strcmp(resource, "/api/time") == 0)) {
      bool is_gain = resource[5] == 'g';
      bool is_rotation = resource[5] == 'r';

      // Ensure the content is NUL-terminated for the JSON parser.
      data.print('\0');

      JsonDocument message;
      auto err = deserializeJson(message, data.begin());
      if (err) {
        return sendReplyStatus(500, "Internal Server Error", "");
      }

      if (is_gain) {
        float value = constrain(message["value"].as<float>(), 0.0, 1.0);
        frames_json["gain"] = value;
      } else if (is_rotation) {
        frames_json["rotation"] = message["value"].as<int>();
      } else {
        if (message["morning"].is<int>()) {
          frames_json["morning"] = message["morning"].as<int>();
        }
        if (message["evening"].is<int>()) {
          frames_json["evening"] = message["evening"].as<int>();
        }
      }

      frames_saving = true;
      frames_saving_stamp = millis();

      // Always display the newly-updated image for a while.
      image_showing = true;
      image_showing_stamp = now;
      image_refresh_stamp = now;

      // Apply the changed setting immediately.
      if (is_gain || is_rotation) {
        image_refresh_stamp -= 60000;
      } else {
        image_showing_stamp -= 60000;
      }

      return sendReplyStatus(200, "OK", "");
    }

    // Should have rejected this at the end of the headers.
    return sendReplyStatus(500, "Internal Server Error", "");
  }

  bool checkAuthorization(const char* auth) {
    String expect("Basic ");
    {
      Base64Encoder encoder(expect);
      encoder.print(config_json["http"]["user"].as<const char*>());
      encoder.print(':');
      encoder.print(config_json["http"]["pass"].as<const char*>());
    }
    return expect == auth;
  }

  const site_entry* findSiteFile(const char* name) {
    for (size_t i = 0; site_table[i].name; i++) {
      if (strcmp(name, site_table[i].name) == 0) {
        return &site_table[i];
      }
    }
    return NULL;
  }

  State sendReplyData(int code,
                      const char* title,
                      const char* type,
                      const size_t length,
                      const char* encoding,
                      const void* body) {
    data.advanceBegin(data.size());
    data.print("HTTP/1.1 ");
    data.print(code);
    data.print(" ");
    data.print(title);
    data.print("\r\nConnection: close\r\nContent-Type: ");
    data.print(type);
    data.print("\r\nContent-Length: ");
    data.print(length);
    if (encoding) {
      data.print("\r\nContent-Encoding: ");
      data.print(encoding);
    }
    data.print("\r\n\r\n");
    data.write(reinterpret_cast<const uint8_t*>(body), length);
    // TODO: Check for errors.

    reply_data = data.begin();
    reply_pos = 0;
    reply_len = data.size();
    return STATE_WRITING_REPLY;
  }

  State sendReplyJson(int code,
                      const char* title,
                      const JsonDocument& content) {
    data.advanceBegin(data.size());
    data.print("HTTP/1.1 ");
    data.print(code);
    data.print(" ");
    data.print(title);
    data.print(
        "\r\nConnection: close"
        "\r\nContent-Type: application/json"
        "\r\nContent-Length: ");
    // Reserve six characters for the length, to be filled later.
    char* content_length_buffer = reinterpret_cast<char*>(data.end());
    data.print("      \r\n\r\n");

    size_t content_start = data.size();
    serializeJson(content, data);
    size_t content_size = data.size() - content_start;

    // TODO: Unhack this!
    ultoa(content_size, content_length_buffer, 10);
    char* content_length_end = strchr(content_length_buffer, '\0');
    if (content_length_end) {
      *content_length_end = ' ';
    }

    reply_data = data.begin();
    reply_pos = 0;
    reply_len = data.size();
    return STATE_WRITING_REPLY;
  }

  State sendReplyStatus(int code,
                        const char* title,
                        const char* extra_headers) {
    data.advanceBegin(data.size());
    data.print("HTTP/1.1 ");
    data.print(code);
    data.print(" ");
    data.print(title);
    data.print(
        "\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: ");
    data.print(strlen(title) + 2);
    data.print("\r\n");
    data.print(extra_headers);
    data.print("\r\n");
    data.print(title);
    data.print("\r\n");
    // TODO: Check for errors.

    reply_data = data.begin();
    reply_pos = 0;
    reply_len = data.size();
    return STATE_WRITING_REPLY;
  }
};

HttpServerConnection http_connection;

static void loopWifi() {
  static enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED
  } state = STATE_IDLE;
  static unsigned long state_change_ms = millis() - 60000;

  int status = WiFiDrv::getConnectionStatus();
  if (status != WL_CONNECTED) {
    // Retry the connection after thirty seconds.
    if (state != STATE_CONNECTING || status == WL_DISCONNECTED ||
        (state == STATE_CONNECTING && millis() - state_change_ms > 10000)) {
      const JsonString& ssid = config_json["wifi"]["ssid"];
      const JsonString& pass = config_json["wifi"]["pass"];
      if (ssid && pass) {
        Serial.printf("%lu: wifi connecting to %s\n", millis(), ssid.c_str());
        // This is the same operation run from wifi.begin(...), but that wrapper
        // may block for a long time waiting for the connection to succeed.
        WiFiDrv::wifiSetPassphrase(ssid.c_str(), ssid.size(), pass.c_str(),
                                   pass.size());
        state = STATE_CONNECTING;
        state_change_ms = millis();
      } else {
        WiFiDrv::disconnect();
        state = STATE_IDLE;
        state_change_ms = millis();
      }
    }
  } else if (state != STATE_CONNECTED) {
    // Just connected to the network.
    Serial.printf("%lu: wifi connected\n", millis());
    state = STATE_CONNECTED;
    state_change_ms = millis();

    const JsonString& name = config_json["wifi"]["name"];
    if (name) {
      Serial.printf("%lu: mdns begin ", millis());
      wifi.localIP().printTo(Serial);
      Serial.printf(" '%s'\n", name.c_str());
      mdns.begin(wifi.localIP(), name.c_str());
    }

    http_server.begin();
  } else {
    // Run normal network services.
    mdns.run();

    if (!http_connection.sock) {
      http_connection.begin(http_server.available());
    }
    if (http_connection.sock) {
      http_connection.run();
    }
  }
}

// *** Main Application ***

static bool checkJsonFile(const char* path, JsonDocument& dst) {
  bool changed = false;

  File32 file = flash_fat.open(path);
  if (file) {
    uint16_t mdate = 0, mtime = 0;
    if (!file.getModifyDateTime(&mdate, &mtime) || mdate != (dst)["_mdate"] ||
        mtime != (dst)["_mtime"]) {
      Serial.printf("%lu: loaded %s\n", millis(), path);
      changed = true;
      deserializeJson(dst, file);
      (dst)["_mdate"] = mdate;
      (dst)["_mtime"] = mtime;
    } else {
      Serial.printf("%lu: unchanged %s\n", millis(), path);
    }
  } else {
    Serial.printf("%lu: failed %s\n", millis(), path);
  }

  return changed;
}

void setup() {
  setupFlash();
  setupMatrix();

  flash_changed_flag = true;
  flash_changed_ms = millis();
}

void loop() {
  if (flash_changed_flag && millis() - flash_changed_ms > 1000) {
    flash_changed_flag = false;
    flash_changed_ms = 0;

    // Changes to the flash override anything from the web server.
    image_saving = false;

    if (!flash_fat_ok) {
      flash_fat_ok = flash_fat.begin(&flash);
    }

    if (checkJsonFile("/config.json", config_json)) {
      wifi.disconnect();
    }

    checkJsonFile("/frames.json", frames_json);

    File32 file = flash_fat.open("/image.bin", O_BINARY | O_RDONLY);
    bzero(image_bin, sizeof(image_bin));
    if (file) {
      if (file.size() == sizeof(image_bin)) {
        file.readBytes((uint8_t*)image_bin, sizeof(image_bin));
      }
      file.close();
    }

    // Always display the newly-loaded image for a while.
    image_showing = true;
    image_showing_stamp = millis();
  }

  // Throttle polling the ESP32 to once every 50ms.
  static unsigned long last_wifi = millis();
  if (millis() - last_wifi > 50) {
    last_wifi = millis();
    loopWifi();
  }

  // Apply the time of day value every thirty seconds.
  if (millis() - image_showing_stamp > 30000) {
    int hm = getHoursMinutes();
    if (hm >= 0) {
      int morning = getMorning();
      int evening = getEvening();
      if (morning < evening) {
        image_showing = (hm <= morning || hm >= evening);
      } else {
        image_showing = (hm >= morning || hm <= evening);
      }
      image_showing_stamp = millis();
    }
  }

  // Throttle matrix refresh to once every second.
  if (millis() - image_refresh_stamp > 1000) {
    image_refresh_stamp = millis();
    loopMatrix();
  }

  if (image_saving && millis() - image_saving_stamp > 1000) {
    image_saving = false;

    File32 file = flash_fat.open("/image.bin", O_CREAT | O_TRUNC | O_WRONLY);
    if (file) {
      Serial.printf("%lu: writing /image.bin\n", millis());
      file.write(image_bin, sizeof(image_bin));

      if (!file.close()) {
        Serial.printf("%lu: error flushing /image.bin\n", millis());
      }
    } else {
      Serial.printf("%lu: error writing /image.bin\n", millis());
    }
  }

  if (frames_saving && millis() - frames_saving_stamp > 1000) {
    frames_saving = false;

    File32 file;
    if (file.open(&flash_fat, "frames.json", O_CREAT | O_TRUNC | O_WRONLY)) {
      Serial.printf("%lu: writing frames.json\n", millis());
      frames_json.remove("_mdate");
      frames_json.remove("_mtime");

      serializeJsonPretty(frames_json, file);

      uint16_t mdate = 0, mtime = 0;
      file.getModifyDateTime(&mdate, &mtime);
      frames_json["_mdate"] = mdate;
      frames_json["_mtime"] = mtime;

      if (!file.close()) {
        Serial.printf("%lu: error flushing frames.json\n", millis());
      }
    } else {
      Serial.printf("%lu: error writing frames.json\n", millis());
    }
  }
}
