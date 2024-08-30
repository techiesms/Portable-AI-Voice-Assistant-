
// ------------------------------------------------------------------------------------------------------------------------------
// ------------------                      Portable AI Voice Assistant Project by techiesms                         ------------------
// ----------------                                       Aug 28, 2024                                              ------------------
// ------------------                                                                                               ------------------
// ------------------                      This is the modified code and the ordignial code was                     ------------------
// ------------------                      from KALOPROJECTS. A huge Shoutout to his amazing work                   ------------------
//                      KALO PROJECTS Github Repo - https://github.com/kaloprojects/KALO-ESP32-Voice-Assistant
// ------------------------------------------------------------------------------------------------------------------------------


// *** HINT: in case of an 'Sketch too Large' Compiler Warning/ERROR in Arduino IDE (ESP32 Dev Module):
// -> select a larger 'Partition Scheme' via menu > tools: e.g. using 'No OTA (2MB APP / 2MB SPIFFS) ***


/*
Library to be installed 

ESP32 Audio I2S -  https://github.com/schreibfaul1/ESP32-audioI2S
ArduinoJSON - https://arduinojson.org/?utm_source=meta&utm_medium=library.properties
SimpleTimer - https://github.com/kiryanenko/SimpleTimer

*/


#define VERSION "\n=== KALO ESP32 Voice Assistant (last update: July 22, 2024) ======================"

#include <WiFi.h>  // only included here
#include <SD.h>    // also needed in other tabs (.ino)

#include <Audio.h>  // needed for PLAYING Audio (via I2S Amplifier, e.g. MAX98357) with ..
                    // Audio.h library from Schreibfaul1: https://github.com/schreibfaul1/ESP32-audioI2S
                    // .. ensure you have actual version (July 18, 2024 or newer needed for 8bit wav files!)
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SimpleTimer.h>

String text;
String filteredAnswer = "";
String repeat;
SimpleTimer Timer;
float batteryVoltage;



// --- PRIVATE credentials -----

const char* ssid = "SSID_NAME";                                                       // ## INSERT your wlan ssid
const char* password = "PASSWORD";                                                // ## INSERT your password
const char* OPENAI_KEY = "***************************************";  // ## optionally (needed for Open AI voices): INSERT your OpenAI key
const char* gemini_KEY = "Your_GEMINI_API_KEY";                   //gemini api
#define TTS_MODEL 0                                                                   // 1 = OpenAI TTS; 0 = Google TTS



String OpenAI_Model = "gpt-3.5-turbo-instruct";  // Model
String OpenAI_Temperature = "0.20";              // temperature
String OpenAI_Max_Tokens = "100";                //Max Tokens

#define AUDIO_FILE "/Audio.wav"  // mandatory, filename for the AUDIO recording

#define TTS_GOOGLE_LANGUAGE "en-IN"  // needed for Google TTS voices only (not needed for multilingual OpenAI voices :) \
                                     // examples: en-US, en-IN, en-BG, en-AU, nl-NL, nl-BE, de-DE, th-TH etc. \
                                     // more infos: https://cloud.google.com/text-to-speech/docs/voices

// --- PIN assignments ---------

#define pin_RECORD_BTN 36
#define pin_VOL_POTI 34
#define pin_repeat 13

#define pin_LED_RED 15
#define pin_LED_GREEN 2
#define pin_LED_BLUE 4

#define pin_I2S_DOUT 25  // 3 pins for I2S Audio Output (Schreibfaul1 audio.h Library)
#define pin_I2S_LRC 26
#define pin_I2S_BCLK 27


// --- global Objects ----------

Audio audio_play;

// declaration of functions in other modules (not mandatory but ensures compiler checks correctly)
// splitting Sketch into multiple tabs see e.g. here: https://www.youtube.com/watch?v=HtYlQXt14zU

bool I2S_Record_Init();
bool Record_Start(String filename);
bool Record_Available(String filename, float* audiolength_sec);

String SpeechToText_Deepgram(String filename);
void Deepgram_KeepAlive();
//for battry
const int batteryPin = 34;             // Pin 34 for battery voltage reading
const float R1 = 100000.0;             // 100k ohm resistor
const float R2 = 10000.0;              // 10k ohm resistor
const float adcMax = 4095.0;           // Max value for ADC on ESP32
const float vRef = 3.4;                // Reference voltage for ESP32
const int numSamples = 100;            // Number of samples for averaging
const float calibrationFactor = 1.48;  // Calibration factor for ADC reading

