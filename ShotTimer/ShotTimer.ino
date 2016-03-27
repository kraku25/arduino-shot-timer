/////////////////////////////////
// Shot Timer
// Author: hestenet
// Canonical Repository: https://github.com/hestenet/arduino-shot-timer
/////////////////////////////////
//  This file is part of ShotTimer. 
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//  http://www.gnu.org/licenses/lgpl.txt
//
/////////////////////////////////

//////////////////////////////////////////
// HARDWARE
// Arduino Uno R3
// Adafruit RGB LCD Shield - https://www.adafruit.com/products/714
// Adafruit Electet Mic/Amp - https://www.adafruit.com/products/1063
// Piezzo Buzzer
//////////////////////////////////////////

/////////////////////////////////////////
// Current Flaws:
// shotListener(); probably could be redesigned to run on a timer interupt.
// as a result, the g_par_times beep may come as early or late as half the sample 
// window time. However, at most reasonable g_sample_windows this will likely be 
// indistinguishable to the user
//
// ParTimes are not saved to EEPROM, as their frequent updating is more likely 
// to burn out the chip.
//
// I would like to add SD card support to save the strings
//
// I would like to add course scoring of some kind, and maybe even shooter 
// profiles, but the arduino Uno is likely not powerful enough for this.
//////////////////////////////////////////

//////////////////////////////////////////
// INCLUDES
//////////////////////////////////////////

//////////////
// DEBUG
//////////////

// Comment this out to disable debug information and remove all
// DEBUG messages at compile time
// #define DEBUG  
#include "DebugMacros.h"

//////////////
// Libraries - Core
// These are libraries shipped with Arduino, or that can be installed from the 
// "Manage Libraries" interface of the Arduino IDE:
//            Sketch -> Include Libraries -> Manage Librariess
//////////////

//PROGMEM aka FLASH memory, non-volatile
#include <avr/pgmspace.h>

// PGMWrap makes it easier to use PROGMEM - declare vars with _p e.g: char_p
#include <PGMWrap.h>

// EEPROM additional non-volatile space 
#include <EEPROM.h>

// EEWrap allows you to read/write from EEPROM without special functions and 
// without directly specifying EEPROM address space. 
#include <EEWrap.h> // the .update() method allows you to only update EEPROM if 
                    // values have changed. 

//Wire library lets you manage I2C and 2 pin
#include <Wire.h>

//Chrono - LightChrono - chronometer - to replace StopWatch
#include <LightChrono.h>

//MenuSystem  
#include <MenuSystem.h>

//Adafruit RGB LCD Shield Library
#include <Adafruit_RGBLCDShield.h>


//////////////
// Libraries - Other
// These are libraries that cannot be found in the defauilt Arduino library 
// manager - however, they can be added manually. A source url for each library 
// is provided - simply download the library and include in:
// ~/Documents/Arduino/Libraries
//////////////

// toneAC
// Bit-Bang tone library for piezo buzzer 
// https://bitbucket.org/teckel12/arduino-toneac/wiki/Home#!difference-between-toneac-and-toneac2
#include <toneAC.h>

//////////////
// Other helpful resources
//////////////
// Adafruit sound level sampling: 
// http://learn.adafruit.com/adafruit-microphone-amplifier-breakout/measuring-sound-levels
// http://stackoverflow.com/questions/18903528/permanently-changing-value-of-parameter
//////////////
// Libraries - Mine
//////////////

//Tones for buttons and buzzer
#include "Pitches.h" // musical pitches - optional - format: NOTE_C4 
//Convert time in ms elapsed to hh:mm:ss.mss
#include "LegibleTime.h"
//Helper functions for managing the LCD Display
#include "LCDHelpers.h"


//////////////
// CONSTANTS
//////////////
const uint8_p PROGMEM kMicPin = A0; //set the input for the mic/amplifier 
                                    // the mic/amp is connected to analog pin 0
const uint8_p PROGMEM kButtonDur = 80;
const int16_p PROGMEM kBeepDur = 400;
const int16_p PROGMEM kBeepNote = NOTE_C4;

//////////////
// PROGMEM
//////////////
// To read these - increment over the array: 
// https://github.com/Chris--A/PGMWrap/blob/master/examples/advanced/use_within_classes/use_within_classes.ino 
// More detailed example of dealing with strings and arrays in PROGMEM:
// http://www.gammon.com.au/progmem
const char PROGMEM kMainName[] = "Shot Timer v.3";
const char PROGMEM kStartName[] = "[Start]";
const char PROGMEM kReviewName[] = "[Review]";
const char PROGMEM kParName[] = "Set Par >>";
const char PROGMEM kParSetName[] = "<< [Toggle Par]";
const char PROGMEM kParTimesName[] = "<< [Par Times]";
const char PROGMEM kSettingsName[] = "Settings >>";
const char PROGMEM kSetDelayName[] = "<< [Set Delay] ";
const char PROGMEM kBuzzerName[] = "<< [Buzzr Vol]";
const char PROGMEM kSensitivityName[] = "<< [Sensitivity]";
const char PROGMEM kEchoName[] = "<< [Echo Reject]";
const int PROGMEM kParLimit = 10;
const int PROGMEM kShotLimit = 200;

//////////////
// Instantiation //@TODO: should maybe have a settings object and timer object? 
//////////////
LightChrono shot_chrono;

// The shield uses the I2C SCL and SDA pins. On classic Arduinos
// this is Analog 4 and 5 so you can't use those for analogRead() anymore
// However, you can connect other I2C sensors to the I2C bus and share
// the I2C bus.
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

////////////////////////////////////////
// EEPROM: Settings to be stored in EEPROM
////////////////////////////////////////
// EEPROM HAS 100,000 READ/WRITE CYCLES, conservatively
// http://tronixstuff.wordpress.com/2011/05/11/discovering-arduinos-internal-eeprom-lifespan/
uint8_e g_delay_setting_e;  // Can be 0
uint8_e g_beep_setting_e;  // Can be 0
uint8_e g_sens_setting_e;  // Can be 0
uint8_e g_sample_setting_e; //Cannot be 0  
// ECHO REJECT: Sample window width in mS (50 mS = 20Hz) for function 
// sampleSound()

