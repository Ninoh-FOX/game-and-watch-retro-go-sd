# Changelog

## What's New

### Version 0.2.0
- Improved flash caching system to refresh content if the cached file has been modified
- Text that spans multiple lines is now correctly handled (Fix #19)
- Allow covers size different from 128x96, all covers for a given system should still be
  the same size to prevent issues in some coverflow modes.
- Replaced gui items list memory management to only allocate one items list and reuse it
  for each systems. (Fix memory overflow crash)

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
