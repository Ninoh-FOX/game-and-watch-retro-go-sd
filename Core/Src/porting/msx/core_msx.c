/*************************************************************/
/**                                                         **/
/**                      core_msx.c                         **/
/**                                                         **/
/** This file contains implementation for the MSX-specific  **/
/** hardware: slots, memory mapper, PPIs, VDP, PSG, clock,  **/
/** etc. Initialization code and definitions needed for the **/
/** machine-dependent drivers are also here.                **/
/**                                                         **/
/*************************************************************/
#include <odroid_system.h>

#include "main.h"
#include "appid.h"

#include "common.h"
#include "rom_manager.h"
#include "gw_linker.h"
#include "gw_flash.h"
#include "gw_lcd.h"

#include "MSX.h"
#include "SoundMSX.h"
#include "Floppy.h"
#include "SHA1.h"
#include "MCF.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>

#include "main_msx.h"
#include "core_msx.h"

#define PRINTOK           if(Verbose) puts("OK")
#define PRINTFAILED       if(Verbose) puts("FAILED")
#define PRINTRESULT(R)    if(Verbose) puts((R)? "OK":"FAILED")

#define RGB2INT(R,G,B)    ((B)|((int)(G)<<8)|((int)(R)<<16))

/* MSDOS chdir() is broken and has to be replaced :( */
#ifdef MSDOS
#include "LibMSDOS.h"
#define chdir(path) ChangeDir(path)
#endif

/** User-defined parameters for fMSX *************************/
int  Mode        = MSX_MSX2P|MSX_NTSC|MSX_MSXDOS2|MSX_GUESSA|MSX_GUESSB;
byte Verbose     = 0x01;           /* Debug msgs ON/OFF      */
int  VPeriod     = CPU_VPERIOD;    /* CPU cycles per VBlank  */
int  HPeriod     = CPU_HPERIOD;    /* CPU cycles per HBlank  */
int  RAMPages    = 4;              /* Number of RAM pages    */
int  VRAMPages   = 2;              /* Number of VRAM pages   */
byte ExitNow     = 0;              /* 1 = Exit the emulator  */

/** Main hardware: CPU, RAM, VRAM, mappers *******************/
Z80 MSXCPU;                        /* Z80 CPU state and regs */

byte *VRAM,*VPAGE;                 /* Video RAM              */

byte *RAM[8];                      /* Main RAM (8x8kB pages) */
byte EmptyRAM[0x4000];             /* Empty RAM page (8kB)   */
byte SaveCMOS;                     /* Save CMOS.ROM on exit  */
byte *MemMap[4][4][8];   /* Memory maps [PPage][SPage][Addr] */

byte Bios1[0x8000];
byte Bios2[0x4000];
byte DiskRom[0x4000];
byte RAMMemory[8*0x4000];
byte VRAMMemory[8*0x4000];


byte *RAMData;                     /* RAM Mapper contents    */
byte RAMMapper[4];                 /* RAM Mapper state       */
byte RAMMask;                      /* RAM Mapper mask        */

byte *ROMData[MAXSLOTS];           /* ROM Mapper contents    */
byte ROMMapper[MAXSLOTS][4];       /* ROM Mappers state      */
byte ROMMask[MAXSLOTS];            /* ROM Mapper masks       */
byte ROMType[MAXSLOTS];            /* ROM Mapper types       */

byte EnWrite[4];                   /* 1 if write enabled     */
byte PSL[4],SSL[4];                /* Lists of current slots */
byte PSLReg,SSLReg[4];   /* Storage for A8h port and (FFFFh) */

/** Cassette tape ********************************************/
FILE *CasStream;

/** Cartridge files used by fMSX *****************************/
const char *ROMName[MAXCARTS] = { "CARTA.ROM","CARTB.ROM" };

/** On-cartridge SRAM data ***********************************/
byte *SRAMData[MAXSLOTS];          /* SRAM (battery backed)  */

/** Disk images used by fMSX *********************************/
char currentDiskName[80];           /* Name of current disk */

/** Soundtrack logging ***************************************/
const char *SndName = "LOG.MID";   /* Sound log file         */

/** Fixed font used by fMSX **********************************/
const char *FNTName = "DEFAULT.FNT"; /* Font file for text   */
byte *FontBuf;                     /* Font for text modes    */

/** Kanji font ROM *******************************************/
byte *Kanji;                       /* Kanji ROM 4096x32      */
int  KanLetter;                    /* Current letter index   */
byte KanCount;                     /* Byte count 0..31       */

/** Keyboard and joystick ************************************/
volatile byte KeyState[16];        /* Keyboard map state     */
word JoyState;                     /* Joystick states        */

/** General I/O registers: i8255 *****************************/
I8255 PPI;                         /* i8255 PPI at A8h-ABh   */
byte IOReg;                        /* Storage for AAh port   */

/** Disk controller: WD1793 **********************************/
WD1793 FDC;                        /* WD1793 at 7FF8h-7FFFh  */
FDIDisk FDD[4];                    /* Floppy disk images     */

/** Sound hardware: PSG, SCC, OPLL ***************************/
AY8910 PSG;                        /* PSG registers & state  */
YM2413 OPLL;                       /* OPLL registers & state */
SCC  SCChip;                       /* SCC registers & state  */
byte SCCOn[2];                     /* 1 = SCC page active    */
word FMPACKey;                     /* MAGIC = SRAM active    */

/** Serial I/O hardware: i8251+i8253 *************************/
I8251 SIO;                         /* SIO registers & state  */

/** Real-time clock ******************************************/
byte RTCReg,RTCMode;               /* RTC register numbers   */
byte RTCRegs[4][13];                   /* RTC registers          */

/** Video processor ******************************************/
byte *ChrGen,*ChrTab,*ColTab;      /* VDP tables (screen)    */
byte *SprGen,*SprTab;              /* VDP tables (sprites)   */
int  ChrGenM,ChrTabM,ColTabM;      /* VDP masks (screen)     */
int  SprTabM;                      /* VDP masks (sprites)    */
word VAddr;                        /* VRAM address in VDP    */
byte VKey,PKey;                    /* Status keys for VDP    */
byte FGColor,BGColor;              /* Colors                 */
byte XFGColor,XBGColor;            /* Second set of colors   */
byte ScrMode;                      /* Current screen mode    */
byte VDP[64],VDPStatus[16];        /* VDP registers          */
byte IRQPending;                   /* Pending interrupts     */
int  ScanLine;                     /* Current scanline       */
byte VDPData;                      /* VDP data buffer        */
byte PLatch;                       /* Palette buffer         */
byte ALatch;                       /* Address buffer         */
int  Palette[16];                  /* Current palette        */

/** Places in DiskROM to be patched with ED FE C9 ************/
static const word DiskPatches[] =
{ 0x4010,0x4013,0x4016,0x401C,0x401F,0 };

/** Places in BIOS to be patched with ED FE C9 ***************/
static const word BIOSPatches[] =
{ 0x00E1,0x00E4,0x00E7,0x00EA,0x00ED,0x00F0,0x00F3,0 };

/** Cartridge map, by primary and secondary slots ************/
static const byte CartMap[4][4] =
{ { 255,3,4,5 },{ 0,0,0,0 },{ 1,1,1,1 },{ 2,255,255,255 } };

/** Screen Mode Handlers [number of screens + 1] *************/
void (*RefreshLine[MAXSCREEN+2])(byte Y) =
{
  RefreshLine0,   /* SCR 0:  TEXT 40x24  */
  RefreshLine1,   /* SCR 1:  TEXT 32x24  */
  RefreshLine2,   /* SCR 2:  BLK 256x192 */
  RefreshLine3,   /* SCR 3:  64x48x16    */
  RefreshLine4,   /* SCR 4:  BLK 256x192 */
  RefreshLine5,   /* SCR 5:  256x192x16  */
  RefreshLine6,   /* SCR 6:  512x192x4   */
  RefreshLine7,   /* SCR 7:  512x192x16  */
  RefreshLine8,   /* SCR 8:  256x192x256 */
  0,              /* SCR 9:  NONE        */
  RefreshLine10,  /* SCR 10: YAE 256x192 */
  RefreshLine10,  /* SCR 11: YAE 256x192 */
  RefreshLine12,  /* SCR 12: YJK 256x192 */
  RefreshLineTx80 /* SCR 0:  TEXT 80x24  */
};

