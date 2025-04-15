#include <Arduino.h>
//--------- lib---------------
#include <WiFi.h>
#include <WebServer.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>

#include <ESPmDNS.h> // Include the ESPmDNS library

#include <EEPROM.h>
#define EEPROM_SIZE 32 // حجم الذاكرة المطلوبة (يمكن تعديله حسب الحاجة)
//----------------- lib---------------

// ----------------------- Configuration -----------------------
bool autoMode = true;
int morningStartHour = 7;
int nightReturnHour = 18;
int stepInterval = 30;
int motorStepTime = 30; // Variable now represents time in seconds

// ----------------------- EEPROM Functions -----------------------
void saveSettingsToEEPROM()
{
  EEPROM.writeBool(0, autoMode);
  EEPROM.write(1, morningStartHour);
  EEPROM.write(2, nightReturnHour);
  EEPROM.write(3, stepInterval);
  EEPROM.write(4, motorStepTime & 0xFF);        // حفظ الجزء الأدنى
  EEPROM.write(5, (motorStepTime >> 8) & 0xFF); // حفظ الجزء الأعلى
  EEPROM.commit();
  Serial.println("✅ Settings saved to EEPROM");
}

void loadSettingsFromEEPROM()
{
  autoMode = EEPROM.readBool(0);
  morningStartHour = EEPROM.read(1);
  nightReturnHour = EEPROM.read(2);
  stepInterval = EEPROM.read(3);
  motorStepTime = EEPROM.read(4) | (EEPROM.read(5) << 8);
  Serial.println("✅ Settings loaded from EEPROM");
}
// ----------------------- EEPROM Functions -----------------------
void validateOrResetSettings()
{
  if (morningStartHour > 23)
    morningStartHour = 6;
  if (nightReturnHour > 23)
    nightReturnHour = 18;
  if (stepInterval < 1 || stepInterval > 60)
    stepInterval = 30;
  if (motorStepTime < 20 || motorStepTime > 3000)
    motorStepTime = 30;
}

// ----------------------- Configuration -----------------------
const char *ssid = "solar_track";
const char *password = "admin70503";

const int RELAY_EAST = 12;
const int RELAY_WEST = 14;
const int SENSOR_EAST = 22;
const int SENSOR_WEST = 23;

// ----------------------- Global Variables -----------------------
bool isMovingEast = false;
bool isMovingWest = false;

unsigned long lastMoveTime = 0; // Stores last movement time
bool returningToEast = false;   // Track if we returned to East

ThreeWire myWire(15, 2, 4); //  DAT = GPIO15, CLK= GPIO2, RST = GPIO4
RtcDS1302<ThreeWire> Rtc(myWire);
WebServer server(80);

const char *adminPassword = "1234"; // Change this to your desired password
bool isAuthenticated = false;       // Tracks if the user is authenticated

