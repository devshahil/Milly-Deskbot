/*
  DeskBot v3 - companion edition + battery monitor
  ESP32-C3 Super Mini + display + button + TTP223 + MPU6050 + HC-SR04 + servo + 1S Li-ion

  ARDUINO IDE SETTINGS (Tools menu)
    Board:             "ESP32C3 Dev Module"
    USB CDC On Boot:   "Enabled"
    Partition Scheme:  "Huge APP (3MB No OTA/1MB SPIFFS)"   <- needed, WiFiManager + HTTPS is big
    Libraries:         Adafruit GFX, Adafruit ST7735 and ST7789 (or Adafruit ILI9341),
                       WiFiManager (tzapu), ESP32Servo

  DISPLAY (wiring unchanged from your working build)
    SCK -> 4   SDA/MOSI -> 6   A0/DC -> 5   RESET -> 3   CS -> 7   VCC, LED -> 3.3V   GND -> G
    Pick the panel with USE_ST7735 below (1 = 1.8" ST7735 160x128, 0 = ILI9341 240x320).

  OTHER WIRING (GPIO numbers = numbers printed on the board)
    Button              : pin 10 <-> G
    MPU6050 (GY-521)    : VCC -> 3.3, GND -> G, SDA -> 8, SCL -> 9, AD0 -> G
    TTP223 touch        : VCC -> 3.3, GND -> G, OUT -> 20
    Servo (SG90)        : red -> 5V rail, brown -> G, orange signal -> 21  (+470uF cap across red/brown)
    HC-SR04 ultrasonic  : VCC -> 5V rail, GND -> G, TRIG -> 2, ECHO -> 1 through divider
                          (ECHO -> 1k -> pin 1, and pin 1 -> 2k -> G).
                          If the module is HC-SR04P / 3.3V type: VCC -> 3.3 and ECHO -> 1 directly.
    Battery sense       : battery+ (after the power switch) -> 100k -> pin 0 -> 100k -> G,
                          plus 100nF from pin 0 to G.  Reads 1S Li-ion (3.0 - 4.2 V).
    Mount the HC-SR04 on the servo horn so it sweeps left and right.

  BATTERY POWER (1S Li-ion / LiPo)
    battery -> TP4056 (with protection) B+/B-  ->  OUT+ -> switch -> 5V boost (1A or more) -> 5V rail
    5V rail feeds the Super Mini "5V" pin, the servo and the HC-SR04.  The board's own regulator makes 3.3V.
    Put a Schottky diode (1N5819) between boost output and the 5V pin if you also plug in USB.

  BEHAVIOUR
    - Idle for "idle seconds" -> clock. The head slowly patrols the room.
    - Something new appears (closer than the learned room) -> switches to eyes and follows it.
    - Touch pad = pet it. Shake = dizzy. Tilt = eyes slide. Flip upside down = sleeps.
    - Weather moods (Open-Meteo, needs latitude/longitude in the dashboard).
    - Battery % shown on the clock and dashboard, low-battery warning, sleepy when nearly empty.
    - Button: short = next mode, hold = action, hold 2 s = reset.
    - Hold the button while powering on = forget WiFi.
*/

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>
#include <time.h>
#include <Adafruit_GFX.h>

// 1 = 1.8" ST7735 (160x128), 0 = ILI9341 (240x320, picture centred)
#define USE_ST7735 1
#if USE_ST7735
  #include <Adafruit_ST7735.h>
#else
  #include <Adafruit_ILI9341.h>
#endif

// ---------- pins ----------
#define TFT_SCK  4
#define TFT_MOSI 6
#define TFT_CS   7
#define TFT_DC   5
#define TFT_RST  3
#define BTN_PIN  10
#define SDA_PIN    8
#define SCL_PIN    9
#define TOUCH_PIN  20
#define SERVO_PIN  21
#define TRIG_PIN   2
#define ECHO_PIN   1
#define BATT_PIN   0   // ADC1 channel, through 100k/100k divider
#define SHAKE_JERK    0.45f   // g change between 50 ms samples that counts as a shake hit (lower = more sensitive)
#define NIGHT_SLEEPS  1       // 1 = at night (22:00-06:00) idle DeskBot sleeps with sleepy eyes instead of showing the clock
#define DETECT_MARGIN 20.0f   // cm closer than the learned room = "something is there"
constexpr int NANG = 9;       // patrol angles 30..150 in 15 degree steps

constexpr int SCRW = 160;
constexpr int SCRH = 128;

#if USE_ST7735
Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
constexpr int OFFX = 0, OFFY = 0;
#else
Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
constexpr int OFFX = 80, OFFY = 56;
#endif
GFXcanvas16 canvas(SCRW, SCRH);
WebServer server(80);
Preferences prefs;

// enums must be declared before any function (Arduino auto-prototypes)
enum Mode { MODE_EYES, MODE_CLOCK, MODE_POMO, MODE_SW, MODE_COUNT };
enum Mood { NORMAL, HAPPY, SLEEPY, ANGRY, SAD, FOCUS, DEPRESSED, SCARED, CRYING, MOOD_COUNT };

// ---------- config ----------
struct Config {
  uint8_t work = 25, brk = 5;
  uint8_t ah = 7, am = 0;
  bool aon = false, h24 = true, snd = true;
  float tz = 0;  // UTC offset in hours
  float lat = 0, lon = 0;
  uint16_t idleSec = 300;
  bool guard = true, wx = true;
} cfg;

void loadCfg() {
  prefs.begin("deskbot", true);
  cfg.work = prefs.getUChar("work", 25);
  cfg.brk  = prefs.getUChar("brk", 5);
  cfg.ah   = prefs.getUChar("ah", 7);
  cfg.am   = prefs.getUChar("am", 0);
  cfg.aon  = prefs.getBool("aon", false);
  cfg.h24  = prefs.getBool("h24", true);
  cfg.snd  = prefs.getBool("snd", true);
  cfg.tz   = prefs.getFloat("tz", 0);
  cfg.lat  = prefs.getFloat("lat", 0);
  cfg.lon  = prefs.getFloat("lon", 0);
  cfg.idleSec = prefs.getUShort("idle", 300);
  cfg.guard = prefs.getBool("guard", true);
  cfg.wx   = prefs.getBool("wx", true);
  prefs.end();
}
void saveCfg() {
  prefs.begin("deskbot", false);
  prefs.putUChar("work", cfg.work);
  prefs.putUChar("brk", cfg.brk);
  prefs.putUChar("ah", cfg.ah);
  prefs.putUChar("am", cfg.am);
  prefs.putBool("aon", cfg.aon);
  prefs.putBool("h24", cfg.h24);
  prefs.putBool("snd", cfg.snd);
  prefs.putFloat("tz", cfg.tz);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  prefs.putUShort("idle", cfg.idleSec);
  prefs.putBool("guard", cfg.guard);
  prefs.putBool("wx", cfg.wx);
  prefs.end();
}

// ---------- state ----------
const char* modeNames[] = {"Eyes", "Clock", "Pomodoro", "Stopwatch"};
Mode mode = MODE_EYES;

