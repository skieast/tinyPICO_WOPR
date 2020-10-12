#include <Arduino.h>
#include "esp_log.h"
#include "esp32-hal-log.h"
#include <Wire.h>
#include <WIFI.h>
#include "Adafruit_GFX.h" 
#include "Adafruit_LEDBackpack.h"
#include "Onebutton.h"
#include "SPIFFS.h"
#include "ESPFlash.h"
#include "Adafruit.h"
#include "rmt.h"


// Defines
#define HAXORZ_EDITION = 1
#ifndef _BV
#define _BV(bit) (1<<(bit))
#endif
#define SERIALRATE 115200
#define ELEMENTS(x)   (sizeof(x) / sizeof(x[0]))

#define BUTTON1 14 // front lef button
#define BUTTON2 15 // front right button

#ifdef HAXORZ_EDITION
#define BUTTON3 32 // back top button
#define BUTTON4 33 // back bottom button
#endif

#define RGBLED 27 // RGB LED Strip
#define DAC 25 // RGB LED Strip

#ifndef WIFI_SSID
  #error Please define WIFI_SSID in environment. 
//#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
  #error Please define WIFI_PASS in environment
//#define WIFI_PASS ""
#endif

// Not using the following two variables. Declared to test defines above.
const char *wifi_pass = WIFI_PASS;
const char* wifi_ssid = WIFI_SSID;

// User settings
// User settable countdown (seonds) for auto clock mode when in menu
// Set 0 to be off
uint8_t settings_clockCountdownTime = 60;
// User settable GMT value
int settings_GMT = 0;
// User settable Daylight Savings state
bool settings_DST = false;
// User settable display brightness
uint8_t settings_displayBrightness = 15;
// User settable clock separator
int settings_separator = 0; // 0 is " ", 1 is "-", 2 is "_"
          // extra separators by Kean
          // 3 is "." (uses no extra digits) and includes day of month
          // 4 is "/" date in DD/MM or MM/DD format (uses . separator)
// User settable leading zero
bool settings_lzero = false;
// User settable date format DD/MM or MM/DD or DDMmm
int settings_datefmt = 0;

// NTP Wifi Time
const char* ntpServer = "pool.ntp.org";
const int   daylightOffset_sec = 3600;
bool        didChangeClockSettings = false;
bool        hasWiFi = false;

//// Program & Menu state
char clockSeparators[] = {' ', '-', '_', '.', '/'};
String stateStrings[] = {"MENU", "RUNNING", "SETTINGS"};
String menuStrings[] = {"MODE MOVIE", "MODE RANDOM", "MODE MESSAGE", "MODE CLOCK", "SETTINGS"};
String settingsStrings[] = {"GMT ", "DST ", "BRIGHT ", "CLK CNT ", "CLK SEP ", "LD ZERO ", "DATE "};
String dateFormatStrings[] = {"DD/MM", "MM/DD", "DDMon"};

enum states {
  MENU = 0,
  RUNNING ,
  SET ,
} currentState;

enum modes {
  MOVIE = 0,
  RANDOM ,
  MESSAGE ,
  CLOCK ,
  SETTINGS ,
} currentMode;

enum settings {
  SET_GMT = 0,
  SET_DST,
  SET_BRIGHT,
  SET_CLOCK,
  SET_SEP,
  SET_LZERO,
  SET_DATEFMT
} currentSetting;


/* Code cracking stuff
   Though this works really well, there are probably much nicer and cleaner
   ways of doing this, so feel free to improve it and make a pull request!
*/
uint8_t counter = 0;
unsigned long nextTick = 0;
unsigned long nextSolve = 0;
uint16_t tickStep = 100;
uint16_t solveStep = 1000;
uint16_t solveStepMin = 4000;
uint16_t solveStepMax = 8000;
float solveStepMulti = 1;
uint8_t solveCount = 0;
uint8_t solveCountFinished = 10;
byte lastDefconLevel = 0;

// Audio stuff
bool beeping = false;
unsigned long nextBeep = 0;
uint8_t beepCount = 3;
int freq = 2000;
int channel = 0;
int resolution = 8;

// RGB stuff
unsigned long nextRGB = 0;
long nextPixelHue = 0;
uint32_t defcon_colors[] = {
  Color(255, 255, 255),
  Color(255, 0, 0),
  Color(255, 255, 0),
  Color(0, 255, 0),
  Color(0, 0, 255),
};

