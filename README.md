# Walhalla_Klok
Software for the Arduino which runs the Walhalla clock.

Necessary libraries:
https://github.com/PaulStoffregen/Time
https://github.com/greiman/PetitFS

PetitFS requirements:
Some options need to be defined, but these save both ram and flash at the cost of some functionality.
PetitFS can only use 8.3 style filenames. Our configuration only supports FAT16 using uppercase filenames and has no support for folders.
The usage of PetitFS has only been tested on a 328p based Arduino Uno.

(PetitFS) In pffArduino.h:
SD_CS_PIN should be defined as pin 4

(PetitFS) In pffconf.h:
Only enable _USE_READ to save flash
Only enable _FS_FAT16 to save flash
Disable _USE_LCC, disables the use of lowercase filenames, but saves both ram and flash
_WORD_ACCESS can stay enabled, this uses less flash than disabling it