// ------------------------------------------------------------------------------------------------------------------------------
void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.setTimeout(100);  // 10 times faster reaction after CR entered (default is 1000ms)
  pinMode(batteryPin, INPUT);
  analogReadResolution(12);  // 12-bit ADC resolution


  // Pin assignments:
  pinMode(pin_LED_RED, OUTPUT);
  pinMode(pin_LED_GREEN, OUTPUT);
  pinMode(pin_LED_BLUE, OUTPUT);
  pinMode(pin_RECORD_BTN, INPUT);  // use INPUT_PULLUP if no external Pull-Up connected ##
  pinMode(pin_repeat, INPUT);
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);

  // on INIT: walk 1 sec thru 3 RGB colors (RED -> GREEN -> BLUE), then stay on GREEN
  led_RGB(50, 0, 0);
  delay(500);
  led_RGB(0, 50, 0);
  delay(500);
  led_RGB(0, 0, 50);
  delay(500);
  led_RGB(0, 0, 0);


  // Hello World
  Serial.println(VERSION);
  Timer.setInterval(10000);
  // Connecting to WLAN
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WLAN ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(". Done, device connected.");
  led_RGB(0, 50, 0);  // GREEN

  // Initialize SD card
  if (!SD.begin()) {
    Serial.println("ERROR - SD Card initialization failed!");
    return;
  }

  // initialize KALO I2S Recording Services (don't forget!)
  I2S_Record_Init();

  // INIT Audio Output (via Audio.h, see here: https://github.com/schreibfaul1/ESP32-audioI2S)
  audio_play.setPinout(pin_I2S_BCLK, pin_I2S_LRC, pin_I2S_DOUT);

  audio_play.setVolume(21);  //21
  // INIT done, starting user interaction
  Serial.println("> HOLD button for recording AUDIO .. RELEASE button for REPLAY & Deepgram transcription");
}