// General stuff
unsigned long countdownToClock = 0;
byte lastSeconds = 0;

// Setup 3 AlphaNumeric displays (4 digits per display)
Adafruit_AlphaNum4 matrix[3] = { Adafruit_AlphaNum4(), Adafruit_AlphaNum4(), Adafruit_AlphaNum4() };

char displaybuffer[12] = {'-', '-', '-', ' ', '-', '-', '-', '-', ' ', '-', '-', '-'};

char missile_code[12] = {'A', 'B', 'C', 'D', 'E', 'F', '0', '1', '2', '3', '4', '5'};

char missile_code_movie[12] = {'C', 'P', 'E', ' ', '1', '7', '0', '4', ' ', 'T', 'K', 'S'};

char missile_code_message[12] = {'L', 'O', 'L', 'Z', ' ', 'F', 'O', 'R', ' ', 'Y', 'O', 'U'};

uint8_t code_solve_order_movie[10] = {7, 1, 4, 6, 11, 2, 5, 0, 10, 9}; // 4 P 1 0 S E 7 C K T

uint8_t code_solve_order_random[12] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// Initialise the buttons using OneButton library
OneButton Button1(BUTTON1, false);
OneButton Button2(BUTTON2, false);
#ifdef HAXORZ_EDITION
OneButton Button3(BUTTON3, false);
OneButton Button4(BUTTON4, false);
#endif

static const char *TAG = "WOPR";
// Use ESP IDF logging
// Set CORE_DEBUG_LEVEL=5 or via build_flags = -DCORE_DEBUG_LEVEL=5 for PlatformIO
void logMemory() {
  ESP_LOGI(TAG,"Total heap: %d", ESP.getHeapSize());
  ESP_LOGI(TAG,"Free heap: %d", ESP.getFreeHeap());
  ESP_LOGI(TAG,"Total PSRAM: %d", ESP.getPsramSize());
  ESP_LOGI(TAG,"Free PSRAM: %d", ESP.getFreePsram());
}

void loadSettings()
{
  ESPFlash<uint8_t> set_ClockCountdown("/set_ClockCountdown");
  int leng = set_ClockCountdown.length();

  // If the clock countdown is 0, then no data exists
  // So we wil create the defaults and reload
  if ( leng == 0 )
  {
    Serial.println("**** Creating settings!!!");
    saveSettings();
    loadSettings();
    return;
  }
  Clear();

//  ESPFlash<uint8_t> set_ClockCountdown("/set_ClockCountdown");
  settings_clockCountdownTime = set_ClockCountdown.get();
  
  ESPFlash<uint8_t> set_Separator("/set_Separator");
  settings_separator = constrain(set_Separator.get(), 0, ELEMENTS(clockSeparators) - 1);

  ESPFlash<int> set_GMT("/set_GMT");
  settings_GMT = set_GMT.get();

  ESPFlash<int> set_DST("/set_DST");
  settings_DST = set_DST.get() == 1;

  ESPFlash<uint8_t> set_Brightness("/set_Brightness");
  settings_displayBrightness = set_Brightness.get();
  
  ESPFlash<int> set_lzero("/set_LeadingZero");
  settings_lzero = set_lzero.get() == 1;

  ESPFlash<int> set_datefmt("/set_DateFormat");
  settings_datefmt = constrain(set_datefmt.get(), 0, ELEMENTS(dateFormatStrings) - 1);
}

void saveSettings()
{
  ESPFlash<int> set_GMT("/set_GMT");
  set_GMT.set(settings_GMT);

  ESPFlash<int> set_DST("/set_DST");
  set_DST.set(settings_DST ? 1 : 0);

  ESPFlash<uint8_t> set_ClockCountdown("/set_ClockCountdown");
  set_ClockCountdown.set(settings_clockCountdownTime);

  ESPFlash<uint8_t> set_Separator("/set_Separator");
  set_Separator.set(settings_separator);

  ESPFlash<uint8_t> set_Brightness("/set_Brightness");
  set_Brightness.set(settings_displayBrightness);

  ESPFlash<int> set_lzero("/set_LeadingZero");
  set_lzero.set(settings_lzero ? 1 : 0);

  ESPFlash<int> set_datefmt("/set_DateFormat");
  set_datefmt.set(settings_datefmt);
}

