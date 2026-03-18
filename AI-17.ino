#include <SoftwareWire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <SPI.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include "SdFat.h"
#include "sdios.h"

// ====================  BLUETOOTH CONFIGURATION ====================
#define BT_SERIAL Serial1  // Using Serial1 (TX=18, RX=19)
#define BT_BAUD_RATE 9600  // Default baud rate (will auto-detect 38400 if needed)

// Bluetooth command buffer - reduced from 1281
#define BT_BUFFER_SIZE 64
char btBuffer[BT_BUFFER_SIZE];
uint8_t btBufferIndex = 0;
unsigned long lastBtSendTime = 0;
#define BT_SEND_INTERVAL 2000  // Send data every 2 seconds
bool btConnected = false;
unsigned long lastBtActivityTime = 0;
#define BT_TIMEOUT 5000  // Consider disconnected after 5s of no activity

// Shared response buffer for command responses (saves RAM vs per-function buffers)
#define RESPONSE_BUFFER_SIZE 100
char responseBuffer[RESPONSE_BUFFER_SIZE];
// ==================================================================

// ====================  WIFI CONFIGURATION (ESP-01S) ====================
#define WIFI_SERIAL Serial2  // Using Serial2 (TX=16, RX=17)
#define WIFI_BAUD_RATE 115200  // ESP-01S default baud rate

// WiFi command buffer - reduced from 256
#define WIFI_BUFFER_SIZE 128
char wifiBuffer[WIFI_BUFFER_SIZE];
uint8_t wifiBufferIndex = 0;
unsigned long lastWifiSendTime = 0;
bool wifiConnected = false;
bool wifiClientConnected = false;
unsigned long lastWifiActivityTime = 0;
#define WIFI_TIMEOUT 30000  // Consider client disconnected after 30s

// WiFi credentials storage
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 48  // Reduced from 64
char wifiSSID[WIFI_SSID_MAX_LEN] = "";
char wifiPassword[WIFI_PASS_MAX_LEN] = "";
uint16_t wifiPort = 8266;  // Default TCP server port

// WiFi state machine
enum WifiState : uint8_t {
  WIFI_STATE_IDLE,
  WIFI_STATE_INITIALIZING,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_SERVER_STARTED,
  WIFI_STATE_CLIENT_CONNECTED,
  WIFI_STATE_ERROR
};
WifiState wifiState = WIFI_STATE_IDLE;
unsigned long wifiStateTimeout = 0;
bool wifiEnabled = false;
char wifiIPAddress[16] = "0.0.0.0";

// Flag to pause automatic sensor data transmission (for settings screens)
bool wifiDataPaused = false;

// Auto-reconnect variables
unsigned long lastWifiHealthCheck = 0;
#define WIFI_HEALTH_CHECK_INTERVAL 30000  // Check every 30 seconds
unsigned long wifiReconnectAttemptTime = 0;
#define WIFI_RECONNECT_DELAY 15000  // Wait 15 seconds between reconnect attempts
uint8_t wifiReconnectAttempts = 0;
#define MAX_WIFI_RECONNECT_ATTEMPTS 10  // Max attempts before giving up

// Track which channel (BT or WiFi) last sent a command for responses
enum ResponseChannel : uint8_t {
  CHANNEL_NONE,
  CHANNEL_BLUETOOTH,
  CHANNEL_WIFI
};
ResponseChannel lastCommandChannel = CHANNEL_NONE;

// ==================== DEBUG FLAGS ====================
// Set to 1 to enable debug output for each category, 0 to disable
// Reducing debug output saves dynamic memory and reduces serial traffic
#define DEBUG_WIFI_STATE 0      // WiFi state machine transitions
#define DEBUG_WIFI_BUFFER 0     // WiFi buffer contents (very verbose)
#define DEBUG_WIFI_RX 0         // WiFi received bytes
#define DEBUG_WIFI_TX 1         // WiFi transmitted messages (keep for troubleshooting)
#define DEBUG_COMMANDS 1        // Command received/processed (keep for troubleshooting)
#define DEBUG_PARSING 0         // Parsing details
#define DEBUG_SENSOR_DATA 0     // Sensor reading details
#define DEBUG_BLUETOOTH 0       // Bluetooth communication
#define DEBUG_RELAYS 0          // Relay state changes
#define DEBUG_EEPROM 0          // EEPROM read/write operations
// =====================================================

// ======================================================================

#define SD_CS_PIN SS  // Chip select pin for SD card

// Create software I2C instances for sensors
SoftwareWire softWire1(2, 3);   // SDA on pin 2, SCL on pin 3 (Sensor 1)
SoftwareWire softWire2(4, 5);   // SDA on pin 4, SCL on pin 5 (Sensor 2)
SoftwareWire softWire3(42, 43); // SDA on pin 42, SCL on pin 43 (Ambient Sensor)

// SCD41 I2C address and commands
#define SCD41_I2C_ADDR 0x62
#define SCD41_START_PERIODIC_MEASUREMENT 0x21b1
#define SCD41_READ_MEASUREMENT 0xec05
#define SCD41_STOP_PERIODIC_MEASUREMENT 0x3f86
#define SCD41_WAKE_UP 0x36f6

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 24

// EEPROM Address Map - Define specific address for every stored value
const uint16_t EEPROM_MAGIC = 0xAB59;  // Changed: removed lights, added independent humidity relay 2
const int EEPROM_MAGIC_ADDR = 0;  // 2 bytes (uint16_t)
const int TEMP_UNIT_ADDR = 2;     // 1 byte (bool) - only used for storing units preference now

// Humidity addresses for Sensor 1 (2 bytes each - uint16_t)
const int HUMIDITY_S1_MAX_ADDR = 3;
const int HUMIDITY_S1_MIN_ADDR = 5;

// Humidity addresses for Sensor 2 (2 bytes each - uint16_t)
const int HUMIDITY_S2_MAX_ADDR = 62;
const int HUMIDITY_S2_MIN_ADDR = 64;

// CO2 addresses for Sensor 1 Fruit
const int CO2_S1_FRUIT_MAX_ADDR = 7;
const int CO2_S1_FRUIT_MIN_ADDR = 9;

// CO2 addresses for Sensor 1 Pin
const int CO2_S1_PIN_MAX_ADDR = 41;
const int CO2_S1_PIN_MIN_ADDR = 43;

// CO2 Mode address for Sensor 1 (1 byte - bool)
const int CO2_S1_MODE_ADDR = 45;

// CO2 Delay addresses for Sensor 1 (4 bytes each - uint32_t)
const int CO2_S1_DELAY_DAYS_ADDR = 46;
const int CO2_S1_DELAY_HOURS_ADDR = 50;
const int CO2_S1_DELAY_MINUTES_ADDR = 54;
const int CO2_S1_DELAY_SECONDS_ADDR = 58;

// CO2 addresses for Sensor 2 Fruit
const int CO2_S2_FRUIT_MAX_ADDR = 66;
const int CO2_S2_FRUIT_MIN_ADDR = 68;

// CO2 addresses for Sensor 2 Pin
const int CO2_S2_PIN_MAX_ADDR = 70;
const int CO2_S2_PIN_MIN_ADDR = 72;

// CO2 Mode address for Sensor 2 (1 byte - bool)
const int CO2_S2_MODE_ADDR = 74;

// CO2 Delay addresses for Sensor 2 (4 bytes each - uint32_t)
const int CO2_S2_DELAY_DAYS_ADDR = 75;
const int CO2_S2_DELAY_HOURS_ADDR = 79;
const int CO2_S2_DELAY_MINUTES_ADDR = 83;
const int CO2_S2_DELAY_SECONDS_ADDR = 87;

// Data Logging Interval addresses (4 bytes each - uint32_t)
const int LOG_DAYS_ADDR = 23;
const int LOG_HOURS_ADDR = 27;
const int LOG_MINUTES_ADDR = 31;
const int LOG_SECONDS_ADDR = 35;
const int LOG_STATUS_ADDR = 39;  // 1 byte (bool)

// Sensor Enable/Disable addresses
const int SENSOR_S1_ENABLE_ADDR = 103;
const int SENSOR_S2_ENABLE_ADDR = 104;

// Calibration addresses for Sensor 1 (signed 16-bit values)
const int CAL_S1_TEMP_ADDR = 91;
const int CAL_S1_HUMIDITY_ADDR = 93;
const int CAL_S1_CO2_ADDR = 95;

// Calibration addresses for Sensor 2 (signed 16-bit values)
const int CAL_S2_TEMP_ADDR = 97;
const int CAL_S2_HUMIDITY_ADDR = 99;
const int CAL_S2_CO2_ADDR = 101;

// NEW: FAT (Forced Air Time) addresses for Sensor 1 (Feature 1)
const int CO2_S1_FAT_INTERVAL_DAYS_ADDR = 105;     // 4 bytes
const int CO2_S1_FAT_INTERVAL_HOURS_ADDR = 109;    // 4 bytes
const int CO2_S1_FAT_INTERVAL_MINUTES_ADDR = 113;  // 4 bytes
const int CO2_S1_FAT_INTERVAL_SECONDS_ADDR = 117;  // 4 bytes
const int CO2_S1_FAT_DURATION_DAYS_ADDR = 121;     // 4 bytes
const int CO2_S1_FAT_DURATION_HOURS_ADDR = 125;    // 4 bytes
const int CO2_S1_FAT_DURATION_MINUTES_ADDR = 129;  // 4 bytes
const int CO2_S1_FAT_DURATION_SECONDS_ADDR = 133;  // 4 bytes
const int CO2_S1_FAT_ENABLE_ADDR = 137;            // 1 byte (bool)

// NEW: FAT (Forced Air Time) addresses for Sensor 2 (Feature 1)
const int CO2_S2_FAT_INTERVAL_DAYS_ADDR = 138;     // 4 bytes
const int CO2_S2_FAT_INTERVAL_HOURS_ADDR = 142;    // 4 bytes
const int CO2_S2_FAT_INTERVAL_MINUTES_ADDR = 146;  // 4 bytes
const int CO2_S2_FAT_INTERVAL_SECONDS_ADDR = 150;  // 4 bytes
const int CO2_S2_FAT_DURATION_DAYS_ADDR = 154;     // 4 bytes
const int CO2_S2_FAT_DURATION_HOURS_ADDR = 158;    // 4 bytes
const int CO2_S2_FAT_DURATION_MINUTES_ADDR = 162;  // 4 bytes
const int CO2_S2_FAT_DURATION_SECONDS_ADDR = 166;  // 4 bytes
const int CO2_S2_FAT_ENABLE_ADDR = 170;            // 1 byte (bool)

// NEW: Ambient Sensor Calibration addresses (starting at 171)
const int CAL_AMBIENT_TEMP_ADDR = 171;    // 2 bytes (int16_t)
const int CAL_AMBIENT_HUMIDITY_ADDR = 173; // 2 bytes (int16_t)
const int CAL_AMBIENT_CO2_ADDR = 175;     // 2 bytes (int16_t)
const int AMBIENT_ENABLE_ADDR = 177;      // 1 byte (bool) - On/Off toggle
const int AMBIENT_CO2_OFFSET_ADDR = 178;  // 2 bytes (int16_t) - CO2 offset for ambient override

// NEW: WiFi configuration addresses (starting at 180)
const int WIFI_ENABLED_ADDR = 180;        // 1 byte (bool)
const int WIFI_SSID_ADDR = 181;           // 32 bytes (char array)
const int WIFI_PASS_ADDR = 213;           // 64 bytes (char array)
const int WIFI_PORT_ADDR = 277;           // 2 bytes (uint16_t)
// Next available address: 279

// Calibration variables
int16_t calS1Temp = 0;
int16_t calS1Humidity = 0;
int16_t calS1CO2 = 0;
int16_t calS2Temp = 0;
int16_t calS2Humidity = 0;
int16_t calS2CO2 = 0;

// NEW: Ambient sensor calibration variables
int16_t calAmbientTemp = 0;
int16_t calAmbientHumidity = 0;
int16_t calAmbientCO2 = 0;
bool ambientEnabled = false;  // Default OFF
int16_t ambientCO2Offset = 0;  // Default 0 ppm - replaces hardcoded 20ppm

// Sensor enable/disable variables
bool sensor1Enabled = true;
bool sensor2Enabled = true;

// Menu scrolling variables - use smaller types
int8_t mainMenuScrollOffset = 0;
int8_t subMenuScrollOffset = 0;

// Menu structure constants - UPDATED for new FAT items
#define NUM_MENU_ITEMS 10
#define NUM_SUB_ITEMS 2
#define HUMIDITY_SUB_ITEMS 4
#define TEMP_SUB_ITEMS 1
#define CO2_SUB_ITEMS 9
#define CALIBRATE_SUB_ITEMS 5
#define WIFI_SUB_ITEMS 5

// Menu display constants for scrolling
#define MENU_DISPLAY_LINES 6
#define SUBMENU_DISPLAY_LINES 5

// Menu array with names of Main Menu items - stored in PROGMEM
const char menu0[] PROGMEM = "Humidity";
const char menu1[] PROGMEM = "Temperature";
const char menu2[] PROGMEM = "CO2-Sensor1";
const char menu3[] PROGMEM = "CO2-Sensor2";
const char menu4[] PROGMEM = "Data Logging";
const char menu5[] PROGMEM = "Date/Time";
const char menu6[] PROGMEM = "Calibrate-S1";
const char menu7[] PROGMEM = "Calibrate-S2";
const char menu8[] PROGMEM = "Ambient-Calibrate";
const char menu9[] PROGMEM = "WiFi Config";
const char* const menuItems[] PROGMEM = {menu0, menu1, menu2, menu3, menu4, menu5, menu6, menu7, menu8, menu9};

// Helper function to get menu item from PROGMEM
char menuBuffer[20];  // Shared buffer for menu string retrieval
const char* getMenuItem(uint8_t index) {
  strcpy_P(menuBuffer, (char*)pgm_read_word(&(menuItems[index])));
  return menuBuffer;
}

// Default values for settings
const uint16_t HUMIDITY_MAX_DEFAULT = 850;
const uint16_t HUMIDITY_MIN_DEFAULT = 800;

// CO2 defaults
const uint16_t CO2_DEFAULT_MAX = 2000;
const uint16_t CO2_DEFAULT_MIN = 1500;
const uint16_t CO2_MAX_RANGE = 10000;

const uint16_t CO2_PIN_DEFAULT_MAX = 1000;
const uint16_t CO2_PIN_DEFAULT_MIN = 800;
const bool CO2_MODE_DEFAULT = true;

// Date/Time constants
const int YEAR_MIN = 2000;
const int YEAR_MAX = 2099;

// RTC pin definitions
#define RTC_DAT 27
#define RTC_CLK 11
#define RTC_RST 28

// Joystick pins
const int JOY_X = A1;
const int JOY_Y = A0;
const int JOY_BTN = 30;

// Relay pins
#define HUMIDITY_S1_RELAY_PIN 34
#define CO2_RELAY_PIN 35
#define CO2_S2_RELAY_PIN 36
#define HUMIDITY_S2_RELAY_PIN 37

// Hardware instances
ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST);
RtcDS1302<ThreeWire> rtc(myWire);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  25, 26, 23, OLED_RESET, 22);

SdFat sd;
File logFile;

// For storing which date component is being edited
int8_t dateEditComponent = 0;

// For storing temporary edit values
int16_t dateTimeTemp[3];
int16_t tempValues[4];

// Values array: [0]=humidity S1, [1]=CO2_S1, [2]=CO2_S2
#define VAL_HUM 0
#define VAL_CO2_S1 1
#define VAL_CO2_S2 2
int16_t values[3][NUM_SUB_ITEMS];

// Sensor reading variables
float currentHumidity1 = -1.0;
float currentHumidity2 = -1.0;
float currentTemperature1 = -1.0;
float currentTemperature2 = -1.0;
float currentCO2_1 = 0.0;
float currentCO2_2 = 0.0;

// Ambient sensor readings
float ambientCO2 = 0.0;
float ambientTemperature = -1.0;
float ambientHumidity = -1.0;

// Last sent sensor values (for change detection to reduce WiFi traffic)
float lastSentCO2_1 = -999.0;
float lastSentTemp1 = -999.0;
float lastSentHum1 = -999.0;
float lastSentCO2_2 = -999.0;
float lastSentTemp2 = -999.0;
float lastSentHum2 = -999.0;
float lastSentCO2_Amb = -999.0;
float lastSentTempAmb = -999.0;
float lastSentHumAmb = -999.0;
bool lastSentRelayHum = false;
bool lastSentRelayCO2S1 = false;
bool lastSentRelayCO2S2 = false;
bool lastSentRelayHum2 = false;
unsigned long lastHeartbeatTime = 0;
#define HEARTBEAT_INTERVAL 30000  // Send data at least every 30 seconds

// Consolidated CO2 sensor state struct (replaces duplicated S1/S2 variables)
struct SensorCO2State {
    uint16_t pinMax, pinMin;
    bool mode;
    uint16_t delayInterval[4];
    bool pinModeActive;
    unsigned long modeStartTime, delayEndTime;
    bool delayActive;
    uint16_t FATInterval[4], FATDuration[4];
    bool FATEnable, FATActive;
    unsigned long relayOffTime, FATEndTime;
};
SensorCO2State co2Sensors[2] = {
    {0, 0, true, {0,0,0,0}, false, 0, 0, false, {0,0,0,0}, {0,0,0,0}, false, false, 0, 0},
    {0, 0, true, {0,0,0,0}, false, 0, 0, false, {0,0,0,0}, {0,0,0,0}, false, false, 0, 0}
};

// Edit component trackers
int8_t co2DelayEditComponent = 0;
int8_t fatEditComponent = 0;

// Humidity sensor 2 settings
uint16_t humidityS2Max, humidityS2Min;

// For tracking timing
unsigned long previousMillis = 0;
const long readInterval = 5000;

int8_t currentMenu = 0;    // Changed from -1 to 0 to match AI-7 style
int8_t subMenu = -1;       // -1 means show main menu, >= 0 means show submenu
int8_t editingValue = -1;
bool useFahrenheit = true;
bool savePrompt = false;
bool showSplash = true;

// Logging variables
bool isLogging = false;
uint16_t logInterval[4] = {0, 0, 1, 0};  // Reduced from uint32_t
unsigned long lastLogTime = 0;
unsigned long nextLogTime = 0;
int8_t intervalEditComponent = 0;
bool sdCardPresent = false;
#define SD_CHECK_INTERVAL 10000
unsigned long lastSDCheckTime = 0;

// Function declarations
static void scrollSubMenu(int y, int totalItems);
void handleInput();
void updateDisplay();
void showSplashScreen();
void updateCO2Relay(uint8_t sensorIdx, uint8_t relayPin, float currentCO2);
void updateRelays();
void readSensors();
float celsiusToFahrenheit(float celsius);
void startPeriodicMeasurement(SoftwareWire& wire);
void wakeUpSensor(SoftwareWire& wire);
bool readMeasurement(SoftwareWire& wire, float& co2, float& temperature, float& humidity);
uint8_t calculateCRC(uint8_t data[], uint8_t len);
const char* getTimeString(int minutes);
bool fileExists(const char* filename);
void saveCO2DelaySettings(uint8_t idx);
void updateNextLogTime();
void logDataToSD();
void debugAllValues();
void debugCalibrationValues();

// Function declarations for FAT feature
void saveCO2FATSettings(uint8_t idx);

// Bluetooth function prototypes
void initBluetooth();
void handleBluetoothCommands();
void sendSensorData();
void processBluetoothCommand(char* command);
void sendBluetoothResponse(const char* response);
void sendBluetoothError(const char* error);

// WiFi function prototypes
void initWifi();
void handleWifiCommands();
void processWifiCommand(char* command);
void sendWifiResponse(const char* response);
void sendWifiError(const char* error);
void sendResponse(const char* response);  // Unified response sender
void sendError(const char* error);        // Unified error sender
bool sendATCommand(const char* cmd, const char* expectedResponse, unsigned long timeout);
bool waitForResponse(const char* expectedResponse, unsigned long timeout);
void wifiStateMachine();
void connectToWifi();
void startTcpServer();
void loadWifiCredentials();
void saveWifiCredentials();
bool checkWifiHealth();
void attemptWifiReconnect();
void wifiMaintenance();


