# Changelog

## What's New

### Version 1.2.0
- Fix the G&W waking up when plugging/unplugging the USB charger
- Genesis/Megadrive emulation improvements :
  - SRAM support
  - Support for games larger than 4MB (Super Street Fighter 2 mapper support)
  - SGDK based games are now working (Xenocrisis, Demons of Asteborg, ...)
  - Various other fixes/improvements
- Fast scroll in games selection after 5s of scrolling (To be improved)
- Add Date/time next to used savestate slots
- Add SRAM file saving support for NES & GB/GBC
- New SRAM management: starting a new game is using sram save (if it exists),
  only one sram file per game is created, all savestates for a game are sharing
  the same sram file. Available on NES, GB/GBC, Genesis, Pokemeon Mini, Zelda 3 and Super Mario World.
  This is allowing to import/export SRAM files with emulators on a computer for example.
- For Amstrad/MSX/Genesis/Nes, set maximum overclocking automatically if no overclocking is set.
  If your device is not well supporting maximum overclocking, set overclocking to intermediate in main menu
  to prevent maximum OC to be applied to these systems
- Use ARM compiler v15.2.rel1
This is a big update, some regression may occurs. Please report them.

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
