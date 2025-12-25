#include <PubSubClient.h>
#include <HX711.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "LittleFS.h"
#include "time.h"      
#include <RTClib.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include "driver/rtc_io.h"

// ---------------- MAPA DE HARDWARE ----------------

#define BUTTON_PIN 7       // Botão (GND + GPIO 7)
#define BUZZER_PIN 21      // Buzzer Passivo

// Controle de Energia
#define PIN_POWER_GROUP_A 14 
#define PIN_POWER_GROUP_B 1  

// Célula de Carga (HX711)
const int DT_LOAD_CELL = 6;
const int SCK_LOAD_CELL = 5;

// Sensor Temperatura (DS18B20)
const int ONE_WIRE_BUS = 2;

// Sensores I2C (AHT10 + RTC)
const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;

// ---------------- CONFIGURAÇÕES GERAIS ----------------

#define MAX_FILES_PER_BATCH 10 
const float CALIBRATION_FACTOR = -29200.0; 

// MQTT
const char* mqtt_server = "your_mqtt_broker";
const int mqtt_port = 1883;
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";

// NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0; 
const int   daylightOffset_sec = 0;

// ---------------- OBJETOS GLOBAIS ----------------
WiFiClient espClient;
PubSubClient client(espClient);
HX711 load_cell;
Adafruit_AHTX0 int_sensor;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ext_sensor(&oneWire);
RTC_DS3231 rtc;
Preferences preferences; 

// Variáveis de Estado
char deviceId[50] = ""; 
char wifi_ssid[32] = "";
char wifi_pass[64] = "";
long scale_offset = 0;
int sleep_minutes = 30;
bool has_external_rtc = false; 


// ---------------- ESTILO CSS + JS (Colmeia Digital) ----------------
const char* customCSS = 
"<style>"
  "body { background-color: #F5F5F5; color: #333; font-family: sans-serif; padding: 20px; }"
  "h1 { color: #2E7D32; text-align: center; margin-bottom: 20px; }" 
  "button { background-color: #2E7D32; color: #fff; border: none; border-radius: 5px; padding: 15px; width: 100%; font-size: 16px; font-weight: bold; cursor: pointer; margin-bottom: 15px; transition: none !important; position: relative; }"
  "button:active { background-color: #1B5E20; top: 2px; }" 
  "input { padding: 12px; width: 100%; border: 1px solid #ccc; border-radius: 5px; margin-bottom: 15px; box-sizing: border-box; }"
  "a { color: #2E7D32; text-decoration: none; }"

  ".ldr { display: inline-block; width: 16px; height: 16px; border: 2px solid #fff; border-top-color: transparent; border-radius: 50%; animation: s 1s linear infinite; margin-right: 10px; vertical-align: middle; }"
  "@keyframes s { to { transform: rotate(360deg); } }"
"</style>"

"<script>"
  "document.addEventListener('DOMContentLoaded', function() {"
    "var btns = document.querySelectorAll('button');"
    "btns.forEach(function(btn) {"
      "btn.addEventListener('click', function() {"
        "var txt = this.innerText;"
        "if(txt.indexOf('Configure') !== -1 || txt.indexOf('Wifi') !== -1) {"
           "this.innerHTML = '<span class=\"ldr\"></span> Procurando Redes...';"
        "} else if(txt.indexOf('Save') !== -1) {"
           "this.innerHTML = '<span class=\"ldr\"></span> Salvando...';"
        "} else {"
           "this.innerHTML = '<span class=\"ldr\"></span> Aguarde...';"
        "}"
        "this.style.opacity = '0.7';"
        "this.style.pointerEvents = 'none';"
      "});"
    "});"
  "});"
"</script>";

// ---------------- FUNÇÕES DE HARDWARE ----------------

void power_sensors(bool state) {
  if (state) {
    Serial.println("[HW] Ligando sensores...");
    pinMode(PIN_POWER_GROUP_A, OUTPUT);
    pinMode(PIN_POWER_GROUP_B, OUTPUT);
    digitalWrite(PIN_POWER_GROUP_A, HIGH); 
    digitalWrite(PIN_POWER_GROUP_B, HIGH); 
    delay(1000); 
  } else {
    Serial.println("[HW] Desligando sensores.");
    digitalWrite(PIN_POWER_GROUP_A, LOW);
    digitalWrite(PIN_POWER_GROUP_B, LOW);
    pinMode(PIN_POWER_GROUP_A, INPUT);
    pinMode(PIN_POWER_GROUP_B, INPUT);
  }
}