/////////////////////////////////////////
// GLOBAL VARIABLES
/////////////////////////////////////////
byte g_delay_time = 11;
byte g_beep_vol = 0;
byte g_sensitivity = 1;
byte g_sample_window = 50;
uint32_t g_shot_times[kShotLimit]; // do we want to instantiate the size in setup()
unsigned long g_par_times[kParLimit]; 
uint32_t g_additive_par;
byte g_current_shot; // REFACTOR, MAY NOT NEED TO BE GLOBAL
byte g_review_shot;  // REFACTOR, MAY NOT NEED TO BE GLOBAL
byte g_current_par;  // REFACTOR, MAY NOT NEED TO BE GLOBAL
int g_threshold = 625; // The g_sensitivity is converted to a g_threshold value
byte g_par_cursor = 1;

///////////////
// Program State Variables
///////////////
// http://stackoverflow.com/questions/18903528/permanently-changing-value-of-parameter
uint8_t g_buttons_state;
boolean g_par_enabled;
enum ProgramState {
  MENU,         // Navigating menus
  TIMER,       // Timer is running // && g_par_enabled
  REVIEW,       // 2 - Reviewing shots 
  SETPARSTATE,  // 3 - Setting Par State // && g_par_enabled
  SETPARTIMES,  // 4 - Setting Par Times
  SETINDPAR,    // 5 - ?? Editing Par // Setting Single Par
  SETDELAY,     // 6 - Setting Delay
  SETBEEP,      // 7 - Setting Beep
  SETSENS,      // 8 - Setting Sensitivity 
  SETECHO       // 9 - Setting Echo
 } g_current_state; 

//////////////
//Menus and Menu Items
//////////////

MenuSystem tm;
Menu mainMenu(kMainName);
  MenuItem menuStart(kStartName);
  MenuItem menuReview(kReviewName);
  Menu parMenu(kParName);
    MenuItem menuParState(kParSetName);
    MenuItem menuParTimes(kParTimesName);
  Menu settingsMenu(kSettingsName);
    MenuItem menuStartDelay(kSetDelayName);
    MenuItem menuBuzzer(kBuzzerName);
    MenuItem menuSensitivity(kSensitivityName);
    MenuItem menuEcho(kEchoName);

//////////////
// FUNCTIONS
//////////////
// Note: Any functions with reference parameters i.e myFunction(char &str); 
// must be prototyped manually
// A prototype is simply an empty declaration
//////////////

//////////////////////////////////////////////////////////
// Render the current menu screen
//////////////////////////////////////////////////////////

void renderMenu() {
  Menu const* kMenu = tm.get_current_menu();
  lcd.setBacklight(WHITE);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcdPrint_p(&lcd, kMenu->get_name()); // lcd.print(F("Shot Timer v.3"));
  DEBUG_PRINT(F("Rendering Menu: "));
  DEBUG_PRINTLN_P(kMenu->get_name(),0);
  lcd.setCursor(0, 1);
  lcdPrint_p(&lcd, kMenu->get_selected()->get_name());
  DEBUG_PRINT(F("Rendering Item: "));
  DEBUG_PRINTLN_P(kMenu->get_selected()->get_name(),0);
}

//////////////////////////////////////////////////////////
// Sample Sound
//////////////////////////////////////////////////////////
int sampleSound() {
  uint32_t startMillis = millis();  // Start of sample window 
  // the peak to peak reading will be the total loudness change across the 
  // sample wiindow!
  int peakToPeak = 0; // peak-to-peak level
  int sample = 0;
  int signalMax = 0;
  int signalMin = 1024;

  // collect data for duration of g_sample_window
  while (millis() - startMillis < g_sample_window)
  {
    sample = analogRead(kMicPin);
    if (sample < 1024) // toss out spurious readings
    {
      if (sample > signalMax)
      {
        signalMax = sample; // save just the max levels
      }
      else if (sample < signalMin)
      {
        signalMin = sample; // save just the min levels
      }
    }
  }
  peakToPeak = signalMax - signalMin; // max - min = peak-peak amplitude
  //int millVolts = ((peakToPeak * 3.3) / 1024)*1000 ; // convert to millivolts
  return (peakToPeak);
}


//////////////////////////////////////////////////////////
// Start the Shot Timer
//////////////////////////////////////////////////////////
// Consider changing these to be 'on_menu_event()' functions - such that they 
// can have a local variable for whether the menu item is active, rather than 
// using a global. 
//////////////////////////////////////////////////////////

void on_menuStart_selected(MenuItem* p_menu_item) {
  DEBUG_PRINTLN(F("Starting Timer"),1);
  g_current_state = TIMER;
  lcd.setBacklight(GREEN);
  // reset the values of the array of shots to 0 NOT <= because g_current_shot is 
  // incremented at the end of the last one recorded
  for (int c = 0; c < g_current_shot; c++) { 
    g_shot_times[c] = 0;
  }
  g_current_shot = 0; //start with the first shot in the array
  lcd.setCursor(0, 0);
  lcd.print(F("Wait for it...  "));
  lcd.setCursor(0, 1);
  lcd.print(F("                ")); // create a clearline function? 
                                    // Save fewer strings in progmem?
  startDelay();
  lcd.setCursor(0, 0);
  lcd.print(F(" GO!!  Shot#    ")); //lcd.setCursor(0, 13);
  lcd.setCursor(0, 1);
  lcd.print(F("Last:")); //10 chars
  BEEP();
  shot_chrono.restart();
}

//////////////////////////////////////////////////////////
// Run the shot timer - runs in loop()
//////////////////////////////////////////////////////////

// @TODO: Decide if passing in current state as an argument or just accessing as
// a global variable!
void runTimer(ProgramState* pState, boolean* parState) {
  //DEBUG_PRINTLN(*runState, 0);
  if (*pState == TIMER)
  { 
    //DEBUG_PRINTLN(F("Enter Run Timer Mode."), 0);
    shotListener();
    parBeeps(parState);
  }
}
//////////////////////////////////////////////////////////
// Beep at each par time - runs indirectly in loop()
//////////////////////////////////////////////////////////

