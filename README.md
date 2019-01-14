# OKGoogle-Lamp-Control
Use and ESP-01, Adafruit io and IFTTT to turn a desk lamp on and off

This project uses an ESP8266-01 to control a desk lamp.

Lamp state is maintained in adafruit io

if cloned you will need to rename private_template.h to private.h to store your Adafruit IO credetials
private.h should be in .gitignore to avoid publishing private details to github

Lamp can be controlled with:
  Local switch - which also updates the state at Adafruit io
  MQTT app on phone
  Google Assistant, via IFTTT recipe outputs to Adafruit io
  
This particular lamp is an Ikea Jansjo LED lamp.  The circuit has been inserted between the switch and the lamp head.
The lamp power supply is 4vDC.  I powered the ESP-01 through a diode to get the 0.7 volt drop.

Connections:
Jellybean diode from lamp's power supply positive line to V+ on ESP-01
10uF capacitor between V+ and GND near ESP-01
CH_PD 10k res to V+
Reset 10k res to V+
Pin 2 10k res to V+
Wire soldered to pin 10 on chip for lamp's inline switch.  This has external 10k pullup and is connected to lamp switch with diode to drop voltage.
It is really delicate wiring, but eliminates the need to manage switch state on boot if Pin 2 was used instead.  If Serial usage is removed then Pin 1 or 3 could be used instead.

Pin 0 was tricky.  Needed to be able to drive a transister but still have pin HIGH on boot for normal run operation on powerup
Follow darlington array as illustrated at:
https://electronics.stackexchange.com/questions/300163/pull-up-on-npn-transistor-during-microcontroller-power-up 
This will provide the gain to drive the lamp, but still have a pullup strong enough for normal boot
Instead of relay on second 2n2222 connect the negative line of the IKEA lamp.
The positive line of the ikea lamp should connect directly to the supply positive voltage, bypassing the inline switch.
Switch case can be pried open and some limited connections can be hidden inside.



