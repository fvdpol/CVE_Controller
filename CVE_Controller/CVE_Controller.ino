/* 
  Controller for the Itho CVE unit
  Frank van de Pol, 2016
  
  - connection to CVE using standard Perilex connector
  - fail-safe mode --> fan running slowly (depending on relais configuration)
  - speed control via 
      remote switch (traditional fan speed switch in kitchen)
      commands over serial (usb) bus
      automatic switching basis the Humidity sensor values
  
 - humidity control: 
       SP hi, SP lo (allow hysteresis)
       store SP in EEPROM (persistance over reboot)
      
  - read-out/monitoring of state, measurements (temperature, humidity)
  
  note: input protection for inputs (switch read-out)
        for systems with 220v switch (ie. standard wiring for CVE) an 220V relay may be used
      
        ?? use an optoisolator with capacitive supply (X2 series capacitor, resistor, rev.pol diode; rc filter)
        http://www.marcspages.co.uk/tech/6103.htm

        advantage of 220V switch: standard configuration; in case of problems/changes the controller can be removed 
        and the CVE connected to the Perilex socket that is controlled by the standard switch.
        
 
  gebruik "HA framework" (zonder RF12)      
  github SI7021 library
  github arduino stuff: aparte repo per library/project (check: werkt dit met meerdere computers en sharing over seafile?)
  
  watchdog: blink the dot on the 7-segment display
   
  
  Hardware: 
    Arduino Mini Pro 5v    
    
    4-relay board (with opto isolators)
    7 segment display (Kingbright SA52-11HWA, common anode)
    SI7020 I2C Humidity sensor for exhaust air monitoring (board with on-board voltage regulator and level converters)
    
    
    
  Set Arduino IDE to:
    Board:      Arduino Pro or Pro Mini
    Processor:  ATMega 328 (5V, 16 MHz)
    
    Note: make sure correct bootloader is installed to avoid the board
    locking-up when watchdog reset occurs.
    https://andreasrohner.at/posts/Electronics/How-to-make-the-Watchdog-Timer-work-on-an-Arduino-Pro-Mini-by-replacing-the-bootloader/


  update september 2016
  add esp-link ESP8266 wifi co-processor to allow stand-allon communications via mqtt + remote monitoring

*/

#define DISPLAY_A  6
#define DISPLAY_B  7
#define DISPLAY_C  8
#define DISPLAY_D  9
#define DISPLAY_E  10
#define DISPLAY_F  11
#define DISPLAY_G  12
#define DISPLAY_DP 13

#define RE1    2
#define RE2    3
#define RE3    4
#define RE4    5

#define SW_SPEED2  A0
#define SW_SPEED3  A1



#define _ENABLE_HEARTBEAT
#define HEARTBEAT_LED       13   // Arduino Digital 4, JeeNode Port 1-Digital
#define HEARTBEAT_INVERTED
#define HEARTBEAT_WATCHDOG


#include "types.h"

#ifdef HEARTBEAT_WATCHDOG
#include <avr/wdt.h>
#endif
#include <EEPROM.h>
#include <JeeLib.h>           // https://github.com/jcw/jeelib
#include <SerialCommand.h>    // https://github.com/kroimon/Arduino-SerialCommand

#include <Wire.h>
#include <SI7021.h>           // https://github.com/fvdpol/SI7021


#include <ELClient.h>
#include <ELClientCmd.h>
#include <ELClientMqtt.h>


//#define SERIAL_BAUD 57600
#define SERIAL_BAUD 115200


#define LED_GLOWTIME  1  /* in 0.1s */

#define MEASURE_INTERVAL 50  /* in 0.1s */
#define PUBLISH_INTERVAL 600 /* in 0.1s */    // ??? make configurable???

enum {
#ifdef _ENABLE_HEARTBEAT 
  TASK_HEARTBEAT,
#endif
  TASK_LIFESIGN,
  TASK_MEASURE,
  TASK_PUBLISH,
  TASK_HUMIDITY_LOW,
  TASK_HUMIDITY_HIGH,
  TASK_DISPLAY,
  TASK_LIMIT };