const char* moodNames[] = {"normal", "happy", "sleepy", "angry", "sad", "focus", "depressed", "scared", "crying"};
int moodForce = -1;  // -1 = automatic
Mood tempMood = NORMAL;
uint32_t tempUntil = 0;
void setTemp(Mood m, uint32_t ms) { tempMood = m; tempUntil = millis() + ms; }

struct tm tmNow;
bool timeOK = false;
uint32_t lastTimeCheck = 0;
String ipStr = "offline";

// pomodoro
bool pRun = false, pWork = true;
uint32_t pEnd = 0, pLeft = 25 * 60000UL;
int pDone = 0;
uint32_t pTotal() { return (uint32_t)(pWork ? cfg.work : cfg.brk) * 60000UL; }
uint32_t pLeftNow() {
  if (!pRun) return pLeft;
  int32_t d = (int32_t)(pEnd - millis());
  return d > 0 ? d : 0;
}

// stopwatch
bool swRun = false;
uint32_t swStart = 0, swEl = 0;

// alarm
bool ringing = false;
uint32_t ringStart = 0;
int lastAlarmKey = -1;

// toast
char toastBuf[24];
uint32_t toastUntil = 0;
void toast(const char* s) {
  strncpy(toastBuf, s, 23);
  toastBuf[23] = 0;
  toastUntil = millis() + 1500;
}

// ---------- battery ----------
float battMv = 0;          // battery voltage in millivolts (smoothed)
int battPct = -1;          // -1 = no battery detected (running from USB)
bool battLow = false;
uint32_t lastBatt = 0, nextBattWarn = 60000;

int battPercent(float mv) {
  // simple Li-ion curve: 4.20 V = 100 %, 3.70 V = 50 %, 3.30 V = 0 %
  static const float v[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int   p[] = {   0,    5,   15,   40,   58,   72,   85,   95,  100};
  if (mv <= v[0]) return 0;
  if (mv >= v[8]) return 100;
  for (int i = 1; i < 9; i++)
    if (mv <= v[i]) return p[i - 1] + (int)((mv - v[i - 1]) * (p[i] - p[i - 1]) / (v[i] - v[i - 1]));
  return 100;
}

// ---------- companion state ----------
void buzzWrite(int) {}   // no buzzer fitted

Servo head;
float servoPos = 90, servoTarget = 90;
uint32_t lastServoStep = 0, servoIdleSince = 0;
bool trackingOn = false;
float trkAngle = 90, lastD = 100;
uint32_t lastActivity = 0, touchUntil = 0, dizzyUntil = 0;
bool wakeFlag = false;
bool flipped = false, mpuOK = false;
int shState = 0;            // 0 = steady, 1 = being shaken right now
bool crying = false;        // stays true until he gets one touch
Mood shMood = NORMAL;       // face shown while being shaken
float shE = 0;              // shake "energy": builds up while shaking, fades when left alone
int shStage = 1;            // 1 scared, 2 sad, 3 angry, 4 crying
float tiltX = 0, tiltY = 0;

// weather (filled by a background task)
volatile bool wxOK = false;
volatile int wxCode = -1;
volatile float wxTemp = 0;
TaskHandle_t wxTask = NULL;

Mood wxMoodFor(int c, float t) {
  if (c == 0 || c == 1) return t > 34 ? SAD : HAPPY;
  if (c >= 95) return ANGRY;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return SAD;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return HAPPY;
  if (c == 45 || c == 48) return SLEEPY;
  return NORMAL;
}
const char* wxText(int c) {
  if (c == 0) return "Clear";
  if (c == 1) return "Mostly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Foggy";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "Weather";
}

void weatherTask(void*) {
  for (;;) {
    if (cfg.wx && (cfg.lat != 0 || cfg.lon != 0) && WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      char url[220];
      snprintf(url, sizeof url,
        "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code",
        cfg.lat, cfg.lon);
      if (http.begin(client, url)) {
        http.setTimeout(6000);
        if (http.GET() == 200) {
          String body = http.getString();
          int c = body.indexOf("\"current\":{");
          if (c >= 0) {
            int t = body.indexOf("\"temperature_2m\":", c);
            int w = body.indexOf("\"weather_code\":", c);
            if (t >= 0 && w >= 0) {
              wxTemp = body.substring(t + 17).toFloat();
              wxCode = body.substring(w + 15).toInt();
              wxOK = true;
            }
          }
        }
        http.end();
      }
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wxOK ? 900000 : 60000));
  }
}

// ---------- buzzer ----------
int beepsLeft = 0;
bool bOn = false;
uint32_t bNext = 0;
void beep(int n) {
  if (!cfg.snd) return;
  beepsLeft = n; bOn = false; bNext = 0;
}
void stopAlarm() {
  ringing = false;
  buzzWrite(LOW);
}
void buzzTick() {
  uint32_t now = millis();
  if (ringing) {
    uint32_t p = now % 1000;
    buzzWrite((p < 100 || (p > 200 && p < 300)) ? HIGH : LOW);
    return;
  }
  if (beepsLeft > 0 && now >= bNext) {
    if (!bOn) { bOn = true; buzzWrite(HIGH); bNext = now + 120; }
    else      { bOn = false; buzzWrite(LOW); beepsLeft--; bNext = now + 120; }
  }
}

// ---------- time / alarm / pomodoro logic ----------
void updateTime() {
  if (millis() - lastTimeCheck < 200) return;
  lastTimeCheck = millis();
  time_t t = time(nullptr);
  timeOK = t > 1700000000;
  localtime_r(&t, &tmNow);
  static uint32_t ipT = 0;
  if (millis() - ipT > 5000) {
    ipT = millis();
    ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("offline");
  }
}

void checkAlarm() {
  if (!cfg.aon || !timeOK) return;
  int key = tmNow.tm_yday * 1440 + tmNow.tm_hour * 60 + tmNow.tm_min;
  if (tmNow.tm_hour == cfg.ah && tmNow.tm_min == cfg.am && key != lastAlarmKey) {
    lastAlarmKey = key;
    ringing = true;
    ringStart = millis();
  }
  if (ringing && millis() - ringStart > 60000) stopAlarm();
}

void pToggle() {
  if (pRun) { pLeft = pLeftNow(); pRun = false; }
  else { pEnd = millis() + pLeft; pRun = true; }
}
void pReset() { pRun = false; pWork = true; pLeft = pTotal(); }
void pTick() {
  if (pRun && (int32_t)(millis() - pEnd) >= 0) {
    if (pWork) { pDone++; beep(3); } else beep(2);
    pWork = !pWork;
    pLeft = pTotal();
    pEnd = millis() + pLeft;  // auto-continue
    setTemp(pWork ? NORMAL : HAPPY, 8000);
  }
}

void swToggle() {
  if (swRun) { swEl += millis() - swStart; swRun = false; }
  else { swStart = millis(); swRun = true; }
}
void swReset() { swRun = false; swEl = 0; }

