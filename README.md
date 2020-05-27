# Wake Clock


## States
* Day
* Sleep
* Doze
* Wake

## Improvements
* Use some sort of dynamic sleep. Were we calculate the seconds to the next state. Halve it and use that as the next sleep cycle time. The halving is because of the drifting that the RTC of ESP8266 does so it's not really accurate.

## Credits
Some of the code is inspired on the [Okay-to-Wake Clock](https://hackaday.io/project/171671-improved-okay-to-wake-clock/discussion-145270) by Mike Szczys. 