void parBeeps(boolean* parState)
{
    //DEBUG_PRINTLN(*parState, 0);
    if (*parState == true) {
      DEBUG_PRINTLN(F("...check for par beep..."),0)
      g_additive_par = 0;
      for (byte i = 0; i < kParLimit; i++) {
        if (g_par_times[i] == 0) {
          break;
        }
        g_additive_par += g_par_times[i]; // add the g_par_times together
        //if (shotTimer.elapsed() <= (g_additive_par + (g_sample_window / 2)) 
        //&& shotTimer.elapsed() >= (g_additive_par - g_sample_window / 2)){
        int timeElapsed = shot_chrono.elapsed();
        // Beep if the current time matches the parTime
        // (within the boundaries of sample window) 
        if (timeElapsed <= (g_additive_par + (g_sample_window / 2)) 
          && timeElapsed >= (g_additive_par - g_sample_window / 2)) {
          DEBUG_PRINTLN(F("PAR BEEP!"),0);
          BEEP();  
        }
      }

    }
}

//////////////////////////////////////////////////////////
// Stop the shot timer
//////////////////////////////////////////////////////////
void stopTimer(boolean out = 0) {
  DEBUG_PRINTLN(F("Stopping Timer"),0);
  if (out == 1) {
    lcd.setBacklight(RED);
  }
  else {
    lcd.setBacklight(WHITE);
  }
  DEBUG_PRINTLN(F("Timer was stopped at:"), 0);
  shot_chrono.elapsed(); // for DEBUG
  for (int i = 0; i < 5; i++) {
    toneAC(kBeepNote, g_beep_vol, 100, false); 
    delay(50);
  }
  if (out == 1) {
    lcd.setBacklight(WHITE);
  }
  // Also - transition menus and re-render menu screens in the stop condition. 
  tm.next(); // move the menu down to review mode
  tm.select(); // move into shot review mode immediately
}

//////////////////////////////////////////////////////////
// Record shots to the string array
//////////////////////////////////////////////////////////

void recordShot() {
  g_shot_times[g_current_shot] = shot_chrono.elapsed();
  DEBUG_PRINT(F("Shot #")); DEBUG_PRINT(g_current_shot + 1); DEBUG_PRINT(F(" - "));
  DEBUG_PRINT(F("\n"));
  //serialPrintln(shotTimer.elapsed());
  //serialPrintln(shot_chrono.elapsed(), 9);
  lcd.setCursor(13, 0);
  lcdPrint(&lcd, g_current_shot + 1, 3);
  lcd.setCursor(6, 1);
  lcdPrintTime(&lcd, g_shot_times[g_current_shot], 9); 
  g_current_shot += 1;
  if (g_current_shot == kShotLimit) { 
    DEBUG_PRINTLN(F("Out of room for shots"),0);
    stopTimer(1);
  }
}

//////////////////////////////////////////////////////////
//review shots - initialize the shot review screen
//////////////////////////////////////////////////////////

void on_menuReview_selected(MenuItem* p_menu_item) {
  if(g_current_state != REVIEW){
    DEBUG_PRINTLN(F("Enter REVIEW Mode"), 0);
    DEBUG_PRINTLN(g_current_state, 0);
    g_current_state = REVIEW;
    DEBUG_PRINTLN(g_current_state, 0);
    if (g_current_shot > 0) {
      g_review_shot = g_current_shot - 1;
    }
    DEBUG_PRINT(F("Reviewing Shot: ")); DEBUG_PRINTLN(g_review_shot,0);
    //DEBUG FOR LOOP - PRINT ALL SHOT TIMES IN THE STRING TO SERIAL 
    for (int t = 0; t < g_current_shot; t++) {
      DEBUG_PRINT(F("Shot #"));
      DEBUG_PRINT(t + 1);
      DEBUG_PRINT(F(" - "));
      g_shot_times[t]; // for DEBUG
    }
    lcd.setBacklight(VIOLET);
    lcd.setCursor(0, 0);
    lcd.print(F("Shot #"));
    lcd.print(g_current_shot);
    lcd.print(F("                "));
    lcd.setCursor(9, 0);
    lcd.print(F(" Split "));
    lcd.setCursor(0, 1);
    lcdPrintTime(&lcd, g_shot_times[g_review_shot], 9);
    lcd.print(F(" "));
    if (g_review_shot == 0) {
      lcd.print(F("   1st"));
    }
    if (g_review_shot > 1) {
      lcdPrintTime(&lcd, g_shot_times[g_review_shot] - g_shot_times[g_review_shot - 1], 6);
    }
    DEBUG_PRINTLN(tm.get_current_menu()->get_name(),0);
  } else {
    DEBUG_PRINTLN(F("Return to Menu"), 0);
    DEBUG_PRINTLN(g_current_state, 0);
    g_current_state = MENU;
    renderMenu();
  }
}

//////////////////////////////////////////////////////////
// review shots - move to the next shot in the string
//////////////////////////////////////////////////////////

void nextShot() {
  DEBUG_PRINTLN(F("nextShot()"), 0);
  DEBUG_PRINTLN(g_current_state, 0);
  lcd.setCursor(0, 0);
  lcd.print(F("Shot #"));
  if (g_current_shot == 0 || g_review_shot == g_current_shot - 1) {
    g_review_shot = 0;
    lcd.print(g_review_shot + 1);
  }
  else {
    g_review_shot++;
    lcd.print(g_review_shot + 1);
  }
  lcd.print(F("                "));
  lcd.setCursor(9, 0);
  lcd.print(F(" Split "));
  lcd.setCursor(0, 1);
  lcdPrintTime(&lcd, g_shot_times[g_review_shot], 9);
  lcd.print(F(" "));
  if (g_review_shot == 0) {
    lcd.print(F("   1st"));
  }
  else {
    lcdPrintTime(&lcd, g_shot_times[g_review_shot] - g_shot_times[g_review_shot - 1], 6);
  }
}

//////////////////////////////////////////////////////////
// review shots - move to the previous shot in the string
//////////////////////////////////////////////////////////