// --- GERADOR DE SOM ---
void play_tone(int frequency, int duration_ms) {
  long delay_us = 500000 / frequency;
  long loop_cycles = (long)frequency * (long)duration_ms / 1000;
  for (long i = 0; i < loop_cycles; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(delay_us);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(delay_us);
  }
}

void beep_ack() { play_tone(2500, 100); } // Bip curto
void beep_stage_2() { play_tone(2000, 100); delay(50); play_tone(2000, 100); } // Bip-Bip (Tara pronta)
void beep_stage_3() { play_tone(1000, 500); } // Bip (Config pronta)
void beep_success() { play_tone(2000, 100); delay(50); play_tone(3000, 200); }
void beep_error() { play_tone(200, 400); delay(100); play_tone(200, 400); } 

// ---------------- PERSISTÊNCIA ----------------

void load_preferences() {
  preferences.begin("hive_config", true);
  
  String tempID = preferences.getString("dev_id", "");
  tempID.toCharArray(deviceId, 50);
  
  String s = preferences.getString("ssid", "");
  s.toCharArray(wifi_ssid, 32);
  
  String p = preferences.getString("pass", "");
  p.toCharArray(wifi_pass, 64);
  
  scale_offset = preferences.getLong("offset", 0);

  sleep_minutes = preferences.getInt("sleep_min", 30);
  if (sleep_minutes < 1) sleep_minutes = 1; 
  
  preferences.end();
  
  Serial.printf("[MEM] ID: %s | SSID: %s | Sleep: %d min\n", deviceId, wifi_ssid, sleep_minutes);
}

void save_config(const char* id, const char* ssid, const char* pass, const char* sleep_str) {
  preferences.begin("hive_config", false);
  
  if(strlen(id) > 0) preferences.putString("dev_id", id);
  if(strlen(ssid) > 0) preferences.putString("ssid", ssid);
  if(strlen(pass) > 0) preferences.putString("pass", pass);
  
  if(strlen(sleep_str) > 0) {
      int val = atoi(sleep_str);
      if (val >= 1) {
          preferences.putInt("sleep_min", val);
          Serial.printf("[MEM] Novo tempo de sono salvo: %d min\n", val);
      }
  }
  
  preferences.end();
  Serial.println("[MEM] Configurações salvas com sucesso.");
}

void save_offset(long offset) {
  preferences.begin("hive_config", false);
  preferences.putLong("offset", offset);
  preferences.end();
  Serial.println("[MEM] Novo offset salvo.");
}

// ---------------- CONEXÃO ----------------

bool setup_wifi() {
  if (strlen(wifi_ssid) == 0) {
    Serial.println("[WIFI] Nenhuma credencial salva no Preferences.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  Serial.printf("[WIFI] Conectando a: %s\n", wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_pass); 

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) { 
    delay(500); 
    Serial.print("."); 
    retries++; 
  }
  Serial.println();
  
  if(WiFi.status() == WL_CONNECTED) {
      Serial.print("[WIFI] Conectado! IP: ");
      Serial.println(WiFi.localIP());
      return true;
  } else {
      Serial.println("[WIFI] Falha.");
      return false;
  }
}

void enter_config_mode() {
  Serial.println("--- MODO CONFIG ---");
  for(int i=0; i<3; i++) { play_tone(1000, 300); delay(100); }
  
  WiFiManager wm;
  wm.setCustomHeadElement(customCSS);
  wm.setTitle("Colmeia Digital");
  
  WiFiManagerParameter custom_id("hiveid", "ID da Colmeia", deviceId, 50);
  wm.addParameter(&custom_id);

  char sleepStr[10];
  itoa(sleep_minutes, sleepStr, 10);
  WiFiManagerParameter custom_sleep("sleep_min", "Intervalo de Leitura (min)", sleepStr, 5);
  wm.addParameter(&custom_sleep);
  
  wm.setConfigPortalTimeout(180); 
  
  if (!wm.startConfigPortal("Colmeia-Config")) { 
    Serial.println("Timeout/Cancelado. Reiniciando...");
    beep_error(); 
    ESP.restart(); 
  }
  
  Serial.println("Dados recebidos do portal.");
  
  String newSSID = wm.getWiFiSSID();
  String newPass = wm.getWiFiPass();

  save_config(custom_id.getValue(), newSSID.c_str(), newPass.c_str(), custom_sleep.getValue());
  
  beep_success(); 
  delay(1000);
  ESP.restart();
}

// ---------------- SISTEMA DE ARQUIVOS & TEMPO ----------------

void setup_filesystem() {
  if (!LittleFS.begin(true)) { LittleFS.format(); LittleFS.begin(true); }
  if (!LittleFS.exists("/buffer")) { LittleFS.mkdir("/buffer"); }
}

void setup_ntp_sync() {
  Serial.println("[TIME] Sincronizando NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  int retry = 0;
  while(!getLocalTime(&timeinfo) && retry < 5){ delay(1000); retry++; }
  if (retry < 5 && has_external_rtc) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      Serial.println("[TIME] RTC Hardware atualizado via NTP.");
  }
}

String get_timestamp() {
  char buffer[32];
  if (has_external_rtc) {
    DateTime now = rtc.now();
    if (now.year() > 2020) {
      snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
      return String(buffer);
    }
  }
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_year + 1900 > 2020) {
       strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
       return String(buffer);
    }
  }
  return "1970-01-01T00:00:00Z";
}