// ----------------------- WiFi Access Point Setup -----------------------
void setupWiFi()
{
  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// ----------------------- Motor Control Functions -----------------------
void stopMotor()
{
  digitalWrite(RELAY_EAST, LOW);
  digitalWrite(RELAY_WEST, LOW);
  isMovingEast = false;
  isMovingWest = false;
  Serial.println("Motor stopped.");
}

void moveEast()
{
  if (digitalRead(SENSOR_EAST) == HIGH)
  {
    Serial.println("East limit reached");
    return;
  }
  if (isMovingWest)
  {
    Serial.println("Cannot move East while West is active!");
    stopMotor();
  }

  Serial.println("Moving East");
  digitalWrite(RELAY_EAST, HIGH);
  digitalWrite(RELAY_WEST, LOW);
  isMovingEast = true;
  isMovingWest = false;
}

void moveWest()
{
  if (digitalRead(SENSOR_WEST) == HIGH)
  {
    Serial.println("West limit reached");
    return;
  }
  if (isMovingEast)
  {
    Serial.println("Cannot move West while East is active!");
    stopMotor();
  }

  Serial.println("Moving West");
  digitalWrite(RELAY_WEST, HIGH);
  digitalWrite(RELAY_EAST, LOW);
  isMovingWest = true;
  isMovingEast = false;
}
//------------------handleUnlock()-------------
void handleUnlock()
{
  String password = server.arg("password");
  if (password == adminPassword)
  {
    isAuthenticated = true;
    Serial.println("✅ Password correct. Settings unlocked.");
  }
  else
  {
    isAuthenticated = false;
    Serial.println("❌ Incorrect password.");
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}

// ----------------------- Web Server Handlers -----------------------
void handleRoot()
{
  RtcDateTime now = Rtc.GetDateTime();
  char timeBuffer[20];
  sprintf(timeBuffer, "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());

  String html = "<!DOCTYPE html><html><head><title>وحدة التحكم بالطاقة الشمسية</title>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "body {text-align:center; font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background:#f0f8ff; color:#333; padding:20px; direction:rtl; margin:0;}"
                "h1 {color:#007bff; margin-bottom: 20px; font-size: 24px;}"
                ".container {max-width: 700px; margin: auto; background: white; padding: 25px; border-radius: 15px; box-shadow: 0px 4px 15px rgba(0,0,0,0.2);}"
                ".btn {display: inline-block; padding: 12px 25px; margin: 10px 5px; border: none; cursor: pointer; color: white; border-radius: 5px; font-size: 16px; text-decoration: none; transition: background 0.3s ease;}"
                ".btn.east {background: #28a745;} .btn.east:hover {background: #218838;}"
                ".btn.west {background: #dc3545;} .btn.west:hover {background: #c82333;}"
                ".btn.stop {background: #ffc107; color: #000;} .btn.stop:hover {background: #e0a800;}"
                ".btn.save {background: #007bff;} .btn.save:hover {background: #0056b3;}"
                ".btn.time {background: #17a2b8;} .btn.time:hover {background: #138496;}"
                ".settings-box {padding: 20px; background: #f8f9fa; border-radius: 10px; margin-top: 20px; text-align: left; box-shadow: 0px 2px 10px rgba(0,0,0,0.1);}"
                "form {margin-top: 20px;} label {font-weight: bold; margin-right: 10px; display: block; margin-bottom: 5px;}"
                ".inline {display: flex; align-items: center; margin-bottom: 15px;}"
                "input[type='checkbox'] {margin-left: 10px;}"
                "input[type='number'], input[type='password'] {width: 100%; padding: 10px; margin: 5px 0; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box;}"
                "input[type='submit'] {width: auto; padding: 10px 20px; cursor: pointer; background: #007bff; color: white; border: none; border-radius: 5px; font-size: 16px; transition: background 0.3s ease;}"
                "input[type='submit']:hover {background: #0056b3;}"
                ".error {color: red; font-weight: bold; margin-top: 10px;}"
                ".success {color: green; font-weight: bold; margin-top: 10px;}"
                "</style></head><body>"
                "<div class='container'>"
                "<h1>وحدة التحكم في الألواح الشمسية</h1>";

  // Display error or success messages
  if (server.hasArg("error"))
  {
    html += "<p class='error'> كلمة المرور غير صحيحة. تم رفض الوصول.</p>";
  }
  else if (server.hasArg("success"))
  {
    html += "<p class='success'> تمت العملية بنجاح!</p>";
  }

  // Display current time
  html += "<p><strong>الوقت الحالي:</strong> " + String(timeBuffer) + "</p>"
                                                                      "<a href='/move?dir=east' class='btn east'>تحرك شرقًا</a>"
                                                                      "<a href='/move?dir=west' class='btn west'>تحرك غربًا</a>"
                                                                      "<a href='/move?dir=stop' class='btn stop'>إيقاف</a>";

  // Time settings form
  html += "<div class='settings-box'>"
          "<h2>ضبط الوقت</h2>"
          "<form action='/settime' method='POST'>"
          "<label>الساعة: <input type='number' name='hour' min='0' max='23'></label>"
          "<label>الدقيقة: <input type='number' name='minute' min='0' max='59'></label>"
          "<label>الثانية: <input type='number' name='second' min='0' max='59'></label>"
          "<label>اليوم: <input type='number' name='day' min='1' max='31'></label>"
          "<label>الشهر: <input type='number' name='month' min='1' max='12'></label>"
          "<label>السنة: <input type='number' name='year' min='2020' max='2099'></label>"
          "<input type='submit' value='ضبط الوقت' class='btn time'></form>"
          "</div>";

  // Settings form
  html += "<div class='settings-box'>"
          "<h2>الإعدادات</h2>"
          "<form action='/settings' method='POST'>"
          "<label>كلمة المرور: <input type='password' name='password' required></label>"
          "<div class='inline'>"
          "<label>الوضع الألي:</label>"
          "<input type='checkbox' name='autoMode' " +
          String(autoMode ? "checked" : "") + ">"
                                              "</div>"
                                              "<label>ساعة بدء الصباح: <input type='number' name='morningStart' value='" +
          String(morningStartHour) + "'></label>"
                                     "<label>ساعة العودة الليلية: <input type='number' name='nightReturn' value='" +
          String(nightReturnHour) + "'></label>"
                                    "<label>فاصل الخطوة (دقيقة): <input type='number' name='stepInterval' value='" +
          String(stepInterval) + "'></label>"
                                 "<label>زمن خطوة المحرك (ثانية): <input type='number' name='motorStepTime' value='" +
          String(motorStepTime) + "'></label>"
                                  "<input type='submit' value='حفظ الإعدادات' class='btn save'></form>"
                                  "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}
//---------------- handleMove()--------------
void handleMove()
{
  String direction = server.arg("dir");
  if (direction == "east")
    moveEast();
  else if (direction == "west")
    moveWest();
  else if (direction == "stop")
    stopMotor();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}
//--------------------handleSettings()------------
void handleSettings()
{
  String password = server.arg("password");
  if (password != adminPassword)
  {
    // Redirect back to the main page with an error message
    server.sendHeader("Location", "/?error=1", true);
    server.send(302, "text/plain", "Redirecting...");
    return;
  }

  autoMode = server.arg("autoMode") == "on";
  morningStartHour = server.arg("morningStart").toInt();
  nightReturnHour = server.arg("nightReturn").toInt();
  stepInterval = server.arg("stepInterval").toInt();

  if (server.hasArg("motorStepTime"))
  {
    int newMotorStepTime = server.arg("motorStepTime").toInt();
    if (newMotorStepTime >= 20 && newMotorStepTime <= 3000) // Adjusted range
    {
      motorStepTime = newMotorStepTime;
      Serial.print("✅ New Motor Step Time (s): ");
      Serial.println(motorStepTime);
    }
    else
    {
      Serial.println("❌ Invalid Motor Step Time. Keeping previous value.");
    }
  }

  // Save all updated settings to EEPROM
  saveSettingsToEEPROM();

  // Redirect back to the main page after successful update
  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");

  Serial.println("✅ Settings updated and saved to EEPROM.");
}
//--------------------handleSetTime()------------
void handleSetTime()
{
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  int second = server.arg("second").toInt();
  int day = server.arg("day").toInt();
  int month = server.arg("month").toInt();
  int year = server.arg("year").toInt();

  RtcDateTime newTime(year, month, day, hour, minute, second);
  Rtc.SetDateTime(newTime);

  Serial.println("✅ Time updated successfully.");
  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
}

// ----------------------- Setup and Loop -----------------------
void setup()
{
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  loadSettingsFromEEPROM();
  validateOrResetSettings();

  pinMode(RELAY_EAST, OUTPUT);
  pinMode(RELAY_WEST, OUTPUT);
  pinMode(SENSOR_EAST, INPUT_PULLDOWN);
  pinMode(SENSOR_WEST, INPUT_PULLDOWN);
  digitalWrite(RELAY_EAST, LOW);
  digitalWrite(RELAY_WEST, LOW);

  setupWiFi();
  Rtc.Begin();

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/settime", HTTP_POST, handleSetTime);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.begin();

  Serial.println("✅ Web server started");
}
//-----------------------------loop ----------
void loop()
{
  server.handleClient(); // معالجة الطلبات دائمًا

  // تحديث الوقت من RTC
  RtcDateTime now = Rtc.GetDateTime();
  int currentHour = now.Hour();
  unsigned long currentMillis = millis();

  // منع تشغيل الريليهات معًا
  if (isMovingEast && isMovingWest)
  {
    stopMotor();
    Serial.println("⚠️ Error: Both relays active! Stopping motor.");
  }

  // **الوضع التلقائي**
  if (autoMode)
  {
    // 🌞 **الصباح: التحرك شرقًا بفواصل زمنية**
    // الصباح: التحرك غربًا بفواصل زمنية
    if (currentHour >= morningStartHour && currentHour < nightReturnHour)
    {
      returningToEast = false; // إعادة ضبط حالة العودة الليلية

      if (!isMovingEast && !isMovingWest && (currentMillis - lastMoveTime >= (stepInterval * 60000)))
      {
        Serial.println("🌞 Auto Mode: Moving West Step");
        moveWest();
        delay(motorStepTime * 1000); // Convert seconds to milliseconds
        stopMotor();
        lastMoveTime = millis();
      }
    }

    // 🌙 **الليل: العودة إلى الشرق حتى الوصول إلى المستشعر**
    else if (currentHour >= nightReturnHour && !returningToEast)
    {
      Serial.println("🌙 Auto Mode: Returning to East");

      // الاستمرار في التحرك شرقًا حتى يلمس الحساس الشرقي
      if (digitalRead(SENSOR_EAST) == LOW)
      {
        Serial.println("🌙 Moving East to return to start position...");
        moveEast(); // تأكد أننا نتحرك شرقًا وليس غربًا
      }
      else
      {
        Serial.println("✅ Reached East Position - Stopping motor");
        stopMotor();
        returningToEast = true; // تأكيد العودة وعدم تكرار العملية
      }
    }
  }
}
