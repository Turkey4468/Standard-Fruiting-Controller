#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <SPI.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include "SdFat.h"
#include "sdios.h"
#include <SoftwareWire.h>

// Create software I2C instance for SCD41 sensor
SoftwareWire softWire(2, 3); // SDA on pin 2, SCL on pin 3

// SCD41 I2C address and commands
#define SCD41_I2C_ADDR 0x62
#define SCD41_START_PERIODIC_MEASUREMENT 0x21b1
#define SCD41_READ_MEASUREMENT 0xec05
#define SCD41_STOP_PERIODIC_MEASUREMENT 0x3f86
#define SCD41_WAKE_UP 0x36f6

// ====================  BLUETOOTH & WIFI CONFIGURATION ====================
#define BT_SERIAL     Serial1    // TX=18, RX=19
#define WIFI_SERIAL   Serial2    // TX=16, RX=17
#define BT_BAUD_RATE  9600
#define WIFI_BAUD_RATE 115200
#define BT_BUFFER_SIZE  64
#define WIFI_BUFFER_SIZE 128
#define BT_SEND_INTERVAL 2000
#define HEARTBEAT_INTERVAL 30000
#define BT_TIMEOUT 5000
#define WIFI_TIMEOUT 30000
#define WIFI_HEALTH_CHECK_INTERVAL 30000
#define WIFI_RECONNECT_DELAY 15000
#define MAX_WIFI_RECONNECT_ATTEMPTS 10
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 48
#define RESPONSE_BUFFER_SIZE 100
// =========================================================================

#define SD_CS_PIN SS  // Chip select pin for SD card

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 24

// EEPROM Address Map - Define specific address for every stored value
const uint16_t EEPROM_MAGIC = 0xAB51;
const int EEPROM_MAGIC_ADDR = 0;  // 2 bytes (uint16_t)
const int TEMP_UNIT_ADDR = 2;     // 1 byte (bool)

// Humidity addresses (2 bytes each - uint16_t)
const int HUMIDITY_MAX_ADDR = 3;
const int HUMIDITY_MIN_ADDR = 5;

// CO2 addresses for Fruit (previously just CO2_MAX/MIN)
const int CO2_FRUIT_MAX_ADDR = 7;     // Same as CO2_MAX_ADDR
const int CO2_FRUIT_MIN_ADDR = 9;     // Same as CO2_MIN_ADDR

// New CO2 addresses for Pin
const int CO2_PIN_MAX_ADDR = 41;      // New address
const int CO2_PIN_MIN_ADDR = 43;      // New address

// CO2 Mode address (1 byte - bool)
const int CO2_MODE_ADDR = 45;        // New address (true = Fruit, false = Pin)

// CO2 Delay addresses (4 bytes each - uint32_t)
const int CO2_DELAY_DAYS_ADDR = 46;
const int CO2_DELAY_HOURS_ADDR = 50;
const int CO2_DELAY_MINUTES_ADDR = 54;
const int CO2_DELAY_SECONDS_ADDR = 58;

// Temperature addresses (1 byte each for Celsius, 1 byte each for Fahrenheit)
const int TEMP_C_MAX_ADDR = 11;
const int TEMP_C_MIN_ADDR = 12;
const int TEMP_H_MAX_ADDR = 13;
const int TEMP_H_MIN_ADDR = 14;
const int TEMP_F_MAX_ADDR = 15;
const int TEMP_F_MIN_ADDR = 16;
const int TEMP_F_H_MAX_ADDR = 17;
const int TEMP_F_H_MIN_ADDR = 18;

// Light Timer addresses (2 bytes each - uint16_t)
const int LIGHT_ON_ADDR = 19;
const int LIGHT_OFF_ADDR = 21;

// Data Logging Interval addresses (4 bytes each - uint32_t)
const int LOG_DAYS_ADDR = 23;
const int LOG_HOURS_ADDR = 27;
const int LOG_MINUTES_ADDR = 31;
const int LOG_SECONDS_ADDR = 35;
const int LOG_STATUS_ADDR = 39;  // 1 byte (bool)

// Temperature Mode address (1 byte - bool)
const int TEMP_MODE_ADDR = 40;   // 1 byte (bool)

// Calibration offset addresses (float, 4 bytes each)
const int CAL_TEMP_ADDR = 62;
const int CAL_HUM_ADDR  = 66;
const int CAL_CO2_ADDR  = 70;

// WiFi configuration addresses (matching AI-17 layout)
const int WIFI_SSID_ADDR    = 180; // 32 bytes
const int WIFI_PASS_ADDR    = 212; // 48 bytes
const int WIFI_PORT_ADDR    = 260; // uint16_t
const int WIFI_ENABLED_ADDR = 262; // byte

// Menu structure constants
const int NUM_MENU_ITEMS = 6;  // Humidity, Temperature, CO2, Lights, Data Logging, Date/Time
const int NUM_SUB_ITEMS = 2;   // Standard number of sub-items (Min/Max or On/Off)
const int TEMP_SUB_ITEMS = 6;  // Cool Max, Cool Min, Heat Max, Heat Min, Mode, Units

// Default values for settings
// Humidity defaults
const uint16_t HUMIDITY_MAX_DEFAULT = 85;  // Default max humidity (%)
const uint16_t HUMIDITY_MIN_DEFAULT = 80;  // Default min humidity (%)

// CO2 defaults
const uint16_t CO2_DEFAULT_MAX = 2000;     // Default max CO2 level (ppm)
const uint16_t CO2_DEFAULT_MIN = 1500;     // Default min CO2 level (ppm)
const uint16_t CO2_MAX_RANGE = 10000;      // Maximum allowable CO2 value

// Light timer defaults
const uint16_t LIGHT_ON_DEFAULT = 8 * 60;   // Default light on time (8:00)
const uint16_t LIGHT_OFF_DEFAULT = 20 * 60; // Default light off time (20:00)

// Temperature defaults (in Fahrenheit)
const uint8_t TEMP_F_COOL_MAX_DEFAULT = 90;  // Cooling max default
const uint8_t TEMP_F_COOL_MIN_DEFAULT = 85;  // Cooling min default
const uint8_t TEMP_F_HEAT_MAX_DEFAULT = 75;  // Heating max default
const uint8_t TEMP_F_HEAT_MIN_DEFAULT = 70;  // Heating min default

// Temperature mode default (true = Heat, false = Cool)
const bool TEMP_MODE_DEFAULT = true;

// Date/Time constants
const int YEAR_MIN = 2000;
const int YEAR_MAX = 2099;

// Menu array with names of Main Menu items.
const char* menuItems[] = {"Humidity", "Temperature", "CO2", "Lights", "Data Logging", "Date/Time"};

// For storing which date component is being edited
int dateEditComponent = 0;  // 0=month, 1=day, 2=year

// For storing temporary edit values
int dateTimeTemp[3];  // [month, day, year] or [hours, minutes]

// For temperature handling
int tempValues[4];  // [C-Max, C-Min, H-Max, H-Min]
float tempValuesF[4];  // For Fahrenheit conversion

// Values array for storing menu settings
int values[NUM_MENU_ITEMS][NUM_SUB_ITEMS];

// Sensor reading variables
float currentHumidity = -1;
float currentTemperature = -1;
float currentCO2 = 0;

// Temperature mode variable
bool tempMode = true;  // true = Heat, false = Cool

// RTC pin definitions (unchanged)
#define RTC_DAT 27
#define RTC_CLK 11
#define RTC_RST 28

// Previously existing definitions remain unchanged
ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST);
RtcDS1302<ThreeWire> rtc(myWire);

// Joystick pins (unchanged)
const int JOY_X = A1;
const int JOY_Y = A0;
const int JOY_BTN = 30;

// Display instance (unchanged)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  /* MOSI 25 */ 25,
  /* CLK 26 */ 26,
  /* DC  23 */ 23,
  /* RST - 24  */ OLED_RESET,
  /* CS  22 */ 22);



// Menu navigation variables
int currentMenu = 0;
int subMenu = -1;
int editingValue = -1;
bool savePrompt = false;
bool showSplash = true;

// Relay pins
#define HUMIDITY_RELAY_PIN 34
#define HEAT_RELAY_PIN 36
#define CO2_RELAY_PIN 35
#define LIGHT_RELAY_PIN 37

// Timing variables
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 300;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;
bool buttonState = HIGH;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_READ_INTERVAL = 5000;
unsigned long lastSDCheckTime = 0;
const unsigned long SD_CHECK_INTERVAL = 60000; // 60 seconds between SD card checks

bool useFahrenheit = true;  // Default to Fahrenheit

// Default values for new CO2 settings
const uint16_t CO2_PIN_DEFAULT_MAX = 1000;     // Default max CO2 level for Pin (ppm)
const uint16_t CO2_PIN_DEFAULT_MIN = 800;      // Default min CO2 level for Pin (ppm)
const bool CO2_MODE_DEFAULT = true;            // Default to Fruit mode (true = Fruit, false = Pin)

// For CO2 pin settings
uint16_t co2PinMax, co2PinMin;
bool co2Mode = true;  // true = Fruit, false = Pin

// For CO2 delay interval components
uint32_t co2DelayInterval[4];  // [days, hours, minutes, seconds]
int co2DelayEditComponent = 0; // 0=days, 1=hours, 2=minutes, 3=seconds

// New constant for number of CO2 submenu items
const int CO2_SUB_ITEMS = 6;  // Fruit-Max, Fruit-Min, Pin-Max, Pin-Min, Delay, Mode

// Variables for CO2 Pin to Fruit auto-switch
unsigned long co2ModeStartTime = 0;
bool co2PinModeActive = false;

// Track the countdown time
unsigned long co2DelayStartTime = 0; // When the delay started (in millis)
unsigned long co2DelayDurationMs = 0; // Total duration of the active delay (in millis)
bool co2DelayActive = false;          // Is a delay currently active?

// For storing interval components used in Data Logging.
uint32_t logInterval[4];  // [days, hours, minutes, seconds]
bool isLogging = false;
bool sdCardPresent = false;

// For editing interval
int intervalEditComponent = 0;  // 0=days, 1=hours, 2=minutes, 3=seconds

SdFat sd;
File logFile;
unsigned long lastLogTime = 0;
unsigned long nextLogTime = 0;

// ====================  BLUETOOTH GLOBALS ====================
char btBuffer[BT_BUFFER_SIZE];
uint8_t btBufferIndex = 0;
unsigned long lastBtSendTime = 0;
unsigned long lastBtActivityTime = 0;
bool btConnected = false;

// ====================  WIFI GLOBALS ====================
char wifiBuffer[WIFI_BUFFER_SIZE];
uint8_t wifiBufferIndex = 0;
bool wifiConnected = false;
bool wifiClientConnected = false;
unsigned long lastWifiActivityTime = 0;
bool wifiEnabled = false;
bool wifiDataPaused = false;
char wifiIPAddress[16] = "0.0.0.0";
char wifiSSID[WIFI_SSID_MAX_LEN] = "";
char wifiPassword[WIFI_PASS_MAX_LEN] = "";
uint16_t wifiPort = 8266;

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
unsigned long lastWifiHealthCheck = 0;
unsigned long wifiReconnectAttemptTime = 0;
uint8_t wifiReconnectAttempts = 0;

// ====================  CALIBRATION GLOBALS ====================
float calTempOffset = 0.0;
float calHumOffset  = 0.0;
float calCO2Offset  = 0.0;

// ====================  RESPONSE ROUTING ====================
enum CommandChannel : uint8_t { CHANNEL_NONE, CHANNEL_BLUETOOTH, CHANNEL_WIFI };
CommandChannel lastCommandChannel = CHANNEL_NONE;
char responseBuffer[RESPONSE_BUFFER_SIZE];

// ====================  AUTO-PUSH CHANGE DETECTION ====================
float lastSentCO2 = -999.0, lastSentTemp = -999.0, lastSentHum = -999.0;
bool lastSentRelayHum = false, lastSentRelayHeat = false;
bool lastSentRelayCO2 = false, lastSentRelayLight = false;
unsigned long lastHeartbeatTime = 0;

// Forward declarations of functions
void debugAllValues();
void updateRelays();
void readSensors();
void showSplashScreen();
void updateDisplay();
void handleInput();
void logDataToSD();
void updateNextLogTime();
float celsiusToFahrenheit(float celsius);
float fahrenheitToCelsius(float fahrenheit);
String getTimeString(int value);
void printDateTime(const RtcDateTime& dt);
void wakeUpSensor(SoftwareWire& wire);
void startPeriodicMeasurement(SoftwareWire& wire);
bool readSensorData(SoftwareWire& wire, float& co2, float& temp, float& hum);

// Bluetooth / WiFi function declarations
void initBluetooth();
void initWifi();
void loadWifiCredentials();
void saveWifiCredentials();
void handleBluetoothCommands();
void handleWifiCommands();
void processBluetoothCommand(char* cmd);
bool handleGetDataCommands(const char* upper, char* cmd);
bool handleSetSensorCommands(const char* upper, char* cmd);
bool handleWiFiCommandsHandler(const char* upper, char* cmd);
bool handleCalibrationCommands(const char* upper, char* cmd);
void sendResponse(const char* msg);
void sendError(const char* msg);
void sendBluetoothResponse(const char* msg);
void sendWifiResponse(const char* msg);
void sendSensorData();
void wifiStateMachine();
void connectToWifi();
void startTcpServer();
void checkWifiHealth();
void attemptWifiReconnect();
void wifiMaintenance();
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
bool waitForResponse(const char* expected, unsigned long timeout);