/** VDP Address Register Masks *******************************/
static const struct { byte R2,R3,R4,R5,M2,M3,M4,M5; } MSK[MAXSCREEN+2] =
{
  { 0x7F,0x00,0x3F,0x00,0x00,0x00,0x00,0x00 }, /* SCR 0:  TEXT 40x24  */
  { 0x7F,0xFF,0x3F,0xFF,0x00,0x00,0x00,0x00 }, /* SCR 1:  TEXT 32x24  */
  { 0x7F,0x80,0x3C,0xFF,0x00,0x7F,0x03,0x00 }, /* SCR 2:  BLK 256x192 */
  { 0x7F,0x00,0x3F,0xFF,0x00,0x00,0x00,0x00 }, /* SCR 3:  64x48x16    */
  { 0x7F,0x80,0x3C,0xFC,0x00,0x7F,0x03,0x03 }, /* SCR 4:  BLK 256x192 */
  { 0x60,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 5:  256x192x16  */
  { 0x60,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 6:  512x192x4   */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 7:  512x192x16  */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 8:  256x192x256 */
  { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* SCR 9:  NONE        */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 10: YAE 256x192 */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 11: YAE 256x192 */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 12: YJK 256x192 */
  { 0x7C,0xF8,0x3F,0x00,0x03,0x07,0x00,0x00 }  /* SCR 0:  TEXT 80x24  */
};

/** MegaROM Mapper Names *************************************/
static const char *ROMNames[MAXMAPPERS+1] = 
{ 
  "GENERIC/8kB","GENERIC/16kB","KONAMI5/8kB",
  "KONAMI4/8kB","ASCII/8kB","ASCII/16kB",
  "GMASTER2/SRAM","FMPAC/SRAM","UNKNOWN"
};

/** Keyboard Mapping *****************************************/
/** This keyboard mapping is used by KBD_SET()/KBD_RES()    **/
/** macros to modify KeyState[] bits.                       **/
/*************************************************************/
const byte Keys[][2] =
{
  { 0,0x00 },{ 8,0x10 },{ 8,0x20 },{ 8,0x80 }, /* None,LEFT,UP,RIGHT */
  { 8,0x40 },{ 6,0x01 },{ 6,0x02 },{ 6,0x04 }, /* DOWN,SHIFT,CONTROL,GRAPH */
  { 7,0x20 },{ 7,0x08 },{ 6,0x08 },{ 7,0x40 }, /* BS,TAB,CAPSLOCK,SELECT */
  { 8,0x02 },{ 7,0x80 },{ 8,0x08 },{ 8,0x04 }, /* HOME,ENTER,DELETE,INSERT */
  { 6,0x10 },{ 7,0x10 },{ 6,0x20 },{ 6,0x40 }, /* COUNTRY,STOP,F1,F2 */
  { 6,0x80 },{ 7,0x01 },{ 7,0x02 },{ 9,0x08 }, /* F3,F4,F5,PAD0 */
  { 9,0x10 },{ 9,0x20 },{ 9,0x40 },{ 7,0x04 }, /* PAD1,PAD2,PAD3,ESCAPE */
  { 9,0x80 },{ 10,0x01 },{ 10,0x02 },{ 10,0x04 }, /* PAD4,PAD5,PAD6,PAD7 */
  { 8,0x01 },{ 0,0x02 },{ 2,0x01 },{ 0,0x08 }, /* SPACE,[!],["],[#] */
  { 0,0x10 },{ 0,0x20 },{ 0,0x80 },{ 2,0x01 }, /* [$],[%],[&],['] */
  { 1,0x02 },{ 0,0x01 },{ 1,0x01 },{ 1,0x08 }, /* [(],[)],[*],[=] */
  { 2,0x04 },{ 1,0x04 },{ 2,0x08 },{ 2,0x10 }, /* [,],[-],[.],[/] */
  { 0,0x01 },{ 0,0x02 },{ 0,0x04 },{ 0,0x08 }, /* 0,1,2,3 */
  { 0,0x10 },{ 0,0x20 },{ 0,0x40 },{ 0,0x80 }, /* 4,5,6,7 */
  { 1,0x01 },{ 1,0x02 },{ 1,0x80 },{ 1,0x80 }, /* 8,9,[:],[;] */
  { 2,0x04 },{ 1,0x08 },{ 2,0x08 },{ 2,0x10 }, /* [<],[=],[>],[?] */
  { 0,0x04 },{ 2,0x40 },{ 2,0x80 },{ 3,0x01 }, /* [@],A,B,C */
  { 3,0x02 },{ 3,0x04 },{ 3,0x08 },{ 3,0x10 }, /* D,E,F,G */
  { 3,0x20 },{ 3,0x40 },{ 3,0x80 },{ 4,0x01 }, /* H,I,J,K */
  { 4,0x02 },{ 4,0x04 },{ 4,0x08 },{ 4,0x10 }, /* L,M,N,O */
  { 4,0x20 },{ 4,0x40 },{ 4,0x80 },{ 5,0x01 }, /* P,Q,R,S */
  { 5,0x02 },{ 5,0x04 },{ 5,0x08 },{ 5,0x10 }, /* T,U,V,W */
  { 5,0x20 },{ 5,0x40 },{ 5,0x80 },{ 1,0x20 }, /* X,Y,Z,[[] */
  { 1,0x10 },{ 1,0x40 },{ 0,0x40 },{ 1,0x04 }, /* [\],[]],[^],[_] */
  { 2,0x02 },{ 2,0x40 },{ 2,0x80 },{ 3,0x01 }, /* [`],a,b,c */
  { 3,0x02 },{ 3,0x04 },{ 3,0x08 },{ 3,0x10 }, /* d,e,f,g */
  { 3,0x20 },{ 3,0x40 },{ 3,0x80 },{ 4,0x01 }, /* h,i,j,k */
  { 4,0x02 },{ 4,0x04 },{ 4,0x08 },{ 4,0x10 }, /* l,m,n,o */
  { 4,0x20 },{ 4,0x40 },{ 4,0x80 },{ 5,0x01 }, /* p,q,r,s */
  { 5,0x02 },{ 5,0x04 },{ 5,0x08 },{ 5,0x10 }, /* t,u,v,w */
  { 5,0x20 },{ 5,0x40 },{ 5,0x80 },{ 1,0x20 }, /* x,y,z,[{] */
  { 1,0x10 },{ 1,0x40 },{ 2,0x02 },{ 8,0x08 }, /* [|],[}],[~],DEL */
  { 10,0x08 },{ 10,0x10 }                      /* PAD8,PAD9 */
};

/** Internal Functions ***************************************/
/** These functions are defined and internally used by the  **/
/** code in MSX.c.                                          **/
/*************************************************************/
byte *LoadFlashROM(const char *Name,const char *Ext,int Size,byte *Buf);
int  LoadGNWCartName(const char *FileName,const char *Ext,int Slot,int Type, bool isBios);
int  GuessROM(const byte *Buf,int Size);
int  FindState(const char *Name);
void SetMegaROM(int Slot,byte P0,byte P1,byte P2,byte P3);
void MapROM(word A,byte V);       /* Switch MegaROM banks            */
void PSlot(byte V);               /* Switch primary slots            */
void SSlot(byte V);               /* Switch secondary slots          */
void VDPOut(byte R,byte V);       /* Write value into a VDP register */
void PPIOut(byte New,byte Old);   /* Set PPI bits (key click, etc.)  */
int  CheckSprites(void);          /* Check for sprite collisions     */
byte RTCIn(byte R);               /* Read RTC registers              */
byte SetScreen(void);             /* Change screen mode              */
word SetIRQ(byte IRQ);            /* Set/Reset IRQ                   */
word StateID(void);               /* Compute emulation state ID      */

/** msx_run() ************************************************/
/** Allocate memory, load ROM images, initialize hardware,  **/
/** CPU and start the emulation. This function returns 0 in **/
/** the case of failure.                                    **/
/*************************************************************/
int msx_start(int NewMode,int NewRAMPages,int NewVRAMPages, unsigned char *SaveState)
{
  /*** Joystick types: ***/
  static const char *JoyTypes[] =
  {
    "nothing","normal joystick"
  };

  /*** CMOS ROM default values: ***/
  static const byte RTCInit[4][13]  =
  {
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    {  0, 0, 0, 0,40,80,15, 4, 4, 0, 0, 0, 0 },
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  };

  int *T,I,J,K;
  word A;

  /*** STARTUP CODE starts here: ***/

  T=(int *)"\01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
#ifdef LSB_FIRST
  if(*T!=1)
  {
    printf("********** This machine is high-endian. **********\n");
    printf("Take #define LSB_FIRST out and compile fMSX again.\n");
    return(0);
  }
#else
  if(*T==1)
  {
    printf("********* This machine is low-endian. **********\n");
    printf("Insert #define LSB_FIRST and compile fMSX again.\n");
    return(0);
  }
#endif

  /* Zero everyting */
  CasStream   =0;
  FontBuf     = 0;
  RAMData     = 0;
  VRAM        = 0;
  Kanji       = 0;
  SaveCMOS    = 0;
  FMPACKey    = 0x0000;
  ExitNow     = 0;
  currentDiskName[0] = 0;

  /* Zero cartridge related data */
  for(J=0;J<MAXSLOTS;++J)
  {
    ROMMask[J]  = 0;
    ROMData[J]  = 0;
    ROMType[J]  = 0;
    SRAMData[J] = 0;
//    memset(SRAMData[J],0,0x4000);
  }

  /* Initialize 16kB for the empty space (scratch RAM) */
  memset(EmptyRAM,NORAM,0x4000);

  /* Reset memory map to the empty space */
  for(I=0;I<4;++I)
    for(J=0;J<4;++J)
      for(K=0;K<8;++K)
        MemMap[I][J][K]=EmptyRAM;

  /* Set invalid modes and RAM/VRAM sizes before calling reset_msx() */
  Mode      = ~NewMode;
  RAMPages  = 0;
  VRAMPages = 0;

  /* Try resetting MSX, allocating memory, loading ROMs */
  if((reset_msx(NewMode,NewRAMPages,NewVRAMPages)^NewMode)&MSX_MODEL) return(0);
  if(!RAMPages||!VRAMPages) return(0);

  // Initialize CMOS memory
  memcpy(RTCRegs,RTCInit,sizeof(RTCRegs));

  /* Start loading system cartridges */
  J=MAXCARTS;

  /* If MSX2 or better and DiskROM present...  */
  /* ...try loading MSXDOS2 cartridge into 3:0 */
  if(!MODEL(MSX_MSX1)&&OPTION(MSX_MSXDOS2)&&(MemMap[3][1][2]!=EmptyRAM)&&!ROMData[2])
    if(LoadGNWCartName("MSXDOS2","rom",2,MAP_GEN16,true))
      SetMegaROM(2,0,1,ROMMask[J]-1,ROMMask[J]);

  /* Load FMPAC cartridge */
  for(;(J<MAXSLOTS)&&ROMData[J];++J);
  if((J<MAXSLOTS)&&LoadGNWCartName("FMPAC","rom",J,MAP_FMPAC,true)) ++J;

  /* Try loading game if it's a rom */
  if ((strcmp(ACTIVE_FILE->ext,"rom") == 0) ||
      (strcmp(ACTIVE_FILE->ext,"mx1") == 0) ||
      (strcmp(ACTIVE_FILE->ext,"mx2") == 0)) {
    /* Try loading Konami GameMaster 2 cartridge */
    // Todo : allow user to choose to load or not
    for(;(J<MAXSLOTS)&&ROMData[J];++J);
    if(J<MAXSLOTS)
    {
      if(LoadGNWCartName("GMASTER2","rom",J,MAP_GMASTER2,true)) ++J;
    }
    //We have loaded in slot 2-5, now we can load games roms in slot 0-1
    if (LoadGNWCartName(ACTIVE_FILE->name,ACTIVE_FILE->ext,0,ROMGUESS(0)|ROMTYPE(0),false)) ++J;
  }

  if(Verbose) printf("Initialize floppy disk controller\n");

  /* Initialize floppy disk controller */
  Reset1793(&FDC,FDD,WD1793_INIT);
  FDC.Verbose=Verbose&0x04;

  /* If selected game is a disk, try loading it */
  if (strcmp(ACTIVE_FILE->ext,"fdi") == 0) {
    FDD[0].Verbose=Verbose&0x04;
    if(msx_change_disk(0,ACTIVE_FILE->name))
      if(Verbose) printf("Inserting %s into drive %c\n",ACTIVE_FILE->name,0+'A');
  }

  // Load State if needed
  if (SaveState) {
    LoadMsxStateFlash(SaveState);
  }

  /* Done with initialization */
  if(Verbose)
  {
    printf("Initializing VDP, FDC, PSG, OPLL, SCC, and CPU...\n");
    printf("  Attached %s to joystick port A\n",JoyTypes[JOYTYPE(0)]);
    printf("  Attached %s to joystick port B\n",JoyTypes[JOYTYPE(1)]);
    printf("  %d CPU cycles per HBlank\n",HPeriod);
    printf("  %d CPU cycles per VBlank\n",VPeriod);
    printf("  %d scanlines\n",VPeriod/HPeriod);
  }

  /* Start execution of the code */
  if(Verbose) printf("RUNNING ROM CODE...\n");
  A=RunZ80(&MSXCPU);

  /* Exiting emulation... */
  if(Verbose) printf("EXITED at PC = %04Xh.\n",A);
  return(1);
}

/** TrashMSX() ***********************************************/
/** Free resources allocated by StartMSX().                 **/
/*************************************************************/
void TrashMSX(void)
{
  /* Shut down sound logging */
  TrashMIDI();

  /* Eject disks, free disk buffers */
  Reset1793(&FDC,FDD,WD1793_EJECT);
}

/** reset_msx() **********************************************/
/** Reset MSX hardware to new operating modes. Returns new  **/
/** modes, possibly not the same as NewMode.                **/
/*************************************************************/
int reset_msx(int NewMode,int NewRAMPages,int NewVRAMPages)
{
  /*** VDP status register states: ***/
  static const byte VDPSInit[16] = { 0x9F,0,0x6C,0,0,0,0,0,0,0,0,0,0,0,0,0 };

  /*** VDP control register states: ***/
  static const byte VDPInit[64]  =
  {
    0x00,0x10,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
  };

  /*** Initial palette: ***/
  static const unsigned int PalInit[16] =
  {
    0x00000000,0x00000000,0x0020C020,0x0060E060,
    0x002020E0,0x004060E0,0x00A02020,0x0040C0E0,
    0x00E02020,0x00E06060,0x00C0C020,0x00C0C080,
    0x00208020,0x00C040A0,0x00A0A0A0,0x00E0E0E0
  };

  byte *P1,*P2;
  int J,I;

  /* If changing hardware model, load new system ROMs */
  if((Mode^NewMode)&MSX_MODEL)
  {
    switch(NewMode&MSX_MODEL)
    {
      case MSX_MSX1:
        if(Verbose) printf("  Opening MSX.ROM...\n");
        P1=LoadFlashROM("MSX","rom",0x8000,Bios1);
        PRINTRESULT(P1);
        if(!P1) NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        else
        {
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=EmptyRAM;
          MemMap[3][1][1]=EmptyRAM;
        }
        break;

      case MSX_MSX2:
        if(Verbose) printf("  Opening MSX2.ROM...\n");
        P1=LoadFlashROM("MSX2","rom",0x8000,Bios1);
        PRINTRESULT(P1);
        if(Verbose) printf("  Opening MSX2EXT.ROM...\n");
        P2=LoadFlashROM("MSX2EXT","rom",0x4000,Bios2);
        PRINTRESULT(P2);
        if(!P1||!P2) 
        {
          NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        }
        else
        {
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=P2;
          MemMap[3][1][1]=P2+0x2000;
        }
        break;

      case MSX_MSX2P:
        if(Verbose) printf("  Opening MSX2P.ROM...\n");
        P1=LoadFlashROM("MSX2P","rom",0x8000,Bios1);
        PRINTRESULT(P1);
        if(Verbose) printf("  Opening MSX2PEXT.ROM...\n");
        P2=LoadFlashROM("MSX2PEXT","rom",0x4000,Bios2);
        PRINTRESULT(P2);
        if(!P1||!P2) 
        {
          NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        }
        else
        {
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=P2;
          MemMap[3][1][1]=P2+0x2000;
        }
        break;

      default:
        /* Unknown MSX model, keep old model */
        if(Verbose) printf("reset_msx(): INVALID HARDWARE MODEL!\n");
        NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        break;
    }
  }

  /* If hardware model changed ok, patch freshly loaded BIOS */
  if((Mode^NewMode)&MSX_MODEL)
  {
    /* Apply patches to BIOS */
    if(Verbose) printf("  Patching BIOS: ");
    for(J=0;BIOSPatches[J];++J)
    {
      if(Verbose) printf("%04X..",BIOSPatches[J]);
      P1=MemMap[0][0][0]+BIOSPatches[J];
      P1[0]=0xED;P1[1]=0xFE;P1[2]=0xC9;
    }
    if(Verbose) printf("\n");
    PRINTOK;
  }

  /* If toggling BDOS patches... */
  if((Mode^NewMode)&MSX_PATCHBDOS)
  {
    /* Try loading DiskROM */
    if(Verbose) printf("  Opening DISK.ROM...\n");
    P1=LoadFlashROM("DISK","rom",0x4000,DiskRom);
    PRINTRESULT(P1);

    /* If failed loading DiskROM, ignore the new PATCHBDOS bit */
    if(!P1) NewMode=(NewMode&~MSX_PATCHBDOS)|(Mode&MSX_PATCHBDOS);
    else
    {
      /* Assign new DiskROM */
      MemMap[3][1][2]=P1;
      MemMap[3][1][3]=P1+0x2000;

      /* If BDOS patching requested... */
      if(NewMode&MSX_PATCHBDOS)
      {
        if(Verbose) printf("  Patching BDOS: ");
        /* Apply patches to BDOS */
        for(J=0;DiskPatches[J];++J)
        {
          if(Verbose) printf("%04X..",DiskPatches[J]);
          P2=P1+DiskPatches[J]-0x4000;
          P2[0]=0xED;P2[1]=0xFE;P2[2]=0xC9;
        }
        if(Verbose) printf("\n");
        PRINTOK;
      }
    }
  }

  /* Assign new modes */
  Mode           = NewMode;

  /* Set ROM types for cartridges A/B */
  ROMType[0]     = ROMTYPE(0);
  ROMType[1]     = ROMTYPE(1);

  /* Set CPU timings */
  VPeriod        = (VIDEO(MSX_PAL)? VPERIOD_PAL:VPERIOD_NTSC)/6;
  HPeriod        = HPERIOD/6;
  MSXCPU.IPeriod    = CPU_H240;
  MSXCPU.IAutoReset = 0;

  /* Numbers of RAM/VRAM pages should be power of 2 */
  for(J=1;J<NewRAMPages;J<<=1);
  NewRAMPages=J;
  for(J=1;J<NewVRAMPages;J<<=1);
  NewVRAMPages=J;

  /* Correct RAM and VRAM sizes */
  if((NewRAMPages<(MODEL(MSX_MSX1)? 4:8))||(NewRAMPages>256))
    NewRAMPages=MODEL(MSX_MSX1)? 4:8;
  if((NewVRAMPages<(MODEL(MSX_MSX1)? 2:8))||(NewVRAMPages>8))
    NewVRAMPages=MODEL(MSX_MSX1)? 2:8;

  /* If changing amount of RAM... */
  if(NewRAMPages!=RAMPages)
  {
    if(Verbose) printf("Initializing %dkB for RAM...",NewRAMPages*16);
    P1=RAMMemory;
    memset(P1,NORAM,NewRAMPages*0x4000);
    RAMPages = NewRAMPages;
    RAMMask  = NewRAMPages-1;
    RAMData  = P1;
  }

  /* If changing amount of VRAM... */
  if(NewVRAMPages!=VRAMPages)
  {
    if(Verbose) printf("Initializing %dkB for VRAM...",NewVRAMPages*16);
    P1=VRAMMemory;
    memset(P1,0x00,NewVRAMPages*0x4000);
    VRAMPages = NewVRAMPages;
    VRAM      = P1;
  }

  /* For all slots... */
  for(J=0;J<4;++J)
  {
    /* Slot is currently read-only */
    EnWrite[J]          = 0;
    /* PSL=0:0:0:0, SSL=0:0:0:0 */
    PSL[J]              = 0;
    SSL[J]              = 0;
    /* RAMMap=3:2:1:0 */
    MemMap[3][2][J*2]   = RAMData+(3-J)*0x4000;
    MemMap[3][2][J*2+1] = MemMap[3][2][J*2]+0x2000;
    RAMMapper[J]        = 3-J;
    /* Setting address space */
    RAM[J*2]            = MemMap[0][0][J*2];
    RAM[J*2+1]          = MemMap[0][0][J*2+1];
  }

  /* For all MegaROMs... */
  for(J=0;J<MAXSLOTS;++J)
    if((I=ROMMask[J]+1)>4)
    {
      /* For normal MegaROMs, set first four pages */
      if((ROMData[J][0]=='A')&&(ROMData[J][1]=='B'))
        SetMegaROM(J,0,1,2,3);
      /* Some MegaROMs default to last pages on reset */
      else if((ROMData[J][(I-2)<<13]=='A')&&(ROMData[J][((I-2)<<13)+1]=='B'))
        SetMegaROM(J,I-2,I-1,I-2,I-1);
      /* If 'AB' signature is not found at the beginning or the end */
      /* then it is not a MegaROM but rather a plain 64kB ROM       */
    }

  /* Reset sound chips */
  Reset8910(&PSG,PSG_CLOCK,0);
  ResetSCC(&SCChip,AY8910_CHANNELS);
  Reset2413(&OPLL,AY8910_CHANNELS);
  Sync8910(&PSG,AY8910_SYNC);
  SyncSCC(&SCChip,SCC_SYNC);
  Sync2413(&OPLL,YM2413_SYNC);

  /* Reset PPI chips and slot selectors */
  Reset8255(&PPI);
  PPI.Rout[0]=PSLReg=0x00;
  PPI.Rout[2]=IOReg=0x00;
  SSLReg[0]=0x00;
  SSLReg[1]=0x00;
  SSLReg[2]=0x00;
  SSLReg[3]=0x00;

  /* Reset floppy disk controller */
  Reset1793(&FDC,FDD,WD1793_KEEP);

  /* Reset VDP */
  memcpy(VDP,VDPInit,sizeof(VDP));
  memcpy(VDPStatus,VDPSInit,sizeof(VDPStatus));

  /* Reset keyboard */
  memset((void *)KeyState,0xFF,16);

  /* Set initial palette */
  for(J=0;J<16;++J)
  {
    Palette[J]=PalInit[J];
    SetColor(J,(Palette[J]>>16)&0xFF,(Palette[J]>>8)&0xFF,Palette[J]&0xFF);
  }

  IRQPending=0x00;                      /* No IRQs pending  */
  SCCOn[0]=SCCOn[1]=0;                  /* SCCs off for now */
  RTCReg=RTCMode=0;                     /* Clock registers  */
  KanCount=0;KanLetter=0;               /* Kanji extension  */
  ChrTab=ColTab=ChrGen=VRAM;            /* VDP tables       */
  SprTab=SprGen=VRAM;
  ChrTabM=ColTabM=ChrGenM=SprTabM=~0;   /* VDP addr. masks  */
  VPAGE=VRAM;                           /* VRAM page        */
  FGColor=BGColor=XFGColor=XBGColor=0;  /* VDP colors       */
  ScrMode=0;                            /* Screen mode      */
  VKey=PKey=1;                          /* VDP keys         */
  VAddr=0x0000;                         /* VRAM access addr */
  ScanLine=0;                           /* Current scanline */
  VDPData=NORAM;                        /* VDP data buffer  */
  JoyState=0;                           /* Joystick state   */

  /* Set "V9958" VDP version for MSX2+ */
  if(MODEL(MSX_MSX2P)) VDPStatus[1]|=0x04;

  /* Reset CPU */
  ResetZ80(&MSXCPU);

  /* Done */
  return(Mode);
}

/** RdZ80() **************************************************/
/** Z80 emulation calls this function to read a byte from   **/
/** address A in the Z80 address space. Also see OpZ80() in **/
/** Z80.c which is a simplified code-only RdZ80() version.  **/
/*************************************************************/
byte RdZ80(word A)
{
  /* Filter out everything but [xx11 1111 1xxx 1xxx] */
  if((A&0x3F88)!=0x3F88) return(RAM[A>>13][A&0x1FFF]);

  /* Secondary slot selector */
  if(A==0xFFFF) return(~SSLReg[PSL[3]]);

  /* Floppy disk controller */
  /* 7FF8h..7FFFh Standard DiskROM  */
  /* BFF8h..BFFFh MSX-DOS BDOS      */
  /* 7F80h..7F87h Arabic DiskROM    */
  /* 7FB8h..7FBFh SV738/TechnoAhead */
  if((PSL[A>>14]==3)&&(SSL[A>>14]==1))
    switch(A)
    {
      /* Standard      MSX-DOS       Arabic        SV738            */
      case 0x7FF8: case 0xBFF8: case 0x7F80: case 0x7FB8: /* STATUS */
      case 0x7FF9: case 0xBFF9: case 0x7F81: case 0x7FB9: /* TRACK  */
      case 0x7FFA: case 0xBFFA: case 0x7F82: case 0x7FBA: /* SECTOR */
      case 0x7FFB: case 0xBFFB: case 0x7F83: case 0x7FBB: /* DATA   */
        return(Read1793(&FDC,A&0x0003));
      case 0x7FFF: case 0xBFFF: case 0x7F84: case 0x7FBC: /* SYSTEM */
        return(Read1793(&FDC,WD1793_READY));
    }

  /* Default to reading memory */
  return(RAM[A>>13][A&0x1FFF]);
}

/** WrZ80() **************************************************/
/** Z80 emulation calls this function to write byte V to    **/
/** address A of Z80 address space.                         **/
/*************************************************************/
void WrZ80(word A,byte V)
{
  /* Secondary slot selector */
  if(A==0xFFFF) { SSlot(V);return; }

  /* Floppy disk controller */
  /* 7FF8h..7FFFh Standard DiskROM  */
  /* BFF8h..BFFFh MSX-DOS BDOS      */
  /* 7F80h..7F87h Arabic DiskROM    */
  /* 7FB8h..7FBFh SV738/TechnoAhead */
  if(((A&0x3F88)==0x3F88)&&(PSL[A>>14]==3)&&(SSL[A>>14]==1))
    switch(A)
    {
      /* Standard      MSX-DOS       Arabic        SV738             */
      case 0x7FF8: case 0xBFF8: case 0x7F80: case 0x7FB8: /* COMMAND */
      case 0x7FF9: case 0xBFF9: case 0x7F81: case 0x7FB9: /* TRACK   */
      case 0x7FFA: case 0xBFFA: case 0x7F82: case 0x7FBA: /* SECTOR  */
      case 0x7FFB: case 0xBFFB: case 0x7F83: case 0x7FBB: /* DATA    */
        Write1793(&FDC,A&0x0003,V);
        return;
      case 0xBFFC: /* Standard/MSX-DOS */
      case 0x7FFC: /* Side: [xxxxxxxS] */
        Write1793(&FDC,WD1793_SYSTEM,FDC.Drive|S_DENSITY|(V&0x01? 0:S_SIDE));
        return;
      case 0xBFFD: /* Standard/MSX-DOS  */
      case 0x7FFD: /* Drive: [xxxxxxxD] */
        Write1793(&FDC,WD1793_SYSTEM,(V&0x01)|S_DENSITY|(FDC.Side? 0:S_SIDE));
        return;
      case 0x7FBC: /* Arabic/SV738 */
      case 0x7F84: /* Side/Drive/Motor: [xxxxMSDD] */
        Write1793(&FDC,WD1793_SYSTEM,(V&0x03)|S_DENSITY|(V&0x04? 0:S_SIDE));
        return;
    }

  /* Write to RAM, if enabled */
  if(EnWrite[A>>14]) { RAM[A>>13][A&0x1FFF]=V;return; }

  /* Switch MegaROM pages */
  if((A>0x3FFF)&&(A<0xC000)) MapROM(A,V);
}

/** InZ80() **************************************************/
/** Z80 emulation calls this function to read a byte from   **/
/** a given I/O port.                                       **/
/*************************************************************/
byte InZ80(word Port)
{
  /* MSX only uses 256 IO ports */
  Port&=0xFF;

  /* Return an appropriate port value */
  switch(Port)
  {

case 0x90: return(0xFD);                   /* Printer READY signal */
case 0xB5: return(RTCIn(RTCReg));          /* RTC registers        */

case 0xA8: /* Primary slot state   */
case 0xA9: /* Keyboard port        */
case 0xAA: /* General IO register  */
case 0xAB: /* PPI control register */
  PPI.Rin[1]=KeyState[PPI.Rout[2]&0x0F];
  return(Read8255(&PPI,Port-0xA8));

case 0xFC: /* Mapper page at 0000h */
case 0xFD: /* Mapper page at 4000h */
case 0xFE: /* Mapper page at 8000h */
case 0xFF: /* Mapper page at C000h */
  return(RAMMapper[Port-0xFC]|~RAMMask);

case 0xD9: /* Kanji support */
  Port=Kanji? Kanji[KanLetter+KanCount]:NORAM;
  KanCount=(KanCount+1)&0x1F;
  return(Port);

case 0x80: /* SIO data */
case 0x81:
case 0x82:
case 0x83:
case 0x84:
case 0x85:
case 0x86:
case 0x87:
  return(NORAM);
  /*return(Rd8251(&SIO,Port&0x07));*/

case 0x98: /* VRAM read port */
  /* Read from VRAM data buffer */
  Port=VDPData;
  /* Reset VAddr latch sequencer */
  VKey=1;
  /* Fill data buffer with a new value */
  VDPData=VPAGE[VAddr];
  /* Increment VRAM address */
  VAddr=(VAddr+1)&0x3FFF;
  /* If rolled over, modify VRAM page# */
  if(!VAddr&&(ScrMode>3))
  {
    VDP[14]=(VDP[14]+1)&(VRAMPages-1);
    VPAGE=VRAM+((int)VDP[14]<<14);
  }
  return(Port);

case 0x99: /* VDP status registers */
  /* Read an appropriate status register */
  Port=VDPStatus[VDP[15]];
  /* Reset VAddr latch sequencer */
// @@@ This breaks Sir Lancelot on ColecoVision, so it must be wrong! 
//  VKey=1;
  /* Update status register's contents */
  switch(VDP[15])
  {
    case 0: VDPStatus[0]&=0x5F;SetIRQ(~INT_IE0);break;
    case 1: VDPStatus[1]&=0xFE;SetIRQ(~INT_IE1);break;
    case 7: VDPStatus[7]=VDP[44]=VDPRead();break;
  }
  /* Return the status register value */
  return(Port);

case 0xA2: /* PSG input port */
  /* PSG[14] returns joystick data */
  if(PSG.Latch==14)
  {
    int L,J;

    /* Number of a joystick port */
    Port = (PSG.R[15]&0x40)>>6;
    L    = JOYTYPE(Port);

    /* If no joystick, return dummy value */
    if(L==JOY_NONE) return(0x7F);

    /* Get joystick state */
    J=~(Port? (JoyState>>8):JoyState)&0x3F;
    Port=PSG.R[15]&(0x10<<Port)? 0x3F:J;

    /* 6th bit is always 1 */
    return(Port|0x40);
  }

  /* PSG[15] resets mouse counters (???) */
  if(PSG.Latch==15)
  {
    /* @@@ For debugging purposes */
    /*printf("Reading from PSG[15]\n");*/

    return(PSG.R[15]&0xF0);
  }

  /* Return PSG[0-13] as they are */
  return(RdData8910(&PSG));

case 0xD0: /* FDC status  */
case 0xD1: /* FDC track   */
case 0xD2: /* FDC sector  */
case 0xD3: /* FDC data    */
case 0xD4: /* FDC IRQ/DRQ */
  /* Brazilian DiskROM I/O ports */
  return(Read1793(&FDC,Port-0xD0));

  }

  /* Return NORAM for non-existing ports */
  if(Verbose&0x20) printf("I/O: Read from unknown PORT[%02Xh]\n",Port);
  return(NORAM);
}

/** OutZ80() *************************************************/
/** Z80 emulation calls this function to write byte V to a  **/
/** given I/O port.                                         **/
/*************************************************************/
void OutZ80(word Port,byte Value)
{
  register byte I,J;

  Port&=0xFF;
  switch(Port)
  {

case 0x7C: WrCtrl2413(&OPLL,Value);return;        /* OPLL Register# */
case 0x7D: WrData2413(&OPLL,Value);return;        /* OPLL Data      */
case 0x91: return;                                /* Printer Data   */
case 0xA0: WrCtrl8910(&PSG,Value);return;         /* PSG Register#  */
case 0xB4: RTCReg=Value&0x0F;return;              /* RTC Register#  */ 

case 0xD8: /* Upper bits of Kanji ROM address */
  KanLetter=(KanLetter&0x1F800)|((int)(Value&0x3F)<<5);
  KanCount=0;
  return;

case 0xD9: /* Lower bits of Kanji ROM address */
  KanLetter=(KanLetter&0x007E0)|((int)(Value&0x3F)<<11);
  KanCount=0;
  return;

case 0x80: /* SIO data */
case 0x81:
case 0x82:
case 0x83:
case 0x84:
case 0x85:
case 0x86:
case 0x87:
  return;
  /*Wr8251(&SIO,Port&0x07,Value);
  return;*/

case 0x98: /* VDP Data */
  VKey=1;
  VDPData=VPAGE[VAddr]=Value;
  VAddr=(VAddr+1)&0x3FFF;
  /* If VAddr rolled over, modify VRAM page# */
  if(!VAddr&&(ScrMode>3)) 
  {
    VDP[14]=(VDP[14]+1)&(VRAMPages-1);
    VPAGE=VRAM+((int)VDP[14]<<14);
  }
  return;

case 0x99: /* VDP Address Latch */
  if(VKey) { ALatch=Value;VKey=0; }
  else
  {
    VKey=1;
    switch(Value&0xC0)
    {
      case 0x80:
        /* Writing into VDP registers */
        VDPOut(Value&0x3F,ALatch);
        break;
      case 0x00:
      case 0x40:
        /* Set the VRAM access address */
        VAddr=(((word)Value<<8)+ALatch)&0x3FFF;
        /* When set for reading, perform first read */
        if(!(Value&0x40))
        {
          VDPData=VPAGE[VAddr];
          VAddr=(VAddr+1)&0x3FFF;
          if(!VAddr&&(ScrMode>3))
          {
            VDP[14]=(VDP[14]+1)&(VRAMPages-1);
            VPAGE=VRAM+((int)VDP[14]<<14);
          }
        }
        break;
    }
  }
  return;

case 0x9A: /* VDP Palette Latch */
  if(PKey) { PLatch=Value;PKey=0; }
  else
  {
    byte R,G,B;
    /* New palette entry written */
    PKey=1;
    J=VDP[16];
    /* Compute new color components */
    R=(PLatch&0x70)*255/112;
    G=(Value&0x07)*255/7;
    B=(PLatch&0x07)*255/7;
    /* Set new color for palette entry J */
    Palette[J]=RGB2INT(R,G,B);
    SetColor(J,R,G,B);
    /* Next palette entry */
    VDP[16]=(J+1)&0x0F;
  }
  return;

case 0x9B: /* VDP Register Access */
  J=VDP[17]&0x3F;
  if(J!=17) VDPOut(J,Value);
  if(!(VDP[17]&0x80)) VDP[17]=(J+1)&0x3F;
  return;

case 0xA1: /* PSG Data */
  /* Put value into a register */
  WrData8910(&PSG,Value);
  return;

case 0xA8: /* Primary slot state   */
case 0xA9: /* Keyboard port        */
case 0xAA: /* General IO register  */
case 0xAB: /* PPI control register */
  /* Write to PPI */
  Write8255(&PPI,Port-0xA8,Value);
  /* If general I/O register has changed... */
  if(PPI.Rout[2]!=IOReg) { PPIOut(PPI.Rout[2],IOReg);IOReg=PPI.Rout[2]; }
  /* If primary slot state has changed... */
  if(PPI.Rout[0]!=PSLReg) PSlot(PPI.Rout[0]);
  /* Done */  
  return;

case 0xB5: /* RTC Data */
  if(RTCReg<13)
  {
    /* J = register bank# now */
    J=RTCMode&0x03;
    /* Store the value */
    RTCRegs[J][RTCReg]=Value;
    /* If CMOS modified, we need to save it */
    if(J>1) SaveCMOS=1;
    return;
  }
  /* RTCRegs[13] is a register bank# */
  if(RTCReg==13) RTCMode=Value;
  return;

case 0xD0: /* FDC command */
case 0xD1: /* FDC track   */
case 0xD2: /* FDC sector  */
case 0xD3: /* FDC data    */
  /* Brazilian DiskROM I/O ports */
  Write1793(&FDC,Port-0xD0,Value);
  return;

case 0xD4: /* FDC system  */
  /* Brazilian DiskROM drive/side: [xxxSxxDx] */
  Value=((Value&0x02)>>1)|S_DENSITY|(Value&0x10? 0:S_SIDE);
  Write1793(&FDC,WD1793_SYSTEM,Value);
  return;

case 0xFC: /* Mapper page at 0000h */
case 0xFD: /* Mapper page at 4000h */
case 0xFE: /* Mapper page at 8000h */
case 0xFF: /* Mapper page at C000h */
  J=Port-0xFC;
  Value&=RAMMask;
  if(RAMMapper[J]!=Value)
  {
    if(Verbose&0x08) printf("RAM-MAPPER: block %d at %Xh\n",Value,J*0x4000);
    I=J<<1;
    RAMMapper[J]      = Value;
    MemMap[3][2][I]   = RAMData+((int)Value<<14);
    MemMap[3][2][I+1] = MemMap[3][2][I]+0x2000;
    if((PSL[J]==3)&&(SSL[J]==2))
    {
      EnWrite[J] = 1;
      RAM[I]     = MemMap[3][2][I];
      RAM[I+1]   = MemMap[3][2][I+1];
    }
  }
  return;

  }

  /* Unknown port */
  if(Verbose&0x20)
    printf("I/O: Write to unknown PORT[%02Xh]=%02Xh\n",Port,Value);
}

/** MapROM() *************************************************/
/** Switch ROM Mapper pages. This function is supposed to   **/
/** be called when ROM page registers are written to.       **/
/*************************************************************/
void MapROM(register word A,register byte V)
{
  byte I,J,PS,SS,*P;

/* @@@ For debugging purposes
printf("(%04Xh) = %02Xh at PC=%04Xh\n",A,V,MSXCPU.PC.W);
*/

  J  = A>>14;           /* 16kB page number 0-3  */
  PS = PSL[J];          /* Primary slot number   */
  SS = SSL[J];          /* Secondary slot number */
  I  = CartMap[PS][SS]; /* Cartridge number      */

  /* Drop out if no cartridge in that slot */
  if(I>=MAXSLOTS) return;

  /* SCC: enable/disable for no cart */
  if(!ROMData[I]&&(A==0x9000)) SCCOn[I]=(V==0x3F)? 1:0;

  /* If writing to SCC... */
  if(SCCOn[I]&&((A&0xDF00)==0x9800))
  {
    /* Compute SCC register number */
    J=A&0x00FF;

    /* If using SCC+... */
    if(A&0x2000)
    {
      /* When no MegaROM present, we allow the program */
      /* to write into SCC wave buffer using EmptyRAM  */
      /* as a scratch pad.                             */
      if(!ROMData[I]&&(J<0xA0)) EmptyRAM[0x1800+J]=V;
   
      /* Output data to SCC chip */
      WriteSCCP(&SCChip,J,V);
    }
    else
    {
      /* When no MegaROM present, we allow the program */
      /* to write into SCC wave buffer using EmptyRAM  */
      /* as a scratch pad.                             */
      if(!ROMData[I]&&(J<0x80)) EmptyRAM[0x1800+J]=V;
   
      /* Output data to SCC chip */
      WriteSCC(&SCChip,J,V);
    }

    /* Done writing to SCC */   
    return;
  }

  /* If no cartridge or no mapper, exit */
  if(!ROMData[I]||!ROMMask[I]) return;

  switch(ROMType[I])
  {
    case MAP_GEN8: /* Generic 8kB cartridges (Konami, etc.) */
      /* Only interested in writes to 4000h-BFFFh */
      if((A<0x4000)||(A>0xBFFF)) break;
      J=(A-0x4000)>>13;
      /* Turn SCC on/off on writes to 8000h-9FFFh */
      if(J==2) SCCOn[I]=(V==0x3F)? 1:0;
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      if(Verbose&0x08)
        printf("ROM-MAPPER %c: 8kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V,PS,SS,J*0x2000+0x4000);
      return;

    case MAP_GEN16: /* Generic 16kB cartridges (MSXDOS2, HoleInOneSpecial) */
      /* Only interested in writes to 4000h-BFFFh */
      if((A<0x4000)||(A>0xBFFF)) break;
      J=(A&0x8000)>>14;
      /* Switch ROM pages */
      V=(V<<1)&ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        RAM[J+3]=MemMap[PS][SS][J+3]=RAM[J+2]+0x2000;
        ROMMapper[I][J]=V;
        ROMMapper[I][J+1]=V|1;
      }
      if(Verbose&0x08)
        printf("ROM-MAPPER %c: 16kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V>>1,PS,SS,J*0x2000+0x4000);
      return;

    case MAP_KONAMI5: /* KONAMI5 8kB cartridges */
      /* Only interested in writes to 5000h/7000h/9000h/B000h */
      if((A<0x5000)||(A>0xB000)||((A&0x1FFF)!=0x1000)) break;
      J=(A-0x5000)>>13;
      /* Turn SCC on/off on writes to 9000h */
      if(J==2) SCCOn[I]=(V==0x3F)? 1:0;
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      if(Verbose&0x08)
        printf("ROM-MAPPER %c: 8kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V,PS,SS,J*0x2000+0x4000);
      return;

    case MAP_KONAMI4: /* KONAMI4 8kB cartridges */
      /* Only interested in writes to 6000h/8000h/A000h */
      /* (page at 4000h is fixed) */
      if((A<0x6000)||(A>0xA000)||(A&0x1FFF)) break;
      J=(A-0x4000)>>13;
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      if(Verbose&0x08)
        printf("ROM-MAPPER %c: 8kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V,PS,SS,J*0x2000+0x4000);
      return;

    case MAP_ASCII8: /* ASCII 8kB cartridges */
      /* If switching pages... */
      if((A>=0x6000)&&(A<0x8000))
      {
        J=(A&0x1800)>>11;
        /* If selecting SRAM... */
        if(V&(ROMMask[I]+1))
        {
          /* Select SRAM page */
          V=0xFF;
          P=SRAMData[I];
          if(Verbose&0x08)
            printf("ROM-MAPPER %c: 8kB SRAM at %d:%d:%04Xh\n",I+'A',PS,SS,J*0x2000+0x4000);
        }
        else
        {
          /* Select ROM page */
          V&=ROMMask[I];
          P=ROMData[I]+((int)V<<13);
          if(Verbose&0x08)
            printf("ROM-MAPPER %c: 8kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V,PS,SS,J*0x2000+0x4000);
        }
        /* If page was actually changed... */
        if(V!=ROMMapper[I][J])
        {
          MemMap[PS][SS][J+2]=P;
          ROMMapper[I][J]=V;
          /* Only update memory when cartridge's slot selected */
          if((PSL[(J>>1)+1]==PS)&&(SSL[(J>>1)+1]==SS)) RAM[J+2]=P;
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0x8000)&&(A<0xC000)&&(ROMMapper[I][((A>>13)&1)+2]==0xFF))
      {
        RAM[A>>13][A&0x1FFF]=V;
        return;
      }
      break;

    case MAP_ASCII16: /*** ASCII 16kB cartridges ***/
      /* NOTE: Vauxall writes garbage to to 7xxxh */
      /* NOTE: Darwin writes valid data to 6x00h (ASCII8 mapper) */
      /* NOTE: Androgynus writes valid data to 77FFh */
      /* If switching pages... */
      if((A>=0x6000)&&(A<0x8000)&&((V<=ROMMask[I]+1)||!(A&0x0FFF)))
      {
        J=(A&0x1000)>>11;
        /* If selecting SRAM... */
        if(V&(ROMMask[I]+1))
        {
          /* Select SRAM page */
          V=0xFF;
          P=SRAMData[I];
          if(Verbose&0x08)
            printf("ROM-MAPPER %c: 2kB SRAM at %d:%d:%04Xh\n",I+'A',PS,SS,J*0x2000+0x4000);
        }
        else
        {
          /* Select ROM page */
          V=(V<<1)&ROMMask[I];
          P=ROMData[I]+((int)V<<13);
          if(Verbose&0x08)
            printf("ROM-MAPPER %c: 16kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V>>1,PS,SS,J*0x2000+0x4000);
        }
        /* If page was actually changed... */
        if(V!=ROMMapper[I][J])
        {
          MemMap[PS][SS][J+2]=P;
          MemMap[PS][SS][J+3]=P+0x2000;
          ROMMapper[I][J]=V;
          ROMMapper[I][J+1]=V|1;
          /* Only update memory when cartridge's slot selected */
          if((PSL[(J>>1)+1]==PS)&&(SSL[(J>>1)+1]==SS))
          {
            RAM[J+2]=P;
            RAM[J+3]=P+0x2000;
          }
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0x8000)&&(A<0xC000)&&(ROMMapper[I][2]==0xFF))
      {
        P=RAM[A>>13];
        A&=0x07FF;
        P[A+0x0800]=P[A+0x1000]=P[A+0x1800]=
        P[A+0x2000]=P[A+0x2800]=P[A+0x3000]=
        P[A+0x3800]=P[A]=V;
        return;
      }
      break;

    case MAP_GMASTER2: /* Konami GameMaster2+SRAM cartridge */
      /* Switch ROM and SRAM pages, page at 4000h is fixed */
      if((A>=0x6000)&&(A<=0xA000)&&!(A&0x1FFF))
      {
        /* Figure out which ROM page gets switched */
        J=(A-0x4000)>>13;
        /* If changing SRAM page... */
        if(V&0x10)
        {
          /* Select SRAM page */
          RAM[J+2]=MemMap[PS][SS][J+2]=SRAMData[I]+(V&0x20? 0x2000:0);
          /* SRAM is now on */
          ROMMapper[I][J]=0xFF;
          if(Verbose&0x08)
            printf("GMASTER2 %c: 4kB SRAM page #%d at %d:%d:%04Xh\n",I+'A',(V&0x20)>>5,PS,SS,J*0x2000+0x4000);
        }
        else
        {
          /* Compute new ROM page number */
          V&=ROMMask[I];
          /* If ROM page number has changed... */
          if(V!=ROMMapper[I][J])
          {
            RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
            ROMMapper[I][J]=V;
          }
          if(Verbose&0x08)
            printf("GMASTER2 %c: 8kB ROM page #%d at %d:%d:%04Xh\n",I+'A',V,PS,SS,J*0x2000+0x4000);
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0xB000)&&(A<0xC000)&&(ROMMapper[I][3]==0xFF))
      {
        RAM[5][(A&0x0FFF)|0x1000]=RAM[5][A&0x0FFF]=V;
        return;
      }
      break;

    case MAP_FMPAC: /* Panasonic FMPAC+SRAM cartridge */
      /* See if any switching occurs */
      switch(A)
      {
        case 0x7FF7: /* ROM page select */
          V=(V<<1)&ROMMask[I];
          ROMMapper[I][0]=V;
          ROMMapper[I][1]=V|1;
          /* 4000h-5FFFh contains SRAM when correct FMPACKey supplied */
          if(FMPACKey!=FMPAC_MAGIC)
          {
            P=ROMData[I]+((int)V<<13);
            RAM[2]=MemMap[PS][SS][2]=P;
            RAM[3]=MemMap[PS][SS][3]=P+0x2000;
          }
          if(Verbose&0x08)
            printf("FMPAC %c: 16kB ROM page #%d at %d:%d:4000h\n",I+'A',V>>1,PS,SS);
          return;
        case 0x7FF6: /* OPL1 enable/disable? */
          if(Verbose&0x08)
            printf("FMPAC %c: (7FF6h) = %02Xh\n",I+'A',V);
          V&=0x11;
          return;
        case 0x5FFE: /* Write 4Dh, then (5FFFh)=69h to enable SRAM */
        case 0x5FFF: /* (5FFEh)=4Dh, then write 69h to enable SRAM */
          FMPACKey=A&1? ((FMPACKey&0x00FF)|((int)V<<8))
                      : ((FMPACKey&0xFF00)|V);
          P=FMPACKey==FMPAC_MAGIC?
            SRAMData[I]:(ROMData[I]+((int)ROMMapper[I][0]<<13));
          RAM[2]=MemMap[PS][SS][2]=P;
          RAM[3]=MemMap[PS][SS][3]=P+0x2000;
          if(Verbose&0x08)
            printf("FMPAC %c: 8kB SRAM %sabled at %d:%d:4000h\n",I+'A',FMPACKey==FMPAC_MAGIC? "en":"dis",PS,SS);
          return;
      }
      /* Write to SRAM */
      if((A>=0x4000)&&(A<0x5FFE)&&(FMPACKey==FMPAC_MAGIC))
      {
        RAM[A>>13][A&0x1FFF]=V;
        return;
      }
      break;
  }

  /* No MegaROM mapper or there is an incorrect write */     
  if(Verbose&0x08) printf("MEMORY: Bad write (%d:%d:%04Xh) = %02Xh\n",PS,SS,A,V);
}

/** PSlot() **************************************************/
/** Switch primary memory slots. This function is called    **/
/** when value in port A8h changes.                         **/
/*************************************************************/
void PSlot(register byte V)
{
  register byte J,I;
  
  if(PSLReg!=V)
    for(PSLReg=V,J=0;J<4;++J,V>>=2)
    {
      I          = J<<1;
      PSL[J]     = V&3;
      SSL[J]     = (SSLReg[PSL[J]]>>I)&3;
      RAM[I]     = MemMap[PSL[J]][SSL[J]][I];
      RAM[I+1]   = MemMap[PSL[J]][SSL[J]][I+1];
      EnWrite[J] = (PSL[J]==3)&&(SSL[J]==2)&&(MemMap[3][2][I]!=EmptyRAM);
    }
}

/** SSlot() **************************************************/
/** Switch secondary memory slots. This function is called  **/
/** when value in (FFFFh) changes.                          **/
/*************************************************************/
void SSlot(register byte V)
{
  register byte J,I;

  /* Cartridge slots do not have subslots, fix them at 0:0:0:0 */
  if((PSL[3]==1)||(PSL[3]==2)) V=0x00;
  /* In MSX1, slot 0 does not have subslots either */
  if(!PSL[3]&&((Mode&MSX_MODEL)==MSX_MSX1)) V=0x00;

  if(SSLReg[PSL[3]]!=V)
    for(SSLReg[PSL[3]]=V,J=0;J<4;++J,V>>=2)
    {
      if(PSL[J]==PSL[3])
      {
        I          = J<<1;
        SSL[J]     = V&3;
        RAM[I]     = MemMap[PSL[J]][SSL[J]][I];
        RAM[I+1]   = MemMap[PSL[J]][SSL[J]][I+1];
        EnWrite[J] = (PSL[J]==3)&&(SSL[J]==2)&&(MemMap[3][2][I]!=EmptyRAM);
      }
    }
}

/** SetIRQ() *************************************************/
/** Set or reset IRQ. Returns IRQ vector assigned to        **/
/** CPU.IRequest. When upper bit of IRQ is 1, IRQ is reset. **/
/*************************************************************/
word SetIRQ(register byte IRQ)
{
  if(IRQ&0x80) IRQPending&=IRQ; else IRQPending|=IRQ;
  MSXCPU.IRequest=IRQPending? INT_IRQ:INT_NONE;
  return(MSXCPU.IRequest);
}

/** SetScreen() **********************************************/
/** Change screen mode. Returns new screen mode.            **/
/*************************************************************/
byte SetScreen(void)
{
  register byte I,J;

  switch(((VDP[0]&0x0E)>>1)|(VDP[1]&0x18))
  {
    case 0x10: J=0;break;
    case 0x00: J=1;break;
    case 0x01: J=2;break;
    case 0x08: J=3;break;
    case 0x02: J=4;break;
    case 0x03: J=5;break;
    case 0x04: J=6;break;
    case 0x05: J=7;break;
    case 0x07: J=8;break;
    case 0x12: J=MAXSCREEN+1;break;
    default:   J=ScrMode;break;
  }

  /* Recompute table addresses */
  I=(J>6)&&(J!=MAXSCREEN+1)? 11:10;
  ChrTab  = VRAM+((int)(VDP[2]&MSK[J].R2)<<I);
  ChrGen  = VRAM+((int)(VDP[4]&MSK[J].R4)<<11);
  ColTab  = VRAM+((int)(VDP[3]&MSK[J].R3)<<6)+((int)VDP[10]<<14);
  SprTab  = VRAM+((int)(VDP[5]&MSK[J].R5)<<7)+((int)VDP[11]<<15);
  SprGen  = VRAM+((int)VDP[6]<<11);
  ChrTabM = ((int)(VDP[2]|~MSK[J].M2)<<I)|((1<<I)-1);
  ChrGenM = ((int)(VDP[4]|~MSK[J].M4)<<11)|0x007FF;
  ColTabM = ((int)(VDP[3]|~MSK[J].M3)<<6)|0x1C03F;
  SprTabM = ((int)(VDP[5]|~MSK[J].M5)<<7)|0x1807F;

  /* Return new screen mode */
  ScrMode=J;
  return(J);
}

/** SetMegaROM() *********************************************/
/** Set MegaROM pages for a given slot. SetMegaROM() always **/
/** assumes 8kB pages.                                      **/
/*************************************************************/
void SetMegaROM(int Slot,byte P0,byte P1,byte P2,byte P3)
{
  byte PS,SS;

  /* @@@ ATTENTION: MUST ADD SUPPORT FOR SRAM HERE!   */
  /* @@@ The FFh value must be treated as a SRAM page */

  /* Slot number must be valid */
  if((Slot<0)||(Slot>=MAXSLOTS)) return;
  /* Find primary/secondary slots */
  for(PS=0;PS<4;++PS)
  {
    for(SS=0;(SS<4)&&(CartMap[PS][SS]!=Slot);++SS);
    if(SS<4) break;
  }
  /* Drop out if slots not found */
  if(PS>=4) return;

  /* Apply masks to ROM pages */
  P0&=ROMMask[Slot];
  P1&=ROMMask[Slot];
  P2&=ROMMask[Slot];
  P3&=ROMMask[Slot];
  /* Set memory map */
  MemMap[PS][SS][2]=ROMData[Slot]+P0*0x2000;
  MemMap[PS][SS][3]=ROMData[Slot]+P1*0x2000;
  MemMap[PS][SS][4]=ROMData[Slot]+P2*0x2000;
  MemMap[PS][SS][5]=ROMData[Slot]+P3*0x2000;
  /* Set ROM mappers */
  ROMMapper[Slot][0]=P0;
  ROMMapper[Slot][1]=P1;
  ROMMapper[Slot][2]=P2;
  ROMMapper[Slot][3]=P3;
}

/** VDPOut() *************************************************/
/** Write value into a given VDP register.                  **/
/*************************************************************/
void VDPOut(register byte R,register byte V)
{ 
  register byte J;

  switch(R)  
  {
    case  0: /* Reset HBlank interrupt if disabled */
             if((VDPStatus[1]&0x01)&&!(V&0x10))
             {
               VDPStatus[1]&=0xFE;
               SetIRQ(~INT_IE1);
             }
             /* Set screen mode */
             if(VDP[0]!=V) { VDP[0]=V;SetScreen(); }
             break;
    case  1: /* Set/Reset VBlank interrupt if enabled or disabled */
             if(VDPStatus[0]&0x80) SetIRQ(V&0x20? INT_IE0:~INT_IE0);
             /* Set screen mode */
             if(VDP[1]!=V) { VDP[1]=V;SetScreen(); }
             break;
    case  2: J=(ScrMode>6)&&(ScrMode!=MAXSCREEN+1)? 11:10;
             ChrTab  = VRAM+((int)(V&MSK[ScrMode].R2)<<J);
             ChrTabM = ((int)(V|~MSK[ScrMode].M2)<<J)|((1<<J)-1);
             break;
    case  3: ColTab  = VRAM+((int)(V&MSK[ScrMode].R3)<<6)+((int)VDP[10]<<14);
             ColTabM = ((int)(V|~MSK[ScrMode].M3)<<6)|0x1C03F;
             break;
    case  4: ChrGen  = VRAM+((int)(V&MSK[ScrMode].R4)<<11);
             ChrGenM = ((int)(V|~MSK[ScrMode].M4)<<11)|0x007FF;
             break;
    case  5: SprTab  = VRAM+((int)(V&MSK[ScrMode].R5)<<7)+((int)VDP[11]<<15);
             SprTabM = ((int)(V|~MSK[ScrMode].M5)<<7)|0x1807F;
             break;
    case  6: V&=0x3F;SprGen=VRAM+((int)V<<11);break;
    case  7: FGColor=V>>4;BGColor=V&0x0F;break;
    case 10: V&=0x07;
             ColTab=VRAM+((int)(VDP[3]&MSK[ScrMode].R3)<<6)+((int)V<<14);
             break;
    case 11: V&=0x03;
             SprTab=VRAM+((int)(VDP[5]&MSK[ScrMode].R5)<<7)+((int)V<<15);
             break;
    case 14: V&=VRAMPages-1;VPAGE=VRAM+((int)V<<14);
             break;
    case 15: V&=0x0F;break;
    case 16: V&=0x0F;PKey=1;break;
    case 17: V&=0xBF;break;
    case 25: VDP[25]=V;
             SetScreen();
             break;
    case 44: VDPWrite(V);break;
    case 46: VDPDraw(V);break;
  }

  /* Write value into a register */
  VDP[R]=V;
} 

/** PPIOut() *************************************************/
/** This function is called on each write to PPI to make    **/
/** key click sound, motor relay clicks, and so on.         **/
/*************************************************************/
void PPIOut(register byte New,register byte Old)
{
  /* Keyboard click bit */
  if((New^Old)&0x80) Drum(DRM_CLICK,64);
  /* Motor relay bit */
  if((New^Old)&0x10) Drum(DRM_CLICK,255);
}

/** RTCIn() **************************************************/
/** Read value from a given RTC register.                   **/
/*************************************************************/
byte RTCIn(register byte R)
{
  static time_t PrevTime;
  static struct tm TM;
  register byte J;
  time_t CurTime;

  /* Only 16 registers/mode */
  R&=0x0F;

  /* Bank mode 0..3 */
  J=RTCMode&0x03;

  if(R>12) J=R==13? RTCMode:NORAM;
  else
    if(J) J=RTCRegs[J][R];
    else
    {
      /* Retrieve system time if any time passed */
      CurTime=time(NULL);
      if(CurTime!=PrevTime)
      {
        TM=*localtime(&CurTime);
        PrevTime=CurTime;
      }

      /* Parse contents of last retrieved TM */
      switch(R)
      {
        case 0:  J=TM.tm_sec%10;break;
        case 1:  J=TM.tm_sec/10;break;
        case 2:  J=TM.tm_min%10;break;
        case 3:  J=TM.tm_min/10;break;
        case 4:  J=TM.tm_hour%10;break;
        case 5:  J=TM.tm_hour/10;break;
        case 6:  J=TM.tm_wday;break;
        case 7:  J=TM.tm_mday%10;break;
        case 8:  J=TM.tm_mday/10;break;
        case 9:  J=(TM.tm_mon+1)%10;break;
        case 10: J=(TM.tm_mon+1)/10;break;
        case 11: J=(TM.tm_year-80)%10;break;
        case 12: J=((TM.tm_year-80)/10)%10;break;
        default: J=0x0F;break;
      } 
    }

  /* Four upper bits are always high */
  return(J|0xF0);
}

/** LoopZ80() ************************************************/
/** Refresh screen, check keyboard and sprites. Call this   **/
/** function on each interrupt.                             **/
/*************************************************************/
word LoopZ80(Z80 *R)
{
  static byte BFlag=0;
  static byte BCount=0;
  static byte ACount=0;
  static byte Drawing=0;
  register int J;

  /* Flip HRefresh bit */
  VDPStatus[2]^=0x20;

  /* If HRefresh is now in progress... */
  if(!(VDPStatus[2]&0x20))
  {
    /* HRefresh takes most of the scanline */
    R->IPeriod=!ScrMode||(ScrMode==MAXSCREEN+1)? CPU_H240:CPU_H256;

    /* New scanline */
    ScanLine=ScanLine<(PALVideo? 312:261)? ScanLine+1:0;

    /* If first scanline of the screen... */
    if(!ScanLine)
    {
      /* Drawing now... */
      Drawing=1;

      /* Reset VRefresh bit */
      VDPStatus[2]&=0xBF;

      /* Refresh display */
      RefreshScreen();

      /* Blinking for TEXT80 */
      if(BCount) BCount--;
      else
      {
        BFlag=!BFlag;
        if(!VDP[13]) { XFGColor=FGColor;XBGColor=BGColor; }
        else
        {
          BCount=(BFlag? VDP[13]&0x0F:VDP[13]>>4)*10;
          if(BCount)
          {
            if(BFlag) { XFGColor=FGColor;XBGColor=BGColor; }
            else      { XFGColor=VDP[12]>>4;XBGColor=VDP[12]&0x0F; }
          }
        }
      }
    }

    /* Line coincidence is active at 0..255 */
    /* in PAL and 0..234/244 in NTSC        */
    J=PALVideo? 256:ScanLines212? 245:235;

    /* When reaching end of screen, reset line coincidence */
    if(ScanLine==J)
    {
      VDPStatus[1]&=0xFE;
      SetIRQ(~INT_IE1);
    }

    /* When line coincidence is active... */
    if(ScanLine<J)
    {
      /* Line coincidence processing */
      J=(((ScanLine+VScroll)&0xFF)-VDP[19])&0xFF;
      if(J==2)
      {
        /* Set HBlank flag on line coincidence */
        VDPStatus[1]|=0x01;
        /* Generate IE1 interrupt */
        if(VDP[0]&0x10) SetIRQ(INT_IE1);
      }
      else
      {
        /* Reset flag immediately if IE1 interrupt disabled */
        if(!(VDP[0]&0x10)) VDPStatus[1]&=0xFE;
      }
    }

    /* Return whatever interrupt is pending */
    R->IRequest=IRQPending? INT_IRQ:INT_NONE;
    return(R->IRequest);
  }

  /*********************************/
  /* We come here for HBlanks only */
  /*********************************/

  /* HBlank takes HPeriod-HRefresh */
  R->IPeriod=!ScrMode||(ScrMode==MAXSCREEN+1)? CPU_H240:CPU_H256;
  R->IPeriod=HPeriod-R->IPeriod;

  /* If last scanline of VBlank, see if we need to wait more */
  J=PALVideo? 313:262;
  if(ScanLine>=J-1)
  {
    J*=CPU_HPERIOD;
    if(VPeriod>J) R->IPeriod+=VPeriod-J;
  }

  /* If first scanline of the bottom border... */
  if(ScanLine==(ScanLines212? 212:192)) Drawing=0;

  /* If first scanline of VBlank... */
  J=PALVideo? (ScanLines212? 212+42:192+52):(ScanLines212? 212+18:192+28);
  if(!Drawing&&(ScanLine==J))
  {
    /* Set VBlank bit, set VRefresh bit */
    VDPStatus[0]|=0x80;
    VDPStatus[2]|=0x40;

    /* Generate VBlank interrupt */
    if(VDP[1]&0x20) SetIRQ(INT_IE0);
  }

  /* Run V9938 engine */
  LoopVDP();

  /* Refresh scanline, possibly with the overscan */
  if(Drawing&&(ScanLine<256))
  {
    if(!ModeYJK||(ScrMode<7)||(ScrMode>8))
      (RefreshLine[ScrMode])(ScanLine);
    else
      if(ModeYAE) RefreshLine10(ScanLine);
      else RefreshLine12(ScanLine);
  }

  /* Every few scanlines, update sound */
  if(!(ScanLine&0x07))
  {
    /* Compute number of microseconds */
    J = (int)(1000000L*(CPU_HPERIOD<<3)/CPU_CLOCK);

    /* Update AY8910 state */
    Loop8910(&PSG,J);

    /* Flush changes to sound channels, only hit drums once a frame */
    Sync8910(&PSG,AY8910_FLUSH|(!ScanLine&&OPTION(MSX_DRUMS)? AY8910_DRUMS:0));
    SyncSCC(&SCChip,SCC_FLUSH);
    Sync2413(&OPLL,YM2413_FLUSH);

    /* Render and play all sound now */
    PlayAllSound(J);
  }

  /* Keyboard, sound, and other stuff always runs at line 192    */
  /* This way, it can't be shut off by overscan tricks (Maarten) */
  if(ScanLine==192)
  {
    /* Clear 5thSprite fields (wrong place to do it?) */
    VDPStatus[0]=(VDPStatus[0]&~0x40)|0x1F;

    /* Check sprites and set Collision bit */
    if(!(VDPStatus[0]&0x20)&&CheckSprites()) VDPStatus[0]|=0x20;

    /* Count MIDI ticks */
    MIDITicks(1000*VPeriod/CPU_CLOCK);

    /* Check joystick & Keyboard */
    JoyState=Joystick();

    /* Exit emulation if requested */
    if(ExitNow) return(INT_QUIT);

    /* If any autofire options selected, run autofire counter */
    if(OPTION(MSX_AUTOSPACE|MSX_AUTOFIREA|MSX_AUTOFIREB))
      if((ACount=(ACount+1)&0x07)>3)
      {
        /* Autofire spacebar if needed */
        if(OPTION(MSX_AUTOSPACE)) KBD_RES(' ');
        /* Autofire FIRE-A if needed */
        if(OPTION(MSX_AUTOFIREA)) JoyState&=~(JST_FIREA|(JST_FIREA<<8));
        /* Autofire FIRE-B if needed */
        if(OPTION(MSX_AUTOFIREB)) JoyState&=~(JST_FIREB|(JST_FIREB<<8));
      }
  }

  /* Return whatever interrupt is pending */
  R->IRequest=IRQPending? INT_IRQ:INT_NONE;
  return(R->IRequest);
}

/** CheckSprites() *******************************************/
/** Check for sprite collisions.                            **/
/*************************************************************/
int CheckSprites(void)
{
  unsigned int I,J,LS,LD;
  byte DH,DV,*S,*D,*PS,*PD,*T;

  /* Must be showing sprites */
  if(SpritesOFF||!ScrMode||(ScrMode>=MAXSCREEN+1)) return(0);

  /* Find bottom/top scanlines */
  DH = ScrMode>3? 216:208;
  LD = 255-(Sprites16x16? 16:8);
  LS = ScanLines212? 211:191;

  /* Find valid, displayed sprites */
  for(I=J=0,S=SprTab;(I<32)&&(S[0]!=DH);++I,S+=4)
    if((S[0]<LS)||(S[0]>LD)) J|=1<<I;

  if(Sprites16x16)
  {
    for(S=SprTab;J;J>>=1,S+=4)
      if(J&1)
        for(I=J>>1,D=S+4;I;I>>=1,D+=4)
          if(I&1) 
          {
            DV=S[0]-D[0];
            if((DV<16)||(DV>240))
      {
              DH=S[1]-D[1];
              if((DH<16)||(DH>240))
        {
                PS=SprGen+((int)(S[2]&0xFC)<<3);
                PD=SprGen+((int)(D[2]&0xFC)<<3);
                if(DV<16) PD+=DV; else { DV=256-DV;PS+=DV; }
                if(DH>240) { DH=256-DH;T=PS;PS=PD;PD=T; }
                while(DV<16)
                {
                  LS=((unsigned int)*PS<<8)+*(PS+16);
                  LD=((unsigned int)*PD<<8)+*(PD+16);
                  if(LD&(LS>>DH)) break;
                  else { ++DV;++PS;++PD; }
                }
                if(DV<16) return(1);
              }
            }
          }
  }
  else
  {
    for(S=SprTab;J;J>>=1,S+=4)
      if(J&1)
        for(I=J>>1,D=S+4;I;I>>=1,D+=4)
          if(I&1) 
          {
            DV=S[0]-D[0];
            if((DV<8)||(DV>248))
            {
              DH=S[1]-D[1];
              if((DH<8)||(DH>248))
              {
                PS=SprGen+((int)S[2]<<3);
                PD=SprGen+((int)D[2]<<3);
                if(DV<8) PD+=DV; else { DV=256-DV;PS+=DV; }
                if(DH>248) { DH=256-DH;T=PS;PS=PD;PD=T; }
                while((DV<8)&&!(*PD&(*PS>>DH))) { ++DV;++PS;++PD; }
                if(DV<8) return(1);
              }
            }
          }
  }

  /* No collisions */
  return(0);
}

/** StateID() ************************************************/
/** Compute 16bit emulation state ID used to identify .STA  **/
/** files.                                                  **/
/*************************************************************/
word StateID(void)
{
  word ID;
  int J,I;

  ID=0x0000;

  /* Add up cartridge ROMs, BIOS, BASIC, ExtBIOS, and DiskBIOS bytes */
  for(I=0;I<MAXSLOTS;++I)
    if(ROMData[I]) for(J=0;J<(ROMMask[I]+1)*0x2000;++J) ID+=I^ROMData[I][J];
  if(MemMap[0][0][0]&&(MemMap[0][0][0]!=EmptyRAM))
    for(J=0;J<0x8000;++J) ID+=MemMap[0][0][0][J];
  if(MemMap[3][1][0]&&(MemMap[3][1][0]!=EmptyRAM))
    for(J=0;J<0x4000;++J) ID+=MemMap[3][1][0][J];
  if(MemMap[3][1][2]&&(MemMap[3][1][2]!=EmptyRAM))
    for(J=0;J<0x4000;++J) ID+=MemMap[3][1][2][J];

  return(ID);
}

/** msx_change_disk() ****************************************/
/** Change disk image in a given drive. Closes current disk **/
/** image if Name=0 was given. Creates a new disk image if  **/
/** Name="" was given. Returns 1 on success or 0 on failure.**/
/*************************************************************/
byte msx_change_disk(byte N,const char *FileName)
{
  const uint8_t *P;
  const retro_emulator_file_t *disk_file;
  const rom_system_t *msx_system;

  /* We only have MAXDRIVES drives */
  if(N>=MAXDRIVES) return(0);

  /* Reset FDC, in case it was running a command */
  Reset1793(&FDC,FDD,WD1793_KEEP);

  /* Eject disk if requested */
  if(!FileName) { EjectFDI(&FDD[N]);return(1); }

  /* Try opening disk from flash */
  if(Verbose) printf("Looking for %s\n",FileName);
  msx_system = rom_manager_system(&rom_mgr, "MSX");
  disk_file = rom_manager_get_file(msx_system,FileName,"fdi");
  if (disk_file != NULL) {
    /* Update pointer to data */
    P = disk_file->address;
    if(Verbose) printf("Found %s.%s:\n",disk_file->name,disk_file->ext);
    LoadFDIFlash(&FDD[N],FileName,(char *)disk_file->address,disk_file->size,FMT_AUTO);
    strcpy(currentDiskName,FileName);
    return 1;
  } else {
    if(Verbose) printf("Problem loading disk : %s not found\n",FileName);
    return 0;
  }
  return 0;
}

/** GuessROM() ***********************************************/
/** Guess MegaROM mapper of a ROM.                          **/
/*************************************************************/
int GuessROM(const byte *Buf,int Size)
{
  int J,I,Result,ROMCount[MAXMAPPERS];
  char S[256];
  retro_emulator_file_t *sha1_file;

  /* No result yet */
  Result = -1;

  /* Try opening file with SHA1 sums */
  const rom_system_t *rom_system = rom_manager_system(&rom_mgr, "MSX_BIOS");
  sha1_file = (retro_emulator_file_t *)rom_manager_get_file(rom_system,"CARTS","sha");
  if ((Result<0) && sha1_file != NULL)
  {
    char S1[41],S2[41];
    SHA1 C;

    /* Compute ROM's SHA1 */
    ResetSHA1(&C);
    InputSHA1(&C,Buf,Size);
    if(ComputeSHA1(&C) && OutputSHA1(&C,S1,sizeof(S1)))
    {
      int offset = 0;
      int readed = 1;
      while((offset<sha1_file->size)&&(readed != 0))
      {
        wdog_refresh();
        readed = sscanf((const char *)(sha1_file->address+offset),"%50[^\n]",S);
        offset += strlen(S)+1;
        if((sscanf(S,"%40s %d",S2,&J)==2) && !strcmp(S1,S2))
        { Result=J;break; }
      }
    }
  }

  /* If found ROM by CRC or SHA1, we are done */
  if(Result>=0) return(Result);

  /* Clear all counters */
  for(J=0;J<MAXMAPPERS;++J) ROMCount[J]=1;
  /* Generic 8kB mapper is default */
  ROMCount[MAP_GEN8]+=1;
  /* ASCII 16kB preferred over ASCII 8kB */
  ROMCount[MAP_ASCII8]-=1;

  /* Count occurences of characteristic addresses */
  for(J=0;J<Size-2;++J)
  {
    I=Buf[J]+((int)Buf[J+1]<<8)+((int)Buf[J+2]<<16);
    switch(I)
    {
      case 0x500032: ROMCount[MAP_KONAMI5]++;break;
      case 0x900032: ROMCount[MAP_KONAMI5]++;break;
      case 0xB00032: ROMCount[MAP_KONAMI5]++;break;
      case 0x400032: ROMCount[MAP_KONAMI4]++;break;
      case 0x800032: ROMCount[MAP_KONAMI4]++;break;
      case 0xA00032: ROMCount[MAP_KONAMI4]++;break;
      case 0x680032: ROMCount[MAP_ASCII8]++;break;
      case 0x780032: ROMCount[MAP_ASCII8]++;break;
      case 0x600032: ROMCount[MAP_KONAMI4]++;
                     ROMCount[MAP_ASCII8]++;
                     ROMCount[MAP_ASCII16]++;
                     break;
      case 0x700032: ROMCount[MAP_KONAMI5]++;
                     ROMCount[MAP_ASCII8]++;
                     ROMCount[MAP_ASCII16]++;
                     break;
      case 0x77FF32: ROMCount[MAP_ASCII16]++;break;
    }
  }

  /* Find which mapper type got more hits */
  for(I=0,J=0;J<MAXMAPPERS;++J)
    if(ROMCount[J]>ROMCount[I]) I=J;

  /* Return the most likely mapper type */
  return(I);
}

/** LoadFNT() ************************************************/
/** Load fixed 8x8 font used in text screen modes when      **/
/** MSX_FIXEDFONT option is enabled. LoadFNT(0) frees the   **/
/** font buffer. Returns 1 on success, 0 on failure.        **/
/*************************************************************/
byte LoadFNT(const char *FileName)
{
#if 0
  FILE *F;

  /* Drop out if no new font requested */
  if(!FileName) { FreeMemory(FontBuf);FontBuf=0;return(1); }
  /* Try opening font file */
  if(!(F=fopen(FileName,"rb"))) return(0);
  /* Allocate memory for 256 8x8 characters, if needed */
  if(!FontBuf) FontBuf=GetMemory(256*8);
  /* Drop out if failed memory allocation */
  if(!FontBuf) { fclose(F);return(0); }
  /* Read font, ignore short reads */
  fread(FontBuf,1,256*8,F);
  /* Done */
  fclose(F);
#endif
  return(1);  
}

/** LoadFlashROM() *******************************************/
/** Load a rom to the given address, Returns addr. of the   **/
/**  allocated space or 0 if failed.                        **/
/*************************************************************/
byte *LoadFlashROM(const char *Name,const char *Ext,int Size,byte *Buf)
{
  byte *P = NULL;
  retro_emulator_file_t *rom_file;
  const rom_system_t *msx_bios = rom_manager_system(&rom_mgr, "MSX_BIOS");

  if(Verbose) printf("LoadFlashROM '%s'\n",Name);
  /* Need address and size */
  if(!Buf||!Size) return(0);

  rom_file = (retro_emulator_file_t *)rom_manager_get_file(msx_bios,Name,Ext);
  if (rom_file != NULL) {
    /* Copy data */
    memcpy(Buf,rom_file->address,Size);
    P = Buf;
  }
  // TODO : check if memcpy is ok


  /* Done */
  return(P);
}

/** LoadGNWCartName() ****************************************/
/** Load cartridge into given slot. Returns cartridge size  **/
/** in 16kB pages on success, 0 on failure.                 **/
/*************************************************************/
int LoadGNWCartName(const char *FileName,const char *Ext,int Slot,int Type, bool isBios)
{
  int C1,C2,Len,Pages,ROM64,BASIC;
  byte *P,PS,SS;
  const retro_emulator_file_t *rom_file;
  const rom_system_t *msx_system;

  /* Slot number must be valid */
  if((Slot<0)||(Slot>=MAXSLOTS)) return(0);
  /* Find primary/secondary slots */
  for(PS=0;PS<4;++PS)
  {
  for(SS=0;(SS<4)&&(CartMap[PS][SS]!=Slot);++SS);
  if(SS<4) break;
  }
  /* Drop out if slots not found */
  if(PS>=4) return(0);

  /* Looking for rom */
  if(Verbose) printf("Looking for %s\n",FileName);
  if(isBios) {
    msx_system = rom_manager_system(&rom_mgr, "MSX_BIOS");
  } else {
    msx_system = rom_manager_system(&rom_mgr, "MSX");
  }
  rom_file = rom_manager_get_file(msx_system,FileName,Ext);
  if (rom_file != NULL) {
    /* Update pointer to data */
    P = (byte *)rom_file->address;
  } else {
    if(Verbose) printf("%s not found\n",FileName);
    return 0;
  }

  if(Verbose) printf("Found %s.%s:\n",rom_file->name,rom_file->ext);

  /* Length in 8kB pages */
  Len = rom_file->size>>13;
//  Len = Len>>13;

  /* Calculate 2^n closest to number of 8kB pages */
  for(Pages=1;Pages<Len;Pages<<=1);

  /* Check "AB" signature in a file */
  ROM64=0;
  C1=P[0];
  C2=P[1];

  /* Maybe this is a flat 64kB ROM? */
  if((C1!='A')||(C2!='B'))
//  if(fseek(F,0x4000,SEEK_SET)>=0)
  {
    C1=P[0x4000];
    C2=P[0x4001];
    ROM64=(C1=='A')&&(C2=='B');
  }

  /* Maybe it is the last 16kB page that contains "AB" signature? */
  if((Len>=2)&&((C1!='A')||(C2!='B')))
//  if(fseek(F,0x2000*(Len-2),SEEK_SET)>=0)
  {
    C1=P[0x2000*(Len-2)];
    C2=P[0x2000*(Len-2)+1];
  }

  /* If we can't find "AB" signature, drop out */
  if((C1!='A')||(C2!='B'))
  {
  if(Verbose) printf("  Not a valid cartridge ROM");
  return(0);
  }

  if(Verbose) printf("  Cartridge %c: ",'A'+Slot);

  /* Done with the file */
//  fclose(F);

  /* Show ROM type and size */
  if(Verbose)
  printf
  (
    "%dkB %s ROM..\n",Len*8,
    ROM64||(Len<=0x8000)? "NORMAL":Type>=MAP_GUESS? "UNKNOWN":ROMNames[Type]
  );

  /* Assign ROMMask for MegaROMs */
  ROMMask[Slot]=!ROM64&&(Len>4)? (Pages-1):0x00;
  /* Allocate space for the ROM */
//  ROMData[Slot]=P=GetMemory(Pages<<13);
//  if(!P) { PRINTFAILED;return(0); }

  /* Try loading ROM */
//  if(!LoadROM(FileName,Len<<13,P)) { PRINTFAILED;return(0); }

  /* Mirror ROM if it is smaller than 2^n pages */
  if(Len<Pages) {
    printf("Warning : We should mirror ROM");
//    memcpy(P+Len*0x2000,P+(Len-Pages/2)*0x2000,(Pages-Len)*0x2000);
  }

  ROMData[Slot]=P;
  /* Detect ROMs containing BASIC code */
  BASIC=(P[0]=='A')&&(P[1]=='B')&&!(P[2]||P[3])&&(P[8]||P[9]);

  /* Set memory map depending on the ROM size */
  switch(Len)
  {
  case 1:
    /* 8kB ROMs are mirrored 8 times: 0:0:0:0:0:0:0:0 */
    if(!BASIC)
    {
    MemMap[PS][SS][0]=P;
    MemMap[PS][SS][1]=P;
    MemMap[PS][SS][2]=P;
    MemMap[PS][SS][3]=P;
    }
    MemMap[PS][SS][4]=P;
    MemMap[PS][SS][5]=P;
    if(!BASIC)
    {
    MemMap[PS][SS][6]=P;
    MemMap[PS][SS][7]=P;
    }
    break;

  case 2:
    /* 16kB ROMs are mirrored 4 times: 0:1:0:1:0:1:0:1 */
    if(!BASIC)
    {
    MemMap[PS][SS][0]=P;
    MemMap[PS][SS][1]=P+0x2000;
    MemMap[PS][SS][2]=P;
    MemMap[PS][SS][3]=P+0x2000;
    }
    MemMap[PS][SS][4]=P;
    MemMap[PS][SS][5]=P+0x2000;
    if(!BASIC)
    {
    MemMap[PS][SS][6]=P;
    MemMap[PS][SS][7]=P+0x2000;
    }
    break;

  case 3:
  case 4:
    /* 24kB and 32kB ROMs are mirrored twice: 0:1:0:1:2:3:2:3 */
    MemMap[PS][SS][0]=P;
    MemMap[PS][SS][1]=P+0x2000;
    MemMap[PS][SS][2]=P;
    MemMap[PS][SS][3]=P+0x2000;
    MemMap[PS][SS][4]=P+0x4000;
    MemMap[PS][SS][5]=P+0x6000;
    MemMap[PS][SS][6]=P+0x4000;
    MemMap[PS][SS][7]=P+0x6000;
    break;

  default:
    if(ROM64)
    {
    /* 64kB ROMs are loaded to fill slot: 0:1:2:3:4:5:6:7 */
    MemMap[PS][SS][0]=P;
    MemMap[PS][SS][1]=P+0x2000;
    MemMap[PS][SS][2]=P+0x4000;
    MemMap[PS][SS][3]=P+0x6000;
    MemMap[PS][SS][4]=P+0x8000;
    MemMap[PS][SS][5]=P+0xA000;
    MemMap[PS][SS][6]=P+0xC000;
    MemMap[PS][SS][7]=P+0xE000;
    }
    break;
  }

  /* Show starting address */
  if(Verbose)
  printf
  (
    "starts at %04Xh..",
    MemMap[PS][SS][2][2]+256*MemMap[PS][SS][2][3]
  );

  /* Guess MegaROM mapper type if not given */
  if((Type>=MAP_GUESS)&&(ROMMask[Slot]+1>4))
  {
  Type=GuessROM(P,Len<<13);
  if(Verbose) printf("guessed %s..\n",ROMNames[Type]);
  if(Slot<MAXCARTS) SETROMTYPE(Slot,Type);
  }

  /* Save MegaROM type */
  ROMType[Slot]=Type;

  /* For Generic/16kB carts, set ROM pages as 0:1:N-2:N-1 */
  if((Type==MAP_GEN16)&&(ROMMask[Slot]+1>4)) {
    if(Verbose) printf("SetMegaROM ..\n");
    SetMegaROM(Slot,0,1,ROMMask[Slot]-1,ROMMask[Slot]);

  }

  /* If cartridge may need a SRAM... */
  if(MAP_SRAM(Type))
  {
    /* Delete previous SRAM resources */
    SRAMData[Slot] = EmptyRAM;
//    memset(SRAMData[Slot],0,0x4000);
  }

  /* Done setting up cartridge */
  reset_msx(Mode,RAMPages,VRAMPages);
  PRINTOK;

  /* Done loading cartridge */
  return(Pages);
}

#define SaveSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { SaveFlashSaveData((unsigned char *)(save_address+Size),(unsigned char *)&(Name),sizeof(Name)); Size+=sizeof(Name); }

#define SaveARRAY(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { SaveFlashSaveData((unsigned char *)(save_address+Size),(unsigned char *)(Name),sizeof(Name)); Size+=sizeof(Name); }

#define SaveDATA(Name,DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
else { SaveFlashSaveData((unsigned char *)(save_address+Size),(unsigned char *)(Name),(DataSize)); Size+=(DataSize); }

#define LoadSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy(&(Name),(void *)(Buf+Size),sizeof(Name));Size+=sizeof(Name); }

#define SkipSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else Size+=sizeof(Name)

#define LoadARRAY(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy((Name),(void *)(Buf+Size),sizeof(Name));Size+=sizeof(Name); }

#define LoadDATA(Name,DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
  else { memcpy((Name),(void *)(Buf+Size),(DataSize));Size+=(DataSize); }

#define SkipDATA(DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
  else Size+=(DataSize)

#define WORK_BLOCK_SIZE (4096)
static int flashBlockOffset = 0;
static bool isLastFlashWrite = 0;

// This function fills 4kB blocks and writes them in flash when full
static void SaveFlashSaveData(unsigned char *dest, unsigned char *src, int size) {
  int blockNumber = 0;
  for (int i = 0; i<size;i++) {
    emulator_framebuffer[flashBlockOffset] = src[i];
    flashBlockOffset++;
    if ((flashBlockOffset == WORK_BLOCK_SIZE) || (isLastFlashWrite && (i == size-1))) {
      // Write block in flash
      int intDest = (int)dest+blockNumber*WORK_BLOCK_SIZE;
      intDest = intDest & ~(WORK_BLOCK_SIZE-1);
      unsigned char *newDest = (unsigned char *)intDest;
      OSPI_DisableMemoryMappedMode();
      OSPI_Program((uint32_t)newDest,(const uint8_t *)emulator_framebuffer,WORK_BLOCK_SIZE);
      OSPI_EnableMemoryMappedMode();
      flashBlockOffset = 0;
      blockNumber++;
    }
  }
}

/** SaveMsxStateFlash() **************************************/
/** Save emulation state into flash.                        **/
/*************************************************************/
int SaveMsxStateFlash(unsigned char *address, int MaxSize)
{
  unsigned int State[256],Size;
  static byte Header[16] = "STE\032\004\0\0\0\0\0\0\0\0\0\0\0";
  unsigned int I,J,K;

  // Convert mem mapped pointer to flash address
  uint32_t save_address = address - &__EXTFLASH_BASE__;

  /* No data written yet */
  Size = 0;
  flashBlockOffset = 0;
  isLastFlashWrite = 0;

  // Erase flash memory
  store_erase(address, MaxSize);

  /* Prepare the header */
  J=StateID();
  Header[5] = RAMPages;
  Header[6] = VRAMPages;
  Header[7] = J&0x00FF;
  Header[8] = J>>8;

  /* Fill out hardware state */
  J=0;
  memset(State,0,sizeof(State));
  State[J++] = VDPData;
  State[J++] = PLatch;
  State[J++] = ALatch;
  State[J++] = VAddr;
  State[J++] = VKey;
  State[J++] = PKey;
  State[J++] = IRQPending;
  State[J++] = ScanLine;
  State[J++] = RTCReg;
  State[J++] = RTCMode;
  State[J++] = KanLetter;
  State[J++] = KanCount;
  State[J++] = IOReg;
  State[J++] = PSLReg;
  State[J++] = FMPACKey;
  State[J++] = msx_button_a_key_index;
  State[J++] = msx_button_b_key_index;

  /* Memory setup */
  for(I=0;I<4;++I)
  {
    State[J++] = SSLReg[I];
    State[J++] = PSL[I];
    State[J++] = SSL[I];
    State[J++] = EnWrite[I];
    State[J++] = RAMMapper[I];
  }

  /* Cartridge setup */
  for(I=0;I<MAXSLOTS;++I)
  {
    State[J++] = ROMType[I];
    for(K=0;K<4;++K) State[J++]=ROMMapper[I][K];
  }

  /* Write out data structures */
  SaveDATA(Header,16);
  SaveDATA(currentDiskName,80);
  SaveSTRUCT(MSXCPU);
  SaveSTRUCT(PPI);
  SaveSTRUCT(VDP);
  SaveARRAY(VDPStatus);
  SaveARRAY(Palette);
  SaveSTRUCT(PSG);
  SaveSTRUCT(OPLL);
  SaveSTRUCT(SCChip);
  SaveARRAY(State);
  SaveDATA(RAMData,RAMPages*0x4000);
  isLastFlashWrite = 1;
  SaveDATA(VRAM,VRAMPages*0x4000);

  /* Return amount of data written */
  return(Size);
}

/** LoadMsxStateFlash() **************************************/
/** Load emulation state from flash.                        **/
/*************************************************************/
int LoadMsxStateFlash(unsigned char *Buf)
{
  static byte Header[16];
  int State[256],J,I,K;
  unsigned int Size = 0;
  int MaxSize = 512*1024;

  /* Read and check the header */
  LoadDATA(Header,16);

  if(memcmp(Header,"STE\032\004",5))
  {
    return(-1);
  }
  if(Header[7]+Header[8]*256!=StateID())
  {
    return(-2);
  }
  if((Header[5]!=(RAMPages&0xFF))||(Header[6]!=(VRAMPages&0xFF)))
  {
    return(-3);
  }

  /* Load hardware state */
  LoadDATA(currentDiskName,80);
  LoadSTRUCT(MSXCPU);
  LoadSTRUCT(PPI);
  LoadSTRUCT(VDP);
  LoadARRAY(VDPStatus);
  LoadARRAY(Palette);
  LoadSTRUCT(PSG);
  LoadSTRUCT(OPLL);
  LoadSTRUCT(SCChip);
  LoadARRAY(State);
  LoadDATA(RAMData,RAMPages*0x4000);
  LoadDATA(VRAM,VRAMPages*0x4000);

  /* Parse hardware state */
  J=0;
  VDPData    = State[J++];
  PLatch     = State[J++];
  ALatch     = State[J++];
  VAddr      = State[J++];
  VKey       = State[J++];
  PKey       = State[J++];
  IRQPending = State[J++];
  ScanLine   = State[J++];
  RTCReg     = State[J++];
  RTCMode    = State[J++];
  KanLetter  = State[J++];
  KanCount   = State[J++];
  IOReg      = State[J++];
  PSLReg     = State[J++];
  FMPACKey   = State[J++];
  msx_button_a_key_index = State[J++];
  msx_button_b_key_index = State[J++];

  /* Memory setup */
  for(I=0;I<4;++I)
  {
    SSLReg[I]       = State[J++];
    PSL[I]          = State[J++];
    SSL[I]          = State[J++];
    EnWrite[I]      = State[J++];
    RAMMapper[I]    = State[J++];
  }

  /* Cartridge setup */
  for(I=0;I<MAXSLOTS;++I)
  {
    ROMType[I] = State[J++];
    for(K=0;K<4;++K) {
      ROMMapper[I][K]=State[J++];
    }
  }

  /* Set RAM mapper pages */
  if(RAMMask)
  for(I=0;I<4;++I)
  {
    RAMMapper[I]       &= RAMMask;
    MemMap[3][2][I*2]   = RAMData+RAMMapper[I]*0x4000;
    MemMap[3][2][I*2+1] = MemMap[3][2][I*2]+0x2000;
  }

  /* Set ROM mapper pages */
  for(I=0;I<MAXSLOTS;++I)
  if(ROMData[I]&&ROMMask[I])
    SetMegaROM(I,ROMMapper[I][0],ROMMapper[I][1],ROMMapper[I][2],ROMMapper[I][3]);

  /* Set main address space pages */
  for(I=0;I<4;++I)
  {
    RAM[2*I]   = MemMap[PSL[I]][SSL[I]][2*I];
    RAM[2*I+1] = MemMap[PSL[I]][SSL[I]][2*I+1];
  }

  /* Set palette */
  for(I=0;I<16;++I)
  SetColor(I,(Palette[I]>>16)&0xFF,(Palette[I]>>8)&0xFF,Palette[I]&0xFF);

  /* Set screen mode and VRAM table addresses */
  SetScreen();

  /* Set some other variables */
  VPAGE    = VRAM+((int)VDP[14]<<14);
  FGColor  = VDP[7]>>4;
  BGColor  = VDP[7]&0x0F;
  XFGColor = FGColor;
  XBGColor = BGColor;

  /* All sound channels could have been changed */
  PSG.Changed     = (1<<AY8910_CHANNELS)-1;
  SCChip.Changed  = (1<<SCC_CHANNELS)-1;
  SCChip.WChanged = (1<<SCC_CHANNELS)-1;
  OPLL.Changed    = (1<<YM2413_CHANNELS)-1;
  OPLL.PChanged   = (1<<YM2413_CHANNELS)-1;
  OPLL.DChanged   = (1<<YM2413_CHANNELS)-1;

  /* Load correct disk if needed */
  if (strlen(currentDiskName) != 0) {
    msx_change_disk(0,currentDiskName);
  }

  /* Return amount of data read */
  return(Size);
}