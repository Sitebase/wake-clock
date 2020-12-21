# Wake Clock
Simple wake clock to indicate to my son when it's okay to wake up.
![Preview clock](preview.jpg)

## States
* Day
* Sleep
* Doze
* Wake

## Improvements
* Use some sort of dynamic sleep. Were we calculate the seconds to the next state. Halve it and use that as the next sleep cycle time. The halving is because of the drifting that the RTC of ESP8266 does so it's not really accurate.
* Better configuration for the mode timings. Maybe let the wake clock fetch them from a online config file.

## Problems
* GPIO16 and GPIO13 didn't seem to work with pull up

## Credits
Some of the code is inspired on the [Okay-to-Wake Clock](https://hackaday.io/project/171671-improved-okay-to-wake-clock/discussion-145270) by Mike Szczys. 