// Cycle the setting for whatever current setting we are changing
void UpdateSetting( int dir )
{
  
  switch (currentSetting)  {

    case SET_GMT :
      settings_GMT += dir;
      if ( settings_GMT > 14 )
        settings_GMT = -12;
      else if ( settings_GMT < -12 )
        settings_GMT = 14;
      didChangeClockSettings = true;
      break;
    case SET_DST :
      settings_DST = !settings_DST;
      didChangeClockSettings = true;
      break;
    case SET_BRIGHT :
      settings_displayBrightness += dir;
      if ( settings_displayBrightness > 15 )
        settings_displayBrightness = 0;
      else if ( settings_displayBrightness < 0 )
        settings_displayBrightness = 15;

      SetDisplayBrightness(settings_displayBrightness);
      break;
    case SET_CLOCK :
      settings_clockCountdownTime += dir * 10; // Larger increments for quicker change
      if ( settings_clockCountdownTime > 60 )
        settings_clockCountdownTime = 0;
      else if ( settings_clockCountdownTime < 0 )
        settings_clockCountdownTime = 60;

      countdownToClock = millis() + settings_clockCountdownTime * 1000;
      break;
    case SET_SEP : 
      settings_separator += dir;
      if ( settings_separator == ELEMENTS(clockSeparators) )
        settings_separator = 0;
      else if ( settings_separator < 0 )
        settings_separator = ELEMENTS(clockSeparators) - 1;
      break;
    case SET_LZERO : 
      settings_lzero = !settings_lzero;
      break;
    case SET_DATEFMT : 
      settings_datefmt += dir;
      if ( settings_datefmt == ELEMENTS(dateFormatStrings) )
        settings_datefmt = 0;
      else if ( settings_datefmt < 0 )
        settings_datefmt = ELEMENTS(dateFormatStrings) - 1;
      break;
    default : 
      break;
  }
  // Update the display showing whatever the new current setting is
  ShowSettings();
}

void ShowSettings()
{
  Serial.print("current setting: ");
  Serial.println(currentSetting);

  String val = "";

  switch (currentSetting) {
    case SET_GMT : 
      val = String(settings_GMT);
      break;
    case SET_DST : 
      val = settings_DST ? "ON" : "OFF";
      break;
    case SET_BRIGHT : 
      val = String(settings_displayBrightness);
      break;
    case SET_CLOCK : 
      if ( settings_clockCountdownTime > 0 )
        val = String(settings_clockCountdownTime);
      else
        val = "OFF";
      break;
    case SET_SEP :
      val = String(clockSeparators[settings_separator]);
      switch (clockSeparators[settings_separator]) {
        case ' ' : 
          val = "SPC";
          break;
        case '.' : 
          val = "Dot";
          break;
        case '/' : 
          val = "Date";
          break;
      }
      break;
    case SET_LZERO : 
      val = settings_lzero ? "ON" : "OFF";
      break;
    case SET_DATEFMT : 
      val = dateFormatStrings[settings_datefmt];
      break;
    default :
      break; 
  }
  DisplayText( settingsStrings[(int)currentSetting] + val);


}

// Adjust the LED display brightness: Range is 0-15
void SetDisplayBrightness( int val )
{
  for (int x = 0; x < 3; x++)
    matrix[x].setBrightness(val);
}

void RGB_SetSeconds( byte secs )
{
  // Only update the defcon display if the value has changed
  // to prevent flickering
  if ( secs == lastSeconds ) return;
  lastSeconds = secs;
  RGB_Clear();
  // Zero buffer
  for (int i = 0; i < 5; i++)
    leds[i] = 0;

  // 10's (0-5)
  if ( (secs / 10) > 0 ) {
    int i = (secs / 10) - 1;
    leds[i] = defcon_colors[i];
  }
  if ( (secs % 10) > 0) {
    // Convert ones place seconds to binary and OR with tens decimal
    int ones = secs % 10;
    for (int i=0; i <= 4; i++) {
      if ( (ones >> i) & 1 )
        leds[i] ^= defcon_colors[4 - i];
    }
  }
  RGB_FillBuffer();
}

