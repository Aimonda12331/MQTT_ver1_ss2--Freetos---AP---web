#include <Arduino.h>
#include <vector>
#include <string.h>

#include <WiFi.h>
#include <WiFiAP.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h> // thư viện bộ nhớ flash
#include <WebServer.h> 

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

#define EEPROM_SIZE 128 // Đảm bảo đủ dung lượng để lưu SSID và mật khẩu
#define LED_PIN 26 // Định nghĩa chân LEDD
#define BUTTON_PIN 0 // Định nghĩa nút nhấn ở G22PIO 0
#define RESET_HOLD_TIME 5000 // Thời gian giữ nút nhấn (5 giây)

bool wifiConfigured = false; // Cờ kiểm tra xem đã cấu hình WiFi chưa

  unsigned long lastPingTime = 0; // Biến lưu thời gian gửi gói ping cuối cùng
  bool ledhientai = false;

  const unsigned long cronInterval = 1000;  // Kiểm tra mỗi 100 ms
  unsigned long thoigianBatLED = 0;  // Lưu thời gian bật LED để tắt led sau 5 giây

// Khai báo các thông tin MQTT
  const char* mqtt_server = "product.skytechnology.vn";
  const int mqtt_port = 7855;
  const char* mqtt_user = "8375ffcf-f704-4717-a96c-69b42b602b0d"; //thingid
  const char* mqtt_pass = "2b51fdbd-7f35-48c9-ab76-08e025d3530f"; //thingkey
  const char* led_topic = "channels/2d4e0ea3-086d-4ea5-a9e7-c2b92e7418d7/messages"; //id
 
  const char* notify_id = "2b51fdbd-7f35-48c9-ab76-08e025d3530f"; // Thay bằng notify_id thực tế
  const char* thing_id = "8375ffcf-f704-4717-a96c-69b42b602b0d"; // Thay bằng ID thiết bị thực tế

  WiFiClient espClient;
  PubSubClient client(espClient);

  // Tên và mật khẩu Access Point
const char *apSSID = "VCV Arlam Box";
const char *apPassword = "vcv@12345";

// Biến lưu thông tin Wi-Fi
String ssid = "";
String password = "";

// Tạo server web
WebServer server(80);

//Lưu WiFi
  void saveWiFiCredentials(const char* ssid, const char* password) {
    EEPROM.writeString(0, ssid);
    EEPROM.writeString(50, password);
    EEPROM.commit();
      // Serial.println("WiFi credentials saved in EEPROM.");
}

//Tải WiFi đã lưu để dùng
  void loadWiFiCredentials(char* ssid, char* password) {
    String storedSSID = EEPROM.readString(0);
    String storedPassword = EEPROM.readString(50);

      if (storedSSID.length() > 0 && storedPassword.length() > 0) {
    strcpy(ssid, storedSSID.c_str());
    strcpy(password, storedPassword.c_str());
  }
}

  void resetAPSmartconfig() {
      Serial.println("Đang xóa thông tin WiFi...");
// Xóa SSID và Password trong EEPROM
    EEPROM.writeString(0, "");
    EEPROM.writeString(50, "");
    EEPROM.commit();

      // Serial.println("Thông tin WiFi đã bị xóa. Khởi động lại ESP32...");
        delay(1000);
    ESP.restart(); // Khởi động lại thiết bị để vào chế độ SmartConfig
}

  void toggleLED(int times, int delayTime) {// đảo led này dùng cho cho nhiều chức năng như kết nối wifi hay reset wifi
    for (int i = 0; i < times; i++) {
      digitalWrite(LED_PIN, HIGH);
        delay(delayTime); // LED sáng
      digitalWrite(LED_PIN, LOW);
        delay(delayTime); // LED tắt
  }
}

