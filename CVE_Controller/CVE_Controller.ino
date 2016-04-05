
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
        SP hi,  
      
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

  


*/



#define _ENABLE_HEARTBEAT
#define HEARTBEAT_LED       13   // Arduino Digital 4, JeeNode Port 1-Digital
#define HEARTBEAT_INVERTED
#define HEARTBEAT_WATCHDOG

#ifdef HEARTBEAT_WATCHDOG
#include <avr/wdt.h>
#endif
#include <EEPROM.h>
#include <JeeLib.h>           // https://github.com/jcw/jeelib
#include <SerialCommand.h>    // https://github.com/kroimon/Arduino-SerialCommand

#include <Wire.h>
#include <SI7021.h>           // https://github.com/fvdpol/SI7021


#define SERIAL_BAUD 57600


#define LED_GLOWTIME  1  /* in 0.1s */

#define SENSOR_INTERVAL 50  /* in 0.1s */

enum {
#ifdef _ENABLE_HEARTBEAT 
  TASK_HEARTBEAT,
#endif
  TASK_LIFESIGN,
  TASK_SENSOR,
  TASK_LIMIT };

static word schedBuf[TASK_LIMIT];
Scheduler scheduler (schedBuf, TASK_LIMIT);

bool debug_txt = false;

SI7021 sensor;


#ifdef _ENABLE_HEARTBEAT
int heartbeat_state = 0;
#endif

SerialCommand sCmd;     // The SerialCommand object



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
      
      " on   - led on\n" 
      " off  - led off\n"
      " hang - test watchdog\n"
      " hello [name]\n"
      " p <arg1> <arg2>\n"
#ifdef _ENABLE_DS1820
      " scan - scan bus for DX18x20 sensors\n" 
#endif
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


  
  sensor.begin();
  if (sensor.sensorExists()) {
    Serial.print("SI70");
    Serial.print(sensor.getDeviceId());
    sensor.setHeater(false);
  } else {
    Serial.print(F("No SI702x"));
  }
  Serial.println(F(" Humidity Sensor detected"));
  
  scheduler.timer(TASK_SENSOR, 1);

    

  // Setup callbacks for SerialCommand commands
  sCmd.addCommand("on",    LED_on);          // Turns LED on
  sCmd.addCommand("off",   LED_off);         // Turns LED off
  sCmd.addCommand("hang", Hang);  

#ifdef _ENABLE_DS1820
  sCmd.addCommand("scan",  DS1820_ScanBus);         // 
#endif

  sCmd.addCommand("hello", sayHello);        // Echos the string argument back
  sCmd.addCommand("p",     processCommand);  // Converts two arguments to integers and echos them back
  sCmd.setDefaultHandler(unrecognized);      // Handler for command that isn't matched  (says "What?")



  
}

void loop() {
//  while (Serial.available())
//    handleInput(Serial.read());
  sCmd.readSerial();     // We don't do much, just process serial commands

  
  switch (scheduler.poll()) {

//    case TASK_LIFESIGN:
//      if (debug_txt) {  
//        Serial.println(F("Send Lifesign message"));
//      }
//
//      scheduler.timer(TASK_LIFESIGN, LIFESIGN_INTERVAL);
//      break;

    case TASK_SENSOR:
      if (debug_txt) {  
        Serial.println(F("Send Lifesign message"));
      }
      if (sensor.sensorExists()) {

          si7021_env data = sensor.getHumidityAndTemperature();
          Serial.print("Temperature: ");
          Serial.println(data.celsiusHundredths/100.0);
          Serial.print("Humidity:    ");
          Serial.println(data.humidityBasisPoints/100.0);
      }


      scheduler.timer(TASK_SENSOR, SENSOR_INTERVAL);
      break;


#ifdef _ENABLE_HEARTBEAT      
    case TASK_HEARTBEAT:
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
      break;
#endif /* _ENABLE_HEARTBEAT */   
      
      
      
  }

}




// serial command handlers (examples from library)

void LED_on() {
  Serial.println(F("LED on"));
  //digitalWrite(arduinoLED, HIGH);
}

void LED_off() {
  Serial.println(F("LED off"));
  //digitalWrite(arduinoLED, LOW);
}


void Hang(void) {
  Serial.println(F("\nHangup... to test watchdog...\n"));
  for (int i=1; i<60; i++) {
    Serial.println(i);
    delay(1000);
  }
  Serial.println(F("Sleepy watchdog???"));
}

void sayHello() {
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