void setup() {
    // Start Serial for debugging
    Serial.begin(9600);

    // Initialize Bluetooth (HC-05/06 on Serial1)
    initBluetooth();

    // Start with display since it uses SPI, not I2C
    if(!display.begin(SSD1306_SWITCHCAPVCC)) {
        for(;;);  // Don't proceed if screen fails
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
      
    // Initialize RTC
    rtc.Begin();

        // Initialize software I2C for SCD41 sensor
    softWire.begin();
    
    // Initialize SCD41 sensor
    Serial.println("Initializing SCD41 sensor...");
    
    // Wake up sensor
    wakeUpSensor(softWire);
    delay(20); // Wait for sensor to wake up
    
    // Start periodic measurements
    startPeriodicMeasurement(softWire);
    delay(5000); // Wait for first measurement
    
    Serial.println("SCD41 sensor initialized");

    // Initial SD card check
    sdCardPresent = sd.begin(SD_CS_PIN);
    lastSDCheckTime = millis(); // Initialize the last SD check time
    
    if (!sdCardPresent) {
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("SD Card Error!");
        display.display();
        delay(2000);
        
        // If SD card is not present, disable data logging
        isLogging = false;
        EEPROM.write(LOG_STATUS_ADDR, 0);
        Serial.println("SD Card not detected - Data Logging disabled");
    } else {
        Serial.println("SD Card detected at startup");
        
        // Check if log file exists, if not, create it with headers
        if (!fileExists("MFC-LOG.TXT")) {
            if (logFile.open("MFC-LOG.TXT", O_WRONLY | O_CREAT)) {
                logFile.println("Date,Time,HumidityMax,HumidityMin,Humidity,HumidityRelay,CoolMax,CoolMin,HeatMax,HeatMin,Temperature,Unit,Mode,HeatRelay,FruitMax,FruitMin,PinMax,PinMin,CO2Mode,CO2,FanRelay,CO2Delay,LightsOn,LightsOff,LightRelay");
                logFile.close();
                Serial.println("Created log file with headers");
            }
        }
    }
    
    // Initialize WiFi (ESP-01S on Serial2)
    initWifi();

    // Set pin modes
    pinMode(JOY_BTN, INPUT_PULLUP);
    pinMode(HUMIDITY_RELAY_PIN, OUTPUT);
    pinMode(HEAT_RELAY_PIN, OUTPUT);
    pinMode(CO2_RELAY_PIN, OUTPUT);
    pinMode(LIGHT_RELAY_PIN, OUTPUT);
    
    // Initialize relay states to OFF
    digitalWrite(HUMIDITY_RELAY_PIN, HIGH);
    digitalWrite(HEAT_RELAY_PIN, HIGH);
    digitalWrite(CO2_RELAY_PIN, HIGH);
    digitalWrite(LIGHT_RELAY_PIN, HIGH);

    // Check if EEPROM needs initialization
    uint16_t magic;
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);
    bool needsInit = (magic != EEPROM_MAGIC);

    if (needsInit) {
        Serial.println("Initializing EEPROM with default values");
        
        // Write magic number
        EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
        
        // Set temperature unit default (Fahrenheit)
        EEPROM.write(TEMP_UNIT_ADDR, 1);
        useFahrenheit = true;
        
        // Set temperature mode default (Heat)
        EEPROM.write(TEMP_MODE_ADDR, TEMP_MODE_DEFAULT);
        tempMode = TEMP_MODE_DEFAULT;
        
        // Set humidity defaults
        uint16_t humidityMax = 85;  // 85%
        uint16_t humidityMin = 80;  // 80%
        EEPROM.put(HUMIDITY_MAX_ADDR, humidityMax);
        EEPROM.put(HUMIDITY_MIN_ADDR, humidityMin);
        values[0][0] = humidityMax;
        values[0][1] = humidityMin;
        
        // Set CO2 defaults (renamed to Fruit)
        uint16_t co2FruitMax = 2000;  // 2000 ppm
        uint16_t co2FruitMin = 1500;  // 1500 ppm
        EEPROM.put(CO2_FRUIT_MAX_ADDR, co2FruitMax);
        EEPROM.put(CO2_FRUIT_MIN_ADDR, co2FruitMin);
        values[2][0] = co2FruitMax;
        values[2][1] = co2FruitMin;
        
        // Set CO2 Pin defaults
        EEPROM.put(CO2_PIN_MAX_ADDR, CO2_PIN_DEFAULT_MAX);
        EEPROM.put(CO2_PIN_MIN_ADDR, CO2_PIN_DEFAULT_MIN);
        co2PinMax = CO2_PIN_DEFAULT_MAX;
        co2PinMin = CO2_PIN_DEFAULT_MIN;
        
        // Set CO2 Mode default (Fruit)
        EEPROM.write(CO2_MODE_ADDR, CO2_MODE_DEFAULT);
        co2Mode = CO2_MODE_DEFAULT;
        
        // Set CO2 delay defaults (all 0)
        EEPROM.put(CO2_DELAY_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_DELAY_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(CO2_DELAY_MINUTES_ADDR, (uint32_t)0);
        EEPROM.put(CO2_DELAY_SECONDS_ADDR, (uint32_t)0);
        co2DelayInterval[0] = 0;  // days
        co2DelayInterval[1] = 0;  // hours
        co2DelayInterval[2] = 0;  // minutes
        co2DelayInterval[3] = 0;  // seconds
        
        // Set temperature defaults
        EEPROM.write(TEMP_F_MAX_ADDR, 90);  // Cooling max 90°F
        EEPROM.write(TEMP_F_MIN_ADDR, 85);  // Cooling min 85°F
        EEPROM.write(TEMP_F_H_MAX_ADDR, 75);  // Heating max 75°F
        EEPROM.write(TEMP_F_H_MIN_ADDR, 70);  // Heating min 70°F
        
        // Calculate and save Celsius equivalents
        EEPROM.write(TEMP_C_MAX_ADDR, (uint8_t)constrain((int)round(fahrenheitToCelsius(90)), 0, 99));
        EEPROM.write(TEMP_C_MIN_ADDR, (uint8_t)constrain((int)round(fahrenheitToCelsius(85)), 0, 99));
        EEPROM.write(TEMP_H_MAX_ADDR, (uint8_t)constrain((int)round(fahrenheitToCelsius(75)), 0, 99));
        EEPROM.write(TEMP_H_MIN_ADDR, (uint8_t)constrain((int)round(fahrenheitToCelsius(70)), 0, 99));
        
        // Load temperature values into memory
        tempValuesF[0] = 90;
        tempValuesF[1] = 85;
        tempValuesF[2] = 75;
        tempValuesF[3] = 70;
        for(int i = 0; i < 4; i++) {
            tempValues[i] = round(fahrenheitToCelsius(tempValuesF[i]));
        }
        
        // Set light timer defaults
        uint16_t lightOn = 8 * 60;   // 8:00
        uint16_t lightOff = 20 * 60;  // 20:00
        EEPROM.put(LIGHT_ON_ADDR, lightOn);
        EEPROM.put(LIGHT_OFF_ADDR, lightOff);
        values[3][0] = lightOn;
        values[3][1] = lightOff;
        
        // Set logging interval defaults
        EEPROM.put(LOG_DAYS_ADDR, (uint32_t)0);
        EEPROM.put(LOG_HOURS_ADDR, (uint32_t)0);
        EEPROM.put(LOG_MINUTES_ADDR, (uint32_t)1);  // Default to 1 minute
        EEPROM.put(LOG_SECONDS_ADDR, (uint32_t)0);
        logInterval[0] = 0;  // days
        logInterval[1] = 0;  // hours
        logInterval[2] = 1;  // minutes
        logInterval[3] = 0;  // seconds
        
        // Set logging status default (disabled)
        EEPROM.write(LOG_STATUS_ADDR, 0);
        isLogging = false;

        // Set calibration offset defaults (0.0)
        EEPROM.put(CAL_TEMP_ADDR, (float)0.0);
        EEPROM.put(CAL_HUM_ADDR,  (float)0.0);
        EEPROM.put(CAL_CO2_ADDR,  (float)0.0);
        calTempOffset = 0.0;
        calHumOffset  = 0.0;
        calCO2Offset  = 0.0;

        // Set WiFi defaults
        EEPROM.write(WIFI_ENABLED_ADDR, 0);
        wifiEnabled = false;
        wifiPort = 8266;
        EEPROM.put(WIFI_PORT_ADDR, wifiPort);

        Serial.println("EEPROM initialized with defaults");
    } else {
        // Load all values from EEPROM
        
        // Load temperature unit setting
        useFahrenheit = (EEPROM.read(TEMP_UNIT_ADDR) == 1);
        
        // Load temperature mode setting
        tempMode = (EEPROM.read(TEMP_MODE_ADDR) == 1);
        
        // Load humidity values
        uint16_t humidityMax, humidityMin;
        EEPROM.get(HUMIDITY_MAX_ADDR, humidityMax);
        EEPROM.get(HUMIDITY_MIN_ADDR, humidityMin);
        values[0][0] = humidityMax;
        values[0][1] = humidityMin;
        
        // Load CO2 values (now renamed to Fruit)
        uint16_t co2FruitMax, co2FruitMin;
        EEPROM.get(CO2_FRUIT_MAX_ADDR, co2FruitMax);
        EEPROM.get(CO2_FRUIT_MIN_ADDR, co2FruitMin);
        values[2][0] = co2FruitMax;
        values[2][1] = co2FruitMin;
        
        // Load CO2 Pin values
        EEPROM.get(CO2_PIN_MAX_ADDR, co2PinMax);
        EEPROM.get(CO2_PIN_MIN_ADDR, co2PinMin);
        
        // Load CO2 Mode setting
        co2Mode = (EEPROM.read(CO2_MODE_ADDR) == 1);
        
        // Load CO2 delay interval values
        uint32_t intervalValue;
        EEPROM.get(CO2_DELAY_DAYS_ADDR, intervalValue);
        co2DelayInterval[0] = intervalValue;
        EEPROM.get(CO2_DELAY_HOURS_ADDR, intervalValue);
        co2DelayInterval[1] = intervalValue;
        EEPROM.get(CO2_DELAY_MINUTES_ADDR, intervalValue);
        co2DelayInterval[2] = intervalValue;
        EEPROM.get(CO2_DELAY_SECONDS_ADDR, intervalValue);
        co2DelayInterval[3] = intervalValue;
        
        // Load temperature values
        tempValues[0] = EEPROM.read(TEMP_C_MAX_ADDR);
        tempValues[1] = EEPROM.read(TEMP_C_MIN_ADDR);
        tempValues[2] = EEPROM.read(TEMP_H_MAX_ADDR);
        tempValues[3] = EEPROM.read(TEMP_H_MIN_ADDR);
        
        tempValuesF[0] = EEPROM.read(TEMP_F_MAX_ADDR);
        tempValuesF[1] = EEPROM.read(TEMP_F_MIN_ADDR);
        tempValuesF[2] = EEPROM.read(TEMP_F_H_MAX_ADDR);
        tempValuesF[3] = EEPROM.read(TEMP_F_H_MIN_ADDR);
        
        // Load light timer values
        uint16_t lightOn, lightOff;
        EEPROM.get(LIGHT_ON_ADDR, lightOn);
        EEPROM.get(LIGHT_OFF_ADDR, lightOff);
        values[3][0] = lightOn;
        values[3][1] = lightOff;
        
        // Load logging interval values
        EEPROM.get(LOG_DAYS_ADDR, intervalValue);
        logInterval[0] = intervalValue;
        EEPROM.get(LOG_HOURS_ADDR, intervalValue);
        logInterval[1] = intervalValue;
        EEPROM.get(LOG_MINUTES_ADDR, intervalValue);
        logInterval[2] = intervalValue;
        EEPROM.get(LOG_SECONDS_ADDR, intervalValue);
        logInterval[3] = intervalValue;
        
        // Load logging status - CRITICAL FIX FOR POWER LOSS RECOVERY
        byte logStatus = EEPROM.read(LOG_STATUS_ADDR);
        Serial.print("Read logging status from EEPROM: ");
        Serial.println(logStatus);
        
        isLogging = (logStatus == 1);
        
        // If logging is enabled but SD card is not present, disable it
        if(isLogging && !sdCardPresent) {
            isLogging = false;
            EEPROM.write(LOG_STATUS_ADDR, 0);
            Serial.println("Data Logging disabled - No SD Card at startup");
        } 
        else if(isLogging && sdCardPresent) {
            // Data logging was enabled before power loss and SD card is present
            Serial.println("Data logging was enabled before power loss - resuming");
            
            // Force an immediate log after power is restored
            nextLogTime = 1; // Set to a very small value to trigger immediate logging
            
            // Log a power restoration event
            if (logFile.open("MFC-LOG.TXT", O_WRONLY | O_CREAT | O_APPEND)) {
                // Get current time
                RtcDateTime now = rtc.GetDateTime();
                
                // Date format
                logFile.print(now.Year());
                logFile.print("-");
                if(now.Month() < 10) logFile.print("0");
                logFile.print(now.Month());
                logFile.print("-");
                if(now.Day() < 10) logFile.print("0");
                logFile.print(now.Day());
                logFile.print(",");
                
                // Time format
                if(now.Hour() < 10) logFile.print("0");
                logFile.print(now.Hour());
                logFile.print(":");
                if(now.Minute() < 10) logFile.print("0");
                logFile.print(now.Minute());
                logFile.print(":");
                if(now.Second() < 10) logFile.print("0");
                logFile.print(now.Second());
                
                // Log a power restoration message with empty fields to maintain CSV format
                logFile.println(",POWER RESTORED,,,,,,,,,,,,,,,,,,,,,,");
                
                logFile.close();
                
                // Ensure logging status is set in EEPROM
                EEPROM.write(LOG_STATUS_ADDR, 1);
            }
        }

        // Load calibration offsets from EEPROM
        EEPROM.get(CAL_TEMP_ADDR, calTempOffset);
        EEPROM.get(CAL_HUM_ADDR,  calHumOffset);
        EEPROM.get(CAL_CO2_ADDR,  calCO2Offset);
        // Sanity-check: treat uninitialized floats as 0
        if(isnan(calTempOffset)) calTempOffset = 0.0;
        if(isnan(calHumOffset))  calHumOffset  = 0.0;
        if(isnan(calCO2Offset))  calCO2Offset  = 0.0;
    }

    // Initialize display state
    showSplash = true;
    
    // Check if there's an active CO2 delay from a previous session
    if(!co2Mode) {
        // Calculate total delay duration in milliseconds
        unsigned long delayDurationMs = (co2DelayInterval[0] * 24L * 60L * 60L * 1000L) +
                                      (co2DelayInterval[1] * 60L * 60L * 1000L) +
                                      (co2DelayInterval[2] * 60L * 1000L) +
                                      (co2DelayInterval[3] * 1000L);
        
        // Only activate if delay values are not all zero
        if(delayDurationMs > 0) {
            co2DelayActive = true;
            co2PinModeActive = true;
            co2DelayStartTime = millis();
            co2DelayDurationMs = delayDurationMs;
            Serial.println("CO2 Pin mode activated with delay from settings");
        }
    }
    
    // Print debug info
    debugAllValues();
}

// SCD41 Sensor Functions using SoftwareWire
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

bool readSensorData(SoftwareWire& wire, float& co2, float& temp, float& hum) {
  // Send read measurement command
  wire.beginTransmission(SCD41_I2C_ADDR);
  wire.write(SCD41_READ_MEASUREMENT >> 8);
  wire.write(SCD41_READ_MEASUREMENT & 0xFF);
  if (wire.endTransmission() != 0) return false;
  
  delay(1); // Wait for measurement
  
  // Read 9 bytes of data
  wire.requestFrom(SCD41_I2C_ADDR, 9);
  if (wire.available() < 9) return false;
  
  // Read CO2 (2 bytes + CRC)
  uint16_t co2_raw = (wire.read() << 8) | wire.read();
  wire.read(); // CRC
  
  // Read temperature (2 bytes + CRC)
  uint16_t temp_raw = (wire.read() << 8) | wire.read();
  wire.read(); // CRC
  
  // Read humidity (2 bytes + CRC)
  uint16_t hum_raw = (wire.read() << 8) | wire.read();
  wire.read(); // CRC
  
  // Convert raw values
  co2 = co2_raw;
  temp = -45.0 + 175.0 * temp_raw / 65536.0;
  hum = 100.0 * hum_raw / 65536.0;
  
  return true;
}

bool fileExists(const char* filename) {
  return sd.exists(filename);
}

float celsiusToFahrenheit(float celsius) {
  return (celsius * 9.0 / 5.0) + 32.0;
}

float fahrenheitToCelsius(float fahrenheit) {
  return (fahrenheit - 32.0) * 5.0 / 9.0;
}

void updateNextLogTime() {
  if(isLogging) {
    // Convert interval to milliseconds
    unsigned long intervalMs = (logInterval[0] * 24L * 60L * 60L * 1000L) +  // Days
                             (logInterval[1] * 60L * 60L * 1000L) +          // Hours
                             (logInterval[2] * 60L * 1000L) +                // Minutes
                             (logInterval[3] * 1000L);                       // Seconds
    
    // If interval is 0, set a minimum of 1 second
    if(intervalMs == 0) intervalMs = 1000;
    
    nextLogTime = millis() + intervalMs;
  }
}

void logDataToSD() {
  if(!isLogging) return;
  
  // First check if SD card is still present
  if (!sdCardPresent) {
    sdCardPresent = sd.begin(SD_CS_PIN);
    if (!sdCardPresent) {
      isLogging = false;
      EEPROM.update(LOG_STATUS_ADDR, 0);
      Serial.println("Logging stopped - SD card not found");
      return;
    }
  }
  
  // Get current time
  RtcDateTime now = rtc.GetDateTime();
  
  // Only proceed if we have valid sensor readings
  if(currentHumidity >= 0 && currentHumidity <= 100 &&
     currentTemperature >= -40 && currentTemperature <= 120 &&
     currentCO2 > 0 && currentCO2 < 10000) {
    
    // Try to open file in append mode with multiple attempts
    bool fileOpened = false;
    for(int attempts = 0; attempts < 3 && !fileOpened; attempts++) {
      fileOpened = logFile.open("MFC-LOG.TXT", O_WRONLY | O_CREAT | O_APPEND);
      if(!fileOpened) {
        delay(100); // Short delay before retry
      }
    }
    
    if(fileOpened) {
      // Format: Date,Time,HumidityMax,HumidityMin,Humidity,HumidityRelay,CoolMax,CoolMin,HeatMax,HeatMin,Temperature,Unit,Mode,HeatRelay,FruitMax,FruitMin,PinMax,PinMin,CO2Mode,CO2,FanRelay,CO2Delay,LightsOn,LightsOff,LightRelay
      
      // Date format: YYYY-MM-DD
      logFile.print(now.Year());
      logFile.print("-");
      if(now.Month() < 10) logFile.print("0");
      logFile.print(now.Month());
      logFile.print("-");
      if(now.Day() < 10) logFile.print("0");
      logFile.print(now.Day());
      logFile.print(",");
      
      // Time format: HH:MM:SS
      if(now.Hour() < 10) logFile.print("0");
      logFile.print(now.Hour());
      logFile.print(":");
      if(now.Minute() < 10) logFile.print("0");
      logFile.print(now.Minute());
      logFile.print(":");
      if(now.Second() < 10) logFile.print("0");
      logFile.print(now.Second());
      logFile.print(",");
      
      // Humidity Max and Min
      logFile.print(values[0][0]);  // Humidity Max
      logFile.print(",");
      logFile.print(values[0][1]);  // Humidity Min
      logFile.print(",");
      
      // Humidity
      logFile.print(currentHumidity, 1);  // 1 decimal place
      logFile.print(",");
      
      // Humidity Relay
      logFile.print(digitalRead(HUMIDITY_RELAY_PIN) == LOW ? "ON" : "OFF");
      logFile.print(",");
      
      // Temperature settings - use the appropriate temp values based on current unit
      if(useFahrenheit) {
        logFile.print(tempValuesF[0]);  // Cool Max
        logFile.print(",");
        logFile.print(tempValuesF[1]);  // Cool Min
        logFile.print(",");
        logFile.print(tempValuesF[2]);  // Heat Max
        logFile.print(",");
        logFile.print(tempValuesF[3]);  // Heat Min
      } else {
        logFile.print(tempValues[0]);  // Cool Max
        logFile.print(",");
        logFile.print(tempValues[1]);  // Cool Min
        logFile.print(",");
        logFile.print(tempValues[2]);  // Heat Max
        logFile.print(",");
        logFile.print(tempValues[3]);  // Heat Min
      }
      logFile.print(",");
      
      // Temperature reading (value only, without unit)
      float tempValue;
      if(useFahrenheit) {
        tempValue = celsiusToFahrenheit(currentTemperature);
      } else {
        tempValue = currentTemperature;
      }
      logFile.print(tempValue, 1);
      logFile.print(",");
      
      // Temperature Unit (separate column)
      logFile.print(useFahrenheit ? "F" : "C");
      logFile.print(",");
      
      // Temperature Mode
      logFile.print(tempMode ? "Heat" : "Cool");
      logFile.print(",");
      
      // Heat Relay
      logFile.print(digitalRead(HEAT_RELAY_PIN) == LOW ? "ON" : "OFF");
      logFile.print(",");
      
      // CO2 Fruit Max and Min
      logFile.print(values[2][0]);  // Fruit Max
      logFile.print(",");
      logFile.print(values[2][1]);  // Fruit Min
      logFile.print(",");
      
      // CO2 Pin Max and Min
      logFile.print(co2PinMax);  // Pin Max
      logFile.print(",");
      logFile.print(co2PinMin);  // Pin Min
      logFile.print(",");
      
      // CO2 Mode
      logFile.print(co2Mode ? "Fruit" : "Pin");
      logFile.print(",");
      
      // CO2 reading
      logFile.print(currentCO2);
      logFile.print(",");
      
      // CO2/Fan Relay
      logFile.print(digitalRead(CO2_RELAY_PIN) == LOW ? "ON" : "OFF");
      logFile.print(",");
      
      // CO2 Delay
      if(co2DelayActive) {
        // Calculate remaining time (wrap-safe subtraction)
        unsigned long elapsed = millis() - co2DelayStartTime;
        unsigned long remainingMillis = (elapsed >= co2DelayDurationMs) ? 0 : (co2DelayDurationMs - elapsed);

        // Convert remaining time to days, hours, minutes, seconds
        uint32_t remainingDays = remainingMillis / (24L * 60L * 60L * 1000L);
        remainingMillis %= (24L * 60L * 60L * 1000L);
        uint32_t remainingHours = remainingMillis / (60L * 60L * 1000L);
        remainingMillis %= (60L * 60L * 1000L);
        uint32_t remainingMinutes = remainingMillis / (60L * 1000L);
        remainingMillis %= (60L * 1000L);
        uint32_t remainingSeconds = remainingMillis / 1000L;
        
        // Display remaining time
        logFile.print(remainingDays);
        logFile.print("d ");
        if(remainingHours < 10) logFile.print("0");
        logFile.print(remainingHours);
        logFile.print(":");
        if(remainingMinutes < 10) logFile.print("0");
        logFile.print(remainingMinutes);
        logFile.print(":");
        if(remainingSeconds < 10) logFile.print("0");
        logFile.print(remainingSeconds);
      } else {
        // Show configured delay values when not counting down
        logFile.print(co2DelayInterval[0]);
        logFile.print("d ");
        if(co2DelayInterval[1] < 10) logFile.print("0");
        logFile.print(co2DelayInterval[1]);
        logFile.print(":");
        if(co2DelayInterval[2] < 10) logFile.print("0");
        logFile.print(co2DelayInterval[2]);
        logFile.print(":");
        if(co2DelayInterval[3] < 10) logFile.print("0");
        logFile.print(co2DelayInterval[3]);
      }
      logFile.print(",");
      
      // Light timer settings
      String onTime = getTimeString(values[3][0]);  // Lights On time
      String offTime = getTimeString(values[3][1]); // Lights Off time
      logFile.print(onTime);
      logFile.print(",");
      logFile.print(offTime);
      logFile.print(",");
      
      // Light Relay
      logFile.print(digitalRead(LIGHT_RELAY_PIN) == LOW ? "ON" : "OFF");
      
      logFile.println();  // End the line
      
      logFile.close();  // Ensure file is properly closed
      
      // CRITICAL: Write logging status to EEPROM after successful logging
      // This ensures the status remains ON even after power loss
      EEPROM.update(LOG_STATUS_ADDR, 1);
      
      // Update timekeeping for next log
      lastLogTime = millis();
      
      // Calculate next log time based on interval
      unsigned long intervalMs = (logInterval[0] * 24L * 60L * 60L * 1000L) +  // Days
                               (logInterval[1] * 60L * 60L * 1000L) +          // Hours
                               (logInterval[2] * 60L * 1000L) +                // Minutes
                               (logInterval[3] * 1000L);                       // Seconds
      
      // If interval is 0, set a minimum of 1 second
      if(intervalMs == 0) intervalMs = 1000;
      
      // Set next log time
      nextLogTime = millis() + intervalMs;
      
      Serial.print("Log successful, next log in ");
      Serial.print(intervalMs / 1000);
      Serial.println(" seconds");
    } else {
      // If we still failed to open the file after multiple attempts
      Serial.println("Failed to open log file after multiple attempts");
      
      // Retry SD card check
      sdCardPresent = sd.begin(SD_CS_PIN);
      
      if(!sdCardPresent) {
        // SD card was removed
        isLogging = false;
        EEPROM.update(LOG_STATUS_ADDR, 0);
        Serial.println("Data Logging disabled - SD Card removed");
      } else {
        // SD card is present but file couldn't be opened, try again shortly
        nextLogTime = millis() + 5000; // Try again in 5 seconds
      }
    }
  } else {
    // If sensors aren't ready yet, try again in a few seconds
    Serial.println("Sensor readings not valid, will retry logging");
    nextLogTime = millis() + 5000; // Try again in 5 seconds
  }
}

void updateRelays() {
    // Humidity Relay Control
    if (currentHumidity >= 0 && currentHumidity <= 100) {
        if (currentHumidity < values[0][1]) {  // Below min
            digitalWrite(HUMIDITY_RELAY_PIN, LOW);  // Turn ON humidifier
        }
        else if (currentHumidity >= values[0][0]) {  // Above max
            digitalWrite(HUMIDITY_RELAY_PIN, HIGH);  // Turn OFF humidifier
        }
    }
    
    // Heat/Cool Relay Control based on Mode
    if (currentTemperature >= -40 && currentTemperature <= 120) {
        float compareTemp;
        if(useFahrenheit) {
            compareTemp = celsiusToFahrenheit(currentTemperature);
            if(tempMode) {
                // Heat mode logic
                if (compareTemp < tempValuesF[3]) {      // Below Heat Min (F)
                    digitalWrite(HEAT_RELAY_PIN, LOW);    // Turn ON heat
                }
                else if (compareTemp >= tempValuesF[2]) { // Above Heat Max (F)
                    digitalWrite(HEAT_RELAY_PIN, HIGH);   // Turn OFF heat
                }
            } else {
                // Cool mode logic
                if (compareTemp > tempValuesF[0]) {      // Above Cool Max (F)
                    digitalWrite(HEAT_RELAY_PIN, LOW);    // Turn ON cooling
                }
                else if (compareTemp <= tempValuesF[1]) { // Below Cool Min (F)
                    digitalWrite(HEAT_RELAY_PIN, HIGH);   // Turn OFF cooling
                }
            }
        } else {
            if(tempMode) {
                // Heat mode logic
                if (currentTemperature < tempValues[3]) {      // Below Heat Min (C)
                    digitalWrite(HEAT_RELAY_PIN, LOW);          // Turn ON heat
                }
                else if (currentTemperature >= tempValues[2]) { // Above Heat Max (C)
                    digitalWrite(HEAT_RELAY_PIN, HIGH);         // Turn OFF heat
                }
            } else {
                // Cool mode logic
                if (currentTemperature > tempValues[0]) {      // Above Cool Max (C)
                    digitalWrite(HEAT_RELAY_PIN, LOW);          // Turn ON cooling
                }
                else if (currentTemperature <= tempValues[1]) { // Below Cool Min (C)
                    digitalWrite(HEAT_RELAY_PIN, HIGH);         // Turn OFF cooling
                }
            }
        }
    }
    
    // CO2 Relay Control - based on current Mode
    if (currentCO2 > 0 && currentCO2 < 10000) {
        uint16_t co2Max, co2Min;
        
        if(co2Mode) {  // Fruit mode
            co2Max = values[2][0];  // Fruit-Max
            co2Min = values[2][1];  // Fruit-Min
        } else {  // Pin mode
            co2Max = co2PinMax;
            co2Min = co2PinMin;
            
            // If Pin mode is active, check if we need to switch back to Fruit mode
            if(!co2PinModeActive) {
                // First time in Pin mode, start the timer
                co2PinModeActive = true;
                
                // Calculate delay in milliseconds
                unsigned long delayTimeMs = (co2DelayInterval[0] * 24L * 60L * 60L * 1000L) +  // Days
                                          (co2DelayInterval[1] * 60L * 60L * 1000L) +          // Hours
                                          (co2DelayInterval[2] * 60L * 1000L) +                // Minutes
                                          (co2DelayInterval[3] * 1000L);                       // Seconds
                
                // If delay is > 0, activate countdown
                if(delayTimeMs > 0) {
                    co2DelayActive = true;
                    co2DelayStartTime = millis();
                    co2DelayDurationMs = delayTimeMs;
                    Serial.println("CO2 Pin mode activated - starting delay timer");
                }
            } else if(co2DelayActive) {
                // Check if countdown has finished (wrap-safe subtraction)
                if(millis() - co2DelayStartTime >= co2DelayDurationMs) {
                    // Time to switch back to Fruit mode
                    co2Mode = true;
                    co2PinModeActive = false;
                    co2DelayActive = false;
                    EEPROM.write(CO2_MODE_ADDR, 1);  // Save change to EEPROM
                    Serial.println("CO2 mode auto-switched from Pin to Fruit after delay");
                }
            }
        }
        
        if (currentCO2 > co2Max) {         // Above max
            digitalWrite(CO2_RELAY_PIN, LOW);    // Turn ON exhaust fan
        }
        else if (currentCO2 <= co2Min) {   // Below min
            digitalWrite(CO2_RELAY_PIN, HIGH);   // Turn OFF exhaust fan
        }
    }
    
    // Light Timer Relay Control
    RtcDateTime now = rtc.GetDateTime();
    int currentMinutes = now.Hour() * 60 + now.Minute();
    int onTime = values[3][0];    // Using saved values from global array
    int offTime = values[3][1];
    
    // Check if On and Off times are the same
    if (onTime == offTime) {
        // If times are identical, keep lights OFF
        digitalWrite(LIGHT_RELAY_PIN, HIGH); // Turn OFF lights
        return;
    }
    else if (onTime < offTime) {  // Normal case (e.g., 8:00 to 20:00)
        if (currentMinutes >= onTime && currentMinutes < offTime) {
            digitalWrite(LIGHT_RELAY_PIN, LOW);  // Turn ON lights
        } else {
            digitalWrite(LIGHT_RELAY_PIN, HIGH); // Turn OFF lights
        }
    } else {  // Overnight case (e.g., 20:00 to 8:00)
        if (currentMinutes >= onTime || currentMinutes < offTime) {
            digitalWrite(LIGHT_RELAY_PIN, LOW);  // Turn ON lights
        } else {
            digitalWrite(LIGHT_RELAY_PIN, HIGH); // Turn OFF lights
        }
    }
}

void readSensors() {
  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
    // Read data from SCD41 sensor using software I2C
    bool valid = readSensorData(softWire, currentCO2, currentTemperature, currentHumidity);
    
    if (valid) {
      // Apply calibration offsets
      currentCO2         += calCO2Offset;
      currentTemperature += calTempOffset;
      currentHumidity    += calHumOffset;

      // Clamp to plausible ranges
      if (currentCO2 < 0) currentCO2 = 0;
      if (currentCO2 > 9999) currentCO2 = 9999;
      if (currentTemperature < -40.0f) currentTemperature = -40.0f;
      if (currentTemperature > 85.0f) currentTemperature = 85.0f;
      if (currentHumidity < 0.0f) currentHumidity = 0.0f;
      if (currentHumidity > 100.0f) currentHumidity = 100.0f;

      Serial.print("SCD41: ");
      Serial.print("CO2="); Serial.print((uint16_t)currentCO2);
      Serial.print(" T="); Serial.print(currentTemperature, 1);
      Serial.print(" H="); Serial.print(currentHumidity, 1);
      Serial.println();
    } else {
      // Set invalid readings
      currentCO2 = 0;
      currentTemperature = -999;
      currentHumidity = -999;
      Serial.println("SCD41 sensor read failed");
    }
    
    lastSensorRead = millis();
  }
}

void showSplashScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  
  // Display Title
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
  
  // Display sensor readings
  if(currentHumidity >= 0 && currentHumidity <= 100) {
    display.print("Humidity: ");
    display.print((int)currentHumidity);
    display.println("%");
  } else {
    display.println("Humidity: ---");
  }
  
  // Update temperature display with units and mode
  if(currentTemperature >= -40 && currentTemperature <= 120) {
    display.print("Temp: ");
    if(useFahrenheit) {
      int tempF = round(celsiusToFahrenheit(currentTemperature));
      display.print(tempF);
      display.print("F ");
    } else {
      display.print((int)currentTemperature);
      display.print("C ");
    }
    // Add Temperature Mode
    display.print(tempMode ? "Heat" : "Cool");
  } else {
    display.println("Temp: ---");
  }
  display.println();

  if(currentCO2 > 0 && currentCO2 < 10000) {
    display.print("CO2: ");
    display.print((uint16_t)currentCO2);
    display.print("ppm ");
    // Add CO2 Mode
    display.print(co2Mode ? "Fruit" : "Pin");
  } else {
    display.println("CO2: ---");
  }
  display.println();
  
  // Display data logging status message if logging is enabled
  if(isLogging) {
    display.println("Logging Data");
  }
  
  display.println();
  display.println("Menu - press button");
  
  display.display();
}

void printDigits(int digits) {
  display.print("0");
  display.print(digits);
}

void drawMenuItem(const char* text, bool selected) {
  if(selected) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    // display.print("> ");
    display.println(text);
    display.setTextColor(SSD1306_WHITE);
  } else {
    // display.print("  ");
    display.println(text);
  }
}

String getTimeString(int value) {
  int hours = value / 60;
  int minutes = value % 60;
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", hours, minutes);
  return String(timeStr);
}

