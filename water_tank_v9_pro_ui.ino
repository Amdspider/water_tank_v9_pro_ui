/*
 * =====================================================================
 *  SMART WATER LEVEL INDICATOR  v8.0  — Professional UI Edition
 * =====================================================================
 *
 * CHANGES FROM v7.0:
 *  [V8-1]  MAIN UI: Replaced "SMART WATER LEVEL" title with real-time
 *          clock (HH:MM:SS) displayed prominently on the main screen.
 *          Motor running → shows the robot animation.
 *          Motor stopped → shows gauge-based main UI.
 *
 *  [V8-2]  SCREEN TITLES: Removed the "1." "2." "3." numbering prefix
 *          from all screen title tabs. Clean names only.
 *
 *  [V8-3]  PROFESSIONAL UI: Completely redesigned screens with
 *          clean lines, proper typography, icon polish, and
 *          no "children's drawing" aesthetic.
 *
 *  [V8-4]  BATTERY REMOVED: All battery ADC code, battPct variable,
 *          and battery icon drawing fully removed.
 *
 *  [V8-5]  BUZZER IN AUTO MODE: No alert buzzer sounds in AUTO mode.
 *          Buzzer only plays for: button presses, motor start/stop
 *          (manual only), maintenance mode entry/exit, sleep, OTA.
 *          Auto-mode events are silent — user cannot interact anyway.
 *
 *  [V8-6]  LED COLOR BY WATER LEVEL:
 *          ≥ highThr (90%)  → Green (full)
 *          ≥ lowThr  (30%)  → Blue (normal)
 *          < lowThr  (30%)  → Yellow (low)
 *          < 10%            → Red (critical/dry)
 *          Error/leak/volt  → Red (overrides)
 *          Motor running    → Cyan (pumping)
 *
 *  [V8-7]  MOTOR RUNNING UI: While motor is running and user presses
 *          BTN1 (next screen), it briefly shows motor control screen
 *          then returns to main. Robot animation plays on main screen
 *          during pumping.
 *
 *  [V8-8]  SMOOTH DISPLAY: Display task tuned for butter-smooth
 *          updates. Robot animation at 15 FPS; other screens at 2 FPS.
 *          Partial redraws only where values changed.
 *
 * All v7.0 features retained (FreeRTOS, night sleep, voltage safety,
 * leak detection, MQTT, OTA, dry-run protection, etc.).
 *
 * WIRING (unchanged):
 *   TFT  CS=5  DC=16  RST=17  BL=2
 *   BTN1=32  BTN2=33
 *   RELAY=4
 *   TRIG=13  ECHO=34
 *   BUZZER=14   VOLT_PIN=36
 *   LEDs  R=25  G=27  B=15
 *
 * BUTTON LOGIC:
 *   Normal scr  BTN1 short = Next screen
 *   Settings    BTN1 short = Select next setting row
 *   Main AUTO   BTN2 short = Switch to MANUAL
 *   Main MANUAL BTN2 short = Motor ON/OFF
 *   Settings    BTN2 short = Change selected setting
 *   Other scr   BTN2 short = Previous screen
 *   Main scr    Long BTN2  = Reset daily usage / force AUTO from MANUAL
 *   Dual hold 3s = MAINTENANCE mode toggle
 * =====================================================================
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <AceButton.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include "mbedtls/md.h"
#include <math.h>

using namespace ace_button;

// ============================================================
//  DEBUG
// ============================================================
#define DBG(fmt, ...)   Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#define INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   Serial.printf("[ERR] " fmt "\n", ##__VA_ARGS__)
#define VOLT(fmt, ...)  Serial.printf("[VOLT] " fmt "\n", ##__VA_ARGS__)
#define PUMP(fmt, ...)  Serial.printf("[PUMP] " fmt "\n", ##__VA_ARGS__)
#define SLEEP(fmt, ...) Serial.printf("[SLEEP] " fmt "\n", ##__VA_ARGS__)
#define MQTT_LOG(fmt,...) Serial.printf("[MQTT] " fmt "\n", ##__VA_ARGS__)

// ============================================================
//  PINS
// ============================================================
#define TFT_CS    5
#define TFT_DC    16
#define TFT_RST   17
#define TFT_BL    2
#define BTN1_PIN  32
#define BTN2_PIN  33
#define LED_R     25
#define LED_G     27
#define LED_B     15
#define PERIPH_5V_EN 26
#define RELAY_PIN 4
#define TRIG_PIN  13
#define ECHO_PIN  34
#define BUZZ_PIN  14
#define VOLT_PIN  36

// ============================================================
//  DISPLAY GEOMETRY  (ST7735 1.8" 160×128 landscape)
// ============================================================
#define SW 160
#define SH 128

// ---- Professional Colour Palette ----
#define C_BG        0x0000   // Pure black
#define C_WHITE     0xFFFF
#define C_LGRAY     0xC618   // Light gray
#define C_GRAY      0x8410   // Medium gray
#define C_DKGRAY    0x2104   // Dark gray
#define C_CYAN      0x07FF
#define C_GREEN     0x07E0
#define C_LTGREEN   0x2FE4
#define C_DKGREEN   0x0380
#define C_RED       0xF800
#define C_DKRED     0x7800
#define C_ORANGE    0xFC60
#define C_YELLOW    0xFFE0
#define C_BLUE      0x001F
#define C_LTBLUE    0x5EFF
#define C_DKBLUE    0x000D
#define C_MDBLUE    0x025F
#define C_PURPLE    0xA01F
#define C_MAGENTA   0xF81F
#define C_TEAL      0x0410
#define C_INDIGO    0x4010

// Accent colors for screens
#define ACCENT_MOTOR   0x07E0   // Green
#define ACCENT_ALERTS  0xFC60   // Orange
#define ACCENT_USAGE   0xA01F   // Purple
#define ACCENT_SYS     0x001F   // Blue
#define ACCENT_SMART   0xF81F   // Magenta

// Robot / Animation colors
#define BG_DAY      0x5EDF
#define BG_NIGHT    0x0C41
#define MOON_COL    0xFFE0
#define MOON_SHAD   0xD69A
#define SUN_COL     0xFFE0
#define SUN_RIM     0xFD20
#define STAR_COL    0xFFFF
#define GROUND_DAY  0x4E8A
#define GROUND_NIGHT 0x2945
#define ROBOT_BODY  0xB5B6
#define ROBOT_DARK  0x8410
#define ROBOT_LIGHT 0xEF5D
#define EYE_WHITE   0xFFFF
#define EYE_PUPIL   0x0000
#define EYE_BLUE    0x5EFF
#define PUMP_BODY   0xAD75
#define PUMP_ACCENT 0xFD20
#define PUMP_HANDLE 0xFFE0
#define WATER_TOP   0x9FFF
#define WATER_MID   0x5EFF
#define WATER_BOT   0x3D7F
#define WATER_DARK  0x001F
#define SPARK_COL   0xFFE0

// Screen title colors (no numbering)
const uint16_t SCREEN_TITLE_COL[] = {
  C_CYAN,       // MAIN SCREEN (not shown as tab)
  ACCENT_ALERTS,// ALERTS
  ACCENT_USAGE, // USAGE DATA
  ACCENT_SYS,   // SYSTEM STATUS
  ACCENT_SMART, // SMART INFO
  C_BLUE        // SETTINGS
};

// Clean titles without numbers
const char* SCREEN_TITLES[] = {
  "MAIN SCREEN",
  "ALERTS",
  "USAGE DATA",
  "SYSTEM STATUS",
  "SMART INFO",
  "SETTINGS"
};
#define SCR_MAIN      0
#define SCR_ALERTS    1
#define SCR_USAGE     2
#define SCR_SYSTEM    3
#define SCR_SMART     4
#define SCR_SETTINGS  5
#define SCREEN_COUNT  6
#define SETTINGS_COUNT 5

// ============================================================
//  BUZZER
// ============================================================
#define BZ_CH   1
#define BZ_RES  8

enum BzPat {
  PAT_IDLE=0, PAT_CLICK, PAT_START, PAT_STOP,
  PAT_INFO, PAT_MAINT, PAT_RESET_DAILY, PAT_ERROR, PAT_SLEEP,
  PAT_BLOCK, PAT_AUTO_DONE, PAT_MANUAL_FULL, PAT_EMERGENCY
};
struct BzNote { uint16_t freq; uint16_t ms; };

// Only button/user-interaction sounds
const BzNote SND_CLICK[]       = {{1800,25},{0,0}};
const BzNote SND_START[]       = {{880,80},{0,40},{1047,80},{0,40},{1319,120},{0,0}};
const BzNote SND_STOP[]        = {{1319,80},{0,30},{1047,80},{0,30},{880,120},{0,0}};
const BzNote SND_INFO[]        = {{1047,40},{0,20},{1319,60},{0,0}};
const BzNote SND_MAINT[]       = {{600,500},{0,0}};
const BzNote SND_RESET_DAILY[] = {{880,80},{0,30},{1047,80},{0,30},{1319,80},{0,30},{1568,100},{0,0}};
const BzNote SND_ERROR[]       = {{500,200},{0,100},{400,300},{0,0}};
const BzNote SND_SLEEP[]       = {{1319,80},{0,40},{1047,80},{0,40},{880,80},{0,40},{660,120},{0,0}};
const BzNote SND_BLOCK[]       = {{400,150},{0,80},{300,200},{0,0}};
const BzNote SND_AUTO_DONE[]   = {{1568,70},{0,30},{1319,70},{0,30},{1047,90},{0,25},{784,130},{0,0}};
const BzNote SND_MANUAL_FULL[] = {{2500,180},{0,80},{2500,180},{0,80},{1900,260},{0,160},{0,0}};
const BzNote SND_EMERGENCY[]   = {{2800,120},{0,40},{2200,120},{0,40},{2800,120},{0,40},{1200,360},{0,0}};

struct BzRequest { int pat; bool repeat; };

// ============================================================
//  CONSTANTS
// ============================================================
#define TANK_H_CM         83.0f
#define TANK_VOL_LITRES   1000.0f
#define SENSOR_OFF_CM     5.0f
#define US_SAMPLES        5
#define US_TIMEOUT_US     30000UL
#define PUMP_TIMEOUT_MS   120000UL
#define PUMP_CHK_MS       30000UL
#define PUMP_MIN_RISE     2.0f
#define MAX_PUMP_MS       1800000UL
#define MIN_RUN_MS        30000UL
#define MIN_REST_MS       60000UL
#define HYSTERESIS        3.0f
#define MAINT_HOLD_MS     3000
#define SAFE_V_LOW        180.0f
#define SAFE_V_HIGH       250.0f
#define AUTO_RETURN_MS    15000UL
#define TANK_FORCE_ON_MAX 90.0f
#define VOLT_CALIB        583.0f
#define VOLT_DC_SAMP      80
#define VOLT_AC_SAMP      200
#define VOLT_SAMP_US      250
#define VOLT_CHK_MS       1000UL
#define MA_WINDOW         8
#define MA_VOLT_WINDOW    16
#define SLOPE_WINDOW      6
#define LEAK_EVAL_MS      30000UL
#define SCORE_THRESHOLD   6
#define LEAK_SLOPE_MIN   -1.5f
#define LEAK_SLOPE_MAX   -0.05f
#define LEAK_TOTAL_DROP_THR 2.0f
#define LEAK_PAUSE_STABLE 0.3f
#define NIGHT_SLEEP_HOUR  23
#define WAKE_HOUR         6
#define MOTOR_BLOCK_START 22
#define MOTOR_BLOCK_END   7
#define MANUAL_FULL_RESPONSE_MS 12000UL
#define NVS_NS            "wtank"
#define MQTT_CLIENT_ID    "water-tank-esp32"
#define FW_VERSION        "v9.1-debug"
#define EEPROM_SZ         512
#define EE_MAGIC_A        0
#define EE_MAGIC          0xAE
#define EE_SETTINGS       1
#define EE_LITRES         220
#define EE_USAGE          240
#define EE_HEALTH         340
#define TZ_STRING         "IST-5:30"

// MQTT topics
#define T_LEVEL     "water_tank/level"
#define T_PUMP      "water_tank/pump"
#define T_VOLT      "water_tank/voltage"
#define T_ALERT     "water_tank/alert"
#define T_USAGE     "water_tank/usage"
#define T_MODE      "water_tank/mode"
#define T_STATUS    "water_tank/status"
#define T_FSM_STATE "water_tank/fsm_state"
#define T_HEALTH    "water_tank/health"
#define T_LEAK      "water_tank/leak"
#define T_ETA       "water_tank/fill_eta"
#define T_SCORE     "water_tank/leak_score"
#define T_DBG_INFO  "water_tank/debug/info"
#define T_DBG_LIVE  "water_tank/debug/live"
#define T_DBG_SENS  "water_tank/debug/sensors"
#define T_DBG_POWER "water_tank/debug/power"
#define T_DBG_TASKS "water_tank/debug/tasks"
#define T_DBG_EVENT "water_tank/debug/events"
#define T_CMD_PMP   "water_tank/cmd/pump"
#define T_CMD_MOD   "water_tank/cmd/mode"
#define T_CMD_RST   "water_tank/cmd/reset"
#define T_CMD_DBG   "water_tank/cmd/debug"
#define MQTT_PUB_MS 5000
#define DEBUG_PUB_MS 3000

// ============================================================
//  STRUCTURES
// ============================================================
struct Settings {
  float    lowThr, highThr;
  float    safeVLow, safeVHigh;
  float    tankH, lpm, tankVolLitres;
  bool     autoMode, schedEn;
  char     broker[80];
  uint16_t mqttPort;
  char     mqttUser[32], mqttPass[40];
  uint8_t  crc;
};
Settings cfg = {
  30.0f, 90.0f, SAFE_V_LOW, SAFE_V_HIGH,
  TANK_H_CM, 15.0f, TANK_VOL_LITRES,
  true, true,
  "3356a8cf8c9943d183bec9e288fc9d4c.s1.eu.hivemq.cloud",
  8883, "spider.home", "Amdspider@home5", 0
};

struct PumpHealth { uint32_t runSecs, cycles; uint8_t crc; };
PumpHealth health = {0,0,0};

enum AlertLevel  { AL_INFO, AL_WARNING, AL_CRITICAL };
enum AlertType   { AT_NONE,AT_LEAK,AT_HIGH_LEVEL,AT_DRY_RUN,AT_VOLTAGE,AT_WIFI_LOST,AT_PUMP_TIMEOUT,AT_PUMP_MAXRUN };
struct Alert {
  AlertLevel level; AlertType type;
  char msg[32]; char detail[40];
  uint8_t hour, minute;
  bool active;
};
#define ALERT_HISTORY 5
Alert alertHistory[ALERT_HISTORY];
int   alertHead    = 0;
int   activeAlerts = 0;

enum SystemState { SYS_IDLE,SYS_NORMAL_USE,SYS_REFILLING,SYS_POSSIBLE_LEAK,SYS_CONFIRMED_LEAK,SYS_ERROR };
const char* sysStateStr[] = {"IDLE","NORMAL","REFILLING","POSS.LEAK","LEAK!","ERROR"};

enum OpMode { MODE_AUTO, MODE_MANUAL, MODE_MAINTENANCE };

struct MovAvg {
  float buf[MA_WINDOW]; int head=0,count=0; float sum=0;
  void push(float v){ if(count==MA_WINDOW)sum-=buf[head]; buf[head]=v; sum+=v; head=(head+1)%MA_WINDOW; if(count<MA_WINDOW)count++; }
  float avg(){ return count?sum/count:0; }
  bool  full(){ return count==MA_WINDOW; }
};
struct MovAvgV {
  float buf[MA_VOLT_WINDOW]; int head=0,count=0; float sum=0;
  void push(float v){ if(count==MA_VOLT_WINDOW)sum-=buf[head]; buf[head]=v; sum+=v; head=(head+1)%MA_VOLT_WINDOW; if(count<MA_VOLT_WINDOW)count++; }
  float avg(){ return count?sum/count:0; }
  bool  full(){ return count==MA_VOLT_WINDOW; }
};

// ============================================================
//  OBJECTS
// ============================================================
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
WiFiClientSecure secClient;
PubSubClient     mqtt(secClient);
ButtonConfig     btnCfg;
AceButton        btn1, btn2;
Preferences      prefs;

// ============================================================
//  RTOS HANDLES
// ============================================================
SemaphoreHandle_t xStateMutex;
SemaphoreHandle_t xTftMutex;
QueueHandle_t     xBuzzerQueue;

// ============================================================
//  SHARED STATE
// ============================================================
volatile float    waterLevel=0, smoothLevel=0, waterPrev=0;
volatile float    currentVoltage=0, smoothVoltage=0;
volatile float    totalLitres=0, dailyUsage=0;
volatile float    hourlyUsage[24]={0};
volatile float    fillEtaMin=0, predictedLevel=0, currentSlope=0;
volatile float    learnedLPM=0;
volatile int      lpmSamples=0;
volatile float    leakSnap=0, leakTotalDrop=0;
volatile int      lastLeakScore=0, leakClearCount=0;
volatile unsigned long leakTimer=0, pauseTimer=0;
volatile bool     pumpRunning=false, pumpStartPend=false, stopPending=false;
volatile bool     pumpTimedOut=false, dryRunChk=false;
volatile bool     voltWarn=false, voltWarnMuted=false, voltIsAbnormal=false;
volatile bool     veryLow=false, veryLowMuted=false;
volatile bool     leakConfirmed=false, tankFullAlerted=false;
volatile bool     alertDismissed=false;
volatile bool     manualFullAlarm=false;
volatile unsigned long pumpOnTime=0, pumpStopTime=0, stopPendTime=0;
volatile unsigned long pumpToutChk=0, dryRunTime=0, voltAbnormalTimer=0;
volatile unsigned long manualFullAlarmAt=0;
volatile unsigned long usageUpd=0;
volatile unsigned long bootTimeMs=0, litresSaveTmr=0, mqttLastPub=0;
volatile unsigned long debugLastPub=0;
volatile unsigned long hbSensor=0, hbControl=0, hbButton=0, hbDisplay=0, hbMqtt=0;
volatile float    levelAtStart=0;
char              pumpStartReason[48]="boot";
char              pumpStopReason[64]="boot";
volatile SystemState sysState=SYS_IDLE;
volatile OpMode   opMode=MODE_AUTO;
volatile bool     g_pendingSave=false;
volatile bool     debugEnabled=false;
volatile uint32_t mqttConnectCount=0, mqttFailCount=0;
volatile float    lastDistanceCm=-1.0f;

// UI state
volatile int   currentScreen=0, lastScreen=-1;
volatile int   settingsIndex=0;
volatile bool  uiRedraw=true;
volatile unsigned long lastButtonTime=0;

// Button dual-hold
volatile unsigned long g_btn1DownAt=0, g_btn2DownAt=0;
volatile bool g_btn1Down=false, g_btn2Down=false, g_maintFired=false;
volatile unsigned long g_btn2HoldStart=0;
volatile bool g_btn2LongFired=false;
volatile unsigned long g_btn1HoldStart=0;
volatile bool g_btn1LongFired=false;
volatile bool g_suppressBtn1Click=false;

// Sleep
volatile bool sleepRequested=false;

// Voltage print timer
volatile unsigned long lastVoltPrint=0;
volatile unsigned long lastDebugPrint=0;

// ISR
volatile unsigned long echoStart=0, echoDur=0;
volatile bool echoReady=false;

// Voltage FSM
enum VState { VS_IDLE,VS_DC,VS_AC,VS_DONE };
VState voltFSM=VS_IDLE;
float  vDcSum=0,vDcBias=0,vAcSumSq=0;
int    vDcN=0,vAcN=0;
unsigned long vTimer=0,vLastChk=0;

// Ultrasonic FSM
enum USState { US_IDLE2,US_TRIG,US_WAIT,US_CALC };
USState usState=US_IDLE2;
float   usBuf[US_SAMPLES];
int     usN=0,usIdx=0;
unsigned long usTmr=0,usLastRead=0;

// Leak / slope
MovAvg  maLevel;
MovAvgV maVolt;
float   levelHistory[SLOPE_WINDOW];
unsigned long levelHistoryTime[SLOPE_WINDOW];
int     levelHistIdx=0;

// NVS secrets
char g_hmacSecret[64]="ChangeThisToA32CharRandomSecret!";
char g_otaPass[64]   ="Str0ng-OTA-Pass!";
char g_brokerCA[4096]="";

// ============================================================
//  ROBOT ANIMATION STATE
// ============================================================
enum Mood { MOOD_IDLE, MOOD_WORKING, MOOD_HAPPY, MOOD_TIRED };
Mood r_mood = MOOD_IDLE;
int r_frame = 0;
float r_pumpHandleY = 0;
bool r_pumpingDown = true;
float r_waveOffset = 0;
struct WaterDrop { float x, y, vy; int life; };
WaterDrop r_drops[8];

// ============================================================
//  UTILITY MACROS
// ============================================================
#define TAKE_MUTEX(m,ms) (xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(ms)) == pdTRUE)
#define GIVE_MUTEX(m)     xSemaphoreGiveRecursive(m)

// ============================================================
//  EEPROM HELPERS
// ============================================================
uint8_t crc8(const uint8_t* d, size_t n){
  uint8_t c=0xFF;
  for(size_t i=0;i<n;i++){c^=d[i];for(uint8_t b=0;b<8;b++)c=(c&0x80)?(c<<1)^0x31:(c<<1);}
  return c;
}
void saveSettings(){
  cfg.crc=crc8((const uint8_t*)&cfg,sizeof(Settings)-1);
  EEPROM.write(EE_MAGIC_A,EE_MAGIC);
  EEPROM.put(EE_SETTINGS,cfg);
  EEPROM.commit();
}
void loadSettings(){
  if(EEPROM.read(EE_MAGIC_A)!=EE_MAGIC){saveSettings();return;}
  Settings t; EEPROM.get(EE_SETTINGS,t);
  if(t.crc==crc8((const uint8_t*)&t,sizeof(Settings)-1))cfg=t;
  else saveSettings();
}
void saveHealth(){
  PumpHealth oldH; EEPROM.get(EE_HEALTH,oldH);
  health.crc=crc8((const uint8_t*)&health,sizeof(PumpHealth)-1);
  if(oldH.runSecs!=health.runSecs||oldH.cycles!=health.cycles){EEPROM.put(EE_HEALTH,health);EEPROM.commit();}
}
void loadHealth(){
  PumpHealth h; EEPROM.get(EE_HEALTH,h);
  if(h.crc==crc8((const uint8_t*)&h,sizeof(PumpHealth)-1))health=h;
}
void saveLitres(){
  float oldL; EEPROM.get(EE_LITRES,oldL);
  if(isnan(oldL)||fabsf(oldL-totalLitres)>0.5f){EEPROM.put(EE_LITRES,totalLitres);EEPROM.commit();}
}
void loadLitres(){
  EEPROM.get(EE_LITRES,totalLitres);
  if(isnan(totalLitres)||totalLitres<0)totalLitres=0;
}
void saveUsage(){
  float oldU[24]; EEPROM.get(EE_USAGE,oldU);
  bool changed=false;
  for(int i=0;i<24;i++)if(isnan(oldU[i])||fabsf(oldU[i]-hourlyUsage[i])>0.1f)changed=true;
  if(changed){EEPROM.put(EE_USAGE,(float*)hourlyUsage);EEPROM.commit();}
}
void loadUsage(){
  float buf[24]; EEPROM.get(EE_USAGE,buf);
  for(int i=0;i<24;i++)hourlyUsage[i]=(isnan(buf[i])||buf[i]<0)?0.0f:buf[i];
}
void loadSecrets(){
  prefs.begin(NVS_NS,false);
  if(prefs.isKey("hmac_key"))prefs.getString("hmac_key",g_hmacSecret,sizeof(g_hmacSecret));
  if(prefs.isKey("ota_pass")) prefs.getString("ota_pass", g_otaPass,  sizeof(g_otaPass));
  if(prefs.isKey("broker_ca"))prefs.getString("broker_ca",g_brokerCA, sizeof(g_brokerCA));
  prefs.end();
}

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
void pushAlert(AlertLevel lv,AlertType ty,const char* msg,const char* detail="");
void clearAlertType(AlertType ty);
void clearAllAlerts();
void playBuzzer(int pat,bool repeat=false);
void stopBuzzer();
void setLED(bool r,bool g,bool b);
void setLEDColor(uint8_t r,uint8_t g,uint8_t b);
bool isVoltSafe(float v);
void emergencyStop(const char* reason);
void requestPumpStop(const char* reason="requested");
void startPump(const char* reason="requested");
bool canStartMotor(char* reason);
bool inSchedule();
bool isNight();
bool isSleepTime();
void doNightSleep();
void updateFillEta();
void dismissAlert();

// ============================================================
//  ISR
// ============================================================
void IRAM_ATTR echoISR(){
  if(digitalRead(ECHO_PIN))echoStart=micros();
  else{ unsigned long d=micros()-echoStart; if(d>0&&d<US_TIMEOUT_US){echoDur=d;echoReady=true;} }
}

// ============================================================
//  HELPERS
// ============================================================
void setLED(bool r,bool g,bool b){
  digitalWrite(LED_R,r);digitalWrite(LED_G,g);digitalWrite(LED_B,b);
}
bool isVoltSafe(float v){ return v>=cfg.safeVLow&&v<=cfg.safeVHigh; }

bool isNight(){
  struct tm ti; if(!getLocalTime(&ti))return false;
  return(ti.tm_hour>=22||ti.tm_hour<7);
}
bool isMotorNightScene(){
  struct tm ti; if(!getLocalTime(&ti))return false;
  int mins=ti.tm_hour*60+ti.tm_min;
  return(mins>=18*60+30||mins<6*60+30);
}
bool inSchedule(){
  if(!cfg.schedEn)return true;
  struct tm ti; if(!getLocalTime(&ti))return true;
  return(ti.tm_hour>=MOTOR_BLOCK_END&&ti.tm_hour<MOTOR_BLOCK_START);
}
bool isSleepTime(){
  struct tm ti; if(!getLocalTime(&ti))return false;
  return(ti.tm_hour>=NIGHT_SLEEP_HOUR||ti.tm_hour<WAKE_HOUR);
}
uint64_t secsUntilWake(){
  struct tm ti; if(!getLocalTime(&ti))return(uint64_t)WAKE_HOUR*3600;
  int nowSec=ti.tm_hour*3600+ti.tm_min*60+ti.tm_sec;
  int wakeSec=WAKE_HOUR*3600;
  if(nowSec<wakeSec)return(uint64_t)(wakeSec-nowSec);
  return(uint64_t)(86400-nowSec+wakeSec);
}

// ============================================================
//  ALERT
// ============================================================
static void recountActiveAlerts(){
  int n=0;
  for(int i=0;i<ALERT_HISTORY;i++) if(alertHistory[i].active) n++;
  activeAlerts=n;
}

void pushAlert(AlertLevel lv,AlertType ty,const char* msg,const char* detail){
  if(TAKE_MUTEX(xStateMutex,20)){
    int i=-1;
    if(ty!=AT_NONE){
      for(int n=0;n<ALERT_HISTORY;n++){
        if(alertHistory[n].active&&alertHistory[n].type==ty){ i=n; break; }
      }
    }
    if(i<0){
      i=alertHead%ALERT_HISTORY;
      alertHead++;
    }
    alertHistory[i].level=lv; alertHistory[i].type=ty;
    alertHistory[i].active=true;
    strncpy(alertHistory[i].msg,msg,31); alertHistory[i].msg[31]='\0';
    strncpy(alertHistory[i].detail,detail?detail:"",39); alertHistory[i].detail[39]='\0';
    struct tm ti;
    if(getLocalTime(&ti)){ alertHistory[i].hour=ti.tm_hour; alertHistory[i].minute=ti.tm_min; }
    else { alertHistory[i].hour=0; alertHistory[i].minute=0; }
    recountActiveAlerts();
    if(currentScreen==SCR_ALERTS)uiRedraw=true;
    GIVE_MUTEX(xStateMutex);
  }
  INFO("ALERT [%s]: %s - %s",
    lv==AL_CRITICAL?"CRIT":lv==AL_WARNING?"WARN":"INFO", msg, detail?detail:"");
}

void clearAlertType(AlertType ty){
  if(TAKE_MUTEX(xStateMutex,20)){
    bool changed=false;
    for(int i=0;i<ALERT_HISTORY;i++){
      if(alertHistory[i].active&&alertHistory[i].type==ty){
        alertHistory[i].active=false;
        changed=true;
      }
    }
    if(changed){
      recountActiveAlerts();
      if(currentScreen==SCR_ALERTS)uiRedraw=true;
    }
    GIVE_MUTEX(xStateMutex);
  }
}

void clearAllAlerts(){
  if(TAKE_MUTEX(xStateMutex,20)){
    for(int i=0;i<ALERT_HISTORY;i++) alertHistory[i].active=false;
    activeAlerts=0;
    if(currentScreen==SCR_ALERTS)uiRedraw=true;
    GIVE_MUTEX(xStateMutex);
  }
}

void dismissAlert(){
  if(TAKE_MUTEX(xStateMutex,20)){
    alertDismissed=true; veryLow=false; voltWarn=false; voltWarnMuted=false;
    pumpTimedOut=false; leakConfirmed=false; leakClearCount=0;
    tankFullAlerted=false;
    manualFullAlarm=false;
    sysState=SYS_IDLE;
    clearAllAlerts();
    setLED(false,false,false);
    GIVE_MUTEX(xStateMutex);
  }
  stopBuzzer();
  INFO("Alert dismissed");
}

// ============================================================
//  BUZZER  [V8-5: Only interactive sounds, no auto-mode alerts]
// ============================================================
void playBuzzer(int pat,bool repeat){
  BzRequest req={pat,repeat};
  xQueueOverwrite(xBuzzerQueue,&req);
}
void stopBuzzer(){
  BzRequest req={PAT_IDLE,false};
  xQueueOverwrite(xBuzzerQueue,&req);
}

static const BzNote* patternForId(int id){
  switch(id){
    case PAT_CLICK:       return SND_CLICK;
    case PAT_START:       return SND_START;
    case PAT_STOP:        return SND_STOP;
    case PAT_INFO:        return SND_INFO;
    case PAT_MAINT:       return SND_MAINT;
    case PAT_RESET_DAILY: return SND_RESET_DAILY;
    case PAT_ERROR:       return SND_ERROR;
    case PAT_SLEEP:       return SND_SLEEP;
    case PAT_BLOCK:       return SND_BLOCK;
    case PAT_AUTO_DONE:   return SND_AUTO_DONE;
    case PAT_MANUAL_FULL: return SND_MANUAL_FULL;
    case PAT_EMERGENCY:   return SND_EMERGENCY;
    default:              return nullptr;
  }
}

void BuzzerTask(void* pv){
  ledcSetup(BZ_CH,1000,BZ_RES);
  ledcAttachPin(BUZZ_PIN,BZ_CH);
  ledcWrite(BZ_CH,0);
  const BzNote* seq=nullptr; bool rep=false;
  uint8_t duty=180;
  int noteIdx=0; unsigned long noteEnd=0;
  for(;;){
    BzRequest req;
    if(xQueueReceive(xBuzzerQueue,&req,0)==pdTRUE){
      ledcWrite(BZ_CH,0); ledcWriteTone(BZ_CH,0);
      if(req.pat==PAT_IDLE){ seq=nullptr; rep=false; noteIdx=0; }
      else{
        const BzNote* ns=patternForId(req.pat);
        if(ns){
          seq=ns; rep=req.repeat; noteIdx=0; noteEnd=0;
          duty=(req.pat==PAT_MANUAL_FULL||req.pat==PAT_EMERGENCY)?255:180;
        }
      }
    }
    if(seq&&millis()>=noteEnd){
      const BzNote& n=seq[noteIdx];
      if(n.ms==0&&n.freq==0){
        if(rep){ noteIdx=0; noteEnd=millis()+200; }
        else{ ledcWrite(BZ_CH,0); ledcWriteTone(BZ_CH,0); seq=nullptr; }
      } else {
        if(n.freq>0){ ledcWriteTone(BZ_CH,n.freq); ledcWrite(BZ_CH,duty); }
        else { ledcWrite(BZ_CH,0); ledcWriteTone(BZ_CH,0); }
        noteEnd=millis()+n.ms; noteIdx++;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ============================================================
//  PUMP CONTROL
// ============================================================
bool canStartMotor(char* reason){
  if(sysState==SYS_ERROR||leakConfirmed){
    strncpy(reason,"Error active",63); return false;
  }
  if(voltWarn&&!voltWarnMuted){
    snprintf(reason,63,"Unsafe voltage! %.0fV",smoothVoltage); return false;
  }
  if(!isVoltSafe(smoothVoltage)&&opMode!=MODE_MANUAL){
    snprintf(reason,63,"Voltage out of range %.0fV",smoothVoltage); return false;
  }
  if(smoothLevel>TANK_FORCE_ON_MAX&&opMode!=MODE_MANUAL){
    snprintf(reason,63,"Tank already >%.0f%%",TANK_FORCE_ON_MAX); return false;
  }
  return true;
}

void emergencyStop(const char* reason){
  if(pumpRunning)health.runSecs+=(millis()-pumpOnTime)/1000;
  strncpy(pumpStopReason,reason?reason:"emergency stop",sizeof(pumpStopReason)-1);
  pumpStopReason[sizeof(pumpStopReason)-1]='\0';
  digitalWrite(RELAY_PIN,LOW);
  pumpRunning=pumpStartPend=stopPending=false;
  manualFullAlarm=false;
  pumpStopTime=millis(); stopBuzzer(); setLED(true,false,false);
  sysState=SYS_ERROR;
  PUMP("Motor OFF - reason=%s",pumpStopReason);
  ERR("Emergency stop: %s",reason);
  if(mqtt.connected())mqtt.publish(T_ALERT,reason);
}

void requestPumpStop(const char* reason){
  if(stopPending||!pumpRunning)return;
  strncpy(pumpStopReason,reason?reason:"requested",sizeof(pumpStopReason)-1);
  pumpStopReason[sizeof(pumpStopReason)-1]='\0';
  stopPending=true; stopPendTime=millis();
  playBuzzer(opMode==MODE_AUTO ? PAT_AUTO_DONE : PAT_STOP);
  PUMP("Stop requested: %s",pumpStopReason);
}

void startPump(const char* reason){
  if(pumpRunning||pumpStartPend||stopPending)return;
  strncpy(pumpStartReason,reason?reason:"requested",sizeof(pumpStartReason)-1);
  pumpStartReason[sizeof(pumpStartReason)-1]='\0';
  char blockReason[64]="";
  if(!canStartMotor(blockReason)){
    pushAlert(AL_WARNING,AT_NONE,blockReason);
    if(opMode==MODE_MANUAL)playBuzzer(PAT_BLOCK); // [V8-5]
    WARN("Motor blocked (%s): %s",pumpStartReason,blockReason);
    uiRedraw=true; return;
  }
  pumpStartPend=true;
  playBuzzer(PAT_START);
  PUMP("Start requested: %s",pumpStartReason);
}

static unsigned long pendStart=0;
void updatePumpShutdown(){
  if(pumpStartPend){
    char reason[64]="";
    if(!canStartMotor(reason)){
      PUMP("Motor ON cancelled - requested=%s block=%s",pumpStartReason,reason);
      pumpStartPend=false; pendStart=0; return;
    }
    if(pendStart==0)pendStart=millis();
    if(millis()-pendStart<450)return;
    pendStart=0;
    digitalWrite(RELAY_PIN,HIGH);
    pumpRunning=true; pumpStartPend=false;
    pumpOnTime=millis(); pumpTimedOut=false;
    levelAtStart=smoothLevel; pumpToutChk=millis();
    dryRunTime=millis()+20000UL; dryRunChk=false;
    health.cycles++; usageUpd=millis();
    updateFillEta();
    uiRedraw=true;
    PUMP("Motor ON - reason=%s level=%.1f%% volt=%.0fV",
         pumpStartReason,smoothLevel,smoothVoltage);
    if(mqtt.connected())mqtt.publish(T_PUMP,"ON",true);
  }
  if(stopPending&&millis()-stopPendTime>=3000UL){
    float rise=smoothLevel-levelAtStart;
    float mins=(millis()-pumpOnTime)/60000.0f;
    if(mins>0.5f&&rise>1.0f){
      float meas=(rise/100.0f)*cfg.tankVolLitres/mins;
      learnedLPM=(learnedLPM*lpmSamples+meas)/(lpmSamples+1);
      if(lpmSamples<20)lpmSamples++;
    }
    digitalWrite(RELAY_PIN,LOW);
    health.runSecs+=(millis()-pumpOnTime)/1000;
    pumpRunning=stopPending=false; pumpStopTime=millis();
    stopBuzzer();
    uiRedraw=true;
    PUMP("Motor OFF - reason=%s ran %.0fs",
         pumpStopReason,(millis()-pumpOnTime)/1000.0f);
    if(mqtt.connected())mqtt.publish(T_PUMP,"OFF",true);
    saveLitres(); saveHealth();
  }
}

void updateFillEta(){
  if(!pumpRunning||smoothLevel>=cfg.highThr){ fillEtaMin=0; return; }
  float need=cfg.highThr-smoothLevel;
  float rate=(learnedLPM>0)?learnedLPM:cfg.lpm;
  if(rate>0){
    float litresNeeded=(need/100.0f)*cfg.tankVolLitres;
    fillEtaMin=litresNeeded/rate;
  } else fillEtaMin=0;
}

void updateUsage(){
  if(!pumpRunning)return;
  unsigned long now=millis();
  float mins=(now-usageUpd)/60000.0f;
  float rate=(learnedLPM>0)?learnedLPM:cfg.lpm;
  float litres=rate*mins;
  totalLitres+=litres; dailyUsage+=litres;
  struct tm ti; if(getLocalTime(&ti))hourlyUsage[ti.tm_hour]+=litres;
  usageUpd=now;
}

void predictNextLevel(){
  struct tm ti; if(!getLocalTime(&ti)){predictedLevel=smoothLevel;return;}
  float est=smoothLevel;
  uint32_t cycles=max((uint32_t)1,health.cycles);
  for(int h=0;h<2;h++){
    int hr=(ti.tm_hour+h)%24;
    float avgL=hourlyUsage[hr]/(float)cycles;
    est-=(avgL/cfg.tankVolLitres)*100.0f;
  }
  predictedLevel=constrain(est,0.0f,100.0f);
}

// ============================================================
//  NIGHT DEEP SLEEP
// ============================================================
void doNightSleep(){
  SLEEP("=== NIGHT SLEEP ===");
  if(pumpRunning){
    requestPumpStop("night sleep");
    unsigned long t=millis();
    while(pumpRunning&&millis()-t<5000)vTaskDelay(pdMS_TO_TICKS(100));
    if(pumpRunning){
      digitalWrite(RELAY_PIN,LOW); pumpRunning=false;
      PUMP("Motor forced OFF - reason=night sleep relay cutoff");
    }
  }
  playBuzzer(PAT_SLEEP); vTaskDelay(pdMS_TO_TICKS(700));
  stopBuzzer();
  saveLitres(); saveHealth(); saveUsage();
  if(mqtt.connected()){
    mqtt.publish(T_STATUS,"sleeping",true);
    mqtt.publish(T_PUMP,"OFF",true);
    vTaskDelay(pdMS_TO_TICKS(200));
    mqtt.disconnect();
  }
  if(TAKE_MUTEX(xTftMutex,500)){
    tft.fillScreen(C_BG);
    // Professional sleep screen
    tft.fillRoundRect(20,20,120,88,8,C_DKGRAY);
    tft.drawRoundRect(20,20,120,88,8,C_CYAN);
    // Moon icon
    tft.fillCircle(80,42,12,C_YELLOW);
    tft.fillCircle(86,38,10,C_DKGRAY);
    tft.setTextColor(C_CYAN,C_BG); tft.setTextSize(1);
    tft.setCursor(42,56); tft.print(F("GOING TO SLEEP"));
    tft.setTextColor(C_WHITE,C_BG);
    tft.setCursor(38,70); tft.print(F("Wake: 06:00 AM"));
    tft.setCursor(44,82); tft.print(F("Good night"));
    struct tm ti; if(getLocalTime(&ti)){
      char tb[12]; snprintf(tb,12,"Now: %02d:%02d",ti.tm_hour,ti.tm_min);
      tft.setTextColor(C_GRAY,C_BG);
      tft.setCursor(44,96); tft.print(tb);
    }
    GIVE_MUTEX(xTftMutex);
  }
  vTaskDelay(pdMS_TO_TICKS(5000));
  digitalWrite(TFT_BL,LOW);
  digitalWrite(TRIG_PIN,LOW);
  digitalWrite(TFT_CS,LOW);
  digitalWrite(TFT_DC,LOW);
  digitalWrite(TFT_RST,LOW);
  pinMode(TRIG_PIN,INPUT);
  pinMode(ECHO_PIN,INPUT);
  pinMode(TFT_CS,INPUT);
  pinMode(TFT_DC,INPUT);
  pinMode(TFT_RST,INPUT);
  digitalWrite(PERIPH_5V_EN,LOW);
  gpio_hold_en((gpio_num_t)PERIPH_5V_EN);
  gpio_hold_en((gpio_num_t)RELAY_PIN);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN1_PIN,0);
  uint64_t sleepUs=secsUntilWake()*1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  SLEEP("Entering deep sleep. Good night!");
  Serial.flush();
  esp_deep_sleep_start();
}

// ============================================================
//  SENSOR TASK
// ============================================================
void SensorTask(void* pv){
  for(;;){
    hbSensor=millis();
    if(usState==US_IDLE2){
      if(millis()-usLastRead>=1000){ usLastRead=millis(); usIdx=0; usN=0; usState=US_TRIG; }
    }
    if(usState==US_TRIG){
      echoReady=false;
      digitalWrite(TRIG_PIN,LOW); delayMicroseconds(2);
      digitalWrite(TRIG_PIN,HIGH); delayMicroseconds(10);
      digitalWrite(TRIG_PIN,LOW);
      usTmr=millis(); usState=US_WAIT;
    }
    if(usState==US_WAIT){
      portDISABLE_INTERRUPTS();
      bool rdy=echoReady; unsigned long dur=echoDur;
      if(rdy)echoReady=false;
      portENABLE_INTERRUPTS();
      if(rdy){
        float d=(dur*0.0343f)/2.0f;
        if(d>=0&&d<=cfg.tankH+SENSOR_OFF_CM+10)usBuf[usN++]=d;
        usIdx++; usState=(usIdx>=US_SAMPLES)?US_CALC:(millis()-usTmr>50)?US_TRIG:US_WAIT;
      } else if(millis()-usTmr>40){ usIdx++; usState=(usIdx>=US_SAMPLES)?US_CALC:US_TRIG; }
    }
    if(usState==US_CALC){
      if(usN>0){
        for(int i=1;i<usN;i++){float k=usBuf[i];int j=i-1;while(j>=0&&usBuf[j]>k){usBuf[j+1]=usBuf[j];j--;}usBuf[j+1]=k;}
        float d=usBuf[usN/2];
        lastDistanceCm=d;
        float h=cfg.tankH-(d-SENSOR_OFF_CM);
        float lvl=constrain(h/cfg.tankH*100.0f,0.0f,100.0f);
        if(TAKE_MUTEX(xStateMutex,10)){
          waterPrev=waterLevel; waterLevel=lvl;
          maLevel.push(waterLevel); smoothLevel=maLevel.avg();
          GIVE_MUTEX(xStateMutex);
        }
        DBG("Sensor: %.1fcm → %.1f%%",d,smoothLevel);
      }
      usState=US_IDLE2;
    }

    // Voltage FSM
    if(voltFSM==VS_IDLE){
      if(millis()-vLastChk>=VOLT_CHK_MS){ vLastChk=millis(); vDcSum=0; vDcN=0; vAcSumSq=0; vAcN=0; voltFSM=VS_DC; vTimer=micros(); }
    } else {
      if(micros()-vTimer>=(unsigned long)VOLT_SAMP_US){
        vTimer=micros();
        if(voltFSM==VS_DC){ vDcSum+=(analogRead(VOLT_PIN)/4095.0f)*3.3f; vDcN++; if(vDcN>=VOLT_DC_SAMP){vDcBias=vDcSum/vDcN;voltFSM=VS_AC;} }
        else if(voltFSM==VS_AC){ float v=((analogRead(VOLT_PIN)/4095.0f)*3.3f-vDcBias); vAcSumSq+=v*v; vAcN++; if(vAcN>=VOLT_AC_SAMP)voltFSM=VS_DONE; }
        else if(voltFSM==VS_DONE){
          float aRms=sqrtf(vAcSumSq/vAcN);
          float cv=(aRms*VOLT_CALIB);
          if(cv<0||cv>800)cv=0;
          if(TAKE_MUTEX(xStateMutex,10)){
            currentVoltage=cv; maVolt.push(cv); smoothVoltage=maVolt.avg();
            GIVE_MUTEX(xStateMutex);
          }
          if(millis()-lastVoltPrint>=1000){
            lastVoltPrint=millis();
            VOLT("AC=%.1fV smooth=%.1fV %s",currentVoltage,smoothVoltage,isVoltSafe(smoothVoltage)?"OK":"FAULT");
          }
          if(maVolt.full()&&millis()-bootTimeMs>12000&&!voltWarnMuted){
            if(!isVoltSafe(smoothVoltage)){
              if(!voltIsAbnormal){voltIsAbnormal=true;voltAbnormalTimer=millis();}
              else if(millis()-voltAbnormalTimer>=5000){
                if(pumpRunning){emergencyStop("Voltage unsafe while running");}
                if(!voltWarn&&!voltWarnMuted){
                  voltWarn=true;
                  char det[40]; snprintf(det,40,"%.0fV (safe:%.0f-%.0fV)",smoothVoltage,cfg.safeVLow,cfg.safeVHigh);
                  pushAlert(AL_CRITICAL,AT_VOLTAGE,"High AC Voltage",det);
                  // [V8-5] No buzzer in auto mode for voltage alert
                  if(opMode==MODE_MANUAL)playBuzzer(PAT_ERROR);
                  setLED(true,false,false);
                }
              }
            } else {
              voltIsAbnormal=false;
              if(voltWarn||voltWarnMuted){
                voltWarn=false; voltWarnMuted=false;
                clearAlertType(AT_VOLTAGE);
              }
            }
          }
          voltFSM=VS_IDLE;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ============================================================
//  CONTROL TASK
// ============================================================
void pushLevelHistory(float lvl){
  levelHistory[levelHistIdx]=lvl;
  levelHistoryTime[levelHistIdx]=millis();
  levelHistIdx=(levelHistIdx+1)%SLOPE_WINDOW;
  int oldest=levelHistIdx;
  float dL=levelHistory[(levelHistIdx-1+SLOPE_WINDOW)%SLOPE_WINDOW]-levelHistory[oldest];
  float dT=(levelHistoryTime[(levelHistIdx-1+SLOPE_WINDOW)%SLOPE_WINDOW]-levelHistoryTime[oldest])/60000.0f;
  currentSlope=(dT>0.1f)?dL/dT:0.0f;
}

void updateSystemState(){
  SystemState prev=sysState;
  if(sysState==SYS_ERROR){ if(!pumpRunning&&usN>0&&!leakConfirmed)sysState=SYS_IDLE; else return; }
  if(pumpRunning)sysState=SYS_REFILLING;
  else if(leakConfirmed)sysState=SYS_CONFIRMED_LEAK;
  else if(lastLeakScore>=4&&lastLeakScore<SCORE_THRESHOLD)sysState=SYS_POSSIBLE_LEAK;
  else if(currentSlope<-0.5f)sysState=SYS_NORMAL_USE;
  else sysState=SYS_IDLE;
  if(sysState!=prev)INFO("State: %s→%s",sysStateStr[prev],sysStateStr[sysState]);
}

void updateLeakScore(){
  if(sysState==SYS_ERROR)return;
  if(pumpRunning){ leakSnap=smoothLevel; leakTotalDrop=0; leakTimer=millis(); pauseTimer=millis(); leakConfirmed=false; lastLeakScore=0; return; }
  if(millis()-leakTimer<LEAK_EVAL_MS)return;
  leakTimer=millis();
  float drop=leakSnap-smoothLevel; leakSnap=smoothLevel;
  if(drop>0)leakTotalDrop+=drop;
  else leakTotalDrop=max(0.0f,leakTotalDrop-0.5f);
  if(fabsf(drop)<=LEAK_PAUSE_STABLE)pauseTimer=millis();
  bool noPause=(millis()-pauseTimer>120000UL);
  int score=0;
  if(currentSlope<-0.05f)score+=2;
  if(noPause)score+=2;
  if(currentSlope<LEAK_SLOPE_MAX&&currentSlope>LEAK_SLOPE_MIN)score+=2;
  if(leakTotalDrop>LEAK_TOTAL_DROP_THR)score+=1;
  lastLeakScore=score;
  if(score>=SCORE_THRESHOLD&&!leakConfirmed){
    leakConfirmed=true; sysState=SYS_CONFIRMED_LEAK;
    pushAlert(AL_CRITICAL,AT_LEAK,"Leak Detected","Possible leakage detected");
    setLED(true,false,true);
    // [V8-5] No repeat buzzer in auto mode for leaks
    if(opMode==MODE_MANUAL)playBuzzer(PAT_ERROR);
  } else if(score<3&&leakConfirmed){
    leakClearCount++;
    if(leakClearCount>=2){
      leakConfirmed=false; leakClearCount=0; leakTotalDrop=0;
      clearAlertType(AT_LEAK);
    }
  } else if(leakConfirmed)leakClearCount=0;
}

void checkWaterLimits(){
  if(sysState==SYS_ERROR)return;
  // Very low / dry run — only buzzer in manual
  if(smoothLevel<10.0f&&!pumpRunning&&!veryLow&&!veryLowMuted&&maLevel.full()&&millis()-bootTimeMs>10000){
    veryLow=true;
    if(opMode==MODE_MANUAL)playBuzzer(PAT_ERROR); // [V8-5]
    setLED(true,false,false);
    pushAlert(AL_CRITICAL,AT_DRY_RUN,"Dry Run Risk","Water critically low");
    WARN("Water VERY LOW: %.1f%%",smoothLevel);
  } else if(smoothLevel>15.0f&&(veryLow||veryLowMuted)){
    veryLow=veryLowMuted=false;
    clearAlertType(AT_DRY_RUN);
  }
  // Tank full — push alert but NO buzzer in auto mode [V8-5]
  if(opMode==MODE_MANUAL&&pumpRunning&&!stopPending&&smoothLevel>=cfg.highThr){
    if(!manualFullAlarm){
      manualFullAlarm=true;
      manualFullAlarmAt=millis();
      pushAlert(AL_CRITICAL,AT_HIGH_LEVEL,"Tank Full","Manual motor still ON");
      playBuzzer(PAT_MANUAL_FULL,true);
      setLED(true,false,false);
      uiRedraw=true;
      WARN("Manual full alarm: %.1f%%",smoothLevel);
    } else if(millis()-manualFullAlarmAt>=MANUAL_FULL_RESPONSE_MS){
      manualFullAlarm=false;
      emergencyStop("Manual full - no response");
      pushAlert(AL_CRITICAL,AT_HIGH_LEVEL,"Emergency Stop","Tank full, motor OFF");
      playBuzzer(PAT_EMERGENCY);
      uiRedraw=true;
      return;
    }
  } else if(manualFullAlarm&&(!pumpRunning||smoothLevel<cfg.highThr)){
    manualFullAlarm=false;
    stopBuzzer();
  }

  if(smoothLevel>=cfg.highThr+5.0f&&!tankFullAlerted){
    tankFullAlerted=true;
    pushAlert(AL_WARNING,AT_HIGH_LEVEL,"High Water Level","Tank level above 90%");
    if(opMode==MODE_MANUAL&&!manualFullAlarm)playBuzzer(PAT_INFO); // [V8-5]
    uiRedraw=true;
    if(opMode==MODE_MANUAL&&pumpRunning&&!manualFullAlarm)requestPumpStop("manual full level");
    INFO("Tank FULL: %.1f%%",smoothLevel);
  }
  if(smoothLevel<cfg.highThr){
    tankFullAlerted=false;
    if(!manualFullAlarm)clearAlertType(AT_HIGH_LEVEL);
  }
}

void controlPump(){
  if(leakConfirmed){ if(pumpRunning)requestPumpStop("leak detected"); return; }
  if(stopPending||opMode!=MODE_AUTO)return;
  if(!inSchedule()){ if(pumpRunning)requestPumpStop("outside schedule"); return; }
  bool restOk=(millis()-pumpStopTime>=MIN_REST_MS);
  if(!pumpRunning&&!pumpStartPend&&!pumpTimedOut&&restOk){
    if(smoothLevel<(cfg.lowThr-HYSTERESIS))startPump("auto low level");
    else if(predictedLevel<cfg.lowThr)startPump("predicted low level");
  }
  if(pumpRunning&&!stopPending){
    bool runOk=(millis()-pumpOnTime>=MIN_RUN_MS);
    if(runOk&&smoothLevel>=(cfg.highThr+HYSTERESIS))requestPumpStop("auto high level reached");
  }
}

// [V8-6] LED color by water level
void updateLEDs(){
  if(sysState==SYS_ERROR||leakConfirmed||voltWarn){
    setLED(true,false,false); // Red — error
  } else if(pumpRunning){
    setLED(false,true,true);  // Cyan — pumping
  } else if(smoothLevel>=cfg.highThr){
    setLED(false,true,false); // Green — full
  } else if(smoothLevel>=cfg.lowThr){
    setLED(false,false,true); // Blue — normal
  } else if(smoothLevel>=10.0f){
    setLED(true,true,false);  // Yellow — low
  } else {
    setLED(true,false,false); // Red — critical
  }
}

void updatePumpTimeout(){
  if(!pumpRunning||millis()-pumpToutChk<PUMP_CHK_MS)return;
  pumpToutChk=millis();
  float elapsed=(millis()-pumpOnTime)/1000.0f;
  float rise=smoothLevel-levelAtStart;
  if(elapsed>=(PUMP_TIMEOUT_MS/1000.0f)&&rise<PUMP_MIN_RISE){
    pumpTimedOut=true;
    emergencyStop("Pump timeout - no level rise");
    pushAlert(AL_CRITICAL,AT_PUMP_TIMEOUT,"Pump Timeout","No level rise detected");
  }
}

void checkDryRun(){
  if(dryRunChk||!pumpRunning||millis()<dryRunTime)return;
  dryRunChk=true;
  if(smoothLevel-levelAtStart<1.0f){
    emergencyStop("Dry run — no rise after 20s");
    pushAlert(AL_CRITICAL,AT_DRY_RUN,"Dry Run Protected","Motor auto stopped");
  }
}

void ControlTask(void* pv){
  for(;;){
    hbControl=millis();
    if(TAKE_MUTEX(xStateMutex,20)){
      pushLevelHistory(smoothLevel);
      updateSystemState(); updateLeakScore();
      checkWaterLimits();
      if(opMode!=MODE_MAINTENANCE){
        controlPump();
        if(pumpRunning){
          updatePumpTimeout(); checkDryRun(); updateUsage();
          updateFillEta(); predictNextLevel();
          if(millis()-pumpOnTime>MAX_PUMP_MS){
            emergencyStop("Max runtime exceeded");
            pushAlert(AL_CRITICAL,AT_PUMP_MAXRUN,"Max Pump Runtime","Auto stopped for safety");
          }
        }
      }
      updatePumpShutdown();
      if(!pumpRunning&&!pumpStartPend&&!stopPending)digitalWrite(RELAY_PIN,LOW);

      if(g_pendingSave){ g_pendingSave=false; GIVE_MUTEX(xStateMutex); saveSettings(); TAKE_MUTEX(xStateMutex,20); }
      if(millis()-litresSaveTmr>300000){ litresSaveTmr=millis(); GIVE_MUTEX(xStateMutex); saveLitres();saveHealth();saveUsage(); TAKE_MUTEX(xStateMutex,20); }

      updateLEDs();

      if(millis()-lastDebugPrint>=5000){
        lastDebugPrint=millis();
        INFO("lvl=%.1f%% volt=%.1fV pump=%s mode=%s state=%s",
          smoothLevel,smoothVoltage,pumpRunning?"ON":"OFF",
          opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MANUAL":"MAINT",
          sysStateStr[sysState]);
      }

      if(isSleepTime()&&!sleepRequested&&!pumpRunning&&!pumpStartPend&&!leakConfirmed&&millis()-bootTimeMs>30000){
        sleepRequested=true;
        GIVE_MUTEX(xStateMutex);
        doNightSleep();
        TAKE_MUTEX(xStateMutex,20);
      }
      GIVE_MUTEX(xStateMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ============================================================
//  BUTTON TASK
// ============================================================
void adjustSettingsStep(int dir){
  switch(settingsIndex){
    case 0:
      opMode=(opMode==MODE_AUTO)?MODE_MANUAL:MODE_AUTO;
      cfg.autoMode=(opMode==MODE_AUTO);
      g_pendingSave=true;
      playBuzzer(PAT_INFO);
      break;
    case 1:
      cfg.lowThr+=5.0f*dir;
      if(cfg.lowThr>50.0f)cfg.lowThr=20.0f;
      if(cfg.lowThr<20.0f)cfg.lowThr=50.0f;
      if(cfg.highThr<cfg.lowThr+10.0f)cfg.highThr=min(95.0f,cfg.lowThr+10.0f);
      g_pendingSave=true;
      playBuzzer(PAT_CLICK);
      break;
    case 2:
      cfg.highThr+=5.0f*dir;
      if(cfg.highThr>95.0f)cfg.highThr=max(70.0f,cfg.lowThr+10.0f);
      if(cfg.highThr<cfg.lowThr+10.0f)cfg.highThr=95.0f;
      if(cfg.highThr<cfg.lowThr+10.0f)cfg.highThr=cfg.lowThr+10.0f;
      g_pendingSave=true;
      playBuzzer(PAT_CLICK);
      break;
    case 3:
      if(dir<0)break;
      dailyUsage=0;
      for(int i=0;i<24;i++)hourlyUsage[i]=0;
      saveUsage();
      playBuzzer(PAT_RESET_DAILY);
      break;
    case 4:
      cfg.tankH+=1.0f*dir;
      if(cfg.tankH>400.0f)cfg.tankH=30.0f;
      if(cfg.tankH<30.0f)cfg.tankH=400.0f;
      g_pendingSave=true;
      playBuzzer(PAT_CLICK);
      break;
  }
  uiRedraw=true;
}

void applySettingsStep(){
  adjustSettingsStep(1);
}

void checkDualHold(){
  bool b1=(digitalRead(BTN1_PIN)==LOW);
  bool b2=(digitalRead(BTN2_PIN)==LOW);
  if(b1&&!g_btn1Down){g_btn1Down=true;g_btn1DownAt=millis();g_btn1HoldStart=millis();}
  if(!b1){g_btn1Down=false;g_maintFired=false;g_btn1LongFired=false;g_btn1HoldStart=0;}
  if(b2&&!g_btn2Down){g_btn2Down=true;g_btn2DownAt=millis();}
  if(!b2){g_btn2Down=false;g_maintFired=false;g_btn2LongFired=false;g_btn2HoldStart=0;}
  if(g_btn1Down&&g_btn2Down&&!g_maintFired){
    unsigned long hs=max(g_btn1DownAt,g_btn2DownAt);
    if(millis()-hs>=(unsigned long)MAINT_HOLD_MS){
      g_maintFired=true;
      if(opMode==MODE_MAINTENANCE){
        opMode=MODE_AUTO; playBuzzer(PAT_MAINT); setLED(false,false,false);
        INFO("Exit MAINTENANCE");
      } else {
        if(pumpRunning){
          digitalWrite(RELAY_PIN,LOW);pumpRunning=false;
          PUMP("Motor forced OFF - reason=maintenance mode");
        }
        opMode=MODE_MAINTENANCE; playBuzzer(PAT_MAINT); setLED(false,false,true);
        INFO("Enter MAINTENANCE");
      }
      uiRedraw=true;
    }
  }
  if(g_btn1Down&&!g_btn2Down&&!g_btn1LongFired&&currentScreen==SCR_SETTINGS){
    if(g_btn1HoldStart==0)g_btn1HoldStart=millis();
    if(millis()-g_btn1HoldStart>=900){
      g_btn1LongFired=true;
      g_suppressBtn1Click=true;
      adjustSettingsStep(-1);
      INFO("Settings row %d decremented",settingsIndex);
    }
  }
  if(g_btn2Down&&!g_btn1Down&&!g_btn2LongFired){
    if(g_btn2HoldStart==0)g_btn2HoldStart=millis();
    if(millis()-g_btn2HoldStart>=2000&&currentScreen==SCR_MAIN){
      g_btn2LongFired=true;
      if(opMode==MODE_MANUAL){
        if(pumpRunning)requestPumpStop("return to auto");
        opMode=MODE_AUTO; cfg.autoMode=true; g_pendingSave=true;
        playBuzzer(PAT_INFO); uiRedraw=true;
        INFO("Forced AUTO mode");
      } else {
        dailyUsage=0; saveUsage();
        playBuzzer(PAT_RESET_DAILY); uiRedraw=true;
        INFO("Daily usage reset");
      }
    }
    if(millis()-g_btn2HoldStart>=2000&&currentScreen==SCR_SETTINGS){
      g_btn2LongFired=true;
      currentScreen=SCR_MAIN;
      playBuzzer(PAT_INFO); uiRedraw=true;
      INFO("Settings saved");
    }
  }
}

void handleButton(AceButton* btn,uint8_t evt,uint8_t){
  uint8_t pin=btn->getPin();
  if(evt==AceButton::kEventClicked)lastButtonTime=millis();
  if(evt!=AceButton::kEventClicked)return;
  if(pin==BTN1_PIN&&g_suppressBtn1Click){
    g_suppressBtn1Click=false;
    return;
  }

  playBuzzer(PAT_CLICK);
  DBG("Button %s clicked scr=%d",pin==BTN1_PIN?"BTN1":"BTN2",currentScreen);

  if(manualFullAlarm){
    manualFullAlarm=false;
    stopBuzzer();
    if(pumpRunning)requestPumpStop("manual full acknowledged");
    dismissAlert();
    uiRedraw=true;
    INFO("Manual full alarm acknowledged");
    return;
  }

  // Dismiss alerts on any button
  if(veryLow||voltWarn||pumpTimedOut||leakConfirmed){
    if(voltWarn)voltWarnMuted=true;
    leakConfirmed=false; leakClearCount=0;
    if(sysState==SYS_ERROR||sysState==SYS_CONFIRMED_LEAK)sysState=SYS_IDLE;
    dismissAlert(); uiRedraw=true;
    return;
  }

  if(opMode==MODE_MAINTENANCE)return;

  if(currentScreen==SCR_ALERTS&&pin==BTN2_PIN){
    dismissAlert();
    currentScreen=SCR_MAIN;
    uiRedraw=true;
    return;
  }

  if(pin==BTN1_PIN){
    if(currentScreen==SCR_SETTINGS){
      settingsIndex++;
      if(settingsIndex>=SETTINGS_COUNT){
        settingsIndex=0;
        currentScreen=SCR_MAIN;
        uiRedraw=true;
        INFO("Exit settings");
      } else {
        uiRedraw=true;
        INFO("Settings row %d",settingsIndex);
      }
    } else {
      currentScreen=(currentScreen+1)%SCREEN_COUNT;
      uiRedraw=true;
      INFO("Screen %d",currentScreen+1);
    }
    return;
  }

  if(currentScreen==SCR_SETTINGS){
    applySettingsStep();
    return;
  }

  if(currentScreen==SCR_MAIN){
    if(opMode==MODE_AUTO){
      opMode=MODE_MANUAL;
      cfg.autoMode=false;
      g_pendingSave=true;
      INFO("Mode MANUAL");
    } else if(opMode==MODE_MANUAL){
      if(pumpRunning)requestPumpStop("manual button"); else startPump("manual button");
    }
    uiRedraw=true;
    return;
  }

  currentScreen=(currentScreen-1+SCREEN_COUNT)%SCREEN_COUNT;
  uiRedraw=true;
  return;

  if(pin==BTN1_PIN){
    currentScreen=(currentScreen+1)%SCREEN_COUNT;
    uiRedraw=true;
    INFO("Screen→%d",currentScreen+1);
  } else {
    switch(currentScreen){
      case 0: // MAIN: toggle mode
        opMode=(opMode==MODE_AUTO)?MODE_MANUAL:MODE_AUTO;
        cfg.autoMode=(opMode==MODE_AUTO); g_pendingSave=true;
        INFO("Mode→%s",opMode==MODE_AUTO?"AUTO":"MANUAL");
        uiRedraw=true;
        break;
      case 1: // MOTOR CONTROL
        if(opMode==MODE_MANUAL){
          if(pumpRunning)requestPumpStop("manual button"); else startPump("manual button");
        } else {
          currentScreen=(currentScreen-1+SCREEN_COUNT)%SCREEN_COUNT;
          uiRedraw=true;
        }
        break;
      default:
        currentScreen=(currentScreen-1+SCREEN_COUNT)%SCREEN_COUNT;
        uiRedraw=true;
        break;
    }
  }
}

void ButtonTask(void* pv){
  for(;;){
    hbButton=millis();
    btn1.check(); btn2.check(); checkDualHold();
    if(currentScreen!=SCR_MAIN&&millis()-lastButtonTime>AUTO_RETURN_MS&&millis()-bootTimeMs>20000){
      currentScreen=SCR_MAIN; uiRedraw=true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
//  DISPLAY HELPERS  [V9 — Professional UI matching reference]
// ============================================================

// ── Inline centred text helper ────────────────────────────
static void tftCentredText(const char* s, int x0, int x1, int y,
                            uint16_t col, uint8_t sz=1, uint16_t bg=0x0000){
  int tw=strlen(s)*6*sz;
  int cx=x0+(x1-x0-tw)/2;
  tft.setTextColor(col,bg); tft.setTextSize(sz);
  tft.setCursor(cx,y); tft.print(s);
}

// ── Professional status bar ───────────────────────────────
static void drawMqttCloudIcon(int x,int y,bool connected){
  uint16_t col=connected?C_LTBLUE:C_RED;
  uint16_t dot=connected?C_GREEN:C_DKGRAY;
  tft.drawCircle(x+5,y+7,4,col);
  tft.drawCircle(x+10,y+5,5,col);
  tft.drawCircle(x+16,y+8,4,col);
  tft.drawFastHLine(x+5,y+12,12,col);
  tft.drawFastVLine(x+8,y+12,5,col);
  tft.drawFastVLine(x+13,y+12,5,col);
  tft.fillCircle(x+8,y+18,1,dot);
  tft.fillCircle(x+13,y+18,1,dot);
}

void drawStatusBar(int scr){
  // Background
  uint16_t barBg = (scr==0) ? C_BG : C_BG;
  tft.fillRect(0,0,SW,16,barBg);
  tft.drawFastHLine(0,16,SW,C_DKGRAY);

  // Time (left, always shown)
  struct tm ti;
  if(getLocalTime(&ti)){
    char tb[9]; snprintf(tb,9,"%02d:%02d %s",
      ti.tm_hour<12?ti.tm_hour:(ti.tm_hour-12),ti.tm_min,
      ti.tm_hour<12?"AM":"PM");
    tft.setTextColor(C_WHITE,barBg); tft.setTextSize(1);
    tft.setCursor(3,4); tft.print(tb);
  }

  // Center: screen title badge (not for main screen)
  if(scr != 0){
    const char* t = SCREEN_TITLES[scr];
    uint16_t col = SCREEN_TITLE_COL[scr];
    int tw = strlen(t)*6;
    int bw = tw+12;
    int bx = (SW-bw)/2;
    tft.fillRoundRect(bx,2,bw,12,3,col);
    tft.setTextColor(C_WHITE,col);
    tft.setCursor(bx+6,4); tft.print(t);
  }

  // WiFi indicator (right)
  int wx=SW-12, wy=4;
  bool wConn=WiFi.isConnected();
  uint16_t wc = wConn ? C_GREEN : C_DKGRAY;
  tft.fillCircle(wx,wy+5,2,wc);
  int rssi=WiFi.RSSI();
  int bars=wConn?((rssi>-60)?3:(rssi>-75)?2:1):0;
  for(int r=0;r<bars;r++){
    int rad=4+r*3;
    for(int a=215;a<=325;a+=8){
      float rd=a*0.01745f;
      int px=wx+(int)(rad*cosf(rd)),py=wy+5+(int)(rad*sinf(rd));
      if(py<=wy+5)tft.drawPixel(px,py,wc);
    }
  }
}

// Thin divider
void drawDivider(int y,uint16_t col=C_DKGRAY){
  tft.drawFastHLine(0,y,SW,col);
}

// Footer with top divider
void drawFooter(const char* txt,uint16_t col=C_DKGRAY){
  drawDivider(112,C_DKGRAY);
  tft.setTextColor(col,C_BG); tft.setTextSize(1);
  int tw=strlen(txt)*6;
  tft.setCursor(max(0,(SW-tw)/2),117); tft.print(txt);
}

// Professional rounded button
void drawBtn(int x,int y,int w,int h,const char* txt,uint16_t bgCol,uint16_t txtCol,bool active=false){
  tft.fillRoundRect(x,y,w,h,5,bgCol);
  if(active)tft.drawRoundRect(x,y,w,h,5,C_WHITE);
  tft.setTextColor(txtCol,bgCol); tft.setTextSize(1);
  int tw=strlen(txt)*6;
  tft.setCursor(x+(w-tw)/2,y+(h-7)/2); tft.print(txt);
}

// Professional motor icon (clean circle-based)
void drawMotorIconPro(int cx,int cy,int r,bool running){
  uint16_t rim = running ? C_GREEN : C_DKGRAY;
  uint16_t fill= running ? 0x0340  : 0x1082;
  tft.fillCircle(cx,cy,r,fill);
  tft.drawCircle(cx,cy,r,rim);
  tft.drawCircle(cx,cy,r-1,rim);
  // Inner ring
  tft.drawCircle(cx,cy,r-4,running?C_LTGREEN:C_GRAY);
  // Center dot
  tft.fillCircle(cx,cy,3,running?C_GREEN:C_GRAY);
  // Spokes (6)
  for(int a=0;a<360;a+=60){
    float rd=a*0.01745f;
    int x1=cx+(int)(4*cosf(rd)),y1=cy+(int)(4*sinf(rd));
    int x2=cx+(int)((r-5)*cosf(rd)),y2=cy+(int)((r-5)*sinf(rd));
    tft.drawLine(x1,y1,x2,y2,running?C_LTGREEN:C_DKGRAY);
  }
  // Output shaft
  tft.fillRect(cx+r,cy-3,7,6,running?C_GREEN:C_DKGRAY);
}

// WiFi icon
void drawWifiIconPro(int cx,int cy,bool conn){
  uint16_t col=conn?C_GREEN:C_RED;
  tft.fillCircle(cx,cy+6,3,col);
  for(int r=0;r<3;r++){
    int radius=6+r*4;
    tft.drawCircle(cx,cy+6,radius,col);
    tft.fillRect(cx-radius-1,cy+7,radius*2+2,radius+1,C_BG);
  }
}

// Lightning bolt icon
void drawLightningIcon(int x,int y,uint16_t col){
  tft.fillTriangle(x+6,y,x,y+8,x+7,y+8,col);
  tft.fillTriangle(x+5,y+7,x+13,y+6,x+7,y+14,col);
}

// Shield icon
void drawShieldIcon(int x,int y,bool ok){
  uint16_t col=ok?C_GREEN:C_RED;
  tft.fillRoundRect(x,y,13,12,2,ok?0x0280:0x5000);
  tft.drawRoundRect(x,y,13,12,2,col);
  tft.drawLine(x,y+11,x+6,y+15,col);
  tft.drawLine(x+12,y+11,x+6,y+15,col);
  if(ok){ tft.setTextColor(col,ok?0x0280:0x5000); tft.setCursor(x+4,y+3); tft.print(F("v")); }
  else   { tft.setTextColor(col,0x5000); tft.setCursor(x+4,y+3); tft.print(F("!")); }
}

// Alert icon (professional)
void drawAlertIconPro(int x,int y,AlertType ty,AlertLevel lv){
  switch(ty){
    case AT_HIGH_LEVEL:
      tft.fillRoundRect(x,y,13,13,2,0x8400);
      tft.setTextColor(C_YELLOW,0x8400); tft.setCursor(x+4,y+2); tft.print(F("!"));
      break;
    case AT_LEAK:
      tft.fillRoundRect(x,y,13,13,2,0x5000);
      tft.setTextColor(C_RED,0x5000); tft.setCursor(x+4,y+2); tft.print(F("~"));
      break;
    case AT_DRY_RUN:
      tft.fillRoundRect(x,y,13,13,2,0x000D);
      tft.setTextColor(C_CYAN,0x000D); tft.setCursor(x+4,y+2); tft.print(F("i"));
      break;
    case AT_VOLTAGE:
      tft.fillRoundRect(x,y,13,13,2,0x8420);
      drawLightningIcon(x,y,C_YELLOW);
      break;
    default:
      tft.fillRoundRect(x,y,13,13,2,C_DKGRAY);
      tft.setTextColor(C_GRAY,C_DKGRAY); tft.setCursor(x+4,y+2); tft.print(F("?"));
      break;
  }
}

// ── Circular water gauge — matches reference image ─────────
// cx,cy = centre  r = outer radius of circle
void drawWaterGaugePro(int cx,int cy,int r,float pct,bool running){
  uint16_t rimCol = running ? C_GREEN : C_CYAN;
  uint16_t rimIn  = running ? 0x0280  : C_DKBLUE;
  uint16_t waterCol,surfCol;
  if(pct>=cfg.highThr){waterCol=0x0340;surfCol=C_LTGREEN;}
  else if(pct>=cfg.lowThr){waterCol=WATER_MID;surfCol=WATER_TOP;}
  else if(pct>=10){waterCol=0x8400;surfCol=C_YELLOW;}
  else{waterCol=0x5000;surfCol=C_RED;}

  // Fill the circle pixel-row by pixel-row
  int fillH=(int)(pct*(2*r)/100.0f);
  int fillTop=cy+r-fillH;           // y where water surface starts
  for(int py=cy-r;py<=cy+r;py++){
    float dy=(float)(py-cy);
    float dx=sqrtf((float)(r*r)-(dy*dy));
    int x0=(int)(cx-dx),x1=(int)(cx+dx);
    uint16_t rowCol;
    if(py>=fillTop){
      float depth=(float)(py-fillTop)/max(1,fillH);
      rowCol=(depth<0.25f)?surfCol:(depth<0.6f)?waterCol:WATER_DARK;
    } else {
      rowCol=C_BG;
    }
    tft.drawFastHLine(x0,py,x1-x0+1,rowCol);
  }

  // Wave on surface
  r_waveOffset+=0.2f;
  if(fillH>2){
    for(int wx=cx-r+2;wx<cx+r-2;wx++){
      float wv=sinf(wx*0.28f+r_waveOffset)*1.5f+sinf(wx*0.45f-r_waveOffset*0.6f)*1.0f;
      int wy=fillTop+(int)wv;
      // clip to circle
      float ddy=(float)(wy-cy);
      if(ddy*ddy<=(float)(r*r)){
        tft.drawPixel(wx,wy,WATER_TOP);
        if(wy+1<=cy+r)tft.drawPixel(wx,wy+1,surfCol);
      }
    }
  }

  // Outer rim (two rings)
  tft.drawCircle(cx,cy,r+1,rimIn);
  tft.drawCircle(cx,cy,r+2,rimCol);
  tft.drawCircle(cx,cy,r+3,rimIn);

  // Percentage text centered
  char ps[7]; snprintf(ps,7,"%.0f%%",(float)((int)pct));
  int tw=strlen(ps)*12;
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE,(pct>=50)?waterCol:C_BG);
  tft.setCursor(cx-tw/2,cy-7); tft.print(ps);
  tft.setTextSize(1);
}

// Ring gauge (arc style for Smart Info screen)
void drawRingGaugePro(int cx,int cy,int r,float pct,uint16_t col){
  // Background arc
  for(int a=135;a<=405;a+=3){
    float rd=a*0.01745f;
    tft.drawPixel(cx+(int)((r)*cosf(rd)),cy+(int)((r)*sinf(rd)),C_DKGRAY);
    tft.drawPixel(cx+(int)((r-1)*cosf(rd)),cy+(int)((r-1)*sinf(rd)),C_DKGRAY);
  }
  // Filled arc
  int deg=(int)(pct*270/100);
  for(int a=135;a<=135+deg;a+=2){
    float rd=a*0.01745f;
    tft.drawPixel(cx+(int)(r*cosf(rd)),cy+(int)(r*sinf(rd)),col);
    tft.drawPixel(cx+(int)((r-1)*cosf(rd)),cy+(int)((r-1)*sinf(rd)),col);
    tft.drawPixel(cx+(int)((r-2)*cosf(rd)),cy+(int)((r-2)*sinf(rd)),col);
  }
  char ps[5]; snprintf(ps,5,"%.0f%%",pct);
  tft.setTextColor(col,C_BG); tft.setTextSize(2);
  int tw=strlen(ps)*12; tft.setCursor(cx-tw/2,cy-7); tft.print(ps);
  tft.setTextSize(1);
}

static void uiThickArc(int cx,int cy,int r,int thickness,int startDeg,int endDeg,uint16_t col){
  for(int rr=r-thickness+1;rr<=r;rr++){
    for(int deg=startDeg;deg<=endDeg;deg+=2){
      float a=deg*0.01745f;
      int x=cx+(int)roundf(cosf(a)*rr);
      int y=cy+(int)roundf(sinf(a)*rr);
      tft.drawPixel(x,y,col);
    }
  }
}

static void drawRefWifiIcon(int x,int y,bool connected){
  int bars=0;
  if(connected){
    int rssi=WiFi.RSSI();
    bars=(rssi>-60)?3:(rssi>-75)?2:1;
  }
  uint16_t col=!connected?C_RED:(bars>=3?C_GREEN:(bars==2?C_YELLOW:C_RED));
  tft.fillCircle(x,y+12,2,col);
  if(bars>=1)uiThickArc(x,y+10,4,1,220,320,col);
  if(bars>=2)uiThickArc(x,y+10,7,1,220,320,col);
  if(bars>=3)uiThickArc(x,y+10,10,1,220,320,col);
}

static void drawRefDropIcon(int x,int y){
  tft.fillTriangle(x,y-8,x-6,y+1,x+6,y+1,C_CYAN);
  tft.fillCircle(x,y+1,6,C_CYAN);
  tft.fillCircle(x-2,y+1,1,C_WHITE);
}

static void drawRefMotorIcon(int x,int y,bool running){
  uint16_t col=running?C_GREEN:C_RED;
  tft.drawRoundRect(x,y,17,10,3,col);
  if(running)tft.fillRoundRect(x+3,y+2,11,7,2,0x0240);
  tft.drawRoundRect(x+4,y+3,9,4,1,col);
  tft.drawLine(x+6,y+10,x+4,y+14,col);
  tft.drawLine(x+12,y+10,x+14,y+14,col);
  tft.drawLine(x+4,y+14,x+14,y+14,col);
  tft.fillRect(x+18,y+4,3,3,col);
}

static void drawAutoIcon(int x,int y,uint16_t col){
  uiThickArc(x,y,8,1,210,330,col);
  uiThickArc(x,y,8,1,30,150,col);
  tftCentredText("A",x-7,x+8,y-4,col,1,C_BG);
  tft.drawLine(x-11,y-1,x-11,y+5,col);
  tft.drawLine(x-11,y+5,x-14,y+1,col);
  tft.drawLine(x-11,y+5,x-8,y+1,col);
  tft.drawLine(x+11,y+5,x+11,y-1,col);
  tft.drawLine(x+11,y-1,x+8,y+2,col);
  tft.drawLine(x+11,y-1,x+14,y+2,col);
}

static void clearRefTankInside(int cx,int cy,int r){
  int clearR=r-4;
  for(int x=cx-clearR;x<=cx+clearR;x++){
    int dx=x-cx;
    int halfH=(int)sqrtf((float)(clearR*clearR-dx*dx));
    tft.drawFastVLine(x,cy-halfH,halfH*2,C_BG);
  }
}

static void drawRefWaterTank(int cx,int cy,int r,float pct){
  int percent=(int)constrain(pct,0.0f,100.0f);
  clearRefTankInside(cx,cy,r);
  int waterTop=cy+r-map(percent,0,100,0,r*2);
  waterTop=constrain(waterTop,cy-r+6,cy+r-6);
  uint16_t waterCol=(percent>=cfg.highThr)?C_GREEN:(percent>=cfg.lowThr)?C_CYAN:(percent>=10)?C_YELLOW:C_RED;

  for(int x=cx-r+4;x<=cx+r-4;x++){
    int dx=x-cx;
    int bottom=cy+(int)sqrtf((float)((r-3)*(r-3)-dx*dx));
    int top=cy-(int)sqrtf((float)((r-3)*(r-3)-dx*dx));
    int start=max(waterTop,top);
    if(bottom>=start)tft.drawFastVLine(x,start,bottom-start+1,waterCol);
  }

  tft.drawCircle(cx,cy,r,C_CYAN);
  tft.drawCircle(cx,cy,r-1,C_CYAN);
  tft.drawCircle(cx,cy,r-2,C_MDBLUE);

  char levelText[6];
  snprintf(levelText,sizeof(levelText),"%d%%",percent);
  int tw=strlen(levelText)*12;
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(cx-tw/2,cy-7);
  tft.print(levelText);
  tft.setTextSize(1);
}

// ============================================================
//  SCREEN 0: MAIN SCREEN
//  Motor OFF  → Circular gauge UI  (matches reference photo)
//  Motor ON   → Robot animation
// ============================================================
void drawScreen0_Gauge(bool fullRedraw){
  (void)fullRedraw;
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,6,C_WHITE);
  tft.drawRoundRect(2,2,156,124,5,0x4A69);
  tft.drawRoundRect(4,5,152,118,4,0x1082);

  drawRefWifiIcon(15,7,WiFi.isConnected());
  drawMqttCloudIcon(29,4,mqtt.connected());

  struct tm ti;
  char clockText[6]="--:--";
  char ampm[3]="";
  if(getLocalTime(&ti)){
    int hr=ti.tm_hour%12;
    if(hr==0)hr=12;
    snprintf(clockText,sizeof(clockText),"%02d:%02d",hr,ti.tm_min);
    snprintf(ampm,sizeof(ampm),"%s",ti.tm_hour<12?"AM":"PM");
  }
  tftCentredText(clockText,54,90,9,C_WHITE,1,C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE,C_BG);
  tft.setCursor(91,9); tft.print(ampm);

  char mainVolt[8];
  snprintf(mainVolt,sizeof(mainVolt),"%.0fV",smoothVoltage);
  tft.setTextColor(C_YELLOW,C_BG);
  tft.setCursor(130,8); tft.print(mainVolt);
  tft.setTextColor(isVoltSafe(smoothVoltage)?C_GREEN:C_RED,C_BG);
  tft.setCursor(141,19); tft.print(F("AC"));
  tft.drawFastHLine(9,28,142,C_CYAN);

  tft.drawFastVLine(78,34,61,C_DKGRAY);
  tft.drawFastHLine(84,68,66,C_DKGRAY);
  drawRefWaterTank(42,64,27,smoothLevel);

  const char* modeText=opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MAN":"MNT";
  uint16_t modeCol=opMode==MODE_AUTO?C_GREEN:opMode==MODE_MANUAL?C_YELLOW:C_ORANGE;
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN,C_BG);
  tft.setCursor(84,36); tft.print(F("MODE"));
  tft.drawRoundRect(84,47,34,13,3,modeCol);
  tftCentredText(modeText,84,118,50,modeCol,1,C_BG);
  if(opMode==MODE_AUTO)drawAutoIcon(136,54,modeCol);

  tft.setTextColor(C_CYAN,C_BG);
  tft.setCursor(84,73); tft.print(F("MOTOR"));
  drawRefMotorIcon(88,84,pumpRunning);
  tftCentredText(pumpRunning?"ON":"OFF",116,154,85,pumpRunning?C_GREEN:C_RED,1,C_BG);
  if(pumpRunning&&fillEtaMin>0){
    char etaB[12];
    snprintf(etaB,sizeof(etaB),"ETA %.0fm",fillEtaMin);
    tftCentredText(etaB,84,154,101,C_CYAN,1,C_BG);
  }

  tft.drawFastHLine(9,101,142,C_DKGRAY);
  drawRefDropIcon(17,115);
  tft.setTextColor(C_CYAN,C_BG);
  tft.setCursor(32,108); tft.print(F("TODAY"));
  tft.setCursor(34,118); tft.print(F("USED"));
  char usageB[10];
  snprintf(usageB,sizeof(usageB),"%.0f",dailyUsage);
  tftCentredText(usageB,82,134,112,C_WHITE,1,C_BG);
  tftCentredText("L",136,151,112,C_CYAN,1,C_BG);

  if(activeAlerts>0){
    tft.fillCircle(151,33,5,C_RED);
    tft.setTextColor(C_WHITE,C_RED);
    tft.setTextSize(1);
    tft.setCursor(149,30); tft.print(activeAlerts);
  }
  return;

  if(fullRedraw){
    tft.fillScreen(C_BG);
    drawStatusBar(0);
  } else {
    tft.fillRect(0,0,SW,16,C_BG);
    drawStatusBar(0);
  }

  // ── Title row ─────────────────────────────────────────────
  // "SMART  WATER LEVEL" centred, white
  if(fullRedraw){
    tft.fillRect(0,17,SW,13,C_BG);
    tftCentredText("SMART  WATER LEVEL",0,SW,19,C_WHITE,1,C_BG);
  }

  // ── Circular water gauge (left half) ─────────────────────
  // Centre at (36, 78), radius 32 — fills the left ~75px vertically
  drawWaterGaugePro(36,76,32,smoothLevel,false);

  // TODAY USED row (below gauge)
  tft.fillRect(0,113,74,15,C_BG);
  tft.fillTriangle(5,127,3,124,7,124,C_LTBLUE);
  tft.fillCircle(5,124,3,C_LTBLUE);
  tft.setTextColor(C_LGRAY,C_BG); tft.setTextSize(1);
  tft.setCursor(12,117); tft.print(F("TODAY USED"));
  tft.setTextColor(C_WHITE,C_BG); tft.setTextSize(1);
  char ul[12]; snprintf(ul,12,"%.0f L",dailyUsage);
  tft.setCursor(12,126); tft.print(ul);

  // ── Vertical divider ─────────────────────────────────────
  tft.drawFastVLine(75,30,98,C_DKGRAY);

  // ── Right column ─────────────────────────────────────────
  const int rx=80;

  // Voltage (top-right, yellow)  e.g. "229V / AC"
  {
    tft.fillRect(rx,17,SW-rx-1,14,C_BG);
    char vb[8]; snprintf(vb,8,"%.0fV",smoothVoltage);
    tft.setTextColor(C_YELLOW,C_BG); tft.setTextSize(1);
    // right-align voltage value
    int vw=strlen(vb)*6;
    tft.setCursor(SW-vw-2,19); tft.print(vb);
    tft.setTextColor(C_DKGREEN,C_BG);
    tft.setCursor(SW-14,27); tft.print(F("AC"));
  }

  // MODE badge
  {
    const char* mStr=opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MANUAL":"MAINT";
    uint16_t mCol=opMode==MODE_AUTO?C_GREEN:opMode==MODE_MANUAL?C_YELLOW:C_ORANGE;
    tft.fillRect(rx,33,SW-rx-1,32,C_BG);
    tft.setTextColor(mCol,C_BG); tft.setTextSize(1);
    tft.setCursor(rx,33); tft.print(F("MODE"));
    // Badge box
    int bw=strlen(mStr)*6+10;
    tft.fillRoundRect(rx,42,bw,13,3,C_DKGRAY);
    tft.drawRoundRect(rx,42,bw,13,3,mCol);
    tft.setTextColor(mCol,C_DKGRAY);
    tft.setCursor(rx+5,44); tft.print(mStr);
    // Arrow icon hint (small "vA^" to right of badge)
    tft.setTextColor(C_DKGRAY,C_BG);
    tft.setCursor(rx+bw+3,44); tft.print(F("vA^"));
  }

  // MOTOR section
  {
    tft.fillRect(rx,67,SW-rx-1,40,C_BG);
    tft.setTextColor(C_CYAN,C_BG); tft.setTextSize(1);
    tft.setCursor(rx,67); tft.print(F("MOTOR"));
    // Motor icon
    drawMotorIconPro(rx+10,82,10,pumpRunning);
    // ON/OFF label
    tft.setTextColor(pumpRunning?C_GREEN:C_DKGRAY,C_BG);
    tft.setCursor(rx+24,79); tft.print(pumpRunning?F("ON"):F("OFF"));
    if(pumpRunning&&fillEtaMin>0){
      char eb[10]; snprintf(eb,10,"~%.0fm",fillEtaMin);
      tft.setTextColor(C_CYAN,C_BG); tft.setCursor(rx+24,89); tft.print(eb);
    }
  }

  // Alert indicator dot
  if(activeAlerts>0){
    tft.fillCircle(71,30,4,C_RED);
    tft.setTextColor(C_WHITE,C_RED);
    tft.setCursor(69,27); tft.print(activeAlerts);
  }
}

// [V8-7] Robot animation while motor running
void drawScreen0_Robot(bool fullRedraw){
  bool night=isMotorNightScene();
  uint16_t bgColor = night ? BG_NIGHT : BG_DAY;
  uint16_t gndColor= night ? GROUND_NIGHT : GROUND_DAY;

  if(fullRedraw){
    drawStatusBar(0);
    tft.fillRect(0,17,SW,55,bgColor);
    tft.fillRect(0,72,SW,SH-72,gndColor);
    tft.drawFastHLine(0,72,SW,night?0x18A3:0x3666);

    // Sun or Moon
    if(night){
      tft.fillCircle(143,30,9,MOON_COL);
      tft.fillCircle(147,27,8,bgColor);
      tft.fillCircle(141,27,2,MOON_SHAD);
    } else {
      tft.fillCircle(143,28,9,SUN_COL);
      tft.drawCircle(143,28,11,SUN_RIM);
      for(int i=0;i<8;i++){
        float a=i*45*0.01745f;
        tft.drawLine(143+(int)(cosf(a)*13),28+(int)(sinf(a)*13),
                     143+(int)(cosf(a)*16),28+(int)(sinf(a)*16),SUN_RIM);
      }
    }
  }

  // Clear moving areas
  tft.fillRect(0,17,130,38,bgColor);
  tft.fillRect(30,45,30,32,bgColor);
  tft.fillRect(78,20,40,55,bgColor);
  tft.fillRect(104,28,30,28,bgColor);

  // Stars or birds
  if(night){
    int sX[]={12,30,55,82,108,22,48,72,98,120};
    int sY[]={25,33,20,38,28,52,44,58,47,62};
    for(int i=0;i<10;i++){
      int blink=(r_frame+i*7)%30;
      if(blink<22){
        uint16_t c=(i%2==0)?STAR_COL:C_YELLOW;
        tft.drawPixel(sX[i],sY[i],c);
        if(blink>17){
          tft.drawPixel(sX[i]-1,sY[i],c); tft.drawPixel(sX[i]+1,sY[i],c);
          tft.drawPixel(sX[i],sY[i]-1,c); tft.drawPixel(sX[i],sY[i]+1,c);
        }
      }
    }
  } else {
    int bX=(r_frame*2)%200;
    if(bX<160){
      int bY=35+(int)(sinf(r_frame*0.08f)*4);
      if((r_frame/8)%2==0){
        tft.drawLine(bX-4,bY,bX-1,bY-2,0x0000); tft.drawLine(bX-1,bY-2,bX+2,bY,0x0000);
      } else {
        tft.drawLine(bX-4,bY-1,bX-1,bY-2,0x0000); tft.drawLine(bX-1,bY-2,bX+2,bY-1,0x0000);
      }
    }
  }

  r_frame++;

  // Pump handle animation
  if(r_pumpingDown){ r_pumpHandleY+=2.5f; if(r_pumpHandleY>20)r_pumpingDown=false; }
  else { r_pumpHandleY-=2.5f; if(r_pumpHandleY<0)r_pumpingDown=true; }

  // TANK (left, x=5)
  {
    int tX=5,tY=73,tW=46,tH=50;
    if(fullRedraw){
      tft.drawRoundRect(tX,tY,tW,tH,3,ROBOT_DARK);
    }
    int fH=(int)((tH-6)*(smoothLevel/100.0f));
    int wTop=tY+tH-3-fH;
    tft.fillRect(tX+2,tY+2,tW-4,tH-4-fH,bgColor);
    for(int row=wTop;row<tY+tH-3;row++){
      float d=(float)(row-wTop)/max(1,fH);
      uint16_t c=(d<0.3f)?WATER_TOP:(d<0.7f)?WATER_MID:WATER_BOT;
      tft.drawFastHLine(tX+2,row,tW-4,c);
    }
    r_waveOffset+=0.3f;
    if(fH>2){
      for(int wx2=tX+2;wx2<tX+tW-2;wx2++){
        float wv=sinf((wx2*0.25f)+r_waveOffset)*2+sinf((wx2*0.4f)-r_waveOffset*0.6f)*1.5f;
        int wy2=wTop+(int)wv;
        if(wy2>=tY&&wy2<tY+tH){
          tft.drawPixel(wx2,wy2,WATER_TOP);
          if(wy2+1<tY+tH)tft.drawPixel(wx2,wy2+1,WATER_MID);
        }
      }
    }
    tft.setTextSize(2); tft.setTextColor(C_WHITE,WATER_MID);
    int pct=(int)smoothLevel;
    tft.setCursor(tX+5+(pct>=100?0:(pct>=10?3:6)),tY+17);
    tft.print(pct); tft.print(F("%"));
    tft.setTextSize(1);
  }

  // PUMP (center, x=68)
  {
    int pX=68,pBY=72;
    // Shared hand-pump body and outlet pipe, mirrored toward the tank.
    tft.fillRect(pX-10,pBY,20,8,PUMP_BODY);
    tft.drawRect(pX-10,pBY,20,8,ROBOT_DARK);
    tft.fillRect(pX-8,pBY+2,16,3,ROBOT_DARK);

    tft.fillRoundRect(pX-6,pBY-40,12,40,2,PUMP_BODY);
    tft.drawRoundRect(pX-6,pBY-40,12,40,2,ROBOT_DARK);
    tft.drawFastVLine(pX-4,pBY-38,36,ROBOT_LIGHT);
    tft.drawFastVLine(pX+3,pBY-38,36,C_GRAY);

    tft.fillRect(pX-7,pBY-42,14,3,PUMP_ACCENT);
    tft.drawRect(pX-7,pBY-42,14,3,ROBOT_DARK);

    // Outlet spout / pipe to the left tank.
    tft.fillRect(pX-14,pBY-20,8,3,PUMP_HANDLE);
    tft.drawRect(pX-14,pBY-20,8,3,ROBOT_DARK);
    tft.fillRect(pX-16,pBY-23,3,6,PUMP_HANDLE);
    tft.drawRect(pX-16,pBY-23,3,6,ROBOT_DARK);
    tft.fillRect(pX-22,pBY-23,7,3,PUMP_HANDLE);
    tft.drawRect(pX-22,pBY-23,7,3,ROBOT_DARK);
    tft.fillTriangle(pX-25,pBY-23,pX-25,pBY-20,pX-28,pBY-21,PUMP_HANDLE);
    tft.drawTriangle(pX-25,pBY-23,pX-25,pBY-20,pX-28,pBY-21,ROBOT_DARK);

    int pivX=pX+3,pivY=pBY-42;
    tft.fillCircle(pivX,pivY,3,ROBOT_DARK);
    tft.drawCircle(pivX,pivY,3,PUMP_ACCENT);
    int hEndX=pivX+28,hEndY=pivY-8+(int)r_pumpHandleY;
    tft.drawLine(pivX,pivY,hEndX,hEndY,PUMP_ACCENT);
    tft.drawLine(pivX,pivY+1,hEndX,hEndY+1,PUMP_ACCENT);
    tft.drawLine(pivX+1,pivY,hEndX+1,hEndY,PUMP_ACCENT);
    tft.drawLine(pivX-1,pivY,hEndX-1,hEndY,PUMP_ACCENT);
    tft.fillCircle(hEndX,hEndY,4,C_LTBLUE);
    tft.drawCircle(hEndX,hEndY,4,ROBOT_DARK);
    tft.fillRect(hEndX-6,hEndY-2,12,4,C_LTBLUE);
    tft.drawRect(hEndX-6,hEndY-2,12,4,ROBOT_DARK);
  }

  // WATER DROPS
  if(r_pumpingDown&&r_pumpHandleY>12&&r_frame%5==0){
    for(int i=0;i<8;i++){
      if(r_drops[i].life<=0){
        r_drops[i]={68-28.0f,(float)(72-21),float(random(15,25))/10.0f,random(20,35)};
        break;
      }
    }
  }
  for(int i=0;i<8;i++){
    if(r_drops[i].life>0){
      tft.fillCircle((int)r_drops[i].x,(int)r_drops[i].y,2,WATER_TOP);
      r_drops[i].y+=r_drops[i].vy; r_drops[i].x-=0.5f; r_drops[i].life--;
      if(r_drops[i].y>74&&r_drops[i].life>5)r_drops[i].life=5;
    }
  }

  // ROBOT (right, x=120)
  {
    int rX=120,rY=52;
    int shake=0;
    if(r_frame%4<2)shake=(r_frame%2)?1:-1; // subtle pump shake
    rX+=shake;
    int hY=rY-14;
    // Body
    tft.fillRoundRect(rX-10,rY,20,20,5,ROBOT_BODY);
    tft.drawRoundRect(rX-10,rY,20,20,5,ROBOT_DARK);
    // Head
    tft.fillRoundRect(rX-9,hY,18,14,4,ROBOT_BODY);
    tft.drawRoundRect(rX-9,hY,18,14,4,ROBOT_DARK);
    // Antenna
    tft.drawLine(rX,hY,rX,hY-5,ROBOT_DARK);
    tft.fillCircle(rX,hY-6,2,(r_frame%20<10)?SPARK_COL:PUMP_ACCENT);
    // Arms to pump handle
    int hX=68+3+28,hYend=72-42-8+(int)r_pumpHandleY;
    tft.drawLine(rX-10,rY+5,hX-5,hYend,ROBOT_DARK);
    tft.drawLine(rX-10,rY+9,hX-5,hYend+4,ROBOT_DARK);
    // Eyes
    int eY=hY+5;
    tft.fillCircle(rX-4,eY,2,EYE_WHITE);
    tft.fillCircle(rX+4,eY,2,EYE_WHITE);
    tft.fillCircle(rX-4,eY,1,EYE_PUPIL);
    tft.fillCircle(rX+4,eY,1,EYE_PUPIL);
    // Blush (happy working)
    tft.fillCircle(rX-7,eY+3,2,0xFDF8);
    tft.fillCircle(rX+7,eY+3,2,0xFDF8);
  }

  // Status line at bottom
  {
    tft.fillRect(0,117,SW,11,C_BG);
    tft.drawFastHLine(0,116,SW,C_DKGRAY);
    // ETA
    if(fillEtaMin>0){
      char eb[22]; snprintf(eb,22,"FILLING  ETA: %.0f min",fillEtaMin);
      tft.setTextColor(C_GREEN,C_BG); tft.setTextSize(1);
      int tw=strlen(eb)*6; tft.setCursor((SW-tw)/2,120); tft.print(eb);
    } else {
      tft.setTextColor(C_GREEN,C_BG); tft.setTextSize(1);
      tft.setCursor(35,120); tft.print(F("MOTOR RUNNING"));
    }
  }
}

// ============================================================
//  SCREEN 1: MOTOR CONTROL  [V9 professional]
// ============================================================
void drawScreen1_Motor(){
  tft.fillScreen(C_BG);
  drawStatusBar(1);
  tft.drawFastHLine(0,17,SW,C_DKGRAY);

  // ── Left panel: MOTOR STATUS ───────────────────────────
  tft.setTextColor(C_LGRAY,C_BG); tft.setTextSize(1);
  tftCentredText("MOTOR STATUS",0,76,20,C_LGRAY,1,C_BG);

  // Large motor icon centred in left panel
  drawMotorIconPro(38,60,22,pumpRunning);

  // Spin animation overlay when running
  if(pumpRunning){
    int ang=(r_frame*12)%360;
    float rd=ang*0.01745f;
    int ax=38+(int)(18*cosf(rd)),ay=60+(int)(18*sinf(rd));
    tft.drawLine(38,60,ax,ay,C_LTGREEN);
    tft.fillCircle(ax,ay,2,C_LTGREEN);
    r_frame++;
  }

  // Status text with coloured pill background
  uint16_t stCol=pumpRunning?C_GREEN:C_RED;
  uint16_t stBg =pumpRunning?0x0280:0x5000;
  const char* stTxt=pumpRunning?"RUNNING":"STOPPED";
  int stW=strlen(stTxt)*6+8;
  tft.fillRoundRect(38-stW/2,84,stW,12,4,stBg);
  tft.drawRoundRect(38-stW/2,84,stW,12,4,stCol);
  tftCentredText(stTxt,0,76,86,stCol,1,stBg);

  // ── Vertical divider ──────────────────────────────────
  tft.drawFastVLine(76,17,78,C_DKGRAY);

  // ── Right panel: MODE + PROTECTION ───────────────────
  tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(82,20); tft.print(F("MODE"));
  const char* mStr=opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MANUAL":"MAINT";
  uint16_t mCol=opMode==MODE_AUTO?C_GREEN:opMode==MODE_MANUAL?C_YELLOW:C_ORANGE;
  int mW=strlen(mStr)*6+10;
  tft.fillRoundRect(82,30,mW,14,4,C_DKGRAY);
  tft.drawRoundRect(82,30,mW,14,4,mCol);
  tftCentredText(mStr,82,82+mW,32,mCol,1,C_DKGRAY);

  // Protection
  tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(82,51); tft.print(F("PROTECTION"));
  bool protOk=isVoltSafe(smoothVoltage)&&!leakConfirmed&&!voltWarn;
  drawShieldIcon(82,61,protOk);
  tft.setTextColor(protOk?C_GREEN:C_RED,C_BG);
  tft.setCursor(99,64); tft.print(protOk?F("ACTIVE"):F("FAULT!"));

  // ETA if pumping
  if(pumpRunning&&fillEtaMin>0){
    char eb[18]; snprintf(eb,18,"ETA %.0f min",fillEtaMin);
    tft.setTextColor(C_DKGRAY,C_BG); tft.setCursor(82,80); tft.print(eb);
  }

  // ── Bottom divider + buttons ──────────────────────────
  tft.drawFastHLine(0,95,SW,C_DKGRAY);

  bool mOn=pumpRunning,mOff=!pumpRunning,mAuto=(opMode==MODE_AUTO);
  // ON button — green
  drawBtn(2,98,48,20,"  ON",mOn?C_GREEN:0x0280,mOn?C_BG:C_LGRAY,mOn);
  // OFF button — red
  drawBtn(56,98,48,20," OFF",mOff?C_RED:0x5000,mOff?C_WHITE:C_LGRAY,mOff);
  // AUTO button — blue
  drawBtn(110,98,48,20,"AUTO",mAuto?C_DKBLUE:C_DKGRAY,mAuto?C_CYAN:C_LGRAY,mAuto);
}

static void uiArcThin(int cx,int cy,int r,int a0,int a1,uint16_t col){
  for(int a=a0;a<=a1;a+=4){
    float rd=a*0.01745f;
    int x=cx+(int)(cosf(rd)*r);
    int y=cy+(int)(sinf(rd)*r);
    tft.drawPixel(x,y,col);
  }
}

static void uiClockIcon(int cx,int cy,uint16_t col){
  tft.drawCircle(cx,cy,5,col);
  tft.drawLine(cx,cy,cx,cy-3,col);
  tft.drawLine(cx,cy,cx+3,cy+2,col);
}

static void uiTrendIcon(int x,int y,uint16_t col){
  tft.drawFastVLine(x,y-5,11,col);
  tft.drawFastHLine(x,y+6,12,col);
  tft.drawLine(x+2,y+3,x+5,y,col);
  tft.drawLine(x+5,y,x+8,y+2,col);
  tft.drawLine(x+8,y+2,x+11,y-2,col);
}

static void uiBrainIcon(int x,int y,uint16_t col){
  tft.drawCircle(x-3,y-3,3,col);
  tft.drawCircle(x+3,y-3,3,col);
  tft.drawCircle(x-3,y+3,3,col);
  tft.drawCircle(x+3,y+3,3,col);
  tft.drawFastVLine(x,y-7,14,col);
}

static void uiCalendarIcon(int x,int y,uint16_t col){
  tft.drawRoundRect(x,y,16,13,2,col);
  tft.drawFastHLine(x,y+4,16,col);
  tft.fillRect(x+3,y+7,3,3,col);
  tft.fillRect(x+9,y+7,3,3,col);
  tft.fillRect(x+3,y+11,3,1,col);
  tft.fillRect(x+9,y+11,3,1,col);
}

static const Alert* uiLatestAlert(){
  for(int i=0;i<ALERT_HISTORY;i++){
    int idx=((alertHead-1-i)%ALERT_HISTORY+ALERT_HISTORY)%ALERT_HISTORY;
    if(alertHistory[idx].active) return &alertHistory[idx];
  }
  return nullptr;
}

// ============================================================
//  SCREEN 2: ALERTS  [V9 professional]
// ============================================================
void drawScreen2_Alerts(){
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,7,C_WHITE);
  tft.drawRoundRect(2,2,156,124,6,C_DKGRAY);
  tft.drawFastHLine(8,18,144,C_RED);
  tftCentredText("ALERTS",0,SW,6,C_RED,2,C_BG);

  bool voltFault=!isVoltSafe(smoothVoltage)||voltWarn;
  const Alert* la=uiLatestAlert();
  bool hasAlert=(la!=nullptr&&la->active)||voltFault;

  tft.drawRoundRect(10,29,140,58,4,C_RED);
  tft.drawRoundRect(11,30,138,56,4,C_DKRED);

  tft.fillTriangle(26,45,14,67,38,67,C_RED);
  tft.fillRoundRect(24,53,4,9,1,C_BG);
  tft.fillCircle(26,64,1,C_BG);

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE,C_BG);
  if(!hasAlert){
    tft.setCursor(46,43); tft.print(F("NO ACTIVE ALERT"));
    tft.setTextColor(C_GRAY,C_BG);
    tft.setCursor(46,57); tft.print(F("System running"));
    tft.setCursor(46,68); tft.print(F("normally"));
  } else if(la!=nullptr&&la->active){
    tft.setCursor(46,43); tft.print(la->msg);
    tft.setTextColor(C_GRAY,C_BG);
    tft.setCursor(46,57); tft.print(la->detail);
    char ts[10]; snprintf(ts,10,"%02d:%02d",la->hour,la->minute);
    tft.setCursor(46,68); tft.print(ts);
  } else {
    char v2[22];
    snprintf(v2,22,"Now %.0fV Lim %.0fV",smoothVoltage,cfg.safeVHigh);
    tft.setCursor(46,43); tft.print(F("HIGH VOLTAGE"));
    tft.setTextColor(C_GRAY,C_BG);
    tft.setCursor(46,57); tft.print(v2);
    tft.setCursor(46,68); tft.print(F("Check supply"));
  }

  tft.fillRoundRect(36,93,88,18,4,C_DKBLUE);
  tft.drawRoundRect(36,93,88,18,4,C_BLUE);
  tft.drawCircle(52,102,5,C_WHITE);
  tft.drawLine(49,102,51,104,C_WHITE);
  tft.drawLine(51,104,55,100,C_WHITE);
  tft.setTextColor(C_WHITE,C_DKBLUE);
  tft.setTextSize(2);
  tft.setCursor(66,96); tft.print(F("OK"));

  tft.setTextSize(1);
  tft.setTextColor(C_DKGRAY,C_BG);
  tftCentredText("BTN2: Dismiss / Back",0,SW,116,C_DKGRAY,1,C_BG);
}
// ============================================================
//  SCREEN 3: USAGE DATA (professional)
// ============================================================
void drawScreen3_Usage(){
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,7,C_WHITE);
  tft.drawRoundRect(2,2,156,124,6,C_DKGRAY);
  tftCentredText("DATA ANALYTICS",0,SW,6,C_WHITE,1,C_BG);
  tft.drawFastHLine(8,16,144,C_BLUE);

  const int px=6, py=22, pw=56, ph=98;
  tft.drawRoundRect(px,py,pw,ph,3,C_DKGRAY);
  tft.drawFastHLine(px+4,py+33,pw-8,C_DKGRAY);
  tft.drawFastHLine(px+4,py+61,pw-8,C_DKGRAY);

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE,C_BG);
  tft.setCursor(px+5,py+5); tft.print(F("TODAY"));
  tft.setTextColor(C_CYAN,C_BG);
  tft.setTextSize(2);
  char ub[10]; snprintf(ub,10,"%.0f",dailyUsage);
  tft.setCursor(px+5,py+14); tft.print(ub);
  tft.setTextSize(1); tft.setCursor(px+40,py+22); tft.print(F("L"));

  float sumDay=0.0f; for(int i=0;i<24;i++) sumDay+=hourlyUsage[i];
  float avgDay=sumDay/24.0f;
  tft.setTextColor(C_WHITE,C_BG);
  tft.setCursor(px+5,py+39); tft.print(F("AVG"));
  tft.setTextColor(C_CYAN,C_BG);
  tft.setTextSize(2);
  char ab[10]; snprintf(ab,10,"%.0f",avgDay);
  tft.setCursor(px+5,py+47); tft.print(ab);
  tft.setTextSize(1); tft.setCursor(px+28,py+55); tft.print(F("L"));

  int peakHr=0; float peakV=0.0f;
  for(int i=0;i<24;i++){ if(hourlyUsage[i]>peakV){ peakV=hourlyUsage[i]; peakHr=i; } }
  tft.setTextColor(C_WHITE,C_BG);
  tft.setCursor(px+5,py+67); tft.print(F("PEAK"));
  uiClockIcon(px+10,py+84,C_CYAN);
  tft.setTextColor(C_CYAN,C_BG);
  char pb[10]; snprintf(pb,10,"%02d-%02d",peakHr,(peakHr+3)%24);
  tft.setCursor(px+18,py+80); tft.print(pb);

  const int cx=68, cy=32, cw=84, ch=74;
  tft.setTextColor(C_WHITE,C_BG);
  tft.setCursor(88,22); tft.print(F("USAGE (L)"));
  tft.drawFastVLine(cx,cy,ch,C_DKGRAY);
  tft.drawFastHLine(cx,cy+ch-1,cw,C_DKGRAY);

  float bins[12];
  float maxBin=1.0f;
  for(int i=0;i<12;i++){
    bins[i]=hourlyUsage[i*2]+hourlyUsage[i*2+1];
    if(bins[i]>maxBin) maxBin=bins[i];
  }
  if(maxBin<20.0f) maxBin=20.0f;

  const int barW=5;
  const int gap=1;
  int bx=cx+3;
  for(int i=0;i<12;i++){
    int bh=(int)((bins[i]/maxBin)*(ch-6));
    if(bh<2) bh=2;
    int by=cy+ch-2-bh;
    tft.fillRoundRect(bx,by,barW,bh,1,(i>=6&&i<=9)?C_CYAN:C_MDBLUE);
    bx+=(barW+gap);
  }

  tft.setTextColor(C_DKGRAY,C_BG);
  tft.setCursor(cx,cy+ch+3); tft.print(F("0"));
  tft.setCursor(cx+31,cy+ch+3); tft.print(F("12"));
  tft.setCursor(cx+63,cy+ch+3); tft.print(F("24"));
  tft.setCursor(101,114); tft.print(F("HRS"));
}
// ============================================================
//  SCREEN 4: SYSTEM STATUS  [V9 professional]
// ============================================================
static void uiRightText(const char* txt,int rightX,int y,uint16_t col){
  int tw=strlen(txt)*6;
  tft.setTextColor(col,C_BG);
  tft.setTextSize(1);
  tft.setCursor(rightX-tw,y);
  tft.print(txt);
}

static void uiSensorIcon(int cx,int cy,uint16_t col){
  tft.drawCircle(cx,cy,2,col);
  uiArcThin(cx,cy,7,130,230,col);
  uiArcThin(cx,cy,7,-50,50,col);
  uiArcThin(cx,cy,10,130,230,col);
  uiArcThin(cx,cy,10,-50,50,col);
  tft.fillCircle(cx,cy-8,1,col);
  tft.fillCircle(cx,cy+8,1,col);
}

static void uiInfoIcon(int cx,int cy,uint16_t col){
  tft.drawCircle(cx,cy,8,col);
  tft.drawCircle(cx,cy,7,col);
  tft.fillCircle(cx,cy-3,1,col);
  tft.drawFastVLine(cx,cy,5,col);
}

static void uiLightningRowIcon(int x,int y,uint16_t col){
  drawLightningIcon(x-7,y-8,col);
}

static void uiWifiRowIcon(int x,int y,uint16_t col){
  (void)col;
  drawRefWifiIcon(x,y-10,WiFi.isConnected());
}

static void drawSystemCardRow(int y,void (*iconFn)(int,int,uint16_t),
                              const char* label,const char* value,
                              uint16_t iconCol,uint16_t valueCol){
  tft.drawRoundRect(5,y,150,25,3,0x39E7);
  iconFn(19,y+13,iconCol);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN,C_BG);
  tft.setCursor(43,y+9); tft.print(label);
  uiRightText(value,149,y+9,valueCol);
}

void drawScreen4_System(){
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,7,C_WHITE);
  tft.drawRoundRect(2,2,156,124,6,0x6B4D);

  bool vOk=isVoltSafe(smoothVoltage);
  bool wOk=WiFi.isConnected();
  bool sOk=(usN>0||maLevel.count>0);

  char voltB[14];
  snprintf(voltB,sizeof(voltB),"%.0fV AC",smoothVoltage);

  drawSystemCardRow(8,  uiLightningRowIcon,
                    "VOLTAGE",voltB,C_YELLOW,vOk?C_WHITE:C_RED);
  drawSystemCardRow(37, uiWifiRowIcon,
                    "WIFI STATUS",wOk?"CONN":"OFF",wOk?C_GREEN:C_RED,wOk?C_GREEN:C_RED);
  drawSystemCardRow(66, uiSensorIcon,
                    "SENSOR",sOk?"NORMAL":"FAULT",sOk?C_CYAN:C_RED,sOk?C_GREEN:C_RED);
  drawSystemCardRow(95, uiInfoIcon,
                    "FIRMWARE","v8.0 PRO",C_CYAN,C_WHITE);
  return;

  tft.fillScreen(C_BG);
  drawStatusBar(4);
  tft.drawFastHLine(0,17,SW,ACCENT_SYS);

  struct SysRow {
    const char* lbl;
    bool ok;
    const char* valOk;
    const char* valFail;
  };
  wOk=WiFi.isConnected();
  vOk=isVoltSafe(smoothVoltage);
  SysRow rows[]={
    {"SENSOR",  usN>0,       "OK",       "FAULT"},
    {"MOTOR",   !pumpTimedOut,"OK",      "TIMEOUT"},
    {"WIFI",    wOk,         "CONNECTED","OFFLINE"},
    {"FIRMWARE",true,        "v8.0 PRO", "v8.0 PRO"},
    {"AC VOLTAGE",vOk,      nullptr,    nullptr},
  };
  const int N=5;

  for(int i=0;i<N;i++){
    int y=20+i*20;
    if(i>0) tft.drawFastHLine(2,y-1,SW-4,C_DKGRAY);

    uint16_t vc=rows[i].ok?C_GREEN:C_RED;

    // Icon (14×14 area at left)
    switch(i){
      case 0: // Soundwave bars
        for(int l=0;l<5;l++){
          int ht=2+l*3;
          tft.drawFastVLine(3+l*3,y+1+(12-ht)/2,ht,vc);
        }
        break;
      case 1: drawMotorIconPro(12,y+8,8,!pumpTimedOut); break;
      case 2: drawWifiIconPro(12,y+3,wOk); break;
      case 3:
        tft.drawCircle(12,y+8,7,C_CYAN);
        tft.fillCircle(12,y+5,1,C_CYAN);
        tft.drawFastVLine(12,y+8,5,C_CYAN);
        break;
      case 4: drawLightningIcon(4,y+1,vOk?C_YELLOW:C_RED); break;
    }

    // Label
    tft.setTextColor(C_LGRAY,C_BG); tft.setTextSize(1);
    tft.setCursor(28,y+3); tft.print(rows[i].lbl);

    // Value right-aligned
    if(i==4){
      char vb[16];
      snprintf(vb,16,"%.0fV %s",smoothVoltage,vOk?"NORMAL":"FAULT!");
      tft.setTextColor(vc,C_BG);
      int vw=strlen(vb)*6;
      tft.setCursor(SW-vw-2,y+3); tft.print(vb);
    } else {
      const char* vs=rows[i].ok?rows[i].valOk:rows[i].valFail;
      int vw=strlen(vs)*6;
      tft.setTextColor(vc,C_BG);
      tft.setCursor(SW-vw-2,y+3); tft.print(vs);
    }
  }

  tft.drawFastHLine(0,120,SW,C_DKGRAY);
  tftCentredText("System health overview",0,SW,123,C_DKGRAY,1,C_BG);
}

// ============================================================
//  SCREEN 5: SMART INFO (professional)
// ============================================================
void drawScreen5_Smart(){
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,7,C_WHITE);
  tft.drawRoundRect(2,2,156,124,6,C_DKGRAY);
  tft.drawFastVLine(79,8,112,C_DKGRAY);

  char nextB[12];
  if(!inSchedule()) snprintf(nextB,12,"%02d:00 AM",MOTOR_BLOCK_END);
  else snprintf(nextB,12,"IN WINDOW");
  const char* predS=predictedLevel<cfg.lowThr?"LOW":(predictedLevel<60?"MED":"HIGH");
  bool lActive=lpmSamples>3;
  char limB[8]; snprintf(limB,8,"%.0fV",cfg.safeVHigh);

  uiClockIcon(15,19,C_WHITE);
  tft.setTextColor(C_WHITE,C_BG); tft.setTextSize(1);
  tft.setCursor(26,11); tft.print(F("NEXT FILL"));
  tft.setTextColor(C_MAGENTA,C_BG); tft.setCursor(26,21); tft.print(nextB);
  tft.drawFastHLine(10,33,64,C_DKGRAY);

  uiTrendIcon(10,47,C_WHITE);
  tft.setTextColor(C_WHITE,C_BG); tft.setCursor(26,38); tft.print(F("PRED LVL"));
  tft.setTextColor(C_MAGENTA,C_BG); tft.setCursor(26,48); tft.print(predS);
  tft.drawFastHLine(10,60,64,C_DKGRAY);

  uiBrainIcon(15,74,C_WHITE);
  tft.setTextColor(C_WHITE,C_BG); tft.setCursor(26,65); tft.print(F("LEARNING"));
  tft.setTextColor(C_MAGENTA,C_BG); tft.setCursor(26,75); tft.print(lActive?F("ACTIVE"):F("CAL"));
  tft.drawFastHLine(10,87,64,C_DKGRAY);

  drawLightningIcon(9,95,C_YELLOW);
  tft.setTextColor(C_WHITE,C_BG); tft.setCursor(26,92); tft.print(F("AC LIMIT"));
  tft.setTextColor(C_MAGENTA,C_BG); tft.setCursor(26,102); tft.print(limB);

  int pArc=210+(int)(predictedLevel*1.2f);
  if(pArc>330) pArc=330;
  uiArcThin(119,35,20,210,330,C_DKGRAY);
  uiArcThin(119,35,20,210,pArc,C_MAGENTA);
  uiCalendarIcon(111,31,C_PURPLE);

  tft.setTextColor(C_WHITE,C_BG);
  tftCentredText("ESTIMATED",80,158,56,C_WHITE,1,C_BG);
  tftCentredText("NEXT 24 HRS",80,158,66,C_WHITE,1,C_BG);
  char pB[10]; snprintf(pB,10,"%.0f%%",predictedLevel);
  tftCentredText(pB,80,158,83,C_MAGENTA,2,C_BG);
}

static void uiChevron(int x,int y,uint16_t color){
  tft.drawLine(x,y,x+4,y+4,color);
  tft.drawLine(x+4,y+4,x,y+8,color);
}

static void uiGearIcon(int cx,int cy,uint16_t color){
  tft.drawCircle(cx,cy,4,color);
  tft.drawCircle(cx,cy,5,color);
  tft.drawFastVLine(cx,cy-8,3,color);
  tft.drawFastVLine(cx,cy+6,3,color);
  tft.drawFastHLine(cx-8,cy,3,color);
  tft.drawFastHLine(cx+6,cy,3,color);
  tft.drawPixel(cx-6,cy-6,color);
  tft.drawPixel(cx+6,cy-6,color);
  tft.drawPixel(cx-6,cy+6,color);
  tft.drawPixel(cx+6,cy+6,color);
}

static void uiBellIcon(int cx,int cy,uint16_t color){
  tft.drawRoundRect(cx-5,cy-3,11,8,4,color);
  tft.drawLine(cx-6,cy+5,cx+6,cy+5,color);
  tft.drawCircle(cx,cy+7,1,color);
  tft.drawCircle(cx,cy-5,1,color);
}

static void uiResetIcon(int cx,int cy,uint16_t color){
  for(int a=200;a<=500;a+=8){
    float r=a*0.01745f;
    int x=cx+(int)roundf(cosf(r)*7);
    int y=cy+(int)roundf(sinf(r)*7);
    tft.drawPixel(x,y,color);
  }
  tft.drawLine(cx-7,cy,cx-11,cy-2,color);
  tft.drawLine(cx-7,cy,cx-10,cy+3,color);
}

static void uiTankSmallIcon(int cx,int cy,uint16_t color){
  tft.drawRoundRect(cx-6,cy-8,13,16,2,color);
  tft.drawFastHLine(cx-5,cy+1,11,color);
  tft.drawFastVLine(cx+10,cy-7,14,color);
  tft.drawFastVLine(cx+12,cy-7,14,color);
}

static void drawSettingsRow(int y,const char* title,const char* subtitle,
                            void (*iconFn)(int,int,uint16_t),bool highlight,
                            const char* rightText){
  const int x=6,w=148,h=19;
  uint16_t bg=highlight?0x014A:0x0861;
  uint16_t border=highlight?C_CYAN:C_DKGRAY;
  uint16_t titleColor=highlight?C_CYAN:C_WHITE;
  tft.fillRoundRect(x,y,w,h,4,bg);
  tft.drawRoundRect(x,y,w,h,4,border);
  tft.fillRect(x+2,y+2,2,h-4,highlight?C_CYAN:C_MDBLUE);
  tft.drawFastVLine(x+22,y+4,h-8,C_DKGRAY);
  iconFn(x+12,y+9,highlight?C_CYAN:C_LTBLUE);
  tft.setTextSize(1);
  tft.setTextColor(titleColor,bg);
  tft.setCursor(x+28,y+3); tft.print(title);
  tft.setTextColor(C_LGRAY,bg);
  tft.setCursor(x+28,y+11); tft.print(subtitle);
  if(rightText){
    int tw=strlen(rightText)*6;
    tft.setTextColor(highlight?C_YELLOW:C_WHITE,bg);
    tft.setCursor(x+w-15-tw,y+6); tft.print(rightText);
  }
  uiChevron(x+w-10,y+5,highlight?C_CYAN:C_GRAY);
}

void drawScreen6_Settings(){
  tft.fillScreen(C_BG);
  tft.drawRoundRect(0,0,160,128,7,C_DKGRAY);
  tft.drawFastHLine(8,6,144,C_CYAN);

  char modeB[8];
  snprintf(modeB,sizeof(modeB),"%s",opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MAN":"MNT");
  char lowB[12];
  snprintf(lowB,sizeof(lowB),"%.0f%%",cfg.lowThr);
  char highB[12];
  snprintf(highB,sizeof(highB),"%.0f%%",cfg.highThr);
  char resetB[12];
  snprintf(resetB,sizeof(resetB),"%.0fL",dailyUsage);
  char tankB[12];
  snprintf(tankB,sizeof(tankB),"%.0fcm",cfg.tankH);

  drawSettingsRow(12, "MODE",  "auto/manual", uiGearIcon,      settingsIndex==0, modeB);
  drawSettingsRow(34, "LOW",   "auto start",  uiBellIcon,      settingsIndex==1, lowB);
  drawSettingsRow(56, "HIGH",  "auto stop",   uiClockIcon,     settingsIndex==2, highB);
  drawSettingsRow(78, "RESET", "today used",  uiResetIcon,     settingsIndex==3, resetB);
  drawSettingsRow(100,"HEIGHT","tank depth",   uiTankSmallIcon, settingsIndex==4, tankB);
}
// ============================================================
//  DISPLAY TASK
// ============================================================
static int prevScreen=-1;
static float prevLevel=-1,prevVolt=-1,prevUsage=-1;
static bool prevPump=false;
static bool prevMqtt=false;
static unsigned long lastFullRedraw=0;

void DisplayTask(void* pv){
  for(;;){
    hbDisplay=millis();
    if(TAKE_MUTEX(xTftMutex,50)){
      bool screenChanged=(currentScreen!=lastScreen);
      if(screenChanged){
        // Diagonal sweep transition
        for(int step=0;step<22;step++){
          for(int y=0;y<SH;y+=8){
            int x=step*10-y/2;
            if(x>-12&&x<SW)tft.fillRect(x,y,18,8,C_BG);
          }
          vTaskDelay(pdMS_TO_TICKS(6));
        }
        lastScreen=currentScreen; uiRedraw=true;
        prevLevel=-1; prevVolt=-1; prevUsage=-1; prevPump=!pumpRunning;
        lastFullRedraw=millis();
      }

      bool needFull=uiRedraw||(millis()-lastFullRedraw>30000);
      bool valChanged=(fabsf(smoothLevel-prevLevel)>0.5f||
                       fabsf(smoothVoltage-prevVolt)>2.0f||
                       fabsf(dailyUsage-prevUsage)>0.5f||
                       prevPump!=pumpRunning||
                       prevMqtt!=mqtt.connected());

      bool isMain=(currentScreen==SCR_MAIN);
      bool anim=isMain&&pumpRunning; // [V8-7] animation only when pumping

      if(needFull||valChanged||anim){
        if(needFull)lastFullRedraw=millis();
        switch(currentScreen){
          case SCR_MAIN:
            if(pumpRunning) drawScreen0_Robot(needFull);
            else            drawScreen0_Gauge(needFull);
            break;
          case SCR_ALERTS:   drawScreen2_Alerts();   break;
          case SCR_USAGE:    drawScreen3_Usage();    break;
          case SCR_SYSTEM:   drawScreen4_System();   break;
          case SCR_SMART:    drawScreen5_Smart();    break;
          case SCR_SETTINGS: drawScreen6_Settings(); break;
        }
        prevLevel=smoothLevel; prevVolt=smoothVoltage;
        prevUsage=dailyUsage;  prevPump=pumpRunning;
        prevMqtt=mqtt.connected();
        uiRedraw=false;
      }
      GIVE_MUTEX(xTftMutex);
    }
    // Animation: faster when robot is showing (15 FPS), slower otherwise
    vTaskDelay(pdMS_TO_TICKS((currentScreen==SCR_MAIN&&pumpRunning)?65:200));
  }
}

// ============================================================
//  MQTT TASK (Core 0)
// ============================================================
const char* resetReasonText(esp_reset_reason_t reason){
  switch(reason){
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

void mqttPublishDebug(){
  if(!mqtt.connected()||!debugEnabled)return;

  char payload[360];
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");

  snprintf(payload,sizeof(payload),
    "{\"fw\":\"%s\",\"build\":\"%s %s\",\"chip\":\"%s\",\"cores\":%u,\"cpu_mhz\":%u,\"sdk\":\"%s\",\"reset\":\"%s\",\"flash\":%u}",
    FW_VERSION,__DATE__,__TIME__,ESP.getChipModel(),ESP.getChipCores(),ESP.getCpuFreqMHz(),
    ESP.getSdkVersion(),resetReasonText(esp_reset_reason()),ESP.getFlashChipSize());
  mqtt.publish(T_DBG_INFO,payload,true);

  snprintf(payload,sizeof(payload),
    "{\"uptime_s\":%lu,\"heap\":%u,\"min_heap\":%u,\"max_alloc\":%u,\"wifi\":%s,\"rssi\":%d,\"ip\":\"%s\",\"mqtt\":%s,\"state\":\"%s\",\"mode\":\"%s\",\"pump\":%s,\"sleep_req\":%s}",
    millis()/1000,ESP.getFreeHeap(),ESP.getMinFreeHeap(),ESP.getMaxAllocHeap(),
    WiFi.isConnected()?"true":"false",WiFi.isConnected()?WiFi.RSSI():0,ip.c_str(),
    mqtt.connected()?"true":"false",sysStateStr[sysState],
    opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MANUAL":"MAINTENANCE",
    pumpRunning?"true":"false",sleepRequested?"true":"false");
  mqtt.publish(T_DBG_LIVE,payload);

  snprintf(payload,sizeof(payload),
    "{\"level\":%.1f,\"raw_level\":%.1f,\"distance_cm\":%.1f,\"voltage\":%.1f,\"sensor_ok\":%s,\"volt_ok\":%s,\"leak_score\":%d,\"eta_min\":%.1f,\"slope\":%.2f}",
    smoothLevel,waterLevel,lastDistanceCm,smoothVoltage,(usN>0||maLevel.count>0)?"true":"false",
    isVoltSafe(smoothVoltage)?"true":"false",lastLeakScore,fillEtaMin,currentSlope);
  mqtt.publish(T_DBG_SENS,payload);

  snprintf(payload,sizeof(payload),
    "{\"ac_v\":%.1f,\"meter\":\"ZMPT only\",\"motor_current\":\"not_measured\",\"motor_power\":\"not_measured\",\"esp_5v\":\"not_measured\",\"box_temp\":\"not_measured\"}",
    smoothVoltage);
  mqtt.publish(T_DBG_POWER,payload);

  unsigned long now=millis();
  snprintf(payload,sizeof(payload),
    "{\"sensor_lag_ms\":%lu,\"control_lag_ms\":%lu,\"button_lag_ms\":%lu,\"display_lag_ms\":%lu,\"mqtt_lag_ms\":%lu,\"mqtt_connects\":%lu,\"mqtt_fails\":%lu,\"alerts\":%d}",
    now-hbSensor,now-hbControl,now-hbButton,now-hbDisplay,now-hbMqtt,
    (unsigned long)mqttConnectCount,(unsigned long)mqttFailCount,activeAlerts);
  mqtt.publish(T_DBG_TASKS,payload);

  snprintf(payload,sizeof(payload),
    "{\"pump_start\":\"%s\",\"pump_stop\":\"%s\",\"very_low\":%s,\"volt_warn\":%s,\"leak\":%s,\"pump_timeout\":%s,\"dry_run_checked\":%s}",
    pumpStartReason,pumpStopReason,veryLow?"true":"false",voltWarn?"true":"false",
    leakConfirmed?"true":"false",pumpTimedOut?"true":"false",dryRunChk?"true":"false");
  mqtt.publish(T_DBG_EVENT,payload);
}

bool verifyHmac(const char* cmd,const char* tok){
  uint8_t result[32]; mbedtls_md_context_t ctx;
  mbedtls_md_type_t type=MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(type),1);
  mbedtls_md_hmac_starts(&ctx,(const uint8_t*)g_hmacSecret,strlen(g_hmacSecret));
  mbedtls_md_hmac_update(&ctx,(const uint8_t*)cmd,strlen(cmd));
  mbedtls_md_hmac_finish(&ctx,result); mbedtls_md_free(&ctx);
  char expected[65]; for(int i=0;i<32;i++)sprintf(expected+i*2,"%02x",result[i]);
  expected[64]=0; return strncmp(expected,tok,64)==0;
}

void mqttCallback(char* topic,byte* payload,unsigned int length){
  char msg[80]={0}; memcpy(msg,payload,min((unsigned int)79,length));
  char* pipe=strchr(msg,'|'); if(!pipe){MQTT_LOG("No HMAC");return;}
  *pipe='\0'; const char* cmd=msg; const char* tok=pipe+1;
  if(!verifyHmac(cmd,tok)){MQTT_LOG("HMAC fail");return;}
  if(!strcmp(topic,T_CMD_PMP)){
    if(!strcmp(cmd,"ON")&&opMode==MODE_MANUAL)startPump("MQTT command");
    else if(!strcmp(cmd,"OFF")&&pumpRunning)requestPumpStop("MQTT command");
  } else if(!strcmp(topic,T_CMD_MOD)){
    if(!strcmp(cmd,"AUTO")){opMode=MODE_AUTO;saveSettings();}
    else if(!strcmp(cmd,"MANUAL"))opMode=MODE_MANUAL;
  } else if(!strcmp(topic,T_CMD_RST)){
    if(!strcmp(cmd,"RESET"))ESP.restart();
  } else if(!strcmp(topic,T_CMD_DBG)){
    if(!strcmp(cmd,"ON")){
      debugEnabled=true;
      debugLastPub=0;
      MQTT_LOG("Debug enabled");
    } else if(!strcmp(cmd,"OFF")){
      debugEnabled=false;
      MQTT_LOG("Debug disabled");
    }
  }
}

void mqttReconnect(){
  static unsigned long last=0;
  if(millis()-last<10000)return; last=millis();
  mqtt.setSocketTimeout(5);
  bool ok=mqtt.connect(MQTT_CLIENT_ID,cfg.mqttUser,cfg.mqttPass,T_STATUS,1,true,"offline");
  if(ok){
    mqttConnectCount++;
    mqtt.subscribe(T_CMD_PMP); mqtt.subscribe(T_CMD_MOD); mqtt.subscribe(T_CMD_RST); mqtt.subscribe(T_CMD_DBG);
    mqtt.publish(T_STATUS,"online",true);
    MQTT_LOG("Connected");
  } else { mqttFailCount++; MQTT_LOG("Failed rc=%d",mqtt.state()); }
}

void mqttPublish(){
  if(!mqtt.connected())return;
  char buf[32];
  dtostrf(smoothLevel,4,1,buf);   mqtt.publish(T_LEVEL,buf);
  dtostrf(smoothVoltage,5,1,buf); mqtt.publish(T_VOLT,buf);
  dtostrf(totalLitres,6,1,buf);   mqtt.publish(T_USAGE,buf);
  dtostrf(fillEtaMin,5,1,buf);    mqtt.publish(T_ETA,buf);
  mqtt.publish(T_PUMP,pumpRunning?"ON":"OFF",true);
  mqtt.publish(T_MODE,opMode==MODE_AUTO?"AUTO":opMode==MODE_MANUAL?"MANUAL":"MAINTENANCE");
  mqtt.publish(T_FSM_STATE,sysStateStr[sysState]);
  snprintf(buf,32,"{\"runs\":%lu,\"hrs\":%.1f}",health.cycles,health.runSecs/3600.0f);
  mqtt.publish(T_HEALTH,buf);
}

void MqttTask(void* pv){
  bool mqttEnabled=(strlen(cfg.broker)>0);
  for(;;){
    ArduinoOTA.handle();
    if(WiFi.isConnected()&&mqttEnabled){
      hbMqtt=millis();
      if(!mqtt.connected())mqttReconnect();
      mqtt.loop();
      if(millis()-mqttLastPub>MQTT_PUB_MS){ mqttLastPub=millis(); mqttPublish(); }
      if(debugEnabled&&millis()-debugLastPub>DEBUG_PUB_MS){ debugLastPub=millis(); mqttPublishDebug(); }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup(){
  Serial.begin(115200); delay(300);
  Serial.println(F("\n============================================"));
  Serial.println(F(" SMART WATER LEVEL INDICATOR  v8.0"));
  Serial.println(F(" Professional UI — FreeRTOS Edition"));
  Serial.println(F("============================================"));

  // GPIO
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PERIPH_5V_EN);
  gpio_hold_dis((gpio_num_t)RELAY_PIN);
  pinMode(PERIPH_5V_EN,OUTPUT); digitalWrite(PERIPH_5V_EN,HIGH);
  delay(500);
  pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,HIGH);
  pinMode(TRIG_PIN,OUTPUT); digitalWrite(TRIG_PIN,LOW);
  pinMode(ECHO_PIN,INPUT);
  pinMode(RELAY_PIN,OUTPUT); digitalWrite(RELAY_PIN,LOW);
  pinMode(LED_R,OUTPUT); pinMode(LED_G,OUTPUT); pinMode(LED_B,OUTPUT);
  pinMode(BTN1_PIN,INPUT_PULLUP); pinMode(BTN2_PIN,INPUT_PULLUP);
  setLED(false,false,false);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN),echoISR,CHANGE);

  esp_sleep_wakeup_cause_t wc=esp_sleep_get_wakeup_cause();
  if(wc==ESP_SLEEP_WAKEUP_EXT0)  INFO("Woke: BTN1 pressed");
  else if(wc==ESP_SLEEP_WAKEUP_TIMER) INFO("Woke: 06:00 timer");
  else INFO("Normal boot");

  // TFT boot screen
  tft.initR(INITR_BLACKTAB); tft.setRotation(3); tft.fillScreen(C_BG);
  tft.setTextColor(C_CYAN,C_BG); tft.setTextSize(1);
  tft.fillRoundRect(10,10,140,108,8,C_DKGRAY);
  tft.drawRoundRect(10,10,140,108,8,C_CYAN);
  tft.setCursor(30,22); tft.print(F("SMART WATER LEVEL"));
  tft.setCursor(54,34); tft.print(F("v8.0 Pro"));
  tft.drawFastHLine(20,44,120,C_DKGRAY);
  tft.setTextColor(C_WHITE,C_DKGRAY);
  tft.setCursor(22,50); tft.print(F("Initialising system..."));

  EEPROM.begin(EEPROM_SZ);
  loadSettings(); loadHealth(); loadLitres(); loadUsage(); loadSecrets();

  // AceButton
  btnCfg.setEventHandler(handleButton);
  btnCfg.setFeature(ButtonConfig::kFeatureClick);
  btnCfg.clearFeature(ButtonConfig::kFeatureLongPress);
  btnCfg.setClickDelay(500);
  btnCfg.setDebounceDelay(30);
  btn1.setButtonConfig(&btnCfg); btn2.setButtonConfig(&btnCfg);
  btn1.init(BTN1_PIN,LOW); btn2.init(BTN2_PIN,LOW);

  // WiFiManager
  WiFiManager wm; wm.setConfigPortalTimeout(120);
  WiFiManagerParameter pBroker("broker","MQTT Broker",cfg.broker,79);
  WiFiManagerParameter pPort("port","MQTT Port","8883",5);
  WiFiManagerParameter pUser("user","MQTT User",cfg.mqttUser,31);
  WiFiManagerParameter pPass("pass","MQTT Pass",cfg.mqttPass,39);
  wm.addParameter(&pBroker); wm.addParameter(&pPort);
  wm.addParameter(&pUser);   wm.addParameter(&pPass);
  tft.setTextColor(C_CYAN,C_DKGRAY); tft.setCursor(22,62); tft.print(F("Connecting WiFi..."));
  if(wm.autoConnect("WaterTank-Setup")){
    INFO("WiFi OK: %s  IP:%s",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
    strncpy(cfg.broker,pBroker.getValue(),79); cfg.broker[79]='\0';
    cfg.mqttPort=atoi(pPort.getValue());
    strncpy(cfg.mqttUser,pUser.getValue(),31); cfg.mqttUser[31]='\0';
    strncpy(cfg.mqttPass,pPass.getValue(),39); cfg.mqttPass[39]='\0';
    saveSettings();
    if(strlen(g_brokerCA)>64)secClient.setCACert(g_brokerCA);
    else secClient.setInsecure();
    mqtt.setServer(cfg.broker,cfg.mqttPort);
    mqtt.setBufferSize(512);
    mqtt.setCallback(mqttCallback);
    ArduinoOTA.setHostname("water-tank-esp32");
    ArduinoOTA.setPassword(g_otaPass);
    ArduinoOTA.onStart([](){
      if(pumpRunning){
        digitalWrite(RELAY_PIN,LOW); pumpRunning=false;
        PUMP("Motor forced OFF - reason=OTA update");
      }
    });
    ArduinoOTA.begin();
    configTime(0,0,"pool.ntp.org","time.nist.gov");
    setenv("TZ",TZ_STRING,1); tzset();
    tft.setCursor(22,74); tft.print(F("WiFi connected!"));
  } else {
    WARN("WiFi offline");
    tft.setCursor(22,74); tft.print(F("WiFi offline mode"));
  }
  if(strlen(cfg.broker)>0){
    mqtt.setServer(cfg.broker,cfg.mqttPort);
    mqtt.setBufferSize(512);
    mqtt.setCallback(mqttCallback);
  }

  // Initial voltage
  { float s=0; for(int i=0;i<80;i++){s+=(analogRead(VOLT_PIN)/4095.0f)*3.3f;delayMicroseconds(200);}
    vDcBias=s/80;
    float sq=0; for(int i=0;i<400;i++){float v=((analogRead(VOLT_PIN)/4095.0f)*3.3f-vDcBias);sq+=v*v;delayMicroseconds(250);}
    currentVoltage=sqrtf(sq/400)*VOLT_CALIB;
    maVolt.push(currentVoltage); smoothVoltage=currentVoltage;
  }

  // Init slope history
  { unsigned long now=millis(); for(int i=0;i<SLOPE_WINDOW;i++){levelHistory[i]=0;levelHistoryTime[i]=now;} }
  leakSnap=waterLevel; leakTimer=millis(); pauseTimer=millis();
  litresSaveTmr=millis(); pumpStopTime=millis(); lastButtonTime=millis();
  bootTimeMs=millis();

  // Init drop array
  memset(r_drops,0,sizeof(r_drops));

  // RTOS
  xStateMutex  = xSemaphoreCreateRecursiveMutex();
  xTftMutex    = xSemaphoreCreateRecursiveMutex();
  xBuzzerQueue = xQueueCreate(1,sizeof(BzRequest));

  tft.fillScreen(C_BG);
  INFO("Starting FreeRTOS tasks...");

  xTaskCreatePinnedToCore(SensorTask,  "Sensor",  4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(ControlTask, "Control", 4096, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(BuzzerTask,  "Buzzer",  2048, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(ButtonTask,  "Button",  2048, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(DisplayTask, "Display", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(MqttTask,    "MQTT",    8192, nullptr, 3, nullptr, 0);

  INFO("All tasks started. System v8.0 running.");
}

// loop() suspended
void loop(){ vTaskDelay(portMAX_DELAY); }
