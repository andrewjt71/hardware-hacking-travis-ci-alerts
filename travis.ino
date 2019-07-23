#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_SSD1306.h>
#include <pitches.h>
#include <ArduinoJson.h>

#define OLED_RESET 0  // GPIO0
#define LOGO16_GLCD_HEIGHT 48
#define LOGO16_GLCD_WIDTH 64
#define SSD1306_LCDWIDTH 64
#define SSD1306_LCDHEIGHT 48

// SECRETS
const char* home_ssid = "CHANGE_ME";
const char* home_password = "CHANGE_ME";
const char* office_ssid = "CHANGE_ME";
const char* office_password = "CHANGE_ME";
const char* travis_token = "token CHANGE_ME";

// FEATURE_FLAGS
const bool noise_disabled = false;
const bool in_office = false;

// GENERAL CONFIG
const char* fingerprint = "43:52:E5:12:B9:91:B5:EC:15:D0:18:D4:94:6E:13:BA:0B:CF:A1:3E"; // https://www.grc.com/fingerprints.html (travis-ci.com)
const int max_builds_supported = 5;
int previousBuildIDs [] = {0,0,0,0,0};
int currentBuilds [] = {0,0,0,0,0};
const char* featured_repo = "peake";

// PIN SETUP
const uint8_t green_pin = D7;
const uint8_t red_pin = D5;
const uint8_t yellow_pin = D8; // Actually D3 on board
const uint8_t speaker_pin = D6;
Adafruit_SSD1306 display(OLED_RESET);

/**
 * This is the function which the chip will run on setup
 */
void setup() { 
  playDoingSomethingSound();
  
  // oled using d1, d2
  pinMode(red_pin, OUTPUT); // red
  pinMode(yellow_pin, OUTPUT); // yellow
  pinMode(green_pin, OUTPUT); // green
  pinMode(speaker_pin, OUTPUT); // speaker

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  display.display();
  delay(2000);

  // Clear the buffer.
  display.clearDisplay();
  
  connectToWifi();
  playBootUpSound();
  delay(2000);
}

/**
 * This is the function which the chip will run on loop
 * 
 * @return void
 */
void loop() {
  process();
  delay(5000);
}

/**
 * The overall process
 * 
 * @return void
 */
void process() {
  String runningJobs = getActiveBuilds();

  // https://arduinojson.org/v6/assistant/
  const size_t capacity = max_builds_supported*JSON_ARRAY_SIZE(0) + 
  max_builds_supported*JSON_ARRAY_SIZE(1) + 
  JSON_ARRAY_SIZE(max_builds_supported) + 
  max_builds_supported*JSON_OBJECT_SIZE(3) + 
  5*JSON_OBJECT_SIZE(4) + 
  max_builds_supported*JSON_OBJECT_SIZE(5) + 
  max_builds_supported*JSON_OBJECT_SIZE(6) + 
  max_builds_supported*JSON_OBJECT_SIZE(8) + 
  max_builds_supported*JSON_OBJECT_SIZE(23);

  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, runningJobs);
  JsonArray builds = doc["builds"];
  
  // Iterate over builds and add to the current builds array
  int currentBuildIndex = 0;
  int numberOfBuilds = 0;

  for (auto build : builds) {
    int id = build["id"];
    String repo = build["repository"]["name"];

    if(repo.indexOf(featured_repo) == -1) {
      continue;
    }

    numberOfBuilds++;
    currentBuilds[currentBuildIndex] = id;
    currentBuildIndex ++;
  }

 if (numberOfBuilds > 0) {
    yellowOn(true);
  } else {
     yellowOn(false);
  }
  
  printText(String(numberOfBuilds) + " Build(s) running", true);

  // Determine builds in previousBuildIDs which are not in currentBuilds
  // @todo: make this dynamic
  for (int i = 0; i < max_builds_supported; i++) {
    int buildId = previousBuildIDs[i];
    if (buildId == 0) {
      break;
    }

    bool inCurrent = isInCurrent(buildId);
    
    if (inCurrent == false) {
      bool jobPassed = passed(buildId);

      if (jobPassed) {
        flashGreen();
        printText("A build passed :)", true);
      } else {
        printText("A build failed :(", true);
        flashRed();
      }
    }
  }
  moveCurrentBuildsToPreviousBuilds();
}

/**
 * Print text to oled display.
 * 
 * @var String text
 * @var bool should Scroll
 * 
 * @return void
 */
void printText(String text, bool shouldScroll) {
  display.clearDisplay();
  
  if (shouldScroll) {
    display.setCursor(0,15);
    display.startscrollleft(0x00, 0x0F);
  } else {
    display.setCursor(35,15);
    display.stopscroll();
  }
  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.println(text);
  display.display();
  delay(1);
}

/**
 * Connect to wifi network
 * 
 * @return void
 */
void connectToWifi() {
  printText("Connecting to wifi.. ", true);

  WiFi.mode(WIFI_STA);

  if (in_office) {
    WiFi.begin(office_ssid, office_password);
  } else {
    WiFi.begin(home_ssid, home_password);
  }
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  printText("Connected!", false);
}

/**
 * Return whether or not the build with a given ID passed
 * 
 * @var int buildId
 * 
 * @return bool
 */
bool passed(int buildId) {  
  String buildUrl = "https://api.travis-ci.com/build/" + String(buildId);
  HTTPClient http;
  http.begin(buildUrl, fingerprint);
  http.addHeader("Travis-API-Version", "3");
  http.addHeader("Authorization", travis_token);

  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();

    https://arduinojson.org/v6/assistant/
    const size_t capacity = JSON_ARRAY_SIZE(0) + 
    JSON_ARRAY_SIZE(1) + 
    JSON_OBJECT_SIZE(3) + 
    2*JSON_OBJECT_SIZE(4) + 
    JSON_OBJECT_SIZE(5) + 
    JSON_OBJECT_SIZE(6) + 
    JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(23) + 
    1190;

    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, payload);

    const String status = doc["state"];

    if (status == "passed") {
      return true;
    } else {
      return false;
    }
    
  } else {
    // If http request failed, log it to the serial and then try again.
    return passed(buildId);
  }
}

