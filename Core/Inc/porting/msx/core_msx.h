#ifndef _CORE_MSX_H_
#define _CORE_MSX_H_

int msx_start(int NewMode,int NewRAMPages,int NewVRAMPages, unsigned char *SaveState);
int reset_msx(int NewMode,int NewRAMPages,int NewVRAMPages);
byte msx_change_disk(byte N,const char *FileName,const char *Address);
int SaveMsxStateFlash(unsigned char *address, int MaxSize);
int LoadMsxStateFlash(unsigned char *Buf);

#endif