void printDateTime(const RtcDateTime& dt) {
  char datestring[20];
  snprintf_P(datestring, 
          sizeof(datestring),
          PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
          dt.Month(),
          dt.Day(),
          dt.Year(),
          dt.Hour(),
          dt.Minute(),
          dt.Second() );
  Serial.println(datestring);
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
        display.println(menuItems[currentMenu]);
        display.println();
        
        if(currentMenu == 1) {  // Temperature menu
            const char* tempItems[] = {"Cool Max", "Cool Min", "Heat Max", "Heat Min", "Mode", "Units"};
            for(int i = 0; i < TEMP_SUB_ITEMS; i++) {
                if(i == subMenu) {
                    display.setTextColor(BLACK, WHITE);
                    // display.print("> ");
                } else {
                    display.setTextColor(WHITE);
                    // display.print("  ");
                }
                
                if(i == 5) {  // Units selection
                    display.print("Units: [");
                    if(!useFahrenheit) display.setTextColor(BLACK, WHITE);
                    display.print("C");
                    display.setTextColor(WHITE);
                    display.print("] [");
                    if(useFahrenheit) display.setTextColor(BLACK, WHITE);
                    display.print("F");
                    display.setTextColor(WHITE);
                    display.println("]");
                } 
                else if(i == 4) {  // Mode selection
                    display.print("Mode: [");
                    if(tempMode) display.setTextColor(BLACK, WHITE);
                    display.print("Heat");
                    display.setTextColor(WHITE);
                    display.print("] [");
                    if(!tempMode) display.setTextColor(BLACK, WHITE);
                    display.print("Cool");
                    display.setTextColor(WHITE);
                    display.println("]");
                }
                else {  // Temperature values
                    display.print(tempItems[i]);
                    display.print(": ");
                    if(editingValue == i) {
                        if(useFahrenheit) {
                            display.print((int)round(tempValuesF[i]));  // Round to whole number for F
                            display.print("F");
                        } else {
                            display.print(tempValues[i]);
                            display.print("C");
                        }
                    } else {
                        if(useFahrenheit) {
                            display.print((int)round(tempValuesF[i]));  // Round to whole number for F
                            display.print("F");
                        } else {
                            display.print(tempValues[i]);
                            display.print("C");
                        }
                    }
                    display.println();
                }
                display.setTextColor(WHITE);
            }
        }
        else if(currentMenu == 5) {  // Date/Time menu
            const char* dateTimeItems[] = {"Date", "Time"};
            for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                if(i == subMenu) {
                    display.setTextColor(BLACK, WHITE);
                    // display.print("> ");
                } else {
                    display.setTextColor(WHITE);
                    // display.print("  ");
                }
                
                display.print(dateTimeItems[i]);
                display.print(": ");
                display.setTextColor(WHITE);
                
                if(editingValue >= 0 && i == subMenu) {  // Currently editing this item
                    if(i == 0) {  // Date editing
                        // Month
                        if(dateEditComponent == 0) display.setTextColor(BLACK, WHITE);
                        if(dateTimeTemp[0] < 10) display.print("0");
                        display.print(dateTimeTemp[0]);
                        display.setTextColor(WHITE);
                        display.print("/");
                        
                        // Day
                        if(dateEditComponent == 1) display.setTextColor(BLACK, WHITE);
                        if(dateTimeTemp[1] < 10) display.print("0");
                        display.print(dateTimeTemp[1]);
                        display.setTextColor(WHITE);
                        display.print("/");
                        
                        // Year
                        if(dateEditComponent == 2) display.setTextColor(BLACK, WHITE);
                        display.print(dateTimeTemp[2]);
                        display.setTextColor(WHITE);
                    } else {  // Time editing
                        if(dateTimeTemp[0] < 10) display.print("0");
                        display.print(dateTimeTemp[0]);
                        display.print(":");
                        if(dateTimeTemp[1] < 10) display.print("0");
                        display.print(dateTimeTemp[1]);
                    }
                } else {  // Show current RTC value
                    RtcDateTime now = rtc.GetDateTime();
                    if(i == 0) {  // Date display
                        if(now.Month() < 10) display.print("0");
                        display.print(now.Month());
                        display.print("/");
                        if(now.Day() < 10) display.print("0");
                        display.print(now.Day());
                        display.print("/");
                        display.print(now.Year());
                    } else {  // Time display
                        if(now.Hour() < 10) display.print("0");
                        display.print(now.Hour());
                        display.print(":");
                        if(now.Minute() < 10) display.print("0");
                        display.print(now.Minute());
                    }
                }
                display.println();
            }
            
            if(editingValue >= 0) {
                display.println();
                if(subMenu == 0) {  // Date
                    display.println("L/R: Select M/D/Y");
                    display.println("Up/Down to change");
                } else {  // Time
                    display.println("Up/Down: Hours");
                    display.println("Left/Right: Minutes");
                }
                display.println("Button when done");
            } else {
                display.println("\nButton to edit");
                display.println("Left to go back");
            }
        }
        else if(currentMenu == 4) {  // Data Logging menu
            const char* logItems[] = {"Interval", "Logging"};
            
            // Only check SD card every 30 seconds
            unsigned long currentTime = millis();
            if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                sdCardPresent = sd.begin(SD_CS_PIN);
                lastSDCheckTime = currentTime;
            }
            
            for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                if(i == subMenu && (i == 0 || sdCardPresent)) {
                    display.setTextColor(BLACK, WHITE);
                } else {
                    display.setTextColor(WHITE);
                }
                
                if(i == 0) {  // Interval
                    display.print("Interval: ");
                    if(editingValue >= 0) {
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
                } else {  // Logging status
                    if(!sdCardPresent) {
                        // Display the disabled message when no SD card
                        display.print("Data Logging Disabled");
                        display.println();
                        display.print("No SD Card");
                        
                        // Also make sure logging is turned off
                        if(isLogging) {
                            isLogging = false;
                            EEPROM.update(LOG_STATUS_ADDR, 0);
                        }
                    } else {
                        // Normal display when SD card is present
                        display.print("Logging: ");
                        if(editingValue >= 0) {
                            display.print("[");
                            if(!isLogging) display.setTextColor(BLACK, WHITE);
                            display.print("OFF");
                            display.setTextColor(WHITE);
                            display.print("][");
                            if(isLogging) display.setTextColor(BLACK, WHITE);
                            display.print("ON");
                            display.setTextColor(WHITE);
                            display.print("]");
                        } else {
                            display.print(isLogging ? "ON" : "OFF");
                        }
                    }
                }
                display.println();
                display.setTextColor(WHITE);
            }
        }
        else if(currentMenu == 3) {  // Lights menu
            for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                if(i == subMenu) {
                    display.setTextColor(BLACK, WHITE);
                    // display.print("> ");
                } else {
                    display.setTextColor(WHITE);
                    // display.print("  ");
                }
                
                display.print(i == 0 ? "On: " : "Off: ");
                int value = editingValue == i ? tempValues[i] : values[currentMenu][i];
                display.print(getTimeString(value));
                display.println();
                display.setTextColor(WHITE);
            }
        }
        else if(currentMenu == 2) {  // CO2 menu
            const char* co2Items[] = {"Fruit-Max", "Fruit-Min", "Pin-Max", "Pin-Min", "Delay", "Mode"};
            for(int i = 0; i < CO2_SUB_ITEMS; i++) {
                if(i == subMenu) {
                    display.setTextColor(BLACK, WHITE);
                } else {
                    display.setTextColor(WHITE);
                }
                
                if(i == 5) {  // Mode selection
                    display.print("Mode: [");
                    if(co2Mode) display.setTextColor(BLACK, WHITE);
                    display.print("Fruit");
                    display.setTextColor(WHITE);
                    display.print("] [");
                    if(!co2Mode) display.setTextColor(BLACK, WHITE);
                    display.print("Pin");
                    display.setTextColor(WHITE);
                    display.println("]");
                }
                else if(i == 4) {  // Delay
                    display.print("Delay: ");
                    if(editingValue >= 0 && i == subMenu) {
                        // When editing, show the values being edited
                        if(co2DelayEditComponent == 0) display.setTextColor(BLACK, WHITE);
                        display.print(co2DelayInterval[0]);
                        display.setTextColor(WHITE);
                        display.print("d ");
                        
                        if(co2DelayEditComponent == 1) display.setTextColor(BLACK, WHITE);
                        if(co2DelayInterval[1] < 10) display.print("0");
                        display.print(co2DelayInterval[1]);
                        display.setTextColor(WHITE);
                        display.print(":");
                        
                        if(co2DelayEditComponent == 2) display.setTextColor(BLACK, WHITE);
                        if(co2DelayInterval[2] < 10) display.print("0");
                        display.print(co2DelayInterval[2]);
                        display.setTextColor(WHITE);
                        display.print(":");
                        
                        if(co2DelayEditComponent == 3) display.setTextColor(BLACK, WHITE);
                        if(co2DelayInterval[3] < 10) display.print("0");
                        display.print(co2DelayInterval[3]);
                        display.setTextColor(WHITE);
                    } else if(co2DelayActive && !co2Mode) {
                        // Show countdown timer when active and in Pin mode
                        
                        // Calculate remaining time (wrap-safe subtraction)
                        unsigned long elapsed = millis() - co2DelayStartTime;
                        unsigned long remainingMillis = (elapsed >= co2DelayDurationMs) ? 0 : (co2DelayDurationMs - elapsed);
                        
                        // Convert remaining time to days, hours, minutes, seconds
                        uint32_t remainingDays = remainingMillis / (24L * 60L * 60L * 1000L);
                        remainingMillis %= (24L * 60L * 60L * 1000L);
                        uint32_t remainingHours = remainingMillis / (60L * 60L * 1000L);
                        remainingMillis %= (60L * 60L * 1000L);
                        uint32_t remainingMinutes = remainingMillis / (60L * 1000L);
                        remainingMillis %= (60L * 1000L);
                        uint32_t remainingSeconds = remainingMillis / 1000L;
                        
                        // Display remaining time
                        display.print(remainingDays);
                        display.print("d ");
                        if(remainingHours < 10) display.print("0");
                        display.print(remainingHours);
                        display.print(":");
                        if(remainingMinutes < 10) display.print("0");
                        display.print(remainingMinutes);
                        display.print(":");
                        if(remainingSeconds < 10) display.print("0");
                        display.print(remainingSeconds);
                    } else {
                        // Show configured delay values when not counting down
                        display.print(co2DelayInterval[0]);
                        display.print("d ");
                        if(co2DelayInterval[1] < 10) display.print("0");
                        display.print(co2DelayInterval[1]);
                        display.print(":");
                        if(co2DelayInterval[2] < 10) display.print("0");
                        display.print(co2DelayInterval[2]);
                        display.print(":");
                        if(co2DelayInterval[3] < 10) display.print("0");
                        display.print(co2DelayInterval[3]);
                    }
                    display.println();
                }
                else {  // CO2 values
                    display.print(co2Items[i]);
                    display.print(": ");
                    
                    if(editingValue == i) {
                        int value = tempValues[i];
                        display.print(value);
                        display.print(" ppm");
                    } else {
                        int value;
                        if(i < 2) {  // Fruit values
                            value = values[2][i];
                        } else if(i < 4) {  // Pin values
                            if(i == 2) value = co2PinMax;
                            else value = co2PinMin;
                        }
                        display.print(value);
                        display.print(" ppm");
                    }
                    display.println();
                }
                display.setTextColor(WHITE);
            }
        }
        else {  // Humidity menu
            for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                if(i == subMenu) {
                    display.setTextColor(BLACK, WHITE);
                    // display.print("> ");
                } else {
                    display.setTextColor(WHITE);
                    // display.print("  ");
                }
                
                display.print(i == 0 ? "Max: " : "Min: ");
                int value = editingValue == i ? tempValues[i] : values[currentMenu][i];
                
                if(currentMenu == 2) {  // CO2
                    display.print(value);
                    display.print(" ppm");
                } else {  // Humidity
                    display.print(value);
                    display.print("%");
                }
                display.println();
                display.setTextColor(WHITE);
            }
        }
        
        if(editingValue >= 0) {
            display.println();
            if(currentMenu == 4) {  // Data Logging menu
                if(subMenu == 0) {  // Interval
                    display.println("L/R: Select D/H/M/S");
                } else {  // Logging
                    display.println("L/R to toggle");
                    
                    // If SD card not present, show warning
                    if(!sdCardPresent) {
                        display.println("No SD Card!");
                    }
                }
            } else if(currentMenu == 3) {  // Lights
                display.println("Up/Down: Hours");
                display.println("Left/Right: Minutes");
            } else if(currentMenu == 2) {  // CO2
                if(subMenu == 4) {  // Delay
                    display.println("L/R: Select D/H/M/S");
                } else if(subMenu == 5) {  // Mode
                    display.println("L/R to toggle");
                } else {  // CO2 values
                    display.println("Up/Down: +/-100");
                    display.println("Left/Right: +/-1");
                }
            } else if(currentMenu == 1 && (subMenu == 4 || subMenu == 5)) {  // Temperature Units or Mode
                display.println("Left/Right to change");
            } else {
                display.println("Up/Down to change");
            }
            display.println("Button when done");
        } else {
            display.println("\nButton to edit");
            display.println("Left to go back");
        }
    }
    else {  // Main menu
        display.println("Main Menu - v55-3");
        display.println();
        
        for(int i = 0; i < NUM_MENU_ITEMS; i++) {
            if(i == currentMenu) {
                display.setTextColor(BLACK, WHITE);
            } else {
                display.setTextColor(WHITE);
            }
            // if(i == currentMenu) display.print("> ");
            // else display.print("  ");
            display.println(menuItems[i]);
            display.setTextColor(WHITE);
        }
        display.println("\nLeft: Return to splash");
    }
    
    display.display();
}