// Take the time data from the RTC and format it into a string we can display
void DisplayTime()
{
  if (!hasWiFi)
  {
    DisplayText("NO CLOCK");
    return;
  }
  // Store the current time into a struct
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    DisplayText("TIME FAILED");
    return;
  }
  // Formt the contents of the time struct into a string for display
  char buf[20];
  char sep = clockSeparators[settings_separator];
  if (sep=='.') {
    String stndrdth;
    switch (timeinfo.tm_mday) {
      case 1: case 21: case 31: stndrdth = "st"; break;
      case 2: case 22: stndrdth = "nd"; break;
      case 3: case 23:  stndrdth = "rd"; break;
      default: stndrdth = "th"; break;
    }
    sprintf(buf, "%2d%s  ", timeinfo.tm_mday, stndrdth.c_str());
  }
  else if (sep=='/') {
    if (settings_datefmt==0) {  // DD/MM
      if (settings_lzero)
        sprintf(buf, "%02d/%02d ", timeinfo.tm_mday, timeinfo.tm_mon+1);
      else 
        sprintf(buf, "%d/%d ", timeinfo.tm_mday, timeinfo.tm_mon+1);
    }
    else if (settings_datefmt==1) {  // MM/DD
      if (settings_lzero)
        sprintf(buf, "%02d/%02d ", timeinfo.tm_mon+1, timeinfo.tm_mday);
      else 
        sprintf(buf, "%d/%d ", timeinfo.tm_mon+1, timeinfo.tm_mday);
    }
    else { // DDMmm (or D Mmm)
      sprintf(buf, "%d", timeinfo.tm_mday);
      if (timeinfo.tm_mday<10) strcat(buf, " ");
      strftime(buf+strlen(buf), sizeof(buf)-strlen(buf), "%b ", &timeinfo);
    }
    sep = '.';
  }  
  else
    sprintf(buf, "  ");

// Lets put the time string in the correct buffer location.  Probably should be a better way.
  char *loc;
  if (strlen(buf)==5) {
    strcat(buf, " ");
  }
  loc = &buf[strlen(buf)];
  

  if ( timeinfo.tm_hour < 10 && !settings_lzero )
    sprintf(loc, " %d%c%02d%c%02d", timeinfo.tm_hour, sep, timeinfo.tm_min, sep, timeinfo.tm_sec);
  else
    sprintf(loc, "%02d%c%02d%c%02d", timeinfo.tm_hour, sep, timeinfo.tm_min, sep, timeinfo.tm_sec);

  // Iterate through each digit on the display and populate the time, or clear the digit
  // uint8_t curDisplay = 0;
  uint8_t curDigit = 0;

  for ( uint8_t i = 0; i < strlen(buf); i++ )
  {
    if (buf[i+1]=='.')
      matrix[curDigit / 4].writeDigitAscii(curDigit % 4, buf[i++], true);
    else
      matrix[curDigit / 4].writeDigitAscii(curDigit % 4, buf[i]);
    curDigit++;
    // if ( curDigit == 4 )
    // {
    //   curDigit = 0;
    //   curDisplay++;
    // }
  }
   RGB_SetSeconds(timeinfo.tm_sec);
  // Show whatever is in the display buffer on the display
  Display();
}

// Display whatever is in txt on the display
void DisplayText(String txt)
{
  // uint8_t curDisplay = 0;
  // uint8_t curDigit = 0;

  Clear();

  // Iterate through each digit and push the character rom the txt string into that position
  for ( uint8_t i = 0; i < txt.length(); i++ )
  {
     if (i<txt.length()-1 && txt.charAt(i+1)=='.')
      matrix[i / 4].writeDigitAscii( i % 4, txt.charAt(i), true);
    else
      matrix[i / 4].writeDigitAscii( i % 4, txt.charAt(i));
    // curDigit++;
    // if ( curDigit == 4 )
    // {
    //   curDigit = 0;
    //   curDisplay++;
    // }
  }

  // Show whatever is in the display buffer on the display
  Display();
}


// Return a random time step for the next solving solution
uint16_t GetNextSolveStep()
{
  return random( solveStepMin, solveStepMax ) * solveStepMulti;
}