void previousShot() {
  DEBUG_PRINTLN(F("previousShot()"), 0);
  DEBUG_PRINTLN(g_current_state, 0);
  lcd.setCursor(0, 0);
  lcd.print(F("Shot #"));
  if (g_current_shot == 0) {
    g_review_shot = 0;
    lcd.print(g_review_shot);
  }
  else if (g_review_shot == 0) {
    g_review_shot = g_current_shot - 1;
    lcd.print(g_review_shot + 1);
  }
  else {
    g_review_shot--;
    lcd.print(g_review_shot + 1);
  }
  lcd.print(F("                "));
  lcd.setCursor(9, 0);
  lcd.print(F(" Split "));
  lcd.setCursor(0, 1);
  lcdPrintTime(&lcd, g_shot_times[g_review_shot], 9);
  lcd.print(F(" "));
  if (g_review_shot == 0) {
    lcd.print(F("   1st"));
  }
  else {
    lcdPrintTime(&lcd, g_shot_times[g_review_shot] - g_shot_times[g_review_shot - 1], 6);
  }
}

//////////////////////////////////////////////////////////
// Rate of Fire
//////////////////////////////////////////////////////////

void rateOfFire(boolean includeDraw = true) {
  DEBUG_PRINTLN(g_current_state, 0);
  DEBUG_PRINTLN(F("rateofFire()"), 0);
  unsigned int rof;
  if (!includeDraw) {
    rof = (g_shot_times[g_current_shot - 1] - g_shot_times[0]) / (g_current_shot - 1);
  }
  else rof = g_shot_times[g_current_shot - 1] / (g_current_shot - 1);

  lcd.setCursor(0, 0);
  lcd.print(F("                "));
  lcd.setCursor(0, 0);
  lcd.print(F("Avg Split:"));
  lcd.setCursor(11, 0);
  lcdPrintTime(&lcd, rof, 6);
  lcd.setCursor(0, 1);
  lcd.print(F("                "));
  lcd.setCursor(0, 1);
  lcd.print(F("Shots/min:"));
  lcd.setCursor(11, 1);
  lcd.print(60000 / rof); // will this produce a decimal? Or a truncated int?
  //10 characters   //1 characters          //4 characters plus truncated?
}


/////////////////////////////////////////////////////////////
// on_menuStartDelay_selected
/////////////////////////////////////////////////////////////