void handleInput() {
    int x = analogRead(JOY_X);
    int y = analogRead(JOY_Y);
    bool buttonRead = !digitalRead(JOY_BTN);
    bool button = false;
    
    if(buttonRead && (millis() - lastButtonPress) > DEBOUNCE_DELAY) {
        button = true;
        lastButtonPress = millis();
    }
    
    if(showSplash) {
        if(button) {
            showSplash = false;
        }
        return;
    }
    
    if(savePrompt) {
        if(x < 200) {
            Serial.println("Save cancelled");
            savePrompt = false;
            // Don't reset subMenu, only editingValue
            editingValue = -1;
        }
        else if(x > 800) {
            Serial.println("Save initiated");
            savePrompt = false;
            
            if(currentMenu == 5) {  // Date/Time menu
                RtcDateTime prevTime = rtc.GetDateTime();
                
                if(subMenu == 0) {  // Date was being edited
                    RtcDateTime newDate = RtcDateTime(
                        dateTimeTemp[2],           // Year
                        dateTimeTemp[0],           // Month
                        dateTimeTemp[1],           // Day
                        prevTime.Hour(),           // Keep existing hour
                        prevTime.Minute(),         // Keep existing minute
                        prevTime.Second()          // Keep existing second
                    );
                    rtc.SetDateTime(newDate);
                    Serial.println("Date updated successfully");
                } else if(subMenu == 1) {  // Time was being edited
                    RtcDateTime newTime = RtcDateTime(
                        prevTime.Year(),           // Keep existing year
                        prevTime.Month(),          // Keep existing month
                        prevTime.Day(),            // Keep existing day
                        dateTimeTemp[0],           // Hours
                        dateTimeTemp[1],           // Minutes
                        0                          // Reset seconds to 0
                    );
                    rtc.SetDateTime(newTime);
                    Serial.println("Time updated successfully");
                }
            }
            else if(currentMenu == 4) {  // Data Logging menu
                if(subMenu == 0) { // Interval settings
                    // Save interval values to their specific addresses
                    EEPROM.put(LOG_DAYS_ADDR, logInterval[0]);
                    EEPROM.put(LOG_HOURS_ADDR, logInterval[1]);
                    EEPROM.put(LOG_MINUTES_ADDR, logInterval[2]);
                    EEPROM.put(LOG_SECONDS_ADDR, logInterval[3]);
                    
                    Serial.println("Saved logging interval values:");
                    Serial.print(logInterval[0]); Serial.print("d ");
                    Serial.print(logInterval[1]); Serial.print("h ");
                    Serial.print(logInterval[2]); Serial.print("m ");
                    Serial.print(logInterval[3]); Serial.println("s");
                }
                else if(subMenu == 1) { // Logging enabled/disabled
                    EEPROM.update(LOG_STATUS_ADDR, isLogging ? 1 : 0);
                    Serial.print("Saved logging status: ");
                    Serial.println(isLogging ? "Enabled" : "Disabled");
                    
                    if(isLogging) {
                        updateNextLogTime();
                    }
                }
            }
            else if(currentMenu == 1) {  // Temperature menu
                if(subMenu < 4) {  // Temperature values
                    // Save both Celsius and Fahrenheit values
                    EEPROM.write(TEMP_C_MAX_ADDR, (uint8_t)constrain(tempValues[0], 0, 99));
                    EEPROM.write(TEMP_C_MIN_ADDR, (uint8_t)constrain(tempValues[1], 0, 99));
                    EEPROM.write(TEMP_H_MAX_ADDR, (uint8_t)constrain(tempValues[2], 0, 99));
                    EEPROM.write(TEMP_H_MIN_ADDR, (uint8_t)constrain(tempValues[3], 0, 99));
                    
                    EEPROM.write(TEMP_F_MAX_ADDR, tempValuesF[0]);
                    EEPROM.write(TEMP_F_MIN_ADDR, tempValuesF[1]);
                    EEPROM.write(TEMP_F_H_MAX_ADDR, tempValuesF[2]);
                    EEPROM.write(TEMP_F_H_MIN_ADDR, tempValuesF[3]);
                    
                    Serial.println("Saved temperature values");
                }
            }
            else if(currentMenu == 0) {  // Humidity menu
                EEPROM.put(HUMIDITY_MAX_ADDR, (uint16_t)tempValues[0]);
                EEPROM.put(HUMIDITY_MIN_ADDR, (uint16_t)tempValues[1]);
                values[0][0] = tempValues[0];
                values[0][1] = tempValues[1];
                
                Serial.print("Saved Humidity - Max: ");
                Serial.print(tempValues[0]);
                Serial.print("%, Min: ");
                Serial.print(tempValues[1]);
                Serial.println("%");
            }
            else if(currentMenu == 2) {  // CO2 menu
                if(subMenu == 4) {  // Delay settings
                    saveCO2DelaySettings();
                }
                else if(subMenu == 5) {  // Mode already saved during toggle
                    Serial.print("CO2 Mode set to: ");
                    Serial.println(co2Mode ? "Fruit" : "Pin");
                    
                    // If mode changed to Pin, initialize the delay timer
                    if(!co2Mode) {
                        co2PinModeActive = false;  // This will trigger timer init in updateRelays
                    }
                }
                else if(subMenu < 2) {  // Fruit values
                    EEPROM.put(CO2_FRUIT_MAX_ADDR, (uint16_t)tempValues[0]);
                    EEPROM.put(CO2_FRUIT_MIN_ADDR, (uint16_t)tempValues[1]);
                    values[2][0] = tempValues[0];
                    values[2][1] = tempValues[1];
                    
                    Serial.print("Saved CO2 Fruit - Max: ");
                    Serial.print(tempValues[0]);
                    Serial.print(" ppm, Min: ");
                    Serial.print(tempValues[1]);
                    Serial.println(" ppm");
                }
                else if(subMenu < 4) {  // Pin values
                    if(subMenu == 2) {  // Pin-Max
                        EEPROM.put(CO2_PIN_MAX_ADDR, (uint16_t)tempValues[2]);
                        co2PinMax = tempValues[2];
                        
                        Serial.print("Saved CO2 Pin - Max: ");
                        Serial.print(tempValues[2]);
                        Serial.println(" ppm");
                    } else {  // Pin-Min
                        EEPROM.put(CO2_PIN_MIN_ADDR, (uint16_t)tempValues[3]);
                        co2PinMin = tempValues[3];
                        
                        Serial.print("Saved CO2 Pin - Min: ");
                        Serial.print(tempValues[3]);
                        Serial.println(" ppm");
                    }
                }
            }
            else if(currentMenu == 3) {  // Lights menu
                EEPROM.put(LIGHT_ON_ADDR, (uint16_t)tempValues[0]);
                EEPROM.put(LIGHT_OFF_ADDR, (uint16_t)tempValues[1]);
                values[3][0] = tempValues[0];
                values[3][1] = tempValues[1];
                
                Serial.print("Saved Light Timer - On: ");
                Serial.print(getTimeString(tempValues[0]));
                Serial.print(", Off: ");
                Serial.println(getTimeString(tempValues[1]));
            }
            
            // Only reset editingValue, keep subMenu
            editingValue = -1;
        }
        return;
    }
    
    if(editingValue >= 0) {
        if(currentMenu == 3) {  // Lights menu
            int hours = tempValues[editingValue] / 60;
            int minutes = tempValues[editingValue] % 60;
            
            // Up/down controls hours
            if(y < 200) {
                hours = (hours + 1) % 24;
            }
            else if(y > 800) {
                hours = (hours - 1 + 24) % 24;
            }
            
            // Left/right controls minutes
            if(x < 200) {
                minutes = (minutes - 1 + 60) % 60;
            }
            else if(x > 800) {
                minutes = (minutes + 1) % 60;
            }
            
            tempValues[editingValue] = (hours * 60) + minutes;
        }
        else if(currentMenu == 2) {  // CO2 menu
            if(subMenu == 5) {  // Mode selection
                if(x < 200 || x > 800) {
                    co2Mode = !co2Mode;
                    EEPROM.write(CO2_MODE_ADDR, co2Mode ? 1 : 0);
                    
                    // Reset Pin mode timer if switching to Pin mode
                    if(!co2Mode) {
                        co2PinModeActive = false;
                    }
                    
                    delay(200);
                }
            }
            else if(subMenu == 4) {  // Delay editing
                // Up/down adjusts the selected component
                if(y < 200 || y > 800) {
                    if(co2DelayEditComponent == 0) {  // Days (0-365)
                        if(y < 200 && co2DelayInterval[0] < 365) {
                            co2DelayInterval[0]++;
                        } else if(y > 800 && co2DelayInterval[0] > 0) {
                            co2DelayInterval[0]--;
                        }
                    }
                    else if(co2DelayEditComponent == 1) {  // Hours (0-23)
                        if(y < 200) {
                            co2DelayInterval[1] = (co2DelayInterval[1] + 1) % 24;
                        } else if(y > 800) {
                            co2DelayInterval[1] = (co2DelayInterval[1] + 23) % 24;
                        }
                    }
                    else if(co2DelayEditComponent == 2) {  // Minutes (0-59)
                        if(y < 200) {
                            co2DelayInterval[2] = (co2DelayInterval[2] + 1) % 60;
                        } else if(y > 800) {
                            co2DelayInterval[2] = (co2DelayInterval[2] + 59) % 60;
                        }
                    }
                    else if(co2DelayEditComponent == 3) {  // Seconds (0-59)
                        if(y < 200) {
                            co2DelayInterval[3] = (co2DelayInterval[3] + 1) % 60;
                        } else if(y > 800) {
                            co2DelayInterval[3] = (co2DelayInterval[3] + 59) % 60;
                        }
                    }
                }
                
                // Left/right switches between components
                if(x < 200) {
                    co2DelayEditComponent = (co2DelayEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    co2DelayEditComponent = (co2DelayEditComponent + 1) % 4;
                }
            }
            else {  // CO2 value editing (Fruit-Max, Fruit-Min, Pin-Max, Pin-Min)
                // Store old value before changing
                int oldValue = tempValues[subMenu];
                
                // Fine adjustment with left/right
                if(x < 200) {
                    tempValues[subMenu] = max(0, tempValues[subMenu] - 1);
                }
                else if(x > 800) {
                    tempValues[subMenu] = min(CO2_MAX_RANGE, tempValues[subMenu] + 1);
                }
                
                // Coarse adjustment with up/down (100 ppm increments)
                if(y < 200) {
                    tempValues[subMenu] = min(CO2_MAX_RANGE, tempValues[subMenu] + 100);
                }
                else if(y > 800) {
                    tempValues[subMenu] = max(0, tempValues[subMenu] - 100);
                }
                
                // Enforce constraints between min and max values for CO2
                if(subMenu == 0) {  // Fruit-Max - ensure it's >= Fruit-Min
                    if(tempValues[0] < tempValues[1]) {
                        tempValues[0] = tempValues[1];  // Set Max equal to Min
                    }
                }
                else if(subMenu == 1) {  // Fruit-Min - ensure it's <= Fruit-Max
                    if(tempValues[1] > tempValues[0]) {
                        tempValues[1] = tempValues[0];  // Set Min equal to Max
                    }
                }
                else if(subMenu == 2) {  // Pin-Max - ensure it's >= Pin-Min
                    if(tempValues[2] < tempValues[3]) {
                        tempValues[2] = tempValues[3];  // Set Max equal to Min
                    }
                }
                else if(subMenu == 3) {  // Pin-Min - ensure it's <= Pin-Max
                    if(tempValues[3] > tempValues[2]) {
                        tempValues[3] = tempValues[2];  // Set Min equal to Max
                    }
                }
            }
        }
        else if(currentMenu == 4) {  // Data Logging menu
            if(subMenu == 0) {  // Interval editing
                // Up/down adjusts the selected component
                if(y < 200 || y > 800) {
                    if(intervalEditComponent == 0) {  // Days
                        if(y < 200 && logInterval[0] < 999) {
                            logInterval[0]++;
                        } else if(y > 800 && logInterval[0] > 0) {
                            logInterval[0]--;
                        }
                    }
                    else if(intervalEditComponent == 1) {  // Hours
                        if(y < 200) {
                            logInterval[1] = (logInterval[1] + 1) % 24;
                        } else if(y > 800) {
                            logInterval[1] = (logInterval[1] + 23) % 24;
                        }
                    }
                    else if(intervalEditComponent == 2) {  // Minutes
                        if(y < 200) {
                            logInterval[2] = (logInterval[2] + 1) % 60;
                        } else if(y > 800) {
                            logInterval[2] = (logInterval[2] + 59) % 60;
                        }
                    }
                    else if(intervalEditComponent == 3) {  // Seconds
                        if(y < 200) {
                            logInterval[3] = (logInterval[3] + 1) % 60;
                        } else if(y > 800) {
                            logInterval[3] = (logInterval[3] + 59) % 60;
                        }
                    }
                }
                
                // Left/right switches between components
                if(x < 200) {
                    intervalEditComponent = (intervalEditComponent + 3) % 4;
                }
                else if(x > 800) {
                    intervalEditComponent = (intervalEditComponent + 1) % 4;
                }
            }
            else if(subMenu == 1) {  // Logging status
                // Check SD card presence
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                    
                    // If SD card status changed from not present to present, update UI
                    if(sdCardPresent) {
                        Serial.println("SD Card detected");
                    } else {
                        Serial.println("No SD Card detected");
                        // Turn off logging if SD card was removed
                        if(isLogging) {
                            isLogging = false;
                            EEPROM.update(LOG_STATUS_ADDR, 0);
                            Serial.println("Data Logging disabled - No SD Card");
                        }
                    }
                }
                
                // Only allow toggling if SD card is present
                if(sdCardPresent && editingValue >= 0) {
                    if(x < 200 || x > 800) {
                        isLogging = !isLogging;
                        delay(200);
                    }
                }
            }
        }
        else if(currentMenu == 5) {  // Date/Time menu
            if(subMenu == 0) {  // Date editing
                // Up/down adjusts the selected component
                if(y < 200 || y > 800) {
                    if(dateEditComponent == 0) {  // Month
                        if(y < 200) {
                            dateTimeTemp[0] = dateTimeTemp[0] % 12 + 1;
                        } else {
                            dateTimeTemp[0] = (dateTimeTemp[0] + 10) % 12 + 1;
                        }
                    }
                    else if(dateEditComponent == 1) {  // Day
                        if(y < 200) {
                            dateTimeTemp[1] = dateTimeTemp[1] % 31 + 1;
                        } else {
                            dateTimeTemp[1] = (dateTimeTemp[1] + 29) % 31 + 1;
                        }
                    }
                    else if(dateEditComponent == 2) {  // Year
                        if(y < 200) {
                            dateTimeTemp[2] = constrain(dateTimeTemp[2] + 1, YEAR_MIN, YEAR_MAX);
                        } else {
                            dateTimeTemp[2] = constrain(dateTimeTemp[2] - 1, YEAR_MIN, YEAR_MAX);
                        }
                    }
                }
                
                // Left/right switches between month/day/year
                if(x < 200) {
                    dateEditComponent = (dateEditComponent + 2) % 3;
                }
                else if(x > 800) {
                    dateEditComponent = (dateEditComponent + 1) % 3;
                }
            }
            else if(subMenu == 1) {  // Time editing
                // Up/down controls hours
                if(y < 200) {
                    dateTimeTemp[0] = (dateTimeTemp[0] + 1) % 24;
                }
                else if(y > 800) {
                    dateTimeTemp[0] = (dateTimeTemp[0] + 23) % 24;
                }
                
                // Left/right controls minutes
                if(x < 200) {
                    dateTimeTemp[1] = (dateTimeTemp[1] + 59) % 60;
                }
                else if(x > 800) {
                    dateTimeTemp[1] = (dateTimeTemp[1] + 1) % 60;
                }
            }
        }
        else if(currentMenu == 1) {  // Temperature menu
            if(editingValue == 5) {  // Units selection
                if(x < 200 || x > 800) {
                    useFahrenheit = !useFahrenheit;
                    EEPROM.write(TEMP_UNIT_ADDR, useFahrenheit ? 1 : 0);
                    delay(200);
                }
            } 
            else if(editingValue == 4) {  // Mode selection
                if(x < 200 || x > 800) {
                    tempMode = !tempMode;
                    EEPROM.write(TEMP_MODE_ADDR, tempMode ? 1 : 0);
                    delay(200);
                }
            }
            else {  // Temperature value editing
                if(y < 200 || y > 800) {
                    if(useFahrenheit) {
                        int currentVal = tempValuesF[editingValue];
                        
                        if(y < 200) {
                            tempValuesF[editingValue] = currentVal + 1;
                        } else if(y > 800) {
                            tempValuesF[editingValue] = currentVal - 1;
                        }
                        
                        // Enforce constraints for temperature values
                        if(editingValue == 0) {  // Cool-Max - ensure it's >= Cool-Min
                            if(tempValuesF[0] < tempValuesF[1]) {
                                tempValuesF[0] = tempValuesF[1];  // Set Max equal to Min
                            }
                        } 
                        else if(editingValue == 1) {  // Cool-Min - ensure it's <= Cool-Max
                            if(tempValuesF[1] > tempValuesF[0]) {
                                tempValuesF[1] = tempValuesF[0];  // Set Min equal to Max
                            }
                        }
                        else if(editingValue == 2) {  // Heat-Max - ensure it's >= Heat-Min
                            if(tempValuesF[2] < tempValuesF[3]) {
                                tempValuesF[2] = tempValuesF[3];  // Set Max equal to Min
                            }
                        }
                        else if(editingValue == 3) {  // Heat-Min - ensure it's <= Heat-Max
                            if(tempValuesF[3] > tempValuesF[2]) {
                                tempValuesF[3] = tempValuesF[2];  // Set Min equal to Max
                            }
                        }
                        
                        // Update Celsius values
                        tempValues[editingValue] = round(fahrenheitToCelsius(tempValuesF[editingValue]));
                    } else {  // Celsius mode
                        int currentVal = tempValues[editingValue];
                        
                        if(y < 200) {
                            tempValues[editingValue] = currentVal + 1;
                        } else if(y > 800) {
                            tempValues[editingValue] = currentVal - 1;
                        }
                        
                        // Enforce constraints for temperature values
                        if(editingValue == 0) {  // Cool-Max - ensure it's >= Cool-Min
                            if(tempValues[0] < tempValues[1]) {
                                tempValues[0] = tempValues[1];  // Set Max equal to Min
                            }
                        } 
                        else if(editingValue == 1) {  // Cool-Min - ensure it's <= Cool-Max
                            if(tempValues[1] > tempValues[0]) {
                                tempValues[1] = tempValues[0];  // Set Min equal to Max
                            }
                        }
                        else if(editingValue == 2) {  // Heat-Max - ensure it's >= Heat-Min
                            if(tempValues[2] < tempValues[3]) {
                                tempValues[2] = tempValues[3];  // Set Max equal to Min
                            }
                        }
                        else if(editingValue == 3) {  // Heat-Min - ensure it's <= Heat-Max
                            if(tempValues[3] > tempValues[2]) {
                                tempValues[3] = tempValues[2];  // Set Min equal to Max
                            }
                        }
                        
                        // Update Fahrenheit values
                        tempValuesF[editingValue] = round(celsiusToFahrenheit(tempValues[editingValue]));
                    }
                }
            }
        }
        else if(currentMenu == 0) {  // Humidity menu
            // Store old value before changing
            int oldValue = tempValues[editingValue];
            
            if(y < 200) tempValues[editingValue]++;
            else if(y > 800) tempValues[editingValue]--;
            
            // Constrain values for humidity (0-100%)
            tempValues[editingValue] = constrain(tempValues[editingValue], 0, 100);
            
            // Enforce constraints for humidity values
            if(editingValue == 0) {  // Max - ensure it's >= Min
                if(tempValues[0] < tempValues[1]) {
                    tempValues[0] = tempValues[1];  // Set Max equal to Min
                }
            }
            else if(editingValue == 1) {  // Min - ensure it's <= Max
                if(tempValues[1] > tempValues[0]) {
                    tempValues[1] = tempValues[0];  // Set Min equal to Max
                }
            }
        }
        
        if(button) {
            if((currentMenu == 1 && (editingValue == 4 || editingValue == 5)) || 
               (currentMenu == 4 && subMenu == 1) ||
               (currentMenu == 2 && subMenu == 5)) {
                editingValue = -1;  // Just exit edit mode for toggles
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
            dateEditComponent = 0;  // Reset date edit component when exiting
            intervalEditComponent = 0;  // Reset interval edit component when exiting
            co2DelayEditComponent = 0;  // Reset CO2 delay edit component when exiting
        }
        else if(button) {
            // For Data Logging menu, check SD card status
            if(currentMenu == 4) {
                // Check SD card every 30 seconds
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                }
                
                // Only allow editing logging status if SD card is present
                if(subMenu == 1 && !sdCardPresent) {
                    // Don't allow editing if no SD card present
                    return;
                }
            }
            
            editingValue = subMenu;
            
            if(currentMenu == 5) {  // Date/Time menu
                RtcDateTime now = rtc.GetDateTime();
                if(subMenu == 0) {  // Initialize date editing
                    dateTimeTemp[0] = now.Month();    // Month
                    dateTimeTemp[1] = now.Day();      // Day
                    dateTimeTemp[2] = now.Year();     // Year
                    dateEditComponent = 0;            // Start with month selected
                } else {  // Initialize time editing
                    dateTimeTemp[0] = now.Hour();     // Hours
                    dateTimeTemp[1] = now.Minute();   // Minutes
                    dateTimeTemp[2] = 0;              // Clear year to indicate time editing
                }
            }
            else if(currentMenu == 1) {  // Temperature menu
                if(subMenu < 4) {  // Temperature values
                    // Values are already loaded since we keep both C and F values in memory
                }
            }
            else if(currentMenu == 4) {  // Data Logging menu
                if(subMenu == 0) {  // Initialize interval editing
                    intervalEditComponent = 0;  // Start with days selected
                }
            }
            else if(currentMenu == 2) {  // CO2 menu
                if(subMenu < 4) {  // CO2 value settings
                    // Always initialize ALL four CO2 values at once to ensure constraints work
                    tempValues[0] = values[2][0];  // Fruit-Max
                    tempValues[1] = values[2][1];  // Fruit-Min
                    tempValues[2] = co2PinMax;     // Pin-Max
                    tempValues[3] = co2PinMin;     // Pin-Min
                    
                    // Enforce constraints for CO2 values at initialization
                    if(tempValues[0] < tempValues[1]) {
                        // Ensure Fruit-Max >= Fruit-Min
                        tempValues[0] = tempValues[1];
                    }
                    if(tempValues[2] < tempValues[3]) {
                        // Ensure Pin-Max >= Pin-Min
                        tempValues[2] = tempValues[3];
                    }
                }
                else if(subMenu == 4) {  // CO2 Delay
                    co2DelayEditComponent = 0;  // Start with days selected
                }
                // No else needed for Mode as it's toggled directly
            }
            else if(currentMenu == 0) {  // Humidity menu
                // Initialize both values to ensure constraints work
                tempValues[0] = values[0][0];  // Max
                tempValues[1] = values[0][1];  // Min
                
                // Enforce constraint for Humidity values at initialization
                if(tempValues[0] < tempValues[1]) {
                    // Ensure Max >= Min
                    tempValues[0] = tempValues[1];
                }
            }
            else {  // Other menus
                for(int i = 0; i < NUM_SUB_ITEMS; i++) {
                    tempValues[i] = values[currentMenu][i];
                }
            }
        }
        else if(y < 200 || y > 800) {
            // Handle submenu navigation
            if(currentMenu == 4) {  // Data Logging menu
                // Check SD card every 30 seconds
                unsigned long currentTime = millis();
                if(currentTime - lastSDCheckTime >= SD_CHECK_INTERVAL) {
                    sdCardPresent = sd.begin(SD_CS_PIN);
                    lastSDCheckTime = currentTime;
                }
                
                // If there's no SD card, don't allow navigating to the logging submenu
                if(!sdCardPresent) {
                    // Keep on interval if trying to navigate away
                    subMenu = 0;
                } else {
                    // Normal navigation when SD card is present
                    if(y < 200) {
                        subMenu = (subMenu - 1 + NUM_SUB_ITEMS) % NUM_SUB_ITEMS;
                    } else if(y > 800) {
                        subMenu = (subMenu + 1) % NUM_SUB_ITEMS;
                    }
                }
            } else if(currentMenu == 1) {  // Temperature menu
                // Normal navigation for Temperature menu
                if(y < 200) {
                    subMenu = (subMenu - 1 + TEMP_SUB_ITEMS) % TEMP_SUB_ITEMS;
                } else if(y > 800) {
                    subMenu = (subMenu + 1) % TEMP_SUB_ITEMS;
                }
            } else if(currentMenu == 2) {  // CO2 menu
                // Navigation for CO2 menu
                if(y < 200) {
                    subMenu = (subMenu - 1 + CO2_SUB_ITEMS) % CO2_SUB_ITEMS;
                } else if(y > 800) {
                    subMenu = (subMenu + 1) % CO2_SUB_ITEMS;
                }
            } else {  // Normal navigation for other menus
                if(y < 200) {
                    subMenu = (subMenu - 1 + NUM_SUB_ITEMS) % NUM_SUB_ITEMS;
                } else if(y > 800) {
                    subMenu = (subMenu + 1) % NUM_SUB_ITEMS;
                }
            }
        }
    }
    else {  // Main menu navigation
        if(x < 200) showSplash = true;
        if(y < 200) currentMenu--;
        else if(y > 800) currentMenu++;
        if(button) {
            Serial.print("Entering subMenu for menu ");
            Serial.println(currentMenu);
            subMenu = 0;
        }
        
        currentMenu = constrain(currentMenu, 0, NUM_MENU_ITEMS - 1);
    }
}