// ---------------- MQTT & JSON ----------------

String getPublishTopic() { return "hive/" + String(deviceId) + "/sensors"; }

bool reconnect_mqtt() {
  if (client.connected()) return true;
  Serial.print("[MQTT] Conectando Broker... ");
  if (client.connect(deviceId, mqtt_user, mqtt_password)) {
      Serial.println("OK");
      return true;
  }
  Serial.print("Falha rc="); Serial.println(client.state());
  return false;
}

void build_json_payload(JsonDocument& doc, String timestamp) {
  load_cell.set_scale(CALIBRATION_FACTOR);
  load_cell.set_offset(scale_offset);

  float weight = load_cell.is_ready() ? load_cell.get_units(10) : -1.0;
  
  sensors_event_t humidity, temp;
  if (!int_sensor.getEvent(&humidity, &temp)) { 
    temp.temperature = -127; humidity.relative_humidity = -1; 
    Serial.println("[ERRO] Falha leitura AHT10");
  }

  ext_sensor.requestTemperatures(); 
  float externalTemp = ext_sensor.getTempCByIndex(0);
  if (externalTemp == DEVICE_DISCONNECTED_C) externalTemp = -127.0;

  Serial.println("--- DADOS COLETADOS ---");
  Serial.printf("Peso: %.3f kg\n", weight);
  Serial.printf("Temp Int: %.2f C | Umid: %.2f %%\n", temp.temperature, humidity.relative_humidity);
  Serial.printf("Temp Ext: %.2f C\n", externalTemp);
  Serial.println("-----------------------");

  doc["weight"] = serialized(String(weight, 3));
  doc["temp_i"] = serialized(String(temp.temperature, 2));
  doc["humid_i"] = serialized(String(humidity.relative_humidity, 2));
  doc["temp_e"] = serialized(String(externalTemp, 2));
  doc["timestamp"] = timestamp;
}

void save_reading_to_buffer(const JsonDocument& doc) {
  String filename = "/buffer/" + doc["timestamp"].as<String>() + ".json";
  filename.replace(":", "-");
  File file = LittleFS.open(filename, "w");
  if (file) {
    serializeJson(doc, file);
    Serial.printf("[BUFFER] Salvo: %s\n", filename.c_str());
  }
  file.close();
}

void send_buffered_readings() {
  File root = LittleFS.open("/buffer");
  if (!root || !root.isDirectory()) return;
  int sent_count = 0;
  File file = root.openNextFile();
  while (file) {
    if (sent_count >= MAX_FILES_PER_BATCH) { Serial.println("[BUFFER] Limite de lote atingido."); break; }
    if (!client.connected() && !reconnect_mqtt()) { break; }
    
    String filename = file.name();
    Serial.print("[BUFFER] Enviando: " + filename + " ... ");
    
    client.beginPublish(getPublishTopic().c_str(), file.size(), false);
    while (file.available()) client.write(file.read());
    if (client.endPublish()) {
      for (int i = 0; i < 10; i++) {
        client.loop(); 
        delay(100);
      }
      Serial.println("OK");
      file.close(); LittleFS.remove("/buffer/" + filename); sent_count++; 
    } else { 
      Serial.println("Falha"); 
      file.close(); 
    }
    file = root.openNextFile();
  }
  root.close();
}