// Fill whatever is in the code buffer into the display buffer
void FillCodes()
{
  // int matrix_index = 0;
  // int character_index = 0;
  char c = 0;
  char c_code = 0;

  for ( int i = 0; i < 12; i++ )
  {
    c = displaybuffer[i];
    c_code = missile_code[i];
    if ( c == '-' )
    {
      // c is a character we need to randomise
      c = random( 48, 91 );
      while ( ( c > 57 && c < 65 ) || c == c_code )
        c = random( 48, 91 );
    }
    matrix[i / 4].writeDigitAscii(i % 4,c);
    
    // matrix[matrix_index].writeDigitAscii( character_index, c );
    // character_index++;
    // if ( character_index == 4 )
    // {
    //   character_index = 0;
    //   matrix_index++;
    // }
  }

  // Show whatever is in the display buffer on the display
  Display();
}

// Randomise the order of the code being solved
void RandomiseSolveOrder()
{
  // bws 20200929 reset code_solve array back to initial state
  for ( uint8_t i = 0; i<12; i++)
    code_solve_order_random[i] = 99;

  for ( uint8_t i = 0; i < 12; i++ )
  {
    uint8_t ind = random(0, 12);
    while ( code_solve_order_random[ind] < 99 ) {
      ind = random(0, 12);
    }

    code_solve_order_random[ind] = i;
  }
}

// Reset the code being solved back to it's starting state
void ResetCode()
{
  if ( currentMode == MOVIE )
  {
    solveStepMulti = 1;
    solveCountFinished  = 10;
    for ( uint8_t i = 0; i < 12; i++ )
      missile_code[i] = missile_code_movie[i];
  }
  else if ( currentMode == RANDOM )
  {
    solveStepMulti = 0.5;

    // Randomise the order in which we solve this code
    RandomiseSolveOrder();

    // Set the code length and populate the code with random chars
    solveCountFinished = 12;

    for ( uint8_t i = 0; i < 12; i++ )
    {
      Serial.print("Setting code index ");
      Serial.print(i);

      // c is a character we need to randomise
      char c = random( 48, 91 );
      while ( c > 57 && c < 65 )
        c = random( 48, 91 );


      Serial.print(" to char ");
      Serial.println( c );

      missile_code[i] = c;
    }
  }
  else if ( currentMode == MESSAGE )
  {
    solveStepMulti = 0.5;

    // Randomise the order in which we solve this code
    RandomiseSolveOrder();
    // Set the code length and populate the code with the stored message
    solveCountFinished = 12;
    for ( uint8_t i = 0; i < 12; i++ )
      missile_code[i] = missile_code_message[i];
  }

  // Set the first solve time step for the first digit lock

  solveStep = GetNextSolveStep();
  nextSolve = millis() + solveStep;
  solveCount = 0;
  lastDefconLevel = 0;

  // Clear code display buffer
  for ( uint8_t i = 0; i < 12; i++ )
  {
    if ( currentMode == MOVIE && ( i == 3 || i == 8 ) )
      displaybuffer[ i ] = ' ';
    else
      displaybuffer[ i ] = '-';
  }
}

/*  Solve the code based on the order of the solver for the current mode
    This is fake of course, but so was the film!
    The reason we solve based on a solver order, is so we can solve the code
    in the order it was solved in the movie.
*/
void SolveCode()
{
  // If the number of digits solved is less than the number to be solved
  if ( solveCount < solveCountFinished )
  {
    // Grab the next digit from the code based on the mode
    uint8_t index = 0;

    if ( currentMode == MOVIE )
    {
      index = code_solve_order_movie[ solveCount ];
      displaybuffer[ index ] = missile_code[ index ];
    }
    else
    {
      index = code_solve_order_random[ solveCount ];
      displaybuffer[ index ] = missile_code[ index ];
    }

    Serial.println("Found " + String(displaybuffer[ index ]) + " @ index: " + String(solveCount));

    // move tghe solver to the next digit of the code
    solveCount++;

    // Get current percentage of code solved so we can set the defcon display
    float solved = 1 - ( (float)solveCount / (float)solveCountFinished);

    Serial.println("Solved " + String(solved));

    byte defconValue = int(solved * 5 + 1);
    RGB_SetDefcon(defconValue, false);

    Serial.println("Defcon " + String(defconValue));

    Serial.println("Next solve index: " + String(solveCount));

    FillCodes();

    // Long beep to indicate a digit in he code has been solved!
    ledcWriteTone(channel, 1500 );
    beeping = true;
    beepCount = 3;
    nextBeep = millis() + 500;
  }
}