static word schedBuf[TASK_LIMIT];
Scheduler scheduler (schedBuf, TASK_LIMIT);

bool debug_txt = false;

SI7021 sensor;

SerialCommand sCmd;     // The SerialCommand object

fanspeed_t   buttonState = SPEED_UNDEFINED ;   


bool humidity_control_mode = 1;   // 1= auto                      // TODO: set via mqtt
float humidity_setpoint_lo = 60.0;                                // TODO: set via mqtt
float humidity_setpoint_hi = 65.0;                                // TODO: set via mqtt
int   humidity_delay = 60;     // seconds before speed change     // TODO: set via mqtt
fanspeed_t humidity_max_speed = SPEED_3;  // maximum speed

float sensor_temperature = -1.0;
float sensor_humidity = -1.0;

// TODO: publish interval temp/humidity values via MQTT



// control sources
fanspeed_t  speed_select_user     = SPEED_2;            // speed that is selected by user (switch, command, mqtt)
fanspeed_t  speed_select_humidity = SPEED_UNDEFINED;    // speed requested by humidity control logic

// output
fanspeed_t  speed_select_fan      = SPEED_UNDEFINED;



// Initialize a connection to esp-link using the normal hardware serial port both for
// SLIP and for debug messages.
ELClient esp(&Serial, &Serial);

// Initialize CMD client (for GetTime)
ELClientCmd cmd(&esp);

// Initialize the MQTT client
ELClientMqtt mqtt(&esp);

// Callback made from esp-link to notify of wifi status changes
// Here we just print something out for grins
void wifiCb(void* response) {
  ELClientResponse *res = (ELClientResponse*)response;
  if (res->argc() == 1) {
    uint8_t status;
    res->popArg(&status, 1);

    if(status == STATION_GOT_IP) {
      Serial.println("WIFI CONNECTED");
    } else {
      Serial.print("WIFI NOT READY: ");
      Serial.println(status);
    }
  }
}


bool connected;

// Callback when MQTT is connected
void mqttConnected(void* response) {
  Serial.println("MQTT connected!");
  mqtt.subscribe("/esp-link/1");
  mqtt.subscribe("/hello/world/#");
  //mqtt.subscribe("/esp-link/2", 1);
  //mqtt.publish("/esp-link/0", "test1");
  connected = true;
}

// Callback when MQTT is disconnected
void mqttDisconnected(void* response) {
  Serial.println("MQTT disconnected");
  connected = false;
}

// Callback when an MQTT message arrives for one of our subscriptions
void mqttData(void* response) {
  ELClientResponse *res = (ELClientResponse *)response;

  Serial.print("Received: topic=");
  String topic = res->popString();
  Serial.println(topic);

  Serial.print("data=");
  String data = res->popString();
  Serial.println(data);
}

void mqttPublished(void* response) {
  Serial.println("MQTT published");
}




int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}


// code to display the sketch name/compile date
// http://forum.arduino.cc/index.php?PHPSESSID=82046rhab9h76mt6q7p8fle0u4&topic=118605.msg894017#msg894017

int pgm_lastIndexOf(uint8_t c, const char * p)
{
  int last_index = -1; // -1 indicates no match
  uint8_t b;
  for(int i = 0; true; i++) {
    b = pgm_read_byte(p++);
    if (b == c)
      last_index = i;
    else if (b == 0) break;
  }
  return last_index;
}

// displays at startup the Sketch running in the Arduino
void display_srcfile_details(void){
  const char *the_path = PSTR(__FILE__);           // save RAM, use flash to hold __FILE__ instead

  int slash_loc = pgm_lastIndexOf('/',the_path); // index of last '/' 
  if (slash_loc < 0) slash_loc = pgm_lastIndexOf('\\',the_path); // or last '\' (windows, ugh)

  int dot_loc = pgm_lastIndexOf('.',the_path);   // index of last '.'
  if (dot_loc < 0) dot_loc = pgm_lastIndexOf(0,the_path); // if no dot, return end of string

  Serial.print(F("\n\nSketch: "));  
  for (int i = slash_loc+1; i < dot_loc; i++) {
    uint8_t b = pgm_read_byte(&the_path[i]);
    if (b != 0) Serial.print((char) b);
    else break;
  }

  Serial.print(F(", Compiled on: "));
  Serial.print(F(__DATE__));
  Serial.print(F(" at "));
  Serial.println(F(__TIME__));
}

