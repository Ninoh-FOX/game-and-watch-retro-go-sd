# Changelog

## What's New

### Version 1.1.0
This update is mostly the result of ZimM-LostPolygon's work.
He improved experience by implementing a sleep/wake up mechanism instead of a full power off.
It is now working the same way as original firmware on this point.
It is also including few other improvements.
- Power button is putting console in sleep mode (as in original firmware) instead of a deep sleep. This is allowing to have very
quick wake up.
- Fix issue with sd card detection on power on/wake up
- NES mapper 85 VRC7 (Lagrange Point) play audio @ 48KHz when CPU is overclocked
- It is now possible to delete a game from file properties
- Now showing battery level in %
- Improved "game resume" logic after a sleep/wake up : just press any key to resume playing
- Improved messages shown during loading of cores/games
- Fixed various UI bugs

## Prerequisites
To install this version, make sure you have:
- A Game & Watch console with a SD card reader and the [Game & Watch Bootloader](https://github.com/sylverb/game-and-watch-bootloader) installed.
- A micro SD card formatted as FAT32 or exFAT.

## Installation Instructions
1. Download the `retro-go_update.bin` file.
2. Copy the `retro-go_update.bin` file to the root directory of your micro SD card.
3. Insert the micro SD card into your Game & Watch.
4. Turn on the Game & Watch and wait for the installation to complete.

## Troubleshooting
Use the [issues page](https://github.com/sylverb/game-and-watch-retro-go-sd/issues) to report any problems.
