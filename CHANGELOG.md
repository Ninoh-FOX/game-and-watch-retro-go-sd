# Changelog

## What's New

### Version 1.3.1

!!!! v1.3.x is a major update, some regression may occurs. Please report them. !!!!

- Fix broken SMS/GG/SG1000/Colecovision emulator
- The SMS/... fix required to make some breaking changes in Pico-8 core, update to pico core v.1.1.3-beta or better ( download it there : https://github.com/Macs75/pico8_gnw_distro )

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