// ---------- mood ----------
Mood currentMood() {
  uint32_t now = millis();
  if (crying) return CRYING;                     // stays until he is touched
  if (shState == 1) return shMood;               // being shaken right now
  if (now < tempUntil) return tempMood;
  if (moodForce >= 0) return (Mood)moodForce;
  if (now < touchUntil) return HAPPY;
  if (trackingOn) return FOCUS;
  if (flipped) return SLEEPY;
  if (battPct >= 0 && battPct <= 5) return SLEEPY;
  if (timeOK && (tmNow.tm_hour >= 22 || tmNow.tm_hour < 6)) return SLEEPY;
  if (pRun && pWork) return FOCUS;
  return NORMAL;
}

struct MoodStyle { float lid, slant, cheek; uint16_t col; };
const MoodStyle styles[MOOD_COUNT] = {
  {0.00f,  0.00f, 0.0f, 0x07FF},  // normal  - cyan
  {0.00f,  0.00f, 1.0f, 0x07E0},  // happy   - green
  {0.45f,  0.00f, 0.0f, 0x867F},  // sleepy  - pale blue
  {0.10f,  0.45f, 0.0f, 0xF800},  // angry   - red
  {0.10f, -0.40f, 0.0f, 0x041F},  // sad     - blue
  {0.25f,  0.08f, 0.0f, 0xFD20},  // focus   - orange
  {0.50f, -0.30f, 0.0f, 0x4AAD},  // depressed - dull grey-blue, droopy
  {0.00f,  0.00f, 0.0f, 0xF7BE},  // scared  - pale white, wide and wobbling
  {0.35f, -0.35f, 0.0f, 0x041F},  // crying  - blue, squeezed, streaming tears
};

// ---------- eyes ----------
float eLid = 0, eSlant = 0, eCheek = 0, eOpen = 1, eOpenT = 1;
float lx = 0, ly = 0, lxT = 0, lyT = 0;
uint16_t eyeCol = 0x07FF;
uint32_t nextLook = 0, nextBlink = 2000, blinkEnd = 0, nextDrift = 20000;
int blinkPhase = 0;
float eStartle = 0;          // 1 = eyes just widened (startled), fades away
void forceBlink() { blinkPhase = 1; blinkEnd = millis() + 140; eOpenT = 0.05f; }

void updateEyes() {
  uint32_t now = millis();
  Mood m = currentMood();
  const MoodStyle& s = styles[m];
  eyeCol = s.col;
  eLid   += (s.lid - eLid) * 0.15f;
  eSlant += (s.slant - eSlant) * 0.15f;
  eCheek += (s.cheek - eCheek) * 0.15f;
  eStartle *= 0.9f;

  // idle: look around
  if (now >= nextLook) {
    lxT = random(-12, 13);
    lyT = random(-7, 8);
    if (random(4) == 0) { lxT = 0; lyT = 0; }
    nextLook = now + random(1200, 4000);
  }
  if (m == SLEEPY) { lxT = 0; lyT = 6; }
  if (m == SAD) { lxT = 0; lyT = 5; }               // looks down
  if (m == DEPRESSED) { lxT = 0; lyT = 7; }
  if (m == CRYING) { lxT = 0; lyT = 6; }
  if (m == SCARED) eStartle = 1.0f;                 // eyes stay wide
  if (now < dizzyUntil) {                       // shaken: eyes spin
    lxT = 12.0f * cosf(now * 0.012f);
    lyT = 7.0f * sinf(now * 0.012f);
    nextLook = now + 200;
  } else if (trackingOn) {                      // following something
    lxT = constrain((trkAngle - servoPos) * 0.6f, -12.0f, 12.0f);
    lyT = 0;
    nextLook = now + 300;
  } else if (mpuOK && (fabsf(tiltX) > 0.2f || fabsf(tiltY) > 0.2f)) {   // tilted: eyes slide
    lxT = constrain(tiltX * 30.0f, -12.0f, 12.0f);
    lyT = constrain(tiltY * 20.0f, -7.0f, 7.0f);
    nextLook = now + 300;
  }
  lx += (lxT - lx) * 0.25f;
  ly += (lyT - ly) * 0.25f;

  // idle: blink (sometimes double blink)
  if (blinkPhase == 0 && now >= nextBlink) {
    blinkPhase = 1;
    blinkEnd = now + ((m == SLEEPY || m == DEPRESSED) ? 500 : 90);
    eOpenT = 0.05f;
  } else if (blinkPhase == 1 && now >= blinkEnd) {
    blinkPhase = 0;
    eOpenT = 1;
    nextBlink = now + (random(5) == 0 ? 220 : random(2000, 6000));
  }
  eOpen += (eOpenT - eOpen) * 0.55f;

  // mood system: random short mood events when in auto mode
  if (moodForce < 0 && !trackingOn && shState == 0 && !crying && now >= nextDrift) {
    int r = random(100);
    Mood nm = r < 60 ? HAPPY : NORMAL;           // only happy now and then. Angry/sad/crying come from you
    setTemp(nm, random(3000, 7000));
    nextDrift = now + random(15000, 45000);
  }
}

void drawBattery(int x, int y, uint16_t col) {
  if (battPct < 0) return;
  canvas.drawRect(x, y, 18, 9, col);
  canvas.fillRect(x + 18, y + 2, 2, 5, col);
  int w = (int)(14.0f * battPct / 100.0f);
  if (w > 0) canvas.fillRect(x + 2, y + 2, w, 5, col);
}

void heart(int x, int y, int s, uint16_t c) {
  canvas.fillCircle(x - s / 2, y, s / 2, c);
  canvas.fillCircle(x + s / 2, y, s / 2, c);
  canvas.fillTriangle(x - s, y + 1, x + s, y + 1, x, y + s + 2, c);
}