void saveCO2DelaySettings() {
    // Save CO2 delay interval values
    EEPROM.put(CO2_DELAY_DAYS_ADDR, co2DelayInterval[0]);
    EEPROM.put(CO2_DELAY_HOURS_ADDR, co2DelayInterval[1]);
    EEPROM.put(CO2_DELAY_MINUTES_ADDR, co2DelayInterval[2]);
    EEPROM.put(CO2_DELAY_SECONDS_ADDR, co2DelayInterval[3]);
    
    Serial.println("Saved CO2 delay interval values:");
    Serial.print(co2DelayInterval[0]); Serial.print("d ");
    Serial.print(co2DelayInterval[1]); Serial.print("h ");
    Serial.print(co2DelayInterval[2]); Serial.print("m ");
    Serial.print(co2DelayInterval[3]); Serial.println("s");
    
    // If delay timer is active and we're in Pin mode, reset the timer with new duration
    if(co2DelayActive && !co2Mode) {
        unsigned long delayTimeMs = (co2DelayInterval[0] * 24L * 60L * 60L * 1000L) +  // Days
                                  (co2DelayInterval[1] * 60L * 60L * 1000L) +          // Hours
                                  (co2DelayInterval[2] * 60L * 1000L) +                // Minutes
                                  (co2DelayInterval[3] * 1000L);                       // Seconds

        // Reset the timer
        co2DelayStartTime = millis();
        co2DelayDurationMs = delayTimeMs;
    }
}

void dumpEEPROMValues() {
    Serial.println("Current EEPROM values:");
    
    // Log interval values
    for(int i = 0; i < 4; i++) {
        uint32_t storedValue;
        switch(i) {
            case 0: EEPROM.get(LOG_DAYS_ADDR, storedValue); break;
            case 1: EEPROM.get(LOG_HOURS_ADDR, storedValue); break;
            case 2: EEPROM.get(LOG_MINUTES_ADDR, storedValue); break;
            case 3: EEPROM.get(LOG_SECONDS_ADDR, storedValue); break;
        }
        Serial.print("logInterval[");
        Serial.print(i);
        Serial.print("] = ");
        Serial.println(storedValue);
    }
    
    byte logStatus = EEPROM.read(LOG_STATUS_ADDR);
    Serial.print("isLogging = ");
    Serial.println(logStatus);
}

void printEEPROMMap() {
    Serial.println("\nEEPROM Address Map:");
    Serial.println("Magic Number: 0-1");
    Serial.println("Temperature Unit: 2");
    Serial.println("Humidity Max: 3-4");
    Serial.println("Humidity Min: 5-6");
    Serial.println("CO2 Fruit Max: 7-8");
    Serial.println("CO2 Fruit Min: 9-10");
    Serial.println("Temperature Celsius: 11-14");
    Serial.println("Temperature Fahrenheit: 15-18");
    Serial.println("Light On: 19-20");
    Serial.println("Light Off: 21-22");
    Serial.println("Log Days: 23-26");
    Serial.println("Log Hours: 27-30");
    Serial.println("Log Minutes: 31-34");
    Serial.println("Log Seconds: 35-38");
    Serial.println("Log Status: 39");
    Serial.println("Temperature Mode: 40");
    Serial.println("CO2 Pin Max: 41-42");
    Serial.println("CO2 Pin Min: 43-44");
    Serial.println("CO2 Mode: 45");
    Serial.println("CO2 Delay Days: 46-49");
    Serial.println("CO2 Delay Hours: 50-53");
    Serial.println("CO2 Delay Minutes: 54-57");
    Serial.println("CO2 Delay Seconds: 58-61");
    Serial.println();
}

void debugCO2Values() {
    uint16_t magic, co2FruitMax, co2FruitMin, co2PinMax, co2PinMin;
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);
    EEPROM.get(CO2_FRUIT_MAX_ADDR, co2FruitMax);
    EEPROM.get(CO2_FRUIT_MIN_ADDR, co2FruitMin);
    EEPROM.get(CO2_PIN_MAX_ADDR, co2PinMax);
    EEPROM.get(CO2_PIN_MIN_ADDR, co2PinMin);
    bool co2ModeVal = (EEPROM.read(CO2_MODE_ADDR) == 1);
    
    Serial.println("EEPROM CO2 Status:");
    Serial.print("Magic Number: 0x"); Serial.println(magic, HEX);
    Serial.print("CO2 Mode: "); Serial.println(co2ModeVal ? "Fruit" : "Pin");
    Serial.print("CO2 Fruit Max: "); Serial.println(co2FruitMax);
    Serial.print("CO2 Fruit Min: "); Serial.println(co2FruitMin);
    Serial.print("CO2 Pin Max: "); Serial.println(co2PinMax);
    Serial.print("CO2 Pin Min: "); Serial.println(co2PinMin);
    
    // Print CO2 Delay values
    uint32_t days, hours, minutes, seconds;
    EEPROM.get(CO2_DELAY_DAYS_ADDR, days);
    EEPROM.get(CO2_DELAY_HOURS_ADDR, hours);
    EEPROM.get(CO2_DELAY_MINUTES_ADDR, minutes);
    EEPROM.get(CO2_DELAY_SECONDS_ADDR, seconds);
    Serial.print("CO2 Delay: ");
    Serial.print(days); Serial.print("d ");
    Serial.print(hours); Serial.print("h ");
    Serial.print(minutes); Serial.print("m ");
    Serial.print(seconds); Serial.println("s");
}

void debugSensorValues() {
    Serial.println("\nCurrent Values:");
    
    // Temperature unit
    Serial.print("Temperature Unit: ");
    Serial.println(useFahrenheit ? "Fahrenheit" : "Celsius");
    
    // Temperature mode
    Serial.print("Temperature Mode: ");
    Serial.println(tempMode ? "Heat" : "Cool");
    
    // Humidity values
    uint16_t humidityMax, humidityMin;
    EEPROM.get(HUMIDITY_MAX_ADDR, humidityMax);
    EEPROM.get(HUMIDITY_MIN_ADDR, humidityMin);
    Serial.print("Humidity Max: "); Serial.print(humidityMax); Serial.println("%");
    Serial.print("Humidity Min: "); Serial.print(humidityMin); Serial.println("%");
    
    // CO2 values
    uint16_t co2FruitMax, co2FruitMin;
    EEPROM.get(CO2_FRUIT_MAX_ADDR, co2FruitMax);
    EEPROM.get(CO2_FRUIT_MIN_ADDR, co2FruitMin);
    Serial.print("CO2 Fruit Max: "); Serial.print(co2FruitMax); Serial.println(" ppm");
    Serial.print("CO2 Fruit Min: "); Serial.print(co2FruitMin); Serial.println(" ppm");
    
    uint16_t co2PinMax, co2PinMin;
    EEPROM.get(CO2_PIN_MAX_ADDR, co2PinMax);
    EEPROM.get(CO2_PIN_MIN_ADDR, co2PinMin);
    Serial.print("CO2 Pin Max: "); Serial.print(co2PinMax); Serial.println(" ppm");
    Serial.print("CO2 Pin Min: "); Serial.print(co2PinMin); Serial.println(" ppm");
    
    bool co2ModeVal = (EEPROM.read(CO2_MODE_ADDR) == 1);
    Serial.print("CO2 Mode: "); Serial.println(co2ModeVal ? "Fruit" : "Pin");
    
    // Light timer values
    uint16_t lightOn, lightOff;
    EEPROM.get(LIGHT_ON_ADDR, lightOn);
    EEPROM.get(LIGHT_OFF_ADDR, lightOff);
    Serial.print("Lights On: "); Serial.println(getTimeString(lightOn));
    Serial.print("Lights Off: "); Serial.println(getTimeString(lightOff));
    
    // Temperature values
    Serial.println("\nTemperature Values:");
    Serial.print("Cool Max: ");
    if(useFahrenheit) {
        Serial.print(EEPROM.read(TEMP_F_MAX_ADDR));
        Serial.println("F");
    } else {
        Serial.print(EEPROM.read(TEMP_C_MAX_ADDR));
        Serial.println("C");
    }
    
    Serial.print("Cool Min: ");
    if(useFahrenheit) {
        Serial.print(EEPROM.read(TEMP_F_MIN_ADDR));
        Serial.println("F");
    } else {
        Serial.print(EEPROM.read(TEMP_C_MIN_ADDR));
        Serial.println("C");
    }
    
    Serial.print("Heat Max: ");
    if(useFahrenheit) {
        Serial.print(EEPROM.read(TEMP_F_H_MAX_ADDR));
        Serial.println("F");
    } else {
        Serial.print(EEPROM.read(TEMP_H_MAX_ADDR));
        Serial.println("C");
    }
    
    Serial.print("Heat Min: ");
    if(useFahrenheit) {
        Serial.print(EEPROM.read(TEMP_F_H_MIN_ADDR));
        Serial.println("F");
    } else {
        Serial.print(EEPROM.read(TEMP_H_MIN_ADDR));
        Serial.println("C");
    }
    
    // Logging values
    Serial.println("\nLogging Settings:");
    uint32_t interval;
    EEPROM.get(LOG_DAYS_ADDR, interval);
    Serial.print(interval); Serial.print("d ");
    EEPROM.get(LOG_HOURS_ADDR, interval);
    Serial.print(interval); Serial.print("h ");
    EEPROM.get(LOG_MINUTES_ADDR, interval);
    Serial.print(interval); Serial.print("m ");
    EEPROM.get(LOG_SECONDS_ADDR, interval);
    Serial.print(interval); Serial.println("s");
    
    Serial.print("Logging Enabled: ");
    Serial.println(EEPROM.read(LOG_STATUS_ADDR) ? "Yes" : "No");
    Serial.println();
}

void debugAllValues() {
    Serial.println("\nEEPROM Values:");
    
    // Temperature unit
    bool tempUnit = EEPROM.read(TEMP_UNIT_ADDR);
    Serial.print("Temperature Unit: "); Serial.println(tempUnit ? "F" : "C");
    
    // Temperature mode
    bool tempModeVal = EEPROM.read(TEMP_MODE_ADDR);
    Serial.print("Temperature Mode: "); Serial.println(tempModeVal ? "Heat" : "Cool");
    
    // CO2 mode
    bool co2ModeVal = EEPROM.read(CO2_MODE_ADDR);
    Serial.print("CO2 Mode: "); Serial.println(co2ModeVal ? "Fruit" : "Pin");
    
    // Humidity values
    uint16_t humidityMax, humidityMin;
    EEPROM.get(HUMIDITY_MAX_ADDR, humidityMax);
    EEPROM.get(HUMIDITY_MIN_ADDR, humidityMin);
    Serial.print("Humidity Max: "); Serial.println(humidityMax);
    Serial.print("Humidity Min: "); Serial.println(humidityMin);
    
    // CO2 values
    uint16_t co2FruitMax, co2FruitMin;
    EEPROM.get(CO2_FRUIT_MAX_ADDR, co2FruitMax);
    EEPROM.get(CO2_FRUIT_MIN_ADDR, co2FruitMin);
    Serial.print("CO2 Fruit Max: "); Serial.println(co2FruitMax);
    Serial.print("CO2 Fruit Min: "); Serial.println(co2FruitMin);
    
    uint16_t co2PinMax, co2PinMin;
    EEPROM.get(CO2_PIN_MAX_ADDR, co2PinMax);
    EEPROM.get(CO2_PIN_MIN_ADDR, co2PinMin);
    Serial.print("CO2 Pin Max: "); Serial.println(co2PinMax);
    Serial.print("CO2 Pin Min: "); Serial.println(co2PinMin);
    
    // CO2 Delay values
    uint32_t days, hours, minutes, seconds;
    EEPROM.get(CO2_DELAY_DAYS_ADDR, days);
    EEPROM.get(CO2_DELAY_HOURS_ADDR, hours);
    EEPROM.get(CO2_DELAY_MINUTES_ADDR, minutes);
    EEPROM.get(CO2_DELAY_SECONDS_ADDR, seconds);
    Serial.print("CO2 Delay: ");
    Serial.print(days); Serial.print("d ");
    Serial.print(hours); Serial.print("h ");
    Serial.print(minutes); Serial.print("m ");
    Serial.print(seconds); Serial.println("s");
    
    // Temperature values
    Serial.println("\nTemperature Values:");
    Serial.print("Celsius Cool Max: "); Serial.println(EEPROM.read(TEMP_C_MAX_ADDR));
    Serial.print("Celsius Cool Min: "); Serial.println(EEPROM.read(TEMP_C_MIN_ADDR));
    Serial.print("Celsius Heat Max: "); Serial.println(EEPROM.read(TEMP_H_MAX_ADDR));
    Serial.print("Celsius Heat Min: "); Serial.println(EEPROM.read(TEMP_H_MIN_ADDR));
    Serial.print("Fahrenheit Cool Max: "); Serial.println(EEPROM.read(TEMP_F_MAX_ADDR));
    Serial.print("Fahrenheit Cool Min: "); Serial.println(EEPROM.read(TEMP_F_MIN_ADDR));
    Serial.print("Fahrenheit Heat Max: "); Serial.println(EEPROM.read(TEMP_F_H_MAX_ADDR));
    Serial.print("Fahrenheit Heat Min: "); Serial.println(EEPROM.read(TEMP_F_H_MIN_ADDR));
    
    // Light timer values
    uint16_t lightOn, lightOff;
    EEPROM.get(LIGHT_ON_ADDR, lightOn);
    EEPROM.get(LIGHT_OFF_ADDR, lightOff);
    Serial.print("\nLight Timer On: "); Serial.println(getTimeString(lightOn));
    Serial.print("Light Timer Off: "); Serial.println(getTimeString(lightOff));
    
    // Logging values
    Serial.println("\nLogging Values:");
    EEPROM.get(LOG_DAYS_ADDR, days);
    EEPROM.get(LOG_HOURS_ADDR, hours);
    EEPROM.get(LOG_MINUTES_ADDR, minutes);
    EEPROM.get(LOG_SECONDS_ADDR, seconds);
    Serial.print("Interval: ");
    Serial.print(days); Serial.print("d ");
    Serial.print(hours); Serial.print("h ");
    Serial.print(minutes); Serial.print("m ");
    Serial.print(seconds); Serial.println("s");
    
    byte logStatus = EEPROM.read(LOG_STATUS_ADDR);
    Serial.print("Logging Status: "); Serial.println(logStatus ? "Enabled" : "Disabled");
    Serial.println();
}

