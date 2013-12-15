/**

@author Byron DeLamatre <byron@delamatre.com>
@url https://github.com/bdelamatre/FatRabbitGarden
@about A controller designed for managing and monitoring your garden. Check readme.md

project structure:
* controller         = variable definition, setup() and loop()
* a_init             = contains *init() functions used in setup()
* b_config           = functions for read/write EEPROM config
* c_commands         = commands for controlling the system
* d_sensors          = checkSensors() and related functions. Support for additional sensor types may be added here.
* e_zones            = functions for turning zones on/off
* f_schedules        = checkSchedules() and related functions. Support for additional schedule types may be added here.
* g_logs             = functions used for logging
* u_utilities        = various utility functions
  
**/

/** 
For non-standard libraries copy submodules included under FatRabbitGarden/libraries/ into your Arduino IDE libraries/ folder. 
**/
#include <EEPROM.h>
#include <SD.h>
#include <Wire.h> 
#include <Chronodot.h> //Chronodot by Stephanie-Maks
#include <Flash.h>
#include <DHT.h> //DHT by AdaFruit

//comment this out in production
#define DEBUG
//#define DEBUGMEM
//#define DEBUGHEARTBEAT
//#define DEBUGTIMESYNC
//#define SETTIME
//#define MANUALCONFIG

const int heartBeatDelay = 5000;
boolean heartBeatInProgress = false;
boolean heartBeatOnline = false;
unsigned long heartBeatSent = 0;
unsigned long heartBeatLast = 0;

/*
SD variables
On the Ethernet Shield, CS is pin 4. Note that even if it's not
used as the CS pin, the hardware CS pin (10 on most Arduino boards,
53 on the Mega) must be left as an output or the SD library
functions will not work.
*/
const int chipSelect = 4;
const int hardwareSelect = 14; //Goldilocks
//const int hardwareSelect = 10;  //Arduino Ethernet Shield R3
Sd2Card card;
SdVolume volume;
SdFile root;

//RTC variable
boolean timeSyncInProgress = false;
boolean timeSynced = false;
int timeAtSync;
DateTime timeSyncedDateTime;
Chronodot RTC;

//SoftwareSerial impSerial(8, 9); // RX on 8, TX on 9

String sendCommandBuffer = "";
String receiveCommandBuffer = ""; 

/*
WARNING, increasing these will allow you to configure more 
schedules, zones and sensors, but increase the RAM and EEPROM 
usage. Be careful if increases these that you stay within your
systems limits, or stability issue will occur.
*/
const int maxSchedules = 4; 
const int maxZones = 8; 
const int maxSensors = 8;

//schedule structure, managed by config structure
struct Schedule{
  char name[32];
  int type; //0=off, 1=timer, 2=soil moisture, 3=temperature
  int zones[maxZones]; //zone id, 0 to maxZones specified
  int zonesRunType; //0=series, 1=parallel
  int sensors[maxSensors]; //zone id, 0 to maxSensors specified
  int timerStartWeekdays[7]; //1-7
  int timerStartHours[24]; //1-24
  int timerStartMinutes[60];//1-60
  int timerStartSeconds[60];//1-60
  int valueMin; //will turn zones on when this value is reached by the specified sensors
  int valueMax; //will turn zones off when this value is reached by the specified sensors
  int isRunning; //0=no,1=yes
};

//zone structure, managed by config structure
struct Zone{
  char name[32];
  int type; //0=off, 1=5v relay
  int pin;
  int safetyOffAfterMinutes;
  int overrideOn; //null=n/a,0=off, 1=on
  int isRunning; //0=off, 1=on
  unsigned long statusRunStarted;
  int statusRunBySchedule;
  int statusSafetyOff;
};

//sensor structure, managed by config structure
struct Sensor{
  char name[32];
  int type; //0=off, 1=soil moisture (analog), 2=soil temperature(DS18B20), 3=air temperature (DHT22), 4=light
  int pin;
  int pin2;
  int frequencyCheckSeconds; //0=every loop
  int frequencyLogSeconds; //0=every log
  unsigned long statusValue;
  unsigned long statusValue2;
  unsigned long statusLastChecked;
  unsigned long statusLastLogged;
};

/**
This is the main structure that contains the complete configuration for the system.
**/
#define CONFIG_VERSION "1v4"
#define CONFIG_START 1024
struct ConfigStore{
  char version[4];
  unsigned long utcOffset;
  Schedule schedules[maxSchedules];
  Zone zones[maxZones];
  Sensor sensors[maxSensors];
} configStore={
  CONFIG_VERSION,
};



// the setup routine runs once when you press reset:
void setup() {

  Serial.begin(19200);
  Serial1.begin(19200);
  
  sendCommandBuffer.reserve(256);
  receiveCommandBuffer.reserve(256);

  #if defined(DEBUG)
    printAvailableMemory();
    printBanner();
  #endif
  
  loadConfig();
  initElectricImp();
  initSd();
  initRtc();  
  initSensors();
  initZones();
  initSchedules();
    
  #if defined(DEBUG) 
    printBreak();
    printAvailableMemory();  
  #endif
  
  printCommandLineAvailable();
  
}

//  loop - runs over and over again forever:
void loop(){
  
  //receive commands
  receiveCommand(Serial1);
  receiveCommand(Serial);

  //heartbeat helps determine if the controller is online or offline
  //we haven't sent a heartbeat, but need to
  if((heartBeatOnline==false && heartBeatInProgress==false && millis() < heartBeatDelay) 
        || (heartBeatInProgress==false && (millis()-heartBeatLast)>=heartBeatDelay)){
          
      //send heartbeat
      sendCommand("system:heartbeat>");
      
  //heartbeat sent, but we haven't received a response for awhile
  }else if(heartBeatInProgress==true
            && millis()-heartBeatSent>=(heartBeatDelay*2)){

        //#if defined(DEBUGHEARTBEAT)
        if(heartBeatOnline==true){
          Serial.println("[HEARTBEAT] [OFFLINE]");
        }
        //#endif 
              
        heartBeatInProgress=false;
        heartBeatOnline = false;
        heartBeatLast = 0;
        heartBeatSent = 0; 

  }
  
  //
  // Time synchronization
  //
  //but we can't do anything until the time is synced
  if(timeSynced==false){
    //if time sync ins't in progress, start
    if(timeSyncInProgress==false){
      //String sendCmd = String("config:set-time<");
      sendCommand("config:set-time>");
    }
  }
  
  //time has been synced and we can continue with controller functions
  DateTime timeLocal = getLocalTime();
  
  //safety turn off
  //safetyOff(timeLocal);

  //push status (every 10 seconds)
  //pushStatus();

  //pull config (every 10 seconds)
  //checkConfig();

  //check sensors
  checkSensors(timeLocal);
    
  //check schedules
  //checkSchedules(timeLocal);

  //display settings
  //pushDisplay();
  
}
