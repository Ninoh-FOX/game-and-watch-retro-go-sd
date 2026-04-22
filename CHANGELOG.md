# Changelog

## What's New

### Version 1.3.0

!!!! This is a big update, some regression may occurs. Please report them. !!!!

- Subfolders support : create some subfolders in systems roms (/roms/xxx/) to sort your roms 
- Pico-8 (beta) support ! This update brings support for Pico-8 fantasy console.
  It requires to manually install Pico-8 core by followinf instructions at https://github.com/Macs75/pico8_gnw_distro
  Due to limited RAM and CPU power of the G&W, some games will not run well (or at all)
  Pico-8 Core is still in beta stage, it can crash (in worst case you could have to wait for battery to be empty to be able to restart the console !), savestates are not handled by retro-go, ...
  Copy your .p8 or .png in /roms/pico8
- No more crash if using SD Card (or sd card content) from another G&W (flash cache data will be erased in this case)
- It'll inform user if firware data on the sd cards do not fit the version of retro-go in flash
- Firmware update : improved install speed
- More Genesis/Megadrive emulation improvements :
  - Use of M68K mmap for better performances

## Prerequisites
To install this version, make sure you have:
- A Game & Watch console with a SD card reader and the [Game & Watch Bootloader](https://github.com/sylverb/game-and-watch-bootloader) installed.
- A micro SD card formatted as FAT32 or exFAT.

## Installation Instructions
1. Download the `retro-go_update.bin` file.
2. Copy the `retro-go_update.bin` file to the root directory of your micro SD card.
3. Insert the micro SD card into your Game & Watch.
4. Turn on the Game & Watch and wait for the installation to complete.

Note : To update bootloader you can download [gnw_bootloader.bin ](https://github.com/sylverb/game-and-watch-bootloader/releases/latest/download/gnw_bootloader.bin) and [gnw_bootloader_0x08032000.bin](https://github.com/sylverb/game-and-watch-bootloader/releases/latest/download/gnw_bootloader_0x08032000.bin) and put them in the root folder of your sd card with `retro-go_update.bin`. After booting the console, the standard update will start and bootloader will also be updated. Check "Bootloader Update Steps" section of README.md for more details, but be aware that a bootloader update failure will require jtag connection to rewrite the bootloader.

## Troubleshooting
Use the [issues page](https://github.com/sylverb/game-and-watch-retro-go-sd/issues) to report any problems.
