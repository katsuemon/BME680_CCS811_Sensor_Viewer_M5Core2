/*
　　　M5Core2 と BME680,CCS811をI2C接続し、温度,湿度,気圧,eCO2,TVOCなどを測定し表示
　　　Wi-FiにつながっていればNTPで時刻合わせをし、Ambientへログを送る
*/

#include <M5Core2.h>
#include <Wire.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Ambient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_CCS811.h>
#include <LovyanGFX.hpp>

#define LGFX_M5STACK_CORE2
#define SEALEVELPRESSURE_HPA (1013.25)
#define TEMPERATURE_OFFSET    0
#define HUMIDIDY_OFFSET       0
#define LCD_BRIGHTNESS      128
#define LCD_DIMMER           24
#define GRAPH_HEIGHT         90
#define CO2_MAX_ON_GRAPH   3000
#define CO2_MIN_ON_GRAPH      0
#define TEMP_MAX_ON_GRAPH    40
#define TEMP_MIN_ON_GRAPH   -10
#define HUMID_MAX_ON_GRAPH  100
#define HUMID_MIN_ON_GRAPH    0

const char* ssid       = "yourssid";          //Wi-Fi AP
const char* password   = "password";          //Wi-Fi Passwd

const char* ntpServer  = "ntp.nict.jp";       // NTPサーバのURL
const long  gmtOffset_sec = 3600 * 9;         // GMT+9(日本時間)
const int   daylightOffset_sec = 0;           // サマータイム時差(無し)

unsigned int channelId = 00000;               // AmbientのチャネルID
const char* writeKey   = "writekey";          // ライトキー

const uint8_t BME680_I2C_ADDRESS = 0x77;      // BME680のI2Cアドレス
const uint8_t CCS811_I2C_ADDRESS = 0x5A;      // CCS811のI2Cアドレス

static LGFX lcd;
static LGFX_Sprite sensor_value(&lcd);
static LGFX_Sprite graph1(&lcd);
static LGFX_Sprite graph2(&lcd);
static Adafruit_BME680 bme;
static Adafruit_CCS811 ccs;
static Ambient ambient;
static WiFiClient client;

float temperature, humidity, pressure, gas, altitude;
float prev_temperature, prev_humidity;
uint16_t eco2, tvoc;
boolean is_scroll = false, prev_is_scroll;
int lcd_brightness = LCD_BRIGHTNESS;
int count;

void read_bme680(float &temperature, float &humidity, float &pressure, float &gas, float &altitude) {
    //BME680から値を取得  
    if (! bme.performReading()) {
      Serial.println("Failed to read BME680");
    }
    temperature = bme.temperature + TEMPERATURE_OFFSET;
    humidity = bme.humidity + HUMIDIDY_OFFSET;
    pressure = bme.pressure / 100.0;
    gas = bme.gas_resistance / 1000.0;
    altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
}

void read_ccs811(float temperature, float humidity, uint16_t &eco2, uint16_t &tvoc) {
    if (ccs.available()) {
      //CCS811に気圧と温度を与えて精度を高める
      //ccs.setEnvironmentalData(humidity-HUMIDIDY_OFFSET, temperature-TEMPERATURE_OFFSET);
    }
    if (ccs.readData()) {
      Serial.println("Failed to read CCS911");
    }
    eco2 = ccs.geteCO2();
    tvoc = ccs.getTVOC();
}

//時刻を表示する
void print_LocalTime() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    lcd.println("Failed to obtain time");
    return;
  }
  lcd.setFont(&fonts::Font0);
  lcd.setTextSize(2);
  // カーソル位置を設定
  lcd.setCursor(45, 224);
  lcd.printf("%04d-%02d-%02d %02d:%02d:%02d"
                , timeinfo.tm_year + 1900
                , timeinfo.tm_mon
                , timeinfo.tm_mday
                , timeinfo.tm_hour
                , timeinfo.tm_min
                , timeinfo.tm_sec
  );
  lcd.setTextSize(0);
}