void showHelp(void) 
{  
  display_srcfile_details(); 
  Serial.print(F("Free RAM:   "));
  Serial.println(freeRam());
  
  
  Serial.println(F("\n"
      "Available commands:\n"

      " speed [s] - set CVE speed [0,1,2,3]\n"
      " status - display current status\n"
      " auto - enable automatic humidity control\n"
      " manual - disable automatic humidity control\n"
      " splo [%] - set low setpoint humidity\n"
      " sphi [%] - set high setpoint humidity\n"
      " delay [s] - set delay [s] before automatic speed change\n"
      "\n"
      " hang - test watchdog\n"
      " disp [c] - display character\n"
      
      " hello [name]\n"
      " p <arg1> <arg2>\n"
//      " d  - toggle debug messages\n"
//      " l  - send lifesign message\n"
      " ?  - show this help\n"
      "\n"));  
}




// the setup function runs once when you press reset or power the board
void setup() {
#ifdef HEARTBEAT_WATCHDOG
  // start hardware watchdog
  wdt_enable(WDTO_8S);
#endif



  // put your setup code here, to run once:
  
#ifdef HEARTBEAT_LED
  pinMode(HEARTBEAT_LED, OUTPUT);
#ifdef HEARTBEAT_INVERTED
  digitalWrite(HEARTBEAT_LED, LOW);
#else
  digitalWrite(HEARTBEAT_LED, HIGH);
#endif  
#endif
#ifdef _ENABLE_HEARTBEAT
  scheduler.timer(TASK_HEARTBEAT, 1);
#endif





  Serial.begin(SERIAL_BAUD);
  showHelp();


  // Sensor setup
  //
  sensor.begin();
  if (sensor.sensorExists()) {
    Serial.print("SI70");
    Serial.print(sensor.getDeviceId());
    sensor.setHeater(false);

    // initialise sensor readings
    si7021_env data = sensor.getHumidityAndTemperature();
    sensor_temperature = data.celsiusHundredths/100.0;
    sensor_humidity = data.humidityBasisPoints/100.0;
  } else {
    Serial.print(F("No SI702x"));
  }
  Serial.println(F(" Humidity Sensor detected"));
  
  scheduler.timer(TASK_MEASURE, 1);
  scheduler.timer(TASK_PUBLISH, 100);
  scheduler.timer(TASK_DISPLAY, 10);


  // I/O setup
  pinMode(RE1, OUTPUT);
  pinMode(RE2, OUTPUT);
  pinMode(RE3, OUTPUT);
  pinMode(RE4, OUTPUT);

  digitalWrite(RE1, HIGH);
  digitalWrite(RE2, HIGH);
  digitalWrite(RE3, HIGH);
  digitalWrite(RE4, HIGH);
 
  pinMode(SW_SPEED2,  INPUT_PULLUP);
  pinMode(SW_SPEED3,  INPUT_PULLUP);


  // Display setup
  //  
  pinMode(DISPLAY_A, OUTPUT);
  pinMode(DISPLAY_B, OUTPUT);
  pinMode(DISPLAY_C, OUTPUT);
  pinMode(DISPLAY_D, OUTPUT);
  pinMode(DISPLAY_E, OUTPUT);
  pinMode(DISPLAY_F, OUTPUT);
  pinMode(DISPLAY_G, OUTPUT);
  pinMode(DISPLAY_DP, OUTPUT);

  setDisplay(' ');
  delay(100); 
  digitalWrite(DISPLAY_DP, HIGH);
  setDisplay('8');
  delay(500);  
  setDisplay(' ');
  delay(500); 
    

  // Setup callbacks for SerialCommand commands
  sCmd.addCommand("hang",   commandHang);     // test for watchdog
  sCmd.addCommand("status", commandStatus);   // status report
  sCmd.addCommand("auto",   commandAuto);   // enable automatic humidity control
  sCmd.addCommand("manual", commandManual);   // disable automatic humidity control
  sCmd.addCommand("splo",   commandSPLow);  
  sCmd.addCommand("sphi",   commandSPHigh); 
  sCmd.addCommand("delay",  commandDelay);
  

  sCmd.addCommand("disp",   commandDisp);    // test display 
  sCmd.addCommand("speed",  commandSpeed);  // Set CVE speed
  
//sCmd.addCommand("hello", commandHello);        // Echos the string argument back
//sCmd.addCommand("p",     processCommand);  // Converts two arguments to integers and echos them back
  sCmd.setDefaultHandler(unrecognized);      // Handler for command that isn't matched  (says "What?")



  Serial.println("EL-Client starting!");
  // Sync-up with esp-link, this is required at the start of any sketch and initializes the
  // callbacks to the wifi status change callback. The callback gets called with the initial
  // status right after Sync() below completes.
  esp.wifiCb.attach(wifiCb); // wifi status change callback, optional (delete if not desired)
  bool ok;
  do {
    ok = esp.Sync();      // sync up with esp-link, blocks for up to 2 seconds
    if (!ok) Serial.println("EL-Client sync failed!");
  } while(!ok);
  Serial.println("EL-Client synced!");

  // Set-up callbacks for events and initialize with es-link.
  mqtt.connectedCb.attach(mqttConnected);
  mqtt.disconnectedCb.attach(mqttDisconnected);
  mqtt.publishedCb.attach(mqttPublished);
  mqtt.dataCb.attach(mqttData);
  mqtt.setup();

  //Serial.println("ARDUINO: setup mqtt lwt");
  //mqtt.lwt("/lwt", "offline", 0, 0); //or mqtt.lwt("/lwt", "offline");

  Serial.println("EL-MQTT ready");
  
  
}


