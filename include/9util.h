#ifndef NINEUTIL_H
#define NINEUTIL_H

#include <stdint.h>

void advbuf(_9ppkt*, size_t);
void bufcpy(_9ppkt*, void*, size_t);

_9pfid* fidtbl(_9pfid*, uint32_t, _9pfidop);
_9pfid* newfid(_9pfid*);

int pstring(char*, _9ppkt*);
int pnstring(char*, size_t, _9ppkt*);
int hstring(char*, uint16_t, _9ppkt*);
int hqid(_9pqid*, _9ppkt*);

void htop8(uint8_t, _9ppkt*);
void htop16(uint16_t, _9ppkt*);
void htop32(uint32_t, _9ppkt*);
void htop64(uint64_t, _9ppkt*);

void ptoh8(uint8_t *dest, _9ppkt*);
void ptoh16(uint16_t *dest, _9ppkt*);
void ptoh32(uint32_t *dest, _9ppkt*);
void ptoh64(uint64_t *dest, _9ppkt*);

void initrand(void);
uint32_t randu32(void);

#endif