void drawEyes() {
  canvas.fillScreen(0);
  Mood cm = currentMood();
  const int eh = 62, r = 14;
  int ew = 46 + (int)(6 * eStartle);
  int jx = 0, jy = 0;
  if (cm == ANGRY) { jx = (int)random(-1, 2); jy = (int)random(-1, 2); }   // trembling with anger
  else if (cm == SCARED) jx = ((millis() / 45) % 2) ? 3 : -3;                // wobbling, being shaken
  else if (cm == CRYING) jy = ((millis() / 180) % 2) ? 2 : 0;                // sobbing
  for (int side = -1; side <= 1; side += 2) {
    int cx = SCRW / 2 + side * 36;
    int cy = SCRH / 2;
    float h = max(4.0f, eh * eOpen * (1.0f + 0.2f * eStartle));
    int x = cx + (int)lx + jx - ew / 2;
    int y = cy + (int)ly + jy - (int)(h / 2);
    canvas.fillRoundRect(x, y, ew, (int)h, min(r, (int)h / 2), eyeCol);

    if (eLid > 0.01f)  // flat upper lid
      canvas.fillRect(x - 1, y - 1, ew + 2, (int)(h * eLid) + 1, 0);

    if (fabsf(eSlant) > 0.01f) {  // angry / sad slanted lid
      int sh = (int)(fabsf(eSlant) * h);
      int inner = side < 0 ? x + ew + 1 : x - 1;
      int outer = side < 0 ? x - 1 : x + ew + 1;
      int tip = eSlant > 0 ? inner : outer;
      canvas.fillTriangle(x - 1, y - 1, x + ew + 1, y - 1, tip, y + sh, 0);
    }

    if (eCheek > 0.01f) {  // happy cheeks push eye up
      int rr = ew * 3 / 4 + 4;
      canvas.fillCircle(cx + (int)lx, (int)(y + h + rr - eCheek * h * 0.55f), rr, 0);
    }

    if (cm == SAD || cm == CRYING) {       // tears rolling down
      int streams = (cm == CRYING) ? 2 : 1;
      uint32_t spd = (cm == CRYING) ? 25 : 45;
      for (int k = 0; k < streams; k++) {
        int tx = cx + (int)lx + side * (14 - k * 12);
        int ty = y + (int)h - 2;
        int dy = (int)(((millis() / spd) + (side > 0 ? 20 : 0) + k * 30) % 60);
        if (dy < 45) {
          int py = ty + dy * 6 / 10 + 3;
          canvas.fillCircle(tx, py, 3, 0x5DFF);
          canvas.fillTriangle(tx - 3, py - 1, tx + 3, py - 1, tx, py - 8, 0x5DFF);
        }
      }
    }
  }
  if (cm == SLEEPY) {
    canvas.setTextColor(0x867F);
    canvas.setTextSize(2);
    canvas.setCursor(128, 6 + (millis() / 600) % 3 * 3);
    canvas.print("z");
  }
  if (cm == DEPRESSED) { // own little rain cloud
    canvas.fillCircle(66, 14, 7, 0x39E7);
    canvas.fillCircle(80, 10, 9, 0x39E7);
    canvas.fillCircle(94, 14, 7, 0x39E7);
    canvas.fillRect(66, 14, 29, 7, 0x39E7);
    for (int i = 0; i < 3; i++) {
      int dy = (int)((millis() / 50 + i * 4) % 12);
      canvas.fillRect(68 + i * 12, 23 + dy, 2, 4, 0x5DFF);
    }
  }
  if (cm == ANGRY) {     // pulsing anger mark
    int rr = 5 + (int)((millis() / 150) % 3);
    canvas.fillCircle(138, 14, rr, 0xF800);
    canvas.fillRect(138 - rr - 1, 13, 2 * rr + 3, 3, 0);
    canvas.fillRect(137, 14 - rr - 1, 3, 2 * rr + 3, 0);
  }
  if (cm == SCARED) {    // cold sweat drop
    int sy = 20 + (int)((millis() / 70) % 14);
    canvas.fillCircle(148, sy, 3, 0x5DFF);
    canvas.fillTriangle(145, sy - 1, 151, sy - 1, 148, sy - 8, 0x5DFF);
  }
  if (trackingOn && cm == FOCUS) {   // locked on: targeting brackets in the corners
    uint16_t bc = ((millis() / 300) % 2) ? 0xFD20 : 0x7A60;
    const int L = 10;
    canvas.fillRect(2, 2, L, 2, bc);               canvas.fillRect(2, 2, 2, L, bc);
    canvas.fillRect(SCRW - 2 - L, 2, L, 2, bc);    canvas.fillRect(SCRW - 4, 2, 2, L, bc);
    canvas.fillRect(2, SCRH - 4, L, 2, bc);        canvas.fillRect(2, SCRH - 2 - L, 2, L, bc);
    canvas.fillRect(SCRW - 2 - L, SCRH - 4, L, 2, bc); canvas.fillRect(SCRW - 4, SCRH - 2 - L, 2, L, bc);
  }
  if (battLow) drawBattery(2, 2, ((millis() / 500) % 2) ? 0xF800 : 0x4208);   // blinking low-battery icon
  if (millis() < touchUntil) {                  // being petted: rising hearts
    uint32_t t = millis() % 1500;
    heart(80, 100 - (int)(t * 70 / 1500), 6, 0xF81F);
  }
}

// ---------- drawing helpers ----------
void ctext(const char* s, int y, int size, uint16_t col) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setTextSize(size);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  canvas.setCursor((SCRW - w) / 2, y);
  canvas.setTextColor(col);
  canvas.print(s);
}
void drawDots() {
  for (int i = 0; i < MODE_COUNT; i++)
    canvas.fillCircle(SCRW / 2 - 15 + i * 10, SCRH - 5, 2, i == mode ? 0xFFFF : 0x4208);
}

void drawClock() {
  canvas.fillScreen(0);
  if (!timeOK) {
    ctext("Syncing time...", 52, 1, 0xFFFF);
    ctext("(needs WiFi)", 66, 1, 0x7BEF);
    drawDots();
    return;
  }
  char b[16];
  int hh = tmNow.tm_hour;
  if (!cfg.h24) { hh %= 12; if (hh == 0) hh = 12; }
  snprintf(b, sizeof b, "%02d:%02d", hh, tmNow.tm_min);
  canvas.setTextSize(4); canvas.setTextColor(0x07FF);
  canvas.setCursor(6, 36); canvas.print(b);

  snprintf(b, sizeof b, "%02d", tmNow.tm_sec);
  canvas.setTextSize(2); canvas.setTextColor(0x7BEF);
  canvas.setCursor(132, 52); canvas.print(b);
  if (!cfg.h24) {
    canvas.setTextSize(1);
    canvas.setCursor(134, 38);
    canvas.print(tmNow.tm_hour >= 12 ? "PM" : "AM");
  }

  strftime(b, sizeof b, "%a %d %b", &tmNow);
  ctext(b, 90, 2, 0xFFFF);

  if (cfg.aon) {
    snprintf(b, sizeof b, "ALM %02d:%02d", cfg.ah, cfg.am);
    ctext(b, 8, 1, 0xFFE0);
  }
  if (wxOK && cfg.wx) {
    snprintf(b, sizeof b, "%.0fC %s", (double)wxTemp, wxText(wxCode));
    ctext(b, 112, 1, 0x7BEF);
  } else ctext(ipStr.c_str(), 112, 1, 0x4208);
  if (battPct >= 0) {
    uint16_t bc = battLow ? 0xF800 : 0x07E0;
    drawBattery(2, 2, bc);
    snprintf(b, sizeof b, "%d%%", battPct);
    canvas.setTextSize(1); canvas.setTextColor(bc);
    canvas.setCursor(25, 3); canvas.print(b);
  }
  drawDots();
}