// Task FreeRTOS để tự động tắt LED sau 5 giây
  void turnOffLedTask(void *parameter) {
    vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi 5 giây
    digitalWrite(LED_PIN, LOW); // Tắt LED
      // Serial.println("LED đã tắt sau 5 giây!");
    vTaskDelete(NULL); // Xóa task để tiết kiệm tài nguyên
}

  void checkResetButtonTask(void *parameter) {
  static unsigned long buttonPressTime = 0;
  static unsigned long lastBlinkTime = 0;
  static bool isButtonHeld = false;

  while (1) {
      if (digitalRead(BUTTON_PIN) == LOW) { // Nếu nút được nhấn
          if (!isButtonHeld) {
              buttonPressTime = millis(); // Lưu thời gian nhấn nút
              isButtonHeld = true; // Đánh dấu trạng thái đang giữ nút
          }

          // Nhấp nháy LED khi đang giữ nút
          if (millis() - lastBlinkTime >= 300) {
              digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Đảo trạng thái LED
              lastBlinkTime = millis();
          }

          // Nếu giữ nút >= RESET_HOLD_TIME (5 giây), reset SmartConfig
          if (millis() - buttonPressTime >= RESET_HOLD_TIME) {
              toggleLED(6, 100); // Nhấp nháy LED 7 lần nhanh
              resetAPSmartconfig(); // Reset SmartConfig
              isButtonHeld = false; // Reset trạng thái
          }
      } else {
          if (isButtonHeld) {
              isButtonHeld = false; // Đánh dấu nút đã được thả
              digitalWrite(LED_PIN, ledhientai ? HIGH : LOW); // Khôi phục trạng thái LED trước đó
          }
      }
      vTaskDelay(10 / portTICK_PERIOD_MS); // Giảm tải CPU, kiểm tra nút mỗi 10ms
  }
}

// Kết nối Wi-Fi
  void setup_wifi() {
// Serial.print("Đang kết nối WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    // delay(500);
// Serial.print(".");
  }
// Serial.println("\nĐã kết nối WiFi.");
}

void reconnect() { // tái kết nối lại với MQTT
  while (!client.connected()) {
    // Serial.print("Đang kết nối MQTT...");
  if(client.connected()) return;
  if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
    Serial.println("Đã kết nối MQTT.");
  client.subscribe(led_topic);
} else {
  Serial.printf("Kết nối thất bại. Mã lỗi: %d\n", client.state());
  delay(5000);
    }
  }
}

// Xử lý sự kiện AccessPoint
void handleRoot() {
  // Trang HTML hiển thị form nhập Wi-Fi
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Wi-Fi Configuration</title>
      <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
        input { margin: 10px; padding: 10px; width: 80%; font-size: 16px; }
        button { padding: 10px 20px; font-size: 16px; cursor: pointer; }
      </style>
    </head>
    
    <body>
      <h1>VCV Alarm Box</h1>
      <form action="/submit" method="POST">
        <input type="text" name="ssid" placeholder="Enter Wi-Fi Name (SSID)" required><br>
        <input type="password" name="password" placeholder="Enter Wi-Fi Password" required><br>
        <button type="submit">Connect</button>
      </form>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleSubmit() {
  ssid = server.arg("ssid");
  password = server.arg("password");

  // Gửi phản hồi cho người dùng
  String response = "<h1>WiFI connected</h1>";
  server.send(200, "text/html", response);

  // Lưu thông tin
  saveWiFiCredentials(ssid.c_str(), password.c_str());

  // Tắt AP, chuyển sang STA
  delay(1000);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.println("Đang kết nối tới Wi-Fi mới...");

  // Chờ kết nối
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    // delay(500);
    // Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Serial.println("\nKết nối thành công! IP: " + WiFi.localIP().toString());
    ESP.restart(); // Khởi động lại dùng WiFi mới
  } else {
    // Serial.println("\nKết nối thất bại. Giữ nguyên chế độ AP.");
    WiFi.softAP(apSSID, apPassword); // Bật lại AP
  }
}

