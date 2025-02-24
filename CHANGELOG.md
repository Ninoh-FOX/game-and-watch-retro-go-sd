# Changelog

## What's New

### Version 0.4.3
- In dual boot configuration, retro-go will always boot until you select to boot OFW in menu
  It requires gnwmanager v0.16.1 or above, install patched OFW using this command :

  ```gnwmanager flash-patch zelda internal_flash_backup_zelda.bin flash_backup_zelda.bin --bootloader```
- Fix small issue with caching progress bar
- Add support for sram saves in Super Mario World and Zelda 3

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