void drawPomo() {
  canvas.fillScreen(0);
  uint32_t left = pLeftNow(), total = pTotal();
  if (left > total) left = total;
  uint16_t col = pWork ? 0xF800 : 0x07E0;
  ctext(pWork ? "FOCUS" : "BREAK", 8, 2, col);
  char b[12];
  snprintf(b, sizeof b, "%02u:%02u", (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
  ctext(b, 34, 5, 0xFFFF);
  canvas.drawRect(10, 82, 140, 10, 0x7BEF);
  int fw = (int)(136.0f * (total - left) / total);
  canvas.fillRect(12, 84, fw, 6, col);
  ctext(pRun ? "running" : "paused - hold to start", 98, 1, 0x7BEF);
  snprintf(b, sizeof b, "Done: %d", pDone);
  ctext(b, 110, 1, 0xFFE0);
  drawDots();
}

void drawStopwatch() {
  canvas.fillScreen(0);
  uint32_t e = swEl + (swRun ? millis() - swStart : 0);
  char b[16];
  snprintf(b, sizeof b, "%02u:%02u.%02u", (unsigned)((e / 60000) % 100),
           (unsigned)((e / 1000) % 60), (unsigned)((e / 10) % 100));
  ctext("STOPWATCH", 10, 1, 0x7BEF);
  ctext(b, 48, 3, swRun ? 0x07FF : 0xFFFF);
  ctext("hold: start/stop", 92, 1, 0x4208);
  ctext("hold 2s: reset", 102, 1, 0x4208);
  drawDots();
}

void drawAlarm() {
  bool f = (millis() / 400) % 2;
  canvas.fillScreen(f ? 0xF800 : 0);
  ctext("ALARM!", 36, 3, 0xFFFF);
  ctext("press button", 90, 1, 0xFFFF);
}

void drawToast() {
  if (millis() >= toastUntil) return;
  canvas.fillRoundRect(20, 50, 120, 28, 6, 0x2104);
  canvas.drawRoundRect(20, 50, 120, 28, 6, 0xFFFF);
  ctext(toastBuf, 60, 1, 0xFFFF);
}

// ---------- sensors, servo and guard logic ----------
float restX = 0, restY = 0, restZ = 1, lastMag = 1;
uint32_t lastMpu = 0;
bool prevFlipped = false;

// shake reactions: while being shaken he goes scared -> sad -> angry -> crying.
// Left steady = normal at once. Crying stays until he is touched once.
uint32_t shLast = 0, shStageSince = 0, shFade = 0;

int shStageFor(float e) { return e < 5.0f ? 1 : e < 13.0f ? 2 : e < 24.0f ? 3 : 4; }
Mood shMoodFor(int st) { return st == 1 ? SCARED : st == 2 ? SAD : ANGRY; }

void shakeHit(float jerk, uint32_t now) {
  lastActivity = now;
  wakeFlag = true;
  if (mode == MODE_CLOCK) mode = MODE_EYES;     // wake up straight away
  if (crying) return;                           // already crying: only a touch helps
  if (shState != 1) {                           // a new shake begins
    shState = 1; shStageSince = now;
    shStage = shStageFor(shE);
    if (shStage > 3) shStage = 3;
    shMood = shMoodFor(shStage);
  }
  shLast = now;
  shE += constrain(jerk, 0.4f, 3.0f) * 0.5f;    // harder shaking builds up faster
}

void shakeTick() {
  uint32_t now = millis();
  float dt = (now - shFade) / 1000.0f;
  shFade = now;
  if (shState == 1) {
    int target = shStageFor(shE);
    if (target > shStage && now - shStageSince >= 700) {      // one step at a time so each face is seen
      shStage++; shStageSince = now;
      if (shStage == 3 && shE < 20.0f) shE = 20.0f;           // remembers being angry for a few seconds
      if (shStage >= 4) {                                     // too much: crying
        crying = true; shState = 0; tempUntil = 0; forceBlink();
        return;
      }
      shMood = shMoodFor(shStage);
      forceBlink();
    }
    if (now - shLast > 500) {                                 // left him steady: back to normal at once
      shState = 0;
      tempUntil = 0;
      lastActivity = now;
    }
  } else if (shE > 0) {
    shE -= 0.8f * dt;                                         // the memory fades in about 10 seconds
    if (shE < 0) shE = 0;
  }
}

int16_t mpuWord() {
  int hi = Wire.read();
  int lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}
bool mpuRead(float& x, float& y, float& z) {
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(0x68, 6) != 6) return false;
  x = mpuWord() / 16384.0f;
  y = mpuWord() / 16384.0f;
  z = mpuWord() / 16384.0f;
  return true;
}
void mpuBegin() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  mpuOK = (Wire.endTransmission() == 0);
  if (!mpuOK) return;
  delay(100);
  float sx = 0, sy = 0, sz = 0; int n = 0;
  for (int i = 0; i < 20; i++) {
    float x, y, z;
    if (mpuRead(x, y, z)) { sx += x; sy += y; sz += z; n++; }
    delay(10);
  }
  if (n) { restX = sx / n; restY = sy / n; restZ = sz / n; }
}
void mpuTick() {
  if (!mpuOK || millis() - lastMpu < 50) return;
  lastMpu = millis();
  float x, y, z;
  if (!mpuRead(x, y, z)) return;
  float mag = sqrtf(x * x + y * y + z * z);
  float rm = sqrtf(restX * restX + restY * restY + restZ * restZ);
  tiltX = x - restX;
  tiltY = y - restY;
  flipped = ((x * restX + y * restY + z * restZ) / (mag * rm + 0.001f)) < -0.5f;
  uint32_t now = millis();
  if (flipped && !prevFlipped) { lastActivity = now; wakeFlag = true; }
  if (!flipped && prevFlipped) { lastActivity = now; wakeFlag = true; dizzyUntil = now + 2000; forceBlink(); }
  prevFlipped = flipped;
  float jerk = fabsf(mag - lastMag);
  lastMag = mag;
  if (fabsf(servoTarget - servoPos) > 1.0f && jerk < 1.0f) return;   // ignore servo vibration, but not hard shakes
  if (jerk > SHAKE_JERK) shakeHit(jerk, now);
}

bool touchPrev = false, cuddleDone = false;
uint32_t touchStart = 0, lastTap = 0;
int tapN = 0;
void touchTick() {
  bool t = digitalRead(TOUCH_PIN) == HIGH;
  uint32_t now = millis();
  if (t) {
    lastActivity = now;
    touchUntil = now + 3000;
    if (!touchPrev) {                                 // a new touch
      wakeFlag = true;
      if (mode == MODE_CLOCK) mode = MODE_EYES;
      touchStart = now; cuddleDone = false;
      shE = 0;                                        // petting wipes the shake memory
      if (crying) {                                   // one touch comforts him: happy and relieved
        crying = false; shState = 0; tapN = 0;
        setTemp(HAPPY, 6000);
        eStartle = 0.6f;
      } else {
        tapN = (now - lastTap < 2000) ? tapN + 1 : 1;
        if (tapN == 2) { setTemp(HAPPY, 1500); eStartle = 0.5f; }              // ticklish
        else if (tapN >= 4) { forceBlink(); setTemp(ANGRY, 2500); tapN = 0; }  // too much poking
      }
      lastTap = now;
    } else if (!cuddleDone && now - touchStart > 6000) {   // held a long time: cosy
      cuddleDone = true;
      setTemp(SLEEPY, 5000);
    }
  }
  touchPrev = t;
}

float readCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long d = pulseIn(ECHO_PIN, HIGH, 12000UL);   // ~2 m max
  if (d == 0) return 999.0f;
  return d * 0.0343f / 2.0f;
}

