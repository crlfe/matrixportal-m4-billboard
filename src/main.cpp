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
#include "gen-site.h"

// *** LED Matrix ***

#define IMAGE_WIDTH (64)
#define IMAGE_HEIGHT (64)
uint16_t image_bin[IMAGE_HEIGHT * IMAGE_WIDTH];

bool image_show = false;
unsigned long last_image_show = 0;

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

static void setup_matrix() {
  matrix.begin();
  matrix.fillScreen(0);
  matrix.show();
}

static void loop_matrix() {
  // DEBUG: Serial.printf("%lu: %d\n", millis(), (int)image_show);
  if (image_show) {
    matrix.drawRGBBitmap(0, 0, image_bin, 64, 64);
  } else {
    matrix.fillScreen(0);
  }

  matrix.show();
}

// *** JSON configuration ***

JsonDocument config_json;
JsonDocument frames_json;

// *** SPI flash and USB mass storage device ***
// <https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/MassStorage/msc_external_flash/msc_external_flash.ino>

Adafruit_FlashTransport_QSPI flash_transport;
Adafruit_SPIFlash flash(&flash_transport);

Adafruit_USBD_MSC flash_usb;
bool flash_changed_flag = false;
unsigned long flash_changed_ms = 0;

FatVolume flash_fat;
bool flash_fat_ok = false;

static int32_t flash_usb_read(uint32_t lba, void* buffer, uint32_t bufsize) {
  return flash.readBlocks(lba, (uint8_t*)buffer, bufsize / 512) ? bufsize : -1;
}