static int count;
static uint32_t last;

void loop() {
  static int display_mode = 0;

  
  esp.Process();

//  if (connected && (millis()-last) > 4000) {
//    Serial.println("publishing");
//    char buf[12];
//
//    itoa(count++, buf, 10);
//    mqtt.publish("esp-link/1", buf);
//
//    itoa(count+99, buf, 10);
//    mqtt.publish("hello/world/arduino", buf);
//
//    uint32_t t = cmd.GetTime();
//    Serial.print("Time: "); Serial.println(t);
//
//    last = millis();
//  }

  

  
//  while (Serial.available())
//    handleInput(Serial.read());
  sCmd.readSerial();     // We don't do much, just process serial commands


  // handle changes from the (hardware) input switch located in kitchen
  if (SwitchChanged()) {
    speed_select_user = GetSwitchState();
    Serial.print(F("input: Switch changed speed to ")); 
    Serial.println(SpeedString(speed_select_user)); 
  }



// set fan speed...
  if (speed_select_humidity != SPEED_UNDEFINED)
    speed_select_fan = speed_select_humidity;
  else
    speed_select_fan = speed_select_user;

  SetFanSpeed(speed_select_fan);
  

  
  
  switch (scheduler.poll()) {

//    case TASK_LIFESIGN:
//      if (debug_txt) {  
//        Serial.println(F("Send Lifesign message"));
//      }
//
//      scheduler.timer(TASK_LIFESIGN, LIFESIGN_INTERVAL);
//      break;

    case TASK_MEASURE:
    
      if (sensor.sensorExists()) {
          si7021_env data = sensor.getHumidityAndTemperature();
          // apply exponential filter to smooth out noise --  ??? smoothing factors configurable???
          sensor_temperature = (0.90 * sensor_temperature) + (0.10 * data.celsiusHundredths/100.0);
          sensor_humidity = (0.90 * sensor_humidity) + (0.10 * data.humidityBasisPoints/100.0);
//          Serial.print(F("Temperature: "));
//          Serial.println(sensor_temperature);
//          Serial.print(F("Humidity:    "));
//          Serial.println(sensor_humidity);
      }


      // humidity control
      if (humidity_control_mode) {    
          
          // start/cancel timer LOW
          if (speed_select_humidity != SPEED_UNDEFINED) {
            if (sensor_humidity < humidity_setpoint_lo) {
              if (scheduler.idle(TASK_HUMIDITY_LOW)) {
                Serial.print(F("dbg: start timer HUMIDITY LOW"));
                scheduler.timer(TASK_HUMIDITY_LOW, humidity_delay * 10);
              }
            } else {
              if (!scheduler.idle(TASK_HUMIDITY_LOW)) {
                Serial.print(F("dbg: cancel timer HUMIDITY LOW"));
                scheduler.cancel(TASK_HUMIDITY_LOW);
              }
            }
          }
          
          // start/cancel timer HIGH
          if (sensor_humidity > humidity_setpoint_hi && speed_select_fan < humidity_max_speed ) {
            if (scheduler.idle(TASK_HUMIDITY_HIGH)) {
              Serial.print(F("dbg: start timer HUMIDITY HIGH"));
              scheduler.timer(TASK_HUMIDITY_HIGH, humidity_delay * 10);
            }
          } else {
            if (!scheduler.idle(TASK_HUMIDITY_HIGH)) {
              Serial.print(F("dbg: cancel timer HUMIDITY HIGH"));
              scheduler.cancel(TASK_HUMIDITY_HIGH);
            }
          }


      } else {
        // manual mode, humidity control disabled    
        if (!scheduler.idle(TASK_HUMIDITY_LOW)) {
          Serial.print(F("dbg: cancel timer HUMIDITY LOW"));
          scheduler.cancel(TASK_HUMIDITY_LOW);
        }

        if (!scheduler.idle(TASK_HUMIDITY_HIGH)) {
          Serial.print(F("dbg: cancel timer HUMIDITY HIGH"));
          scheduler.cancel(TASK_HUMIDITY_HIGH);
        }

        if (speed_select_humidity != SPEED_UNDEFINED) {
          speed_select_humidity = SPEED_UNDEFINED;
        }
      }    

      scheduler.timer(TASK_MEASURE, MEASURE_INTERVAL);
      break;



    case TASK_PUBLISH:

      Serial.print(F("Temperature: "));
      Serial.println(sensor_temperature);
      Serial.print(F("Humidity:    "));
      Serial.println(sensor_humidity);


      if (connected) {
    
      char buf[12];

      dtostrf(sensor_temperature,4,2,buf);
      mqtt.publish("esp-link/cve-controller/temperature", buf);

      dtostrf(sensor_humidity,4,2,buf);
      mqtt.publish("esp-link/cve-controller/humidity", buf);
    

  }


      scheduler.timer(TASK_PUBLISH, PUBLISH_INTERVAL);
      break;
    
      


#ifdef _ENABLE_HEARTBEAT      
    case TASK_HEARTBEAT:
        handleHeartBeat();
      break;
#endif /* _ENABLE_HEARTBEAT */   



    case TASK_HUMIDITY_LOW:
            Serial.println(F("Humidity LOW Timer expired.. time to ramp down!"));
            switch (speed_select_humidity) {
              case SPEED_1:
                speed_select_humidity = SPEED_0;
                break;
              case SPEED_2:
                speed_select_humidity = SPEED_1;
                break;
              case SPEED_3:
                speed_select_humidity = SPEED_2;
                break;
            }
            // set speed override when ramped down to original requested speed
            if (speed_select_humidity <= speed_select_user) {
              speed_select_humidity = SPEED_UNDEFINED;
            }
            
            Serial.print(F("humidity: Changed speed to ")); 
            Serial.println(SpeedString(speed_select_humidity)); 
            break;
    
    case TASK_HUMIDITY_HIGH:
            Serial.println(F("Humidity HIGH Timer expired.. time to ramp up!"));
            if (speed_select_humidity == SPEED_UNDEFINED) {
                speed_select_humidity = speed_select_fan;
            }
            switch (speed_select_humidity) {
              case SPEED_0:
                speed_select_humidity = SPEED_1;
                break;
              case SPEED_1:
                speed_select_humidity = SPEED_2;
                break;
              case SPEED_2:
                speed_select_humidity = SPEED_3;
                break;
            }
            Serial.print(F("humidity: Changed speed to ")); 
            Serial.println(SpeedString(speed_select_humidity)); 
            break;                 


    // update LED display on CVE controller
    // in case Humidity control mode is active the dispay will alternate between H and the fan speed
    case TASK_DISPLAY:
      
      if (display_mode == 0 && speed_select_humidity != SPEED_UNDEFINED) {
          setDisplay('H');         
          display_mode = 1;
      } else {    
          switch(speed_select_fan) {
           case SPEED_0:            
                setDisplay('0');
                break;
           case SPEED_1:            
                setDisplay('1');
                break;
           case SPEED_2:            
                setDisplay('2');
                break;
           case SPEED_3:
                setDisplay('3');
                break;
          } 
          display_mode = 0;
    }
    
    scheduler.timer(TASK_DISPLAY, 5);
    break;

            
  }
  

}



