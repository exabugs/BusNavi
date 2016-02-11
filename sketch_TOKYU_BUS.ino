#include <Esp.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// アプリ状態
struct APPSTATUS {
  int pos;
};

// モード切り替えピン
const int MODE_PIN = 0; // GPIO0

// Wi-Fi設定保存ファイル
const char* settings = "/wifi_settings.txt";

// サーバモードでのパスワード
const String pass = "thereisnospoon";

// モータ制御ピン
const int PIN[] = {14, 12, 13, 5};

// 2相励磁 (4096ステップで2回転)
const int S = 4;
const int steps[S] = {
  0b1100,
  0b0110,
  0b0011,
  0b1001
};



ESP8266WebServer server(80);

/**
 * アプリ設定
 */
 struct APPCONFIG {
   String ssid; // WiFi SSID
   String pass; // WiFi Password
   String HOST; // WEBAPI
   String PATH; // WEBAPI
   String DSMK; // 乗車バス停
   String ASMK; // 降車バス停
};

void getAppConfig(APPCONFIG& config) {
  File f = SPIFFS.open(settings, "r");
  config.ssid = f.readStringUntil('\n');
  config.pass = f.readStringUntil('\n');
  config.HOST = f.readStringUntil('\n');
  config.PATH = f.readStringUntil('\n');
  config.DSMK = f.readStringUntil('\n');
  config.ASMK = f.readStringUntil('\n');
  f.close();

  config.ssid.trim();
  config.pass.trim();
  config.HOST.trim();
  config.PATH.trim();
  config.DSMK.trim();
  config.ASMK.trim();

  Serial.println("SSID: " + config.ssid);
  Serial.println("PASS: " + config.pass);
  Serial.println("HOST: " + config.HOST);
  Serial.println("PATH: " + config.PATH);
  Serial.println("DSMK: " + config.DSMK);
  Serial.println("ASMK: " + config.ASMK);
}

void handleRootGet() {
  APPCONFIG config;
  getAppConfig(config);

  String html = "";
  html += "<h1>WiFi Settings</h1>";
  html += "<form method='post'>";
  html += " <table>";
  html += "  <tr><td>ssid</td><td><input type='text' name='ssid' placeholder='ssid' value='" + config.ssid + "'></td></tr>";
  html += "  <tr><td>pass</td><td><input type='text' name='pass' placeholder='pass' value='" + config.pass + "'></td></tr>";
  html += "  <tr><td>HOST</td><td><input type='text' name='HOST' placeholder='HOST' value='" + config.HOST + "'></td></tr>";
  html += "  <tr><td>PATH</td><td><input type='text' name='PATH' placeholder='PATH' value='" + config.PATH + "'></td></tr>";
  html += "  <tr><td>DSMK</td><td><input type='text' name='DSMK' placeholder='DSMK' value='" + config.DSMK + "'></td></tr>";
  html += "  <tr><td>ASMK</td><td><input type='text' name='ASMK' placeholder='ASMK' value='" + config.ASMK + "'></td></tr>";
  html += " </table>";
  html += " <input type='submit'>";
  html += "</form>";
  server.send(200, "text/html", html);
}

void handleRootPost() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String HOST = server.arg("HOST");
  String PATH = server.arg("PATH");
  String DSMK = server.arg("DSMK");
  String ASMK = server.arg("ASMK");

  File f = SPIFFS.open(settings, "w");
  f.println(ssid);
  f.println(pass);
  f.println(HOST);
  f.println(PATH);
  f.println(DSMK);
  f.println(ASMK);
  f.close();

  String html = "";
  html += "<h1>WiFi Settings</h1>";
  html += ssid + "<br>";
  html += pass + "<br>";
  html += HOST + "<br>";
  html += PATH + "<br>";
  html += DSMK + "<br>";
  html += ASMK + "<br>";
  server.send(200, "text/html", html);
}

// モータ制御
void out(int x) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN[i], (x >> i) & 1 ? HIGH : LOW);
  }
}

// リクエスト送信
String postData(String host, int port, String path, String json) {

  String data = "";
  data += "POST " + path + " HTTP/1.1\r\n";
  data += "Host: " + host + "\r\n";
  data += "Connection: close\r\n";
  data += "Content-Type: application/json;\r\n";
  data += "Content-Length: " + String(json.length()) + "\r\n";
  data += "\r\n";
  data += json;
  Serial.println(data);

  WiFiClient client;
  if (client.connect(host.c_str(), port)) {
    Serial.println("Connection success!!!");
    client.print(data.c_str());
    delay(10);
    String response = client.readString();
    int bodypos =  response.indexOf("\r\n\r\n") + 4;
    return response.substring(bodypos);
  } else {
    Serial.println("Connection failed");
    client.stop();
    return "";
  }
}

