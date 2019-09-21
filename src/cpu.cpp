/****************************************************************************
*
*  APPLE //E EMULATOR FOR WINDOWS
*
*  Copyright (C) 1994-96, Michael O'Brien.  All rights reserved.
*
***/

#include "pch.h"
#pragma  hdrstop

constexpr BYTE AF_SIGN      = 0x80;
constexpr BYTE AF_OVERFLOW  = 0x40;
constexpr BYTE AF_RESERVED  = 0x20;
constexpr BYTE AF_BREAK     = 0x10;
constexpr BYTE AF_DECIMAL   = 0x08;
constexpr BYTE AF_INTERRUPT = 0x04;
constexpr BYTE AF_ZERO      = 0x02;
constexpr BYTE AF_CARRY     = 0x01;

constexpr int SHORTOPCODES = 22;
constexpr int BENCHOPCODES = 33;

registers regs;

static CpuType cpuType;

static const BYTE benchopcode[BENCHOPCODES] = {
    0x06, 0x16, 0x24, 0x45, 0x48, 0x65, 0x68, 0x76,
    0x84, 0x85, 0x86, 0x91, 0x94, 0xA4, 0xA5, 0xA6,
    0xB1, 0xB4, 0xC0, 0xC4, 0xC5, 0xE6, 0x19, 0x6D,
    0x8D, 0x99, 0x9D, 0xAD, 0xB9, 0xBD, 0xDD, 0xED,
    0xEE
};


/****************************************************************************
*
*  GENERAL PURPOSE MACROS
*
***/

#define AF_TO_EF() flagc = (regs.ps & AF_CARRY);                            \
                   flagn = (regs.ps & AF_SIGN);                             \
                   flagv = (regs.ps & AF_OVERFLOW);                         \
                   flagz = (regs.ps & AF_ZERO)
#define EF_TO_AF() regs.ps = (regs.ps & ~(AF_CARRY | AF_SIGN |              \
                                         AF_OVERFLOW | AF_ZERO))            \
                              | (flagc ? AF_CARRY    : 0)                   \
                              | (flagn ? AF_SIGN     : 0)                   \
                              | (flagv ? AF_OVERFLOW : 0)                   \
                              | (flagz ? AF_ZERO     : 0)
#define CMOS     if (!apple2e) { ++*cyclecounter; break; }
#define CYC(a)   *cyclecounter += (a)
#define POP()    (mem[regs.sp >= 0x1FF ? (regs.sp = 0x100) : ++regs.sp])
#define PUSH(a)  mem[regs.sp--] = (a);                                      \
                 if (regs.sp < 0x100) { regs.sp = 0x1FF; }
#define READ()   ((addr & 0xFF00) == 0xC000                                 \
                    ? ioRead[addr & 0xFF](regs.pc,(BYTE)addr,0,0)           \
                    : mem[addr])
#define SETNZ(a) {                                                          \
                   flagn = ((a) & 0x80);                                    \
                   flagz = !((a) & 0xFF);                                   \
                 }
#define SETZ(a)  flagz = !((a) & 0xFF)
#define TOBCD(a) (((((a) / 10) % 10) << 4) | ((a) % 10))
#define TOBIN(a) (((a) >> 4) * 10 + ((a) & 0x0F))
#define WRITE(a) {                                                          \
                   memDirty[addr >> 8] = 0xFF;                              \
                   LPBYTE page = memWrite[0][addr >> 8];                    \
                   if (page)                                                \
                     page[addr & 0xFF] = (BYTE)(a);                         \
                   else if ((addr & 0xFF00) == 0xC000)                      \
                     ioWrite[addr & 0xFF](regs.pc,(BYTE)addr,1,(BYTE)(a));  \
                 }


/****************************************************************************
*
*  ADDRESSING MODE MACROS
*
***/

#define ABS    addr = *(LPWORD)(mem + regs.pc); regs.pc += 2;
#define ABSX   addr = *(LPWORD)(mem + regs.pc) + (WORD)regs.x; regs.pc += 2;
#define ABSY   addr = *(LPWORD)(mem + regs.pc) + (WORD)regs.y; regs.pc += 2;
#define IABS   addr = *(LPWORD)(mem + *(LPWORD)(mem + regs.pc)); regs.pc += 2;
#define IABSX  addr = *(LPWORD)(mem + *(LPWORD)(mem + regs.pc) + (WORD)regs.x); regs.pc += 2;
#define IMM    addr = regs.pc++;
#define INDX   addr = *(LPWORD)(mem + ((mem[regs.pc++] + regs.x) & 0xFF));
#define INDY   addr = *(LPWORD)(mem + mem[regs.pc++]) + (WORD)regs.y;
#define IND    addr = *(LPWORD)(mem + mem[regs.pc++]);
#define REL    addr = (signed char)mem[regs.pc++];
#define ZPG    addr = mem[regs.pc++];
#define ZPGX   addr = (mem[regs.pc++] + regs.x) & 0xFF;
#define ZPGY   addr = (mem[regs.pc++] + regs.y) & 0xFF;


/****************************************************************************
*
*  INSTRUCTION MACROS
*
***/

#define ADC      temp = READ();                                             \
                 if (regs.ps & AF_DECIMAL) {                                \
                   val    = TOBIN(regs.a)+TOBIN(temp)+(flagc != 0);         \
                   flagc  = (val > 99);                                     \
                   regs.a = TOBCD(val);                                     \
                   if (apple2e)                                             \
                     SETNZ(regs.a);                                         \
                 }                                                          \
                 else {                                                     \
                   val    = regs.a+temp+(flagc != 0);                       \
                   flagc  = (val > 0xFF);                                   \
                   flagv  = (((regs.a & 0x80) == (temp & 0x80)) &&          \
                             ((regs.a & 0x80) != (val & 0x80)));            \
                   regs.a = val & 0xFF;                                     \
                   SETNZ(regs.a);                                           \
                 }
#define AND      regs.a &= READ();                                          \
                 SETNZ(regs.a);
#define ASL      val   = READ() << 1;                                       \
                 flagc = (val > 0xFF);                                      \
                 SETNZ(val);                                                \
                 WRITE(val);
#define ASLA     val   = regs.a << 1;                                       \
                 flagc = (val > 0xFF);                                      \
                 SETNZ(val);                                                \
                 regs.a = (BYTE)val;
#define BCC      if (!flagc) regs.pc += addr;
#define BCS      if ( flagc) regs.pc += addr;
#define BEQ      if ( flagz) regs.pc += addr;
#define BIT      val   = READ();                                            \
                 flagz = !(regs.a & val);                                   \
                 flagn = val & 0x80;                                        \
                 flagv = val & 0x40;