// Task gửi ping MQTT định kỳ
void pingMQTTTask(void *parameter) {
  while (1) {
      // Tạo ID ngẫu nhiên cho gói tin
      char randomID[12];
      snprintf(randomID, sizeof(randomID), "%lu", millis());

      // Tạo JSON message
      DynamicJsonDocument doc(256);
      doc["id"] = randomID;
      doc["type"] = "CP01";
      doc["data"]["id"] = thing_id;
      doc["data"]["action"] = "Update";
      doc["data"]["source"] = "device";

      char buffer[256];
      serializeJson(doc, buffer);

      // Gửi ping qua MQTT
      String topic = "channels/" + String(notify_id) + "/messages";
      if (client.publish(topic.c_str(), buffer)) {
          Serial.printf("Gửi ping MQTT thành công: %s\n", buffer);
      } else {
          Serial.println("Gửi ping thất bại!");
      }
      vTaskDelay(pdMS_TO_TICKS(30000)); // Đợi 30 giây trước khi gửi lại
  }
}

void PublishACK(DynamicJsonDocument doc, String type, String id, String Cron, String name) {// gọi biến id đã khai báo
  if (!client.connected()) {  
    reconnect(); // Đảm bảo kết nối MQTT
  }
  unsigned long timestamp = millis(); 

  bool ledStatus = digitalRead(LED_PIN) == HIGH ? true : false;
//tạo 1 biến type và ghi nó vào để gọi và để lựa 
  if(type == "DA01"){
    doc["id"] = id;
    doc["type"] = "DA05";
    doc["data"]["id"] = "2d4e0ea3-086d-4ea5-a9e7-c2b92e7418d7"; // ID cố định
    doc["data"]["action"] = "update";
    doc["data"]["source"] = "Device";
    doc["data"]["data"]["name"] = name;
    doc["data"]["data"]["id"] = "0";
    doc["data"]["data"]["status"] = ledStatus;
    doc["data"]["data"]["_type"] = "DA01";
}
  else if( type == "DA02"){
    doc["id"] = id;
    doc["type"] = "DA05";
    doc["data"]["source"] = "Device";
    doc["data"]["data"]["id"] = "2d4e0ea3-086d-4ea5-a9e7-c2b92e7418d7"; // ID cố định
    doc["data"]["data"]["index"] = doc["data"]["data"]["index"]; 
}

char buffer[1024];
serializeJson(doc, buffer);

// Tạo topic MQTT
String topic = "channels/2d4e0ea3-086d-4ea5-a9e7-c2b92e7418d7/messages";

// Gửi dữ liệu qua MQTT
if (client.publish(topic.c_str(), buffer)) {
// Serial.printf("Đã gửi PING: %s\n", buffer);
} else {
// Serial.println("Gửi PING thất bại!");
}
}

// Xử lý dữ liệu nhận được từ MQTT
void callbackHandlePackage(char* topic, byte* payload, unsigned int length) {
    // Serial.printf("Nhận dữ liệu từ topic: %s\n", topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
  message += (char) payload[i];
}