void draw_graph1(int co2) {
  //eCO2の最大値を3000としておく
  int h = (co2 * GRAPH_HEIGHT) / CO2_MAX_ON_GRAPH;
  int y = int(GRAPH_HEIGHT - h); 

  for (int i = 0; i < 7; i++) {
    if (co2 <= 600) {
      graph1.drawFastVLine(319, y, h, CYAN);
    } else if (co2 > 600 && co2 <= 800) {
      graph1.drawFastVLine(319, y, h, GREEN);
    } else if (co2 > 800 && co2 <= 1000) {
      graph1.drawFastVLine(319, y, h, YELLOW);
    } else if (co2 > 1000 && co2 <= 1400) {
      graph1.drawFastVLine(319, y, h, ORANGE);
    } else if (co2 > 1400 && co2 <= 1900) {
      graph1.drawFastVLine(319, y, h, MAGENTA);
    } else if (co2 > 1900) {
      graph1.drawFastVLine(319, y, h, RED);
    }
    graph1.pushSprite(&lcd,  0,129,BLACK);
    graph2.pushSprite(&lcd,  0,129,BLACK);
    graph1.scroll(-1, 0);
    print_LocalTime();
    delay(1);
  }
  graph1.drawFastVLine(319, y, h, BLACK);
  graph1.pushSprite(&lcd,  0,129);
  graph1.scroll(-1, 0);
  delay(1);
}

void draw_graph2(int temp,int humid,int prev_temp,int prev_humid) {
  //温度の最大値と最小値をグラフ用に補正
  if (temp <= TEMP_MIN_ON_GRAPH) { temp = TEMP_MIN_ON_GRAPH;}
  if (temp >= TEMP_MAX_ON_GRAPH) { temp = TEMP_MAX_ON_GRAPH;}
  temp = temp - TEMP_MIN_ON_GRAPH;
  if (prev_temp <= TEMP_MIN_ON_GRAPH) { prev_temp = TEMP_MIN_ON_GRAPH;}
  if (prev_temp >= TEMP_MAX_ON_GRAPH) { prev_temp = TEMP_MAX_ON_GRAPH;}
  prev_temp = prev_temp - TEMP_MIN_ON_GRAPH;
  int ty = int(GRAPH_HEIGHT - (temp * GRAPH_HEIGHT) / (TEMP_MAX_ON_GRAPH - TEMP_MIN_ON_GRAPH));
  int hy = int(GRAPH_HEIGHT - (humid * GRAPH_HEIGHT) / (HUMID_MAX_ON_GRAPH - HUMID_MIN_ON_GRAPH));

  int prev_ty = int(GRAPH_HEIGHT - (prev_temp * GRAPH_HEIGHT) / (TEMP_MAX_ON_GRAPH - TEMP_MIN_ON_GRAPH));
  int prev_hy = int(GRAPH_HEIGHT - (prev_humid * GRAPH_HEIGHT) / (HUMID_MAX_ON_GRAPH - HUMID_MIN_ON_GRAPH));

  graph2.drawLine(318,prev_ty,319,ty,YELLOW);
  graph2.drawLine(318,prev_hy,319,hy,CYAN);

  graph2.pushSprite(&lcd,  0,129,BLACK);
  graph2.scroll(-1, 0);
}

