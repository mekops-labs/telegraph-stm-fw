#include <Arduino.h>

#ifndef VERSION
    #error "VERSION not defined! Please set VERSION in platformio.ini build_flags."
    #define VERSION "unknown"
#endif

// == WIFI (OPTIONAL) ==
// The file `credentials.ini` holds the credentials. The build passes them as
// flags.
//
// Note: without the credentials the firmware still builds and runs. The USB
// serial console controls all functions. Only the web interface, the OTA and
// the network bridge are absent.
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  #define HAS_WIFI 1
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <ArduinoOTA.h>
#endif

// == PIN DEFINITIONS ==
// The default values are for the Waveshare ESP32 One. The XIAO ESP32-S3
// environment replaces them with build flags from platformio.ini.
#ifndef STM_TX_PIN
#define STM_TX_PIN 13   // Connect to STM32 RX (PA10)
#endif
#ifndef STM_RX_PIN
#define STM_RX_PIN 14   // Connect to STM32 TX (PA9)
#endif
#ifndef PIN_BOOT0
#define PIN_BOOT0  23
#endif
#ifndef PIN_RST
#define PIN_RST    18
#endif
#ifndef PIN_LED
#define PIN_LED    21
#endif

// == BAUD RATES ==
#define DEBUG_BAUD      115200 // USB Serial
#define BOOTLOADER_BAUD 57600  // Bootloader Sync (Stable)
#define BRIDGE_BAUD     115200 // Normal Operation
#define SER2NET_PORT    8888   // TCP Port for Serial Bridge
#define WIFI_CONNECT_TIMEOUT_MS 20000

#ifdef HAS_WIFI
WebServer server(80);
WiFiServer ser2netServer(SER2NET_PORT);
WiFiClient tcpClient;
bool netUp = false;   // True after the servers start.

// The WiFi event task stores these values. The main task reports them.
//
// Note: a printf call from the event task does not always reach the USB port.
volatile int lastDisconnectReason = -1;
volatile uint32_t disconnectCount = 0;
volatile uint32_t connectedCount = 0;
#endif

// STM32 Protocol Constants (AN3155)
#define STM32_ACK   0x79
#define STM32_NACK  0x1F
#define CMD_INIT    0x7F
#define CMD_GETID   0x02
#define CMD_ERASE   0x43
#define CMD_WRITE   0x31

#define FLASH_CHUNK 256

// Global State
bool flashingMode = false;
bool flashSuccess = false;
uint32_t flashAddress = 0x08000000;
uint8_t stmBuffer[FLASH_CHUNK];
uint16_t stmBufferIndex = 0;
size_t flashBytesReceived = 0;

// ==========================================
// STATUS LED
// ==========================================

static inline void ledSet(bool on) {
#ifdef LED_ACTIVE_LOW
    digitalWrite(PIN_LED, on ? LOW : HIGH);
#else
    digitalWrite(PIN_LED, on ? HIGH : LOW);
#endif
}

static inline void ledToggle() {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
}

// ==========================================
// STM32 HARDWARE CONTROL
// ==========================================

#include "ipc_peer.h"

void resetSTM32(bool enterBootloader) {
    // A flash operation and the protocol cannot share the port.
    if (ipcActive()) ipcEnd();

    digitalWrite(PIN_BOOT0, enterBootloader ? HIGH : LOW);
    delay(50);
    digitalWrite(PIN_RST, LOW);
    delay(100);
    digitalWrite(PIN_RST, HIGH);
    delay(enterBootloader ? 500 : 200);
}