// ------------------------------------------------------------------------------------------------------------------------------
void loop() {


here:

  if (digitalRead(pin_RECORD_BTN) == LOW)  // Recording started (ongoing)
  {
    led_RGB(50, 0, 0);  //  RED means 'Recording ongoing'
    delay(30);          // unbouncing & suppressing button 'click' noise in begin of audio recording

    // Before we start any recording we stop any earlier Audio Output or streaming (e.g. radio)
    if (audio_play.isRunning()) {
      audio_play.connecttohost("");  // 'audio_play.stopSong()' wouldn't be enough (STT wouldn't reconnect)
    }

    //Start Recording
    Record_Start(AUDIO_FILE);
  }

  if (digitalRead(pin_RECORD_BTN) == HIGH)  // Recording not started yet .. OR stopped now (on release button)
  {
    led_RGB(0, 0, 0);

    float recorded_seconds;
    if (Record_Available(AUDIO_FILE, &recorded_seconds))  //  true once when recording finalized (.wav file available)
    {
      if (recorded_seconds > 0.4)  // ignore short btn TOUCH (e.g. <0.4 secs, used for 'audio_play.stopSong' only)
      {
        // ## Demo 1 - PLAY your own recorded AUDIO file (from SD card)
        // Hint to 8bit: you need AUDIO.H library from July 18,2024 or later (otherwise 8bit produce only loud (!) noise)
        // we commented out Demo 1 to jump to Demo 2 directly  .. uncomment once if you want to listen to your record !
        /*audio_play.connecttoFS(SD, AUDIO_FILE );              // play your own recorded audio  
        while (audio_play.isRunning()) {audio_play.loop();}     // wait here until done (just for Demo purposes)  */

        // ## Demo 2 [SpeechToText] - Transcript the Audio (waiting here until done)
        // led_RGB(HIGH, HIGH, LOW);  // BLUE means: 'Deepgram server creates transcription'

        String transcription = SpeechToText_Deepgram(AUDIO_FILE);

        //led_RGB(HIGH, LOW, HIGH);  // GREEN means: 'Ready for recording'
        String again = "Please Ask Again . . . . . . . . . . . ";


        Serial.println(transcription);
        if (transcription == "") {
          led_RGB(0, 0, 255);
          if (TTS_MODEL == 1)
            audio_play.openai_speech(OPENAI_KEY, "tts-1", again, "shimmer", "mp3", "1");  //ONYX,shimmer,alloy (Uncomment this to use OpenAI TTS)
          else
            speakTextInChunks(again, 93);  // ( Uncomment this to use Google TTS )
          Serial.println("Please Ask Again");
          while (audio_play.isRunning())  // wait here until finished (just for Demo purposes, before we play Demo 4)
          {
            audio_play.loop();
          }
          goto here;
        }



        //----------------------------------------------------
        WiFiClientSecure client;
        client.setInsecure();  // Disable SSL verification for simplicity (not recommended for production)
        String Answer = "";    // Declare Answer variable here

        text = "";

        if (client.connect("generativelanguage.googleapis.com", 443)) {
          String url = "/v1beta/models/gemini-1.5-flash:generateContent?key=" + String(gemini_KEY);

          String payload = String("{\"contents\": [{\"parts\":[{\"text\":\"" + transcription + "\"}]}],\"generationConfig\": {\"maxOutputTokens\": " + OpenAI_Max_Tokens + "}}");


          // Send the HTTP POST request
          client.println("POST " + url + " HTTP/1.1");
          client.println("Host: generativelanguage.googleapis.com");
          client.println("Content-Type: application/json");
          client.print("Content-Length: ");
          client.println(payload.length());
          client.println();
          client.println(payload);

          // Read the response
          String response;
          while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
              break;
            }
          }

          // Read the actual response
          response = client.readString();
          parseResponse(response);
        } else {
          Serial.println("Connection failed!");
        }

        client.stop();  // End the connection
        //----------------------------------------------------

        if (filteredAnswer != "")  // we found spoken text .. now starting Demo examples:
        {
          led_RGB(0, 0, 255);
          Serial.print("OpenAI speaking: ");
          Serial.println(filteredAnswer);

          if (TTS_MODEL == 1)
            audio_play.openai_speech(OPENAI_KEY, "tts-1", filteredAnswer.c_str(), "shimmer", "mp3", "1");  //ONYX,shimmer,alloy (Uncomment this to use OpenAI TTS)
          else
            speakTextInChunks(filteredAnswer, 93);  // ( Uncomment this to use Google TTS )
        }
      }
    }
  }



  //for repeat-------------------------
  if (digitalRead(pin_repeat) == LOW) {
    delay(500);
    analogWrite(pin_LED_BLUE, 255);
    Serial.print("repeat - ");
    Serial.println(repeat);
    if (TTS_MODEL == 1)
      audio_play.openai_speech(OPENAI_KEY, "tts-1", repeat, "shimmer", "mp3", "1");  //ONYX,shimmer,alloy (Uncomment this to use OpenAI TTS)
    else
      speakTextInChunks(repeat, 93);  // ( Uncomment this to use Google TTS )
  }

  audio_play.loop();

  if (audio_play.isRunning()) {

    analogWrite(pin_LED_BLUE, 255);
    if (digitalRead(pin_RECORD_BTN) == LOW) {
      goto here;
    }
  } else {


    analogWrite(pin_LED_BLUE, 0);
  }

  String batt = "battery low. please charge";
  if (Timer.isReady()) {
    battry_filtering();
    Serial.print("Battery Voltage: ");
    Serial.println(batteryVoltage);
    if (batteryVoltage < 3.4) {
      if (TTS_MODEL == 1)
      audio_play.openai_speech(OPENAI_KEY, "tts-1", batt.c_str(), "shimmer", "mp3", "1");
      else
      speakTextInChunks(batt.c_str(), 93);  // ( Uncomment this to use Google TTS )
    }

    Timer.reset();
  }

  // Schreibfaul1 loop für Play Audio



  // [Optional]: Stabilize WiFiClientSecure.h + Improve Speed of STT Deepgram response (~1 sec faster)
  // Idea: Connect once, then sending each 5 seconds dummy bytes (to overcome Deepgram auto-closing 10 secs after last request)
  // keep in mind: WiFiClientSecure.h still not 100% reliable (assuming RAM heap issue, rarely freezes after e.g. 10 mins)

  if (digitalRead(pin_RECORD_BTN) == HIGH && !audio_play.isRunning())  // but don't do it during recording or playing
  {
    static uint32_t millis_ping_before;
    if (millis() > (millis_ping_before + 5000)) {
      millis_ping_before = millis();
      led_RGB(0, 0, 0);  // short LED OFF means: 'Reconnection server, can't record in moment'
      Deepgram_KeepAlive();
    }
  }
}

