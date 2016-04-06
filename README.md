# CVE_Controller

Controller for the Itho CVE unit


Current Status: Idea, no working hardware/software

Frank van de Pol, 2016

Objective:
Remote control & monitoring of the CVE Unit

Itho ECO CVE supports a wireless remote control over 868 MHz. Some hacks exist using reverse engineering of the protocol and using a CC1150 tranceiver to communicate with the CVE (http://www.progz.nl/blog/index.php/2014/12/reverse-engineering-remote-itho-cve-eco-rft-part-1/), or by modifying a remote unit and simulating button presses.

Drawback of above approach is that the solution is very specific to the Itho CVE Hardware, and even more important: it does not provide feedback on the current state (eg. local/other control switches).

Solution is a more generic approach using the classic perilex connector to control the unit.

Advantages:
- works with many brands of CVE (eg. Itho, Stork Air, Orcon, Zehnder, Vent-Axia)


  
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
   
  
##  Hardware 
    
### Arduino Mini Pro 5v       

 Set Arduino IDE to:
    Board:      Arduino Pro or Pro Mini
    Processor:  ATMega 328 (5V, 16 MHz)
  

### 4-relay board (with opto isolators)
### 7 segment display (Kingbright SA52-11HWA, common anode)

LED Display:

0 - fan switched off
1 - fan in low speed
2 - fan in medium speed
3 - fan in high speed

H - High Humidity -> setpoint override


Kingbright SA52-11 HWA Pinout


```
 10   9   8   7   6
  |   |   |   |   |

         __a__
       f/    /b
       /__g_/ 
     e/    /c
     /____/  x
       d     dp

  |   |   |   |   |
  1   2   3   4   5
```

![Kingbright SA52-11 HWA Pinout](https://raw.githubusercontent.com/fvdpol/CVE_Controller/master/Hardware/Display/Kingbright%20SA52-11%20HWA.jpg)

| Pin | Function |
| --- | -------- |
| 1   | e        |
| 2   | d        |
| 3   | CA       |
| 4   | c        |
| 5   | dp       |
| 6   | b        |
| 7   | a        |
| 8   | CA       |
| 9   | f        |
| 10  | g        |




### SI7021 I2C Humidity sensor for exhaust air monitoring

### 220V Relay for input monitoring

Coil with 220V; contact switch input on Arduino    
    
    
 




Arduino I/O Assignment

| Pin | Function 
| --- | --------
| A4  |	I2C SDA
| A5  |	I2C SCL
|     |
| A0  | Input A (manual selection Speed=2)
| A1  | Input B (manual selection Speed=3)
|     |
| D2  | Output Relay 1 
| D3  | Output Relay 2 
| D4  | Output Relay 3 
| D5  | Output Relay 4 (tbd) 
|     |
| D6  | Output 7 segment display a
| D7  | Output 7 segment display b
| D8  | Output 7 segment display c
| D9  | Output 7 segment display d
| D10 | Output 7 segment display e
| D11 | Output 7 segment display f
| D12 | Output 7 segment display g
| D13 | Output 7 segment display dp + onboard LED Arduino 
| 
| A2  |	Unassigned
| A3  | Unassigned
| A6  | Unassigned
| A7  | Unassigned





Issue: Watchdog is nog working; when the watchdog gets activated the Arduino Pro hangs instead of performing a reboot

https://andreasrohner.at/posts/Electronics/How-to-make-the-Watchdog-Timer-work-on-an-Arduino-Pro-Mini-by-replacing-the-bootloader/


https://www.arduino.cc/en/Tutorial/ArduinoISP
pins for ISP programming of new bootloader

ISCP Connector
![ISP 6Pin](https://www.arduino.cc/en/uploads/Tutorial/ISP.png)

| Pin | ISP connector  	| ISP Programmer	
| --- | -------------  	| -------------- 
|  1  | MISO		   	| D12				
|  2  | VCC				| 5V 				 
|  3  | SCK 			| D13				
|  4  | MOSI			| D11				
|  5  | /RESET 			| D10				
|  6  | GND 			| GND				

As the Arduino Pro Mini has a 6-pin ISP connector the programming was simply done by using ArduinoISP on an Uno
Connections from Uno (programmer) to the Pro Mini (target) using 6 pin to 6 pin connector, with the exception of the reset line: pin 10 to pin 5 on the target's isp connector. To prevent the Uno auto-reset a 10uF capacitor was installed between Uno's reset pin and ground.




## SI7021 Library

https://github.com/mlsorensen/SI7021

Features

Precision Relative Humidity Sensor:
	Si7013/21: ± 3% RH (maximum) @ 0–80% RH
>>	Si7020/34: ± 4% RH (maximum) @ 0–80% RH
	Si7006: ± 5% RH (maximum) @ 0–80% RH
Temperature Sensor:
>>	Si7013/20/21/34: ±0.4 °C accuracy (maximum) @ -10 to +85 °C
	Si7006: ±1.0 °C accuracy (maximum) @ -10 to +85 °C
0 to 100% RH operating range
Up to -40 to +125 °C operating range

Integrated on-chip heater
Tiny DFN package
	Si7006/13/20/21: 3 mm x 3 mm
	Si7034: 2 mm x 2 mm
Excellent long term stability
Factory calibrated
Optional factory-installed filter/cover
Lifetime protection during reflow and in operation
Protects against contamination from dust, dirt, household chemicals and other liquids
AEC-Q100 automotive qualified (Si7013/20/21/34)


Pinout:

| Pin | Function | Wire Color
| --- | -------- | ----------
| 1   | VIN      | Red
| 2   | GND      | Blue (+shield)
| 3   | SCL      | White
| 4   | SDA      | Yellow