#define BITI     flagz = !(regs.a & READ());
#define BMI      if ( flagn) regs.pc += addr;
#define BNE      if (!flagz) regs.pc += addr;
#define BPL      if (!flagn) regs.pc += addr;
#define BRA      regs.pc += addr;
#define BRK      PUSH(regs.pc >> 8)                                         \
                 PUSH(regs.pc & 0xFF)                                       \
                 EF_TO_AF();                                                \
                 regs.ps |= AF_BREAK;                                       \
                 PUSH(regs.ps)                                              \
                 regs.ps |= AF_INTERRUPT;                                   \
                 regs.pc = *(LPWORD)(mem+0xFFFE);
#define BVC      if (!flagv) regs.pc += addr;
#define BVS      if ( flagv) regs.pc += addr;
#define CLC      flagc = 0;
#define CLD      regs.ps &= ~AF_DECIMAL;
#define CLI      regs.ps &= ~AF_INTERRUPT;
#define CLV      flagv = 0;
#define CMP      val   = READ();                                            \
                 flagc = (regs.a >= val);                                   \
                 val   = regs.a-val;                                        \
                 SETNZ(val);
#define CPX      val   = READ();                                            \
                 flagc = (regs.x >= val);                                   \
                 val   = regs.x-val;                                        \
                 SETNZ(val);
#define CPY      val   = READ();                                            \
                 flagc = (regs.y >= val);                                   \
                 val   = regs.y-val;                                        \
                 SETNZ(val);
#define DEA      --regs.a;                                                  \
                 SETNZ(regs.a);
#define DEC      val = READ()-1;                                            \
                 SETNZ(val);                                                \
                 WRITE(val);
#define DEX      --regs.x;                                                  \
                 SETNZ(regs.x);
#define DEY      --regs.y;                                                  \
                 SETNZ(regs.y);
#define EOR      regs.a ^= READ();                                          \
                 SETNZ(regs.a);
#define INA      ++regs.a;                                                  \
                 SETNZ(regs.a);
#define INC      val = READ()+1;                                            \
                 SETNZ(val);                                                \
                 WRITE(val);
#define INX      ++regs.x;                                                  \
                 SETNZ(regs.x);
#define INY      ++regs.y;                                                  \
                 SETNZ(regs.y);
#define JMP      regs.pc = addr;
#define JSR      --regs.pc;                                                 \
                 PUSH(regs.pc >> 8)                                         \
                 PUSH(regs.pc & 0xFF)                                       \
                 regs.pc = addr;
#define LDA      regs.a = READ();                                           \
                 SETNZ(regs.a);
#define LDX      regs.x = READ();                                           \
                 SETNZ(regs.x);
#define LDY      regs.y = READ();                                           \
                 SETNZ(regs.y);
#define LSR      val   = READ();                                            \
                 flagc = (val & 1);                                         \
                 flagn = 0;                                                 \
                 val >>= 1;                                                 \
                 SETZ(val);                                                 \
                 WRITE(val);
#define LSRA     flagc = (regs.a & 1);                                      \
                 flagn = 0;                                                 \
                 regs.a >>= 1;                                              \
                 SETZ(regs.a);
#define NOP
#define ORA      regs.a |= READ();                                          \
                 SETNZ(regs.a);
#define PHA      PUSH(regs.a)
#define PHP      EF_TO_AF();                                                \
                 regs.ps |= AF_RESERVED;                                    \
                 PUSH(regs.ps)
#define PHX      PUSH(regs.x)
#define PHY      PUSH(regs.y)
#define PLA      regs.a = POP();                                            \
                 SETNZ(regs.a);
#define PLP      regs.ps = POP();                                           \
                 AF_TO_EF();
#define PLX      regs.x = POP();                                            \
                 SETNZ(regs.x);
#define PLY      regs.y = POP();                                            \
                 SETNZ(regs.y);
#define ROL      val   = (READ() << 1) | (flagc != 0);                      \
                 flagc = (val > 0xFF);                                      \
                 SETNZ(val);                                                \
                 WRITE(val);
#define ROLA     val    = (((WORD)regs.a) << 1) | (flagc != 0);             \
                 flagc  = (val > 0xFF);                                     \
                 regs.a = val & 0xFF;                                       \
                 SETNZ(regs.a);
#define ROR      temp  = READ();                                            \
                 val   = (temp >> 1) | (flagc ? 0x80 : 0);                  \
                 flagc = temp & 1;                                          \
                 SETNZ(val);                                                \
                 WRITE(val);
#define RORA     val    = (((WORD)regs.a) >> 1) | (flagc ? 0x80 : 0);       \
                 flagc  = regs.a & 1;                                       \
                 regs.a = val & 0xFF;                                       \
                 SETNZ(regs.a);
#define RTI      regs.ps = POP();                                           \
                 AF_TO_EF();                                                \
                 regs.pc = POP();                                           \
                 regs.pc |= (((WORD)POP()) << 8);
#define RTS      regs.pc = POP();                                           \
                 regs.pc |= (((WORD)POP()) << 8);                           \
                 ++regs.pc;
#define SBC      temp = READ();                                             \
                 if (regs.ps & AF_DECIMAL) {                                \
                   val    = TOBIN(regs.a)-TOBIN(temp)-!flagc;               \
                   flagc  = (val < 0x8000);                                 \
                   regs.a = TOBCD(val);                                     \
                   if (apple2e)                                             \
                     SETNZ(regs.a);                                         \
                 }                                                          \
                 else {                                                     \
                   val    = regs.a-temp-!flagc;                             \
                   flagc  = (val < 0x8000);                                 \
                   flagv  = (((regs.a & 0x80) != (temp & 0x80)) &&          \
                             ((regs.a & 0x80) != (val & 0x80)));            \
                   regs.a = val & 0xFF;                                     \
                   SETNZ(regs.a);                                           \
                 }
#define SEC      flagc = 1;
#define SED      regs.ps |= AF_DECIMAL;
#define SEI      regs.ps |= AF_INTERRUPT;
#define STA      WRITE(regs.a);
#define STX      WRITE(regs.x);
#define STY      WRITE(regs.y);
#define STZ      WRITE(0);
#define TAX      regs.x = regs.a;                                           \
                 SETNZ(regs.x);
#define TAY      regs.y = regs.a;                                           \
                 SETNZ(regs.y);
#define TRB      val   = READ();                                            \
                 flagz = !(regs.a & val);                                   \
                 val  &= ~regs.a;                                           \
                 WRITE(val);
#define TSB      val   = READ();                                            \
                 flagz = !(regs.a & val);                                   \
                 val   |= regs.a;                                           \
                 WRITE(val);
#define TSX      regs.x = regs.sp & 0xFF;                                   \
                 SETNZ(regs.x);