const int RANGE = 2048;

/**
 * 保存した値を取得して、新しい値を保存する
 */
int EEPROM_pos_update(int newPos) {
  APPSTATUS buf;
  EEPROM.begin(100);
  EEPROM.get<APPSTATUS>(0, buf);
  int nowPos = buf.pos;

  buf.pos = newPos;
  EEPROM.put<APPSTATUS>(0, buf);
  EEPROM.commit();

  return nowPos;
}

void drive(int P_now, int P_ref) {

  // モーター制御
  while (true) {
    int d = P_ref - P_now;
    if (d == 0) break;
    // P_ref に近づくように ±1 する
    P_now += d / abs(d);
    // P_now にするには、どの方向に回せばよいか
    int i = (P_now + RANGE) % S;
    out(steps[i]);
    delay(20);
  }

  // 電流OFF
  out(0);
}

/**
 * 初期化(クライアントモード)
 */
void setup_client() {

  APPCONFIG config;
  getAppConfig(config);

  WiFi.begin(config.ssid.c_str(), config.pass.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // 情報取得
  StaticJsonBuffer<200> reqBuffer;
  JsonObject& reqRoot = reqBuffer.createObject();
  reqRoot["DSMK"] = config.DSMK;
  reqRoot["ASMK"] = config.ASMK;
  String json;
  reqRoot.printTo(json);

//  String json = "{\"DSMK\":" + config.DSMK + ", \"ASMK\":" + config.ASMK + "}";


  String buff = postData(config.HOST, 80, config.PATH, json);

  // レスポンスのJSONをパース
  StaticJsonBuffer<1024> resBuffer;
  JsonObject& root = resBuffer.parseObject(buff);
  JsonArray& item = root["item"].asArray();
  int i;
  for (i = 0; i < 6; i++) {
    const char* route = item[i][0]["route"];
    Serial.print(" i : ");
    Serial.print(i);
    Serial.println(route);
    if (route != 0) break;
  }

  // サーバはバスの位置を配列[0]〜[6]で返す
  // 360度を16分割して、[3]の位置を鉛直方向にする
  int P_ref = (i - 3) * 2048 / 16;

  // EEPROMから現在の値を取得して、新しい値を保存する
  int P_now = EEPROM_pos_update(P_ref);

  // モーター制御
  drive(P_now, P_ref);

  // 待ち時間を調整する
  (i + 1) * 20;

  ESP.deepSleep(10 * 1000 * 1000);
}

/**
 * 初期化(サーバモード)
 */
void setup_server() {
  byte mac[6];
  WiFi.macAddress(mac);
  String ssid = "";
  for (int i = 0; i < 6; i++) {
    ssid += String(mac[i], HEX);
  }
  Serial.println("SSID: " + ssid);
  Serial.println("PASS: " + pass);

  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ssid.c_str(), pass.c_str());

  server.on("/", HTTP_GET, handleRootGet);
  server.on("/", HTTP_POST, handleRootPost);
  server.begin();
  Serial.println("HTTP server started.");
}

/**
 * 初期化
 */
void setup() {
  Serial.begin(115200);
  Serial.println();

  // 1秒以内にMODEを切り替える
  //  0 : Server
  //  1 : Client
  delay(1000);

  // ファイルシステム初期化
  SPIFFS.begin();

  // PIN
  for (int i = 0; i < 4; i++) {
    pinMode(PIN[i], OUTPUT) ;
  }

  pinMode(MODE_PIN, INPUT);
  if (digitalRead(MODE_PIN) == 0) {
    // サーバモード初期化
    setup_server();
  } else {
    // クライアントモード初期化
    setup_client();
  }
}

void loop() {
  server.handleClient();

  // 0補正する
  if (0 < Serial.available()) {
    delay(10);
    String buff = "";
    while (0 < Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n') {

        // EEPROMから現在の値を取得して、新しい値を保存する
        int P_now = EEPROM_pos_update(0);

        int d = atoi(buff.c_str());
        int P_ref = P_now + d;

        Serial.print(" now:");
        Serial.print(P_now);

        // モーター制御
        drive(P_now, P_ref);

        Serial.println(d);
        break;
      } else if ('0' <= ch && ch <= '9' || ch == '-') {
        // ASCII文字コード
        buff += ch;
      }
    }
  }
}