void servoAttach() {
  if (!head.attached()) {
    head.setPeriodHertz(50);
    head.attach(SERVO_PIN, 500, 2400);
  }
}
void servoGo(float a) { servoTarget = constrain(a, 25.0f, 155.0f); }
void servoTick() {
  uint32_t now = millis();
  if (now - lastServoStep < 15) return;
  lastServoStep = now;
  float d = servoTarget - servoPos;
  if (fabsf(d) < 0.3f) {
    servoPos = servoTarget;
    if (head.attached() && now - servoIdleSince > 1000) head.detach();   // no hum when still
    return;
  }
  servoIdleSince = now;
  servoAttach();
  float step = d * 0.2f;
  if (fabsf(step) < 0.6f) step = (d > 0) ? 0.6f : -0.6f;
  if (fabsf(step) > 3.0f) step = (step > 0) ? 3.0f : -3.0f;
  if (fabsf(step) > fabsf(d)) step = d;
  servoPos += step;
  head.write((int)roundf(servoPos));
}

// learned "empty room" distances
float baseCm[NANG];
bool baseOK = false;
int patIdx = 0, patDir = 1, patPhase = 0, hitCount = 0;
uint32_t patNext = 0;
// tracking
int trkPhase = 0;
bool trkArmed = false;
uint32_t trkNext = 0, lastSeen = 0, trkStart = 0, lastBoop = 0;
bool greeted = false;
float trkD[3];
float actAngle = 90, actDist = 100;

void calibrate() {
  trackingOn = false; patPhase = 0; hitCount = 0;
  servoAttach();
  for (int i = 0; i < NANG; i++) {
    float a = 30.0f + i * 15.0f;
    head.write((int)a);
    servoPos = servoTarget = a;
    delay(350);
    float r[3];
    for (int k = 0; k < 3; k++) { r[k] = readCm(); delay(30); }
    baseCm[i] = max(min(r[0], r[1]), min(max(r[0], r[1]), r[2]));   // median of 3
  }
  head.write(90);
  servoPos = servoTarget = 90;
  delay(300);
  patIdx = 0; patDir = 1;
  baseOK = true;
  servoIdleSince = millis();
}

int angleIdx(float a) { return constrain((int)roundf((a - 30.0f) / 15.0f), 0, NANG - 1); }

void startTracking(float a, float d) {
  uint32_t now = millis();
  trackingOn = true;
  trkAngle = a; trkPhase = 0; trkArmed = false;
  lastSeen = now; lastD = d;
  actAngle = a; actDist = d;
  lastActivity = now;
  wakeFlag = true;
  trkStart = now; greeted = false;
  eStartle = 0.8f;                  // eyes widen, then he locks on
}

void patrolTick() {
  uint32_t now = millis();
  if (now < patNext) return;
  float a = 30.0f + patIdx * 15.0f;
  if (patPhase == 0) { servoGo(a); patPhase = 1; patNext = now + 140; return; }
  if (fabsf(servoPos - a) > 0.6f) return;
  float d = readCm();
  float b = min(baseCm[patIdx], 200.0f);
  bool hit = d > 4.0f && d < 250.0f && d < b - DETECT_MARGIN;
  if (hit) {
    if (++hitCount >= 2) { hitCount = 0; startTracking(a, d); return; }
    patNext = now + 80;       // measure again at the same angle
    return;
  }
  hitCount = 0;
  if (d < 250.0f && d > baseCm[patIdx]) baseCm[patIdx] = d;   // room got emptier: relearn
  patIdx += patDir;
  if (patIdx >= NANG) { patIdx = NANG - 2; patDir = -1; }
  if (patIdx < 0) { patIdx = 1; patDir = 1; }
  patPhase = 0;
  patNext = now + (mode == MODE_CLOCK ? 500 : 80);
}

void trackTick() {
  uint32_t now = millis();
  if (now < trkNext) return;
  static const float off[3] = {-10.0f, 10.0f, 0.0f};
  float target = constrain(trkAngle + off[trkPhase], 25.0f, 155.0f);
  if (!trkArmed) { servoGo(target); trkArmed = true; trkNext = now + 70; return; }
  if (fabsf(servoPos - target) > 0.8f) return;
  trkArmed = false;
  trkD[trkPhase] = readCm();
  if (++trkPhase < 3) { trkNext = now + 30; return; }
  trkPhase = 0;

  float lim = min(min(baseCm[angleIdx(trkAngle)], 200.0f) - 10.0f, 180.0f);
  static const int order[3] = {2, 0, 1};      // prefer centre on ties
  int best = -1; float bd = 999.0f;
  for (int k = 0; k < 3; k++) {
    int i = order[k];
    if (trkD[i] > 4.0f && trkD[i] < lim && trkD[i] < bd - 1.0f) { bd = trkD[i]; best = i; }
  }
  if (best >= 0) {
    trkAngle = constrain(trkAngle + off[best] * 0.8f, 25.0f, 155.0f);
    lastSeen = now; lastD = bd;
    if (fabsf(trkAngle - actAngle) > 8.0f || fabsf(bd - actDist) > 10.0f) {   // it moved: not idle
      lastActivity = now; actAngle = trkAngle; actDist = bd;
    }
    if (bd < 10.0f && now - lastBoop > 5000 && shState == 0 && !crying) {          // very close: boop!
      lastBoop = now; setTemp(HAPPY, 1500); eStartle = 0.7f;
    } else if (!greeted && now - trkStart > 5000 && shState == 0) {    // someone stays around: friend
      greeted = true; if (!crying) setTemp(HAPPY, 3000);
    }
  }
  if (now - lastSeen > 3000) {                // lost it: go back to patrol
    trackingOn = false;
    if (greeted && shState == 0 && !crying) setTemp(SAD, 1800);
    patIdx = angleIdx(trkAngle); patPhase = 0; hitCount = 0;
  }
}

void batteryTick() {
  uint32_t now = millis();
  if (now - lastBatt < 2000) return;
  if (fabsf(servoTarget - servoPos) > 1.0f) return;   // servo load makes the voltage dip
  lastBatt = now;
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BATT_PIN);
  float mv = (sum / 8.0f) * 2.0f;                     // 100k/100k divider
  if (mv < 2500.0f) { battPct = -1; battLow = false; battMv = 0; return; }   // no battery: USB powered
  battMv = (battMv < 1.0f) ? mv : battMv * 0.8f + mv * 0.2f;
  battPct = battPercent(battMv);
  battLow = battPct <= 15;
  if (battLow && now >= nextBattWarn) {
    nextBattWarn = now + 300000UL;
  }
}