/**
 * Return whether or not a given build id is in the array of current builds.
 * 
 * @var int buildId
 * 
 * @return bool
 */
bool isInCurrent(int buildId) {
  for (int i = 0; i < max_builds_supported; i++) {
    if (currentBuilds[i] == 0) {
      return false;
    }
    if (currentBuilds[i] == buildId) {
      return true;
    }
  }

  return false;
}

/**
 * Clear the array of current builds ready for the next iteration
 * 
 * @return void
 */
void clearCurrentBuilds() {
  memset(currentBuilds, 0, sizeof(currentBuilds));
}

/**
 * Clear the array of previous builds ready for the next iteration
 * 
 * @return void
 */
void clearPrevBuilds() {
  memset(previousBuildIDs, 0, sizeof(previousBuildIDs));
}

/**
 * Move the ids in the currentBuilds array to the previousBuilds array ready for the next iteration
 * 
 * @return void
 */
void moveCurrentBuildsToPreviousBuilds() {
  clearPrevBuilds();

  for (int i = 0; i < max_builds_supported; i++) {
    if (currentBuilds[i] == 0) {
      break; 
    }

    previousBuildIDs[i] = currentBuilds[i];
  }

  clearCurrentBuilds();
}

/**
 * Print current builds to the debugger
 * 
 * @return void
 */
void printCurrentBuilds() {
  for (int i = 0; i < max_builds_supported; i++) {
    if (currentBuilds[i] != 0) {
      Serial.println(currentBuilds[i]);  
    }
  }
}

/**
 * Print prev builds to the debugger
 * 
 * @return void
 */
void printPrevBuilds() {
  for (int i = 0; i < max_builds_supported; i++) {
    if (previousBuildIDs[i] != 0) {
      Serial.println(previousBuildIDs[i]);  
    }
  }
}

/**
 * Retrieve active builds from Travis
 * 
 * @return void
 */
String getActiveBuilds() {
  String activeBuildUrl = "https://api.travis-ci.com/owner/boxuk/active";
  HTTPClient http;
  http.begin(activeBuildUrl, fingerprint);
  http.addHeader("Travis-API-Version", "3");
  http.addHeader("Authorization", travis_token);
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    return payload;
  } else {
    return getActiveBuilds();
  }
}

/**
 * Flash red LED
 * 
 * @return void
 */
void flashRed() {
  digitalWrite(red_pin, HIGH);
  playFail();
  delay(3000);
  digitalWrite(red_pin, LOW);
}

/**
 * Flash yellow LED
 * 
 * @return void
 */
void flashYellow() {
  digitalWrite(yellow_pin, HIGH);
  playDoingSomethingSound();
  delay(3000);
  digitalWrite(yellow_pin, LOW);
}

/**
 * Turn yellow LED on or off
 * 
 * @return void
 */
void yellowOn(bool $on) {
  if ($on) {
    digitalWrite(yellow_pin, HIGH);
  } else {
    digitalWrite(yellow_pin, LOW);
  }
}

/**
 * Turn yellow LED on or off
 * 
 * @return void
 */
void greenOn(bool $on) {
  if ($on) {
    digitalWrite(green_pin, HIGH);
  } else {
    digitalWrite(green_pin, LOW);
  }
}

/**
 * Flash green LED
 * 
 * @return void
 */
void flashGreen() {
  digitalWrite(green_pin, HIGH);
  playStatusChange();
  delay(3000);
  digitalWrite(green_pin, LOW);
}

void playBootUpSound(){
  if (noise_disabled) {
    return;
  }
  greenOn(true);
  tone(speaker_pin,NOTE_E6,125);
  delay(130);
  tone(speaker_pin,NOTE_G6,125);
  delay(130);
  tone(speaker_pin,NOTE_E7,125);
  delay(130);
  tone(speaker_pin,NOTE_C7,125);
  delay(130);
  tone(speaker_pin,NOTE_D7,125);
  delay(130);
  tone(speaker_pin,NOTE_G7,125);
  delay(125);
  noTone(speaker_pin);
  greenOn(false);
}

/**
 * Play sound which indicates an event is happening.
 */
void playDoingSomethingSound(){
  if (noise_disabled) {
    return;
  }
  
  tone(speaker_pin,NOTE_E6,125);
  delay(130);
  tone(speaker_pin,NOTE_G6,125);
  noTone(speaker_pin);
}

/**
 * Play sound which indicates a status has changed.
 */
void playStatusChange(){
  if (noise_disabled) {
    return;
  }
  
  tone(speaker_pin,NOTE_E6,125);
  delay(130);
  tone(speaker_pin,NOTE_G6,125);
  delay(130);
  tone(speaker_pin,NOTE_E7,125);
  delay(130);
}

/**
 * Play sound which indicates a failure has occurred.
 */
void playFail(){
  if (noise_disabled) {
    return;
  }
  
  beep(20,50);
  beep(20,50);
  beep(20,50);
  beep(20,50);
  beep(20,50);
  beep(20,50);
}

/**
 * Beep the piezo buzzer
 * 
 * @return void
 */
void beep(int note, unsigned char delayms){
  
  analogWrite(speaker_pin, note);
  delay(delayms);
  analogWrite(speaker_pin, 0);
  delay(delayms); 
}  