void loop() {
  // Bluetooth and WiFi communication
  handleBluetoothCommands();
  handleWifiCommands();
  wifiStateMachine();
  wifiMaintenance();

  // Auto-push sensor data when client connected
  if (!wifiDataPaused && (btConnected || wifiClientConnected)) {
    sendSensorData();
  }

  readSensors();
  updateRelays();
  handleInput();
  
    // Check if it's time to log data
    if (isLogging && sdCardPresent && millis() >= nextLogTime) {
        logDataToSD();
    }

  
  if(showSplash) {
    showSplashScreen();
  } else {
    updateDisplay();
  }
  
  delay(50);
}

// =============================================================================
// BLUETOOTH / WIFI FUNCTIONS
// =============================================================================

void initBluetooth() {
  BT_SERIAL.begin(BT_BAUD_RATE);
  delay(100);
  BT_SERIAL.println("SYS:READY");
}

void initWifi() {
  long baudRates[] = {9600, 57600, 38400, 115200, 74880};
  int numRates = 5;
  bool found = false;

  for (int i = 0; i < numRates && !found; i++) {
    WIFI_SERIAL.begin(baudRates[i]);
    delay(100);
    while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
    WIFI_SERIAL.println("AT");
    delay(500);
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
    }
  }

  if (!found) {
    Serial.println(F("ESP-01S not responding"));
    wifiState = WIFI_STATE_ERROR;
    return;
  }

  loadWifiCredentials();

  if (!wifiEnabled) {
    wifiState = WIFI_STATE_IDLE;
    return;
  }
  if (strlen(wifiSSID) == 0) {
    wifiState = WIFI_STATE_IDLE;
    return;
  }

  wifiState = WIFI_STATE_INITIALIZING;
  wifiStateTimeout = millis() + 5000;
  Serial.println("WiFi connecting...");
  delay(100);
  WIFI_SERIAL.println("AT+RST");
  delay(2000);
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
}

void loadWifiCredentials() {
  wifiEnabled = (EEPROM.read(WIFI_ENABLED_ADDR) == 1);
  for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
    wifiSSID[i] = EEPROM.read(WIFI_SSID_ADDR + i);
  }
  wifiSSID[WIFI_SSID_MAX_LEN - 1] = '\0';
  for (int i = 0; i < WIFI_PASS_MAX_LEN; i++) {
    wifiPassword[i] = EEPROM.read(WIFI_PASS_ADDR + i);
  }
  wifiPassword[WIFI_PASS_MAX_LEN - 1] = '\0';
  EEPROM.get(WIFI_PORT_ADDR, wifiPort);
  if (wifiPort == 0 || wifiPort == 0xFFFF) wifiPort = 8266;
}

void saveWifiCredentials() {
  EEPROM.write(WIFI_ENABLED_ADDR, wifiEnabled ? 1 : 0);
  for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, wifiSSID[i]);
  }
  for (int i = 0; i < WIFI_PASS_MAX_LEN; i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, wifiPassword[i]);
  }
  EEPROM.put(WIFI_PORT_ADDR, wifiPort);
}

// ---------------------------------------------------------------------------
// Bluetooth command reading
// ---------------------------------------------------------------------------
void handleBluetoothCommands() {
  while (BT_SERIAL.available() > 0) {
    char c = BT_SERIAL.read();
    lastBtActivityTime = millis();
    if (!btConnected) {
      btConnected = true;
      BT_SERIAL.println("SYS:CONNECTED");
    }
    if (c == '\n' || c == '\r') {
      if (btBufferIndex > 0) {
        btBuffer[btBufferIndex] = '\0';
        lastCommandChannel = CHANNEL_BLUETOOTH;
        processBluetoothCommand(btBuffer);
        btBufferIndex = 0;
      }
    } else if (btBufferIndex < BT_BUFFER_SIZE - 1) {
      btBuffer[btBufferIndex++] = c;
    } else {
      btBufferIndex = 0;
      sendError("ERR:BUFFER_OVERFLOW");
    }
  }

  if (btConnected && (millis() - lastBtActivityTime > BT_TIMEOUT)) {
    btConnected = false;
  }
}