void setup() {
    Serial.begin(115200);
    
    // Initialize Bluetooth
    initBluetooth();
    
    // Initialize WiFi (ESP-01S)
    initWifi();

    if(!display.begin(SSD1306_SWITCHCAPVCC)) {
        for(;;);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    
    // Initialize software I2C for all three SCD41 sensors
    softWire1.begin();
    softWire2.begin();
    softWire3.begin();  // NEW: Ambient sensor
    
    // Initialize RTC
    rtc.Begin();
    
    // Wake up all three sensors
    wakeUpSensor(softWire1);
    wakeUpSensor(softWire2);
    wakeUpSensor(softWire3);  // NEW: Ambient sensor
    delay(20);
    
    // Start periodic measurements on all three sensors
    startPeriodicMeasurement(softWire1);
    startPeriodicMeasurement(softWire2);
    startPeriodicMeasurement(softWire3);  // NEW: Ambient sensor

    // Serial.println("All three SCD41 sensors initialized");
    delay(5000);
    
    // Initial SD card check
    sdCardPresent = sd.begin(SD_CS_PIN);
    lastSDCheckTime = millis();
    
    if (!sdCardPresent) {
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("SD Card Error!");
        display.display();
        delay(2000);
        
        isLogging = false;
        EEPROM.write(LOG_STATUS_ADDR, 0);
        // Serial.println("SD Card not detected - Data Logging disabled");
    } else {
        // Serial.println("SD Card detected at startup");
        
        // Check if log file exists, if not, create it with headers
        if (!fileExists("MFC-LOG.TXT")) {
            if (logFile.open("MFC-LOG.TXT", O_WRONLY | O_CREAT)) {
                // Updated headers to include ambient sensor and FAT info
                logFile.println("Date,Time,S1Enabled,HumidityS1Max,HumidityS1Min,HumidityS1,S2Enabled,HumidityS2Max,HumidityS2Min,HumidityS2,HumidityS1Relay,CO2S1FruitMax,CO2S1FruitMin,CO2S1PinMax,CO2S1PinMin,CO2S1Mode,CO2S1,CO2S1Relay,CO2S1Delay,CO2S1FATActive,CO2S2FruitMax,CO2S2FruitMin,CO2S2PinMax,CO2S2PinMin,CO2S2Mode,CO2S2,CO2S2Relay,CO2S2Delay,CO2S2FATActive,AmbientCO2,TempS1,TempS2,AmbientTemp,Unit");
                logFile.close();
                // Serial.println("Created log file with headers for all sensors including ambient");
            }
        }
    }
    
    // Set pin modes
    pinMode(JOY_BTN, INPUT_PULLUP);
    pinMode(HUMIDITY_S1_RELAY_PIN, OUTPUT);
    pinMode(CO2_RELAY_PIN, OUTPUT);
    pinMode(CO2_S2_RELAY_PIN, OUTPUT);
    pinMode(HUMIDITY_S2_RELAY_PIN, OUTPUT);

    // Initialize relay states to OFF
    digitalWrite(HUMIDITY_S1_RELAY_PIN, HIGH);
    digitalWrite(CO2_RELAY_PIN, HIGH);
    digitalWrite(CO2_S2_RELAY_PIN, HIGH);
    digitalWrite(HUMIDITY_S2_RELAY_PIN, HIGH);

    // Check if EEPROM needs initialization
    uint16_t magic;
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);
    bool needsInit = (magic != EEPROM_MAGIC);

    if (needsInit) {
        // Serial.println("Initializing EEPROM with default values");
        
        EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
        
        EEPROM.write(TEMP_UNIT_ADDR, 1);
        useFahrenheit = true;
        
        // Humidity defaults
        uint16_t humidityS1Max = 850;
        uint16_t humidityS1Min = 800;
        EEPROM.put(HUMIDITY_S1_MAX_ADDR, humidityS1Max);
        EEPROM.put(HUMIDITY_S1_MIN_ADDR, humidityS1Min);
        values[VAL_HUM][0] = humidityS1Max;
        values[VAL_HUM][1] = humidityS1Min;
        
        EEPROM.put(HUMIDITY_S2_MAX_ADDR, (uint16_t)850);
        EEPROM.put(HUMIDITY_S2_MIN_ADDR, (uint16_t)800);
        humidityS2Max = 850;
        humidityS2Min = 800;
        
        // CO2 defaults for sensor 1
        uint16_t co2S1FruitMax = 2000;
        uint16_t co2S1FruitMin = 1500;
        EEPROM.put(CO2_S1_FRUIT_MAX_ADDR, co2S1FruitMax);
        EEPROM.put(CO2_S1_FRUIT_MIN_ADDR, co2S1FruitMin);
        values[VAL_CO2_S1][0] = co2S1FruitMax;
        values[VAL_CO2_S1][1] = co2S1FruitMin;
        
        EEPROM.put(CO2_S1_PIN_MAX_ADDR, CO2_PIN_DEFAULT_MAX);
        EEPROM.put(CO2_S1_PIN_MIN_ADDR, CO2_PIN_DEFAULT_MIN);
        co2Sensors[0].pinMax = CO2_PIN_DEFAULT_MAX;
        co2Sensors[0].pinMin = CO2_PIN_DEFAULT_MIN;

        EEPROM.write(CO2_S1_MODE_ADDR, CO2_MODE_DEFAULT);
        co2Sensors[0].mode = CO2_MODE_DEFAULT;

        // CO2 delay defaults for sensor 1
        EEPROM.put(CO2_S1_DELAY_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_DELAY_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_DELAY_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_DELAY_SECONDS_ADDR, (uint32_t)0);
        co2Sensors[0].delayInterval[0] = 0;
        co2Sensors[0].delayInterval[1] = 0;
        co2Sensors[0].delayInterval[2] = 0;
        co2Sensors[0].delayInterval[3] = 0;

        // NEW: FAT defaults for sensor 1
        EEPROM.put(CO2_S1_FAT_INTERVAL_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_INTERVAL_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_DURATION_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_DURATION_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_DURATION_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S1_FAT_DURATION_SECONDS_ADDR, (uint32_t)0);
        EEPROM.write(CO2_S1_FAT_ENABLE_ADDR, 0);
        co2Sensors[0].FATInterval[0] = 0;
        co2Sensors[0].FATInterval[1] = 0;
        co2Sensors[0].FATInterval[2] = 0;
        co2Sensors[0].FATInterval[3] = 0;
        co2Sensors[0].FATDuration[0] = 0;
        co2Sensors[0].FATDuration[1] = 0;
        co2Sensors[0].FATDuration[2] = 0;
        co2Sensors[0].FATDuration[3] = 0;
        co2Sensors[0].FATEnable = false;
        
        // CO2 defaults for sensor 2
        uint16_t co2S2FruitMax = 2000;
        uint16_t co2S2FruitMin = 1500;
        EEPROM.put(CO2_S2_FRUIT_MAX_ADDR, co2S2FruitMax);
        EEPROM.put(CO2_S2_FRUIT_MIN_ADDR, co2S2FruitMin);
        values[VAL_CO2_S2][0] = co2S2FruitMax;
        values[VAL_CO2_S2][1] = co2S2FruitMin;
        
        EEPROM.put(CO2_S2_PIN_MAX_ADDR, CO2_PIN_DEFAULT_MAX);
        EEPROM.put(CO2_S2_PIN_MIN_ADDR, CO2_PIN_DEFAULT_MIN);
        co2Sensors[1].pinMax = CO2_PIN_DEFAULT_MAX;
        co2Sensors[1].pinMin = CO2_PIN_DEFAULT_MIN;

        EEPROM.write(CO2_S2_MODE_ADDR, CO2_MODE_DEFAULT);
        co2Sensors[1].mode = CO2_MODE_DEFAULT;

        // CO2 delay defaults for sensor 2
        EEPROM.put(CO2_S2_DELAY_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_DELAY_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_DELAY_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_DELAY_SECONDS_ADDR, (uint32_t)0);
        co2Sensors[1].delayInterval[0] = 0;
        co2Sensors[1].delayInterval[1] = 0;
        co2Sensors[1].delayInterval[2] = 0;
        co2Sensors[1].delayInterval[3] = 0;

        // NEW: FAT defaults for sensor 2
        EEPROM.put(CO2_S2_FAT_INTERVAL_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_INTERVAL_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_DURATION_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_DURATION_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_DURATION_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_S2_FAT_DURATION_SECONDS_ADDR, (uint32_t)0);
        EEPROM.write(CO2_S2_FAT_ENABLE_ADDR, 0);
        co2Sensors[1].FATInterval[0] = 0;
        co2Sensors[1].FATInterval[1] = 0;
        co2Sensors[1].FATInterval[2] = 0;
        co2Sensors[1].FATInterval[3] = 0;
        co2Sensors[1].FATDuration[0] = 0;
        co2Sensors[1].FATDuration[1] = 0;
        co2Sensors[1].FATDuration[2] = 0;
        co2Sensors[1].FATDuration[3] = 0;
        co2Sensors[1].FATEnable = false;
        
        // NEW: Initialize ambient sensor calibration values
        EEPROM.put(CAL_AMBIENT_TEMP_ADDR, (int16_t)0);
        EEPROM.put(CAL_AMBIENT_HUMIDITY_ADDR, (int16_t)0);
        EEPROM.put(CAL_AMBIENT_CO2_ADDR, (int16_t)0);
        
        // NEW: Initialize ambient enable state to OFF (default)
        EEPROM.write(AMBIENT_ENABLE_ADDR, 0);
        ambientEnabled = false;
        
        // NEW: Initialize ambient CO2 offset to 0 (default)
        EEPROM.put(AMBIENT_CO2_OFFSET_ADDR, (int16_t)0);
        ambientCO2Offset = 0;

        // Sensor enable defaults
        EEPROM.put(SENSOR_S1_ENABLE_ADDR, 1);
        EEPROM.put(SENSOR_S2_ENABLE_ADDR, 1);
        sensor1Enabled = true;
        sensor2Enabled = true;

        // Logging defaults
        EEPROM.put(LOG_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(LOG_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(LOG_MINUTES_ADDR, (uint32_t)1);
        EEPROM.put(LOG_SECONDS_ADDR, (uint32_t)0);
        logInterval[0] = 0;
        logInterval[1] = 0;
        logInterval[2] = 1;
        logInterval[3] = 0;

        // Calibration defaults
        EEPROM.put(CAL_S1_TEMP_ADDR, (int16_t)0);
        EEPROM.put(CAL_S1_HUMIDITY_ADDR, (int16_t)0);
        EEPROM.put(CAL_S1_CO2_ADDR, (int16_t)0);
        EEPROM.put(CAL_S2_TEMP_ADDR, (int16_t)0);
        EEPROM.put(CAL_S2_HUMIDITY_ADDR, (int16_t)0);
        EEPROM.put(CAL_S2_CO2_ADDR, (int16_t)0);
        calS1Temp = 0;
        calS1Humidity = 0;
        calS1CO2 = 0;
        calS2Temp = 0;
        calS2Humidity = 0;
        calS2CO2 = 0;
        
        EEPROM.write(LOG_STATUS_ADDR, 0);
        isLogging = false;

        // Serial.println("EEPROM initialized with defaults");
    } else {
        // Load all values from EEPROM
        
        useFahrenheit = (EEPROM.read(TEMP_UNIT_ADDR) == 1);
        
        // Load humidity values
        uint16_t humidityS1Max, humidityS1Min;
        EEPROM.get(HUMIDITY_S1_MAX_ADDR, humidityS1Max);
        EEPROM.get(HUMIDITY_S1_MIN_ADDR, humidityS1Min);
        values[VAL_HUM][0] = humidityS1Max;
        values[VAL_HUM][1] = humidityS1Min;
        
        EEPROM.get(HUMIDITY_S2_MAX_ADDR, humidityS2Max);
        EEPROM.get(HUMIDITY_S2_MIN_ADDR, humidityS2Min);
        
        // Load CO2 values for sensor 1
        uint16_t co2S1FruitMax, co2S1FruitMin;
        EEPROM.get(CO2_S1_FRUIT_MAX_ADDR, co2S1FruitMax);
        EEPROM.get(CO2_S1_FRUIT_MIN_ADDR, co2S1FruitMin);
        values[VAL_CO2_S1][0] = co2S1FruitMax;
        values[VAL_CO2_S1][1] = co2S1FruitMin;
        
        EEPROM.get(CO2_S1_PIN_MAX_ADDR, co2Sensors[0].pinMax);
        EEPROM.get(CO2_S1_PIN_MIN_ADDR, co2Sensors[0].pinMin);

        co2Sensors[0].mode = (EEPROM.read(CO2_S1_MODE_ADDR) == 1);

        // Load CO2 delay values for sensor 1
        uint16_t intervalValue;
        EEPROM.get(CO2_S1_DELAY_DAYS_ADDR, intervalValue);
        co2Sensors[0].delayInterval[0] = intervalValue;
        EEPROM.get(CO2_S1_DELAY_HOURS_ADDR, intervalValue);
        co2Sensors[0].delayInterval[1] = intervalValue;
        EEPROM.get(CO2_S1_DELAY_MINUTES_ADDR, intervalValue);
        co2Sensors[0].delayInterval[2] = intervalValue;
        EEPROM.get(CO2_S1_DELAY_SECONDS_ADDR, intervalValue);
        co2Sensors[0].delayInterval[3] = intervalValue;

        // NEW: Load FAT values for sensor 1
        EEPROM.get(CO2_S1_FAT_INTERVAL_DAYS_ADDR, intervalValue);
        co2Sensors[0].FATInterval[0] = intervalValue;
        EEPROM.get(CO2_S1_FAT_INTERVAL_HOURS_ADDR, intervalValue);
        co2Sensors[0].FATInterval[1] = intervalValue;
        EEPROM.get(CO2_S1_FAT_INTERVAL_MINUTES_ADDR, intervalValue);
        co2Sensors[0].FATInterval[2] = intervalValue;
        EEPROM.get(CO2_S1_FAT_INTERVAL_SECONDS_ADDR, intervalValue);
        co2Sensors[0].FATInterval[3] = intervalValue;
        EEPROM.get(CO2_S1_FAT_DURATION_DAYS_ADDR, intervalValue);
        co2Sensors[0].FATDuration[0] = intervalValue;
        EEPROM.get(CO2_S1_FAT_DURATION_HOURS_ADDR, intervalValue);
        co2Sensors[0].FATDuration[1] = intervalValue;
        EEPROM.get(CO2_S1_FAT_DURATION_MINUTES_ADDR, intervalValue);
        co2Sensors[0].FATDuration[2] = intervalValue;
        EEPROM.get(CO2_S1_FAT_DURATION_SECONDS_ADDR, intervalValue);
        co2Sensors[0].FATDuration[3] = intervalValue;
        co2Sensors[0].FATEnable = (EEPROM.read(CO2_S1_FAT_ENABLE_ADDR) == 1);
        
        // Load CO2 values for sensor 2
        uint16_t co2S2FruitMax, co2S2FruitMin;
        EEPROM.get(CO2_S2_FRUIT_MAX_ADDR, co2S2FruitMax);
        EEPROM.get(CO2_S2_FRUIT_MIN_ADDR, co2S2FruitMin);
        values[VAL_CO2_S2][0] = co2S2FruitMax;
        values[VAL_CO2_S2][1] = co2S2FruitMin;
        
        EEPROM.get(CO2_S2_PIN_MAX_ADDR, co2Sensors[1].pinMax);
        EEPROM.get(CO2_S2_PIN_MIN_ADDR, co2Sensors[1].pinMin);

        co2Sensors[1].mode = (EEPROM.read(CO2_S2_MODE_ADDR) == 1);

        // Load CO2 delay values for sensor 2
        EEPROM.get(CO2_S2_DELAY_DAYS_ADDR, intervalValue);
        co2Sensors[1].delayInterval[0] = intervalValue;
        EEPROM.get(CO2_S2_DELAY_HOURS_ADDR, intervalValue);
        co2Sensors[1].delayInterval[1] = intervalValue;
        EEPROM.get(CO2_S2_DELAY_MINUTES_ADDR, intervalValue);
        co2Sensors[1].delayInterval[2] = intervalValue;
        EEPROM.get(CO2_S2_DELAY_SECONDS_ADDR, intervalValue);
        co2Sensors[1].delayInterval[3] = intervalValue;

        // NEW: Load FAT values for sensor 2
        EEPROM.get(CO2_S2_FAT_INTERVAL_DAYS_ADDR, intervalValue);
        co2Sensors[1].FATInterval[0] = intervalValue;
        EEPROM.get(CO2_S2_FAT_INTERVAL_HOURS_ADDR, intervalValue);
        co2Sensors[1].FATInterval[1] = intervalValue;
        EEPROM.get(CO2_S2_FAT_INTERVAL_MINUTES_ADDR, intervalValue);
        co2Sensors[1].FATInterval[2] = intervalValue;
        EEPROM.get(CO2_S2_FAT_INTERVAL_SECONDS_ADDR, intervalValue);
        co2Sensors[1].FATInterval[3] = intervalValue;
        EEPROM.get(CO2_S2_FAT_DURATION_DAYS_ADDR, intervalValue);
        co2Sensors[1].FATDuration[0] = intervalValue;
        EEPROM.get(CO2_S2_FAT_DURATION_HOURS_ADDR, intervalValue);
        co2Sensors[1].FATDuration[1] = intervalValue;
        EEPROM.get(CO2_S2_FAT_DURATION_MINUTES_ADDR, intervalValue);
        co2Sensors[1].FATDuration[2] = intervalValue;
        EEPROM.get(CO2_S2_FAT_DURATION_SECONDS_ADDR, intervalValue);
        co2Sensors[1].FATDuration[3] = intervalValue;
        co2Sensors[1].FATEnable = (EEPROM.read(CO2_S2_FAT_ENABLE_ADDR) == 1);
        
        // NEW: Load ambient sensor calibration values
        EEPROM.get(CAL_AMBIENT_TEMP_ADDR, calAmbientTemp);
        EEPROM.get(CAL_AMBIENT_HUMIDITY_ADDR, calAmbientHumidity);
        EEPROM.get(CAL_AMBIENT_CO2_ADDR, calAmbientCO2);
        
        // NEW: Load ambient enable state
        ambientEnabled = EEPROM.read(AMBIENT_ENABLE_ADDR);
        
        // NEW: Load ambient CO2 offset
        EEPROM.get(AMBIENT_CO2_OFFSET_ADDR, ambientCO2Offset);


        // Load logging values
        EEPROM.get(LOG_DAYS_ADDR, intervalValue);
        logInterval[0] = intervalValue;
        EEPROM.get(LOG_HOURS_ADDR, intervalValue);
        logInterval[1] = intervalValue;
        EEPROM.get(LOG_MINUTES_ADDR, intervalValue);
        logInterval[2] = intervalValue;
        EEPROM.get(LOG_SECONDS_ADDR, intervalValue);
        logInterval[3] = intervalValue;
        
        isLogging = (EEPROM.read(LOG_STATUS_ADDR) == 1);
        
        // Load calibration values
        int16_t calValue;
        EEPROM.get(CAL_S1_TEMP_ADDR, calValue);
        calS1Temp = calValue;
        EEPROM.get(CAL_S1_HUMIDITY_ADDR, calValue);
        calS1Humidity = calValue;
        EEPROM.get(CAL_S1_CO2_ADDR, calValue);
        calS1CO2 = calValue;
        EEPROM.get(CAL_S2_TEMP_ADDR, calValue);
        calS2Temp = calValue;
        EEPROM.get(CAL_S2_HUMIDITY_ADDR, calValue);
        calS2Humidity = calValue;
        EEPROM.get(CAL_S2_CO2_ADDR, calValue);
        calS2CO2 = calValue;
        
        // Load sensor enable flags
        sensor1Enabled = (EEPROM.read(SENSOR_S1_ENABLE_ADDR) == 1);
        sensor2Enabled = (EEPROM.read(SENSOR_S2_ENABLE_ADDR) == 1);

        // Serial.println("EEPROM values loaded");
    }
    
    // Initialize timing for data logging
    if(isLogging) {
        updateNextLogTime();
    }
    
    // Set initial relay off times for FAT tracking
    co2Sensors[0].relayOffTime = millis();
    co2Sensors[1].relayOffTime = millis();
}

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0 / 5.0) + 32.0;
}