#ifdef _ENABLE_HEARTBEAT
void handleHeartBeat(void)
{
      static int heartbeat_state = 0;

#ifdef HEARTBEAT_WATCHDOG
      wdt_reset(); // reset the hardware watchdog timer
      scheduler.timer(TASK_HEARTBEAT, 20);
#endif
#ifdef HEARTBEAT_LED
      switch (heartbeat_state) {
        case 0:
#ifdef HEARTBEAT_INVERTED
            digitalWrite(HEARTBEAT_LED, LOW);
#else
            digitalWrite(HEARTBEAT_LED, HIGH);
#endif  
          scheduler.timer(TASK_HEARTBEAT, 1);
          heartbeat_state = 1;
          break;

        case 1:
#ifdef HEARTBEAT_INVERTED
            digitalWrite(HEARTBEAT_LED, HIGH);
#else
            digitalWrite(HEARTBEAT_LED, LOW);
#endif  
          scheduler.timer(TASK_HEARTBEAT, 2);
          heartbeat_state = 2;
          break;

        case 2:
#ifdef HEARTBEAT_INVERTED
          digitalWrite(HEARTBEAT_LED, LOW);
#else
          digitalWrite(HEARTBEAT_LED, HIGH);
#endif  
          scheduler.timer(TASK_HEARTBEAT, 1);
          heartbeat_state = 3;
          break;

        case 3:
#ifdef HEARTBEAT_INVERTED
            digitalWrite(HEARTBEAT_LED, HIGH);
#else
            digitalWrite(HEARTBEAT_LED, LOW);
#endif  
          scheduler.timer(TASK_HEARTBEAT, 10);
          heartbeat_state = 0;
          break;                
      }
#endif /* HEARTBEAT_LED */      
#endif /* _ENABLE_HEARTBEAT */
}