void print_sendor_data(float temperature, float humidity, float pressure, float altitude, uint16_t eco2) {
  sensor_value.drawLine(0,  61, 319,  61, LIGHTGREY);
  sensor_value.drawLine(0, 127, 319, 127, LIGHTGREY);
  lcd.drawLine(0, 220, 319, 220, LIGHTGREY);

  //気温の表示
  sensor_value.fillRect(0, 0,  320, 61, BLACK);
  if (temperature >= 10) {
    sensor_value.drawFloat(temperature,     1, 56, 6, &fonts::Font4);
  } else if (temperature >= 0 && temperature < 10) {
    sensor_value.drawFloat(temperature,     1, 70, 6, &fonts::Font4);
  } else if (temperature < 0 && temperature > -10) {
    sensor_value.drawFloat(temperature,     1, 62, 6, &fonts::Font4);
  } else if (temperature <= -10) {
    sensor_value.drawFloat(temperature,     1, 48, 6, &fonts::Font4);
  }
  sensor_value.drawString("℃" ,116,  7,&fonts::lgfxJapanGothicP_20);

  //湿度の表示
  sensor_value.drawFloat(humidity,    1, 196, 6, &fonts::Font4);
  sensor_value.drawString("%"  ,258,  6,&fonts::Font4);

  //気圧の表示
  if (pressure < 1000) {
    sensor_value.drawFloat(pressure, 1, 42, 32, &fonts::Font4);
  } else {
    sensor_value.drawFloat(pressure, 1, 28, 32, &fonts::Font4);
  }
  sensor_value.drawString("hPa",116, 32,&fonts::Font4);

  //高度の表示
  if (altitude < 100) {
    sensor_value.drawFloat(altitude, 1, 196, 32, &fonts::Font4);
  } else {
    sensor_value.drawFloat(altitude, 1, 182, 32, &fonts::Font4);
  }
  sensor_value.drawString("m"  ,258, 32,&fonts::Font4);

  //CO2の表示
  sensor_value.fillRect(0, 62, 320, 64, BLACK);
  if (eco2 < 1000) {
    sensor_value.drawNumber(eco2, 129, 71, &fonts::Font7);
  } else {
    sensor_value.drawNumber(eco2,  97, 71, &fonts::Font7);
  }
  sensor_value.drawString("ppm",230, 99,&fonts::Font4);

  //CPU温度の表示
  sensor_value.drawString("CPU",28, 72,&fonts::Font2);
  sensor_value.drawFloat(temperatureRead(),1,28,88,&fonts::Font2);

  sensor_value.pushSprite(&lcd,  0,  0);

  delay(1);
}

void task_sensor_check(void* arg) {
  while (1) {
    //BME680から値を取得
    read_bme680(temperature, humidity, pressure, gas, altitude);

    //CCS811から値を取得
    read_ccs811(temperature, humidity, eco2, tvoc);

    Serial.printf("T:%2.2f*C H:%0.2f%% P:%0.2fhPA A:%7.2fm G:%6.1fKOhms TVOC:%6dppb CO2:%6dppm\n"
                  , temperature
                  , humidity
                  , pressure
                  , altitude
                  , gas
                  , tvoc
                  , eco2
    );

    prev_temperature = temperature;
    prev_humidity = humidity;
    is_scroll = !is_scroll;
    delay(10000);
  }
}

void task_draw_screen(void* arg) {
  while (1) {
    //データの表示
    print_sendor_data(temperature, humidity, pressure, altitude, eco2);

    //時刻の表示
    print_LocalTime();

    //グラフの表示
    if (is_scroll != prev_is_scroll) {
      draw_graph1(eco2);
      draw_graph2(temperature,humidity,prev_temperature,prev_humidity);
      prev_is_scroll = is_scroll;
    }

    //省電力の為に５分経ったら画面を暗くする、画面タッチで復帰
    for (int i = 0; i < 10; i++) { //1秒のウェイト
      if (count >= 300 && lcd_brightness == LCD_BRIGHTNESS) { //5分経ったら画面を暗くする
        lcd_brightness = LCD_DIMMER;
        lcd.setBrightness(lcd_brightness);
      }
      if (M5.Touch.ispressed() && lcd_brightness == LCD_DIMMER) { //画面タッチで画面の輝度を戻す
        lcd_brightness = LCD_BRIGHTNESS;
        lcd.setBrightness(lcd_brightness);
        count = 0;
      }
      delay(100);
    }
    if (lcd_brightness == LCD_BRIGHTNESS) {count++;}
  }
}