static int32_t flash_usb_write(uint32_t lba,
                               uint8_t* buffer,
                               uint32_t bufsize) {
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

static void flash_usb_flush() {
  flash.syncBlocks();
  flash_fat.cacheClear();

  flash_changed_flag = true;
  flash_changed_ms = millis();
}

static void setup_flash() {
  if (!flash.begin()) {
    // TODO
  }

  flash_usb.setID("Adafruit", "External Flash", "1.0");
  flash_usb.setCapacity(flash.pageSize() * flash.numPages() / 512, 512);
  flash_usb.setReadWriteCallback(flash_usb_read, flash_usb_write,
                                 flash_usb_flush);
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
  unsigned long last_begin;
  unsigned long last_progress;

  uint8_t data[16 * 1024];
  size_t dataPos;
  size_t dataLen;

  const char* method;
  const char* resource;
  const char* version;
  const char* authorization;
  const char* contentType;
  unsigned long contentLength;

  const uint8_t* replyData;
  size_t replyPos;
  size_t replyLen;

  void clear() {
    state = STATE_READING_REQUEST;
    sock.stop();
    last_begin = 0;
    last_progress = 0;
    bzero(data, sizeof(data));
    dataPos = 0;
    dataLen = 0;
    method = NULL;
    resource = NULL;
    version = NULL;
    authorization = NULL;
    contentType = NULL;
    contentLength = 0;
    replyData = NULL;
    replyPos = 0;
    replyLen = 0;
  }

  void begin(WiFiClient sock) {
    clear();
    this->sock = sock;
    last_begin = last_progress = millis();
  }

  void run() {
    // Check timeouts.
    unsigned long now = millis();
    if (sock) {
      if (now - last_begin > 15000) {
        Serial.printf("http connection timeout (body=%ld)\n",
                      (long)dataLen - (long)dataPos);
        state = STATE_CLOSE;
      } else if (now - last_progress > 1000) {
        Serial.printf("http connection idle timeout (body=%ld)\n",
                      (long)dataLen - (long)dataPos);
        state = STATE_CLOSE;
      }
    }

    if (state == STATE_READING_REQUEST || state == STATE_READING_HEADERS) {
      int n = sock.available()
                  ? sock.read(data + dataLen, sizeof(data) - dataLen)
                  : 0;
      if (n <= 0) {
        return;
      }
      last_progress = now;

      // Scan the data for newlines, starting at the beginning of the latest
      // line.
      dataLen += n;
      for (size_t i = dataPos; i < dataLen;) {
        n = matchNewline(i);
        if (n <= 0) {
          i++;
          continue;
        }

        char* line = (char*)data + dataPos;
        data[i] = '\0';

        dataPos = i + n;
        i = dataPos;

        if (line[0] == '\0') {
          // Found a blank line.
          state = processRequestDone();
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
      int n =
          avail > 0 ? sock.read(data + dataLen, min((size_t)avail, sizeof(data) - dataLen))
                : 0;
      if (n <= 0) {
        return;
      }
      last_progress = now;

      dataLen += n;
      if (dataLen > dataPos && dataLen - dataPos >= contentLength) {
        state = processBodyDone();
      }
    } else if (state == STATE_WRITING_REPLY) {
      // TODO: Would be nice to check sock.availableForWrite, but that seems to
      // be unimplemented. For the moment, throttling our write to 1K buffers
      // seems to avoid problems.
      size_t n =
          sock.write(replyData + replyPos, min(1024u, replyLen - replyPos));
      if (n <= 0) {
        return;
      }
      last_progress = now;

      replyPos += n;
      if (replyPos >= replyLen || !sock.connected()) {
        state = STATE_CLOSE;
      }
    } else if (state == STATE_CLOSE) {
      sock.stop();
    }
  }

 private:
  int matchNewline(size_t i) const {
    char a = i + 0 < dataLen ? data[i + 0] : '\0';
    char b = i + 1 < dataLen ? data[i + 1] : '\0';

    if (a == '\r' && b == '\n') {
      return 2;
    } else if (a == '\r' || a == '\n') {
      return 1;
    } else {
      return 0;
    }
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
      contentType = p1;
    } else if (strcasecmp(p0, "Content-Length") == 0) {
      // Parse error is indicated by ULONG_MAX.
      unsigned long length = strtoul(p1, NULL, 10);
      if (dataLen > sizeof(data) || length > sizeof(data) - dataLen) {
        return sendReplyStatus(413, "Content Too Large", "");
      }
      contentLength = length;
    } else if (strcasecmp(p0, "Authorization") == 0) {
      authorization = p1;
    }

    return STATE_READING_HEADERS;
  }

  State processRequestDone() {
    if (!method || !resource || !version) {
      return sendReplyStatus(400, "Bad Request", "");
    } else if (!authorization || !checkAuthorization(authorization)) {
      return sendReplyStatus(
          401, "Unauthorized",
          "WWW-Authenticate: Basic realm=\"billboard\", charset=\"UTF-8\"\r\n");
    } else if (strcmp(method, "GET") == 0) {
      if (strcmp(resource, "/image.bin") == 0) {
        return sendReplyData(200, "OK", "application/octet-stream",
                             sizeof(image_bin), NULL,
                             (const uint8_t*)image_bin);
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
    } else if (strcmp(method, "PUT") == 0 &&
               strcmp(resource, "/image.bin") == 0) {
      return STATE_READING_BODY;
    } else {
      return sendReplyStatus(405, "Method Not Allowed", "");
    }
  }

  State processBodyDone() {
    // Should only reach here if the client is uploading a new image.bin.
    if (!method || !resource || !version || strcmp(method, "PUT") != 0 ||
        strcmp(resource, "/image.bin") != 0 ||
        dataPos + sizeof(image_bin) > dataLen) {
      Serial.printf("http PUT image.bin failed (contentLength=%ld, body=%ld)\n",
                    (long)contentLength, (long)dataLen - (long)dataPos);
      return sendReplyStatus(500, "Internal Server Error", "");
    }
    memcpy(image_bin, data + dataPos, sizeof(image_bin));

    // Overwrite the saved image file. TODO: Is the SPI flash slow enough that
    // this need to be async?
    File32 file = flash_fat.open("/image.bin", O_CREAT | O_TRUNC | O_WRONLY);
    if (file) {
      file.write(image_bin, sizeof(image_bin));
      if (!file.close()) {
        Serial.print("http PUT image.bin failed write\n");
        return sendReplyStatus(500, "Internal Server Error", "");
      }
    }

    // DEBUG: Serial.printf("new upload at %lu\n", millis());

    // Always display the newly-updated image for a while.
    image_show = true;
    last_image_show = millis();

    return sendReplyStatus(200, "OK", "");
  }

  bool checkAuthorization(const char* auth) {
    String expect("Basic ");
    {
      Base64Encoder encoder(expect);
      encoder.puts(config_json["http"]["user"]);
      encoder.putc(':');
      encoder.puts(config_json["http"]["pass"]);
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
                      const uint8_t* body) {
    static const char* kFormat =
        "HTTP/1.1 %d %s\r\n"
        "Connection: close\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "%s%s%s"  // Possible Content-Encoding header
        "\r\n";

    size_t cap = sizeof(data) - dataLen;
    int n = snprintf((char*)data + dataLen, cap, kFormat, code, title, type,
                     length, encoding ? "Content-Encoding: " : "",
                     encoding ? encoding : "", encoding ? "\r\n" : "");
    if (n <= 0 || (size_t)n >= cap || (size_t)n + length >= cap) {
      return sendReplyStatus(500, "Internal Server Error", "");
    }
    memcpy(data + dataLen + n, body, length);
    replyData = data + dataLen;
    replyPos = 0;
    replyLen = n + length;
    return STATE_WRITING_REPLY;
  }

  State sendReplyStatus(int code,
                        const char* title,
                        const char* extra_headers) {
    static const char* kFormat =
        "HTTP/1.1 %d %s\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "%s"
        "\r\n"
        "%s\r\n";
    int n = snprintf((char*)data, sizeof(data), kFormat, code, title,
                     strlen(title) + 2, extra_headers, title);
    replyData = data;
    replyPos = 0;
    replyLen = n > 0 && (size_t)n < sizeof(data) ? n : 0;
    return STATE_WRITING_REPLY;
  }

} http_connection;

static void loop_wifi() {
  static enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED
  } state = STATE_IDLE;
  static unsigned long state_change_ms;

  int status = WiFiDrv::getConnectionStatus();
  if (status != WL_CONNECTED) {
    // Retry the connection after thirty seconds.
    if (state != STATE_CONNECTING || status == WL_DISCONNECTED ||
        (state == STATE_CONNECTING && millis() - state_change_ms > 30000)) {
      const JsonString& ssid = config_json["wifi"]["ssid"];
      const JsonString& pass = config_json["wifi"]["pass"];
      if (ssid && pass) {
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

static bool check_json_file(const char* path, JsonDocument* dst) {
  bool changed = false;

  File32 file = flash_fat.open(path);
  uint16_t mdate = 0, mtime = 0;
  if (!file.getModifyDateTime(&mdate, &mtime) || mdate != (*dst)["_mdate"] ||
      mtime != (*dst)["_mtime"]) {
    changed = true;
    deserializeJson(*dst, file);
    (*dst)["_mdate"] = mdate;
    (*dst)["_mtime"] = mtime;
  }

  return changed;
}

static int get_hours_minutes() {
  // TODO: hard-coded UTC-4

  unsigned long seconds = wifi.getTime();
  if (seconds > 0) {
    time_t t = seconds;
    tm td = {0};
    if (localtime_r(&t, &td)) {
      return ((td.tm_hour + 20) % 24) * 100 + td.tm_min;
    }
  }
  return -1;
}

void setup() {
  setup_flash();
  setup_matrix();

  flash_changed_flag = true;
  flash_changed_ms = millis();
}

void loop() {
  if (flash_changed_flag && millis() - flash_changed_ms > 1000) {
    flash_changed_flag = false;
    flash_changed_ms = 0;

    if (!flash_fat_ok) {
      flash_fat_ok = flash_fat.begin(&flash);
    }

    if (check_json_file("config.json", &config_json)) {
      wifi.disconnect();
    }

    File32 file = flash_fat.open("/image.bin", O_BINARY | O_RDONLY);
    if (file.size() == sizeof(image_bin)) {
      file.readBytes((uint8_t*)image_bin, sizeof(image_bin));
    } else {
      bzero(image_bin, sizeof(image_bin));
    }
    file.close();

    // Always display the newly-loaded image for a while.
    image_show = true;
    last_image_show = millis();
  }

  // Throttle polling the ESP32 to once every 50ms.
  static unsigned long last_wifi = millis();
  if (millis() - last_wifi > 50) {
    last_wifi = millis();
    loop_wifi();
  }

  // Apply the time of day value every thirty seconds.
  if (millis() - last_image_show > 30000) {
    int hm = get_hours_minutes();
    if (hm >= 0) {
      image_show = (hm < 600 || hm > 2100);
      last_image_show = millis();
    }
  }

  // Throttle matrix refresh to once every second.
  static unsigned long last_matrix = millis();
  if (millis() - last_matrix > 1000) {
    last_matrix = millis();
    loop_matrix();

    // Serial.printf("state %d %lu %lu\n", (int)http_connection.state,
    // http_connection.replyPos, http_connection.replyLen);
  }
}