// Clear the contents of the display buffers and update the display
void Clear()
{
  // There are 3 LED drivers
  for ( int i = 0; i < 3; i++ )
  {
    // There are 4 digits per LED driver
    for ( int d = 0; d < 4; d++ )
      matrix[i].writeDigitAscii( d, ' ');

    matrix[i].writeDisplay();
  }
}

// Show the contents of the display buffer on the displays
void Display()
{
  for ( int i = 0; i < 3; i++ )
    matrix[i].writeDisplay();
}

void RGB_SetDefcon( byte level, bool force )
{
  // Only update the defcon display if the value has changed
  // to prevent flickering
  if ( lastDefconLevel != level || force )
  {
    lastDefconLevel = level;

    // Clear the RGB LEDs
    RGB_Clear();

    // Level needs to be clamped to between 0 and 4
    byte newLevel = constrain(level - 1, 0, 4);
    leds[newLevel] = defcon_colors[newLevel];

    RGB_FillBuffer();
  }
}

void RGB_Rainbow(int wait)
{
  if ((long)(millis() - nextRGB)>=0)
  {
    nextRGB = millis() + wait;
    nextPixelHue += 256;

    if ( nextPixelHue > 65536 )
      nextPixelHue = 0;

    // For each RGB LED
    for (int i = 0; i < 5; i++)
    {
      int pixelHue = nextPixelHue + (i * 65536L / 5);
      leds[i] = gamma32(ColorHSV(pixelHue));
    }
    // Update RGB LEDs
    RGB_FillBuffer();
  }
}

int pingpong(int t, int length)
{
  return t % length;
}


void RGB_SetColor_ALL(uint32_t col)
{
  // For each RGB LED
  for (int i = 0; i < 5; i++)
    leds[i] = col;

  // Update RGB LEDs
  RGB_FillBuffer();
}


// Button press code her
long nextButtonPress = 0;

// This is triggered from a long press on button 1
void Button1_SaveSettings()
{
  if ( currentState == SET && currentMode == SETTINGS )
  {
    Serial.println("SAAAAAVE!");
    DisplayText( "SAVING..." );
    saveSettings();
    delay(500);

    if ( didChangeClockSettings )
    {
      // We need to start the MCU now to kick in any time changes
      // As they happen during startup when wifi is connected
      ESP.restart();
    }
    else
    {
      // Reset the menu state after save
      currentState = MENU;
      currentSetting = SET_GMT;
      countdownToClock = millis() + settings_clockCountdownTime * 1000;
      DisplayText( "SETTINGS" );
    }
  }
}

void Button1Press()
{
  // Only allow a button press every 10ms
  if ( nextButtonPress < millis() )
  {
    nextButtonPress = millis() + 10;

    // If we are not in the menu, cancel the current state and show the menu
    if ( currentState == RUNNING )
    {
      currentState = MENU;

      DisplayText( "MENU" );

      //Shutdown the audio is it's beeping
      ledcWriteTone(channel, 0);
      beeping = false;
    }
    else if ( currentState == MENU )
    {
      // Update the current program state and display it on the menu
      int nextMode = (int)currentMode + 1;
      if ( nextMode == ELEMENTS(menuStrings) )
        nextMode = 0;
      currentMode = (modes)nextMode;

      DisplayText( menuStrings[(int)currentMode] );
    }
    else if ( currentState == SET )
    {
      // Update the current settings state and display it on the menu
      int nextMode = (int)currentSetting + 1;
      if ( nextMode == ELEMENTS(settingsStrings) )
        nextMode = 0;
      currentSetting = (settings)nextMode;

      ShowSettings();
    }

    // Reset the clock countdown now that we are back in the menu
    // settings_clockCountdownTime is in seconds, we need milliseconds
    countdownToClock = millis() + settings_clockCountdownTime * 1000;

    Serial.print("Current State: ");
    Serial.print( stateStrings[(int)currentState] );

    Serial.print("  Current Mode: ");
    Serial.println( menuStrings[(int)currentMode] );
  }
}