void on_menuStartDelay_selected(MenuItem* p_menu_item) {
  if(g_current_state != SETDELAY){
    DEBUG_PRINTLN(F("Enter SETDELAY Mode"), 0);
    g_current_state = SETDELAY;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Start Delay"));
    lcd.setCursor(0, 1);
    if (g_delay_time > 11) {
      lcd.print(F("Random 2-6s"));
    }
    else if (g_delay_time == 11) {
      lcd.print(F("Random 1-4s"));
    }
    else {
      lcd.print(g_delay_time);
    }
  }
  else {
    DEBUG_PRINTLN(F("Save Delay and Return to Menu"), 0);
    g_delay_setting_e = g_delay_time;
    g_current_state = MENU;
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// increaseDelay
/////////////////////////////////////////////////////////////

void increaseDelay() {
  DEBUG_PRINTLN(F("increaseDelay()"), 0);
  if (g_delay_time == 12) {
    g_delay_time = 0;
  }
  else {
    g_delay_time++;
  }
  lcd.setCursor(0, 1);
  if (g_delay_time > 11) {
    lcd.print(F("Random 2-6s"));
  }
  else if (g_delay_time == 11) {
    lcd.print(F("Random 1-4s"));
  }
  else {
    lcd.print(g_delay_time);
  }
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// decreaseDelay
/////////////////////////////////////////////////////////////

void decreaseDelay() {
  DEBUG_PRINTLN(F("decreaseDelay()"), 0);
  if (g_delay_time == 0) {
    g_delay_time = 12;
  }
  else {
    g_delay_time--;
  }
  lcd.setCursor(0, 1);
  if (g_delay_time > 11) {
    lcd.print(F("Random 2-6s"));
  }
  else if (g_delay_time == 11) {
    lcd.print(F("Random 1-4s"));
  }
  else {
    lcd.print(g_delay_time);
  }
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// startDelay
/////////////////////////////////////////////////////////////

void startDelay() {
  DEBUG_PRINT(F("Start Delay: ")); DEBUG_PRINTLN(g_delay_time, 0);
  if (g_delay_time > 11) {
    delay(random(2000, 6001)); //from 2 to 6 seconds
  }
  else if (g_delay_time == 11) {
    delay(random(1000, 4001)); //from 1 to 4 seconds
  }
  else {
    delay(g_delay_time * 1000); //fixed number of seconds
  }
}

/////////////////////////////////////////////////////////////
// on_menuBuzzer_selected
/////////////////////////////////////////////////////////////

void on_menuBuzzer_selected(MenuItem* p_menu_item) {
  if(g_current_state != SETBEEP){
    DEBUG_PRINTLN(F("Enter SETBEEP Mode"), 0);
    g_current_state = SETBEEP;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Buzzer Volume"));
    lcd.setCursor(0, 1);
    lcdPrint(&lcd, g_beep_vol, 2);
  }
  else {
    DEBUG_PRINTLN(F("Save BeepVol and Return to Menu"), 0);
    g_beep_setting_e = g_beep_vol;
    g_current_state = MENU;
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// increaseBeepVol
/////////////////////////////////////////////////////////////

void increaseBeepVol() {
  DEBUG_PRINTLN(F("increaseBeepVoly()"), 0);
  if (g_beep_vol == 10) {
    g_beep_vol = 0;
  }
  else {
    g_beep_vol++;
  }
  lcd.setCursor(0, 1);
  lcdPrint(&lcd, g_beep_vol, 2);
  //@TODO REplace with a single PROGMEM clear buffer
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// decreaseBeepVol
/////////////////////////////////////////////////////////////

void decreaseBeepVol() {
  DEBUG_PRINTLN(F("decreaseBeepVoly()"), 0);
  if (g_beep_vol == 0) {
    g_beep_vol = 10;
  }
  else {
    g_beep_vol--;
  }
  lcd.setCursor(0, 1);
  lcdPrint(&lcd, g_beep_vol, 2);
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// on_menuSensitivity_selected
/////////////////////////////////////////////////////////////

void on_menuSensitivity_selected(MenuItem* p_menu_item) {
    if(g_current_state != SETSENS){
    DEBUG_PRINTLN(F("Enter SETSENS Mode"), 0);
    g_current_state = SETSENS;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Sensitivity"));
    lcd.setCursor(0, 1);
    lcdPrint(&lcd, g_sensitivity, 2);
  }
  else {
    DEBUG_PRINTLN(F("Save Sensitivity and Return to Menu"), 0);
    g_sens_setting_e = g_sensitivity;
    g_current_state = MENU;
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// increaseSensitivity
/////////////////////////////////////////////////////////////

void increaseSensitivity() {
  DEBUG_PRINTLN(F("increaseSensitivity()"), 0);
  if (g_sensitivity == 20) {
    g_sensitivity = 0;
  }
  else {
    g_sensitivity++;
  }
  sensToThreshold();
  lcd.setCursor(0, 1);
  lcdPrint(&lcd, g_sensitivity, 2);
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// decreaseSensitivity
/////////////////////////////////////////////////////////////

void decreaseSensitivity() {
  DEBUG_PRINTLN(F("decreaseSensitivity()"), 0);
  if (g_sensitivity == 0) {
    g_sensitivity = 20;
  }
  else {
    g_sensitivity--;
  }
  sensToThreshold();
  lcd.setCursor(0, 1);
  lcdPrint(&lcd, g_sensitivity, 2);
  lcd.print(F("                "));
}

/////////////////////////////////////////////////////////////
// on_menuEcho_selected - EEPROM
/////////////////////////////////////////////////////////////

void on_menuEcho_selected(MenuItem* p_menu_item) {
  if(g_current_state != SETECHO){
    DEBUG_PRINTLN(F("Enter SETECHO Mode"), 0);
    g_current_state = SETECHO;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Echo Protect"));
    lcd.setCursor(0, 1);
    lcd.print(g_sample_window);
    lcd.print(F("ms"));
  }
  else {
    DEBUG_PRINTLN(F("Save Echo and Return to Menu"), 0);
    g_sample_setting_e = g_sample_window;
    g_current_state = MENU;
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// increaseEchoProtect
/////////////////////////////////////////////////////////////

void increaseEchoProtect() {
  DEBUG_PRINTLN(F("increaseEchoProtect()"), 0);
  if (g_sample_window == 100) {
    g_sample_window = 10;
  }
  else {
    g_sample_window += 10;
  }
  lcd.setCursor(0, 1);
  lcd.print(g_sample_window);
  lcd.print(F("ms              ")); //CLEARLINE
}

/////////////////////////////////////////////////////////////
// decreaseEchoProtect
/////////////////////////////////////////////////////////////

void decreaseEchoProtect() {
  DEBUG_PRINTLN(F("decreaseEchoProtect()"), 0);
  if (g_sample_window == 10) {
    g_sample_window = 100;
  }
  else {
    g_sample_window -= 10;
  }
  lcd.setCursor(0, 1);
  lcd.print(g_sample_window);
  lcd.print(F("ms              "));//CLEARLINE
}

/////////////////////////////////////////////////////////////
// convert g_sensitivity to g_threshold
/////////////////////////////////////////////////////////////

void sensToThreshold() {
  g_threshold = 650 - (25 * g_sensitivity);
}

/////////////////////////////////////////////////////////////
// on_menuParState_selected
/////////////////////////////////////////////////////////////

void on_menuParState_selected(MenuItem* p_menu_item) {
  DEBUG_PRINT(F("State before select: ")); DEBUG_PRINTLN(g_current_state,0);
  DEBUG_PRINTLN(tm.get_current_menu()->get_name(),0);
  if(g_current_state != SETPARSTATE){
    DEBUG_PRINTLN(F("Enter SETPARSTATE Mode"),0);
    g_current_state = SETPARSTATE;
    DEBUG_PRINT(F("State after select: ")); DEBUG_PRINTLN(g_current_state,0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Par Times"));
    lcd.setCursor(0, 1);
    if (g_par_enabled == false) {
      lcd.print(F("[DISABLED]"));
    }
    else {
      lcd.print(F("[ENABLED]"));
    }
  }
  else {
    DEBUG_PRINTLN(F("Return to Menu"), 0);
    g_current_state = MENU;
    DEBUG_PRINT(F("State after select: ")); DEBUG_PRINTLN(g_current_state,0);
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// toggleParState
/////////////////////////////////////////////////////////////

void toggleParState() {
  g_par_enabled = !g_par_enabled;
  DEBUG_PRINT(F("g_current_state: ")); DEBUG_PRINTLN(g_current_state,0);
  DEBUG_PRINT(F("Toggled Par to: "));DEBUG_PRINTLN(g_par_enabled, 0);
  lcd.setCursor(0, 1);
  if (g_par_enabled == false) {
    lcd.print(F("[DISABLED]")); //10 characters
  }
  else {
    lcd.print(F("[ENABLED] ")); //10 characters
  }
}

/////////////////////////////////////////////////////////////
// on_menuParTimes_selected
/////////////////////////////////////////////////////////////

void on_menuParTimes_selected(MenuItem* p_menu_item) {
    DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
    DEBUG_PRINT(F("State before select: ")); DEBUG_PRINTLN(g_current_state,0);
  if(g_current_state != SETPARTIMES){
    DEBUG_PRINTLN(F("Enter SETPARTIMES Mode"), 0);
    g_current_state = SETPARTIMES;
    DEBUG_PRINT(F("State after select: ")); DEBUG_PRINTLN(g_current_state,0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("<<"));
    lcd.setCursor(5, 0);
    lcd.print(F("Par"));
    lcd.setCursor(9, 0);
    lcdPrint(&lcd, (g_current_par + 1), 2);
    lcd.setCursor(4, 1);
    if (g_current_par > 0) {
      lcd.print(F("+"));
    }
    else {
      lcd.print(F(" "));
    }
    lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
    DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
  }
  else {
    g_current_state = MENU;
    DEBUG_PRINT(F("State after select: ")); DEBUG_PRINTLN(g_current_state,0);
    renderMenu();
  }
}

/////////////////////////////////////////////////////////////
// parUp()
/////////////////////////////////////////////////////////////

void parUp() {
  DEBUG_PRINTLN(F("parUp()"), 0);
  if (g_current_par == 0) {
    g_current_par = kParLimit - 1;
  }
  else {
    g_current_par--;
  }
  lcd.setCursor(9, 0);
  lcdPrint(&lcd, (g_current_par + 1), 2);
  lcd.setCursor(4, 1);
  if (g_current_par > 0) {
    lcd.print(F("+"));
  }
  else {
    lcd.print(F(" "));
  }
  lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
  DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
}

/////////////////////////////////////////////////////////////
// parDown()
/////////////////////////////////////////////////////////////

void parDown() {
  DEBUG_PRINTLN(F("parDown()"), 1);
  DEBUG_PRINTLN(kParLimit,0);
  if (g_current_par == kParLimit - 1) {
    g_current_par = 0;
  }
  else {
    g_current_par++;
  }
  lcd.setCursor(9, 0);
  lcdPrint(&lcd, (g_current_par + 1), 2);
  lcd.setCursor(4, 1);
  if (g_current_par > 0) {
    lcd.print(F("+"));
  }
  else {
    lcd.print(F(" "));
  }
  lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
  DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
}

/////////////////////////////////////////////////////////////
// editPar()
/////////////////////////////////////////////////////////////

void editPar() {
  if(g_current_state != SETINDPAR){
    DEBUG_PRINTLN(F("Enter SETINDPAR Mode"), 0);
    g_current_state = SETINDPAR;
    lcd.setBacklight(GREEN);
    lcd.setCursor(0, 0);
    lcd.print(F("Edit        "));
    lcd.setCursor(0, 1);
    lcd.print(F("P"));
    lcdPrint(&lcd, g_current_par + 1, 2);
    if (g_current_par > 0) {
      lcd.print(F(" +"));
    }
    else {
      lcd.print(F("  "));
    }
    lcd.setCursor(5, 1);
    lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
    g_par_cursor = 4; //reset cursor to the seconds position
    lcdCursor();
  }
  else {
    DEBUG_PRINTLN(F("Return to SETPARTIMES"), 0);
    Serial.print(g_current_state);
    DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
    lcd.setBacklight(WHITE);
    tm.select();
  }
}

/////////////////////////////////////////////////////////////
// leftCursor()
/////////////////////////////////////////////////////////////

void leftCursor() {
  DEBUG_PRINTLN(F("leftCursor()"), 0);
  if (g_par_cursor == 7) {
    g_par_cursor = 1;
  }
  else {
    g_par_cursor++;
  }
  lcdCursor();
}

/////////////////////////////////////////////////////////////
// rightCursor()
/////////////////////////////////////////////////////////////

void rightCursor() {
  DEBUG_PRINTLN(F("leftCursor()"), 0);
  if (g_par_cursor == 1) {
    g_par_cursor = 7;
  }
  else {
    g_par_cursor--;
  }
  lcdCursor();
}

/////////////////////////////////////////////////////////////
// lcdCursor()
/////////////////////////////////////////////////////////////
//switch case for cursor position displayed on screen
void lcdCursor() {
  DEBUG_PRINT(F("Displaying Cursor at: "));DEBUG_PRINTLN(g_par_cursor, 0);
  switch (g_par_cursor) {
    case 1: //milliseconds
      lcd.setCursor(11, 0); //icon at 13
      lcd.print(F("  _"));
      lcd.setCursor(5, 0); //left behind icon at 5
      lcd.print(F(" "));
      break;
    case 2: //ten milliseconds
      lcd.setCursor(10, 0); //icon at 12
      lcd.print(F("  _  "));
      break;
    case 3: //hundred milliseconds
      lcd.setCursor(9, 0); //icon at 11
      lcd.print(F("  _  "));
      break;
    case 4: //seconds
      lcd.setCursor(7, 0); //icon at 9
      lcd.print(F("  _  "));
      break;
    case 5: //ten seconds
      lcd.setCursor(6, 0); // icon at 8
      lcd.print(F("  _  "));
      break;
    case 6: //minutes
      lcd.setCursor(4, 0); //icon at 6
      lcd.print(F("  _  "));
      break;
    case 7: //ten minutes
      lcd.setCursor(5, 0); //icon at 5
      lcd.print(F("_  "));
      lcd.setCursor(13, 0); //left behind icon at 13
      lcd.print(F(" "));
      break;
  }
}

/////////////////////////////////////////////////////////////
// increaseTime()
/////////////////////////////////////////////////////////////

void increaseTime() {
  DEBUG_PRINT(F("Increase time at: "));DEBUG_PRINTLN(g_par_cursor, 0);
  switch (g_par_cursor) {
    case 1: // milliseconds
      if (g_par_times[g_current_par] == 5999999) {
        break;
      }
      else {
        g_par_times[g_current_par]++;
      }
      break;
    case 2: // hundreds milliseconds
      if (g_par_times[g_current_par] >= 5999990) {
        break;
      }
      else {
        g_par_times[g_current_par] += 10;
      }
      break;
    case 3: // tens milliseconds
      if (g_par_times[g_current_par] >= 5999900) {
        break;
      }
      else {
        g_par_times[g_current_par] += 100;
      }
      break;
    case 4: // seconds
      if (g_par_times[g_current_par] >= 5999000) {
        break;
      }
      else {
        g_par_times[g_current_par] += 1000;
      }
      break;
    case 5: // ten seconds
      if (g_par_times[g_current_par] >= 5990000) {
        break;
      }
      else {
        g_par_times[g_current_par] += 10000;
      }
      break;
    case 6: // minutes
      if (g_par_times[g_current_par] >= 5940000) {
        break;
      }
      else {
        g_par_times[g_current_par] += 60000;
      }
      break;
    case 7: // 10 minutes
      if (g_par_times[g_current_par] >= 5400000) {
        break;
      }
      else {
        g_par_times[g_current_par] += 600000;
      }
      break;
  }
  lcd.setCursor(5, 1);
  lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
}

/////////////////////////////////////////////////////////////
// decreaseTime()
/////////////////////////////////////////////////////////////

void decreaseTime() {
  DEBUG_PRINT(F("Decrease time at: "));DEBUG_PRINTLN(g_par_cursor, 0);
  switch (g_par_cursor) {
    case 1:
      if (g_par_times[g_current_par] < 1) {
        break;
      }
      else {
        g_par_times[g_current_par]--;
      }
      break;
    case 2:
      if (g_par_times[g_current_par] < 10) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 10;
      }
      break;
    case 3:
      if (g_par_times[g_current_par] < 100) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 100;
      }
      break;
    case 4:
      if (g_par_times[g_current_par] < 1000) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 1000;
      }
      break;
    case 5:
      if (g_par_times[g_current_par] < 10000) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 10000;
      }
      break;
    case 6:
      if (g_par_times[g_current_par] < 60000) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 60000;
      }
      break;
    case 7:
      if (g_par_times[g_current_par] < 600000) {
        break;
      }
      else {
        g_par_times[g_current_par] -= 600000;
      }
      break;
  }
  lcd.setCursor(5, 1);
  lcdPrintTime(&lcd, g_par_times[g_current_par], 9);
}

/////////////////////////////////////////////////////////////
// BEEP
/////////////////////////////////////////////////////////////

void BEEP() {
  toneAC(kBeepNote, g_beep_vol, kBeepDur, true);
}

/////////////////////////////////////////////////////////////
// buttonTone
/////////////////////////////////////////////////////////////

void buttonTone() {
  toneAC(kBeepNote, g_beep_vol, kButtonDur, true);
}

/////////////////////////////////////////////////////////////
// SETUP FUNCTIONS
/////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////
// eepromSetup
// Note - EEWrap automatically uses an .update() on EEPROM writes, 
// to avoid wearing out the EEPROM if the value being set is the same as 
// the existing value. 
/////////////////////////////////////////////////////////////

void eepromSetup() {
  DEBUG_PRINTLN(F("Checking if EEPROM needs to be set..."), 0);
  // Unset EEPROM values are set to 255, NOT 0
  // if ANY of our EEPROM stored settings come back 255, we'll know that the 
  // EEPROM settings have not been set
  // By checking all 4 settings, we help ensure that legacy EEPROM data doesn't
  // slip in and cause unexpected behavior.
  byte unSet = 255;
  
  if (g_sample_setting_e == unSet || g_sens_setting_e == unSet 
    || g_beep_setting_e == unSet || g_delay_setting_e == unSet) {
    DEBUG_PRINTLN(F("Setting EEPROM"), 0);
    g_delay_setting_e = g_delay_time;
      DEBUG_PRINTLN(F("Set g_delay_setting_e to "), 0);
      DEBUG_PRINTLN(g_delay_time, 0);
    g_beep_setting_e = g_beep_vol;
      DEBUG_PRINTLN(F("Set g_beep_setting_e to "), 0);
      DEBUG_PRINTLN(g_beep_vol, 0);
    g_sens_setting_e = g_sensitivity;
      DEBUG_PRINTLN(F("Set g_sens_setting_e to "), 0);
      DEBUG_PRINTLN(g_sensitivity, 0);
    g_sample_setting_e = g_sample_window;
      DEBUG_PRINTLN(F("Set g_sample_setting_e to "), 0);
      DEBUG_PRINTLN(g_sample_window, 0);
  }
  else {
    DEBUG_PRINTLN(F("Reading settings from EEPROM"), 0);
    g_delay_time = g_delay_setting_e;
      DEBUG_PRINTLN(F("Set g_delay_time to "), 0);
      DEBUG_PRINTLN(g_delay_time, 0);
    g_beep_vol = g_beep_setting_e;
      DEBUG_PRINTLN(F("Set g_beep_vol to "), 0);
      DEBUG_PRINTLN(g_beep_vol, 0);
    g_sensitivity = g_sens_setting_e;
      DEBUG_PRINTLN(F("Set g_sensitivity to "), 0);
      DEBUG_PRINTLN(g_sensitivity, 0);
    g_sample_window = g_sample_setting_e;
      DEBUG_PRINTLN(F("Set g_sample_window to "), 0);
      DEBUG_PRINTLN(g_sample_window, 0);
  }
  sensToThreshold(); 
}


//////////////////////////////////////////////////////////
// Menu Setup
//////////////////////////////////////////////////////////

void menuSetup()
{
    DEBUG_PRINTLN(F("Setting up menu:"),0);
    DEBUG_PRINTLN_P(kMainName,0);
  mainMenu.add_item(&menuStart, &on_menuStart_selected);
    DEBUG_PRINTLN_P(kStartName,0);
  mainMenu.add_item(&menuReview, &on_menuReview_selected);
    DEBUG_PRINTLN_P(kReviewName,0);
  mainMenu.add_menu(&parMenu);
    DEBUG_PRINTLN_P(kParName,0);
    parMenu.add_item(&menuParState, &on_menuParState_selected);
      DEBUG_PRINTLN_P(kParSetName,0);
    parMenu.add_item(&menuParTimes, &on_menuParTimes_selected);
      DEBUG_PRINTLN_P(kParTimesName,0);
  mainMenu.add_menu(&settingsMenu);
    DEBUG_PRINTLN_P(kSettingsName,0);
    settingsMenu.add_item(&menuStartDelay, &on_menuStartDelay_selected);
      DEBUG_PRINTLN_P(kSetDelayName,0);
    settingsMenu.add_item(&menuBuzzer, &on_menuBuzzer_selected);
      DEBUG_PRINTLN_P(kBuzzerName,0);
    settingsMenu.add_item(&menuSensitivity, &on_menuSensitivity_selected);
      DEBUG_PRINTLN_P(kSensitivityName,0);
    settingsMenu.add_item(&menuEcho, &on_menuEcho_selected);
    DEBUG_PRINTLN_P(kEchoName,0);
  tm.set_root_menu(&mainMenu); 
    DEBUG_PRINTLN(F("Menu Setup Complete"),0);
}

//////////////////////////////////////////////////////////
// LCD Setup
//////////////////////////////////////////////////////////

void lcdSetup() {
  DEBUG_PRINTLN(F("Setting up the LCD"),0);
  lcd.begin(16, 2);
  lcd.setBacklight(WHITE);
  renderMenu();
}

//////////////////////////////////////////////////////////
// Listeners
//////////////////////////////////////////////////////////

//////////////
// Button Listener
// returns true if the button state 
//////////////
void buttonListener(Adafruit_RGBLCDShield* lcd, 
                    uint8_t* bState, ProgramState* pState) {
  //DEBUG_PRINT(F("pState: ")); DEBUG_PRINTLN(*pState,0);
  //DEBUG_PRINT(F("g_current_state: ")); DEBUG_PRINTLN(g_current_state,0);
  /////////////////////////////
  // buttonStateManager
  /////////////////////////////
  //DEBUG_PRINTLN(F("Listening to button input"),0);
  uint8_t stateNow = lcd->readButtons();
  //DEBUG_PRINT(F("stateNow: ")); DEBUG_PRINTLN(stateNow,0);
  //DEBUG_PRINT(F("bState: ")); DEBUG_PRINTLN(*bState, 0);
  uint8_t newButton = stateNow & ~*bState; // true if stateNow != bState
  if (newButton) {DEBUG_PRINT(F("Button Pushed: "));}
  *bState = stateNow;
  /////////////////////////////

  /////////////////////////////
  // buttonReactor
  /////////////////////////////
  switch (*pState){
    case MENU:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_name(),0);
          tm.select();
          if(g_current_state == MENU){renderMenu();}
          break;
        case BUTTON_RIGHT:
          DEBUG_PRINTLN(F("RIGHT/SELECT"), 0);
          tm.select();
          if(g_current_state == MENU){renderMenu();}
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/BACK"), 0);
          tm.back();
          renderMenu();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/NEXT"), 0);
          tm.next();
          renderMenu();
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/PREV"), 0);
          tm.prev();
          renderMenu();
          break;
        }
      break;
    case TIMER:
      if (newButton & BUTTON_SELECT){
        DEBUG_PRINTLN(F("SELECT/stopTimer()"), 0);
        stopTimer();
      }
      break;
    case REVIEW:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          tm.select();
          break;
        case BUTTON_RIGHT:
          DEBUG_PRINTLN(F("RIGHT/rateOfFire()"), 0);
          rateOfFire();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/g_review_shot--;nextShot()"), 0);
          g_review_shot--;
          nextShot();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/nextShot()"), 0);
          nextShot(); 
          //@TODO<-- Maybe I should be building a shot string class, with 
          //functions, rather than using functions to operate on a global array. 
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/previousShot()"), 0);
          previousShot();
          break;
        }
      break;
    case SETPARSTATE:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/(BACK)SELECT"), 0);
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_name(),0);
          tm.select();
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_name(),0);
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          tm.select();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/toggleParState()"), 0);
          toggleParState(); 
          // @TODO<-- Maybe I should build a par times class with par state and 
          // array of part times - and have functions on the class if function 
          // names of all objects I manipulate with my buttons are the same and 
          // on the same buttons - could use polymorphism? 
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/toggleParState()"), 0);
          toggleParState();
          break;
        }
      break;
    case SETPARTIMES:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
          DEBUG_PRINTLN(F("SELECT/editPar"), 0);
          editPar();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
          tm.select(); //@TODO: Bug here
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/parDown()"), 0);
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
          parDown(); 
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/parUp()"), 0);
          DEBUG_PRINTLN_P(tm.get_current_menu()->get_selected()->get_name(),0);
          parUp();
          break;
        }
      break;
    case SETINDPAR:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/editPar()"), 0);
          editPar();
          break;
        case BUTTON_RIGHT:
          DEBUG_PRINTLN(F("RIGHT/rightCursor()"), 0);
          rightCursor();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/leftCursor()"), 0);
          leftCursor();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/decreaseTime()"), 0);
          decreaseTime();
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/increaseTime()"), 0);
          increaseTime();
          break;
        }
      break;
    case SETDELAY:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          tm.select();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          tm.select(); //@TODO: Bug here
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/decreaseDelay()"), 0);
          decreaseDelay(); 
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/increaseDelay()"), 0);
          increaseDelay();
          break;
        }
      break;
    case SETBEEP:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          tm.select();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          tm.select(); 
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/decreaseBeepVol()"), 0);
          increaseBeepVol();
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/increaseBeepVol()"), 0);
          decreaseBeepVol();
          break;
        }
      break;
    case SETSENS:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          tm.select();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          tm.select();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/decreaseSensitivity())"), 0);
          decreaseSensitivity();
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/increaseSensitivity()"), 0);
          increaseSensitivity();
          break;
        }
      break;
    case SETECHO:
      switch (newButton) {
        case BUTTON_SELECT:
          DEBUG_PRINTLN(F("SELECT/SELECT"), 0);
          tm.select();
          break;
        case BUTTON_LEFT:
          DEBUG_PRINTLN(F("LEFT/(BACK)SELECT"), 0);
          tm.select();
          break;
        case BUTTON_DOWN:
          DEBUG_PRINTLN(F("DOWN/decreaseEchoProtect();)"), 0);
          decreaseEchoProtect();
          break;
        case BUTTON_UP:
          DEBUG_PRINTLN(F("UP/increaseEchoProtect();"), 0);
          increaseEchoProtect();
          break;
        }
      break;
  }
}

//////////////////////////////////////////////////////////
// Listen for Shots
//////////////////////////////////////////////////////////

void shotListener() {
  //DEBUG_PRINTLN(F("Listen-start:"),0);
  if (sampleSound() >= g_threshold) {
    recordShot();
  }
  //DEBUG_PRINTLN(F("Listen-end:"),0);
}

//////////////////////////////////////////////////////////
// SETUP AND LOOP
//////////////////////////////////////////////////////////

void setup() {
  randomSeed(analogRead(1));
  DEBUG_SETUP();
  //eepromSetup();
  menuSetup();
  lcdSetup();
  DEBUG_PRINTLN(F("Setup Complete"), 0);
}

void loop() {
  //Probably all button actions should come before runTimer()
  buttonListener(&lcd, &buttonsState, &g_current_state); 
  runTimer(&g_current_state, &g_par_enabled); 
} 
