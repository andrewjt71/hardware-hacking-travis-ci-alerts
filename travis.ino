#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_SSD1306.h>
#include <pitches.h>
#include <ArduinoJson.h>

#define OLED_RESET 0  // GPIO0

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
const int max_jobs_supported = 3;
const int max_stages_supported = 2;
int previousBuildIDs [] = {0,0,0,0,0};
int currentBuilds [] = {0,0,0,0,0};
const char* featured_repo = "peake";

// PIN SETUP
const uint8_t green_pin = D7;
const uint8_t red_pin = D5;
const uint8_t yellow_pin = D8; // Actually D3 on board
const uint8_t speaker_pin = D6;
Adafruit_SSD1306 display(OLED_RESET); // 64×48 pixels (https://wiki.wemos.cc/products:d1_mini_shields:oled_shield)

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

  // For use debugging.
  Serial.begin(9600);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  
  // Default all LED's to off. This must be done after display.begin, or yellow will be kept on.
  yellowOn(false);
  greenOn(false);
  redOn(false);

  printText("Hi =)", false, 2, false);
  delay(3000);
  
  connectToWifi();
  delay(2000);
}

/**
 * This is the function which the chip will run on loop
 * 
 * @return void
 */
void loop() {
  process();
  delay(8000);
}

/**
 * The overall process
 * 
 * @return void
 */
void process() {
  DynamicJsonDocument buildData = getActiveBuilds();
  JsonArray builds = buildData["builds"];
  
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
  
  printText(String(numberOfBuilds), false, 3, true);

  // Determine builds in previousBuildIDs which are not in currentBuilds
  // @todo: make this dynamic
  for (int i = 0; i < max_builds_supported; i++) {
    int buildId = previousBuildIDs[i];
    if (buildId == 0) {
      break;
    }

    bool inCurrent = isInCurrent(buildId);
    
    if (inCurrent == false) {
      const DynamicJsonDocument build = getBuild(buildId);
      const String status = build["state"];
      const String prTitle = build["pull_request_title"];
      const String user = build["created_by"]["login"];

      printText(user.substring(0, 7), true, 2, false);
      if (status == "passed") {
        flashGreen();
      } else {
        flashRed();
      }
      
      printText(String(numberOfBuilds), false, 3, true);
    }
  }
  moveCurrentBuildsToPreviousBuilds();
}

/**
 * Connect to wifi network
 * 
 * @return void
 */
void connectToWifi() {
  yellowOn(true);
  printText("WiFi", false, 2, false);

  WiFi.mode(WIFI_STA);

  if (in_office) {
    WiFi.begin(office_ssid, office_password);
  } else {
    WiFi.begin(home_ssid, home_password);
  }
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  yellowOn(false);

  greenOn(true);
  printText("Ready", false, 2, false);
  playBootUpSound();
  delay(2000);
  
  printText(" ...", false, 2, false);
  greenOn(false);
}

/**
 * Return build data
 * 
 * @var int buildId
 * 
 * @return DynamicJsonDocument
 */
DynamicJsonDocument getBuild(int buildId) {  
  String buildUrl = "https://api.travis-ci.com/build/" + String(buildId);
  HTTPClient http;
  http.begin(buildUrl, fingerprint);
  http.addHeader("Travis-API-Version", "3");
  http.addHeader("Authorization", travis_token);

  int httpCode = http.GET();

  // @todo: Handle failure.
  if(httpCode == HTTP_CODE_OK) {

    String payload = http.getString();
    http.end();

    // https://arduinojson.org/v6/assistant/
    const size_t capacity = 
    JSON_ARRAY_SIZE(max_stages_supported) + // Stages
    JSON_ARRAY_SIZE(max_jobs_supported) + // Jobs
    JSON_OBJECT_SIZE(3) + // Permissions
    JSON_OBJECT_SIZE(4) + // Branch
    max_jobs_supported * JSON_OBJECT_SIZE(4) + // Each job
    JSON_OBJECT_SIZE(5) + // Created by
    JSON_OBJECT_SIZE(6) + // Repository
    JSON_OBJECT_SIZE(8) + // Commit
    JSON_OBJECT_SIZE(23) + // JSON top level
    2000; // buffer for string duplication

    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, payload);

    return doc;
  }
}

/**
 * Retrieve active builds from Travis
 * 
 * @return void
 */
DynamicJsonDocument getActiveBuilds() {
  printText(" ...", false, 2, false);
  String activeBuildUrl = "https://api.travis-ci.com/owner/boxuk/active";
  HTTPClient http;
  http.begin(activeBuildUrl, fingerprint);
  http.addHeader("Travis-API-Version", "3");
  http.addHeader("Authorization", travis_token);
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();

    // https://arduinojson.org/v6/assistant/
    const size_t capacity = 
    max_builds_supported * JSON_ARRAY_SIZE(max_stages_supported) + // Array of stages for each build
    max_builds_supported * JSON_ARRAY_SIZE(max_jobs_supported) +  //Job array for each build
    JSON_ARRAY_SIZE(max_builds_supported) + // Array of builds
    max_builds_supported * JSON_OBJECT_SIZE(3) + // Permissions for each build
    max_builds_supported * max_jobs_supported * JSON_OBJECT_SIZE(4) + // Each job for each build
    max_builds_supported * JSON_OBJECT_SIZE(4) + // Branch for each build
    JSON_OBJECT_SIZE(4) + // JSON top level
    max_builds_supported * JSON_OBJECT_SIZE(5) + // Created by for each build
    max_builds_supported * JSON_OBJECT_SIZE(6) + // Repository for each build
    max_builds_supported * JSON_OBJECT_SIZE(8) + // Commit for each build
    max_builds_supported * JSON_OBJECT_SIZE(23) + // Each build object
    5000; // buffer for string duplication
 
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, payload);
    
    return doc;
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
 * Print text to oled display.
 * 
 * @var String text
 * @var bool shouldScroll
 * @var int fontSize
 * @var bool centered
 * 
 * @return void
 */
void printText(String text, bool shouldScroll, int fontSize, bool centered) {
  display.clearDisplay();

  if (centered) {
    display.setCursor(58,10);
  } else {
    display.setCursor(35,12);
  }

  if (shouldScroll) {
    display.startscrollleft(0x00, 0x0F);
  } else {
    display.stopscroll();
  }
  
  display.setTextSize(fontSize);
  display.setTextColor(WHITE);
  
  display.println(text);
  display.display();
}

/**
 * Flash red LED
 * 
 * @return void
 */
void flashRed() {
  digitalWrite(red_pin, HIGH);
  playFail();
  delay(10000);
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
  delay(10000);
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
 * Turn redlow LED on or off
 * 
 * @return void
 */
void redOn(bool $on) {
  if ($on) {
    digitalWrite(red_pin, HIGH);
  } else {
    digitalWrite(red_pin, LOW);
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
  delay(10000);
  digitalWrite(green_pin, LOW);
}

void playBootUpSound(){
  if (noise_disabled) {
    return;
  }

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