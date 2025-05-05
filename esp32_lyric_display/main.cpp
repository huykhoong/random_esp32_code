#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <vector>        
#include "Dongle_20.h"
#include "Dongle_14.h"
#include "Dongle_16.h"
#include "loading.h"

#define SDA_PIN 21
#define SCL_PIN 22
#define RST_PIN U8X8_PIN_NONE
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, RST_PIN, SCL_PIN, SDA_PIN);

#define BTN_UP     12
#define BTN_DOWN   13
#define BTN_SELECT 14

const char* ssid     = "Huy va Nhi";
const char* password = "matkhau123";

WebServer server(80);
std::vector<String> lyrics;
int currentIndex = 0;
uint8_t newMACAddress[] = {0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66};
volatile bool lyricReceived = false;
TaskHandle_t gifTaskHandle = NULL;

void playGIF(const AnimatedGIF* gif, uint16_t loopCount = 1) {
  if (loopCount == 0) {
    while (true) {
      for (uint8_t frame = 0; frame < gif->frame_count; frame++) {
        u8g2.clearBuffer();
        for (uint16_t y = 0; y < gif->height; y++) {
          for (uint16_t x = 0; x < gif->width; x++) {
            uint16_t byteIndex = y * (((gif->width + 7) / 8)) + (x / 8);
            uint8_t  bitIndex  = 7 - (x % 8);
            if (gif->frames[frame][byteIndex] & (1 << bitIndex)) {
              u8g2.drawPixel(x, y);
            }
          }
        }
        u8g2.sendBuffer();
        delay(gif->delays[frame]);
      }
    }
  }
  for (uint16_t loop = 0; loop < loopCount; loop++) {
    for (uint8_t frame = 0; frame < gif->frame_count; frame++) {
      // Xóa buffer trước khi vẽ
      u8g2.clearBuffer();

      // Vẽ từng pixel
      for (uint16_t y = 0; y < gif->height; y++) {
        for (uint16_t x = 0; x < gif->width; x++) {
          uint16_t byteIndex = y * (((gif->width + 7) / 8)) + (x / 8);
          uint8_t  bitIndex  = 7 - (x % 8);
          if (gif->frames[frame][byteIndex] & (1 << bitIndex)) {
            u8g2.drawPixel(x, y);
          }
        }
      }

      // Đẩy buffer ra màn hình
      u8g2.sendBuffer();

      // Chờ theo delay của khung
      delay(gif->delays[frame]);
    }
  }
}  

// vẽ text với wrap tại word-boundary, không split UTF-8
void drawWrapped(const String &text, int x, int y, int maxWidth, int lineHeight) {
  int len = text.length();
  // 1) tách text thành các “words” (giữ lại dấu cách ở cuối)
  std::vector<String> words;
  int idx = 0;
  while (idx < len) {
    int nextSpace = text.indexOf(' ', idx);
    if (nextSpace == -1) nextSpace = len;
    // chuỗi từ idx đến nextSpace (chưa bao gồm space)
    String w = text.substring(idx, nextSpace);
    // nếu chưa phải cuối, thêm dấu space để hiển thị đúng
    if (nextSpace < len) w += ' ';
    words.push_back(w);
    idx = nextSpace + 1;
  }

  // 2) gom từng word vào dòng, nếu vượt width thì xuống dòng mới
  int cursorY = y;
  String line = "";
  for (auto &w : words) {
    // thử nếu cộng thêm word này vẫn fit
    if (u8g2.getUTF8Width((line + w).c_str()) <= maxWidth) {
      line += w;
    } else {
      // vẽ dòng hiện tại
      u8g2.setCursor(x, cursorY);
      u8g2.print(line);
      // sang dòng mới
      cursorY += lineHeight;
      line = w;
    }
  }
  // vẽ dòng cuối (nếu còn)
  if (line.length() && cursorY <= u8g2.getDisplayHeight()) {
    u8g2.setCursor(x, cursorY);
    u8g2.print(line);
  }
}


void displayLyric() {
  u8g2.clearBuffer();
  u8g2.enableUTF8Print();
  u8g2.setFont(Dongle_20);
  u8g2.setCursor(0, 20);
  if (lyrics.empty()) {
    u8g2.print("No lyrics");
  } else {
    String &line = lyrics[currentIndex];
    drawWrapped(line, 0, 10, 128, 16);
  }
  u8g2.sendBuffer();
}

void handleButtons() {

  static uint32_t last=0;
  if (millis()-last<200) return;
  if (digitalRead(BTN_UP)==LOW && !lyrics.empty()) {
    currentIndex = max(0, currentIndex-1);
    displayLyric(); last=millis();
  }
  if (digitalRead(BTN_DOWN)==LOW && !lyrics.empty()) {
    currentIndex = min((int)lyrics.size()-1, currentIndex+1);
    displayLyric(); last=millis();
  }
  if (digitalRead(BTN_SELECT)==LOW && !lyrics.empty()) {
    currentIndex = lyrics.size()-1;
    displayLyric(); last=millis();
  }
}

void gifTask(void* pvParameters);

// Task chạy GIF loading cho tới khi lyric về
void gifTask(void* pvParameters) {
  while (!lyricReceived) {
    playGIF(&loading_gif, 1);  // render 1 loop của GIF
    // Trả quyền cho RTOS
    vTaskDelay(1);
  }
  // Khi có lyric, xóa chính task này
  vTaskDelete(NULL);
}

void onReceiveLyricPlain() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }
  String txt = server.arg("plain");
  if (txt.length() == 0) {
    server.send(400, "text/plain", "no text");
    return;
  }
  // Lưu lyric and remove old nếu vượt quá 50
  lyrics.push_back(txt);
  if (lyrics.size() > 50) lyrics.erase(lyrics.begin());
  currentIndex = lyrics.size() - 1;
  // Báo đã nhận lyric
  lyricReceived = true;
  // Hiển thị lyric ngay
  displayLyric();
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(115200);
  
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, &newMACAddress[0]);
  if (err == ESP_OK) {
    Serial.println("Success changing Mac Address");
  }


  u8g2.begin();
  displayLyric();

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // Chọn một font nhỏ
  u8g2.setCursor(0, 10);
  u8g2.print("WiFi...");
  u8g2.sendBuffer(); // Gửi buffer ban đầu ra màn hình

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    u8g2.print('.'); // In thêm dấu chấm lên màn hình
    u8g2.sendBuffer(); // Cập nhật màn hình với dấu chấm mới
  }
  Serial.println(" connected!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // Thiết lập handler và khởi động HTTP server
  server.on("/lyrics", HTTP_POST, onReceiveLyricPlain);
  server.begin();
  Serial.println("HTTP server started");

  // Tạo task chạy GIF trên core 1
  xTaskCreatePinnedToCore(
    gifTask,
    "GIF Player",
    4096,
    NULL,
    1,
    &gifTaskHandle,
    1
  );
}

void loop() {
  server.handleClient();
  // Khi đã nhận lyric, mới xử lý nút và hiển thị tiếp
  if (lyricReceived) {
    handleButtons();
  }
}