//
// Command Handlers
//

void commandHang(void) {
  Serial.println(F("\nHangup... to test watchdog...\n"));
  for (int i=1; i<60; i++) {
    Serial.println(i);
    delay(1000);
  }
  Serial.println(F("Sleepy watchdog???"));
}



String SpeedString(fanspeed_t s) {
    switch(s) {
     case SPEED_UNDEFINED:
      return F("SPEED_UNDEFINED");
     case SPEED_0:
      return F("SPEED_0");
     case SPEED_1:
      return F("SPEED_1");
     case SPEED_2:
      return F("SPEED_2");
     case SPEED_3:
      return F("SPEED_3");
     default:
      return F("Invalid");
    }
}

void commandStatus(void) {
  Serial.println(F("\nStatus:\n"));

  Serial.print(F("Free RAM: "));
  Serial.println(freeRam());

  Serial.print(F("speed_select_user: "));
  Serial.println(SpeedString(speed_select_user));
  
  Serial.print(F("speed_select_humidity: "));
  Serial.println(SpeedString(speed_select_humidity));
  
  Serial.print(F("speed_select_fan: "));
  Serial.println(SpeedString(speed_select_fan));


  Serial.print(F("humidity_control_mode: "));
  Serial.println(humidity_control_mode);
  Serial.print(F("humidity_setpoint_lo: "));
  Serial.println(humidity_setpoint_lo);
  Serial.print(F("humidity_setpoint_hi: "));
  Serial.println(humidity_setpoint_hi);
  Serial.print(F("humidity_delay: "));
  Serial.println(humidity_delay);

  Serial.print(F("Sensor: "));
  if (sensor.sensorExists()) {
    Serial.print("SI70");
    Serial.print(sensor.getDeviceId());
    sensor.setHeater(false);
  } else {
    Serial.print(F("No SI702x"));
  }
  Serial.println(F(" detected"));

  Serial.print("Temperature: ");
  Serial.println(sensor_temperature);
  Serial.print("Humidity: ");
  Serial.println(sensor_humidity);
}