// Serial.printf("Payload: %s\n", message.c_str());

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, message.c_str());

  if (error) {
  // Serial.printf("Lỗi phân tích JSON: %s\n", error.f_str());
  return;
}
  String type = doc["type"].as<String>(); //Lưu type DA01 và DA02 vào type
  String cron = doc["data"]["data"]["cron"].as<String>();
  String action = doc["data"]["action"].as<String>();
  String name = doc["data"]["data"]["name"].as<String>();

  if(doc["type"] == "DA02") {//xử lý 2 loại gói tin với việc dùng biến type để phân loại nếu callback có DA02 hay DA01 
  String id = doc["id"].as<String>(); // Lấy id từ tin nhắn
  String action = doc["data"]["action"].as<String>(); // Lấy action
  String dataId = doc["data"]["data"]["id"]; // Lấy data id
  String index = doc["data"]["data"]["index"].as<String>(); // Lấy index
  String cron = doc["data"]["data"]["cron"].as<String>(); // Lấy cron
  bool status = doc["data"]["data"]["status"]; // Lấy status
  bool repeat = doc["data"]["data"]["repeat"]; // Lấy repeat
  bool notify = doc["data"]["data"]["notify"]; // Lấy notify
  String _type = doc["data"]["data"]["_type"].as<String>(); // Lấy _type
  long dateTime = doc["data"]["data"]["dateTime"]; // Lấy dateTime

// Serial.printf("DA02 - ID: %s | Action: %s | Data ID: %s | Index: %s | Cron: %s | Status: %s | Repeat: %s | Notify: %s | Type: %s | DateTime: %ld\n",
// id.c_str(), action.c_str(), dataId.c_str(), index.c_str(), cron.c_str(),
// status ? "TRUE" : "FALSE", repeat ? "TRUE" : "FALSE", notify ? "TRUE" : "FALSE",
// _type.c_str(), dateTime);

  String indexDelete = doc["data"]["data"]["index"].as<String>();
      PublishACK(doc,type,id,cron,name); // publish theo doc vì đã khai báo ở payload không cần phải khai báo từng phần tửtử

}else if(doc["type"] == "DA01") {// lúc này sẽ có 2 gói CP01 và DA01 gửi về nếu đúng là DA01 thì mơi chạy nếu sai(CP01) thì out 

  String id = doc["id"].as<String>();// Lấy id từ tin nhắn và gửi lên lại bằng cách gán id nhận được vào biến (id)
  String name = doc["data"]["data"]["name"];
  bool status = doc["data"]["data"]["status"];
  long dateTime = doc["data"]["data"]["dateTime"];

// Serial.printf("DA01 - ID: %s | Name: %s | Status: %s | DateTime: %ld\n",
// id.c_str(), name.c_str(),
// status ? "TRUE" : "FALSE", dateTime);
if(status != ledhientai){
// Điều khiển LED dựa trên trạng thái
digitalWrite(LED_PIN, status ? HIGH : LOW);
// Serial.println(status ? "LED Bật" : "LED Tắt");
ledhientai = status;
PublishACK(doc, type, id, cron, name);
}
} 
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  char ssid[33] = "";
  char password[65] = "";
  loadWiFiCredentials(ssid, password);

  Serial.printf("Đọc từ EEPROM - SSID: %s, Password: %s\n", ssid, password);

  if (strlen(ssid) == 0 || strlen(password) == 0) {
    // ❌ Chưa có WiFi → bật AP để người dùng nhập
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    // Serial.println("Access Point Started");
    // Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/submit", HTTP_POST, handleSubmit);
    server.begin();
    // Serial.println("HTTP server started");
  } else {
    // ✅ Có WiFi → thử kết nối
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Serial.print("Đang kết nối WiFi");
    unsigned long startConnectTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startConnectTime < 15000) {
      // delay(500);
      // Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      // Serial.println("\n✅ Kết nối WiFi thành công! IP: " + WiFi.localIP().toString());
      wifiConfigured = true;
    } else {
      // Serial.println("\n❌ Kết nối WiFi thất bại! Chuyển về chế độ AP để cấu hình lại.");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apSSID, apPassword);
      // Serial.println("Access Point Started");
      // Serial.println(WiFi.softAPIP());

      server.on("/", handleRoot);
      server.on("/submit", HTTP_POST, handleSubmit);
      server.begin();
      // Serial.println("HTTP server started (AP mode).");
    }
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(512);
  client.setCallback(callbackHandlePackage);

  xTaskCreate(pingMQTTTask, "Ping MQTT Task", 4096, NULL, 1, NULL);
  xTaskCreate(checkResetButtonTask, "Check Reset Button", 2048, NULL, 1, NULL);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConfigured) {
      wifiConfigured = true;
      // Serial.println("WiFi connected, switching to operational mode.");
    }
    setup_wifi();
    reconnect();
    client.loop();
  }
  server.handleClient();
}