// Start the target. Then set the link to the transparent bridge mode.
void returnToBridge() {
    resetSTM32(false);
    Serial1.end();
    Serial1.begin(BRIDGE_BAUD, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
    flashingMode = false;
}

// ==========================================
// STM32 PROTOCOL
// ==========================================

void sendByte(uint8_t b) {
    Serial1.write(b);
}

bool waitForACK() {
    unsigned long start = millis();
    while (millis() - start < 1000) {
        if (Serial1.available()) {
            uint8_t b = Serial1.read();
            if (b == STM32_ACK) return true;
            if (b == STM32_NACK) return false;
        }
    }
    return false;
}

bool stm32_sync() {
    // 1. Configure Serial1 for Bootloader (8E1)
    Serial1.end();
    Serial1.begin(BOOTLOADER_BAUD, SERIAL_8E1, STM_RX_PIN, STM_TX_PIN);
    while(Serial1.available()) Serial1.read(); // Clear junk

    // 2. Send Init Byte
    sendByte(CMD_INIT);
    return waitForACK();
}

int stm32_get_id() {
    sendByte(CMD_GETID);
    sendByte(0xFF ^ CMD_GETID);

    if (!waitForACK()) return -1;

    unsigned long start = millis();
    while (!Serial1.available()) { if(millis()-start > 500) return -2; }
    uint8_t n = Serial1.read(); // Number of bytes - 1

    uint16_t pid = 0;
    for (int i=0; i <= n; i++) {
        while (!Serial1.available()) { if(millis()-start > 500) return -3; }
        uint8_t val = Serial1.read();
        pid = (pid << 8) | val;
    }

    if (!waitForACK()) return -4;
    return pid;
}

bool stm32_erase() {
    sendByte(CMD_ERASE);
    sendByte(0xFF ^ CMD_ERASE);
    if (!waitForACK()) return false;
    sendByte(0xFF);
    sendByte(0x00);
    return waitForACK();
}

bool stm32_write_chunk(uint8_t* data, uint16_t len) {
    sendByte(CMD_WRITE);
    sendByte(0xFF ^ CMD_WRITE);
    if (!waitForACK()) return false;

    uint8_t checksum = 0;
    sendByte((flashAddress >> 24) & 0xFF); checksum ^= ((flashAddress >> 24) & 0xFF);
    sendByte((flashAddress >> 16) & 0xFF); checksum ^= ((flashAddress >> 16) & 0xFF);
    sendByte((flashAddress >> 8)  & 0xFF); checksum ^= ((flashAddress >> 8)  & 0xFF);
    sendByte((flashAddress >> 0)  & 0xFF); checksum ^= ((flashAddress >> 0)  & 0xFF);
    sendByte(checksum);
    if (!waitForACK()) return false;

    sendByte(len - 1);
    checksum = (len - 1);
    for (int i = 0; i < len; i++) {
        sendByte(data[i]);
        checksum ^= data[i];
    }
    sendByte(checksum);

    bool result = waitForACK();
    if (result) flashAddress += len;
    return result;
}

// ==========================================
// USB SERIAL CONSOLE
// ==========================================
// This is a command channel with lines on the USB port. It identifies and
// flashes the target without a network.
//
// Note: each reply starts with OK, ERR or INFO. Thus a script on the host can
// read the replies.
//
//   id            identify target (bootloader sync + GET ID), then run
//   run           reset target into normal run mode
//   boot          reset target into bootloader mode and stay there
//   flash <size>  erase, then stream <size> raw bytes (see below)
//   help
//
// The `flash` command uses this sequence. The device replies READY. Then the
// host sends a maximum of 256 bytes. The host waits for `ACK <total>`. Then
// the host sends the next block.
//
// Note: the host controls the rate. Thus the fast USB port does not send more
// data than the 57600 baud link accepts.

enum ConsoleState { CONSOLE_CMD, CONSOLE_BINARY };
ConsoleState consoleState = CONSOLE_CMD;
size_t binaryRemaining = 0;
size_t chunkRemaining = 0;

char cmdBuf[64];
uint8_t cmdLen = 0;

void printHelp() {
#ifdef HAS_WIFI
    Serial.println("INFO commands: id | run | boot | flash <size> | wifi | scan | ipc | help");
#else
    Serial.println("INFO commands: id | run | boot | flash <size> | ipc | help");
#endif
}

#ifdef HAS_WIFI
void cmdWifiStatus() {
    // Show the credentials from the build flags. Thus a person can find an
    // error in the quotation marks.
    Serial.printf("INFO configured ssid='%s' pass_len=%d\n",
                  WIFI_SSID, (int)strlen(WIFI_PASS));
    Serial.printf("INFO events: connected=%u disconnected=%u last_reason=%d\n",
                  (unsigned)connectedCount, (unsigned)disconnectCount,
                  lastDisconnectReason);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("OK connected ssid=%s rssi=%d ip=%s\n",
                      WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("ERR not connected (status=%d)\n", (int)WiFi.status());
    }
}

// Do a full connection attempt in the main task.
//
// Note: thus the console shows the full sequence. At start-up the attempt
// occurs before a host connects to the port.
// mode: "" default, "low" reduces TX power (tests supply sag during TX bursts),
// "bssid" locks the exact AP and channel found by a scan.
void cmdWifiConnect(const char *mode) {
    WiFi.disconnect(false, true);
    delay(300);
    disconnectCount = 0;
    connectedCount = 0;
    lastDisconnectReason = -1;

    WiFi.mode(WIFI_STA);

    if (strcmp(mode, "low") == 0) {
        WiFi.setTxPower(WIFI_POWER_8_5dBm);
    }
    if (strcmp(mode, "bg") == 0) {
        // Use the b and g modes only. Some access-point drivers cannot
        // connect an ESP32 that supports 11n.
        esp_wifi_set_protocol(WIFI_IF_STA,
                              WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
    }
    WiFi.setSleep(false);
    Serial.printf("INFO txpower=%d sta_mac=%s\n",
                  (int)WiFi.getTxPower(), WiFi.macAddress().c_str());

    wl_status_t rc;
    if (strcmp(mode, "bssid") == 0) {
        int n = WiFi.scanNetworks();
        int found = -1;
        for (int i = 0; i < n; i++)
            if (WiFi.SSID(i) == WIFI_SSID) { found = i; break; }
        if (found < 0) {
            Serial.println("ERR ssid not found in scan");
            WiFi.scanDelete();
            return;
        }
        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(found), 6);
        int ch = WiFi.channel(found);
        Serial.printf("INFO locking ch=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
                      ch, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        WiFi.scanDelete();
        rc = WiFi.begin(WIFI_SSID, WIFI_PASS, ch, bssid);
    } else {
        rc = WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    Serial.printf("INFO begin rc=%d mode=%d\n", (int)rc, (int)WiFi.getMode());

    // What the driver actually holds, and what it says when asked directly.
    wifi_config_t cfg = {};
    esp_err_t ge = esp_wifi_get_config(WIFI_IF_STA, &cfg);
    Serial.printf("INFO driver cfg rc=%d ssid='%s' pw_len=%d authmode=%d\n",
                  (int)ge, (const char *)cfg.sta.ssid,
                  (int)strlen((const char *)cfg.sta.password),
                  (int)cfg.sta.threshold.authmode);
    esp_err_t ce = esp_wifi_connect();
    Serial.printf("INFO esp_wifi_connect rc=%d\n", (int)ce);

    unsigned long start = millis();
    int last = -1;
    while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        int now = (int)WiFi.status();
        if (now != last) {
            Serial.printf("INFO status=%d conn=%u disc=%u reason=%d\n", now,
                          (unsigned)connectedCount, (unsigned)disconnectCount,
                          lastDisconnectReason);
            last = now;
        }
        if (now == WL_CONNECTED) {
            Serial.printf("OK connected ip=%s\n", WiFi.localIP().toString().c_str());
            return;
        }
        delay(250);
    }
    Serial.printf("ERR timeout conn=%u disc=%u last_reason=%d\n",
                  (unsigned)connectedCount, (unsigned)disconnectCount,
                  lastDisconnectReason);
}

// Scan for the available access points.
//
// Note: the ESP32-S3 uses the 2.4 GHz band only. Thus this board cannot always
// connect to an access point that the host finds.
void cmdWifiScan() {
    // Stop the connection attempts. A repeated attempt keeps the radio busy
    // and makes the scan fail. The function starts the attempts again at the
    // end.
    WiFi.disconnect();
    delay(200);

    int n = WiFi.scanNetworks();
    if (n < 0) {
        Serial.printf("ERR scan failed (%d)\n", n);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        return;
    }
    for (int i = 0; i < n; i++) {
        Serial.printf("INFO %-32s ch=%2d rssi=%4d enc=%d\n",
                      WiFi.SSID(i).c_str(), WiFi.channel(i),
                      WiFi.RSSI(i), (int)WiFi.encryptionType(i));
    }
    WiFi.scanDelete();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("OK %d networks\n", n);
}
#endif

void cmdIdentify() {
    flashingMode = true;
    resetSTM32(true);

    if (!stm32_sync()) {
        Serial.println("ERR sync failed - check wiring (TX/RX swapped?), power, BOOT0");
    } else {
        int pid = stm32_get_id();
        if (pid < 0) {
            Serial.printf("ERR get-id failed (%d)\n", pid);
        } else {
            Serial.printf("OK pid=0x%03X%s\n", pid,
                          pid == 0x418 ? " (STM32F105/107 Connectivity Line)" : "");
        }
    }

    returnToBridge();
}

void cmdFlashBegin(size_t size) {
    if (size == 0) {
        Serial.println("ERR size must be > 0");
        return;
    }

    flashingMode = true;
    flashSuccess = true;
    flashAddress = 0x08000000;
    stmBufferIndex = 0;
    flashBytesReceived = 0;

    resetSTM32(true);
    if (!stm32_sync()) {
        Serial.println("ERR sync failed");
        returnToBridge();
        return;
    }
    Serial.println("INFO sync ok, erasing");

    if (!stm32_erase()) {
        Serial.println("ERR erase failed");
        returnToBridge();
        return;
    }
    Serial.println("INFO erase ok");

    binaryRemaining = size;
    chunkRemaining = size < FLASH_CHUNK ? size : FLASH_CHUNK;
    consoleState = CONSOLE_BINARY;
    Serial.println("READY");
}

void handleCommand(char* line) {
    if (strcmp(line, "id") == 0) {
        cmdIdentify();
    } else if (strcmp(line, "run") == 0) {
        returnToBridge();
        Serial.println("OK target running");
    } else if (strcmp(line, "boot") == 0) {
        flashingMode = true;
        resetSTM32(true);
        Serial.println("OK target in bootloader");
    } else if (strncmp(line, "flash ", 6) == 0) {
        cmdFlashBegin((size_t)strtoul(line + 6, NULL, 10));
#ifdef HAS_WIFI
    } else if (strcmp(line, "wifi") == 0) {
        cmdWifiStatus();
    } else if (strcmp(line, "scan") == 0) {
        cmdWifiScan();
    } else if (strcmp(line, "connect") == 0) {
        cmdWifiConnect("");
    } else if (strncmp(line, "connect ", 8) == 0) {
        cmdWifiConnect(line + 8);
#endif
    } else if (strncmp(line, "ipc ", 4) == 0) {
        ipcCommand(line + 4);
    } else if (strcmp(line, "ipc") == 0) {
        ipcHelp();
    } else if (strcmp(line, "help") == 0 || line[0] == '\0') {
        printHelp();
    } else {
        Serial.printf("ERR unknown command '%s'\n", line);
    }
}

// Accept one byte of a `flash` transfer.
//
// Note: at the end of each block, the function writes the block to the STM32.
// It then sends ACK, and the host sends the next block.
void consumeBinaryByte(uint8_t b) {
    stmBuffer[stmBufferIndex++] = b;
    binaryRemaining--;
    chunkRemaining--;
    flashBytesReceived++;

    if (chunkRemaining > 0 && stmBufferIndex < FLASH_CHUNK) return;

    ledToggle();
    if (!stm32_write_chunk(stmBuffer, stmBufferIndex)) {
        flashSuccess = false;
        Serial.printf("ERR write failed at 0x%08X\n", flashAddress);
        consoleState = CONSOLE_CMD;
        binaryRemaining = 0;
        returnToBridge();
        return;
    }
    stmBufferIndex = 0;

    if (binaryRemaining == 0) {
        ledSet(true);
        Serial.printf("OK flashed %u bytes\n", (unsigned)flashBytesReceived);
        consoleState = CONSOLE_CMD;
        returnToBridge();
        return;
    }

    chunkRemaining = binaryRemaining < FLASH_CHUNK ? binaryRemaining : FLASH_CHUNK;
    Serial.printf("ACK %u\n", (unsigned)flashBytesReceived);
}

void pollConsole() {
    while (Serial.available()) {
        uint8_t b = Serial.read();

        if (consoleState == CONSOLE_BINARY) {
            consumeBinaryByte(b);
            continue;
        }

        if (b == '\n' || b == '\r') {
            if (cmdLen == 0) continue;
            cmdBuf[cmdLen] = '\0';
            cmdLen = 0;
            handleCommand(cmdBuf);
        } else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = (char)b;
        }
    }
}

// ==========================================
// WEB SERVER HANDLERS
// ==========================================

#ifdef HAS_WIFI

const char* html_page = R"(
<html>
<head>
<title>HAZK-03 Flasher</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    body { font-family: sans-serif; padding: 20px; max-width: 600px; margin: auto; background: #fafafa; }
    h1 { text-align: center; color: #333; }
    .card { background: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    .btn { display: block; width: 100%; padding: 12px; margin-top: 10px; border: none; cursor: pointer; font-size: 16px; border-radius: 4px; font-weight: bold; }
    .btn-check { background: #28a745; color: white; }
    .btn-flash { background: #007bff; color: white; }
    #status { margin-top: 15px; padding: 10px; background: #eee; border-radius: 4px; font-family: monospace; white-space: pre-wrap; min-height: 40px; }
    #progress { width: 100%; background-color: #ddd; border-radius: 4px; margin-top: 10px; display: none; }
    #bar { width: 0%; height: 20px; background-color: #007bff; border-radius: 4px; text-align: center; line-height: 20px; color: white; font-size: 12px; }
</style>
<script>
    function checkConnection() {
        document.getElementById('status').innerText = "Checking connection...";
        fetch('/identify').then(r => r.text()).then(t => {
            document.getElementById('status').innerText = t;
        });
    }
    function uploadFirmware() {
        var input = document.getElementById('file_input');
        if(input.files.length === 0){ alert("Select a file first!"); return; }
        var file = input.files[0];
        var formData = new FormData();
        formData.append("update", file);
        var xhr = new XMLHttpRequest();
        document.getElementById('progress').style.display = 'block';
        document.getElementById('status').innerText = "Uploading...";

        xhr.upload.addEventListener("progress", function(e) {
            if (e.lengthComputable) {
                var percent = Math.round((e.loaded / e.total) * 100);
                document.getElementById('bar').style.width = percent + "%";
                document.getElementById('bar').innerText = percent + "%";
            }
        }, false);

        xhr.onload = function() {
            document.getElementById('status').innerText = xhr.responseText;
            var color = (xhr.status == 200 && xhr.responseText.indexOf("OK") != -1) ? "#28a745" : "#dc3545";
            document.getElementById('bar').style.backgroundColor = color;
        };
        xhr.open("POST", "/update");
        xhr.send(formData);
    }
</script>
</head>
<body>
    <h1>HAZK-03 Flasher v. )" VERSION R"(</h1>

    <div class="card">
        <h3>1. Diagnostics</h3>
        <p>Verify wiring and bootloader mode.</p>
        <button class="btn btn-check" onclick=checkConnection()>Identify Target</button>
        <div id="status">Ready.</div>
    </div>

    <div class="card">
        <h3>2. Firmware Update</h3>
        <p>Upload 'firmware.bin' to flash.</p>
        <input type='file' id='file_input' name='update' style="margin-bottom: 10px;">
        <button class='btn btn-flash' onclick=uploadFirmware()>Flash Firmware</button>
        <div id="progress"><div id="bar">0%</div></div>
    </div>
</body>
</html>
)";

void handleRoot() {
    server.send(200, "text/html", html_page);
}

void handleIdentify() {
    flashingMode = true;
    resetSTM32(true);

    if (!stm32_sync()) {
        server.send(200, "text/plain", "Error: Sync Failed.\n- Check wiring (TX/RX flipped?)\n- Check Power");
    } else {
        int pid = stm32_get_id();
        String msg = "Success! Chip Detected.\nPID: 0x" + String(pid, HEX);
        if (pid == 0x418) msg += " (STM32F105/107 Connectivity Line)";
        server.send(200, "text/plain", msg);
    }

    returnToBridge();
}

/* Set the speed of the bridge without a new flash operation.
 *
 * Note: a target with an incorrect clock sends at an incorrect rate. A change
 * of this value thus finds the true core frequency. If the output is readable
 * at the rate R, the true clock is R divided by the configured rate, times the
 * clock that the firmware expects.
 */
void handleBaud() {
    if (!server.hasArg("b")) {
        server.send(400, "text/plain", "ERR missing b");
        return;
    }

    long b = server.arg("b").toInt();
    if (b < 300 || b > 2000000) {
        server.send(400, "text/plain", "ERR baud out of range");
        return;
    }

    Serial1.end();
    Serial1.begin(b, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
    server.send(200, "text/plain", String("OK bridge baud ") + b);
}

/* Measure the STM32 link for a given time. Report the data rate and the
 * quality of the bytes.
 *
 * Note: this function counts the bytes locally. Thus the measurement covers
 * the UART and the cable. It does not cover the WiFi path.
 *
 * Note: the target sends text. Thus each byte outside of the printable ASCII
 * range is an error. A frame error or a lost bit gives such a byte. Correct
 * output does not.
 */
void handleUartTest() {
    int secs = server.hasArg("secs") ? server.arg("secs").toInt() : 3;
    if (secs < 1 || secs > 20) {
        secs = 3;
    }

    flashingMode = true;  // Stop the bridge during the measurement.
    while (Serial1.available()) {
        Serial1.read();
    }

    unsigned long total = 0;
    unsigned long bad = 0;
    unsigned long deadline = millis() + (unsigned long)secs * 1000UL;

    while ((long)(millis() - deadline) < 0) {
        while (Serial1.available()) {
            uint8_t c = (uint8_t)Serial1.read();
            total++;
            bool ok = (c >= 0x20 && c <= 0x7e) || c == '\r' || c == '\n';
            if (!ok) {
                bad++;
            }
        }
    }

    flashingMode = false;

    String out = String("bytes=") + total +
                 " bad=" + bad +
                 " bytes_per_s=" + (total / (unsigned long)secs) +
                 " line_rate_bps=" + (total * 10UL / (unsigned long)secs);
    server.send(200, "text/plain", out);
}

void handleReset() {
    resetSTM32(false);
    server.send(200, "text/plain", "OK target reset");
}

// Run a peer command that arrives in the query string, and give the output
// back as text. Thus the protocol runs without a USB connection.
void handleIpc() {
    if (!server.hasArg("cmd")) {
        server.send(400, "text/plain", "ERR missing cmd\n");
        return;
    }

    server.send(200, "text/plain", ipcRun(server.arg("cmd").c_str()));
}

void handleUpdateResult() {
    Serial.println(flashSuccess ? "FLASH COMPLETE" : "FLASH FAILED");
    returnToBridge();
    server.send(200, "text/plain", flashSuccess ? "OK: Flashed & Rebooted" : "FAIL: Upload Error");
}

void handleUpdateUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        flashingMode = true;
        flashSuccess = true;
        flashAddress = 0x08000000;
        stmBufferIndex = 0;
        flashBytesReceived = 0;

        Serial.println(">> STARTING FLASH");
        resetSTM32(true);
        if (!stm32_sync()) {
            Serial.println("SYNC FAIL");
            flashSuccess = false;
            return;
        }
        Serial.println("SYNC OK. Erasing...");
        if (!stm32_erase()) {
            Serial.println("ERASE FAIL");
            flashSuccess = false;
            return;
        }
        Serial.println("ERASE OK. Writing...");

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!flashSuccess) return;

        ledToggle();
        flashBytesReceived += upload.currentSize;
        if (upload.totalSize > 0) {
            Serial.printf("Progress: %u%%\r", (flashBytesReceived * 100) / upload.totalSize);
        }

        for (size_t i = 0; i < upload.currentSize; i++) {
            stmBuffer[stmBufferIndex++] = upload.buf[i];
            if (stmBufferIndex == FLASH_CHUNK) {
                if (!stm32_write_chunk(stmBuffer, FLASH_CHUNK)) flashSuccess = false;
                stmBufferIndex = 0;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        ledSet(true);
        if (flashSuccess && stmBufferIndex > 0) {
            if (!stm32_write_chunk(stmBuffer, stmBufferIndex)) flashSuccess = false;
        }
        Serial.printf("\nDONE. Size: %u\n", upload.totalSize);
    }
}

void setupWiFi() {
    // Wait for a limited time. An absent access point must not stop the USB
    // console.
    //
    // Note: the WiFi.begin() function continues the attempts in the
    // background. Thus the network becomes available later.
    // The reason code shows the difference between an absent access point
    // and an incorrect key.
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        lastDisconnectReason = info.wifi_sta_disconnected.reason;
        disconnectCount++;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        connectedCount++;
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("INFO connecting to WiFi");
    unsigned long start = millis();
    wl_status_t last = (wl_status_t)-1;
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        wl_status_t now = WiFi.status();
        if (now != last) {
            Serial.printf("INFO wifi status=%d\n", (int)now);
            last = now;
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nINFO WiFi not connected - serial console only");
        return;
    }
    // Disable the power-save mode. That mode adds a delay of some hundred ms
    // to each request. This link carries interactive flash operations.
    WiFi.setSleep(false);
    Serial.println("\nINFO WiFi connected");
    ser2netServer.begin();

    ArduinoOTA.setHostname("hazk-flasher");
    ArduinoOTA.onStart([]() {
        flashingMode = true; // Stop bridge during update
        server.stop(); // Stop web server during update
        Serial.println("OTA Start");
    });
    ArduinoOTA.onEnd([]() {
        flashingMode = false;
        server.begin(); // Restart web server after update
        Serial.println("\nOTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total);
        ledToggle();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("\nError: ");
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        else Serial.printf("Unknown Error: %u\n", error);
    });
    ArduinoOTA.begin();

    server.on("/", handleRoot);
    server.on("/identify", handleIdentify);
    server.on("/baud", handleBaud);
    server.on("/uarttest", handleUartTest);
    server.on("/reset", handleReset);
    server.on("/ipc", handleIpc);
    server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
    server.begin();

    netUp = true;
    Serial.print("INFO web interface: http://"); Serial.println(WiFi.localIP());
}

#endif // HAS_WIFI

// ==========================================
// MAIN LOOP
// ==========================================

void setup() {
    Serial.begin(DEBUG_BAUD);
    // Use a buffer of 4 KB. At 1 Mbaud the loop accepts near 100 KB each
    // second. The default buffer of 256 bytes is too small.
    Serial1.setRxBufferSize(4096);
    Serial1.begin(BRIDGE_BAUD, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);

    pinMode(PIN_BOOT0, OUTPUT);
#ifdef NRST_OPEN_DRAIN
    // Release-high: the STM32's internal NRST pull-up sets the idle level, so
    // the flasher never fights an external reset source.
    pinMode(PIN_RST, OUTPUT_OPEN_DRAIN);
#else
    pinMode(PIN_RST, OUTPUT);
#endif
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_BOOT0, LOW);
    digitalWrite(PIN_RST, HIGH);
    ledSet(true);

#ifdef HAS_WIFI
    setupWiFi();
#endif

    Serial.println("\nINFO hazk-flasher " VERSION " ready");
    printHelp();
}

void loop() {
#ifdef HAS_WIFI
    if (netUp) {
        ArduinoOTA.handle();
        server.handleClient();

        // Handle new TCP connections
        if (ser2netServer.hasClient()) {
            if (tcpClient) tcpClient.stop();
            tcpClient = ser2netServer.available();
            if (tcpClient && tcpClient.connected())
                tcpClient.write("Welcome to HAZK flasher. Serial output of target:\n", 51);
        }
    }
#endif

    pollConsole();

    // The peer owns the port while it is active. Thus the raw bridge stays
    // out of the frames of the protocol.
    if (ipcActive()) {
        ipcPoll();
        return;
    }

    if (!flashingMode) {
        // STM32 -> USB & Network
        if (Serial1.available()) {
            uint8_t buf[128];
            int avail = Serial1.available();
            if (avail > 128) avail = 128;
            size_t len = Serial1.readBytes(buf, avail);
            Serial.write(buf, len);
#ifdef HAS_WIFI
            if (tcpClient && tcpClient.connected()) tcpClient.write(buf, len);
#endif
            ledToggle();
        }
#ifdef HAS_WIFI
        // Network -> STM32
        while (tcpClient && tcpClient.connected() && tcpClient.available()) {
            Serial1.write(tcpClient.read());
        }
#endif
    }
}