uint32_t nextWx = 20000;
void companionTick() {
  uint32_t now = millis();
  servoTick();
  touchTick();
  mpuTick();
  shakeTick();
  batteryTick();

  if (wakeFlag) { wakeFlag = false; if (mode == MODE_CLOCK) mode = MODE_EYES; }

  bool watch = (mode == MODE_EYES || mode == MODE_CLOCK);
  if (shState == 1) {
    servoGo(servoPos);          // hold still while being shaken
  } else if (!cfg.guard || !watch || !baseOK) {
    trackingOn = false;
    patPhase = 0;
    servoGo(90);
  } else if (trackingOn) trackTick();
  else patrolTick();

  // idle for too long -> clock
  if (mode == MODE_EYES && !ringing && shState == 0 && !crying && now - lastActivity > (uint32_t)cfg.idleSec * 1000UL) {
    if (trackingOn) {          // someone sitting still: treat as part of the room
      int bi = angleIdx(trkAngle);
      for (int j = bi - 1; j <= bi + 1; j++)
        if (j >= 0 && j < NANG) baseCm[j] = min(baseCm[j], lastD);
      trackingOn = false;
      patPhase = 0;
    }
    bool night = timeOK && (tmNow.tm_hour >= 22 || tmNow.tm_hour < 6);
    if (!(NIGHT_SLEEPS && night)) mode = MODE_CLOCK;   // at night he just sleeps instead
  }

  // weather mood every few minutes
  if (wxOK && cfg.wx && mode == MODE_EYES && !crying && now >= nextWx && now >= tempUntil) {
    Mood wm = wxMoodFor(wxCode, wxTemp);
    if (wm != NORMAL) setTemp(wm, 12000);
    nextWx = now + 180000UL;
  }
}

// ---------- button ----------
bool btnDown = false;
uint32_t btnT = 0, btnLast = 0;

void nextMode() { mode = (Mode)((mode + 1) % MODE_COUNT); beep(1); }

void actionMain() {
  switch (mode) {
    case MODE_EYES: {
      static int k = 0;
      k = (k + 1) % MOOD_COUNT;
      setTemp((Mood)k, 10000);
      beep(1);
      break;
    }
    case MODE_CLOCK:
      cfg.aon = !cfg.aon; saveCfg();
      toast(cfg.aon ? "Alarm ON" : "Alarm OFF");
      beep(cfg.aon ? 2 : 1);
      break;
    case MODE_POMO: pToggle(); toast(pRun ? "Started" : "Paused"); beep(1); break;
    case MODE_SW:   swToggle(); toast(swRun ? "Started" : "Stopped"); beep(1); break;
    default: break;
  }
}
void actionReset() {
  if (mode == MODE_POMO) { pReset(); toast("Pomodoro reset"); beep(2); }
  else if (mode == MODE_SW) { swReset(); toast("Stopwatch reset"); beep(2); }
}

void handleButton() {
  bool d = digitalRead(BTN_PIN) == LOW;
  uint32_t now = millis();
  if (d && !btnDown && now - btnLast > 30) {
    btnDown = true; btnT = now; btnLast = now;
  } else if (!d && btnDown && now - btnLast > 30) {
    btnDown = false; btnLast = now;
    uint32_t dur = now - btnT;
    lastActivity = now;
    if (ringing) { stopAlarm(); return; }
    if (dur < 600) nextMode();
    else if (dur < 2000) actionMain();
    else actionReset();
  }
}