void commandAuto(void) {
  Serial.println(F("mode: auto"));
  humidity_control_mode = 1;
}


void commandManual(void) {
  Serial.println(F("mode: manual"));
  humidity_control_mode = 0;
}


  
void commandSPLow() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    humidity_setpoint_lo = atof(arg);    
    Serial.print(F("humidity_setpoint_lo: "));
    Serial.println(humidity_setpoint_lo);
  }
}


void commandSPHigh() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    humidity_setpoint_hi = atof(arg);    
    Serial.print(F("humidity_setpoint_hi: "));
    Serial.println(humidity_setpoint_hi);
  }
}

void commandDelay() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    humidity_delay = atoi(arg);    
    Serial.print(F("humidity_delay: "));
    Serial.println(humidity_delay);
  }
}



void commandDisp() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    char c = arg[0];
    Serial.print(F("Display: "));
    Serial.println(c);
    setDisplay(c);
  }
}



void commandSpeed() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    char c = arg[0];
       
    fanspeed_t new_speed   = SPEED_UNDEFINED;
    switch(c) {
     case '0':
           new_speed   = SPEED_0;
        break;

     case '1':
           new_speed   = SPEED_1;
        break;

     case '2':
           new_speed   = SPEED_2;
        break;

     case '3':
           new_speed   = SPEED_3;
        break;
    }
  
    if (new_speed != SPEED_UNDEFINED) {  
      Serial.print(F("input: Command changed speed to ")); 
      Serial.println(new_speed); 
      speed_select_user = new_speed;      
    }    
  }
}




void commandHello() {
  char *arg;
  arg = sCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    Serial.print(F("Hello "));
    Serial.println(arg);
  }
  else {
    Serial.println(F("Hello, whoever you are"));
  }
}



void processCommand() {
  int aNumber;
  char *arg;

  Serial.println(F("We're in processCommand"));
  arg = sCmd.next();
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    Serial.print(F("First argument was: "));
    Serial.println(aNumber);
  }
  else {
    Serial.println(F("No arguments"));
  }

  arg = sCmd.next();
  if (arg != NULL) {
    aNumber = atol(arg);
    Serial.print(F("Second argument was: "));
    Serial.println(aNumber);
  }
  else {
    Serial.println(F("No second argument"));
  }
}








// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  showHelp();
}