void go_to_sleep() {
  power_sensors(false);
  load_cell.power_down(); 
  rtc_gpio_pullup_en((gpio_num_t)BUTTON_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)BUTTON_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0); 
  
  // CONVERSÃO: Minutos para Microssegundos (min * 60 * 1.000.000)
  uint64_t sleep_us = (uint64_t)sleep_minutes * 60 * 1000000ULL;
  
  esp_sleep_enable_timer_wakeup(sleep_us);
  Serial.printf("[SLEEP] Dormindo por %d minutos. Zzz...\n", sleep_minutes);
  Serial.flush(); 
  esp_deep_sleep_start();
}

// ---------------- SETUP PRINCIPAL ----------------

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  pinMode(BUZZER_PIN, OUTPUT);
  
  load_preferences(); 

  // --- LÓGICA DO BOTÃO (3 ESTÁGIOS) ---
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[BOOT] Acordado pelo botão.");
    delay(50);
    if (digitalRead(BUTTON_PIN) == HIGH) {
       Serial.println("[BTN] Ruído.");
    } else {
       beep_ack(); 
       unsigned long pressStart = millis();
       bool beep_s2 = false, beep_s3 = false;
       while (digitalRead(BUTTON_PIN) == LOW) {
           unsigned long duration = millis() - pressStart;
           if (duration > 3000 && !beep_s2) { beep_stage_2(); beep_s2 = true; }
           if (duration > 6000 && !beep_s3) { beep_stage_3(); beep_s3 = true; }
           delay(50);
       }
       
       unsigned long totalDuration = millis() - pressStart;
       
       if (totalDuration >= 6000) {
           Serial.println("[ACTION] Configuração");
           enter_config_mode();
       } else if (totalDuration >= 3000) {
           Serial.println("[ACTION] Tara");
           power_sensors(true); 
           load_cell.begin(DT_LOAD_CELL, SCK_LOAD_CELL);
           load_cell.power_up();
           load_cell.set_scale(CALIBRATION_FACTOR); 
           load_cell.tare(20); 
           save_offset(load_cell.get_offset()); 
           beep_success(); 
           go_to_sleep();
       } else {
          Serial.println("[ACTION] Leitura Forçada");
       }
    }
  }

  // --- FLUXO DE LEITURA ---
  Serial.println("[BOOT] Iniciando Leitura...");
  power_sensors(true); 
  setup_filesystem();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000); 
  
  int rtc_tries = 0;
  while (!rtc.begin() && rtc_tries < 3) { delay(100); rtc_tries++; }
  has_external_rtc = (rtc_tries < 3);
  if(has_external_rtc) Serial.println("[RTC] OK"); else Serial.println("[RTC] OFF");

  bool aht_ok = false;
  for(int i=0; i<3; i++) {
    if(int_sensor.begin(&Wire)) { aht_ok = true; break; }
    delay(100);
  }
  
  ext_sensor.begin();
  load_cell.begin(DT_LOAD_CELL, SCK_LOAD_CELL);
  load_cell.power_up(); 

  bool wifi_ok = false;
  if (strlen(wifi_ssid) > 0) {
      wifi_ok = setup_wifi(); 
      if (!wifi_ok) Serial.println("[WIFI] Falha. Modo Offline.");
  } else {
      Serial.println("[WIFI] Vazio. Config...");
      enter_config_mode();
  }
  
  bool needs_sync = !has_external_rtc;
  if (has_external_rtc) {
      DateTime now = rtc.now();
      if (rtc.lostPower() || now.year() < 2025) needs_sync = true;
  }
  if (wifi_ok && needs_sync) setup_ntp_sync();

  // Coleta e Envio
  String ts = get_timestamp(); 
  JsonDocument doc;
  build_json_payload(doc, ts);

  if (wifi_ok) {
    client.setServer(mqtt_server, mqtt_port);
    if (reconnect_mqtt()) {
      send_buffered_readings();
      char payload[512]; serializeJson(doc, payload);
      Serial.println("[MQTT] Enviando payload...");
      if (client.publish(getPublishTopic().c_str(), payload)) {
        for (int i = 0; i < 10; i++) {
        client.loop(); 
        delay(100);
        } 
        Serial.println("[MQTT] Sucesso."); 
      } else save_reading_to_buffer(doc);
    } else save_reading_to_buffer(doc);
  } else {
    Serial.println("[WIFI] Offline.");
    save_reading_to_buffer(doc);
  }

  if (wifi_ok) { client.disconnect(); WiFi.mode(WIFI_OFF); }
  go_to_sleep();
}

void loop() {}