// CRC calculation for SCD41
uint8_t calculateCRC(uint8_t data[], uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

void wakeUpSensor(SoftwareWire& wire) {
    wire.beginTransmission(SCD41_I2C_ADDR);
    wire.write(SCD41_WAKE_UP >> 8);
    wire.write(SCD41_WAKE_UP & 0xFF);
    wire.endTransmission();
}

void startPeriodicMeasurement(SoftwareWire& wire) {
    wire.beginTransmission(SCD41_I2C_ADDR);
    wire.write(SCD41_START_PERIODIC_MEASUREMENT >> 8);
    wire.write(SCD41_START_PERIODIC_MEASUREMENT & 0xFF);
    wire.endTransmission();
}

bool readMeasurement(SoftwareWire& wire, float& co2, float& temperature, float& humidity) {
    wire.beginTransmission(SCD41_I2C_ADDR);
    wire.write(SCD41_READ_MEASUREMENT >> 8);
    wire.write(SCD41_READ_MEASUREMENT & 0xFF);
    if(wire.endTransmission() != 0) {
        return false;
    }
    
    delay(1);
    
    uint8_t data[9];
    if(wire.requestFrom(SCD41_I2C_ADDR, 9) != 9) {
        return false;
    }
    
    for(int i = 0; i < 9; i++) {
        data[i] = wire.read();
    }
    
    // Verify CRCs
    uint8_t crcData[2];
    crcData[0] = data[0]; crcData[1] = data[1];
    if(calculateCRC(crcData, 2) != data[2]) return false;
    
    crcData[0] = data[3]; crcData[1] = data[4];
    if(calculateCRC(crcData, 2) != data[5]) return false;
    
    crcData[0] = data[6]; crcData[1] = data[7];
    if(calculateCRC(crcData, 2) != data[8]) return false;
    
    // Extract values
    uint16_t co2Raw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t tempRaw = ((uint16_t)data[3] << 8) | data[4];
    uint16_t humRaw = ((uint16_t)data[6] << 8) | data[7];
    
    co2 = co2Raw;
    temperature = -45.0 + 175.0 * (tempRaw / 65536.0);
    humidity = 100.0 * (humRaw / 65536.0);
    
    return true;
}

void readSensors() {
    unsigned long currentMillis = millis();
    
    if(currentMillis - previousMillis >= readInterval) {
        previousMillis = currentMillis;
        
        // Read sensor 1
        float co2_1, temp_1, hum_1;
        if(readMeasurement(softWire1, co2_1, temp_1, hum_1)) {
            // Apply calibrations
            currentCO2_1 = co2_1 + calS1CO2;
            currentTemperature1 = temp_1 + (calS1Temp / 10.0);
            currentHumidity1 = hum_1 + (calS1Humidity / 10.0);
            
            // Constrain values to reasonable ranges
            currentCO2_1 = constrain(currentCO2_1, 0, 10000);
            currentHumidity1 = constrain(currentHumidity1, 0, 100);
        }
        
        // Read sensor 2
        float co2_2, temp_2, hum_2;
        if(readMeasurement(softWire2, co2_2, temp_2, hum_2)) {
            // Apply calibrations
            currentCO2_2 = co2_2 + calS2CO2;
            currentTemperature2 = temp_2 + (calS2Temp / 10.0);
            currentHumidity2 = hum_2 + (calS2Humidity / 10.0);
            
            // Constrain values to reasonable ranges
            currentCO2_2 = constrain(currentCO2_2, 0, 10000);
            currentHumidity2 = constrain(currentHumidity2, 0, 100);
        }
        
        // NEW: Read Ambient Sensor ONLY if enabled
        if (ambientEnabled) {
            float rawAmbientCO2, rawAmbientTemp, rawAmbientHum;
            if (readMeasurement(softWire3, rawAmbientCO2, rawAmbientTemp, rawAmbientHum)) {
                ambientCO2 = rawAmbientCO2 + calAmbientCO2;
                ambientTemperature = rawAmbientTemp + (calAmbientTemp / 10.0);
                ambientHumidity = rawAmbientHum + (calAmbientHumidity / 10.0);
            }
        } else {
            // When disabled, reset ambient values to indicate no reading
            ambientCO2 = 0.0;
            ambientTemperature = -1.0;
            ambientHumidity = -1.0;
        }
    }
}

// Helper: CO2 relay control for one sensor (Step 3 deduplication)
void updateCO2Relay(uint8_t sensorIdx, uint8_t relayPin, float currentCO2) {
    bool sensorEnabled = (sensorIdx == 0) ? sensor1Enabled : sensor2Enabled;
    uint8_t valIdx = (sensorIdx == 0) ? VAL_CO2_S1 : VAL_CO2_S2;
    uint8_t modeAddr = (sensorIdx == 0) ? CO2_S1_MODE_ADDR : CO2_S2_MODE_ADDR;
    SensorCO2State& s = co2Sensors[sensorIdx];

    if (!sensorEnabled) {
        bool wasOn = (digitalRead(relayPin) == LOW);
        digitalWrite(relayPin, HIGH);
        if (wasOn) {
            s.relayOffTime = millis();
        }
    } else if (currentCO2 > 0 && currentCO2 < 10000) {
        bool relayCurrentlyOn = (digitalRead(relayPin) == LOW);
        bool normalControlWantsOn = false;
        bool ambientOverride = false;

        uint16_t co2Max, co2Min;

        if (s.mode) {  // Fruit mode
            co2Max = values[valIdx][0];
            co2Min = values[valIdx][1];
        } else {  // Pin mode
            co2Max = s.pinMax;
            co2Min = s.pinMin;

            // Pin mode delay logic
            if (!s.pinModeActive) {
                s.pinModeActive = true;

                unsigned long delayTimeMs = (s.delayInterval[0] * 24L * 60L * 60L * 1000L) +
                                            (s.delayInterval[1] * 60L * 60L * 1000L) +
                                            (s.delayInterval[2] * 60L * 1000L) +
                                            (s.delayInterval[3] * 1000L);

                if (delayTimeMs > 0) {
                    s.delayActive = true;
                    s.delayEndTime = millis() + delayTimeMs;
                }
            } else if (s.delayActive) {
                if (millis() >= s.delayEndTime) {
                    s.mode = true;
                    s.pinModeActive = false;
                    s.delayActive = false;
                    EEPROM.write(modeAddr, 1);
                }
            }
        }

        // Ambient CO2 override logic
        if (ambientEnabled && ambientCO2 > 0 && ambientCO2 < 10000) {
            if (ambientCO2 > co2Min) {
                if (currentCO2 < (ambientCO2 + ambientCO2Offset)) {
                    ambientOverride = true;
                }
            }
        }

        // Normal control
        if (currentCO2 > co2Max) {
            normalControlWantsOn = true;
        } else if (currentCO2 <= co2Min) {
            normalControlWantsOn = false;
        } else {
            normalControlWantsOn = relayCurrentlyOn;
        }

        // FAT (Forced Air Time) logic
        bool fatWantsOn = false;

        if (s.FATEnable && sensorEnabled) {
            unsigned long fatIntervalMs = (s.FATInterval[0] * 24L * 60L * 60L * 1000L) +
                                          (s.FATInterval[1] * 60L * 60L * 1000L) +
                                          (s.FATInterval[2] * 60L * 1000L) +
                                          (s.FATInterval[3] * 1000L);
            unsigned long fatDurationMs = (s.FATDuration[0] * 24L * 60L * 60L * 1000L) +
                                          (s.FATDuration[1] * 60L * 60L * 1000L) +
                                          (s.FATDuration[2] * 60L * 1000L) +
                                          (s.FATDuration[3] * 1000L);

            if (fatIntervalMs > 0 && fatDurationMs > 0) {
                if (s.FATActive) {
                    if (normalControlWantsOn) {
                        s.FATActive = false;
                        s.relayOffTime = millis();
                    } else if (millis() >= s.FATEndTime) {
                        s.FATActive = false;
                        s.relayOffTime = millis();
                    } else {
                        fatWantsOn = true;
                    }
                } else {
                    if (!relayCurrentlyOn) {
                        unsigned long timeOff = millis() - s.relayOffTime;
                        if (timeOff >= fatIntervalMs) {
                            s.FATActive = true;
                            s.FATEndTime = millis() + fatDurationMs;
                            fatWantsOn = true;
                        }
                    }
                }
            }
        }

        // Final relay decision
        bool shouldBeOn = false;

        if (ambientEnabled && ambientOverride) {
            shouldBeOn = fatWantsOn;
        } else if (fatWantsOn) {
            shouldBeOn = true;
        } else {
            shouldBeOn = normalControlWantsOn;
        }

        if (shouldBeOn) {
            digitalWrite(relayPin, LOW);
        } else {
            digitalWrite(relayPin, HIGH);
            if (relayCurrentlyOn) {
                s.relayOffTime = millis();
            }
        }
    }
}

// UPDATED: updateRelays function with FAT and Ambient CO2 features
void updateRelays() {
    // ===== Humidity S1 Relay Control (HUMIDITY_S1_RELAY_PIN) =====
    if (!sensor1Enabled || currentHumidity1 < 0 || currentHumidity1 > 100) {
        digitalWrite(HUMIDITY_S1_RELAY_PIN, HIGH);
    } else {
        if (currentHumidity1 < (values[VAL_HUM][1] / 10.0)) {
            digitalWrite(HUMIDITY_S1_RELAY_PIN, LOW);
        } else if (currentHumidity1 >= (values[VAL_HUM][0] / 10.0)) {
            digitalWrite(HUMIDITY_S1_RELAY_PIN, HIGH);
        }
    }

    // ===== Humidity S2 Relay Control (HUMIDITY_S2_RELAY_PIN) =====
    if (!sensor2Enabled || currentHumidity2 < 0 || currentHumidity2 > 100) {
        digitalWrite(HUMIDITY_S2_RELAY_PIN, HIGH);
    } else {
        if (currentHumidity2 < (humidityS2Min / 10.0)) {
            digitalWrite(HUMIDITY_S2_RELAY_PIN, LOW);
        } else if (currentHumidity2 >= (humidityS2Max / 10.0)) {
            digitalWrite(HUMIDITY_S2_RELAY_PIN, HIGH);
        }
    }

    // ===== CO2 Relay Control (parameterized helper called for both sensors) =====
    updateCO2Relay(0, CO2_RELAY_PIN, currentCO2_1);
    updateCO2Relay(1, CO2_S2_RELAY_PIN, currentCO2_2);
}

// MODIFIED: showSplashScreen() with conditional ambient display based on ambientEnabled
void showSplashScreen() {
  // Ensure display buffer is completely cleared
  display.clearDisplay();
  display.fillScreen(0);
  display.setTextSize(1);
  
  display.setCursor(0,0);
  
  // Get and display RTC time
  RtcDateTime dt = rtc.GetDateTime();
  
  // Format date DD/MM/YY
  if(dt.Month() < 10) display.print("0");
  display.print(dt.Month());
  display.print("/");
  if(dt.Day() < 10) display.print("0");
  display.print(dt.Day());
  display.print("/");
  display.print(dt.Year() % 100);
  display.print("     ");
  
  // Format time HH:MM:SS
  if(dt.Hour() < 10) display.print("0");
  display.print(dt.Hour());
  display.print(":");
  if(dt.Minute() < 10) display.print("0");
  display.print(dt.Minute());
  display.print(":");
  if(dt.Second() < 10) display.print("0");
  display.print(dt.Second());
  display.println();
  display.println();

  display.print("Hum: ");

  // Show Ambient Humidity ONLY if ambientEnabled
  if (ambientEnabled) {
      if(ambientHumidity >= 0) {
          display.print(ambientHumidity, 1);
          display.print("%");
      } else {
          display.print("--");
      }
      display.print(", ");
  }

  // Sensor 1 Humidity
  if (sensor1Enabled) {
      if(currentHumidity1 >= 0) {
          display.print(currentHumidity1, 1);
          display.print("%");
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.print(" ");

  // Sensor 2 Humidity
  if (sensor2Enabled) {
      if(currentHumidity2 >= 0) {
          display.print(currentHumidity2, 1);
          display.print("%");
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.println();

  display.print("Tmp: ");

  // Show Ambient Temperature ONLY if ambientEnabled
  if (ambientEnabled) {
      if(ambientTemperature > -40) {
          float displayTemp = useFahrenheit ? celsiusToFahrenheit(ambientTemperature) : ambientTemperature;
          display.print(displayTemp, 1);
          display.print(useFahrenheit ? "F" : "C");
      } else {
          display.print("--");
      }
      display.print(", ");
  }

  // Temperature S1
  if (sensor1Enabled) {
      if(currentTemperature1 > -40) {
          float displayTemp = useFahrenheit ? celsiusToFahrenheit(currentTemperature1) : currentTemperature1;
          display.print(displayTemp, 1);
          display.print(useFahrenheit ? "F" : "C");
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.print(" ");

  // Temperature S2
  if (sensor2Enabled) {
      if(currentTemperature2 > -40) {
          float displayTemp = useFahrenheit ? celsiusToFahrenheit(currentTemperature2) : currentTemperature2;
          display.print(displayTemp, 1);
          display.print(useFahrenheit ? "F" : "C");
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.println();

  // CO2 line with conditional ambient sensor display
  display.print("CO2: ");

  // Show Ambient CO2 ONLY if ambientEnabled
  if (ambientEnabled) {
      if(ambientCO2 > 0) {
          display.print((uint16_t)ambientCO2);
      } else {
          display.print("--");
      }
      display.print(", ");
  }

  // CO2 S1
  if (sensor1Enabled) {
      if(currentCO2_1 > 0) {
          display.print((uint16_t)currentCO2_1);
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.print(", ");

  // CO2 S2
  if (sensor2Enabled) {
      if(currentCO2_2 > 0) {
          display.print((uint16_t)currentCO2_2);
      } else {
          display.print("--");
      }
  } else {
      display.print("OFF");
  }
  display.println();
  
  // Add separate line for CO2 modes
  display.print(co2Sensors[0].mode ? "S1:Fruit" : "S1:Pin");
  display.print(" ");
  display.print(co2Sensors[1].mode ? "S2:Fruit" : "S2:Pin");
  display.println();
  
  // Display data logging status message if logging is enabled
  if(isLogging) {
    display.println("Logging Data");
  }
  
  // Display WiFi IP address if WiFi is enabled and connected
  if(wifiEnabled && wifiConnected && strlen(wifiIPAddress) > 0 && strcmp(wifiIPAddress, "0.0.0.0") != 0) {
    display.print("WiFi: ");
    display.println(wifiIPAddress);
  } else if(!isLogging) {
    display.println();  // Keep spacing consistent
  }
  
  display.println("Menu - press button");
  
  // Ensure display is updated
  display.display();
}


void debugAllValues() {
    // Serial.println("\nEEPROM Values:");
    
    bool tempUnit = EEPROM.read(TEMP_UNIT_ADDR);
    // Serial.print("Temperature Unit: "); Serial.println(tempUnit ? "F" : "C");
    
    bool co2S1ModeVal = EEPROM.read(CO2_S1_MODE_ADDR);
    bool co2S2ModeVal = EEPROM.read(CO2_S2_MODE_ADDR);
    // Serial.print("CO2 S1 Mode: "); Serial.println(co2S1ModeVal ? "Fruit" : "Pin");
    // Serial.print("CO2 S2 Mode: "); Serial.println(co2S2ModeVal ? "Fruit" : "Pin");
    
    // Humidity values
    uint16_t humS1Max, humS1Min, humS2Max, humS2Min;
    EEPROM.get(HUMIDITY_S1_MAX_ADDR, humS1Max);
    EEPROM.get(HUMIDITY_S1_MIN_ADDR, humS1Min);
    EEPROM.get(HUMIDITY_S2_MAX_ADDR, humS2Max);
    EEPROM.get(HUMIDITY_S2_MIN_ADDR, humS2Min);
    
    // Serial.print("Humidity S1 Max: "); Serial.println(humS1Max / 10.0);
    // Serial.print("Humidity S1 Min: "); Serial.println(humS1Min / 10.0);
    // Serial.print("Humidity S2 Max: "); Serial.println(humS2Max / 10.0);
    // Serial.print("Humidity S2 Min: "); Serial.println(humS2Min / 10.0);
    
    // CO2 values
    uint16_t co2S1FruitMax, co2S1FruitMin, co2S1PinMaxVal, co2S1PinMinVal;
    EEPROM.get(CO2_S1_FRUIT_MAX_ADDR, co2S1FruitMax);
    EEPROM.get(CO2_S1_FRUIT_MIN_ADDR, co2S1FruitMin);
    EEPROM.get(CO2_S1_PIN_MAX_ADDR, co2S1PinMaxVal);
    EEPROM.get(CO2_S1_PIN_MIN_ADDR, co2S1PinMinVal);
    
    // Serial.print("CO2 S1 Fruit Max: "); Serial.println(co2S1FruitMax);
    // Serial.print("CO2 S1 Fruit Min: "); Serial.println(co2S1FruitMin);
    // Serial.print("CO2 S1 Pin Max: "); Serial.println(co2S1PinMaxVal);
    // Serial.print("CO2 S1 Pin Min: "); Serial.println(co2S1PinMinVal);
    
    uint16_t co2S2FruitMax, co2S2FruitMin, co2S2PinMaxVal, co2S2PinMinVal;
    EEPROM.get(CO2_S2_FRUIT_MAX_ADDR, co2S2FruitMax);
    EEPROM.get(CO2_S2_FRUIT_MIN_ADDR, co2S2FruitMin);
    EEPROM.get(CO2_S2_PIN_MAX_ADDR, co2S2PinMaxVal);
    EEPROM.get(CO2_S2_PIN_MIN_ADDR, co2S2PinMinVal);
    
    // Serial.print("CO2 S2 Fruit Max: "); Serial.println(co2S2FruitMax);
    // Serial.print("CO2 S2 Fruit Min: "); Serial.println(co2S2FruitMin);
    // Serial.print("CO2 S2 Pin Max: "); Serial.println(co2S2PinMaxVal);
    // Serial.print("CO2 S2 Pin Min: "); Serial.println(co2S2PinMinVal);
}

// Static buffer for time string (saves heap vs String)
char timeStrBuf[6];  // "HH:MM\0"

const char* getTimeString(int minutes) {
  int hours = minutes / 60;
  int mins = minutes % 60;
  timeStrBuf[0] = '0' + (hours / 10);
  timeStrBuf[1] = '0' + (hours % 10);
  timeStrBuf[2] = ':';
  timeStrBuf[3] = '0' + (mins / 10);
  timeStrBuf[4] = '0' + (mins % 10);
  timeStrBuf[5] = '\0';
  return timeStrBuf;
}

bool fileExists(const char* filename) {
  return sd.exists(filename);
}

void updateDisplay() {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextColor(WHITE);
    
    if(savePrompt) {
        display.println("Save changes?");
        display.println();
        display.println("Left: Cancel");
        display.println("Right: Save");
    }
    else if(subMenu >= 0) {
        // Show selected menu with submenu
        display.println(getMenuItem(currentMenu));
        display.println();
        
        if(subMenu >= 0) {
            // In submenu
            if(currentMenu == 0) {  // Humidity menu
                const char* humidityItems[] = {"S1-Max", "S1-Min", "S2-Max", "S2-Min"};
                for(int i = 0; i < HUMIDITY_SUB_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    display.print(humidityItems[i]);
                    display.print(": ");
                    
                    if(editingValue == i) {
                        float value = tempValues[i] / 10.0;
                        display.print(value, 1);
                        display.print(" %");
                    } else {
                        float value;
                        if(i < 2) {
                            value = values[VAL_HUM][i] / 10.0;
                        } else {
                            if(i == 2) value = humidityS2Max / 10.0;
                            else value = humidityS2Min / 10.0;
                        }
                        display.print(value, 1);
                        display.print(" %");
                    }
                    display.println();
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 1) {  // Temperature menu
                const char* tempItems[] = {"Units"};
                for(int i = 0; i < TEMP_SUB_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    display.print("Units: [");
                    if(!useFahrenheit) display.setTextColor(BLACK, WHITE);
                    display.print("C");
                    display.setTextColor(WHITE);
                    display.print("] [");
                    if(useFahrenheit) display.setTextColor(BLACK, WHITE);
                    display.print("F");
                    display.setTextColor(WHITE);
                    display.println("]");
                    
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 2 || currentMenu == 3) {  // CO2 menus - UPDATED for new items with SCROLLING
                // UPDATED: Added Interval, Duration, FAT to the menu
                const char* co2Items[] = {"Fruit-Max", "Fruit-Min", "Pin-Max", "Pin-Min", "Delay", "Mode", "Interval", "Duration", "FAT"};
                
                // Determine which sensor we're working with
                bool isSensor1 = (currentMenu == 2);
                
                // Calculate visible range for scrolling
                int startIdx = subMenuScrollOffset;
                int endIdx = min(CO2_SUB_ITEMS, startIdx + SUBMENU_DISPLAY_LINES);
                
                for(int i = startIdx; i < endIdx; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 5) {  // Mode selection
                        display.print("Mode: [");
                        bool currentMode = isSensor1 ? co2Sensors[0].mode : co2Sensors[1].mode;
                        if(currentMode) display.setTextColor(BLACK, WHITE);
                        display.print("Fruit");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(!currentMode) display.setTextColor(BLACK, WHITE);
                        display.print("Pin");
                        display.setTextColor(WHITE);
                        display.println("]");
                    }
                    else if(i == 4) {  // Delay
                        uint16_t* delayInterval = isSensor1 ? co2Sensors[0].delayInterval : co2Sensors[1].delayInterval;
                        bool delayActive = isSensor1 ? co2Sensors[0].delayActive : co2Sensors[1].delayActive;
                        unsigned long delayEndTime = isSensor1 ? co2Sensors[0].delayEndTime : co2Sensors[1].delayEndTime;
                        bool currentMode = isSensor1 ? co2Sensors[0].mode : co2Sensors[1].mode;
                        
                        display.print("Delay: ");
                        if(editingValue >= 0 && i == subMenu) {
                            // Editing mode
                            if(co2DelayEditComponent == 0) display.setTextColor(BLACK, WHITE);
                            display.print(delayInterval[0]);
                            display.setTextColor(WHITE);
                            display.print("d ");
                            
                            if(co2DelayEditComponent == 1) display.setTextColor(BLACK, WHITE);
                            if(delayInterval[1] < 10) display.print("0");
                            display.print(delayInterval[1]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(co2DelayEditComponent == 2) display.setTextColor(BLACK, WHITE);
                            if(delayInterval[2] < 10) display.print("0");
                            display.print(delayInterval[2]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(co2DelayEditComponent == 3) display.setTextColor(BLACK, WHITE);
                            if(delayInterval[3] < 10) display.print("0");
                            display.print(delayInterval[3]);
                            display.setTextColor(WHITE);
                        } else {
                            // Display mode
                            display.print(delayInterval[0]);
                            display.print("d ");
                            if(delayInterval[1] < 10) display.print("0");
                            display.print(delayInterval[1]);
                            display.print(":");
                            if(delayInterval[2] < 10) display.print("0");
                            display.print(delayInterval[2]);
                            display.print(":");
                            if(delayInterval[3] < 10) display.print("0");
                            display.print(delayInterval[3]);
                            
                            if(delayActive) {
                                unsigned long remainingTime = (delayEndTime - millis()) / 1000;
                                unsigned long hours = remainingTime / 3600;
                                unsigned long minutes = (remainingTime % 3600) / 60;
                                display.print(" (");
                                display.print(hours);
                                display.print("h");
                                display.print(minutes);
                                display.print("m)");
                            }
                        }
                        display.println();
                    }
                    else if(i == 6) {  // NEW: Interval
                        uint16_t* fatInterval = isSensor1 ? co2Sensors[0].FATInterval : co2Sensors[1].FATInterval;
                        
                        display.print("Interval: ");
                        if(editingValue >= 0 && i == subMenu) {
                            // Editing mode
                            if(fatEditComponent == 0) display.setTextColor(BLACK, WHITE);
                            display.print(fatInterval[0]);
                            display.setTextColor(WHITE);
                            display.print("d ");
                            
                            if(fatEditComponent == 1) display.setTextColor(BLACK, WHITE);
                            if(fatInterval[1] < 10) display.print("0");
                            display.print(fatInterval[1]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(fatEditComponent == 2) display.setTextColor(BLACK, WHITE);
                            if(fatInterval[2] < 10) display.print("0");
                            display.print(fatInterval[2]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(fatEditComponent == 3) display.setTextColor(BLACK, WHITE);
                            if(fatInterval[3] < 10) display.print("0");
                            display.print(fatInterval[3]);
                            display.setTextColor(WHITE);
                        } else {
                            display.print(fatInterval[0]);
                            display.print("d ");
                            if(fatInterval[1] < 10) display.print("0");
                            display.print(fatInterval[1]);
                            display.print(":");
                            if(fatInterval[2] < 10) display.print("0");
                            display.print(fatInterval[2]);
                            display.print(":");
                            if(fatInterval[3] < 10) display.print("0");
                            display.print(fatInterval[3]);
                        }
                        display.println();
                    }
                    else if(i == 7) {  // NEW: Duration
                        uint16_t* fatDuration = isSensor1 ? co2Sensors[0].FATDuration : co2Sensors[1].FATDuration;
                        bool fatActive = isSensor1 ? co2Sensors[0].FATActive : co2Sensors[1].FATActive;
                        unsigned long fatEndTime = isSensor1 ? co2Sensors[0].FATEndTime : co2Sensors[1].FATEndTime;
                        
                        display.print("Duration: ");
                        if(editingValue >= 0 && i == subMenu) {
                            // Editing mode
                            if(fatEditComponent == 0) display.setTextColor(BLACK, WHITE);
                            display.print(fatDuration[0]);
                            display.setTextColor(WHITE);
                            display.print("d ");
                            
                            if(fatEditComponent == 1) display.setTextColor(BLACK, WHITE);
                            if(fatDuration[1] < 10) display.print("0");
                            display.print(fatDuration[1]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(fatEditComponent == 2) display.setTextColor(BLACK, WHITE);
                            if(fatDuration[2] < 10) display.print("0");
                            display.print(fatDuration[2]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(fatEditComponent == 3) display.setTextColor(BLACK, WHITE);
                            if(fatDuration[3] < 10) display.print("0");
                            display.print(fatDuration[3]);
                            display.setTextColor(WHITE);
                        } else {
                            display.print(fatDuration[0]);
                            display.print("d ");
                            if(fatDuration[1] < 10) display.print("0");
                            display.print(fatDuration[1]);
                            display.print(":");
                            if(fatDuration[2] < 10) display.print("0");
                            display.print(fatDuration[2]);
                            display.print(":");
                            if(fatDuration[3] < 10) display.print("0");
                            display.print(fatDuration[3]);
                            
                            if(fatActive) {
                                unsigned long remainingTime = (fatEndTime - millis()) / 1000;
                                unsigned long hours = remainingTime / 3600;
                                unsigned long minutes = (remainingTime % 3600) / 60;
                                display.print(" (");
                                display.print(hours);
                                display.print("h");
                                display.print(minutes);
                                display.print("m)");
                            }
                        }
                        display.println();
                    }
                    else if(i == 8) {  // NEW: FAT Enable/Disable
                        bool fatEnabled = isSensor1 ? co2Sensors[0].FATEnable : co2Sensors[1].FATEnable;
                        display.print("FAT: [");
                        if(fatEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("On");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(!fatEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("Off");
                        display.setTextColor(WHITE);
                        display.println("]");
                    }
                    else {
                        // Display setpoints (Fruit-Max, Fruit-Min, Pin-Max, Pin-Min)
                        display.print(co2Items[i]);
                        display.print(": ");
                        
                        if(editingValue == i) {
                            display.print(tempValues[i]);
                        } else {
                            // Display correct values based on item index
                            if(isSensor1) {
                                // Sensor 1
                                if(i == 0) display.print(values[VAL_CO2_S1][0]);
                                else if(i == 1) display.print(values[VAL_CO2_S1][1]);
                                else if(i == 2) display.print(co2Sensors[0].pinMax);
                                else if(i == 3) display.print(co2Sensors[0].pinMin);
                            } else {
                                // Sensor 2
                                if(i == 0) display.print(values[VAL_CO2_S2][0]);
                                else if(i == 1) display.print(values[VAL_CO2_S2][1]);
                                else if(i == 2) display.print(co2Sensors[1].pinMax);
                                else if(i == 3) display.print(co2Sensors[1].pinMin);
                            }
                        }
                        display.println();
                    }
                    
                    display.setTextColor(WHITE);
                }
                
            }
            else if(currentMenu == 4) {  // Data Logging menu
                const char* logItems[] = {"Interval", "Logging"};
                for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                    if(!sdCardPresent && i == 1) {
                        display.setTextColor(WHITE);
                        display.println();
                        display.println("No SD Card");
                        display.println("Insert card to enable");
                        break;
                    }
                    
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 0) {  // Interval
                        display.print("Interval: ");
                        if(editingValue >= 0 && i == subMenu) {
                            if(intervalEditComponent == 0) display.setTextColor(BLACK, WHITE);
                            display.print(logInterval[0]);
                            display.setTextColor(WHITE);
                            display.print("d ");
                            
                            if(intervalEditComponent == 1) display.setTextColor(BLACK, WHITE);
                            if(logInterval[1] < 10) display.print("0");
                            display.print(logInterval[1]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(intervalEditComponent == 2) display.setTextColor(BLACK, WHITE);
                            if(logInterval[2] < 10) display.print("0");
                            display.print(logInterval[2]);
                            display.setTextColor(WHITE);
                            display.print(":");
                            
                            if(intervalEditComponent == 3) display.setTextColor(BLACK, WHITE);
                            if(logInterval[3] < 10) display.print("0");
                            display.print(logInterval[3]);
                            display.setTextColor(WHITE);
                        } else {
                            display.print(logInterval[0]);
                            display.print("d ");
                            if(logInterval[1] < 10) display.print("0");
                            display.print(logInterval[1]);
                            display.print(":");
                            if(logInterval[2] < 10) display.print("0");
                            display.print(logInterval[2]);
                            display.print(":");
                            if(logInterval[3] < 10) display.print("0");
                            display.print(logInterval[3]);
                        }
                        display.println();
                    }
                    else if(i == 1) {  // Logging status
                        display.print("Logging: [");
                        if(isLogging) display.setTextColor(BLACK, WHITE);
                        display.print("On");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(!isLogging) display.setTextColor(BLACK, WHITE);
                        display.print("Off");
                        display.setTextColor(WHITE);
                        display.println("]");
                    }
                    
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 5) {  // Date/Time menu
                const char* dateItems[] = {"Date", "Time"};
                for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 0) {  // Date
                        display.print("Date: ");
                        if(editingValue == 0) {
                            if(dateEditComponent == 0) display.setTextColor(BLACK, WHITE);
                            if(dateTimeTemp[0] < 10) display.print("0");
                            display.print(dateTimeTemp[0]);
                            display.setTextColor(WHITE);
                            display.print("/");
                            
                            if(dateEditComponent == 1) display.setTextColor(BLACK, WHITE);
                            if(dateTimeTemp[1] < 10) display.print("0");
                            display.print(dateTimeTemp[1]);
                            display.setTextColor(WHITE);
                            display.print("/");
                            
                            if(dateEditComponent == 2) display.setTextColor(BLACK, WHITE);
                            display.print(dateTimeTemp[2]);
                            display.setTextColor(WHITE);
                        } else {
                            RtcDateTime now = rtc.GetDateTime();
                            if(now.Month() < 10) display.print("0");
                            display.print(now.Month());
                            display.print("/");
                            if(now.Day() < 10) display.print("0");
                            display.print(now.Day());
                            display.print("/");
                            display.print(now.Year());
                        }
                        display.println();
                    }
                    else if(i == 1) {  // Time
                        display.print("Time: ");
                        if(editingValue == 1) {
                            if(dateTimeTemp[0] < 10) display.print("0");
                            display.print(dateTimeTemp[0]);
                            display.print(":");
                            if(dateTimeTemp[1] < 10) display.print("0");
                            display.print(dateTimeTemp[1]);
                        } else {
                            RtcDateTime now = rtc.GetDateTime();
                            if(now.Hour() < 10) display.print("0");
                            display.print(now.Hour());
                            display.print(":");
                            if(now.Minute() < 10) display.print("0");
                            display.print(now.Minute());
                        }
                        display.println();
                    }
                    
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 6 || currentMenu == 7) {  // Calibration menus
                bool isSensor1 = (currentMenu == 6);
                const char* calItems[] = {"Temp", "Humidity", "CO2"};
                const int SENSOR_CALIBRATE_ITEMS = 4;  // S1 and S2 only have 4 items (not 5 like Ambient)
                
                for(int i = 0; i < SENSOR_CALIBRATE_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 3) {  // On/Off toggle
                        bool sensorEnabled = isSensor1 ? sensor1Enabled : sensor2Enabled;
                        display.print("Sensor: [");
                        if(sensorEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("On");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(!sensorEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("Off");
                        display.setTextColor(WHITE);
                        display.println("]");
                    } else {
                        // Display calibration value with asterisk if non-zero
                        display.print(calItems[i]);
                        display.print(": ");
                        
                        if(editingValue == i) {
                            // Show temp value being edited
                            if(i == 0) {
                                display.print(tempValues[i] / 10.0, 1);
                            } else {
                                display.print(tempValues[i]);
                            }
                        } else {
                            // Show current calibration value
                            int16_t calVal;
                            if(isSensor1) {
                                if(i == 0) calVal = calS1Temp;
                                else if(i == 1) calVal = calS1Humidity;
                                else calVal = calS1CO2;
                            } else {
                                if(i == 0) calVal = calS2Temp;
                                else if(i == 1) calVal = calS2Humidity;
                                else calVal = calS2CO2;
                            }
                            
                            if(i == 0) {
                                display.print(calVal / 10.0, 1);
                            } else {
                                display.print(calVal);
                            }
                            
                            // Add asterisk if value is non-zero
                            if(calVal != 0) {
                                display.print("*");
                            }
                        }
                        display.println();
                    }
                    
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 8) {  // NEW: Ambient-Calibrate menu
                const char* calibrateItems[] = {"Temperature", "Humidity", "CO2", "CO2 Offset", "On/Off"};
                for(int i = 0; i < CALIBRATE_SUB_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 4) {  // On/Off toggle (moved to index 4)
                        display.print("On/Off: [");
                        if(!ambientEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("OFF");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(ambientEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("ON");
                        display.setTextColor(WHITE);
                        display.println("]");
                    } else {
                        display.print(calibrateItems[i]);
                        display.print(": ");
                        
                        int16_t value;
                        if(editingValue == i) {
                            value = tempValues[i];
                        } else {
                            switch(i) {
                                case 0: value = calAmbientTemp; break;
                                case 1: value = calAmbientHumidity; break;
                                case 2: value = calAmbientCO2; break;
                                case 3: value = ambientCO2Offset; break;  // NEW: CO2 Offset
                            }
                        }
                        
                        if(i < 2) {  // Temperature or Humidity (tenths)
                            float displayValue = value / 10.0;
                            if(displayValue >= 0) display.print("+");
                            display.print(displayValue, 1);
                            display.println(i == 0 ? " deg" : " %");
                        } else {  // CO2 and CO2 Offset (whole numbers)
                            if(value >= 0) display.print("+");
                            display.print(value);
                            if(i == 3) display.println(" ppm");  // Add unit for CO2 Offset
                            else display.println();
                        }
                    }
                    display.setTextColor(WHITE);
                }
            }
            else if(currentMenu == 9) {  // NEW: WiFi Config menu
                const char* wifiItems[] = {"IP Address", "Port", "Network", "Restart", "Enable"};
                for(int i = 0; i < WIFI_SUB_ITEMS; i++) {
                    if(i == subMenu) {
                        display.setTextColor(BLACK, WHITE);
                    } else {
                        display.setTextColor(WHITE);
                    }
                    
                    if(i == 0) {  // IP Address (read-only)
                        display.print("IP: ");
                        display.println(wifiIPAddress);
                    } else if(i == 1) {  // Port (read-only)
                        display.print("Port: ");
                        display.println(wifiPort);
                    } else if(i == 2) {  // Network (read-only)
                        display.print("Net: ");
                        if(strlen(wifiSSID) > 0 && wifiState >= WIFI_STATE_CONNECTED) {
                            // Truncate SSID if too long for display
                            if(strlen(wifiSSID) > 10) {
                                char truncSSID[11];
                                strncpy(truncSSID, wifiSSID, 9);
                                truncSSID[9] = '.';
                                truncSSID[10] = '\0';
                                display.println(truncSSID);
                            } else {
                                display.println(wifiSSID);
                            }
                        } else {
                            display.println("Not connected");
                        }
                    } else if(i == 3) {  // Restart WiFi
                        display.println("Restart WiFi");
                    } else if(i == 4) {  // Enable/Disable toggle
                        display.print("WiFi: [");
                        if(!wifiEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("OFF");
                        display.setTextColor(WHITE);
                        display.print("] [");
                        if(wifiEnabled) display.setTextColor(BLACK, WHITE);
                        display.print("ON");
                        display.setTextColor(WHITE);
                        display.println("]");
                    }
                    display.setTextColor(WHITE);
                }
            }
        } else {
            // No submenu selected, show submenu options
            if(currentMenu == 6 || currentMenu == 7 || currentMenu == 8) {
                display.println("Temp");
                display.println("Humidity");
                display.println("CO2");
                if(currentMenu == 8) {
                    display.println("CO2 Offset");
                }
                display.println("On/Off");
            }
            else if(currentMenu == 9) {  // WiFi Config submenu options
                display.println("IP Address");
                display.println("Port");
                display.println("Network");
                display.println("Restart");
                display.println("Enable");
            }
            else if(currentMenu == 5) {
                display.println("Date");
                display.println("Time");
            }
            else if(currentMenu == 4) {
                display.println("Interval");
                display.println("Logging");
            }
            else if(currentMenu == 2 || currentMenu == 3) {
                display.println("Fruit-Max");
                display.println("Fruit-Min");
                display.println("Pin-Max");
                display.println("Pin-Min");
                display.println("Delay");
                display.println("Mode");
                display.println("Interval");  // NEW
                display.println("Duration");  // NEW
                display.println("FAT");       // NEW
            }
            else if(currentMenu == 1) {
                display.println("Units");
            }
            else if(currentMenu == 0) {
                display.println("S1-Max");
                display.println("S1-Min");
                display.println("S2-Max");
                display.println("S2-Min");
            }
        }
    }
    else {
        // Main menu with scrolling (when subMenu == -1)
        display.println("Main Menu - v17.4");
        display.println();
        
        // Calculate which items to show based on scroll offset
        int startItem = mainMenuScrollOffset;
        int endItem = min(startItem + MENU_DISPLAY_LINES, NUM_MENU_ITEMS);
        
        for(int i = startItem; i < endItem; i++) {
            if(i == currentMenu) {
                display.setTextColor(BLACK, WHITE);
            } else {
                display.setTextColor(WHITE);
            }
            display.println(getMenuItem(i));
            display.setTextColor(WHITE);
        }
        
        display.println("\nLeft: Return to splash");
    }
    
    display.display();
}

// Helper: advance subMenu up or down by joystick Y, wrapping at totalItems
static void scrollSubMenu(int y, int totalItems) {
    if(y < 200) {
        subMenu = (subMenu - 1 + totalItems) % totalItems;
    } else if(y > 800) {
        subMenu = (subMenu + 1) % totalItems;
    }
}

void handleInput() {
    int x = analogRead(JOY_X);
    int y = analogRead(JOY_Y);
    bool button = digitalRead(JOY_BTN) == LOW;
    
    if(button) delay(200);
    
    if(showSplash) {
        if(button) {
            showSplash = false;
        }
        return;
    }
    
    if(savePrompt) {
        if(x < 200) {
            // Left = Cancel
            // Serial.println("Save cancelled");
            savePrompt = false;
            editingValue = -1;
        }
        else if(x > 800) {
            // Right = Save the values
            if(currentMenu == 0) {  // Humidity menu
                values[VAL_HUM][0] = tempValues[0];
                values[VAL_HUM][1] = tempValues[1];
                humidityS2Max = tempValues[2];
                humidityS2Min = tempValues[3];
                
                EEPROM.put(HUMIDITY_S1_MAX_ADDR, (uint16_t)tempValues[0]);
                EEPROM.put(HUMIDITY_S1_MIN_ADDR, (uint16_t)tempValues[1]);
                EEPROM.put(HUMIDITY_S2_MAX_ADDR, (uint16_t)tempValues[2]);
                EEPROM.put(HUMIDITY_S2_MIN_ADDR, (uint16_t)tempValues[3]);
                
                // Serial.println("Humidity values saved");
            }
            else if(currentMenu == 2) {  // CO2-Sensor1 menu
                if(subMenu < 4) {
                    values[VAL_CO2_S1][0] = tempValues[0];
                    values[VAL_CO2_S1][1] = tempValues[1];
                    co2Sensors[0].pinMax = tempValues[2];
                    co2Sensors[0].pinMin = tempValues[3];

                    EEPROM.put(CO2_S1_FRUIT_MAX_ADDR, (uint16_t)tempValues[0]);
                    EEPROM.put(CO2_S1_FRUIT_MIN_ADDR, (uint16_t)tempValues[1]);
                    EEPROM.put(CO2_S1_PIN_MAX_ADDR, (uint16_t)tempValues[2]);
                    EEPROM.put(CO2_S1_PIN_MIN_ADDR, (uint16_t)tempValues[3]);
                } else if(subMenu == 4) {
                    saveCO2DelaySettings(0);
                } else if(subMenu == 6) {  // NEW: Save Interval
                    saveCO2FATSettings(0);
                } else if(subMenu == 7) {  // NEW: Save Duration
                    saveCO2FATSettings(0);
                }
            }
            else if(currentMenu == 3) {  // CO2-Sensor2 menu
                if(subMenu < 4) {
                    values[VAL_CO2_S2][0] = tempValues[0];
                    values[VAL_CO2_S2][1] = tempValues[1];
                    co2Sensors[1].pinMax = tempValues[2];
                    co2Sensors[1].pinMin = tempValues[3];

                    EEPROM.put(CO2_S2_FRUIT_MAX_ADDR, (uint16_t)tempValues[0]);
                    EEPROM.put(CO2_S2_FRUIT_MIN_ADDR, (uint16_t)tempValues[1]);
                    EEPROM.put(CO2_S2_PIN_MAX_ADDR, (uint16_t)tempValues[2]);
                    EEPROM.put(CO2_S2_PIN_MIN_ADDR, (uint16_t)tempValues[3]);
                } else if(subMenu == 4) {
                    saveCO2DelaySettings(1);
                } else if(subMenu == 6) {  // NEW: Save Interval
                    saveCO2FATSettings(1);
                } else if(subMenu == 7) {  // NEW: Save Duration
                    saveCO2FATSettings(1);
                }
            }
            else if(currentMenu == 4) {  // Data Logging menu
                if(subMenu == 0) {
                    EEPROM.put(LOG_DAYS_ADDR, logInterval[0]);
                    EEPROM.put(LOG_HOURS_ADDR, logInterval[1]);
                    EEPROM.put(LOG_MINUTES_ADDR, logInterval[2]);
                    EEPROM.put(LOG_SECONDS_ADDR, logInterval[3]);
                    
                    // Serial.println("Logging interval saved");
                    // Serial.print(logInterval[0]); Serial.print("d ");
                    // Serial.print(logInterval[1]); Serial.print("h ");
                    // Serial.print(logInterval[2]); Serial.print("m ");
                    // Serial.print(logInterval[3]); Serial.println("s");
                    
                    if(isLogging) {
                        updateNextLogTime();
                    }
                }
                else if(subMenu == 1) {
                    EEPROM.write(LOG_STATUS_ADDR, isLogging ? 1 : 0);
                    
                    if(isLogging) {
                        updateNextLogTime();
                        // Serial.println("Data Logging enabled");
                    } else {
                        // Serial.println("Data Logging disabled");
                    }
                }
            }
            else if(currentMenu == 5) {  // Date/Time menu
                if(subMenu == 0) {
                    RtcDateTime newDate(dateTimeTemp[2], dateTimeTemp[0], dateTimeTemp[1], 
                                       rtc.GetDateTime().Hour(), rtc.GetDateTime().Minute(), 0);
                    rtc.SetDateTime(newDate);
                    // Serial.println("Date saved");
                }
                else if(subMenu == 1) {
                    RtcDateTime now = rtc.GetDateTime();
                    RtcDateTime newTime(now.Year(), now.Month(), now.Day(), 
                                       dateTimeTemp[0], dateTimeTemp[1], 0);
                    rtc.SetDateTime(newTime);
                    // Serial.println("Time saved");
                }
            }
            else if(currentMenu == 6 || currentMenu == 7) {  // Calibration menus
                if(subMenu < 3) {
                    if(currentMenu == 6) {
                        calS1Temp = tempValues[0];
                        calS1Humidity = tempValues[1];
                        calS1CO2 = tempValues[2];
                        
                        EEPROM.put(CAL_S1_TEMP_ADDR, calS1Temp);
                        EEPROM.put(CAL_S1_HUMIDITY_ADDR, calS1Humidity);
                        EEPROM.put(CAL_S1_CO2_ADDR, calS1CO2);
                        
                        // Serial.println("Sensor 1 calibration saved");
                    } else {
                        calS2Temp = tempValues[0];
                        calS2Humidity = tempValues[1];
                        calS2CO2 = tempValues[2];
                        
                        EEPROM.put(CAL_S2_TEMP_ADDR, calS2Temp);
                        EEPROM.put(CAL_S2_HUMIDITY_ADDR, calS2Humidity);
                        EEPROM.put(CAL_S2_CO2_ADDR, calS2CO2);
                        
                        // Serial.println("Sensor 2 calibration saved");
                    }
                } else if(subMenu == 3) {
                    if(currentMenu == 6) {
                        EEPROM.write(SENSOR_S1_ENABLE_ADDR, sensor1Enabled);
                        // Serial.print("Sensor 1 ");
                        // Serial.println(sensor1Enabled ? "Enabled" : "Disabled");
                    } else {
                        EEPROM.write(SENSOR_S2_ENABLE_ADDR, sensor2Enabled);
                        // Serial.print("Sensor 2 ");
                        // Serial.println(sensor2Enabled ? "Enabled" : "Disabled");
                    }
                }
            }
            else if(currentMenu == 8) {  // NEW: Ambient-Calibrate menu
                if(subMenu == 0) {
                    EEPROM.put(CAL_AMBIENT_TEMP_ADDR, (int16_t)tempValues[0]);
                    calAmbientTemp = tempValues[0];
                    // Serial.println("Saved Ambient Temp calibration");
                } else if(subMenu == 1) {
                    EEPROM.put(CAL_AMBIENT_HUMIDITY_ADDR, (int16_t)tempValues[1]);
                    calAmbientHumidity = tempValues[1];
                    // Serial.println("Saved Ambient Humidity calibration");
                } else if(subMenu == 2) {
                    EEPROM.put(CAL_AMBIENT_CO2_ADDR, (int16_t)tempValues[2]);
                    calAmbientCO2 = tempValues[2];
                    // Serial.println("Saved Ambient CO2 calibration");
                } else if(subMenu == 3) {
                    // NEW: Save CO2 Offset
                    EEPROM.put(AMBIENT_CO2_OFFSET_ADDR, (int16_t)tempValues[3]);
                    ambientCO2Offset = tempValues[3];
                    // Serial.print("Saved Ambient CO2 Offset: ");
                    // Serial.println(ambientCO2Offset);
                } else if(subMenu == 4) {
                    // Toggle ambient sensor on/off (moved to index 4)
                    ambientEnabled = !ambientEnabled;
                    EEPROM.write(AMBIENT_ENABLE_ADDR, ambientEnabled);
                    // Serial.print("Ambient Sensor ");
                    // Serial.println(ambientEnabled ? "Enabled" : "Disabled");
                }
            }
            
            savePrompt = false;
            editingValue = -1;
        }
        return;
    }
    
    if(subMenu >= 0) {
        // We're in a submenu - handle editing and navigation
        if(editingValue >= 0) {
        if(currentMenu == 2 || currentMenu == 3) {  // CO2 menus - UPDATED for new items
            bool isSensor1 = (currentMenu == 2);
            
            if(subMenu < 4) {
                if(y < 200) tempValues[editingValue] += 10;
                else if(y > 800) tempValues[editingValue] -= 10;
                
                if(x < 200) tempValues[editingValue] -= 100;
                else if(x > 800) tempValues[editingValue] += 100;
                
                tempValues[editingValue] = constrain(tempValues[editingValue], 0, CO2_MAX_RANGE);
                
                if(editingValue == 0 && tempValues[0] < tempValues[1]) {
                    tempValues[0] = tempValues[1];
                }
                else if(editingValue == 1 && tempValues[1] > tempValues[0]) {
                    tempValues[1] = tempValues[0];
                }
                else if(editingValue == 2 && tempValues[2] < tempValues[3]) {
                    tempValues[2] = tempValues[3];
                }
                else if(editingValue == 3 && tempValues[3] > tempValues[2]) {
                    tempValues[3] = tempValues[2];
                }
            }
            else if(subMenu == 4) {  // Delay editing
                uint16_t* delayInterval = isSensor1 ? co2Sensors[0].delayInterval : co2Sensors[1].delayInterval;
                
                if(y < 200 || y > 800) {
                    if(co2DelayEditComponent == 0) {
                        if(y < 200) {
                            delayInterval[0] = (delayInterval[0] + 1) % 366;
                        } else if(y > 800) {
                            delayInterval[0] = (delayInterval[0] + 365) % 366;
                        }
                    }
                    else if(co2DelayEditComponent == 1) {
                        if(y < 200) {
                            delayInterval[1] = (delayInterval[1] + 1) % 24;
                        } else if(y > 800) {
                            delayInterval[1] = (delayInterval[1] + 23) % 24;
                        }
                    }
                    else if(co2DelayEditComponent == 2) {
                        if(y < 200) {
                            delayInterval[2] = (delayInterval[2] + 1) % 60;
                        } else if(y > 800) {
                            delayInterval[2] = (delayInterval[2] + 59) % 60;
                        }
                    }
                    else if(co2DelayEditComponent == 3) {
                        if(y < 200) {
                            delayInterval[3] = (delayInterval[3] + 1) % 60;
                        } else if(y > 800) {
                            delayInterval[3] = (delayInterval[3] + 59) % 60;
                        }
                    }
                }
                
                if(x < 200) {
                    co2DelayEditComponent = (co2DelayEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    co2DelayEditComponent = (co2DelayEditComponent + 1) % 4;
                }
            }
            else if(subMenu == 5) {  // Mode toggle
                if(x < 200 || x > 800) {
                    if(isSensor1) {
                        co2Sensors[0].mode = !co2Sensors[0].mode;
                        delay(200);
                    } else {
                        co2Sensors[1].mode = !co2Sensors[1].mode;
                        delay(200);
                    }
                }
            }
            else if(subMenu == 6) {  // NEW: Interval editing
                uint16_t* fatInterval = isSensor1 ? co2Sensors[0].FATInterval : co2Sensors[1].FATInterval;
                
                if(y < 200 || y > 800) {
                    if(fatEditComponent == 0) {
                        if(y < 200) {
                            fatInterval[0] = (fatInterval[0] + 1) % 366;
                        } else if(y > 800) {
                            fatInterval[0] = (fatInterval[0] + 365) % 366;
                        }
                    }
                    else if(fatEditComponent == 1) {
                        if(y < 200) {
                            fatInterval[1] = (fatInterval[1] + 1) % 24;
                        } else if(y > 800) {
                            fatInterval[1] = (fatInterval[1] + 23) % 24;
                        }
                    }
                    else if(fatEditComponent == 2) {
                        if(y < 200) {
                            fatInterval[2] = (fatInterval[2] + 1) % 60;
                        } else if(y > 800) {
                            fatInterval[2] = (fatInterval[2] + 59) % 60;
                        }
                    }
                    else if(fatEditComponent == 3) {
                        if(y < 200) {
                            fatInterval[3] = (fatInterval[3] + 1) % 60;
                        } else if(y > 800) {
                            fatInterval[3] = (fatInterval[3] + 59) % 60;
                        }
                    }
                }
                
                if(x < 200) {
                    fatEditComponent = (fatEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    fatEditComponent = (fatEditComponent + 1) % 4;
                }
            }
            else if(subMenu == 7) {  // NEW: Duration editing
                uint16_t* fatDuration = isSensor1 ? co2Sensors[0].FATDuration : co2Sensors[1].FATDuration;
                
                if(y < 200 || y > 800) {
                    if(fatEditComponent == 0) {
                        if(y < 200) {
                            fatDuration[0] = (fatDuration[0] + 1) % 366;
                        } else if(y > 800) {
                            fatDuration[0] = (fatDuration[0] + 365) % 366;
                        }
                    }
                    else if(fatEditComponent == 1) {
                        if(y < 200) {
                            fatDuration[1] = (fatDuration[1] + 1) % 24;
                        } else if(y > 800) {
                            fatDuration[1] = (fatDuration[1] + 23) % 24;
                        }
                    }
                    else if(fatEditComponent == 2) {
                        if(y < 200) {
                            fatDuration[2] = (fatDuration[2] + 1) % 60;
                        } else if(y > 800) {
                            fatDuration[2] = (fatDuration[2] + 59) % 60;
                        }
                    }
                    else if(fatEditComponent == 3) {
                        if(y < 200) {
                            fatDuration[3] = (fatDuration[3] + 1) % 60;
                        } else if(y > 800) {
                            fatDuration[3] = (fatDuration[3] + 59) % 60;
                        }
                    }
                }
                
                if(x < 200) {
                    fatEditComponent = (fatEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    fatEditComponent = (fatEditComponent + 1) % 4;
                }
            }
            else if(subMenu == 8) {  // NEW: FAT toggle
                if(x < 200 || x > 800) {
                    if(isSensor1) {
                        co2Sensors[0].FATEnable = !co2Sensors[0].FATEnable;
                        delay(200);
                    } else {
                        co2Sensors[1].FATEnable = !co2Sensors[1].FATEnable;
                        delay(200);
                    }
                }
            }
        }
        else if(currentMenu == 1) {  // Temperature menu
            if(x < 200 || x > 800) {
                useFahrenheit = !useFahrenheit;
                delay(200);
            }
        }
        else if(currentMenu == 4) {  // Data Logging menu
            if(subMenu == 0) {
                if(y < 200 || y > 800) {
                    if(intervalEditComponent == 0) {
                        if(y < 200) {
                            logInterval[0] = (logInterval[0] + 1) % 366;
                        } else if(y > 800) {
                            logInterval[0] = (logInterval[0] + 365) % 366;
                        }
                    }
                    else if(intervalEditComponent == 1) {
                        if(y < 200) {
                            logInterval[1] = (logInterval[1] + 1) % 24;
                        } else if(y > 800) {
                            logInterval[1] = (logInterval[1] + 23) % 24;
                        }
                    }
                    else if(intervalEditComponent == 2) {
                        if(y < 200) {
                            logInterval[2] = (logInterval[2] + 1) % 60;
                        } else if(y > 800) {
                            logInterval[2] = (logInterval[2] + 59) % 60;
                        }
                    }
                    else if(intervalEditComponent == 3) {
                        if(y < 200) {
                            logInterval[3] = (logInterval[3] + 1) % 60;
                        } else if(y > 800) {
                            logInterval[3] = (logInterval[3] + 59) % 60;
                        }
                    }
                }
                
                if(x < 200) {
                    intervalEditComponent = (intervalEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    intervalEditComponent = (intervalEditComponent + 1) % 4;
                }
            }
            else if(subMenu == 1) {
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                    
                    if(sdCardPresent) {
                        // Serial.println("SD Card detected");
                    } else {
                        // Serial.println("No SD Card detected");
                        if(isLogging) {
                            isLogging = false;
                            EEPROM.write(LOG_STATUS_ADDR, 0);
                            // Serial.println("Data Logging disabled - No SD Card");
                        }
                    }
                }
                
                if(sdCardPresent && editingValue >= 0) {
                    if(x < 200 || x > 800) {
                        isLogging = !isLogging;
                        delay(200);
                    }
                }
            }
        }
        else if(currentMenu == 5) {  // Date/Time menu
            if(subMenu == 0) {
                if(y < 200 || y > 800) {
                    if(dateEditComponent == 0) {
                        if(y < 200) {
                            dateTimeTemp[0] = dateTimeTemp[0] % 12 + 1;
                        } else {
                            dateTimeTemp[0] = (dateTimeTemp[0] + 10) % 12 + 1;
                        }
                    }
                    else if(dateEditComponent == 1) {
                        if(y < 200) {
                            dateTimeTemp[1] = dateTimeTemp[1] % 31 + 1;
                        } else {
                            dateTimeTemp[1] = (dateTimeTemp[1] + 29) % 31 + 1;
                        }
                    }
                    else if(dateEditComponent == 2) {
                        if(y < 200) {
                            dateTimeTemp[2] = constrain(dateTimeTemp[2] + 1, YEAR_MIN, YEAR_MAX);
                        } else {
                            dateTimeTemp[2] = constrain(dateTimeTemp[2] - 1, YEAR_MIN, YEAR_MAX);
                        }
                    }
                }
                
                if(x < 200) {
                    dateEditComponent = (dateEditComponent + 2) % 3;
                }
                else if(x > 800) {
                    dateEditComponent = (dateEditComponent + 1) % 3;
                }
            }
            else if(subMenu == 1) {
                if(y < 200) {
                    dateTimeTemp[0] = (dateTimeTemp[0] + 1) % 24;
                }
                else if(y > 800) {
                    dateTimeTemp[0] = (dateTimeTemp[0] + 23) % 24;
                }
                
                if(x < 200) {
                    dateTimeTemp[1] = (dateTimeTemp[1] + 59) % 60;
                }
                else if(x > 800) {
                    dateTimeTemp[1] = (dateTimeTemp[1] + 1) % 60;
                }
            }
        }
        else if(currentMenu == 0) {  // Humidity menu
            if(y < 200) tempValues[editingValue] += 1;
            else if(y > 800) tempValues[editingValue] -= 1;
            
            if(x < 200) tempValues[editingValue] -= 100;
            else if(x > 800) tempValues[editingValue] += 100;
            
            tempValues[editingValue] = constrain(tempValues[editingValue], 0, 1000);
            
            if(editingValue == 0 && tempValues[0] < tempValues[1]) {
                tempValues[0] = tempValues[1];
            }
            else if(editingValue == 1 && tempValues[1] > tempValues[0]) {
                tempValues[1] = tempValues[0];
            }
            else if(editingValue == 2 && tempValues[2] < tempValues[3]) {
                tempValues[2] = tempValues[3];
            }
            else if(editingValue == 3 && tempValues[3] > tempValues[2]) {
                tempValues[3] = tempValues[2];
            }
        }
        else if(currentMenu == 6 || currentMenu == 7 || currentMenu == 8) {  // Calibration menus (UPDATED to include menu 9)
            if(y < 200) {
                if(subMenu < 2) {
                    tempValues[editingValue] = min(1000, tempValues[editingValue] + 1);
                } else {
                    tempValues[editingValue] = min(1000, tempValues[editingValue] + 1);
                }
            }
            else if(y > 800) {
                if(subMenu < 2) {
                    tempValues[editingValue] = max(-1000, tempValues[editingValue] - 1);
                } else {
                    tempValues[editingValue] = max(-1000, tempValues[editingValue] - 1);
                }
            }
            
            if(x < 200) {
                if(subMenu < 2) {
                    tempValues[editingValue] = max(-1000, tempValues[editingValue] - 10);
                } else {
                    tempValues[editingValue] = max(-1000, tempValues[editingValue] - 100);
                }
            }
            else if(x > 800) {
                if(subMenu < 2) {
                    tempValues[editingValue] = min(1000, tempValues[editingValue] + 10);
                } else {
                    tempValues[editingValue] = min(1000, tempValues[editingValue] + 100);
                }
            }
        }
        
        if(button) {
            if((currentMenu == 1 && editingValue == 0) || 
               (currentMenu == 4 && subMenu == 1) ||
               (currentMenu == 2 && (subMenu == 5 || subMenu == 8)) ||
               (currentMenu == 3 && (subMenu == 5 || subMenu == 8)) ||
               (currentMenu == 6 && subMenu == 3) ||
               (currentMenu == 7 && subMenu == 3) ||
               (currentMenu == 8 && subMenu == 4) ||
               (currentMenu == 9)) {  // WiFi Config menu items don't need save prompt
                editingValue = -1;
            } else {
                editingValue = -1;
                savePrompt = true;
            }
        }
        return;
    }
    
    if(subMenu >= 0) {
        if(x < 200) {
            subMenu = -1;
            dateEditComponent = 0;
            intervalEditComponent = 0;
            co2DelayEditComponent = 0;
            fatEditComponent = 0;  // NEW: Reset FAT interval edit component
            fatEditComponent = 0;  // NEW: Reset FAT duration edit component
            subMenuScrollOffset = 0;  // Reset submenu scroll offset when exiting submenu
        }
        else if(button) {
            if(currentMenu == 4) {
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                }
                
                if(subMenu == 1 && !sdCardPresent) {
                    return;
                }
            }
            
            editingValue = subMenu;
            
            if(currentMenu == 5) {
                RtcDateTime now = rtc.GetDateTime();
                if(subMenu == 0) {
                    dateTimeTemp[0] = now.Month();
                    dateTimeTemp[1] = now.Day();
                    dateTimeTemp[2] = now.Year();
                    dateEditComponent = 0;
                } else {
                    dateTimeTemp[0] = now.Hour();
                    dateTimeTemp[1] = now.Minute();
                    dateTimeTemp[2] = 0;
                }
            }
            else if(currentMenu == 4) {
                if(subMenu == 0) {
                    intervalEditComponent = 0;
                }
            }
            else if(currentMenu == 6 || currentMenu == 7) {
                if(subMenu == 3) {
                    // Toggle sensor on/off and save immediately
                    if(currentMenu == 6) {
                        sensor1Enabled = !sensor1Enabled;
                        EEPROM.write(SENSOR_S1_ENABLE_ADDR, sensor1Enabled ? 1 : 0);
                        // Serial.print("Sensor 1 ");
                        // Serial.println(sensor1Enabled ? "Enabled" : "Disabled");
                    } else {
                        sensor2Enabled = !sensor2Enabled;
                        EEPROM.write(SENSOR_S2_ENABLE_ADDR, sensor2Enabled ? 1 : 0);
                        // Serial.print("Sensor 2 ");
                        // Serial.println(sensor2Enabled ? "Enabled" : "Disabled");
                    }
                } else {
                    // Initialize calibration values for editing
                    if(currentMenu == 6) {
                        tempValues[0] = calS1Temp;
                        tempValues[1] = calS1Humidity;
                        tempValues[2] = calS1CO2;
                    } else {
                        tempValues[0] = calS2Temp;
                        tempValues[1] = calS2Humidity;
                        tempValues[2] = calS2CO2;
                    }
                }
            }
            else if(currentMenu == 8) {  // NEW: Ambient-Calibrate menu
                if(subMenu == 4) {
                    // Toggle ambient sensor on/off and save immediately
                    ambientEnabled = !ambientEnabled;
                    EEPROM.write(AMBIENT_ENABLE_ADDR, ambientEnabled ? 1 : 0);
                    // Serial.print("Ambient Sensor ");
                    // Serial.println(ambientEnabled ? "Enabled" : "Disabled");
                } else {
                    // Initialize ambient calibration values for editing
                    tempValues[0] = calAmbientTemp;
                    tempValues[1] = calAmbientHumidity;
                    tempValues[2] = calAmbientCO2;
                    tempValues[3] = ambientCO2Offset;  // NEW: CO2 Offset
                }
            }
            else if(currentMenu == 9) {  // WiFi Config menu
                if(subMenu == 3) {
                    // Restart WiFi
                    // Serial.println("Restarting WiFi from menu...");
                    wifiState = WIFI_STATE_IDLE;
                    wifiConnected = false;
                    wifiClientConnected = false;
                    wifiReconnectAttempts = 0;
                    strcpy(wifiIPAddress, "0.0.0.0");
                    initWifi();
                    editingValue = -1;  // Don't enter editing mode
                } else if(subMenu == 4) {
                    // Toggle WiFi on/off and save immediately
                    wifiEnabled = !wifiEnabled;
                    saveWifiCredentials();  // Save the new state
                    // Serial.print("WiFi ");
                    // Serial.println(wifiEnabled ? "Enabled" : "Disabled");
                    
                    if(wifiEnabled) {
                        // Start WiFi if it was just enabled
                        wifiReconnectAttempts = 0;
                        initWifi();
                    } else {
                        // Stop WiFi if it was just disabled
                        wifiState = WIFI_STATE_IDLE;
                        wifiConnected = false;
                        wifiClientConnected = false;
                        strcpy(wifiIPAddress, "0.0.0.0");
                    }
                    editingValue = -1;  // Don't enter editing mode
                } else {
                    // IP Address, Port, Network are read-only - do nothing
                    editingValue = -1;
                }
            }
            else if(currentMenu == 2) {
                if(subMenu < 4) {
                    tempValues[0] = values[VAL_CO2_S1][0];
                    tempValues[1] = values[VAL_CO2_S1][1];
                    tempValues[2] = co2Sensors[0].pinMax;
                    tempValues[3] = co2Sensors[0].pinMin;

                    if(tempValues[0] < tempValues[1]) {
                        tempValues[0] = tempValues[1];
                    }
                    if(tempValues[2] < tempValues[3]) {
                        tempValues[2] = tempValues[3];
                    }
                }
                else if(subMenu == 4) {
                    co2DelayEditComponent = 0;
                }
                else if(subMenu == 6) {  // NEW: Interval
                    fatEditComponent = 0;
                }
                else if(subMenu == 7) {  // NEW: Duration
                    fatEditComponent = 0;
                }
            }
            else if(currentMenu == 3) {
                if(subMenu < 4) {
                    tempValues[0] = values[VAL_CO2_S2][0];
                    tempValues[1] = values[VAL_CO2_S2][1];
                    tempValues[2] = co2Sensors[1].pinMax;
                    tempValues[3] = co2Sensors[1].pinMin;

                    if(tempValues[0] < tempValues[1]) {
                        tempValues[0] = tempValues[1];
                    }
                    if(tempValues[2] < tempValues[3]) {
                        tempValues[2] = tempValues[3];
                    }
                }
                else if(subMenu == 4) {
                    co2DelayEditComponent = 0;
                }
                else if(subMenu == 6) {  // NEW: Interval
                    fatEditComponent = 0;
                }
                else if(subMenu == 7) {  // NEW: Duration
                    fatEditComponent = 0;
                }
            }
            else if(currentMenu == 0) {
                tempValues[0] = values[VAL_HUM][0];
                tempValues[1] = values[VAL_HUM][1];
                tempValues[2] = humidityS2Max;
                tempValues[3] = humidityS2Min;
                
                if(tempValues[0] < tempValues[1]) {
                    tempValues[0] = tempValues[1];
                }
                if(tempValues[2] < tempValues[3]) {
                    tempValues[2] = tempValues[3];
                }
            }
        }
        else if(y < 200 || y > 800) {
            if(currentMenu == 4) {
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                }
                if(!sdCardPresent) {
                    subMenu = 0;
                } else {
                    scrollSubMenu(y, NUM_SUB_ITEMS);
                }
            } else if(currentMenu == 1) {
                scrollSubMenu(y, TEMP_SUB_ITEMS);
            } else if(currentMenu == 2 || currentMenu == 3) {
                scrollSubMenu(y, CO2_SUB_ITEMS);
                // Update scroll offset to keep selected item visible
                if(subMenu < subMenuScrollOffset) {
                    subMenuScrollOffset = subMenu;
                } else if(subMenu >= subMenuScrollOffset + SUBMENU_DISPLAY_LINES) {
                    subMenuScrollOffset = subMenu - SUBMENU_DISPLAY_LINES + 1;
                }
                subMenuScrollOffset = constrain(subMenuScrollOffset, 0, max(0, CO2_SUB_ITEMS - SUBMENU_DISPLAY_LINES));
            } else if(currentMenu == 0) {
                scrollSubMenu(y, HUMIDITY_SUB_ITEMS);
            } else if(currentMenu == 6 || currentMenu == 7) {  // Calibrate-S1 and Calibrate-S2 (4 items)
                const int SENSOR_CALIBRATE_ITEMS = 4;
                scrollSubMenu(y, SENSOR_CALIBRATE_ITEMS);
            } else if(currentMenu == 8) {  // Ambient-Calibrate (5 items)
                scrollSubMenu(y, CALIBRATE_SUB_ITEMS);
            } else if(currentMenu == 9) {  // WiFi Config (5 items)
                scrollSubMenu(y, WIFI_SUB_ITEMS);
            } else {
                scrollSubMenu(y, NUM_SUB_ITEMS);
            }
        }
    }
    }
    else {
        // Main menu navigation with scrolling (subMenu == -1)
        if(x < 200) showSplash = true;
        
        if(y < 200) {
            currentMenu--;
            if(currentMenu < 0) currentMenu = NUM_MENU_ITEMS - 1;
            
            if(currentMenu < mainMenuScrollOffset) {
                mainMenuScrollOffset = currentMenu;
            }
            else if(currentMenu >= mainMenuScrollOffset + MENU_DISPLAY_LINES) {
                mainMenuScrollOffset = currentMenu - MENU_DISPLAY_LINES + 1;
            }
        }
        else if(y > 800) {
            currentMenu++;
            if(currentMenu >= NUM_MENU_ITEMS) currentMenu = 0;
            
            if(currentMenu < mainMenuScrollOffset) {
                mainMenuScrollOffset = currentMenu;
            }
            else if(currentMenu >= mainMenuScrollOffset + MENU_DISPLAY_LINES) {
                mainMenuScrollOffset = currentMenu - MENU_DISPLAY_LINES + 1;
            }
        }
        
        if(button) {
            subMenu = 0;
            subMenuScrollOffset = 0;  // Reset submenu scroll offset when entering any submenu
        }
        
        mainMenuScrollOffset = constrain(mainMenuScrollOffset, 0, max(0, NUM_MENU_ITEMS - MENU_DISPLAY_LINES));
        currentMenu = constrain(currentMenu, 0, NUM_MENU_ITEMS - 1);
    }
}

void updateNextLogTime() {
  if(isLogging) {
    unsigned long intervalMs = (logInterval[0] * 24L * 60L * 60L * 1000L) +
                             (logInterval[1] * 60L * 60L * 1000L) +
                             (logInterval[2] * 60L * 1000L) +
                             (logInterval[3] * 1000L);
    
    if(intervalMs == 0) intervalMs = 1000;
    
    nextLogTime = millis() + intervalMs;
  }
}

void logDataToSD() {
  if(!isLogging) return;
  
  if (!sdCardPresent) {
    sdCardPresent = sd.begin(SD_CS_PIN);
    if (!sdCardPresent) {
      isLogging = false;
      EEPROM.write(LOG_STATUS_ADDR, 0);
      // Serial.println("Logging stopped - SD card not found");
      return;
    }
  }
  
  RtcDateTime now = rtc.GetDateTime();
  
  if (logFile.open("MFC-LOG.TXT", O_WRONLY | O_APPEND)) {
    // Date
    logFile.print(now.Month());
    logFile.print("/");
    logFile.print(now.Day());
    logFile.print("/");
    logFile.print(now.Year());
    logFile.print(",");
    
    // Time
    logFile.print(now.Hour());
    logFile.print(":");
    if (now.Minute() < 10) logFile.print("0");
    logFile.print(now.Minute());
    logFile.print(",");
    
    // Sensor 1 Enabled Status
    logFile.print(sensor1Enabled ? "On" : "Off");
    logFile.print(",");
    
    // Humidity S1 Max
    logFile.print(values[VAL_HUM][0] / 10.0, 1);
    logFile.print(",");
    
    // Humidity S1 Min
    logFile.print(values[VAL_HUM][1] / 10.0, 1);
    logFile.print(",");
    
    // Humidity S1 Current
    if (sensor1Enabled) {
      logFile.print(currentHumidity1, 1);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // Sensor 2 Enabled Status
    logFile.print(sensor2Enabled ? "On" : "Off");
    logFile.print(",");
    
    // Humidity S2 Max
    logFile.print(humidityS2Max / 10.0, 1);
    logFile.print(",");
    
    // Humidity S2 Min
    logFile.print(humidityS2Min / 10.0, 1);
    logFile.print(",");
    
    // Humidity S2 Current
    if (sensor2Enabled) {
      logFile.print(currentHumidity2, 1);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // Humidity S1 Relay Status
    logFile.print(digitalRead(HUMIDITY_S1_RELAY_PIN) == LOW ? "On" : "Off");
    logFile.print(",");
    
    // CO2 Sensor 1 Fruit Max
    logFile.print(values[VAL_CO2_S1][0]);
    logFile.print(",");
    
    // CO2 Sensor 1 Fruit Min
    logFile.print(values[VAL_CO2_S1][1]);
    logFile.print(",");
    
    // CO2 Sensor 1 Pin Max
    logFile.print(co2Sensors[0].pinMax);
    logFile.print(",");

    // CO2 Sensor 1 Pin Min
    logFile.print(co2Sensors[0].pinMin);
    logFile.print(",");

    // CO2 Sensor 1 Mode
    logFile.print(co2Sensors[0].mode ? "Fruit" : "Pin");
    logFile.print(",");
    
    // CO2 Sensor 1 Current
    if (sensor1Enabled) {
      logFile.print((uint16_t)currentCO2_1);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // CO2 Sensor 1 Relay Status
    logFile.print(digitalRead(CO2_RELAY_PIN) == LOW ? "On" : "Off");
    logFile.print(",");
    
    // CO2 Sensor 1 Delay Remaining (seconds)
    if (co2Sensors[0].delayActive) {
      unsigned long remaining = (co2Sensors[0].delayEndTime > millis()) ? (co2Sensors[0].delayEndTime - millis()) : 0;
      logFile.print(remaining / 1000);
    } else {
      logFile.print("0");
    }
    logFile.print(",");

    // CO2 Sensor 1 FAT Active
    logFile.print(co2Sensors[0].FATActive ? "Yes" : "No");
    logFile.print(",");
    
    // CO2 Sensor 2 Fruit Max
    logFile.print(values[VAL_CO2_S2][0]);
    logFile.print(",");
    
    // CO2 Sensor 2 Fruit Min
    logFile.print(values[VAL_CO2_S2][1]);
    logFile.print(",");
    
    // CO2 Sensor 2 Pin Max
    logFile.print(co2Sensors[1].pinMax);
    logFile.print(",");

    // CO2 Sensor 2 Pin Min
    logFile.print(co2Sensors[1].pinMin);
    logFile.print(",");

    // CO2 Sensor 2 Mode
    logFile.print(co2Sensors[1].mode ? "Fruit" : "Pin");
    logFile.print(",");
    
    // CO2 Sensor 2 Current
    if (sensor2Enabled) {
      logFile.print((uint16_t)currentCO2_2);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // CO2 Sensor 2 Relay Status
    logFile.print(digitalRead(CO2_S2_RELAY_PIN) == LOW ? "On" : "Off");
    logFile.print(",");
    
    // CO2 Sensor 2 Delay Remaining (seconds)
    if (co2Sensors[1].delayActive) {
      unsigned long remaining = (co2Sensors[1].delayEndTime > millis()) ? (co2Sensors[1].delayEndTime - millis()) : 0;
      logFile.print(remaining / 1000);
    } else {
      logFile.print("0");
    }
    logFile.print(",");

    // CO2 Sensor 2 FAT Active
    logFile.print(co2Sensors[1].FATActive ? "Yes" : "No");
    logFile.print(",");
    
    // NEW: Ambient CO2 (Conditional)
    if(ambientEnabled && ambientCO2 > 0) {
      logFile.print((uint16_t)ambientCO2);
    } else {
      logFile.print("--");
    }
    logFile.print(",");
    
    // Temperature Sensor 1
    if (sensor1Enabled) {
      float displayTemp1 = useFahrenheit ? celsiusToFahrenheit(currentTemperature1) : currentTemperature1;
      logFile.print(displayTemp1, 1);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // Temperature Sensor 2
    if (sensor2Enabled) {
      float displayTemp2 = useFahrenheit ? celsiusToFahrenheit(currentTemperature2) : currentTemperature2;
      logFile.print(displayTemp2, 1);
    } else {
      logFile.print("OFF");
    }
    logFile.print(",");
    
    // NEW: Ambient Temperature (Conditional)
    if(ambientEnabled && ambientTemperature > -40) {
      float displayTempAmbient = useFahrenheit ? celsiusToFahrenheit(ambientTemperature) : ambientTemperature;
      logFile.print(displayTempAmbient, 1);
    } else {
      logFile.print("--");
    }
    logFile.print(",");
    
    // Temperature Unit
    logFile.println(useFahrenheit ? "F" : "C");

    logFile.close();
    
    lastLogTime = millis();
    
    unsigned long logIntervalMs = (logInterval[0] * 24L * 60L * 60L * 1000L) +
                                 (logInterval[1] * 60L * 60L * 1000L) +
                                 (logInterval[2] * 60L * 1000L) +
                                 (logInterval[3] * 1000L);
    
    if (logIntervalMs == 0) logIntervalMs = 60000;
    
    nextLogTime = millis() + logIntervalMs;
    
    // Serial.println("Data logged successfully");
  } else {
    // Serial.println("Failed to open log file");
    
    sdCardPresent = sd.begin(SD_CS_PIN);
    
    if(!sdCardPresent) {
      isLogging = false;
      EEPROM.write(LOG_STATUS_ADDR, 0);
      // Serial.println("Data Logging disabled - SD Card removed");
    } else {
      nextLogTime = millis() + 5000;
    }
  }
}

void saveCO2DelaySettings(uint8_t idx) {
    SensorCO2State& s = co2Sensors[idx];
    if (idx == 0) {
        EEPROM.put(CO2_S1_DELAY_DAYS_ADDR,    s.delayInterval[0]);
        EEPROM.put(CO2_S1_DELAY_HOURS_ADDR,   s.delayInterval[1]);
        EEPROM.put(CO2_S1_DELAY_MINUTES_ADDR, s.delayInterval[2]);
        EEPROM.put(CO2_S1_DELAY_SECONDS_ADDR, s.delayInterval[3]);
    } else {
        EEPROM.put(CO2_S2_DELAY_DAYS_ADDR,    s.delayInterval[0]);
        EEPROM.put(CO2_S2_DELAY_HOURS_ADDR,   s.delayInterval[1]);
        EEPROM.put(CO2_S2_DELAY_MINUTES_ADDR, s.delayInterval[2]);
        EEPROM.put(CO2_S2_DELAY_SECONDS_ADDR, s.delayInterval[3]);
    }
    if (s.delayActive && !s.mode) {
        unsigned long delayTimeMs = (s.delayInterval[0] * 24L * 60L * 60L * 1000L) +
                                    (s.delayInterval[1] * 60L * 60L * 1000L) +
                                    (s.delayInterval[2] * 60L * 1000L) +
                                    (s.delayInterval[3] * 1000L);
        s.delayEndTime = millis() + delayTimeMs;
    }
}

// Unified FAT settings save (replaces saveCO2S1FATSettings / saveCO2S2FATSettings)
void saveCO2FATSettings(uint8_t idx) {
    SensorCO2State& s = co2Sensors[idx];
    if (idx == 0) {
        EEPROM.put(CO2_S1_FAT_INTERVAL_DAYS_ADDR,    s.FATInterval[0]);
        EEPROM.put(CO2_S1_FAT_INTERVAL_HOURS_ADDR,   s.FATInterval[1]);
        EEPROM.put(CO2_S1_FAT_INTERVAL_MINUTES_ADDR, s.FATInterval[2]);
        EEPROM.put(CO2_S1_FAT_INTERVAL_SECONDS_ADDR, s.FATInterval[3]);
        EEPROM.put(CO2_S1_FAT_DURATION_DAYS_ADDR,    s.FATDuration[0]);
        EEPROM.put(CO2_S1_FAT_DURATION_HOURS_ADDR,   s.FATDuration[1]);
        EEPROM.put(CO2_S1_FAT_DURATION_MINUTES_ADDR, s.FATDuration[2]);
        EEPROM.put(CO2_S1_FAT_DURATION_SECONDS_ADDR, s.FATDuration[3]);
        EEPROM.write(CO2_S1_FAT_ENABLE_ADDR, s.FATEnable ? 1 : 0);
    } else {
        EEPROM.put(CO2_S2_FAT_INTERVAL_DAYS_ADDR,    s.FATInterval[0]);
        EEPROM.put(CO2_S2_FAT_INTERVAL_HOURS_ADDR,   s.FATInterval[1]);
        EEPROM.put(CO2_S2_FAT_INTERVAL_MINUTES_ADDR, s.FATInterval[2]);
        EEPROM.put(CO2_S2_FAT_INTERVAL_SECONDS_ADDR, s.FATInterval[3]);
        EEPROM.put(CO2_S2_FAT_DURATION_DAYS_ADDR,    s.FATDuration[0]);
        EEPROM.put(CO2_S2_FAT_DURATION_HOURS_ADDR,   s.FATDuration[1]);
        EEPROM.put(CO2_S2_FAT_DURATION_MINUTES_ADDR, s.FATDuration[2]);
        EEPROM.put(CO2_S2_FAT_DURATION_SECONDS_ADDR, s.FATDuration[3]);
        EEPROM.write(CO2_S2_FAT_ENABLE_ADDR, s.FATEnable ? 1 : 0);
    }
}

void debugCalibrationValues() {
    // Serial.println("=== Calibration Values Debug ===");
    // Serial.print("calS1Temp: "); Serial.println(calS1Temp);
    // Serial.print("calS1Humidity: "); Serial.println(calS1Humidity);
    // Serial.print("calS1CO2: "); Serial.println(calS1CO2);
    // Serial.print("calS2Temp: "); Serial.println(calS2Temp);
    // Serial.print("calS2Humidity: "); Serial.println(calS2Humidity);
    // Serial.print("calS2CO2: "); Serial.println(calS2CO2);
    
    // Serial.print("S1 Temp asterisk: "); Serial.println(calS1Temp != 0 ? "YES" : "NO");
    // Serial.print("S1 Humidity asterisk: "); Serial.println(calS1Humidity != 0 ? "YES" : "NO");
    // Serial.print("S1 CO2 asterisk: "); Serial.println(calS1CO2 != 0 ? "YES" : "NO");
    // Serial.print("S2 Temp asterisk: "); Serial.println(calS2Temp != 0 ? "YES" : "NO");
    // Serial.print("S2 Humidity asterisk: "); Serial.println(calS2Humidity != 0 ? "YES" : "NO");
    // Serial.print("S2 CO2 asterisk: "); Serial.println(calS2CO2 != 0 ? "YES" : "NO");
    // Serial.println("================================");
}

// ==================== BLUETOOTH FUNCTIONS ====================

void initBluetooth() {
  BT_SERIAL.begin(BT_BAUD_RATE);
  // Serial.print("Bluetooth initialized at ");
  // Serial.print(BT_BAUD_RATE);
  // Serial.println(" baud on Serial1 (TX=18, RX=19)");
  
  // Send ready message
  delay(100);
  BT_SERIAL.println("SYS:READY");
  
  // Serial.println("Bluetooth command protocol active");
  // Serial.println("Available commands: GET:DATA, GET:STATUS, SET:..., PING");
}

void handleBluetoothCommands() {
  // Check for incoming Bluetooth data
  while (BT_SERIAL.available() > 0) {
    char c = BT_SERIAL.read();
    
    // Mark activity
    lastBtActivityTime = millis();
    if (!btConnected) {
      btConnected = true;
      // Serial.println("Bluetooth device connected");
      BT_SERIAL.println("SYS:CONNECTED");
    }
    
    // Handle newline/carriage return as command terminator
    if (c == '\n' || c == '\r') {
      if (btBufferIndex > 0) {
        btBuffer[btBufferIndex] = '\0';  // Null terminate
        lastCommandChannel = CHANNEL_BLUETOOTH;  // Set response channel
        processBluetoothCommand(btBuffer);
        btBufferIndex = 0;  // Reset buffer
      }
    } 
    // Add character to buffer if there's space
    else if (btBufferIndex < BT_BUFFER_SIZE - 1) {
      btBuffer[btBufferIndex++] = c;
    }
    // Buffer overflow - reset
    else {
      // Serial.println("BT buffer overflow - resetting");
      btBufferIndex = 0;
      sendError("ERR:BUFFER_OVERFLOW");
    }
  }
  
  // Check for Bluetooth timeout
  if (btConnected && (millis() - lastBtActivityTime > BT_TIMEOUT)) {
    btConnected = false;
    // Serial.println("Bluetooth device disconnected (timeout)");
  }
  
  // Auto-send sensor data periodically if connected (via BT or WiFi)
  bool btShouldSend = btConnected;
  bool wifiShouldSend = wifiClientConnected && wifiState == WIFI_STATE_CLIENT_CONNECTED && !wifiDataPaused;
  
  if ((btShouldSend || wifiShouldSend) && (millis() - lastBtSendTime >= BT_SEND_INTERVAL)) {
    // Send to Bluetooth if connected
    if (btShouldSend) {
      lastCommandChannel = CHANNEL_BLUETOOTH;
      sendSensorData();
    }
    // Send to WiFi if connected AND not paused
    if (wifiShouldSend) {
      #if DEBUG_WIFI_TX
      Serial.print("WiFi send: state=");
      Serial.print(wifiState);
      Serial.print(" client=");
      Serial.println(wifiClientConnected);
      #endif
      lastCommandChannel = CHANNEL_WIFI;
      sendSensorData();
    }
    lastBtSendTime = millis();
  }
  
  // Debug: Print WiFi status every 10 seconds
  static unsigned long lastWifiStatusPrint = 0;
  if (millis() - lastWifiStatusPrint > 10000) {
    lastWifiStatusPrint = millis();
    #if DEBUG_WIFI_STATE
    Serial.print("WiFi status: enabled=");
    Serial.print(wifiEnabled);
    Serial.print(" state=");
    Serial.print(wifiState);
    Serial.print(" client=");
    Serial.print(wifiClientConnected);
    Serial.print(" bufIdx=");
    Serial.print(wifiBufferIndex);
    Serial.print(" paused=");
    Serial.print(wifiDataPaused);
    if (wifiBufferIndex > 0 && wifiBufferIndex < 20) {
      Serial.print(" buf=[");
      for (int i = 0; i < wifiBufferIndex; i++) {
        if (wifiBuffer[i] >= 32 && wifiBuffer[i] < 127) {
          Serial.print(wifiBuffer[i]);
        } else {
          Serial.print("\\x");
          if (wifiBuffer[i] < 16) Serial.print("0");
          Serial.print((int)wifiBuffer[i], HEX);
        }
      }
      Serial.print("]");
    }
    Serial.println();
    #endif
  }
}

// Forward declarations for command handler groups (Step 4)
bool handleGetDataCommands(const char* cmd, char* rawCmd);
bool handleSetSensorCommands(const char* cmd, char* rawCmd);
bool handleWiFiCommands(const char* cmd, char* rawCmd);
bool handleCalibrationCommands(const char* cmd, char* rawCmd);

// Dispatcher (Step 4) - routes to handler groups, each returns true if handled
void processBluetoothCommand(char* command) {
  char upperCommand[BT_BUFFER_SIZE];
  strncpy(upperCommand, command, BT_BUFFER_SIZE - 1);
  upperCommand[BT_BUFFER_SIZE - 1] = '\0';
  for (uint8_t i = 0; upperCommand[i]; i++) {
    upperCommand[i] = toupper(upperCommand[i]);
  }

  if (!handleGetDataCommands(upperCommand, command) &&
      !handleSetSensorCommands(upperCommand, command) &&
      !handleWiFiCommands(upperCommand, command) &&
      !handleCalibrationCommands(upperCommand, command)) {
    sendError("ERR:UNKNOWN_COMMAND");
  }
}

// ===== GET DATA commands handler =====
bool handleGetDataCommands(const char* upperCommand, char* command) {
  // GET:DATA - Send all sensor data
  if (strcmp(upperCommand, "GET:DATA") == 0) {
    sendSensorData();
    return true;
  }
  
  // GET:STATUS - Send system status
  else if (strcmp(upperCommand, "GET:STATUS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "STATUS:S1=%s,S2=%s,AMB=%s,LOG=%s,SD=%s",
      sensor1Enabled ? "ON" : "OFF",
      sensor2Enabled ? "ON" : "OFF",
      ambientEnabled ? "ON" : "OFF",
      isLogging ? "ON" : "OFF",
      sdCardPresent ? "OK" : "NONE"
    );
    sendResponse(responseBuffer);
    return true;
  }

  // GET:RELAYS - Send relay states
  else if (strcmp(upperCommand, "GET:RELAYS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "RELAYS:HUM1=%s,CO2_S1=%s,CO2_S2=%s,HUM2=%s",
      digitalRead(HUMIDITY_S1_RELAY_PIN) == LOW ? "ON" : "OFF",
      digitalRead(CO2_RELAY_PIN) == LOW ? "ON" : "OFF",
      digitalRead(CO2_S2_RELAY_PIN) == LOW ? "ON" : "OFF",
      digitalRead(HUMIDITY_S2_RELAY_PIN) == LOW ? "ON" : "OFF"
    );
    sendResponse(responseBuffer);
    return true;
  }

  // GET:DATETIME - Send current date/time
  else if (strcmp(upperCommand, "GET:DATETIME") == 0) {
    RtcDateTime now = rtc.GetDateTime();
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "DATETIME:%04d-%02d-%02d %02d:%02d:%02d",
      now.Year(), now.Month(), now.Day(),
      now.Hour(), now.Minute(), now.Second()
    );
    sendResponse(responseBuffer);
    return true;
  }
  



// ============================================================
// CONSOLIDATED GET COMMANDS FOR DRAWER-SPECIFIC LOADING
// These return all data for a drawer in a single response
// ============================================================

// GET:CO2_S1_ALL - All CO2 Sensor 1 data in one response
// Shortened field names to reduce response size:
// M=Mode(F/P), A=FruitMax, B=FruitMin, C=PinMax, D=PinMin,
// E=DelayDays, F=DelayHours, G=DelayMins, H=DelaySecs, I=DelayActive,
// J=FATIntDays, K=FATIntHours, L=FATIntMins, N=FATIntSecs,
// O=FATDurDays, P=FATDurHours, Q=FATDurMins, R=FATDurSecs, S=FATEnable, T=FATActive
else if (strcmp(upperCommand, "GET:CO2_S1_ALL") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S1_ALL:M=%s,A=%u,B=%u,C=%u,D=%u,E=%u,F=%u,G=%u,H=%u,I=%u,J=%u,K=%u,L=%u,N=%u,O=%u,P=%u,Q=%u,R=%u,S=%u,T=%u",
    co2Sensors[0].mode ? "F" : "P",
    values[VAL_CO2_S1][0], values[VAL_CO2_S1][1],
    co2Sensors[0].pinMax, co2Sensors[0].pinMin,
    co2Sensors[0].delayInterval[0], co2Sensors[0].delayInterval[1], co2Sensors[0].delayInterval[2], co2Sensors[0].delayInterval[3],
    co2Sensors[0].delayActive ? 1 : 0,
    co2Sensors[0].FATInterval[0], co2Sensors[0].FATInterval[1], co2Sensors[0].FATInterval[2], co2Sensors[0].FATInterval[3],
    co2Sensors[0].FATDuration[0], co2Sensors[0].FATDuration[1], co2Sensors[0].FATDuration[2], co2Sensors[0].FATDuration[3],
    co2Sensors[0].FATEnable ? 1 : 0,
    co2Sensors[0].FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
  return true;
}

// GET:CO2_S2_ALL - All CO2 Sensor 2 data in one response
else if (strcmp(upperCommand, "GET:CO2_S2_ALL") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S2_ALL:M=%s,A=%u,B=%u,C=%u,D=%u,E=%u,F=%u,G=%u,H=%u,I=%u,J=%u,K=%u,L=%u,N=%u,O=%u,P=%u,Q=%u,R=%u,S=%u,T=%u",
    co2Sensors[1].mode ? "F" : "P",
    values[VAL_CO2_S2][0], values[VAL_CO2_S2][1],
    co2Sensors[1].pinMax, co2Sensors[1].pinMin,
    co2Sensors[1].delayInterval[0], co2Sensors[1].delayInterval[1], co2Sensors[1].delayInterval[2], co2Sensors[1].delayInterval[3],
    co2Sensors[1].delayActive ? 1 : 0,
    co2Sensors[1].FATInterval[0], co2Sensors[1].FATInterval[1], co2Sensors[1].FATInterval[2], co2Sensors[1].FATInterval[3],
    co2Sensors[1].FATDuration[0], co2Sensors[1].FATDuration[1], co2Sensors[1].FATDuration[2], co2Sensors[1].FATDuration[3],
    co2Sensors[1].FATEnable ? 1 : 0,
    co2Sensors[1].FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
  return true;
}

// GET:CAL_ALL - All calibration data in one response
else if (strcmp(upperCommand, "GET:CAL_ALL") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_ALL:A=%d,B=%d,C=%d,D=%d,E=%d,F=%d,G=%d,H=%d,I=%d,J=%d,K=%u,L=%u,N=%u",
    calS1Temp, calS1Humidity, calS1CO2,
    calS2Temp, calS2Humidity, calS2CO2,
    calAmbientTemp, calAmbientHumidity, calAmbientCO2, ambientCO2Offset,
    sensor1Enabled ? 1 : 0,
    sensor2Enabled ? 1 : 0,
    ambientEnabled ? 1 : 0
  );
  sendResponse(responseBuffer);
  return true;
}

// GET:HUM_ALL - All humidity data in one response
else if (strcmp(upperCommand, "GET:HUM_ALL") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "HUM_ALL:A=%u,B=%u,C=%u,D=%u",
    values[VAL_HUM][0], values[VAL_HUM][1],
    humidityS2Max, humidityS2Min
  );
  sendResponse(responseBuffer);
  return true;
}


// ============================================================
// DRAWER-SPECIFIC GET COMMANDS FOR ANDROID APP
// Add these to processBluetoothCommand() in AI-16.ino
// Insert BEFORE the existing "GET:CONFIG" handler (around line 3715)
// ============================================================



// GET:HUM_CONFIG - DISABLED (replaced by GET:HUM_ALL)
#if 0
else if (strcmp(upperCommand, "GET:HUM_CONFIG") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_HUM:S1_MAX=%u,S1_MIN=%u,S2_MAX=%u,S2_MIN=%u",
    values[VAL_HUM][0], values[VAL_HUM][1], humidityS2Max, humidityS2Min
  );
  sendResponse(responseBuffer);
}
#endif

// GET:CO2_S1_CONFIG - DISABLED (replaced by GET:CO2_S1_ALL)
#if 0
// Returns: mode, fruit/pin thresholds, delay, and FAT settings
else if (strcmp(upperCommand, "GET:CO2_S1_CONFIG") == 0) {
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  // Part 1: Basic CO2 S1 config
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_CO2_S1:MODE=%s,FRUIT_MAX=%u,FRUIT_MIN=%u,PIN_MAX=%u,PIN_MIN=%u",
    co2S1Mode ? "FRUIT" : "PIN",
    values[VAL_CO2_S1][0], values[VAL_CO2_S1][1], co2S1PinMax, co2S1PinMin
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 2: CO2 S1 Delay
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S1_DELAY:D=%u,H=%u,M=%u,S=%u,ACT=%u",
    co2S1DelayInterval[0], co2S1DelayInterval[1], co2S1DelayInterval[2], co2S1DelayInterval[3],
    co2S1DelayActive ? 1 : 0
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 3: CO2 S1 FAT
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S1_FAT:ID=%u,IH=%u,IM=%u,IS=%u,DD=%u,DH=%u,DM=%u,DS=%u,EN=%u,ACT=%u",
    co2S1FATInterval[0], co2S1FATInterval[1], co2S1FATInterval[2], co2S1FATInterval[3],
    co2S1FATDuration[0], co2S1FATDuration[1], co2S1FATDuration[2], co2S1FATDuration[3],
    co2S1FATEnable ? 1 : 0, co2S1FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif

// GET:CO2_S2_CONFIG - DISABLED (replaced by GET:CO2_S2_ALL)
#if 0
// Returns: mode, fruit/pin thresholds, delay, and FAT settings
else if (strcmp(upperCommand, "GET:CO2_S2_CONFIG") == 0) {
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  // Part 1: Basic CO2 S2 config
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_CO2_S2:MODE=%s,FRUIT_MAX=%u,FRUIT_MIN=%u,PIN_MAX=%u,PIN_MIN=%u",
    co2S2Mode ? "FRUIT" : "PIN",
    values[VAL_CO2_S2][0], values[VAL_CO2_S2][1], co2S2PinMax, co2S2PinMin
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 2: CO2 S2 Delay
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S2_DELAY:D=%u,H=%u,M=%u,S=%u,ACT=%u",
    co2S2DelayInterval[0], co2S2DelayInterval[1], co2S2DelayInterval[2], co2S2DelayInterval[3],
    co2S2DelayActive ? 1 : 0
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 3: CO2 S2 FAT
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S2_FAT:ID=%u,IH=%u,IM=%u,IS=%u,DD=%u,DH=%u,DM=%u,DS=%u,EN=%u,ACT=%u",
    co2S2FATInterval[0], co2S2FATInterval[1], co2S2FATInterval[2], co2S2FATInterval[3],
    co2S2FATDuration[0], co2S2FATDuration[1], co2S2FATDuration[2], co2S2FATDuration[3],
    co2S2FATEnable ? 1 : 0, co2S2FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif

// GET:TEMP_CONFIG - Temperature settings only (for Temperature drawer)
else if (strcmp(upperCommand, "GET:TEMP_CONFIG") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_TEMP:UNIT=%s",
    useFahrenheit ? "F" : "C"
  );
  sendResponse(responseBuffer);
  return true;
}

// GET:LOGGING_CONFIG - Data logging settings only (for Data Logging drawer)
else if (strcmp(upperCommand, "GET:LOGGING_CONFIG") == 0) {
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) { WIFI_SERIAL.read(); }
  }
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "STATUS:S1=%s,S2=%s,AMB=%s,LOG=%s",
    sensor1Enabled ? "ON" : "OFF",
    sensor2Enabled ? "ON" : "OFF",
    ambientEnabled ? "ON" : "OFF",
    isLogging ? "ON" : "OFF"
  );
  sendResponse(responseBuffer);
  delay(150);
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "LOG_INTERVAL:D=%u,H=%u,M=%u,S=%u",
    logInterval[0], logInterval[1], logInterval[2], logInterval[3]
  );
  sendResponse(responseBuffer);
  return true;
}

// GET:CAL_CONFIG - DISABLED (replaced by GET:CAL_ALL)
#if 0
// Same as GET:CALIBRATION but with added STATUS for sensor enable states
else if (strcmp(upperCommand, "GET:CAL_CONFIG") == 0) {
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  // Part 1: Status (for sensor enable toggles)
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "STATUS:S1=%s,S2=%s,AMB=%s,LOG=%s",
    sensor1Enabled ? "ON" : "OFF",
    sensor2Enabled ? "ON" : "OFF",
    ambientEnabled ? "ON" : "OFF",
    isLogging ? "ON" : "OFF"
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 2: S1 Calibration
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_S1:TEMP=%d,HUM=%d,CO2=%d",
    calS1Temp, calS1Humidity, calS1CO2
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 3: S2 Calibration
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_S2:TEMP=%d,HUM=%d,CO2=%d",
    calS2Temp, calS2Humidity, calS2CO2
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 4: Ambient Calibration
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_AMB:TEMP=%d,HUM=%d,CO2=%d,OFFSET=%d",
    calAmbientTemp, calAmbientHumidity, calAmbientCO2, ambientCO2Offset
  );
  sendResponse(responseBuffer);
}
#endif

// ============================================================
// END OF DRAWER-SPECIFIC GET COMMANDS
// ============================================================


  
// GET:CONFIG - DISABLED (replaced by drawer-specific GET:*_ALL commands)
#if 0
else if (strcmp(upperCommand, "GET:CONFIG") == 0) {
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  // Part 1: Humidity settings
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_HUM:S1_MAX=%u,S1_MIN=%u,S2_MAX=%u,S2_MIN=%u",
    values[VAL_HUM][0], values[VAL_HUM][1], humidityS2Max, humidityS2Min
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 2: CO2 Sensor 1
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_CO2_S1:MODE=%s,FRUIT_MAX=%u,FRUIT_MIN=%u,PIN_MAX=%u,PIN_MIN=%u",
    co2S1Mode ? "FRUIT" : "PIN",
    values[VAL_CO2_S1][0], values[VAL_CO2_S1][1], co2S1PinMax, co2S1PinMin
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 3: CO2 Sensor 2
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_CO2_S2:MODE=%s,FRUIT_MAX=%u,FRUIT_MIN=%u,PIN_MAX=%u,PIN_MIN=%u",
    co2S2Mode ? "FRUIT" : "PIN",
    values[VAL_CO2_S2][0], values[VAL_CO2_S2][1], co2S2PinMax, co2S2PinMin
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 4: Lights
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_LIGHTS:ON=%u,OFF=%u",
    values[VAL_LIGHTS][0], values[VAL_LIGHTS][1]
  );
  sendResponse(responseBuffer);
  delay(150);
  
  // Part 5: Temperature unit
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CFG_TEMP:UNIT=%s",
    useFahrenheit ? "F" : "C"
  );
  sendResponse(responseBuffer);
  delay(200);
}
#endif
  
// GET:CALIBRATION - DISABLED (replaced by GET:CAL_ALL)
#if 0
else if (strcmp(upperCommand, "GET:CALIBRATION") == 0) {
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_S1:TEMP=%d,HUM=%d,CO2=%d",
    calS1Temp, calS1Humidity, calS1CO2
  );
  sendResponse(responseBuffer);
  delay(150);
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_S2:TEMP=%d,HUM=%d,CO2=%d",
    calS2Temp, calS2Humidity, calS2CO2
  );
  sendResponse(responseBuffer);
  delay(150);
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CAL_AMB:TEMP=%d,HUM=%d,CO2=%d,OFFSET=%d",
    calAmbientTemp, calAmbientHumidity, calAmbientCO2, ambientCO2Offset
  );
  sendResponse(responseBuffer);
  delay(200);
}
#endif

// GET:CO2_S1_DELAY - DISABLED (included in GET:CO2_S1_ALL)
#if 0  
else if (strcmp(upperCommand, "GET:CO2_S1_DELAY") == 0) {
  // Flush any pending serial data before response
  if (lastCommandChannel == CHANNEL_WIFI) {
    delay(50);
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S1_DELAY:D=%u,H=%u,M=%u,S=%u,ACT=%u",
    co2S1DelayInterval[0], co2S1DelayInterval[1], co2S1DelayInterval[2], co2S1DelayInterval[3],
    co2S1DelayActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif

// GET:CO2_S2_DELAY - DISABLED (included in GET:CO2_S2_ALL)
#if 0  
else if (strcmp(upperCommand, "GET:CO2_S2_DELAY") == 0) {
  // Flush any pending serial data before response
  if (lastCommandChannel == CHANNEL_WIFI) {
    delay(50);
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S2_DELAY:D=%u,H=%u,M=%u,S=%u,ACT=%u",
    co2S2DelayInterval[0], co2S2DelayInterval[1], co2S2DelayInterval[2], co2S2DelayInterval[3],
    co2S2DelayActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif

// GET:CO2_S1_FAT - DISABLED (included in GET:CO2_S1_ALL)
#if 0  
else if (strcmp(upperCommand, "GET:CO2_S1_FAT") == 0) {
  // Flush any pending serial data before response
  if (lastCommandChannel == CHANNEL_WIFI) {
    delay(50);
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S1_FAT:ID=%u,IH=%u,IM=%u,IS=%u,DD=%u,DH=%u,DM=%u,DS=%u,EN=%u,ACT=%u",
    co2S1FATInterval[0], co2S1FATInterval[1], co2S1FATInterval[2], co2S1FATInterval[3],
    co2S1FATDuration[0], co2S1FATDuration[1], co2S1FATDuration[2], co2S1FATDuration[3],
    co2S1FATEnable ? 1 : 0, co2S1FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif

// GET:CO2_S2_FAT - DISABLED (included in GET:CO2_S2_ALL)
#if 0  
else if (strcmp(upperCommand, "GET:CO2_S2_FAT") == 0) {
  // Flush any pending serial data before response
  if (lastCommandChannel == CHANNEL_WIFI) {
    delay(50);
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "CO2_S2_FAT:ID=%u,IH=%u,IM=%u,IS=%u,DD=%u,DH=%u,DM=%u,DS=%u,EN=%u,ACT=%u",
    co2S2FATInterval[0], co2S2FATInterval[1], co2S2FATInterval[2], co2S2FATInterval[3],
    co2S2FATDuration[0], co2S2FATDuration[1], co2S2FATDuration[2], co2S2FATDuration[3],
    co2S2FATEnable ? 1 : 0, co2S2FATActive ? 1 : 0
  );
  sendResponse(responseBuffer);
}
#endif
  
else if (strcmp(upperCommand, "GET:LOG_INTERVAL") == 0) {
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
    "LOG_INTERVAL:D=%u,H=%u,M=%u,S=%u",
    logInterval[0], logInterval[1], logInterval[2], logInterval[3]
  );
  sendResponse(responseBuffer);
  return true;
}

  // PING - handled here for fast response
  else if (strcmp(upperCommand, "PING") == 0) {
    sendResponse("PONG");
    return true;
  }

  return false;
} // end handleGetDataCommands

// ===== SET SENSOR/CONFIG commands handler =====
bool handleSetSensorCommands(const char* upperCommand, char* command) {

  // SET:PAUSE_DATA - Pause automatic sensor data transmission (for settings screens)
  if (strcmp(upperCommand, "SET:PAUSE_DATA") == 0) {
    wifiDataPaused = true;
    #if DEBUG_COMMANDS
    Serial.println(">>> WiFi data PAUSED");
    #endif
    sendResponse("OK:DATA_PAUSED");
    return true;
  }

  // SET:RESUME_DATA - Resume automatic sensor data transmission
  else if (strcmp(upperCommand, "SET:RESUME_DATA") == 0) {
    wifiDataPaused = false;
    #if DEBUG_COMMANDS
    Serial.println(">>> WiFi data RESUMED");
    #endif
    sendResponse("OK:DATA_RESUMED");
    return true;
  }


// OLD INDIVIDUAL HUMIDITY SET COMMANDS - DISABLED (replaced by SET:HUM_S1 and SET:HUM_S2)
#if 0
  // SET:HUM_S1_MAX=value
  else if (strncmp(upperCommand, "SET:HUM_S1_MAX=", 15) == 0) {
    uint16_t value = atoi(command + 15);
    if (value >= 0 && value <= 1000) {
      values[VAL_HUM][0] = value;
      EEPROM.put(HUMIDITY_S1_MAX_ADDR, value);
      sendResponse("OK:HUM_S1_MAX");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
  
  // SET:HUM_S1_MIN=value
  else if (strncmp(upperCommand, "SET:HUM_S1_MIN=", 15) == 0) {
    uint16_t value = atoi(command + 15);
    if (value >= 0 && value <= 1000) {
      values[VAL_HUM][1] = value;
      EEPROM.put(HUMIDITY_S1_MIN_ADDR, value);
      sendResponse("OK:HUM_S1_MIN");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
  
  // SET:HUM_S2_MAX=value
  else if (strncmp(upperCommand, "SET:HUM_S2_MAX=", 15) == 0) {
    uint16_t value = atoi(command + 15);
    if (value >= 0 && value <= 1000) {
      humidityS2Max = value;
      EEPROM.put(HUMIDITY_S2_MAX_ADDR, value);
      sendResponse("OK:HUM_S2_MAX");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
  
  // SET:HUM_S2_MIN=value
  else if (strncmp(upperCommand, "SET:HUM_S2_MIN=", 15) == 0) {
    uint16_t value = atoi(command + 15);
    if (value >= 0 && value <= 1000) {
      humidityS2Min = value;
      EEPROM.put(HUMIDITY_S2_MIN_ADDR, value);
      sendResponse("OK:HUM_S2_MIN");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
#endif

// OLD INDIVIDUAL CO2 SET COMMANDS - DISABLED (replaced by SET:CO2_S1_CFG and SET:CO2_S2_CFG)
#if 0
  // SET:CO2_S1_FRUIT_MAX=value
  else if (strncmp(upperCommand, "SET:CO2_S1_FRUIT_MAX=", 21) == 0) {
    uint16_t value = atoi(command + 21);
    if (value >= 0 && value <= CO2_MAX_RANGE) {
      values[VAL_CO2_S1][0] = value;
      EEPROM.put(CO2_S1_FRUIT_MAX_ADDR, value);
      sendResponse("OK:CO2_S1_FRUIT_MAX");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
  
  // SET:CO2_S1_FRUIT_MIN=value
  else if (strncmp(upperCommand, "SET:CO2_S1_FRUIT_MIN=", 21) == 0) {
    uint16_t value = atoi(command + 21);
    if (value >= 0 && value <= CO2_MAX_RANGE) {
      values[VAL_CO2_S1][1] = value;
      EEPROM.put(CO2_S1_FRUIT_MIN_ADDR, value);
      sendResponse("OK:CO2_S1_FRUIT_MIN");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  }
  
  // SET:CO2_S1_MODE=FRUIT or PIN
  else if (strncmp(upperCommand, "SET:CO2_S1_MODE=", 16) == 0) {
    if (strcmp(command + 16, "FRUIT") == 0) {
      co2S1Mode = true;
      EEPROM.write(CO2_S1_MODE_ADDR, 1);
      sendResponse("OK:CO2_S1_MODE=FRUIT");
    } else if (strcmp(command + 16, "PIN") == 0) {
      co2S1Mode = false;
      EEPROM.write(CO2_S1_MODE_ADDR, 0);
      sendResponse("OK:CO2_S1_MODE=PIN");
    } else {
      sendError("ERR:INVALID_MODE");
    }
  }
  
  // SET:CO2_S2_MODE=FRUIT or PIN
  else if (strncmp(upperCommand, "SET:CO2_S2_MODE=", 16) == 0) {
    if (strcmp(command + 16, "FRUIT") == 0) {
      co2S2Mode = true;
      EEPROM.write(CO2_S2_MODE_ADDR, 1);
      sendResponse("OK:CO2_S2_MODE=FRUIT");
    } else if (strcmp(command + 16, "PIN") == 0) {
      co2S2Mode = false;
      EEPROM.write(CO2_S2_MODE_ADDR, 0);
      sendResponse("OK:CO2_S2_MODE=PIN");
    } else {
      sendError("ERR:INVALID_MODE");
    }
  }
#endif
  
  // SET:SENSOR_S1=ON or OFF
  else if (strncmp(upperCommand, "SET:SENSOR_S1=", 14) == 0) {
    if (strcmp(command + 14, "ON") == 0) {
      sensor1Enabled = true;
      EEPROM.write(SENSOR_S1_ENABLE_ADDR, 1);
      sendResponse("OK:SENSOR_S1=ON");
    } else if (strcmp(command + 14, "OFF") == 0) {
      sensor1Enabled = false;
      EEPROM.write(SENSOR_S1_ENABLE_ADDR, 0);
      sendResponse("OK:SENSOR_S1=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
    return true;
  }
  
  // SET:SENSOR_S2=ON or OFF
  else if (strncmp(upperCommand, "SET:SENSOR_S2=", 14) == 0) {
    if (strcmp(command + 14, "ON") == 0) {
      sensor2Enabled = true;
      EEPROM.write(SENSOR_S2_ENABLE_ADDR, 1);
      sendResponse("OK:SENSOR_S2=ON");
    } else if (strcmp(command + 14, "OFF") == 0) {
      sensor2Enabled = false;
      EEPROM.write(SENSOR_S2_ENABLE_ADDR, 0);
      sendResponse("OK:SENSOR_S2=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
    return true;
  }
  
  // SET:AMBIENT=ON or OFF
  else if (strncmp(upperCommand, "SET:AMBIENT=", 12) == 0) {
    if (strcmp(command + 12, "ON") == 0) {
      ambientEnabled = true;
      EEPROM.write(AMBIENT_ENABLE_ADDR, 1);
      sendResponse("OK:AMBIENT=ON");
    } else if (strcmp(command + 12, "OFF") == 0) {
      ambientEnabled = false;
      EEPROM.write(AMBIENT_ENABLE_ADDR, 0);
      sendResponse("OK:AMBIENT=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
    return true;
  }
  
  // SET:LOGGING=ON or OFF
  else if (strncmp(upperCommand, "SET:LOGGING=", 12) == 0) {
    if (strcmp(command + 12, "ON") == 0) {
      if (sdCardPresent) {
        isLogging = true;
        EEPROM.write(LOG_STATUS_ADDR, 1);
        nextLogTime = millis();
        sendResponse("OK:LOGGING=ON");
      } else {
        sendError("ERR:NO_SD_CARD");
      }
    } else if (strcmp(command + 12, "OFF") == 0) {
      isLogging = false;
      EEPROM.write(LOG_STATUS_ADDR, 0);
      sendResponse("OK:LOGGING=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
    return true;
  }
  
  // SET:TEMP_UNIT=C or F
  else if (strncmp(upperCommand, "SET:TEMP_UNIT=", 14) == 0) {
    if (strcmp(command + 14, "C") == 0) {
      useFahrenheit = false;
      EEPROM.write(TEMP_UNIT_ADDR, 0);
      sendResponse("OK:TEMP_UNIT=C");
    } else if (strcmp(command + 14, "F") == 0) {
      useFahrenheit = true;
      EEPROM.write(TEMP_UNIT_ADDR, 1);
      sendResponse("OK:TEMP_UNIT=F");
    } else {
      sendError("ERR:INVALID_UNIT");
    }
    return true;
  }
  
  // SET:CO2_S1_DELAY=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S1_DELAY=", 17) == 0) {
    int d, h, m, s;
    if (sscanf(command + 17, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2Sensors[0].delayInterval[0] = d;
      co2Sensors[0].delayInterval[1] = h;
      co2Sensors[0].delayInterval[2] = m;
      co2Sensors[0].delayInterval[3] = s;
      EEPROM.put(CO2_S1_DELAY_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S1_DELAY_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S1_DELAY_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S1_DELAY_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S1_DELAY");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
    return true;
  }

  // SET:CO2_S2_DELAY=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S2_DELAY=", 17) == 0) {
    int d, h, m, s;
    if (sscanf(command + 17, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2Sensors[1].delayInterval[0] = d;
      co2Sensors[1].delayInterval[1] = h;
      co2Sensors[1].delayInterval[2] = m;
      co2Sensors[1].delayInterval[3] = s;
      EEPROM.put(CO2_S2_DELAY_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S2_DELAY_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S2_DELAY_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S2_DELAY_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S2_DELAY");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
    return true;
  }
  
// OLD INDIVIDUAL FAT SET COMMANDS - DISABLED (replaced by SET:CO2_S1_FAT and SET:CO2_S2_FAT)
#if 0
  // SET:CO2_S1_FAT_INT=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S1_FAT_INT=", 19) == 0) {
    int d, h, m, s;
    if (sscanf(command + 19, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2S1FATInterval[0] = d;
      co2S1FATInterval[1] = h;
      co2S1FATInterval[2] = m;
      co2S1FATInterval[3] = s;
      EEPROM.put(CO2_S1_FAT_INTERVAL_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S1_FAT_INTERVAL_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S1_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S1_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S1_FAT_INT");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
  }
  
  // SET:CO2_S1_FAT_DUR=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S1_FAT_DUR=", 19) == 0) {
    int d, h, m, s;
    if (sscanf(command + 19, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2S1FATDuration[0] = d;
      co2S1FATDuration[1] = h;
      co2S1FATDuration[2] = m;
      co2S1FATDuration[3] = s;
      EEPROM.put(CO2_S1_FAT_DURATION_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S1_FAT_DURATION_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S1_FAT_DURATION_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S1_FAT_DURATION_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S1_FAT_DUR");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
  }
  
  // SET:CO2_S1_FAT_EN=ON or OFF
  else if (strncmp(upperCommand, "SET:CO2_S1_FAT_EN=", 18) == 0) {
    if (strcmp(command + 18, "ON") == 0) {
      co2S1FATEnable = true;
      EEPROM.write(CO2_S1_FAT_ENABLE_ADDR, 1);
      sendResponse("OK:CO2_S1_FAT_EN=ON");
    } else if (strcmp(command + 18, "OFF") == 0) {
      co2S1FATEnable = false;
      EEPROM.write(CO2_S1_FAT_ENABLE_ADDR, 0);
      sendResponse("OK:CO2_S1_FAT_EN=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
  }
  
  // SET:CO2_S2_FAT_INT=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S2_FAT_INT=", 19) == 0) {
    int d, h, m, s;
    if (sscanf(command + 19, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2S2FATInterval[0] = d;
      co2S2FATInterval[1] = h;
      co2S2FATInterval[2] = m;
      co2S2FATInterval[3] = s;
      EEPROM.put(CO2_S2_FAT_INTERVAL_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S2_FAT_INTERVAL_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S2_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S2_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S2_FAT_INT");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
  }
  
  // SET:CO2_S2_FAT_DUR=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:CO2_S2_FAT_DUR=", 19) == 0) {
    int d, h, m, s;
    if (sscanf(command + 19, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2S2FATDuration[0] = d;
      co2S2FATDuration[1] = h;
      co2S2FATDuration[2] = m;
      co2S2FATDuration[3] = s;
      EEPROM.put(CO2_S2_FAT_DURATION_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(CO2_S2_FAT_DURATION_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(CO2_S2_FAT_DURATION_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_S2_FAT_DURATION_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_S2_FAT_DUR");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
  }
  
  // SET:CO2_S2_FAT_EN=ON or OFF
  else if (strncmp(upperCommand, "SET:CO2_S2_FAT_EN=", 18) == 0) {
    if (strcmp(command + 18, "ON") == 0) {
      co2S2FATEnable = true;
      EEPROM.write(CO2_S2_FAT_ENABLE_ADDR, 1);
      sendResponse("OK:CO2_S2_FAT_EN=ON");
    } else if (strcmp(command + 18, "OFF") == 0) {
      co2S2FATEnable = false;
      EEPROM.write(CO2_S2_FAT_ENABLE_ADDR, 0);
      sendResponse("OK:CO2_S2_FAT_EN=OFF");
    } else {
      sendError("ERR:INVALID_STATE");
    }
  }
#endif
  
  // SET:LOG_INTERVAL=days,hours,minutes,seconds
  else if (strncmp(upperCommand, "SET:LOG_INTERVAL=", 17) == 0) {
    int d, h, m, s;
    if (sscanf(command + 17, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      logInterval[0] = d;
      logInterval[1] = h;
      logInterval[2] = m;
      logInterval[3] = s;
      EEPROM.put(LOG_DAYS_ADDR, (uint32_t)d);
      EEPROM.put(LOG_HOURS_ADDR, (uint32_t)h);
      EEPROM.put(LOG_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(LOG_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:LOG_INTERVAL");
    } else {
      sendError("ERR:INVALID_FORMAT");
    }
    return true;
  }
  

// ============================================================
// CONSOLIDATED SET COMMANDS - Multiple values in one command
// ============================================================

// SET:HUM_S1=<max>,<min> - Set both humidity S1 values at once
else if (strncmp(upperCommand, "SET:HUM_S1=", 11) == 0) {
  int max_val, min_val;
  if (sscanf(command + 11, "%d,%d", &max_val, &min_val) == 2) {
    if (max_val >= 0 && max_val <= 1000 && min_val >= 0 && min_val <= 1000) {
      values[VAL_HUM][0] = max_val;
      values[VAL_HUM][1] = min_val;
      EEPROM.put(HUMIDITY_S1_MAX_ADDR, (uint16_t)max_val);
      EEPROM.put(HUMIDITY_S1_MIN_ADDR, (uint16_t)min_val);
      sendResponse("OK:HUM_S1");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

// SET:HUM_S2=<max>,<min> - Set both humidity S2 values at once
else if (strncmp(upperCommand, "SET:HUM_S2=", 11) == 0) {
  int max_val, min_val;
  if (sscanf(command + 11, "%d,%d", &max_val, &min_val) == 2) {
    if (max_val >= 0 && max_val <= 1000 && min_val >= 0 && min_val <= 1000) {
      humidityS2Max = max_val;
      humidityS2Min = min_val;
      EEPROM.put(HUMIDITY_S2_MAX_ADDR, (uint16_t)max_val);
      EEPROM.put(HUMIDITY_S2_MIN_ADDR, (uint16_t)min_val);
      sendResponse("OK:HUM_S2");
    } else {
      sendError("ERR:VALUE_RANGE");
    }
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

// SET:CO2_S1_CFG=<mode>,<fruitMax>,<fruitMin>,<pinMax>,<pinMin>
else if (strncmp(upperCommand, "SET:CO2_S1_CFG=", 15) == 0) {
  char modeStr[6];
  int fMax, fMin, pMax, pMin;
  if (sscanf(command + 15, "%5[^,],%d,%d,%d,%d", modeStr, &fMax, &fMin, &pMax, &pMin) == 5) {
    // Convert mode string to uppercase for comparison
    for (int i = 0; modeStr[i]; i++) modeStr[i] = toupper(modeStr[i]);

    co2Sensors[0].mode = (strcmp(modeStr, "FRUIT") == 0 || strcmp(modeStr, "F") == 0);
    values[VAL_CO2_S1][0] = fMax;
    values[VAL_CO2_S1][1] = fMin;
    co2Sensors[0].pinMax = pMax;
    co2Sensors[0].pinMin = pMin;

    EEPROM.write(CO2_S1_MODE_ADDR, co2Sensors[0].mode ? 1 : 0);
    EEPROM.put(CO2_S1_FRUIT_MAX_ADDR, (uint16_t)fMax);
    EEPROM.put(CO2_S1_FRUIT_MIN_ADDR, (uint16_t)fMin);
    EEPROM.put(CO2_S1_PIN_MAX_ADDR, (uint16_t)pMax);
    EEPROM.put(CO2_S1_PIN_MIN_ADDR, (uint16_t)pMin);

    sendResponse("OK:CO2_S1_CFG");
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

// SET:CO2_S2_CFG=<mode>,<fruitMax>,<fruitMin>,<pinMax>,<pinMin>
else if (strncmp(upperCommand, "SET:CO2_S2_CFG=", 15) == 0) {
  char modeStr[6];
  int fMax, fMin, pMax, pMin;
  if (sscanf(command + 15, "%5[^,],%d,%d,%d,%d", modeStr, &fMax, &fMin, &pMax, &pMin) == 5) {
    for (int i = 0; modeStr[i]; i++) modeStr[i] = toupper(modeStr[i]);

    co2Sensors[1].mode = (strcmp(modeStr, "FRUIT") == 0 || strcmp(modeStr, "F") == 0);
    values[VAL_CO2_S2][0] = fMax;
    values[VAL_CO2_S2][1] = fMin;
    co2Sensors[1].pinMax = pMax;
    co2Sensors[1].pinMin = pMin;

    EEPROM.write(CO2_S2_MODE_ADDR, co2Sensors[1].mode ? 1 : 0);
    EEPROM.put(CO2_S2_FRUIT_MAX_ADDR, (uint16_t)fMax);
    EEPROM.put(CO2_S2_FRUIT_MIN_ADDR, (uint16_t)fMin);
    EEPROM.put(CO2_S2_PIN_MAX_ADDR, (uint16_t)pMax);
    EEPROM.put(CO2_S2_PIN_MIN_ADDR, (uint16_t)pMin);

    sendResponse("OK:CO2_S2_CFG");
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

// SET:CO2_S1_FAT=<intD>,<intH>,<intM>,<intS>,<durD>,<durH>,<durM>,<durS>,<enabled>
else if (strncmp(upperCommand, "SET:CO2_S1_FAT=", 15) == 0) {
  int iD, iH, iM, iS, dD, dH, dM, dS, en;
  if (sscanf(command + 15, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &iD, &iH, &iM, &iS, &dD, &dH, &dM, &dS, &en) == 9) {
    co2Sensors[0].FATInterval[0] = iD; co2Sensors[0].FATInterval[1] = iH;
    co2Sensors[0].FATInterval[2] = iM; co2Sensors[0].FATInterval[3] = iS;
    co2Sensors[0].FATDuration[0] = dD; co2Sensors[0].FATDuration[1] = dH;
    co2Sensors[0].FATDuration[2] = dM; co2Sensors[0].FATDuration[3] = dS;
    co2Sensors[0].FATEnable = (en != 0);
    EEPROM.put(CO2_S1_FAT_INTERVAL_DAYS_ADDR, (uint32_t)iD);
    EEPROM.put(CO2_S1_FAT_INTERVAL_HOURS_ADDR, (uint32_t)iH);
    EEPROM.put(CO2_S1_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)iM);
    EEPROM.put(CO2_S1_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)iS);
    EEPROM.put(CO2_S1_FAT_DURATION_DAYS_ADDR, (uint32_t)dD);
    EEPROM.put(CO2_S1_FAT_DURATION_HOURS_ADDR, (uint32_t)dH);
    EEPROM.put(CO2_S1_FAT_DURATION_MINUTES_ADDR, (uint32_t)dM);
    EEPROM.put(CO2_S1_FAT_DURATION_SECONDS_ADDR, (uint32_t)dS);
    EEPROM.write(CO2_S1_FAT_ENABLE_ADDR, co2Sensors[0].FATEnable ? 1 : 0);
    sendResponse("OK:CO2_S1_FAT");
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

// SET:CO2_S2_FAT=<intD>,<intH>,<intM>,<intS>,<durD>,<durH>,<durM>,<durS>,<enabled>
else if (strncmp(upperCommand, "SET:CO2_S2_FAT=", 15) == 0) {
  int iD, iH, iM, iS, dD, dH, dM, dS, en;
  if (sscanf(command + 15, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &iD, &iH, &iM, &iS, &dD, &dH, &dM, &dS, &en) == 9) {
    co2Sensors[1].FATInterval[0] = iD; co2Sensors[1].FATInterval[1] = iH;
    co2Sensors[1].FATInterval[2] = iM; co2Sensors[1].FATInterval[3] = iS;
    co2Sensors[1].FATDuration[0] = dD; co2Sensors[1].FATDuration[1] = dH;
    co2Sensors[1].FATDuration[2] = dM; co2Sensors[1].FATDuration[3] = dS;
    co2Sensors[1].FATEnable = (en != 0);
    EEPROM.put(CO2_S2_FAT_INTERVAL_DAYS_ADDR, (uint32_t)iD);
    EEPROM.put(CO2_S2_FAT_INTERVAL_HOURS_ADDR, (uint32_t)iH);
    EEPROM.put(CO2_S2_FAT_INTERVAL_MINUTES_ADDR, (uint32_t)iM);
    EEPROM.put(CO2_S2_FAT_INTERVAL_SECONDS_ADDR, (uint32_t)iS);
    EEPROM.put(CO2_S2_FAT_DURATION_DAYS_ADDR, (uint32_t)dD);
    EEPROM.put(CO2_S2_FAT_DURATION_HOURS_ADDR, (uint32_t)dH);
    EEPROM.put(CO2_S2_FAT_DURATION_MINUTES_ADDR, (uint32_t)dM);
    EEPROM.put(CO2_S2_FAT_DURATION_SECONDS_ADDR, (uint32_t)dS);
    EEPROM.write(CO2_S2_FAT_ENABLE_ADDR, co2Sensors[1].FATEnable ? 1 : 0);
    sendResponse("OK:CO2_S2_FAT");
  } else {
    sendError("ERR:FORMAT");
  }
  return true;
}

  if (strncmp(upperCommand, "SET:DATETIME=", 13) == 0) {
    int year, month, day, hour, minute, second;
    if (sscanf(command + 13, "%d,%d,%d,%d,%d,%d",
               &year, &month, &day, &hour, &minute, &second) == 6) {
      RtcDateTime newDateTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                              (uint8_t)hour, (uint8_t)minute, (uint8_t)second);
      rtc.SetDateTime(newDateTime);
      RtcDateTime confirmed = rtc.GetDateTime();
      snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
          "DATETIME:%04d-%02d-%02d %02d:%02d:%02d",
          confirmed.Year(), confirmed.Month(), confirmed.Day(),
          confirmed.Hour(), confirmed.Minute(), confirmed.Second());
      sendResponse(responseBuffer);
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }

  return false;
} // end handleSetSensorCommands

// ===== WIFI CONFIGURATION commands handler =====
bool handleWiFiCommands(const char* upperCommand, char* command) {

  // GET:WIFI_CONFIG - Get WiFi configuration
  if (strcmp(upperCommand, "GET:WIFI_CONFIG") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "WIFI_CONFIG:EN=%s,SSID=%s,PORT=%u,IP=%s,ST=%d",
      wifiEnabled ? "Y" : "N",
      wifiSSID,
      wifiPort,
      wifiIPAddress,
      (int)wifiState
    );
    sendResponse(responseBuffer);
    return true;
  }

  // SET:WIFI_SSID=<ssid> - Set WiFi SSID
  else if (strncmp(upperCommand, "SET:WIFI_SSID=", 14) == 0) {
    const char* newSSID = command + 14;
    if (strlen(newSSID) > 0 && strlen(newSSID) < WIFI_SSID_MAX_LEN) {
      strncpy(wifiSSID, newSSID, WIFI_SSID_MAX_LEN - 1);
      wifiSSID[WIFI_SSID_MAX_LEN - 1] = '\0';
      saveWifiCredentials();
      sendResponse("OK:WIFI_SSID");
    } else {
      sendError("ERR:INVALID_SSID");
    }
    return true;
  }

  // SET:WIFI_PASS=<password> - Set WiFi password
  else if (strncmp(upperCommand, "SET:WIFI_PASS=", 14) == 0) {
    const char* newPass = command + 14;
    if (strlen(newPass) < WIFI_PASS_MAX_LEN) {
      strncpy(wifiPassword, newPass, WIFI_PASS_MAX_LEN - 1);
      wifiPassword[WIFI_PASS_MAX_LEN - 1] = '\0';
      saveWifiCredentials();
      sendResponse("OK:WIFI_PASS");
    } else {
      sendError("ERR:INVALID_PASS");
    }
    return true;
  }

  // SET:WIFI_PORT=<port> - Set WiFi TCP port
  else if (strncmp(upperCommand, "SET:WIFI_PORT=", 14) == 0) {
    uint16_t newPort = atoi(command + 14);
    if (newPort > 0 && newPort < 65535) {
      wifiPort = newPort;
      saveWifiCredentials();
      sendResponse("OK:WIFI_PORT");
    } else {
      sendError("ERR:INVALID_PORT");
    }
    return true;
  }

  // SET:WIFI_ENABLED=ON/OFF - Enable/disable WiFi
  else if (strncmp(upperCommand, "SET:WIFI_ENABLED=", 17) == 0) {
    if (strcmp(command + 17, "ON") == 0) {
      wifiEnabled = true;
      saveWifiCredentials();
      sendResponse("OK:WIFI_ENABLED_ON");
      // Reinitialize WiFi
      initWifi();
    } else if (strcmp(command + 17, "OFF") == 0) {
      wifiEnabled = false;
      wifiState = WIFI_STATE_IDLE;
      wifiConnected = false;
      wifiClientConnected = false;
      saveWifiCredentials();
      sendResponse("OK:WIFI_ENABLED_OFF");
    } else {
      sendError("ERR:INVALID_VALUE");
    }
    return true;
  }

  // WIFI_RESTART - Restart WiFi connection
  else if (strcmp(upperCommand, "WIFI_RESTART") == 0) {
    sendResponse("OK:WIFI_RESTARTING");
    wifiState = WIFI_STATE_IDLE;
    wifiConnected = false;
    wifiClientConnected = false;
    wifiReconnectAttempts = 0;  // Reset reconnect counter
    strcpy(wifiIPAddress, "0.0.0.0");
    initWifi();
    return true;
  }

  return false;
} // end handleWiFiCommands

// ===== CALIBRATION commands handler =====
bool handleCalibrationCommands(const char* upperCommand, char* command) {

  // SET:CAL_S1=<temp>,<hum>,<co2> - Set all S1 calibration at once
  if (strncmp(upperCommand, "SET:CAL_S1=", 11) == 0) {
    int temp, hum, co2;
    if (sscanf(command + 11, "%d,%d,%d", &temp, &hum, &co2) == 3) {
      calS1Temp = temp;
      calS1Humidity = hum;
      calS1CO2 = co2;
      EEPROM.put(CAL_S1_TEMP_ADDR, (int16_t)temp);
      EEPROM.put(CAL_S1_HUMIDITY_ADDR, (int16_t)hum);
      EEPROM.put(CAL_S1_CO2_ADDR, (int16_t)co2);
      sendResponse("OK:CAL_S1");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }

  // SET:CAL_S2=<temp>,<hum>,<co2> - Set all S2 calibration at once
  else if (strncmp(upperCommand, "SET:CAL_S2=", 11) == 0) {
    int temp, hum, co2;
    if (sscanf(command + 11, "%d,%d,%d", &temp, &hum, &co2) == 3) {
      calS2Temp = temp;
      calS2Humidity = hum;
      calS2CO2 = co2;
      EEPROM.put(CAL_S2_TEMP_ADDR, (int16_t)temp);
      EEPROM.put(CAL_S2_HUMIDITY_ADDR, (int16_t)hum);
      EEPROM.put(CAL_S2_CO2_ADDR, (int16_t)co2);
      sendResponse("OK:CAL_S2");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }

  // SET:CAL_AMB=<temp>,<hum>,<co2>,<offset> - Set all ambient calibration at once
  else if (strncmp(upperCommand, "SET:CAL_AMB=", 12) == 0) {
    int temp, hum, co2, offset;
    if (sscanf(command + 12, "%d,%d,%d,%d", &temp, &hum, &co2, &offset) == 4) {
      calAmbientTemp = temp;
      calAmbientHumidity = hum;
      calAmbientCO2 = co2;
      ambientCO2Offset = offset;
      EEPROM.put(CAL_AMBIENT_TEMP_ADDR, (int16_t)temp);
      EEPROM.put(CAL_AMBIENT_HUMIDITY_ADDR, (int16_t)hum);
      EEPROM.put(CAL_AMBIENT_CO2_ADDR, (int16_t)co2);
      EEPROM.put(AMBIENT_CO2_OFFSET_ADDR, (int16_t)offset);
      sendResponse("OK:CAL_AMB");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }

  return false;
} // end handleCalibrationCommands

void sendSensorData() {
  char tempStr[10];
  bool anythingSent = false;
  
  // Check if heartbeat is needed (force send everything)
  bool forceHeartbeat = (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL);
  
  // Flush any pending serial data before starting multi-part response
  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) {
      WIFI_SERIAL.read();
    }
  }
  
  // Sensor 1 data - only send if changed or heartbeat
  if (sensor1Enabled) {
    float displayTemp1 = useFahrenheit ? celsiusToFahrenheit(currentTemperature1) : currentTemperature1;
    
    // Check if values changed (using small threshold for floats)
    bool s1Changed = forceHeartbeat ||
                     abs(currentCO2_1 - lastSentCO2_1) >= 1.0 ||
                     abs(displayTemp1 - lastSentTemp1) >= 0.1 ||
                     abs(currentHumidity1 - lastSentHum1) >= 0.1;
    
    if (s1Changed) {
      strcpy(responseBuffer, "DATA_S1:CO2=");
      dtostrf(currentCO2_1, 1, 0, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",TEMP=");
      dtostrf(displayTemp1, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",HUM=");
      dtostrf(currentHumidity1, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      
      sendResponse(responseBuffer);
      delay(150);
      
      // Update last sent values
      lastSentCO2_1 = currentCO2_1;
      lastSentTemp1 = displayTemp1;
      lastSentHum1 = currentHumidity1;
      anythingSent = true;
    }
  }
  
  // Sensor 2 data - only send if changed or heartbeat
  if (sensor2Enabled) {
    float displayTemp2 = useFahrenheit ? celsiusToFahrenheit(currentTemperature2) : currentTemperature2;
    
    bool s2Changed = forceHeartbeat ||
                     abs(currentCO2_2 - lastSentCO2_2) >= 1.0 ||
                     abs(displayTemp2 - lastSentTemp2) >= 0.1 ||
                     abs(currentHumidity2 - lastSentHum2) >= 0.1;
    
    if (s2Changed) {
      strcpy(responseBuffer, "DATA_S2:CO2=");
      dtostrf(currentCO2_2, 1, 0, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",TEMP=");
      dtostrf(displayTemp2, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",HUM=");
      dtostrf(currentHumidity2, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      
      sendResponse(responseBuffer);
      delay(150);
      
      lastSentCO2_2 = currentCO2_2;
      lastSentTemp2 = displayTemp2;
      lastSentHum2 = currentHumidity2;
      anythingSent = true;
    }
  }
  
  // Ambient sensor data - only send if changed or heartbeat
  if (ambientEnabled && ambientCO2 > 0) {
    float displayTempAmb = useFahrenheit ? celsiusToFahrenheit(ambientTemperature) : ambientTemperature;
    
    bool ambChanged = forceHeartbeat ||
                      abs(ambientCO2 - lastSentCO2_Amb) >= 1.0 ||
                      abs(displayTempAmb - lastSentTempAmb) >= 0.1 ||
                      abs(ambientHumidity - lastSentHumAmb) >= 0.1;
    
    if (ambChanged) {
      strcpy(responseBuffer, "DATA_AMB:CO2=");
      dtostrf(ambientCO2, 1, 0, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",TEMP=");
      dtostrf(displayTempAmb, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      strcat(responseBuffer, ",HUM=");
      dtostrf(ambientHumidity, 1, 1, tempStr);
      strcat(responseBuffer, tempStr);
      
      sendResponse(responseBuffer);
      delay(150);
      
      lastSentCO2_Amb = ambientCO2;
      lastSentTempAmb = displayTempAmb;
      lastSentHumAmb = ambientHumidity;
      anythingSent = true;
    }
  }
  
  // Relay status - only send if changed or heartbeat
  bool relayHum1 = (digitalRead(HUMIDITY_S1_RELAY_PIN) == LOW);
  bool relayCO2S1 = (digitalRead(CO2_RELAY_PIN) == LOW);
  bool relayCO2S2 = (digitalRead(CO2_S2_RELAY_PIN) == LOW);
  bool relayHum2 = (digitalRead(HUMIDITY_S2_RELAY_PIN) == LOW);

  bool relayChanged = forceHeartbeat ||
                      relayHum1 != lastSentRelayHum ||
                      relayCO2S1 != lastSentRelayCO2S1 ||
                      relayCO2S2 != lastSentRelayCO2S2 ||
                      relayHum2 != lastSentRelayHum2;

  if (relayChanged) {
    // Temperature unit - send with relay data
    sendResponse(useFahrenheit ? "TEMP_UNIT:F" : "TEMP_UNIT:C");
    delay(150);

    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "RELAYS:HUM1=%s,CO2_S1=%s,CO2_S2=%s,HUM2=%s",
      relayHum1 ? "ON" : "OFF",
      relayCO2S1 ? "ON" : "OFF",
      relayCO2S2 ? "ON" : "OFF",
      relayHum2 ? "ON" : "OFF"
    );
    sendResponse(responseBuffer);

    lastSentRelayHum = relayHum1;
    lastSentRelayCO2S1 = relayCO2S1;
    lastSentRelayCO2S2 = relayCO2S2;
    lastSentRelayHum2 = relayHum2;
    anythingSent = true;
  }
  
  // Update heartbeat time if anything was sent
  if (anythingSent || forceHeartbeat) {
    lastHeartbeatTime = millis();
  }
}

void sendBluetoothResponse(const char* response) {
  BT_SERIAL.println(response);
  // Serial.print("BT > ");
  // Serial.println(response);
}

void sendBluetoothError(const char* error) {
  BT_SERIAL.println(error);
  // Serial.print("BT Error > ");
  // Serial.println(error);
}

// ==================== WIFI FUNCTIONS (ESP-01S AT Commands) ====================

void loadWifiCredentials() {
  // Check if WiFi is enabled
  wifiEnabled = (EEPROM.read(WIFI_ENABLED_ADDR) == 1);
  
  // Load SSID
  for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
    wifiSSID[i] = EEPROM.read(WIFI_SSID_ADDR + i);
  }
  wifiSSID[WIFI_SSID_MAX_LEN - 1] = '\0';  // Ensure null termination
  
  // Load Password
  for (int i = 0; i < WIFI_PASS_MAX_LEN; i++) {
    wifiPassword[i] = EEPROM.read(WIFI_PASS_ADDR + i);
  }
  wifiPassword[WIFI_PASS_MAX_LEN - 1] = '\0';  // Ensure null termination
  
  // Load Port
  EEPROM.get(WIFI_PORT_ADDR, wifiPort);
  if (wifiPort == 0 || wifiPort == 0xFFFF) {
    wifiPort = 8266;  // Default port
  }
  
  // Serial.print("WiFi credentials loaded - Enabled: ");
  // Serial.print(wifiEnabled ? "Yes" : "No");
  // Serial.print(", SSID: ");
  // Serial.print(wifiSSID);
  // Serial.print(", Port: ");
  // Serial.println(wifiPort);
}

void saveWifiCredentials() {
  EEPROM.write(WIFI_ENABLED_ADDR, wifiEnabled ? 1 : 0);
  
  // Save SSID
  for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, wifiSSID[i]);
  }
  
  // Save Password
  for (int i = 0; i < WIFI_PASS_MAX_LEN; i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, wifiPassword[i]);
  }
  
  // Save Port
  EEPROM.put(WIFI_PORT_ADDR, wifiPort);
  
  // Serial.println("WiFi credentials saved to EEPROM");
}

void initWifi() {
  // Try common baud rates
  long baudRates[] = {9600, 57600, 38400, 115200, 74880};
  int numRates = 5;
  bool found = false;
  
  for (int i = 0; i < numRates && !found; i++) {
    WIFI_SERIAL.begin(baudRates[i]);
    delay(100);
    
    while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
    
    WIFI_SERIAL.println("AT");
    delay(500);  // Give time for response
    
    // Read response into buffer, look for OK
    char buf[32] = {0};
    uint8_t idx = 0;
    while (WIFI_SERIAL.available() && idx < 31) {
      buf[idx++] = WIFI_SERIAL.read();
    }
    buf[idx] = '\0';
    
    if (strstr(buf, "OK")) {
      Serial.print(F("ESP-01S @ "));
      Serial.println(baudRates[i]);
      found = true;
      break;
    }
  }
  
  if (!found) {
    Serial.println(F("ESP-01S not responding"));
    wifiState = WIFI_STATE_ERROR;
    return;
  }
  
  // Serial.println("WiFi Serial initialized");
  
  // Load credentials from EEPROM
  loadWifiCredentials();
  
  if (!wifiEnabled) {
    // Serial.println("WiFi is disabled in settings");
    wifiState = WIFI_STATE_IDLE;
    return;
  }
  
  if (strlen(wifiSSID) == 0) {
    // Serial.println("No WiFi SSID configured");
    wifiState = WIFI_STATE_IDLE;
    return;
  }
  
  // Start initialization
  wifiState = WIFI_STATE_INITIALIZING;
  wifiStateTimeout = millis() + 5000;
  
  Serial.println("WiFi connecting...");
  
  // Reset ESP-01S
  delay(100);
  WIFI_SERIAL.println("AT+RST");
  delay(2000);  // Wait for reset
  
  // Clear any pending data
  while (WIFI_SERIAL.available()) {
    WIFI_SERIAL.read();
  }
}

bool sendATCommand(const char* cmd, const char* expectedResponse, unsigned long timeout) {
  // Clear buffer
  while (WIFI_SERIAL.available()) {
    WIFI_SERIAL.read();
  }
  
  // Serial.print("WiFi AT > ");
  // Serial.println(cmd);
  
  WIFI_SERIAL.println(cmd);
  
  return waitForResponse(expectedResponse, timeout);
}

bool waitForResponse(const char* expectedResponse, unsigned long timeout) {
  unsigned long startTime = millis();
  char window[16] = {0};
  uint8_t wIdx = 0;
  
  while (millis() - startTime < timeout) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      window[wIdx] = c;
      wIdx = (wIdx + 1) % 15;
      window[wIdx] = 0;
      
      if (strstr(window, expectedResponse)) return true;
      if (strstr(window, "ERROR") || strstr(window, "FAIL")) return false;
    }
  }
  return false;
}

void wifiStateMachine() {
  if (!wifiEnabled) {
    return;
  }
  
  switch (wifiState) {
    case WIFI_STATE_INITIALIZING:
      // Test AT communication
      if (sendATCommand("AT", "OK", 2000)) {
        Serial.println("ESP-01S responding");
        
        // Set station mode
        if (sendATCommand("AT+CWMODE=1", "OK", 2000)) {
          // Serial.println("Set to Station mode");
          wifiState = WIFI_STATE_CONNECTING;
          wifiStateTimeout = millis() + 20000;  // 20 second timeout for connection
          connectToWifi();
        } else {
          wifiState = WIFI_STATE_ERROR;
        }
      } else {
        if (millis() > wifiStateTimeout) {
          Serial.println("ESP-01S not responding - check wiring");
          wifiState = WIFI_STATE_ERROR;
        }
      }
      break;
      
    case WIFI_STATE_CONNECTING:
      // Check if connected
      if (millis() > wifiStateTimeout) {
        // Serial.println("WiFi connection timeout");
        wifiState = WIFI_STATE_ERROR;
      }
      // Connection result is handled in connectToWifi()
      break;
      
    case WIFI_STATE_CONNECTED:
      // Start TCP server
      startTcpServer();
      break;
      
    case WIFI_STATE_SERVER_STARTED:
      // Normal operation - handled in handleWifiCommands()
    //   debugWifiState("STATE_SERVER_STARTED check");
      break;
      
    case WIFI_STATE_CLIENT_CONNECTED:
      // Client connected - handled in handleWifiCommands()
      // Check for client timeout
      if (wifiClientConnected && (millis() - lastWifiActivityTime > WIFI_TIMEOUT)) {
        // Serial.println("WiFi client timeout");
        wifiClientConnected = false;
        wifiState = WIFI_STATE_SERVER_STARTED;
      }
      break;
      
    case WIFI_STATE_ERROR:
      // Could implement retry logic here
      break;
      
    case WIFI_STATE_IDLE:
    default:
      break;
  }
}

void connectToWifi() {
  Serial.print(F("Connecting to: "));
  Serial.println(wifiSSID);
  
  // Use responseBuffer for command
  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE, "AT+CWJAP=\"%s\",\"%s\"", wifiSSID, wifiPassword);
  
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
  WIFI_SERIAL.println(responseBuffer);
  
  // Simple sliding window to detect OK/FAIL - no String needed
  char window[8] = {0};
  uint8_t wIdx = 0;
  unsigned long startTime = millis();
  bool gotOK = false, gotFail = false;
  
  while (millis() - startTime < 20000 && !gotOK && !gotFail) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      window[wIdx] = c;
      wIdx = (wIdx + 1) % 7;
      window[wIdx] = 0;
      if (strstr(window, "OK")) gotOK = true;
      if (strstr(window, "FAIL")) gotFail = true;
    }
    delay(5);
  }
  
  Serial.print(F("OK:"));
  Serial.print(gotOK ? "Y" : "N");
  Serial.print(F(" FAIL:"));
  Serial.println(gotFail ? "Y" : "N");
  
  bool verified = false;
  if (!gotFail) {
    delay(1000);
    while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
    WIFI_SERIAL.println(F("AT+CWJAP?"));
    
    memset(window, 0, sizeof(window));
    wIdx = 0;
    startTime = millis();
    bool foundSSID = false;
    
    while (millis() - startTime < 3000 && !foundSSID) {
      if (WIFI_SERIAL.available()) {
        char c = WIFI_SERIAL.read();
        if (c == wifiSSID[0]) {
          bool match = true;
          uint8_t ssidLen = strlen(wifiSSID);
          for (uint8_t i = 1; i < ssidLen && match; i++) {
            unsigned long t = millis();
            while (!WIFI_SERIAL.available() && millis() - t < 100);
            if (WIFI_SERIAL.available()) {
              if (WIFI_SERIAL.read() != wifiSSID[i]) match = false;
            } else match = false;
          }
          if (match) foundSSID = true;
        }
      }
      delay(5);
    }
    
    if (foundSSID) {
      Serial.println(F("Verified connected!"));
      verified = true;
    } else if (gotOK) {
      verified = true;
    }
  }
  
  if (verified || (gotOK && !gotFail)) {
    Serial.println(F("WiFi connected!"));
    wifiConnected = true;
    wifiState = WIFI_STATE_CONNECTED;
    wifiReconnectAttempts = 0;
    
    delay(500);
    while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
    WIFI_SERIAL.println(F("AT+CIFSR"));
    
    startTime = millis();
    bool inIP = false;
    uint8_t ipIdx = 0;
    char ipBuf[16] = {0};
    
    while (millis() - startTime < 3000 && ipIdx < 15) {
      if (WIFI_SERIAL.available()) {
        char c = WIFI_SERIAL.read();
        if (!inIP && c == '"') {
          inIP = true;
        } else if (inIP) {
          if (c == '"') {
            if (ipIdx >= 7) {
              uint8_t dots = 0;
              for (uint8_t i = 0; i < ipIdx; i++) {
                if (ipBuf[i] == '.') dots++;
              }
              if (dots == 3 && strcmp(ipBuf, "0.0.0.0") != 0) {
                strcpy(wifiIPAddress, ipBuf);
                Serial.print(F("IP: "));
                Serial.println(wifiIPAddress);
                break;
              }
            }
            inIP = false;
            ipIdx = 0;
            memset(ipBuf, 0, sizeof(ipBuf));
          } else if ((c >= '0' && c <= '9') || c == '.') {
            ipBuf[ipIdx++] = c;
          } else {
            inIP = false;
            ipIdx = 0;
            memset(ipBuf, 0, sizeof(ipBuf));
          }
        }
      }
      delay(5);
    }
  } else {
    Serial.println(F("WiFi failed"));
    wifiState = WIFI_STATE_ERROR;
  }
}

void startTcpServer() {
  // Serial.println("Starting TCP server...");
  
  // Enable multiple connections
  if (!sendATCommand("AT+CIPMUX=1", "OK", 2000)) {
    // Serial.println("Failed to enable multiple connections");
    wifiState = WIFI_STATE_ERROR;
    return;
  }
  
  // Start server on specified port
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%u", wifiPort);
  
  if (sendATCommand(cmd, "OK", 2000)) {
    Serial.print("Server: ");
    Serial.print(wifiIPAddress);
    Serial.print(":");
    Serial.println(wifiPort);
    wifiState = WIFI_STATE_SERVER_STARTED;
    // Serial.print("WiFi state set to: ");
    // Serial.println(wifiState);
  } else {
    Serial.println("TCP server failed");
    wifiState = WIFI_STATE_ERROR;
  }
}

void debugWifiState(const char* location) {
  // Commented out to save memory
  // Serial.print("WiFi State at ");
  // Serial.print(location);
  // Serial.print(": ");
  // Serial.println(wifiState);
}

// Check if WiFi is still connected and has valid IP
bool checkWifiHealth() {
  if (!wifiEnabled) return false;
  
  // If we have a client connected and recently active, skip the check
  // to avoid interfering with data flow
  if (wifiClientConnected && (millis() - lastWifiActivityTime < 30000)) {
    return true;
  }
  
  // If server is running, assume healthy for now
  // We'll detect disconnection via CLOSED messages
  if (wifiState >= WIFI_STATE_SERVER_STARTED) {
    return true;
  }
  
  return wifiConnected;
}

// Attempt to reconnect WiFi
void attemptWifiReconnect() {
  if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS) {
    // Only print once when max reached
    if (wifiReconnectAttempts == MAX_WIFI_RECONNECT_ATTEMPTS) {
      Serial.println("Max reconnect attempts");
      wifiReconnectAttempts++;  // Increment to prevent repeated message
    }
    return;
  }
  
  if (millis() - wifiReconnectAttemptTime < WIFI_RECONNECT_DELAY) {
    return;  // Wait between attempts
  }
  
  wifiReconnectAttempts++;
  wifiReconnectAttemptTime = millis();
  
  Serial.print("Reconnect ");
  Serial.print(wifiReconnectAttempts);
  Serial.print("/");
  Serial.println(MAX_WIFI_RECONNECT_ATTEMPTS);
  
  // Reset state and reinitialize
  wifiState = WIFI_STATE_IDLE;
  wifiConnected = false;
  wifiClientConnected = false;
  wifiDataPaused = false;
  strcpy(wifiIPAddress, "0.0.0.0");
  
  initWifi();
}

// Periodic WiFi maintenance - call from main loop
void wifiMaintenance() {
  if (!wifiEnabled) return;
  
  // Only do health checks when we think we're connected
  if (wifiState == WIFI_STATE_SERVER_STARTED || wifiState == WIFI_STATE_CLIENT_CONNECTED) {
    if (millis() - lastWifiHealthCheck > WIFI_HEALTH_CHECK_INTERVAL) {
      lastWifiHealthCheck = millis();
      
      if (!checkWifiHealth()) {
        Serial.println("WiFi lost, reconnecting...");
        wifiState = WIFI_STATE_ERROR;
        wifiConnected = false;
        strcpy(wifiIPAddress, "0.0.0.0");
      } else {
        // Connection is healthy, reset reconnect counter
        wifiReconnectAttempts = 0;
      }
    }
  }
  
  // Handle error state - attempt reconnection
  if (wifiState == WIFI_STATE_ERROR) {
    attemptWifiReconnect();
  }
}

void handleWifiCommands() {
  if (!wifiEnabled || wifiState < WIFI_STATE_SERVER_STARTED) {
    return;
  }
  
  // Clear any stale CR/LF at start of buffer
  while (wifiBufferIndex > 0 && (wifiBuffer[0] == '\r' || wifiBuffer[0] == '\n')) {
    // Shift buffer left
    memmove(wifiBuffer, wifiBuffer + 1, wifiBufferIndex);
    wifiBufferIndex--;
    wifiBuffer[wifiBufferIndex] = '\0';
  }
  
  // Check for available data
  int avail = WIFI_SERIAL.available();
  if (avail > 0) {
    #if DEBUG_WIFI_RX
      Serial.print("[WiFi RX:");
      Serial.print(avail);
      Serial.print(" bytes] ");
    #endif
  }
  
  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    #if DEBUG_WIFI_RX
    // Print character or hex for non-printable
    if (c >= 32 && c < 127) {
      Serial.print(c);
    } else {
      Serial.print("\\x");
      if ((uint8_t)c < 16) Serial.print("0");
      Serial.print((uint8_t)c, HEX);
    }
    #endif
    
    // Add to buffer
    if (wifiBufferIndex < WIFI_BUFFER_SIZE - 1) {
      wifiBuffer[wifiBufferIndex++] = c;
      wifiBuffer[wifiBufferIndex] = '\0';
    }
    
    // Check for client connect notification (0,CONNECT)
    char* connectPtr = strstr(wifiBuffer, ",CONNECT");
    if (connectPtr != NULL && strstr(wifiBuffer, "+IPD") == NULL) {
      #if DEBUG_WIFI_STATE
      Serial.println();
      Serial.println(">>> Detected ,CONNECT");
      #endif
      if (!wifiClientConnected) {
        wifiClientConnected = true;
        wifiState = WIFI_STATE_CLIENT_CONNECTED;
        #if DEBUG_WIFI_STATE
        Serial.println(">>> Client flag set TRUE!");
        #endif
        lastWifiActivityTime = millis();
      }
      // Find end of CONNECT message (look for newline after CONNECT)
      char* endPtr = strchr(connectPtr, '\n');
      if (endPtr != NULL && (endPtr + 1 - wifiBuffer) < wifiBufferIndex) {
        // There's more data after CONNECT - preserve it
        int remaining = wifiBufferIndex - (endPtr + 1 - wifiBuffer);
        memmove(wifiBuffer, endPtr + 1, remaining);
        wifiBufferIndex = remaining;
        wifiBuffer[wifiBufferIndex] = '\0';
      } else {
        wifiBufferIndex = 0;
        wifiBuffer[0] = '\0';
      }
      continue;
    }
    
    // Check for +IPD message: +IPD,<id>,<len>:<data>
    // Also check for partial pattern ,<id>,<len>: (when +IPD was consumed)
    char* ipdPtr = strstr(wifiBuffer, "+IPD,");
    char* partialPtr = NULL;
    
    // If no full +IPD found, look for partial pattern like ",0," at start of meaningful data
    if (ipdPtr == NULL) {
      // Look for pattern like ",0,11:" which is partial +IPD
      for (int i = 0; i < wifiBufferIndex - 4; i++) {
        if (wifiBuffer[i] == ',' && 
            wifiBuffer[i+1] >= '0' && wifiBuffer[i+1] <= '9' &&
            wifiBuffer[i+2] == ',') {
          // Found potential partial IPD, check for colon
          char* testColon = strchr(wifiBuffer + i + 3, ':');
          if (testColon != NULL && (testColon - (wifiBuffer + i)) < 10) {
            partialPtr = wifiBuffer + i;
            break;
          }
        }
      }
    }
    
    char* parsePtr = ipdPtr ? ipdPtr : partialPtr;
    int skipLen = ipdPtr ? 5 : 1;  // Skip "+IPD," or just ","
    
    if (parsePtr != NULL) {
      char* colon = strchr(parsePtr, ':');
      if (colon != NULL) {
        // Parse length - find the comma after connection ID
        char* comma1 = strchr(parsePtr + skipLen, ',');
        if (comma1 != NULL && comma1 < colon) {
          int dataLen = atoi(comma1 + 1);
          char* dataStart = colon + 1;
          int receivedLen = wifiBufferIndex - (dataStart - wifiBuffer);
          
          // Wait until we have all the data
          if (receivedLen >= dataLen) {
            #if DEBUG_WIFI_RX
            Serial.println();
            Serial.print(">>> Got ");
            Serial.print(dataLen);
            Serial.println(" bytes");
            #endif
            
            // Mark client as connected
            if (!wifiClientConnected) {
              wifiClientConnected = true;
              wifiState = WIFI_STATE_CLIENT_CONNECTED;
              #if DEBUG_WIFI_STATE
              Serial.println(">>> Client flag set TRUE (via IPD)!");
              #endif
            }
            lastWifiActivityTime = millis();
            
            // Extract command into responseBuffer (temporary use)
            int cmdLen = 0;
            for (int i = 0; i < dataLen && i < (RESPONSE_BUFFER_SIZE - 1); i++) {
              char ch = dataStart[i];
              if (ch != '\r' && ch != '\n' && ch >= 32) {
                responseBuffer[cmdLen++] = ch;
              }
            }
            responseBuffer[cmdLen] = '\0';
            
            if (cmdLen > 0) {
              #if DEBUG_COMMANDS
              Serial.print(F(">>> Cmd: ["));
              Serial.print(responseBuffer);
              Serial.println(F("]"));
              #endif
              lastCommandChannel = CHANNEL_WIFI;
              processBluetoothCommand(responseBuffer);
            }
            
            // Remove processed data from buffer, keep any remaining data
            // Calculate where the processed +IPD message ends
            int processedEnd = (dataStart - wifiBuffer) + dataLen;
            if (processedEnd < wifiBufferIndex) {
              // There's more data after this +IPD - shift it to start of buffer
              int remaining = wifiBufferIndex - processedEnd;
              memmove(wifiBuffer, wifiBuffer + processedEnd, remaining);
              wifiBufferIndex = remaining;
              wifiBuffer[wifiBufferIndex] = '\0';
            } else {
              // Clear buffer
              wifiBufferIndex = 0;
              wifiBuffer[0] = '\0';
            }
          }
        }
      }
    }
    
    // Check for connection closed
    else if (strstr(wifiBuffer, "CLOSED") != NULL) {
      #if DEBUG_WIFI_STATE
      Serial.println();
      Serial.println(">>> Client disconnected");
      #endif
      wifiClientConnected = false;
      wifiDataPaused = false;  // ADD THIS LINE - Reset pause state for next connection
      wifiState = WIFI_STATE_SERVER_STARTED;
      wifiBufferIndex = 0;
      wifiBuffer[0] = '\0';
    }
    // Clear buffer if too long
    else if (wifiBufferIndex > 150) {
      wifiBufferIndex = 0;
      wifiBuffer[0] = '\0';
    }
  }
}

void processWifiCommand(char* command) {
  // Serial.print("WiFi Command: ");
  // Serial.println(command);
  
  // Convert to uppercase for comparison
  char upperCmd[BT_BUFFER_SIZE];
  strncpy(upperCmd, command, BT_BUFFER_SIZE - 1);
  upperCmd[BT_BUFFER_SIZE - 1] = '\0';
  for (int i = 0; upperCmd[i]; i++) {
    upperCmd[i] = toupper(upperCmd[i]);
  }
  
  // Handle WiFi-specific commands first
  
  // GET:WIFI_STATUS - Get WiFi connection status
  if (strcmp(upperCmd, "GET:WIFI_STATUS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE, 
      "WIFI_STATUS:EN=%s,CONN=%s,IP=%s,PORT=%u",
      wifiEnabled ? "Y" : "N",
      wifiConnected ? "Y" : "N",
      wifiIPAddress,
      wifiPort
    );
    sendWifiResponse(responseBuffer);
    return;
  }
  
  // All other commands go through the standard processor
  // The processBluetoothCommand function will use lastCommandChannel
  // to determine where to send responses
  lastCommandChannel = CHANNEL_WIFI;
  processBluetoothCommand(command);
}

void sendWifiResponse(const char* response) {
  #if DEBUG_WIFI_TX
  Serial.print("[sendWiFi:");
  Serial.print(response);
  Serial.print(" state=");
  Serial.print(wifiState);
  Serial.println("]");
  #endif
  
  if (wifiState >= WIFI_STATE_SERVER_STARTED) {
    if (!wifiClientConnected) {
      wifiClientConnected = true;
      wifiState = WIFI_STATE_CLIENT_CONNECTED;
      #if DEBUG_WIFI_STATE
      Serial.println(">>> Client flag set via send");
      #endif
      lastWifiActivityTime = millis();
    }
  } else {
    #if DEBUG_WIFI_TX
    Serial.println("[sendWiFi: NOT READY]");
    #endif
    return;
  }
  
  int len = strlen(response) + 2;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%d", len);
  
  WIFI_SERIAL.println(cmd);
  
  // Wait for ">" prompt - BUT preserve any +IPD data we encounter
  unsigned long startTime = millis();
  bool gotPrompt = false;
  
  while (millis() - startTime < 2000) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      
      if (c == '>') {
        gotPrompt = true;
        break;
      }
      
      // If we see start of +IPD data, put it in the wifi buffer for later processing
      // This preserves incoming data instead of discarding it
      if (c == '+' || (wifiBufferIndex > 0 && wifiBufferIndex < WIFI_BUFFER_SIZE - 1)) {
        // Only buffer if it looks like +IPD or we're already buffering
        if (c == '+' || strncmp(wifiBuffer, "+IPD", min(wifiBufferIndex, 4)) == 0) {
          if (wifiBufferIndex < WIFI_BUFFER_SIZE - 1) {
            wifiBuffer[wifiBufferIndex++] = c;
            wifiBuffer[wifiBufferIndex] = '\0';
          }
        }
      }
      // Ignore OK, AT echo, etc. - just don't buffer random chars
    }
  }
  
  if (!gotPrompt) {
    #if DEBUG_WIFI_TX
    Serial.println("WiFi: No > prompt - client disconnected?");
    #endif
    wifiClientConnected = false;
    wifiState = WIFI_STATE_SERVER_STARTED;
    return;
  }
  
  delay(10);
  
  WIFI_SERIAL.println(response);
  
  // Wait for SEND OK
  char window[12] = {0};
  uint8_t wIdx = 0;
  startTime = millis();
  while (millis() - startTime < 500) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      window[wIdx] = c;
      wIdx = (wIdx + 1) % 11;
      window[wIdx] = 0;
      if (strstr(window, "SEND OK") || strstr(window, "+IPD")) break;
    }
  }
  
  lastWifiActivityTime = millis();
}

void sendWifiError(const char* error) {
  sendWifiResponse(error);
  // Serial.print("WiFi Error > ");
  // Serial.println(error);
}

// Unified response sender - sends to whichever channel last received a command
void sendResponse(const char* response) {
  if (lastCommandChannel == CHANNEL_WIFI) {
    sendWifiResponse(response);
  } else {
    sendBluetoothResponse(response);
  }
}

void sendError(const char* error) {
  if (lastCommandChannel == CHANNEL_WIFI) {
    sendWifiError(error);
  } else {
    sendBluetoothError(error);
  }
}

// ============================================================

void loop() {
  // Handle Bluetooth communication
  handleBluetoothCommands();
  
  // Handle WiFi communication
  handleWifiCommands();
  wifiStateMachine();
  wifiMaintenance();  // Auto-reconnect and health check

  readSensors();
  updateRelays();
  handleInput();
  
  if (isLogging && sdCardPresent && millis() >= nextLogTime) {
    logDataToSD();
  }
  
  if(showSplash) {
    showSplashScreen();
  } else {
    updateDisplay();
  }
  
  delay(100);
}