void Button2Press()
{
  // Only allow a button press every 10ms
  if ( nextButtonPress < millis() )
  {
    nextButtonPress = millis() + 10;

    // If in the menu, start whatever menu option we are in
    if ( currentState == MENU )
    {
      // Check to see what mode we are in, because not all modes start the
      // code sequence is
      if ( currentMode == SETTINGS )
      {
        currentState = SET;
        Serial.println("Going into settings mode");
        ShowSettings();
      }
      else
      {
        // Set the defcon state if we are not the clock, otherwise clear the RGB
        if ( currentMode != CLOCK )
          RGB_SetDefcon(5, true);
        else
          RGB_Clear(true);

        ResetCode();
        Clear();
        currentState = RUNNING;
      }
    }
    else if ( currentState == SET )
    {
      // If in the settings, cycle the setting for whatever setting we are on
      if ( currentMode == SETTINGS )
      {
        UpdateSetting(1);
      }
    }
  }

  Serial.print("Current State: ");
  Serial.println( stateStrings[(int)currentState] );
}

#ifdef HAXORZ_EDITION

void Button3Press()
{
  // Only allow a button press every 10ms
  if ( nextButtonPress < millis() )
  {
    nextButtonPress = millis() + 10;

    // If in the settings, cycle the setting UP for whatever menu option we are in
    if ( currentState == SET && currentMode == SETTINGS )
    {
      UpdateSetting(1);
    }
  }
}

void Button4Press()
{
  // Only allow a button press every 10ms
  if ( nextButtonPress < millis() )
  {
    nextButtonPress = millis() + 10;

    // If in the settings, cycle the setting DOWN for whatever menu option we are in
    if ( currentState == SET && currentMode == SETTINGS )
    {
      UpdateSetting(-1);
    }
  }
}


#endif

void StartWifi()
{

  if ( sizeof(WIFI_SSID)==1)
  {
    DisplayText( "SSID NOT SET" );
    RGB_SetColor_ALL( Color(255, 0, 0) );
    hasWiFi = false;
    delay(3000);
  }
  else if ( sizeof(WIFI_PASS)==1 )
  {
    DisplayText( "PASS NOT SET" );
    RGB_SetColor_ALL( Color(255, 0, 0) );
    hasWiFi = false;
    delay(3000);
  }
  else
  {
    DisplayText( "TRYING WiFi" );

    //connect to WiFi
    int wifi_counter = 100;
    Serial.printf("Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED && wifi_counter > 0)
    {
      delay(100);
      RGB_Rainbow(0);
      wifi_counter--;
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED && wifi_counter == 0)
    {
      DisplayText( "WiFi FAILED" );
      RGB_SetColor_ALL( Color(255, 0, 0) );
      hasWiFi = false;
      //while(1) {delay(1000);}
      delay(5000);
    }
    else
    {
      Serial.println(" CONNECTED");
      DisplayText( "WiFi GOOD" );
      RGB_SetColor_ALL( Color(0, 255, 0) );

      hasWiFi = true;

      delay(1000);

      //init and get the time

      configTime(settings_GMT * 3600, settings_DST ? daylightOffset_sec : 0, ntpServer);

      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        DisplayText( "Time FAILED" );
        RGB_SetColor_ALL( Color(255, 0, 0) );
      }
      else
      {
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

        //disconnect WiFi as it's no longer needed
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);

        DisplayText( "Time Set OK" );
        RGB_SetColor_ALL( Color(0, 0, 255) );
      }

      delay(2000);
    }
  }
}