// ---------- web dashboard ----------
const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>DeskBot</title>
<style>
body{font-family:system-ui;background:#111;color:#eee;margin:0 auto;padding:16px;max-width:480px}
h1{font-size:20px}.c{background:#1c1c1e;border-radius:14px;padding:14px;margin:12px 0}
button{background:#2d6cdf;color:#fff;border:0;border-radius:10px;padding:10px 14px;margin:3px;font-size:15px}
input,select{background:#2a2a2d;color:#eee;border:1px solid #444;border-radius:8px;padding:8px;width:100px}
label{display:flex;justify-content:space-between;align-items:center;margin:8px 0}
.big{font-size:36px;text-align:center}
</style></head><body>
<h1>DeskBot</h1>
<div class=c><div class=big id=t>--:--</div>
<div id=info style="text-align:center;color:#999;font-size:13px"></div></div>
<div class=c><b>Mode</b><br>
<button onclick="d('mode',0)">Eyes</button><button onclick="d('mode',1)">Clock</button>
<button onclick="d('mode',2)">Pomodoro</button><button onclick="d('mode',3)">Stopwatch</button></div>
<div class=c><b>Controls</b><br>
<button onclick="d('pomo')">Pomodoro start/pause</button><button onclick="d('pomoreset')">Reset</button><br>
<button onclick="d('sw')">Stopwatch start/stop</button><button onclick="d('swreset')">Reset</button><br>
<button style="background:#d33" onclick="d('stopalarm')">Stop alarm</button>
<button onclick="d('calib')">Recalibrate room</button></div>
<div class=c><b>Mood</b><br>
<select id=mood onchange="d('mood',this.value)" style="width:auto">
<option value=-1>Auto</option><option value=0>Normal</option><option value=1>Happy</option>
<option value=2>Sleepy</option><option value=3>Angry</option><option value=4>Sad</option>
<option value=5>Focus</option><option value=6>Depressed</option><option value=7>Scared</option><option value=8>Crying</option></select></div>
<div class=c><b>Settings</b>
<label>Work (min)<input id=work type=number min=1 max=120></label>
<label>Break (min)<input id=brk type=number min=1 max=60></label>
<label>Alarm time<input id=alarm type=time></label>
<label>Alarm on<input id=aon type=checkbox style="width:auto"></label>
<label>UTC offset (h)<input id=tz type=number step=0.5></label>
<label>24h clock<input id=h24 type=checkbox style="width:auto"></label>
<label>Sounds<input id=snd type=checkbox style="width:auto"></label>
<label>Latitude<input id=lat type=number step=0.001></label>
<label>Longitude<input id=lon type=number step=0.001></label>
<label>Weather moods<input id=wxon type=checkbox style="width:auto"></label>
<label>Guard mode<input id=guard type=checkbox style="width:auto"></label>
<label>Idle to clock (s)<input id=idle type=number min=10 max=3600></label>
<button onclick=save()>Save</button></div>
<script>
const $=i=>document.getElementById(i);
function d(a,v){fetch('/api/do?a='+a+'&v='+(v===undefined?0:v)).then(load)}
function load(){fetch('/api/status').then(r=>r.json()).then(s=>{
$('t').textContent=s.time;
$('info').textContent='Mode: '+s.modeName+' | Mood: '+s.mood+' | Pomodoro '+s.pomo+(s.pRun?' (running)':' (paused)')+' | Done: '+s.done+' | Weather: '+s.wx+(s.track?' | Tracking':'')+(s.bat>=0?' | Battery: '+s.bat+'% ('+s.mv+' mV)':' | USB power');
if(!window.f){window.f=1;$('work').value=s.work;$('brk').value=s.brk;$('alarm').value=s.alarm;
$('aon').checked=!!s.aon;$('tz').value=s.tz;$('h24').checked=!!s.h24;$('snd').checked=!!s.snd;$('lat').value=s.lat;$('lon').value=s.lon;$('idle').value=s.idle;$('guard').checked=!!s.guard;$('wxon').checked=!!s.wxon;}
})}
function save(){fetch('/api/save?work='+$('work').value+'&brk='+$('brk').value+'&alarm='+$('alarm').value+
'&aon='+(+$('aon').checked)+'&tz='+$('tz').value+'&h24='+(+$('h24').checked)+'&snd='+(+$('snd').checked)+'&lat='+$('lat').value+'&lon='+$('lon').value+'&idle='+$('idle').value+'&guard='+(+$('guard').checked)+'&wx='+(+$('wxon').checked))
.then(()=>alert('Saved'))}
setInterval(load,2000);load();
</script></body></html>)HTML";

void applyTime() {
  configTime((long)(cfg.tz * 3600), 0, "pool.ntp.org", "time.google.com");
}

void handleStatus() {
  char tb[16] = "--:--:--";
  if (timeOK) strftime(tb, sizeof tb, "%H:%M:%S", &tmNow);
  uint32_t left = pLeftNow();
  char pomo[20];
  snprintf(pomo, sizeof pomo, "%s %02u:%02u", pWork ? "work" : "break",
           (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
  char wxs[32] = "n/a";
  if (wxOK) snprintf(wxs, sizeof wxs, "%s %.0fC", wxText(wxCode), (double)wxTemp);
  char buf[760];
  snprintf(buf, sizeof buf,
    "{\"time\":\"%s\",\"modeName\":\"%s\",\"mood\":\"%s\",\"pomo\":\"%s\",\"pRun\":%d,"
    "\"done\":%d,\"work\":%d,\"brk\":%d,\"alarm\":\"%02d:%02d\",\"aon\":%d,"
    "\"tz\":%.2f,\"h24\":%d,\"snd\":%d,\"lat\":%.3f,\"lon\":%.3f,\"idle\":%d,"
    "\"guard\":%d,\"wxon\":%d,\"wx\":\"%s\",\"track\":%d,\"bat\":%d,\"mv\":%d}",
    tb, modeNames[mode], moodNames[currentMood()], pomo, pRun, pDone, cfg.work, cfg.brk,
    cfg.ah, cfg.am, cfg.aon, cfg.tz, cfg.h24, cfg.snd,
    cfg.lat, cfg.lon, cfg.idleSec, cfg.guard, cfg.wx, wxs, trackingOn, battPct, (int)battMv);
  server.send(200, "application/json", buf);
}

void handleDo() {
  String a = server.arg("a");
  int v = server.arg("v").toInt();
  if (a == "mode") mode = (Mode)constrain(v, 0, MODE_COUNT - 1);
  else if (a == "pomo") pToggle();
  else if (a == "pomoreset") pReset();
  else if (a == "sw") swToggle();
  else if (a == "swreset") swReset();
  else if (a == "mood") moodForce = constrain(v, -1, MOOD_COUNT - 1);
  else if (a == "stopalarm") stopAlarm();
  else if (a == "calib") calibrate();
  server.send(200, "text/plain", "ok");
}

void handleSave() {
  if (server.hasArg("work")) cfg.work = constrain(server.arg("work").toInt(), 1, 120);
  if (server.hasArg("brk"))  cfg.brk  = constrain(server.arg("brk").toInt(), 1, 60);
  if (server.hasArg("alarm")) {
    int h, m;
    if (sscanf(server.arg("alarm").c_str(), "%d:%d", &h, &m) == 2) {
      cfg.ah = constrain(h, 0, 23); cfg.am = constrain(m, 0, 59);
    }
  }
  if (server.hasArg("aon")) cfg.aon = server.arg("aon") == "1";
  if (server.hasArg("tz"))  cfg.tz  = server.arg("tz").toFloat();
  if (server.hasArg("h24")) cfg.h24 = server.arg("h24") == "1";
  if (server.hasArg("snd")) cfg.snd = server.arg("snd") == "1";
  if (server.hasArg("lat")) cfg.lat = server.arg("lat").toFloat();
  if (server.hasArg("lon")) cfg.lon = server.arg("lon").toFloat();
  if (server.hasArg("idle")) cfg.idleSec = constrain(server.arg("idle").toInt(), 10, 3600);
  if (server.hasArg("guard")) cfg.guard = server.arg("guard") == "1";
  if (server.hasArg("wx")) cfg.wx = server.arg("wx") == "1";
  if (wxTask) xTaskNotifyGive(wxTask);
  if (!pRun) pLeft = pTotal();
  saveCfg();
  applyTime();
  server.send(200, "text/plain", "ok");
}

// ---------- setup / loop ----------
void showMsg(const char* s) {
  tft.fillScreen(0);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(2);
  tft.setCursor(4, 16);
  tft.print(s);
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATT_PIN, ADC_11db);
  digitalWrite(TRIG_PIN, LOW);
  loadCfg();
  prefs.begin("deskbot", false);                 // one-time: idle-to-clock = 5 minutes
  if (prefs.getUChar("ver", 0) < 3) { prefs.putUChar("ver", 3); prefs.putUShort("idle", 300); cfg.idleSec = 300; }
  prefs.end();
  pLeft = pTotal();
  randomSeed(esp_random());

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
#if USE_ST7735
  tft.initR(INITR_BLACKTAB);  // if colors/offset look wrong try INITR_GREENTAB or INITR_REDTAB
#else
  tft.begin();
#endif
  tft.setRotation(1);
  tft.fillScreen(0);

  bool wipe = digitalRead(BTN_PIN) == LOW;
  showMsg("Connecting\nWiFi...");
  WiFiManager wm;
  if (wipe) { wm.resetSettings(); showMsg("WiFi reset!"); delay(1000); }
  wm.setAPCallback([](WiFiManager*) {
    showMsg("Join WiFi:\nDeskBot-Setup\nthen open\n192.168.4.1");
  });
  wm.setConfigPortalTimeout(180);
  wm.autoConnect("DeskBot-Setup");

  applyTime();
  MDNS.begin("deskbot");

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE); });
  server.on("/api/status", handleStatus);
  server.on("/api/do", handleDo);
  server.on("/api/save", handleSave);
  server.begin();

  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("offline");
  String msg = "Dashboard:\n" + ip + "\ndeskbot.local";
  showMsg(msg.c_str());
  delay(3000);
  xTaskCreate(weatherTask, "wx", 10240, NULL, 1, &wxTask);
  mpuBegin();
  showMsg("Learning\nthe room...\nstay clear!");
  calibrate();
  batteryTick();
  tft.fillScreen(0);
  lastActivity = millis();
}

void loop() {
  delay(1);
  server.handleClient();
  handleButton();
  updateTime();
  checkAlarm();
  pTick();
  buzzTick();
  companionTick();

  static uint32_t last = 0;
  uint32_t iv = (mode == MODE_EYES || ringing) ? 33 : 100;
  if (millis() - last < iv) return;
  last = millis();

  if (ringing) drawAlarm();
  else switch (mode) {
    case MODE_EYES: updateEyes(); drawEyes(); break;
    case MODE_CLOCK: drawClock(); break;
    case MODE_POMO: drawPomo(); break;
    case MODE_SW: drawStopwatch(); break;
    default: break;
  }
  drawToast();
  tft.drawRGBBitmap(OFFX, OFFY, canvas.getBuffer(), SCRW, SCRH);
}