void speakTextInChunks(String text, int maxLength) {
  int start = 0;
  while (start < text.length()) {
    int end = start + maxLength;

    // Ensure we don't split in the middle of a word
    if (end < text.length()) {
      while (end > start && text[end] != ' ' && text[end] != '.' && text[end] != ',') {
        end--;
      }
    }

    // If no space or punctuation is found, just split at maxLength
    if (end == start) {
      end = start + maxLength;
    }

    String chunk = text.substring(start, end);
    audio_play.connecttospeech(chunk.c_str(), TTS_GOOGLE_LANGUAGE);

    while (audio_play.isRunning()) {
      audio_play.loop();
      if (digitalRead(pin_RECORD_BTN) == LOW) {
        break;
      }
    }

    start = end + 1;  // Move to the next part, skipping the space
                      // delay(200);       // Small delay between chunks
  }
}

// ------------------------------------------------------------------------------------------------------------------------------

// Revised section to handle response parsing
void parseResponse(String response) {
  repeat = "";
  // Extract JSON part from the response
  int jsonStartIndex = response.indexOf("{");
  int jsonEndIndex = response.lastIndexOf("}");

  if (jsonStartIndex != -1 && jsonEndIndex != -1) {
    String jsonPart = response.substring(jsonStartIndex, jsonEndIndex + 1);
    // Serial.println("Clean JSON:");
    // Serial.println(jsonPart);

    DynamicJsonDocument doc(1024);  // Increase size if needed
    DeserializationError error = deserializeJson(doc, jsonPart);

    if (error) {
      Serial.print("DeserializeJson failed: ");
      Serial.println(error.c_str());
      return;
    }

    if (doc.containsKey("candidates")) {
      for (const auto& candidate : doc["candidates"].as<JsonArray>()) {
        if (candidate.containsKey("content") && candidate["content"].containsKey("parts")) {

          for (const auto& part : candidate["content"]["parts"].as<JsonArray>()) {
            if (part.containsKey("text")) {
              text += part["text"].as<String>();
            }
          }
          text.trim();
          // Serial.print("Extracted Text: ");
          // Serial.println(text);
          filteredAnswer = "";
          for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            if (isalnum(c) || isspace(c) || c == ',' || c == '.' || c == '\'') {
              filteredAnswer += c;
            } else {
              filteredAnswer += ' ';
            }
          }
          // filteredAnswer = text;
          // Serial.print("FILTERED - ");
          //Serial.println(filteredAnswer);

          repeat = filteredAnswer;
        }
      }
    } else {
      Serial.println("No 'candidates' field found in JSON response.");
    }
  } else {
    Serial.println("No valid JSON found in the response.");
  }
}


void led_RGB(int red, int green, int blue) {
  static bool red_before, green_before, blue_before;
  // writing to real pin only if changed (increasing performance for frequently repeated calls)
  if (red != red_before) {
    analogWrite(pin_LED_RED, red);
    red_before = red;
  }
  if (green != green_before) {
    analogWrite(pin_LED_GREEN, green);
    green_before = green;
  }
  if (blue != blue_before) {
    analogWrite(pin_LED_BLUE, blue);
    blue_before = blue;
  }
}
void battry_filtering() {
  float adcValueSum = 0;

  // ADC Averaging
  for (int i = 0; i < numSamples; i++) {
    adcValueSum += analogRead(batteryPin);
    delay(2);
  }

  float adcValueAvg = adcValueSum / numSamples;
  batteryVoltage = adcValueAvg * (vRef / adcMax) * calibrationFactor;
  batteryVoltage = batteryVoltage * ((R1 + R2) / R2);

  // Publishing the calculated battery voltage to Adafruit IO
  Serial.print("Battery Voltage: ");
  Serial.println(batteryVoltage);

  //photocell.publish(batteryVoltage);
}