void setup() {
  
  Serial.begin(SERIALRATE);
  esp_log_level_set("*", ESP_LOG_INFO);
  logMemory();
  delay(1000);
  Serial.println("");
  Serial.println("Wargames Missile Codes");

 // Setup RMT RGB strip
  while ( !RGB_Setup(RGBLED, 50) )
  {
    // This is not good...
    delay(10);
  }

  // Attatch button IO for OneButton
  Button1.attachClick(Button1Press);
  Button1.attachDuringLongPress(Button1_SaveSettings);
  Button2.attachClick(Button2Press);
#ifdef HAXORZ_EDITION
  Button3.attachClick(Button3Press);
  Button4.attachClick(Button4Press);
#endif

  // Initialise each of the HT16K33 LED Drivers
  matrix[0].begin(0x70);  // pass in the address  0x70 == 1, 0x72 == 2, 0x74 == 3
  matrix[1].begin(0x72);  // pass in the address  0x70 == 1, 0x72 == 2, 0x74 == 3
  matrix[2].begin(0x74);  // pass in the address  0x70 == 1, 0x72 == 2, 0x74 == 3

  // Reset the code variables
  ResetCode();

  // Clear the display & RGB strip
  Clear();
  RGB_Clear();

  // Setup the Audio channel
  ledcSetup(channel, freq, resolution);
  ledcAttachPin(DAC, channel);


  ESP_LOGI(TAG,"Setup SPIFFS");
  if (!SPIFFS.begin(true)) {
      ESP_LOGI(TAG,"An Error has occurred while mounting SPIFFS, Formatting");
      bool formatted = SPIFFS.format();
      if (!formatted) {
        ESP_LOGI(TAG,"SPIFFS format failed");
      }
  
  }

  // Load the user settings. If this fails, defaults are created.
  DisplayText( "FRMAT SPIFFS" );
  loadSettings();
  
  SetDisplayBrightness(settings_displayBrightness);

  /* Initialise WiFi to get the current time.
     Once the time is obtained, WiFi is disconnected and the internal
     ESP32 RTC is used to keep the time
     Make sure you have set your SSID and Password in secret.h
  */

  StartWifi();

  // User settable countdown from main menu to go into clock if no user interaction
  // Has happened. settings_clockCountdownTime is in seconds and we want milliseconds

  countdownToClock = millis() + settings_clockCountdownTime * 1000;

  // Display MENU
  DisplayText( "MENU" );

}


void loop()
{
  // Used by OneButton to poll for button inputs
  Button1.tick();
  Button2.tick();
#ifdef HAXORZ_EDITION
  Button3.tick();
  Button4.tick();
#endif

  // We are in the menu
  if ( currentState == MENU )
  {
    // We dont need to do anything here, but lets show some fancy RGB!
    RGB_Rainbow(10);

    // Timer to go into clock if no user interaction for XX seconds
    // If settings_clockCountdownTime is 0, this feature is off
    if ( hasWiFi && settings_clockCountdownTime > 0 && ((long)(millis() - countdownToClock)>=0))
    {
      Clear();
      RGB_Clear(true);
      currentMode = CLOCK;
      currentState = RUNNING;
    }
  }
  // We are running a simulation
  else if ( currentState == SET )
  {


  }
  else
  {
    if ( currentMode == CLOCK )
    {
      if ((long)(millis() - nextBeep)>=0)
      {
        DisplayTime();
        nextBeep = millis() + 100; // 1000;
      }
    }
    else
    {
      // We have solved the code
      if ( solveCount == solveCountFinished )
      {
        if ((long)(millis() - nextBeep)>=0)
        {
          beeping = !beeping;
          nextBeep = millis() + 500;

          if ( beeping )
          {
            if ( beepCount > 0 )
            {
              RGB_SetDefcon(1, true);
              FillCodes();
              beepCount--;
              ledcWriteTone(channel, 1500);
            }
            else
            {
              RGB_SetDefcon(1, true);
              DisplayText("LAUNCHING...");
            }
          }
          else
          {
            Clear();
            RGB_Clear(true);
            ledcWriteTone(channel, 0 );
          }
        }

        // We are solved, so no point running any of the code below!
        return;
      }

      // Only update the displays every "tickStep"
      if ((long)(millis() - nextTick)>=0)
      {
        nextTick = millis() + tickStep;

        // This displays whatever the current state of the display is
        FillCodes();

        // If we are not currently beeping, play some random beep/bop computer-y sounds
        if ( !beeping )
          ledcWriteTone(channel, random(90, 250));
      }

      // This is where we solve each code digit
      // The next solve step is a random length to make it take a different time every run
      if ((long)(millis() - nextSolve)>=0)
      {
        nextSolve = millis() + solveStep;
        // Set the solve time step to a random length
        solveStep = GetNextSolveStep();
        //
        SolveCode();
      }

      // Zturn off any beeping if it's trying to beep
      if ( beeping )
      {
        if ((long)(millis() - nextBeep)>=0)
        {
          ledcWriteTone(channel, 0);
          beeping = false;
        }
      }
    }
  }
}