void setup() {
  //　M5Core2の初期化
  //       LCD ,SD  ,Seri ,I2C
  M5.begin(true,true,false,true);
  Wire.begin();
  Serial.begin(115200);
  pinMode(32, INPUT_PULLUP);
  pinMode(33, INPUT_PULLUP);

  lcd.init();
  lcd.wakeup();
  lcd.setBrightness(128);

  //　画面にクロスハッチを描く
  for (int16_t y = 0; y < 15; y++) {
    lcd.drawFastHLine(0, y * 16, 319, WHITE);
  }
  for (int16_t x = 0; x < 20; x++) {
    lcd.drawFastVLine(x * 16, 0, 239, WHITE);
  }
  delay(1000);
  lcd.clear(BLACK);

  //Wi-Fiに接続できるか確認
  lcd.setCursor(0, 0);
  lcd.print("Wifi check...");
  Serial.print("Wifi check...");
  WiFi.begin(ssid, password);

  // 500ms*120回なので、1分でタイムアウト
  for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    lcd.println("Connect OK");
    lcd.println(WiFi.localIP());    
    Serial.println("Connect OK");
    Serial.println(WiFi.localIP());
  } else {
    lcd.print("ERROR\n");
    Serial.println("ERROR"); // Wifi接続エラー
  }

  //Wifi接続出来たらNTPにアクセスして時刻合わせ
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("NTP server check...");
    Serial.print("NTP server check...");  
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm t;
    if (!getLocalTime(&t)) {
      lcd.print("ERROR\n");
      Serial.println("ERROR"); // NTPサーバ接続エラー
    } else {
      lcd.print("Connect OK\n");
      Serial.println("Connect OK");
    }
  }

  //センサー類の接続チェック
  //CCS811のチェック
  lcd.print("CCS811 check...");
  Serial.print("CCS811 check...");
  if (!ccs.begin(CCS811_I2C_ADDRESS)) {
    lcd.println("\nCould not find a valid CCS881 sensor, check wiring!");
    Serial.println("Could not find a valid CCS881 sensor, check wiring!");
    while (1);
  } else {
    lcd.println("Connect OK");
    Serial.println("Connect OK");
  }

  //CCS811の準備完了を待つ
  while (!ccs.available());
  lcd.println("CCS811:Ready");

  //BME680のチェック
  lcd.print("BME680 check...");
  Serial.print("BME680 check...");

  if (!bme.begin(BME680_I2C_ADDRESS)) {
    lcd.print("\nCould not find a valid BME680 sensor, check wiring!");
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while (1);
  } else {
    lcd.println("Connect OK");
    Serial.println("Connect OK");
    // Set up oversampling and filter initialization
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms
  }

  lcd.println("All checks completed.");
  Serial.println("All checks completed.");
  delay(100);
  lcd.clear(BLACK);

  //グラフ1用のスプライト
  graph1.setColorDepth(8);
  graph1.createSprite(320, 90);
  graph1.fillSprite(BLACK);

  //グラフ2用のスプライト
  graph2.setColorDepth(8);
  graph2.createSprite(320, 90);
  graph2.fillSprite(BLACK);

  //センサーの値を描くスプライト
  sensor_value.setColorDepth(4);
  sensor_value.createSprite(320,128);
  sensor_value.fillSprite(BLACK);

  ambient.begin(channelId, writeKey, &client); // チャネルIDとライトキーを指定してAmbientの初期化

  //とりあえずセンサーから初回データを読んでおく
  read_bme680(temperature, humidity, pressure, gas, altitude);
  read_ccs811(temperature, humidity, eco2, tvoc);
  prev_temperature = temperature;
  prev_humidity = humidity;

  //センサーチェック用のタスクと画面描画用のタスクを作成
  xTaskCreatePinnedToCore(task_sensor_check, "Task0", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(task_draw_screen, "Task1", 4096, NULL, 1, NULL, 1);
}

void loop() {
  M5.update();

  if (WiFi.status() == WL_CONNECTED) {
    lcd.sleep();
    lcd.setBrightness(0);

    //Ambientへデータ送信
    ambient.set(1, temperature);
    ambient.set(2, humidity);
    ambient.set(3, pressure);
    ambient.set(4, eco2);
    ambient.send();
    Serial.println("Send Data for Ambient");

    delay(1000);
    lcd.wakeup();
    lcd.setBrightness(lcd_brightness);
  } else {
    Serial.println("Wi-Fi connection Fail");
    delay(1000);
  }

  delay(299000);
}