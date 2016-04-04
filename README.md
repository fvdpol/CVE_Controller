# CVE_Controller

Controller for the Itho CVE unit
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

A4	I2C SDA
A5	I2C SCL



D13	Led onboard + 7 segment decimal dot