#define TXA      regs.a = regs.x;                                           \
                 SETNZ(regs.a);
#define TXS      regs.sp = 0x100 | regs.x;
#define TYA      regs.a = regs.y;                                           \
                 SETNZ(regs.a);
#define INVALID1
#define INVALID2 if (apple2e) ++regs.pc;
#define INVALID3 if (apple2e) regs.pc += 2;


/****************************************************************************
*
*   Addressing mode helpers
*
***/

//===========================================================================
static inline uint16_t Absolute() {
    uint16_t addr = *(uint16_t *)(mem + regs.pc);
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t AbsoluteX() {
    uint16_t addr = *(uint16_t *)(mem + regs.pc) + uint16_t(regs.x);
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t AbsoluteX(int * cycles) {
    uint16_t oldaddr = regs.pc;
    uint16_t addr = *(uint16_t *)(mem + regs.pc) + uint16_t(regs.x);
    if ((oldaddr ^ addr) & 0xff00)
        ++*cycles;
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t AbsoluteY(int * cycles) {
    uint16_t oldaddr = regs.pc;
    uint16_t addr = *(uint16_t *)(mem + regs.pc) + uint16_t(regs.y);
    if ((oldaddr ^ addr) & 0xff00)
        ++*cycles;
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t Immediate() {
    return regs.pc++;
}

//===========================================================================
static inline uint16_t Indirect() {
    uint16_t addr = *(uint16_t *)(mem + *(uint16_t *)(mem + regs.pc));
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t Indirect_6502() {
    uint16_t addr;
    if (mem[regs.pc] == 0xff) {
        uint16_t addr0 = *(uint16_t *)(mem + regs.pc);
        uint16_t addr1 = addr0 - 0xff;
        addr = uint16_t(mem[addr0]) | uint16_t(mem[addr1] << 8);
    }
    else
        addr = *(uint16_t *)(mem + *(uint16_t *)(mem + regs.pc));
    regs.pc += 2;
    return addr;
}

//===========================================================================
static inline uint16_t IndirectX() {
    uint16_t addr = *(uint16_t *)(mem + ((mem[regs.pc] + regs.x) & 0xff));
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t IndirectY(int * cycles) {
    uint16_t oldaddr = regs.pc;
    uint16_t addr = *(uint16_t *)(mem + mem[regs.pc]) + (uint16_t)regs.y;
    if ((oldaddr ^ addr) & 0xff00)
        ++*cycles;
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t IndirectZeroPage() {
    uint16_t addr = *(uint16_t *)(mem + mem[regs.pc]);
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t Relative() {
    uint16_t addr = regs.pc + (int8_t)mem[regs.pc];
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t ZeroPage() {
    uint16_t addr = mem[regs.pc];
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t ZeroPageX() {
    uint16_t addr = uint8_t(mem[regs.pc] + regs.x);
    ++regs.pc;
    return addr;
}

//===========================================================================
static inline uint16_t ZeroPageY() {
    uint16_t addr = uint8_t(mem[regs.pc] + regs.y);
    ++regs.pc;
    return addr;
}


/****************************************************************************
*
*  Memory access helpers
*
***/

//===========================================================================
static inline void Branch(uint8_t condition, uint16_t addr, int * cycles) {
    if (condition) {
        uint16_t oldaddr = regs.pc;
        regs.pc = addr;
        *cycles += 1 + ((oldaddr ^ addr) ^ 0xff00) ? 1 : 0;
    }
}

//===========================================================================
static inline uint8_t Pop8() {
    regs.sp = ((regs.sp + 1) & 0xff) | 0x100;
    return mem[regs.sp];
}

//===========================================================================
static inline uint16_t Pop16() {
    uint16_t addr0 = Pop8();
    uint16_t addr1 = Pop8();
    return addr0 | addr1 << 8;
}

//===========================================================================
static inline void Push8(uint8_t value) {
    mem[regs.sp] = value;
    regs.sp = (regs.sp - 1) | 0x100;
}

//===========================================================================
static inline uint8_t Read8(uint16_t addr) {
    if ((addr & 0xff00) == 0xc000)
        return ioRead[addr & 0xff](regs.pc, (uint8_t)addr, 0, 0);
    else
        return mem[addr];
}

//===========================================================================
static inline uint16_t Read16(uint16_t addr) {
    return *(uint16_t *)(mem + addr);
}

//===========================================================================
static inline void Write8(uint16_t addr, uint8_t value) {
    int       offset = addr & 0xff;
    int       pageno = addr >> 8;
    uint8_t * page   = memWrite[0][pageno];
    memDirty[pageno] = 0xff;
    if (page)
        page[offset] = value;
    else if (pageno == 0xc0)
        ioWrite[offset](regs.pc, uint8_t(offset), 1, value);
}


/****************************************************************************
*
*   Status bit helpers
*
***/

//===========================================================================
static inline void UpdateZ(uint8_t value) {
    if (value)
        regs.ps &= ~AF_ZERO;
    else
        regs.ps |= AF_ZERO;
}

//===========================================================================
static inline void UpdateNZ(uint8_t value) {
    if (value & 0x80)
        regs.ps |= AF_SIGN;
    else
        regs.ps &= ~AF_SIGN;
    if (value)
        regs.ps &= ~AF_ZERO;
    else
        regs.ps |= AF_ZERO;
}


/****************************************************************************
*
*   Instruction implementations
*
***/

//===========================================================================
static inline void Adc_6502(uint8_t val8) {
    uint32_t acc   = uint32_t(regs.a);
    uint32_t add   = uint32_t(val8);
    uint32_t carry = uint32_t(regs.ps & AF_CARRY);
    uint32_t newval;
    if (regs.ps & AF_DECIMAL) {
        uint32_t lo = (acc & 0x0f) + (add & 0x0f) + carry;
        uint32_t carrylo = 0;
        if (lo >= 0x0a) {
            carrylo = 0x10;
            lo -= 0x0a;
        }

        uint32_t hi = (acc & 0xf0) + (add & 0xf0) + carrylo;
        if (hi >= 0xa0) {
            regs.ps |= AF_CARRY;
            hi -= 0xa0;
        }
        else
            regs.ps &= ~AF_CARRY;

        newval = hi | lo;
        if (((acc ^ newval) & 0x80) && !((acc ^ add) & 0x80))
            regs.ps |= AF_OVERFLOW;
        else
            regs.ps &= ~AF_OVERFLOW;
    }
    else {
        newval = acc + add + carry;
        if (newval & 0x100)
            regs.ps |= AF_CARRY;
        else
            regs.ps &= ~AF_CARRY;

        if ((acc & 0x80) == (add & 0x80) && (acc & 0x80) != (newval & 0x80))
            regs.ps |= AF_OVERFLOW;
        else
            regs.ps &= ~AF_OVERFLOW;
    }

    regs.a = uint8_t(newval);
    UpdateNZ(regs.a);
}

//===========================================================================
static inline void And(uint8_t val8) {
    regs.a &= val8;
    UpdateNZ(regs.a);
}

//===========================================================================
static inline uint8_t Asl(uint8_t val8) {
    if (val8 & 0x80)
        regs.ps |= AF_CARRY;
    else
        regs.ps &= ~AF_CARRY;
    val8 <<= 1;
    UpdateNZ(val8);
    return val8;
}

//===========================================================================
static inline void Bit(uint8_t val8) {
    UpdateZ(regs.a & val8);
    if (val8 & 0x80)
        regs.ps |= AF_SIGN;
    else
        regs.ps &= ~AF_SIGN;
    if (val8 & 0x40)
        regs.ps |= AF_OVERFLOW;
    else
        regs.ps &= ~AF_OVERFLOW;
}

//===========================================================================
static inline void Brk() {
    Push8(regs.pc >> 8);
    Push8(regs.pc & 0xff);
    regs.ps |= AF_BREAK;
    Push8(regs.ps);
    regs.ps |= AF_INTERRUPT;
    regs.pc = Read16(0xfffe);
}

//===========================================================================
static inline void Clc() {
    regs.ps &= ~AF_CARRY;
}

//===========================================================================
static inline void Cli() {
    regs.ps &= ~AF_INTERRUPT;
}

//===========================================================================
static inline void Eor(uint8_t val8) {
    regs.a ^= val8;
    UpdateNZ(regs.a);
}

//===========================================================================
static inline uint8_t Lsr(uint8_t val8) {
    if (val8 & 1)
        regs.ps |= AF_CARRY;
    else
        regs.ps &= ~AF_CARRY;
    regs.ps &= ~AF_SIGN;
    val8 >>= 1;
    UpdateZ(val8);
    return val8;
}

//===========================================================================
static inline void Jmp(uint16_t addr) {
    regs.pc = addr;
}

//===========================================================================
static inline void Jsr(uint16_t addr) {
    --regs.pc;
    Push8(regs.pc >> 8);
    Push8(regs.pc & 0xff);
    regs.pc = addr;
}

//===========================================================================
static inline void Ora(uint8_t val8) {
    regs.a |= val8;
    UpdateNZ(regs.a);
}

//===========================================================================
static inline void Php() {
    regs.ps |= AF_RESERVED;
    Push8(regs.ps);
}

//===========================================================================
static inline void Pla() {
    regs.a = Pop8();
    UpdateNZ(regs.a);
}

//===========================================================================
static inline uint8_t Rol(uint8_t val8) {
    uint8_t hibit = val8 >> 7;
    val8 = val8 << 1 | (regs.ps & AF_CARRY);
    if (hibit)
        regs.ps |= AF_CARRY;
    else
        regs.ps &= ~AF_CARRY;
    UpdateNZ(val8);
    return val8;
}

//===========================================================================
static inline uint8_t Ror(uint8_t val8) {
    uint8_t lobit = val8 & 1;
    val8 = val8 >> 1 | ((regs.ps & AF_CARRY) << 7);
    if (lobit)
        regs.ps |= AF_CARRY;
    else
        regs.ps &= ~AF_CARRY;
    UpdateNZ(val8);
    return val8;
}

//===========================================================================
static inline void Rti() {
    regs.ps = Pop8();
    regs.pc = Pop16();
}

//===========================================================================
static inline void Rts() {
    regs.pc = Pop16() + 1;
}

//===========================================================================
static inline void Sec() {
    regs.ps |= AF_CARRY;
}

//===========================================================================
static inline uint8_t Trb(uint8_t val8) {
    UpdateZ(regs.a & val8);
    val8 &= ~regs.a;
    return val8;
}

//===========================================================================
static inline uint8_t Tsb(uint8_t val8) {
    UpdateZ(val8 & regs.a);
    val8 |= regs.a;
    return val8;
}

//===========================================================================
int CpuStep() {
    int      cycles = 0;
    uint16_t addr;
    uint8_t  val8;

    switch (mem[regs.pc++]) {
        case 0x03: // invalid1
        case 0x04:
        case 0x07:
        case 0x0b:
        case 0x0c:
        case 0x0f:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x17:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1f:
        case 0x27:
        case 0x23:
        case 0x2b:
        case 0x2f:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x37:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3f:
        case 0x43:
        case 0x47:
        case 0x4b:
        case 0x4f:
        case 0x52:
        case 0x53:
        case 0x57:
        case 0x5a:
        case 0x5b:
        case 0x5f:
        case 0x63:
        case 0x64:
        case 0x67:
        case 0x6b:
        case 0x6f:
            cycles += 1;
            break;

        case 0x02: // invalid2
        case 0x22:
        case 0x42:
        case 0x44:
        case 0x54:
        case 0x62:
        case 0x82:
        case 0xc2:
        case 0xd4:
        case 0xe2:
        case 0xf4:
            cycles += 2;
            break;

        case 0x00: // BRK
            Brk();
            cycles += 7;
            break;
        case 0x01: // ORA (indx)
            addr = IndirectX();
            val8 = Read8(addr);
            Ora(val8);
            cycles += 6;
            break;
        case 0x05: // ORA (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            Ora(val8);
            cycles += 3;
            break;
        case 0x06: // ASL (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            val8 = Asl(val8);
            Write8(addr, val8);
            cycles += 5;
            break;
        case 0x08: // PHP
            Php();
            cycles += 3;
            break;
        case 0x09: // ORA (imm)
            addr = Immediate();
            val8 = Read8(addr);
            Ora(val8);
            cycles += 2;
            break;
        case 0x0a: // ASL (acc)
            val8 = regs.a;
            val8 = Asl(val8);
            regs.a = val8;
            cycles += 2;
            break;
        case 0x0d: // ORA (abs)
            addr = Absolute();
            val8 = Read8(addr);
            Ora(val8);
            cycles += 4;
            break;
        case 0x0e: // ASL (abs)
            addr = Absolute();
            val8 = Read8(addr);
            val8 = Asl(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x10: // BPL
            addr = Relative();
            Branch(!(regs.ps & AF_SIGN), addr, &cycles);
            cycles += 2;
            break;
        case 0x11: // ORA (indy)
            addr = IndirectY(&cycles);
            val8 = Read8(addr);
            Ora(val8);
            cycles += 5;
            break;
        case 0x15: // ORA (zpgx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            Ora(val8);
            cycles += 4;
            break;
        case 0x16: // ASL (zpgx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            val8 = Asl(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x18: // CLC
            regs.ps &= AF_CARRY;
            cycles += 1;
            break;
        case 0x19: // ORA (absy)
            addr = AbsoluteY(&cycles);
            val8 = Read8(addr);
            Ora(val8);
            cycles += 4;
            break;
        case 0x1d: // ORA (absx)
            addr = AbsoluteX(&cycles);
            val8 = Read8(addr);
            Ora(val8);
            cycles += 4;
            break;
        case 0x1e: // ASL (absx)
            addr = AbsoluteX();
            val8 = Read8(addr);
            val8 = Asl(val8);
            Write8(addr, val8);
            cycles += 7;
            break;
        case 0x20: // JSR (abs)
            addr = Absolute();
            Jsr(addr);
            cycles += 6;
            break;
        case 0x21: // AND (indx)
            addr = IndirectX();
            val8 = Read8(addr);
            And(val8);
            cycles += 6;
            break;
        case 0x24: // BIT (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            Bit(val8);
            cycles += 3;
            break;
        case 0x25: // AND (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            And(val8);
            cycles += 3;
            break;
        case 0x26: // ROL (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            val8 = Rol(val8);
            Write8(addr, val8);
            cycles += 5;
            break;
        case 0x28: // PLP
            regs.ps = Pop8();
            cycles += 4;
            break;
        case 0x29: // AND (imm)
            addr = Immediate();
            val8 = Read8(addr);
            And(val8);
            cycles += 2;
            break;
        case 0x2a: // ROL (acc)
            regs.a = Rol(regs.a);
            cycles += 2;
            break;
        case 0x2c: // BIT (abs)
            addr = Absolute();
            val8 = Read8(addr);
            Bit(val8);
            cycles += 4;
            break;
        case 0x2d: // AND (abs)
            addr = Absolute();
            val8 = Read8(addr);
            And(val8);
            cycles += 4;
            break;
        case 0x2e: // ROL (abs)
            addr = Absolute();
            val8 = Read8(addr);
            val8 = Rol(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x30: // BMI
            addr = Relative();
            Branch(regs.ps & AF_SIGN, addr, &cycles);
            cycles += 2;
            break;
        case 0x31: // AND (indy)
            addr = IndirectY(&cycles);
            val8 = Read8(addr);
            And(val8);
            cycles += 5;
            break;
        case 0x35: // AND (zpx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            And(val8);
            cycles += 4;
            break;
        case 0x36: // ROL (zpx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            val8 = Rol(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x38: // SEC
            Sec();
            cycles += 2;
            break;
        case 0x39: // AND (absy)
            addr = AbsoluteY(&cycles);
            val8 = Read8(addr);
            And(val8);
            cycles += 4;
            break;
        case 0x3d: // AND (absx)
            addr = AbsoluteX(&cycles);
            val8 = Read8(addr);
            And(val8);
            cycles += 4;
            break;
        case 0x3e: // ROL (absx)
            addr = AbsoluteX();
            val8 = Read8(addr);
            val8 = Rol(val8);
            Write8(addr, val8);
            cycles += 7;
            break;
        case 0x40: // RTI
            Rti();
            cycles += 6;
            break;
        case 0x41: // EOR (indx)
            addr = IndirectX();
            val8 = Read8(addr);
            Eor(val8);
            cycles += 6;
            break;
        case 0x45: // EOR (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            Eor(val8);
            cycles += 3;
            break;
        case 0x46: // LSR (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            val8 = Lsr(val8);
            Write8(addr, val8);
            cycles += 5;
            break;
        case 0x48: // PHA
            Push8(regs.a);
            cycles += 3;
            break;
        case 0x49: // EOR (imm)
            addr = Immediate();
            val8 = Read8(addr);
            Eor(val8);
            cycles += 2;
            break;
        case 0x4a: // LSR (acc)
            regs.a = Lsr(regs.a);
            cycles += 2;
            break;
        case 0x4c: // JMP (abs)
            addr = Absolute();
            Jmp(addr);
            cycles += 3;
            break;
        case 0x4d: // EOR (abs)
            addr = Absolute();
            val8 = Read8(addr);
            Eor(val8);
            cycles += 4;
            break;
        case 0x4e: // LSR (abs)
            addr = Absolute();
            val8 = Read8(addr);
            val8 = Lsr(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x50: // BVC
            addr = Relative();
            Branch(!(regs.ps & AF_OVERFLOW), addr, &cycles);
            cycles += 2;
            break;
        case 0x51: // EOR (indy)
            addr = IndirectY(&cycles);
            val8 = Read8(addr);
            Eor(val8);
            cycles += 5;
            break;
        case 0x55: // EOR (zpx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            Eor(val8);
            cycles += 4;
            break;
        case 0x56: // LSR (zpx)
            addr = ZeroPageX();
            val8 = Read8(addr);
            val8 = Lsr(val8);
            Write8(addr, val8);
            cycles += 6;
            break;
        case 0x58: // CLI
            Cli();
            cycles += 2;
            break;
        case 0x59: // EOR (absy)
            addr = AbsoluteY(&cycles);
            val8 = Read8(addr);
            Eor(val8);
            cycles += 4;
            break;
        case 0x5c: // invalid3
            cycles += 8;
            break;
        case 0x5d: // EOR (absx)
            addr = AbsoluteX(&cycles);
            val8 = Read8(addr);
            Eor(val8);
            cycles += 4;
            break;
        case 0x5e: // LSR (absx)
            addr = AbsoluteX();
            val8 = Read8(addr);
            val8 = Lsr(val8);
            Write8(addr, val8);
            cycles += 7;
            break;
        case 0x60: // RTS
            Rts();
            cycles += 6;
            break;
        case 0x61: // ADC (indx)
            addr = IndirectX();
            val8 = Read8(addr);
            Adc_6502(val8);
            cycles += 6;
            break;
        case 0x65: // ADC (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            Adc_6502(val8);
            cycles += 3;
            break;
        case 0x66: // ROR (zpg)
            addr = ZeroPage();
            val8 = Read8(addr);
            val8 = Ror(val8);
            Write8(addr, val8);
            cycles += 5;
            break;
        case 0x68: // PLA
            Pla();
            cycles += 4;
            break;
        case 0x69: // ADC (imm)
            addr = Immediate();
            val8 = Read8(addr);
            Adc_6502(val8);
            cycles += 2;
            break;
        case 0x6a: // ROR (acc)
            regs.a = Ror(regs.a);
            cycles += 2;
            break;
        case 0x6c: // JMP (iabs)
            addr = Indirect_6502();
            Jmp(addr);
            cycles += 6;
            break;
        case 0x6d: // ADC (abs)
            addr = Absolute();
            val8 = Read8(addr);
            Adc_6502(val8);
            cycles += 4;
            break;
        case 0x6e: // ROR (abs)
            addr = Absolute();
            val8 = Read8(addr);
            val8 = Ror(val8);
            Write8(addr, val8);
            cycles += 7;
            break;

#if 0
            case 0x70:       REL BVS       CYC(3);  break;
            case 0x71:       INDY ADC      CYC(5);  break;
            case 0x72: CMOS  IND ADC       CYC(5);  break;
            case 0x73:       INVALID1      CYC(1);  break;
            case 0x74: CMOS  ZPGX STZ      CYC(4);  break;
            case 0x75:       ZPGX ADC      CYC(4);  break;
            case 0x76:       ZPGX ROR      CYC(6);  break;
            case 0x77:       INVALID1      CYC(1);  break;
            case 0x78:       SEI           CYC(2);  break;
            case 0x79:       ABSY ADC      CYC(4);  break;
            case 0x7A: CMOS  PLY           CYC(4);  break;
            case 0x7B:       INVALID1      CYC(1);  break;
            case 0x7C: CMOS  IABSX JMP     CYC(6);  break;
            case 0x7D:       ABSX ADC      CYC(4);  break;
            case 0x7E:       ABSX ROR      CYC(6);  break;
            case 0x7F:       INVALID1      CYC(1);  break;
#endif

            case 0x81:
                addr = AbsoluteX(&cycles);
                Write8(addr, regs.a);
                cycles += 6;
                break;
    }

    return cycles;
}

//===========================================================================
int InternalCpuExecute(DWORD attemptcycles, int64_t * cyclecounter) {
    WORD addr;
    BOOL flagc;
    BOOL flagn;
    BOOL flagv;
    BOOL flagz;
    WORD temp;
    WORD val;
    AF_TO_EF();
    int64_t startcycles = *cyclecounter;
    do {
        switch (mem[regs.pc++]) {
            case 0x00:       BRK           CYC(7);  break;
            case 0x01:       INDX ORA      CYC(6);  break;
            case 0x02:       INVALID2      CYC(2);  break;
            case 0x03:       INVALID1      CYC(1);  break;
            case 0x04: CMOS  ZPG TSB       CYC(5);  break;
            case 0x05:       ZPG ORA       CYC(3);  break;
            case 0x06:       ZPG ASL       CYC(5);  break;
            case 0x07:       INVALID1      CYC(1);  break;
            case 0x08:       PHP           CYC(3);  break;
            case 0x09:       IMM ORA       CYC(2);  break;
            case 0x0A:       ASLA          CYC(2);  break;
            case 0x0B:       INVALID1      CYC(1);  break;
            case 0x0C: CMOS  ABS TSB       CYC(6);  break;
            case 0x0D:       ABS ORA       CYC(4);  break;
            case 0x0E:       ABS ASL       CYC(6);  break;
            case 0x0F:       INVALID1      CYC(1);  break;
            case 0x10:       REL BPL       CYC(3);  break;
            case 0x11:       INDY ORA      CYC(5);  break;
            case 0x12: CMOS  IND ORA       CYC(5);  break;
            case 0x13:       INVALID1      CYC(1);  break;
            case 0x14: CMOS  ZPG TRB       CYC(5);  break;
            case 0x15:       ZPGX ORA      CYC(4);  break;
            case 0x16:       ZPGX ASL      CYC(6);  break;
            case 0x17:       INVALID1      CYC(1);  break;
            case 0x18:       CLC           CYC(2);  break;
            case 0x19:       ABSY ORA      CYC(4);  break;
            case 0x1A: CMOS  INA           CYC(2);  break;
            case 0x1B:       INVALID1      CYC(1);  break;
            case 0x1C: CMOS  ABS TRB       CYC(6);  break;
            case 0x1D:       ABSX ORA      CYC(4);  break;
            case 0x1E:       ABSX ASL      CYC(6);  break;
            case 0x1F:       INVALID1      CYC(1);  break;
            case 0x20:       ABS JSR       CYC(6);  break;
            case 0x21:       INDX AND      CYC(6);  break;
            case 0x22:       INVALID2      CYC(2);  break;
            case 0x23:       INVALID1      CYC(1);  break;
            case 0x24:       ZPG BIT       CYC(3);  break;
            case 0x25:       ZPG AND       CYC(3);  break;
            case 0x26:       ZPG ROL       CYC(5);  break;
            case 0x27:       INVALID1      CYC(1);  break;
            case 0x28:       PLP           CYC(4);  break;
            case 0x29:       IMM AND       CYC(2);  break;
            case 0x2A:       ROLA          CYC(2);  break;
            case 0x2B:       INVALID1      CYC(1);  break;
            case 0x2C:       ABS BIT       CYC(4);  break;
            case 0x2D:       ABS AND       CYC(4);  break;
            case 0x2E:       ABS ROL       CYC(6);  break;
            case 0x2F:       INVALID1      CYC(1);  break;
            case 0x30:       REL BMI       CYC(3);  break;
            case 0x31:       INDY AND      CYC(5);  break;
            case 0x32: CMOS  IND AND       CYC(5);  break;
            case 0x33:       INVALID1      CYC(1);  break;
            case 0x34: CMOS  ZPGX BIT      CYC(4);  break;
            case 0x35:       ZPGX AND      CYC(4);  break;
            case 0x36:       ZPGX ROL      CYC(6);  break;
            case 0x37:       INVALID1      CYC(1);  break;
            case 0x38:       SEC           CYC(2);  break;
            case 0x39:       ABSY AND      CYC(4);  break;
            case 0x3A: CMOS  DEA           CYC(2);  break;
            case 0x3B:       INVALID1      CYC(1);  break;
            case 0x3C: CMOS  ABSX BIT      CYC(4);  break;
            case 0x3D:       ABSX AND      CYC(4);  break;
            case 0x3E:       ABSX ROL      CYC(6);  break;
            case 0x3F:       INVALID1      CYC(1);  break;
            case 0x40:       RTI           CYC(6);  break;
            case 0x41:       INDX EOR      CYC(6);  break;
            case 0x42:       INVALID2      CYC(2);  break;
            case 0x43:       INVALID1      CYC(1);  break;
            case 0x44:       INVALID2      CYC(3);  break;
            case 0x45:       ZPG EOR       CYC(3);  break;
            case 0x46:       ZPG LSR       CYC(5);  break;
            case 0x47:       INVALID1      CYC(1);  break;
            case 0x48:       PHA           CYC(3);  break;
            case 0x49:       IMM EOR       CYC(2);  break;
            case 0x4A:       LSRA          CYC(2);  break;
            case 0x4B:       INVALID1      CYC(1);  break;
            case 0x4C:       ABS JMP       CYC(3);  break;
            case 0x4D:       ABS EOR       CYC(4);  break;
            case 0x4E:       ABS LSR       CYC(6);  break;
            case 0x4F:       INVALID1      CYC(1);  break;
            case 0x50:       REL BVC       CYC(3);  break;
            case 0x51:       INDY EOR      CYC(5);  break;
            case 0x52: CMOS  IND EOR       CYC(5);  break;
            case 0x53:       INVALID1      CYC(1);  break;
            case 0x54:       INVALID2      CYC(4);  break;
            case 0x55:       ZPGX EOR      CYC(4);  break;
            case 0x56:       ZPGX LSR      CYC(6);  break;
            case 0x57:       INVALID1      CYC(1);  break;
            case 0x58:       CLI           CYC(2);  break;
            case 0x59:       ABSY EOR      CYC(4);  break;
            case 0x5A: CMOS  PHY           CYC(3);  break;
            case 0x5B:       INVALID1      CYC(1);  break;
            case 0x5C:       INVALID3      CYC(8);  break;
            case 0x5D:       ABSX EOR      CYC(4);  break;
            case 0x5E:       ABSX LSR      CYC(6);  break;
            case 0x5F:       INVALID1      CYC(1);  break;
            case 0x60:       RTS           CYC(6);  break;
            case 0x61:       INDX ADC      CYC(6);  break;
            case 0x62:       INVALID2      CYC(2);  break;
            case 0x63:       INVALID1      CYC(1);  break;
            case 0x64: CMOS  ZPG STZ       CYC(3);  break;
            case 0x65:       ZPG ADC       CYC(3);  break;
            case 0x66:       ZPG ROR       CYC(5);  break;
            case 0x67:       INVALID1      CYC(1);  break;
            case 0x68:       PLA           CYC(4);  break;
            case 0x69:       IMM ADC       CYC(2);  break;
            case 0x6A:       RORA          CYC(2);  break;
            case 0x6B:       INVALID1      CYC(1);  break;
            case 0x6C:       IABS JMP      CYC(6);  break;
            case 0x6D:       ABS ADC       CYC(4);  break;
            case 0x6E:       ABS ROR       CYC(6);  break;
            case 0x6F:       INVALID1      CYC(1);  break;
            case 0x70:       REL BVS       CYC(3);  break;
            case 0x71:       INDY ADC      CYC(5);  break;
            case 0x72: CMOS  IND ADC       CYC(5);  break;
            case 0x73:       INVALID1      CYC(1);  break;
            case 0x74: CMOS  ZPGX STZ      CYC(4);  break;
            case 0x75:       ZPGX ADC      CYC(4);  break;
            case 0x76:       ZPGX ROR      CYC(6);  break;
            case 0x77:       INVALID1      CYC(1);  break;
            case 0x78:       SEI           CYC(2);  break;
            case 0x79:       ABSY ADC      CYC(4);  break;
            case 0x7A: CMOS  PLY           CYC(4);  break;
            case 0x7B:       INVALID1      CYC(1);  break;
            case 0x7C: CMOS  IABSX JMP     CYC(6);  break;
            case 0x7D:       ABSX ADC      CYC(4);  break;
            case 0x7E:       ABSX ROR      CYC(6);  break;
            case 0x7F:       INVALID1      CYC(1);  break;
            case 0x80: CMOS  REL BRA       CYC(3);  break;
            case 0x81:       INDX STA      CYC(6);  break;
            case 0x82:       INVALID2      CYC(2);  break;
            case 0x83:       INVALID1      CYC(1);  break;
            case 0x84:       ZPG STY       CYC(3);  break;
            case 0x85:       ZPG STA       CYC(3);  break;
            case 0x86:       ZPG STX       CYC(3);  break;
            case 0x87:       INVALID1      CYC(1);  break;
            case 0x88:       DEY           CYC(2);  break;
            case 0x89: CMOS  IMM BITI      CYC(2);  break;
            case 0x8A:       TXA           CYC(2);  break;
            case 0x8B:       INVALID1      CYC(1);  break;
            case 0x8C:       ABS STY       CYC(4);  break;
            case 0x8D:       ABS STA       CYC(4);  break;
            case 0x8E:       ABS STX       CYC(4);  break;
            case 0x8F:       INVALID1      CYC(1);  break;
            case 0x90:       REL BCC       CYC(3);  break;
            case 0x91:       INDY STA      CYC(6);  break;
            case 0x92: CMOS  IND STA       CYC(5);  break;
            case 0x93:       INVALID1      CYC(1);  break;
            case 0x94:       ZPGX STY      CYC(4);  break;
            case 0x95:       ZPGX STA      CYC(4);  break;
            case 0x96:       ZPGY STX      CYC(4);  break;
            case 0x97:       INVALID1      CYC(1);  break;
            case 0x98:       TYA           CYC(2);  break;
            case 0x99:       ABSY STA      CYC(5);  break;
            case 0x9A:       TXS           CYC(2);  break;
            case 0x9B:       INVALID1      CYC(1);  break;
            case 0x9C: CMOS  ABS STZ       CYC(4);  break;
            case 0x9D:       ABSX STA      CYC(5);  break;
            case 0x9E: CMOS  ABSX STZ      CYC(5);  break;
            case 0x9F:       INVALID1      CYC(1);  break;
            case 0xA0:       IMM LDY       CYC(2);  break;
            case 0xA1:       INDX LDA      CYC(6);  break;
            case 0xA2:       IMM LDX       CYC(2);  break;
            case 0xA3:       INVALID1      CYC(1);  break;
            case 0xA4:       ZPG LDY       CYC(3);  break;
            case 0xA5:       ZPG LDA       CYC(3);  break;
            case 0xA6:       ZPG LDX       CYC(3);  break;
            case 0xA7:       INVALID1      CYC(1);  break;
            case 0xA8:       TAY           CYC(2);  break;
            case 0xA9:       IMM LDA       CYC(2);  break;
            case 0xAA:       TAX           CYC(2);  break;
            case 0xAB:       INVALID1      CYC(1);  break;
            case 0xAC:       ABS LDY       CYC(4);  break;
            case 0xAD:       ABS LDA       CYC(4);  break;
            case 0xAE:       ABS LDX       CYC(4);  break;
            case 0xAF:       INVALID1      CYC(1);  break;
            case 0xB0:       REL BCS       CYC(3);  break;
            case 0xB1:       INDY LDA      CYC(5);  break;
            case 0xB2: CMOS  IND LDA       CYC(5);  break;
            case 0xB3:       INVALID1      CYC(1);  break;
            case 0xB4:       ZPGX LDY      CYC(4);  break;
            case 0xB5:       ZPGX LDA      CYC(4);  break;
            case 0xB6:       ZPGY LDX      CYC(4);  break;
            case 0xB7:       INVALID1      CYC(1);  break;
            case 0xB8:       CLV           CYC(2);  break;
            case 0xB9:       ABSY LDA      CYC(4);  break;
            case 0xBA:       TSX           CYC(2);  break;
            case 0xBB:       INVALID1      CYC(1);  break;
            case 0xBC:       ABSX LDY      CYC(4);  break;
            case 0xBD:       ABSX LDA      CYC(4);  break;
            case 0xBE:       ABSY LDX      CYC(4);  break;
            case 0xBF:       INVALID1      CYC(1);  break;
            case 0xC0:       IMM CPY       CYC(2);  break;
            case 0xC1:       INDX CMP      CYC(6);  break;
            case 0xC2:       INVALID2      CYC(2);  break;
            case 0xC3:       INVALID1      CYC(1);  break;
            case 0xC4:       ZPG CPY       CYC(3);  break;
            case 0xC5:       ZPG CMP       CYC(3);  break;
            case 0xC6:       ZPG DEC       CYC(5);  break;
            case 0xC7:       INVALID1      CYC(1);  break;
            case 0xC8:       INY           CYC(2);  break;
            case 0xC9:       IMM CMP       CYC(2);  break;
            case 0xCA:       DEX           CYC(2);  break;
            case 0xCB:       INVALID1      CYC(1);  break;
            case 0xCC:       ABS CPY       CYC(4);  break;
            case 0xCD:       ABS CMP       CYC(4);  break;
            case 0xCE:       ABS DEC       CYC(5);  break;
            case 0xCF:       INVALID1      CYC(1);  break;
            case 0xD0:       REL BNE       CYC(3);  break;
            case 0xD1:       INDY CMP      CYC(5);  break;
            case 0xD2: CMOS  IND CMP       CYC(5);  break;
            case 0xD3:       INVALID1      CYC(1);  break;
            case 0xD4:       INVALID2      CYC(4);  break;
            case 0xD5:       ZPGX CMP      CYC(4);  break;
            case 0xD6:       ZPGX DEC      CYC(6);  break;
            case 0xD7:       INVALID1      CYC(1);  break;
            case 0xD8:       CLD           CYC(2);  break;
            case 0xD9:       ABSY CMP      CYC(4);  break;
            case 0xDA: CMOS  PHX           CYC(3);  break;
            case 0xDB:       INVALID1      CYC(1);  break;
            case 0xDC:       INVALID3      CYC(4);  break;
            case 0xDD:       ABSX CMP      CYC(4);  break;
            case 0xDE:       ABSX DEC      CYC(6);  break;
            case 0xDF:       INVALID1      CYC(1);  break;
            case 0xE0:       IMM CPX       CYC(2);  break;
            case 0xE1:       INDX SBC      CYC(6);  break;
            case 0xE2:       INVALID2      CYC(2);  break;
            case 0xE3:       INVALID1      CYC(1);  break;
            case 0xE4:       ZPG CPX       CYC(3);  break;
            case 0xE5:       ZPG SBC       CYC(3);  break;
            case 0xE6:       ZPG INC       CYC(5);  break;
            case 0xE7:       INVALID1      CYC(1);  break;
            case 0xE8:       INX           CYC(2);  break;
            case 0xE9:       IMM SBC       CYC(2);  break;
            case 0xEA:       NOP           CYC(2);  break;
            case 0xEB:       INVALID1      CYC(1);  break;
            case 0xEC:       ABS CPX       CYC(4);  break;
            case 0xED:       ABS SBC       CYC(4);  break;
            case 0xEE:       ABS INC       CYC(6);  break;
            case 0xEF:       INVALID1      CYC(1);  break;
            case 0xF0:       REL BEQ       CYC(3);  break;
            case 0xF1:       INDY SBC      CYC(5);  break;
            case 0xF2: CMOS  IND SBC       CYC(5);  break;
            case 0xF3:       INVALID1      CYC(1);  break;
            case 0xF4:       INVALID2      CYC(4);  break;
            case 0xF5:       ZPGX SBC      CYC(4);  break;
            case 0xF6:       ZPGX INC      CYC(6);  break;
            case 0xF7:       INVALID1      CYC(1);  break;
            case 0xF8:       SED           CYC(2);  break;
            case 0xF9:       ABSY SBC      CYC(4);  break;
            case 0xFA: CMOS  PLX           CYC(4);  break;
            case 0xFB:       INVALID1      CYC(1);  break;
            case 0xFC:       INVALID3      CYC(4);  break;
            case 0xFD:       ABSX SBC      CYC(4);  break;
            case 0xFE:       ABSX INC      CYC(6);  break;
            case 0xFF:       INVALID1      CYC(1);  break;
        }
    } while (*cyclecounter - startcycles < attemptcycles);
    EF_TO_AF();
    return int(*cyclecounter - startcycles);
}

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//===========================================================================
void CpuDestroy() {
}

//===========================================================================
int CpuExecute(DWORD cycles, int64_t * cyclecounter) {
    static BOOL laststep = FALSE;

    if (cycles == 0) {
        laststep = TRUE;
        return InternalCpuExecute(0, cyclecounter);
    }

    if (laststep)
        laststep = FALSE;

    return InternalCpuExecute(cycles, cyclecounter);
}

//===========================================================================
void CpuInitialize() {
    CpuDestroy();

    regs.a = 0;
    regs.x = 0;
    regs.y = 0;
    regs.ps = 0x20;
    regs.pc = *(LPWORD)(mem + 0xFFFC);
    regs.sp = 0x01FF;
}

//===========================================================================
void CpuSetType (CpuType type) {
    cpuType = type;
}

//===========================================================================
void CpuSetupBenchmark() {
    regs.a  = 0;
    regs.x  = 0;
    regs.y  = 0;
    regs.pc = 0x300;
    regs.sp = 0x1FF;

    // CREATE CODE SEGMENTS CONSISTING OF GROUPS OF COMMONLY-USED OPCODES
    int addr = 0x300;
    int opcode = 0;
    do {
        mem[addr++] = benchopcode[opcode];
        mem[addr++] = benchopcode[opcode];
        if (opcode >= SHORTOPCODES)
            mem[addr++] = 0;
        if ((++opcode >= BENCHOPCODES) || ((addr & 0x0F) >= 0x0B)) {
            mem[addr++] = 0x4C;
            mem[addr++] = (opcode >= BENCHOPCODES) ? 0x00 : ((addr >> 4) + 1) << 4;
            mem[addr++] = 0x03;
            while (addr & 0x0F)
                ++addr;
        }
    } while (opcode < BENCHOPCODES);
}
