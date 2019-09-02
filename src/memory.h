/****************************************************************************
*
*  APPLE //E EMULATOR FOR WINDOWS
*
*  Copyright (C) 1994-96, Michael O'Brien.  All rights reserved.
*
***/

#pragma once

constexpr int MAXIMAGES = 16;

typedef BYTE (* iofunction)(WORD, BYTE, BYTE, BYTE);


extern iofunction ioread[0x100];
extern iofunction iowrite[0x100];
extern LPBYTE     mem;
extern LPBYTE     memdirty;
extern LPBYTE     memwrite[MAXIMAGES][0x100];
extern DWORD      pages;

void   MemDestroy();
LPBYTE MemGetAuxPtr(WORD offset);
LPBYTE MemGetMainPtr(WORD offset);
void   MemInitialize();
void   MemReset();
void   MemResetPaging();
BYTE   MemReturnRandomData(BOOL highbit);

BYTE MemCheckPaging(WORD, BYTE, BYTE, BYTE);
BYTE MemSetPaging(WORD, BYTE, BYTE, BYTE);