void setDisplay(char code) {
  switch(code) {
     case '0':
        digitalWrite(DISPLAY_A, LOW);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, LOW);
        digitalWrite(DISPLAY_D, LOW);
        digitalWrite(DISPLAY_E, LOW);
        digitalWrite(DISPLAY_F, LOW);
        digitalWrite(DISPLAY_G, HIGH);
        break;

     case '1':
        digitalWrite(DISPLAY_A, HIGH);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, LOW);
        digitalWrite(DISPLAY_D, HIGH);
        digitalWrite(DISPLAY_E, HIGH);
        digitalWrite(DISPLAY_F, HIGH);
        digitalWrite(DISPLAY_G, HIGH);
        break;

     case '2':
        digitalWrite(DISPLAY_A, LOW);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, HIGH);
        digitalWrite(DISPLAY_D, LOW);
        digitalWrite(DISPLAY_E, LOW);
        digitalWrite(DISPLAY_F, HIGH);
        digitalWrite(DISPLAY_G, LOW);
        break;

     case '3':
        digitalWrite(DISPLAY_A, LOW);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, LOW);
        digitalWrite(DISPLAY_D, LOW);
        digitalWrite(DISPLAY_E, HIGH);
        digitalWrite(DISPLAY_F, HIGH);
        digitalWrite(DISPLAY_G, LOW);
        break;

     case '8':
        digitalWrite(DISPLAY_A, LOW);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, LOW);
        digitalWrite(DISPLAY_D, LOW);
        digitalWrite(DISPLAY_E, LOW);
        digitalWrite(DISPLAY_F, LOW);
        digitalWrite(DISPLAY_G, LOW);
        break;
       

     case 'H':
        digitalWrite(DISPLAY_A, HIGH);
        digitalWrite(DISPLAY_B, LOW);
        digitalWrite(DISPLAY_C, LOW);
        digitalWrite(DISPLAY_D, HIGH);
        digitalWrite(DISPLAY_E, LOW);
        digitalWrite(DISPLAY_F, LOW);
        digitalWrite(DISPLAY_G, LOW);
        break;
       
    default:
        // blank
       digitalWrite(DISPLAY_A, HIGH);
       digitalWrite(DISPLAY_B, HIGH);
       digitalWrite(DISPLAY_C, HIGH);
       digitalWrite(DISPLAY_D, HIGH);
       digitalWrite(DISPLAY_E, HIGH);
       digitalWrite(DISPLAY_F, HIGH);
       digitalWrite(DISPLAY_G, HIGH);
  }
 }
 

 
 





// scanning of the input switch 
bool SwitchChanged(void)
{
  static fanspeed_t lastButtonState = SPEED_UNDEFINED;   // the previous reading from the input pin

// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
  static long lastDebounceTime = 0;  // the last time the output pin was toggled
  long debounceDelay = 50;          // the debounce time; increase if the output flickers

   
  bool changed=false;

  fanspeed_t reading = (fanspeed_t) (1 + digitalRead(SW_SPEED2) + (digitalRead(SW_SPEED3) << 1));

  // Switch label  SW_SPEED2  SW_SPEED3  speed
  //  "1"           0         0          SPEED_1 = 1              
  //  "2"           1         0          SPEED_2 = 2
  //  "3"           0         1          SPEED_3 = 3
  //                1         1          n/a

  // clamp in case for some reason both switches for speed=2 and speed=3 are activated
  if (reading > SPEED_3) 
    reading = SPEED_3;


  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;
      changed=true;
      }
    }
  

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
  return changed;
}


fanspeed_t GetSwitchState(void)
{
  return buttonState;
}


 





bool SetFanSpeed(fanspeed_t newspeed)
{
  static fanspeed_t lastspeed = SPEED_UNDEFINED;
  
  if (newspeed != lastspeed && newspeed != SPEED_UNDEFINED) {
    
    lastspeed = newspeed;  
 
    // debug...
    Serial.print(F("fan: Changed speed to ")); 
    Serial.println(newspeed); 
    
    switch(newspeed) {
        case SPEED_0:
          digitalWrite(RE1, LOW);
          digitalWrite(RE2, HIGH);
          digitalWrite(RE3, HIGH);          
          break;

        case SPEED_1:
          digitalWrite(RE1, HIGH);
          digitalWrite(RE2, HIGH);
          digitalWrite(RE3, HIGH);
          break;

        case SPEED_2:
          digitalWrite(RE1, HIGH);
          digitalWrite(RE2, LOW);
          digitalWrite(RE3, HIGH);
          break;
          
        case SPEED_3:
          digitalWrite(RE1, HIGH);
          digitalWrite(RE2, LOW);
          digitalWrite(RE3, LOW);
          break;          
    }   
  } 
}