// ---------------------------------------------------------------------------
// WiFi command reading (parses ESP-01S +IPD,<id>,<len>:<data> messages)
// ---------------------------------------------------------------------------
void handleWifiCommands() {
  if (!wifiEnabled || wifiState < WIFI_STATE_SERVER_STARTED) return;

  // Trim leading CR/LF
  while (wifiBufferIndex > 0 && (wifiBuffer[0] == '\r' || wifiBuffer[0] == '\n')) {
    memmove(wifiBuffer, wifiBuffer + 1, wifiBufferIndex);
    wifiBufferIndex--;
    wifiBuffer[wifiBufferIndex] = '\0';
  }

  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    if (wifiBufferIndex < WIFI_BUFFER_SIZE - 1) {
      wifiBuffer[wifiBufferIndex++] = c;
      wifiBuffer[wifiBufferIndex] = '\0';
    }

    // Detect client connect
    char* connectPtr = strstr(wifiBuffer, ",CONNECT");
    if (connectPtr != NULL && strstr(wifiBuffer, "+IPD") == NULL) {
      if (!wifiClientConnected) {
        wifiClientConnected = true;
        wifiState = WIFI_STATE_CLIENT_CONNECTED;
        lastWifiActivityTime = millis();
      }
      char* endPtr = strchr(connectPtr, '\n');
      if (endPtr != NULL && (endPtr + 1 - wifiBuffer) < wifiBufferIndex) {
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

    // Parse +IPD,<id>,<len>:<data>
    char* ipdPtr = strstr(wifiBuffer, "+IPD,");
    if (ipdPtr != NULL) {
      char* colon = strchr(ipdPtr, ':');
      if (colon != NULL) {
        char* comma1 = strchr(ipdPtr + 5, ',');
        if (comma1 != NULL && comma1 < colon) {
          int dataLen = atoi(comma1 + 1);
          char* dataStart = colon + 1;
          int receivedLen = wifiBufferIndex - (dataStart - wifiBuffer);
          if (receivedLen >= dataLen) {
            if (!wifiClientConnected) {
              wifiClientConnected = true;
              wifiState = WIFI_STATE_CLIENT_CONNECTED;
            }
            lastWifiActivityTime = millis();
            int cmdLen = 0;
            for (int i = 0; i < dataLen && i < (RESPONSE_BUFFER_SIZE - 1); i++) {
              char ch = dataStart[i];
              if (ch != '\r' && ch != '\n' && ch >= 32) {
                responseBuffer[cmdLen++] = ch;
              }
            }
            responseBuffer[cmdLen] = '\0';
            if (cmdLen > 0) {
              lastCommandChannel = CHANNEL_WIFI;
              processBluetoothCommand(responseBuffer);
            }
            int processedEnd = (dataStart - wifiBuffer) + dataLen;
            if (processedEnd < wifiBufferIndex) {
              int remaining = wifiBufferIndex - processedEnd;
              memmove(wifiBuffer, wifiBuffer + processedEnd, remaining);
              wifiBufferIndex = remaining;
              wifiBuffer[wifiBufferIndex] = '\0';
            } else {
              wifiBufferIndex = 0;
              wifiBuffer[0] = '\0';
            }
          }
        }
      }
    } else if (strstr(wifiBuffer, "CLOSED") != NULL) {
      wifiClientConnected = false;
      wifiDataPaused = false;
      wifiState = WIFI_STATE_SERVER_STARTED;
      wifiBufferIndex = 0;
      wifiBuffer[0] = '\0';
    } else if (wifiBufferIndex >= WIFI_BUFFER_SIZE - 1) {
      wifiBufferIndex = 0;
      wifiBuffer[0] = '\0';
    }
  }
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
void processBluetoothCommand(char* command) {
  char upper[WIFI_BUFFER_SIZE];  // 128 bytes — large enough for both BT and WiFi commands
  strncpy(upper, command, WIFI_BUFFER_SIZE - 1);
  upper[WIFI_BUFFER_SIZE - 1] = '\0';
  for (uint8_t i = 0; upper[i]; i++) upper[i] = toupper(upper[i]);

  if (!handleGetDataCommands(upper, command) &&
      !handleSetSensorCommands(upper, command) &&
      !handleWiFiCommandsHandler(upper, command) &&
      !handleCalibrationCommands(upper, command)) {
    sendError("ERR:UNKNOWN_COMMAND");
  }
}

// ---------------------------------------------------------------------------
// GET commands
// ---------------------------------------------------------------------------
bool handleGetDataCommands(const char* upper, char* command) {
  if (strcmp(upper, "GET:DATA") == 0) {
    sendSensorData();
    return true;
  }
  else if (strcmp(upper, "GET:STATUS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "STATUS:CO2_MODE=%s,LOG=%s,SD=%s",
      co2Mode ? "Fruit" : "Pin",
      isLogging ? "ON" : "OFF",
      sdCardPresent ? "OK" : "NONE"
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:RELAYS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "RELAYS:HUM=%s,HEAT=%s,CO2=%s,LIGHT=%s",
      digitalRead(HUMIDITY_RELAY_PIN) == LOW ? "ON" : "OFF",
      digitalRead(HEAT_RELAY_PIN)     == LOW ? "ON" : "OFF",
      digitalRead(CO2_RELAY_PIN)      == LOW ? "ON" : "OFF",
      digitalRead(LIGHT_RELAY_PIN)    == LOW ? "ON" : "OFF"
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:DATETIME") == 0) {
    RtcDateTime now = rtc.GetDateTime();
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "DATETIME:%04d-%02d-%02d %02d:%02d:%02d",
      now.Year(), now.Month(), now.Day(),
      now.Hour(), now.Minute(), now.Second()
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:HUM_ALL") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "HUM_ALL:MAX=%u,MIN=%u",
      values[0][0], values[0][1]
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:CO2_ALL") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "CO2_ALL:M=%s,FM=%u,Fm=%u,PM=%u,Pm=%u,DD=%u,DH=%u,DM=%u,DS=%u,DA=%u",
      co2Mode ? "F" : "P",
      values[2][0], values[2][1],
      co2PinMax, co2PinMin,
      co2DelayInterval[0], co2DelayInterval[1], co2DelayInterval[2], co2DelayInterval[3],
      co2DelayActive ? 1 : 0
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:TEMP_CONFIG") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "TEMP_CONFIG:UNIT=%s,MODE=%s,CM=%d,Cm=%d,HM=%d,Hm=%d",
      useFahrenheit ? "F" : "C",
      tempMode ? "Heat" : "Cool",
      useFahrenheit ? (int)tempValuesF[0] : tempValues[0],
      useFahrenheit ? (int)tempValuesF[1] : tempValues[1],
      useFahrenheit ? (int)tempValuesF[2] : tempValues[2],
      useFahrenheit ? (int)tempValuesF[3] : tempValues[3]
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:LOGGING_CONFIG") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "LOGGING_CONFIG:LOG=%s,D=%u,H=%u,M=%u,S=%u",
      isLogging ? "ON" : "OFF",
      logInterval[0], logInterval[1], logInterval[2], logInterval[3]
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:WIFI_CONFIG") == 0) {
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
  else if (strcmp(upper, "GET:WIFI_STATUS") == 0) {
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "WIFI_STATUS:EN=%s,CONN=%s,IP=%s,PORT=%u",
      wifiEnabled ? "Y" : "N",
      wifiConnected ? "Y" : "N",
      wifiIPAddress,
      wifiPort
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "GET:CAL_ALL") == 0) {
    char tStr[10], hStr[10], cStr[10];
    dtostrf(calTempOffset, 1, 2, tStr);
    dtostrf(calHumOffset,  1, 2, hStr);
    dtostrf(calCO2Offset,  1, 2, cStr);
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "CAL_ALL:T=%s,H=%s,CO2=%s",
      tStr, hStr, cStr
    );
    sendResponse(responseBuffer);
    return true;
  }
  else if (strcmp(upper, "PING") == 0) {
    sendResponse("PONG");
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// SET commands
// ---------------------------------------------------------------------------
bool handleSetSensorCommands(const char* upper, char* command) {
  if (strcmp(upper, "SET:PAUSE_DATA") == 0) {
    wifiDataPaused = true;
    sendResponse("OK:DATA_PAUSED");
    return true;
  }
  else if (strcmp(upper, "SET:RESUME_DATA") == 0) {
    wifiDataPaused = false;
    sendResponse("OK:DATA_RESUMED");
    return true;
  }
  // SET:HUM=<max>,<min>
  else if (strncmp(upper, "SET:HUM=", 8) == 0) {
    int maxV, minV;
    if (sscanf(command + 8, "%d,%d", &maxV, &minV) == 2) {
      values[0][0] = maxV;
      values[0][1] = minV;
      EEPROM.put(HUMIDITY_MAX_ADDR, (uint16_t)maxV);
      EEPROM.put(HUMIDITY_MIN_ADDR, (uint16_t)minV);
      sendResponse("OK:HUM");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  // SET:CO2_CFG=<mode>,<fMax>,<fMin>,<pMax>,<pMin>
  else if (strncmp(upper, "SET:CO2_CFG=", 12) == 0) {
    char modeStr[6];
    int fMax, fMin, pMax, pMin;
    if (sscanf(command + 12, "%5[^,],%d,%d,%d,%d", modeStr, &fMax, &fMin, &pMax, &pMin) == 5) {
      for (int i = 0; modeStr[i]; i++) modeStr[i] = toupper(modeStr[i]);
      co2Mode = (strcmp(modeStr, "FRUIT") == 0 || strcmp(modeStr, "F") == 0);
      values[2][0] = fMax;
      values[2][1] = fMin;
      co2PinMax = pMax;
      co2PinMin = pMin;
      EEPROM.write(CO2_MODE_ADDR, co2Mode ? 1 : 0);
      EEPROM.put(CO2_FRUIT_MAX_ADDR, (uint16_t)fMax);
      EEPROM.put(CO2_FRUIT_MIN_ADDR, (uint16_t)fMin);
      EEPROM.put(CO2_PIN_MAX_ADDR, (uint16_t)pMax);
      EEPROM.put(CO2_PIN_MIN_ADDR, (uint16_t)pMin);
      sendResponse("OK:CO2_CFG");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  // SET:CO2_DELAY=<d>,<h>,<m>,<s>
  else if (strncmp(upper, "SET:CO2_DELAY=", 14) == 0) {
    int d, h, m, s;
    if (sscanf(command + 14, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      co2DelayInterval[0] = d;
      co2DelayInterval[1] = h;
      co2DelayInterval[2] = m;
      co2DelayInterval[3] = s;
      EEPROM.put(CO2_DELAY_DAYS_ADDR,    (uint32_t)d);
      EEPROM.put(CO2_DELAY_HOURS_ADDR,   (uint32_t)h);
      EEPROM.put(CO2_DELAY_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(CO2_DELAY_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:CO2_DELAY");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  // SET:TEMP_CFG=<cMax>,<cMin>,<hMax>,<hMin>,<mode>,<unit>
  else if (strncmp(upper, "SET:TEMP_CFG=", 13) == 0) {
    float cMax, cMin, hMax, hMin;
    char modeStr[6], unitStr[2];
    if (sscanf(command + 13, "%f,%f,%f,%f,%5[^,],%1s",
               &cMax, &cMin, &hMax, &hMin, modeStr, unitStr) == 6) {
      for (int i = 0; modeStr[i]; i++) modeStr[i] = toupper(modeStr[i]);
      unitStr[0] = toupper(unitStr[0]);
      bool newFahrenheit = (unitStr[0] == 'F');
      bool newMode = (strcmp(modeStr, "HEAT") == 0);
      useFahrenheit = newFahrenheit;
      tempMode = newMode;
      EEPROM.write(TEMP_UNIT_ADDR, useFahrenheit ? 1 : 0);
      EEPROM.write(TEMP_MODE_ADDR, tempMode ? 1 : 0);
      if (useFahrenheit) {
        tempValuesF[0] = cMax; tempValuesF[1] = cMin;
        tempValuesF[2] = hMax; tempValuesF[3] = hMin;
        EEPROM.write(TEMP_F_MAX_ADDR,   (uint8_t)cMax);
        EEPROM.write(TEMP_F_MIN_ADDR,   (uint8_t)cMin);
        EEPROM.write(TEMP_F_H_MAX_ADDR, (uint8_t)hMax);
        EEPROM.write(TEMP_F_H_MIN_ADDR, (uint8_t)hMin);
        tempValues[0] = round(fahrenheitToCelsius(cMax));
        tempValues[1] = round(fahrenheitToCelsius(cMin));
        tempValues[2] = round(fahrenheitToCelsius(hMax));
        tempValues[3] = round(fahrenheitToCelsius(hMin));
        EEPROM.write(TEMP_C_MAX_ADDR, (uint8_t)constrain(tempValues[0], 0, 99));
        EEPROM.write(TEMP_C_MIN_ADDR, (uint8_t)constrain(tempValues[1], 0, 99));
        EEPROM.write(TEMP_H_MAX_ADDR, (uint8_t)constrain(tempValues[2], 0, 99));
        EEPROM.write(TEMP_H_MIN_ADDR, (uint8_t)constrain(tempValues[3], 0, 99));
      } else {
        tempValues[0] = (int)cMax; tempValues[1] = (int)cMin;
        tempValues[2] = (int)hMax; tempValues[3] = (int)hMin;
        EEPROM.write(TEMP_C_MAX_ADDR, (uint8_t)constrain(tempValues[0], 0, 99));
        EEPROM.write(TEMP_C_MIN_ADDR, (uint8_t)constrain(tempValues[1], 0, 99));
        EEPROM.write(TEMP_H_MAX_ADDR, (uint8_t)constrain(tempValues[2], 0, 99));
        EEPROM.write(TEMP_H_MIN_ADDR, (uint8_t)constrain(tempValues[3], 0, 99));
        tempValuesF[0] = celsiusToFahrenheit(cMax);
        tempValuesF[1] = celsiusToFahrenheit(cMin);
        tempValuesF[2] = celsiusToFahrenheit(hMax);
        tempValuesF[3] = celsiusToFahrenheit(hMin);
        EEPROM.write(TEMP_F_MAX_ADDR,   (uint8_t)tempValuesF[0]);
        EEPROM.write(TEMP_F_MIN_ADDR,   (uint8_t)tempValuesF[1]);
        EEPROM.write(TEMP_F_H_MAX_ADDR, (uint8_t)tempValuesF[2]);
        EEPROM.write(TEMP_F_H_MIN_ADDR, (uint8_t)tempValuesF[3]);
      }
      sendResponse("OK:TEMP_CFG");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  // SET:LOG_INTERVAL=<d>,<h>,<m>,<s>
  else if (strncmp(upper, "SET:LOG_INTERVAL=", 17) == 0) {
    int d, h, m, s;
    if (sscanf(command + 17, "%d,%d,%d,%d", &d, &h, &m, &s) == 4) {
      logInterval[0] = d; logInterval[1] = h;
      logInterval[2] = m; logInterval[3] = s;
      EEPROM.put(LOG_DAYS_ADDR,    (uint32_t)d);
      EEPROM.put(LOG_HOURS_ADDR,   (uint32_t)h);
      EEPROM.put(LOG_MINUTES_ADDR, (uint32_t)m);
      EEPROM.put(LOG_SECONDS_ADDR, (uint32_t)s);
      sendResponse("OK:LOG_INTERVAL");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  // SET:LOGGING=ON|OFF
  else if (strncmp(upper, "SET:LOGGING=", 12) == 0) {
    if (strcmp(upper + 12, "ON") == 0) {
      if (sdCardPresent) {
        isLogging = true;
        EEPROM.update(LOG_STATUS_ADDR, 1);
        nextLogTime = millis();
        sendResponse("OK:LOGGING=ON");
      } else {
        sendError("ERR:NO_SD_CARD");
      }
    } else if (strcmp(upper + 12, "OFF") == 0) {
      isLogging = false;
      EEPROM.update(LOG_STATUS_ADDR, 0);
      sendResponse("OK:LOGGING=OFF");
    } else {
      sendError("ERR:INVALID_VALUE");
    }
    return true;
  }
  // SET:DATETIME=<y>,<mo>,<d>,<h>,<mi>,<s>
  else if (strncmp(upper, "SET:DATETIME=", 13) == 0) {
    int year, month, day, hour, minute, second;
    if (sscanf(command + 13, "%d,%d,%d,%d,%d,%d",
               &year, &month, &day, &hour, &minute, &second) == 6) {
      RtcDateTime newDT((uint16_t)year, (uint8_t)month, (uint8_t)day,
                        (uint8_t)hour, (uint8_t)minute, (uint8_t)second);
      rtc.SetDateTime(newDT);
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
  // SET:LIGHTS=<onMins>,<offMins>
  else if (strncmp(upper, "SET:LIGHTS=", 11) == 0) {
    int onMins, offMins;
    if (sscanf(command + 11, "%d,%d", &onMins, &offMins) == 2) {
      values[3][0] = onMins;
      values[3][1] = offMins;
      EEPROM.put(LIGHT_ON_ADDR,  (uint16_t)onMins);
      EEPROM.put(LIGHT_OFF_ADDR, (uint16_t)offMins);
      sendResponse("OK:LIGHTS");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// WiFi config commands
// ---------------------------------------------------------------------------
bool handleWiFiCommandsHandler(const char* upper, char* command) {
  if (strncmp(upper, "SET:WIFI_SSID=", 14) == 0) {
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
  else if (strncmp(upper, "SET:WIFI_PASS=", 14) == 0) {
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
  else if (strncmp(upper, "SET:WIFI_PORT=", 14) == 0) {
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
  else if (strncmp(upper, "SET:WIFI_ENABLED=", 17) == 0) {
    if (strcmp(upper + 17, "ON") == 0) {
      wifiEnabled = true;
      saveWifiCredentials();
      sendResponse("OK:WIFI_ENABLED_ON");
      initWifi();
    } else if (strcmp(upper + 17, "OFF") == 0) {
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
  else if (strcmp(upper, "WIFI_RESTART") == 0) {
    sendResponse("OK:WIFI_RESTARTING");
    wifiState = WIFI_STATE_IDLE;
    wifiConnected = false;
    wifiClientConnected = false;
    wifiReconnectAttempts = 0;
    strcpy(wifiIPAddress, "0.0.0.0");
    initWifi();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Calibration commands
// ---------------------------------------------------------------------------
bool handleCalibrationCommands(const char* upper, char* command) {
  // SET:CAL=<tempOffset>,<humOffset>,<co2Offset>
  if (strncmp(upper, "SET:CAL=", 8) == 0) {
    float t, h, c;
    if (sscanf(command + 8, "%f,%f,%f", &t, &h, &c) == 3) {
      calTempOffset = t;
      calHumOffset  = h;
      calCO2Offset  = c;
      EEPROM.put(CAL_TEMP_ADDR, t);
      EEPROM.put(CAL_HUM_ADDR,  h);
      EEPROM.put(CAL_CO2_ADDR,  c);
      sendResponse("OK:CAL");
    } else {
      sendError("ERR:FORMAT");
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Response routing
// ---------------------------------------------------------------------------
void sendResponse(const char* msg) {
  if (lastCommandChannel == CHANNEL_WIFI) {
    sendWifiResponse(msg);
  } else {
    sendBluetoothResponse(msg);
  }
}

void sendError(const char* msg) {
  sendResponse(msg);
}

void sendBluetoothResponse(const char* msg) {
  BT_SERIAL.println(msg);
}

void sendWifiResponse(const char* response) {
  if (wifiState < WIFI_STATE_SERVER_STARTED) return;

  if (!wifiClientConnected) {
    wifiClientConnected = true;
    wifiState = WIFI_STATE_CLIENT_CONNECTED;
    lastWifiActivityTime = millis();
  }

  int len = strlen(response) + 2;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%d", len);
  WIFI_SERIAL.println(cmd);

  // Wait for '>' prompt
  unsigned long startTime = millis();
  bool gotPrompt = false;
  while (millis() - startTime < 2000) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (c == '>') { gotPrompt = true; break; }
      if (c == '+' && wifiBufferIndex < WIFI_BUFFER_SIZE - 1) {
        wifiBuffer[wifiBufferIndex++] = c;
        wifiBuffer[wifiBufferIndex] = '\0';
      }
    }
  }
  if (!gotPrompt) {
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

// ---------------------------------------------------------------------------
// Auto-push sensor data
// ---------------------------------------------------------------------------
void sendSensorData() {
  char tempStr[10];
  bool anythingSent = false;
  bool forceHeartbeat = (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL);

  if (lastCommandChannel == CHANNEL_WIFI) {
    while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
  }

  float displayTemp = useFahrenheit ? celsiusToFahrenheit(currentTemperature) : currentTemperature;

  bool dataChanged = forceHeartbeat ||
                     abs(currentCO2 - lastSentCO2) >= 1.0 ||
                     abs(displayTemp - lastSentTemp) >= 0.1 ||
                     abs(currentHumidity - lastSentHum) >= 0.1;

  if (dataChanged) {
    strcpy(responseBuffer, "DATA:CO2=");
    dtostrf(currentCO2, 1, 0, tempStr);
    strcat(responseBuffer, tempStr);
    strcat(responseBuffer, ",TEMP=");
    dtostrf(displayTemp, 1, 1, tempStr);
    strcat(responseBuffer, tempStr);
    strcat(responseBuffer, ",HUM=");
    dtostrf(currentHumidity, 1, 1, tempStr);
    strcat(responseBuffer, tempStr);
    sendResponse(responseBuffer);
    delay(100);

    lastSentCO2  = currentCO2;
    lastSentTemp = displayTemp;
    lastSentHum  = currentHumidity;
    anythingSent = true;
  }

  bool relayHum   = (digitalRead(HUMIDITY_RELAY_PIN) == LOW);
  bool relayHeat  = (digitalRead(HEAT_RELAY_PIN)     == LOW);
  bool relayCO2   = (digitalRead(CO2_RELAY_PIN)      == LOW);
  bool relayLight = (digitalRead(LIGHT_RELAY_PIN)    == LOW);

  bool relayChanged = forceHeartbeat ||
                      relayHum   != lastSentRelayHum   ||
                      relayHeat  != lastSentRelayHeat  ||
                      relayCO2   != lastSentRelayCO2   ||
                      relayLight != lastSentRelayLight;

  if (relayChanged) {
    sendResponse(useFahrenheit ? "TEMP_UNIT:F" : "TEMP_UNIT:C");
    delay(100);
    snprintf(responseBuffer, RESPONSE_BUFFER_SIZE,
      "RELAYS:HUM=%s,HEAT=%s,CO2=%s,LIGHT=%s",
      relayHum   ? "ON" : "OFF",
      relayHeat  ? "ON" : "OFF",
      relayCO2   ? "ON" : "OFF",
      relayLight ? "ON" : "OFF"
    );
    sendResponse(responseBuffer);
    lastSentRelayHum   = relayHum;
    lastSentRelayHeat  = relayHeat;
    lastSentRelayCO2   = relayCO2;
    lastSentRelayLight = relayLight;
    anythingSent = true;
  }

  if (anythingSent || forceHeartbeat) {
    lastHeartbeatTime = millis();
  }
}

// ---------------------------------------------------------------------------
// WiFi state machine
// ---------------------------------------------------------------------------
void wifiStateMachine() {
  if (!wifiEnabled) return;

  switch (wifiState) {
    case WIFI_STATE_INITIALIZING:
      if (sendATCommand("AT", "OK", 2000)) {
        if (sendATCommand("AT+CWMODE=1", "OK", 2000)) {
          wifiState = WIFI_STATE_CONNECTING;
          wifiStateTimeout = millis() + 20000;
          connectToWifi();
        } else {
          wifiState = WIFI_STATE_ERROR;
        }
      } else {
        if (millis() > wifiStateTimeout) {
          Serial.println("ESP-01S not responding");
          wifiState = WIFI_STATE_ERROR;
        }
      }
      break;

    case WIFI_STATE_CONNECTING:
      if (millis() > wifiStateTimeout) {
        wifiState = WIFI_STATE_ERROR;
      }
      break;

    case WIFI_STATE_CONNECTED:
      startTcpServer();
      break;

    case WIFI_STATE_CLIENT_CONNECTED:
      if (wifiClientConnected && (millis() - lastWifiActivityTime > WIFI_TIMEOUT)) {
        wifiClientConnected = false;
        wifiState = WIFI_STATE_SERVER_STARTED;
      }
      break;

    case WIFI_STATE_SERVER_STARTED:
    case WIFI_STATE_ERROR:
    case WIFI_STATE_IDLE:
    default:
      break;
  }
}

void connectToWifi() {
  Serial.print(F("Connecting: "));
  Serial.println(wifiSSID);

  snprintf(responseBuffer, RESPONSE_BUFFER_SIZE, "AT+CWJAP=\"%s\",\"%s\"", wifiSSID, wifiPassword);
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
  WIFI_SERIAL.println(responseBuffer);

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
      if (strstr(window, "OK"))   gotOK   = true;
      if (strstr(window, "FAIL")) gotFail = true;
    }
    delay(5);
  }

  if (gotOK && !gotFail) {
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
              for (uint8_t i = 0; i < ipIdx; i++) if (ipBuf[i] == '.') dots++;
              if (dots == 3 && strcmp(ipBuf, "0.0.0.0") != 0) {
                strcpy(wifiIPAddress, ipBuf);
                Serial.print(F("IP: ")); Serial.println(wifiIPAddress);
                break;
              }
            }
            inIP = false; ipIdx = 0; memset(ipBuf, 0, sizeof(ipBuf));
          } else if ((c >= '0' && c <= '9') || c == '.') {
            ipBuf[ipIdx++] = c;
          } else {
            inIP = false; ipIdx = 0; memset(ipBuf, 0, sizeof(ipBuf));
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
  if (!sendATCommand("AT+CIPMUX=1", "OK", 2000)) {
    wifiState = WIFI_STATE_ERROR;
    return;
  }
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%u", wifiPort);
  if (sendATCommand(cmd, "OK", 2000)) {
    Serial.print("Server: ");
    Serial.print(wifiIPAddress);
    Serial.print(":");
    Serial.println(wifiPort);
    wifiState = WIFI_STATE_SERVER_STARTED;
  } else {
    Serial.println("TCP server failed");
    wifiState = WIFI_STATE_ERROR;
  }
}

void checkWifiHealth() {
  // If client recently active or server running, consider healthy
  if (wifiClientConnected && (millis() - lastWifiActivityTime < 30000)) return;
  if (wifiState < WIFI_STATE_SERVER_STARTED) {
    wifiConnected = false;
    strcpy(wifiIPAddress, "0.0.0.0");
  }
}

void attemptWifiReconnect() {
  if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS) return;
  if (millis() - wifiReconnectAttemptTime < WIFI_RECONNECT_DELAY) return;

  wifiReconnectAttempts++;
  wifiReconnectAttemptTime = millis();

  Serial.print("Reconnect ");
  Serial.print(wifiReconnectAttempts);
  Serial.print("/");
  Serial.println(MAX_WIFI_RECONNECT_ATTEMPTS);

  wifiState = WIFI_STATE_IDLE;
  wifiConnected = false;
  wifiClientConnected = false;
  wifiDataPaused = false;
  strcpy(wifiIPAddress, "0.0.0.0");
  initWifi();
}

void wifiMaintenance() {
  if (!wifiEnabled) return;

  if (wifiState == WIFI_STATE_SERVER_STARTED || wifiState == WIFI_STATE_CLIENT_CONNECTED) {
    if (millis() - lastWifiHealthCheck > WIFI_HEALTH_CHECK_INTERVAL) {
      lastWifiHealthCheck = millis();
      checkWifiHealth();
    }
  }

  if (wifiState == WIFI_STATE_ERROR) {
    attemptWifiReconnect();
  }
}

bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
  WIFI_SERIAL.println(cmd);
  return waitForResponse(expected, timeout);
}

bool waitForResponse(const char* expected, unsigned long timeout) {
  unsigned long startTime = millis();
  char window[16] = {0};
  uint8_t wIdx = 0;
  while (millis() - startTime < timeout) {
    if (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      window[wIdx] = c;
      wIdx = (wIdx + 1) % 15;
      window[wIdx] = 0;
      if (strstr(window, expected)) return true;
      if (strstr(window, "ERROR") || strstr(window, "FAIL")) return false;
    }
  }
  return false;
}
