
#ifndef _M6502ASM_H_INCLUDED_
#define _M6502ASM_H_INCLUDED_

#include <string>
#include "type.h"

namespace M6502 {
namespace Asm {

typedef enum {
    IMM, ZPG, ZPX, ZPY, ABS, ABX, ABY, IND, INX, INY, REL, IMP, ACC
} addressing;

static const u8 carry       = 1 << 0;
static const u8 zero        = 1 << 1;
static const u8 interrupt   = 1 << 2;
static const u8 decimal     = 1 << 3;
static const u8 overflow    = 1 << 6;
static const u8 negative    = 1 << 7;

struct metadata {
    unsigned int bytes;
    unsigned int cycles;
    addressing type;
    const std::string name;
    u8 rflags;
    u8 wflags;
    bool unofficial;
    bool jam;
};

extern const struct metadata instructions[256];

};
};

#define ADC_IMM     0x69
#define ADC_ZPG     0x65
#define ADC_ZPX     0x75
#define ADC_ABS     0x6d
#define ADC_ABX     0x7d
#define ADC_ABY     0x79
#define ADC_INX     0x61
#define ADC_INY     0x71
#define AND_IMM     0x29
#define AND_ZPG     0x25
#define AND_ZPX     0x35
#define AND_ABS     0x2d
#define AND_ABX     0x3d
#define AND_ABY     0x39
#define AND_INX     0x21
#define AND_INY     0x31
#define ASL_ACC     0x0a
#define ASL_ZPG     0x06
#define ASL_ZPX     0x16
#define ASL_ABS     0x0e
#define ASL_ABX     0x1e
#define BCC_REL     0x90
#define BCS_REL     0xb0
#define BEQ_REL     0xf0
#define BIT_ZPG     0x24
#define BIT_ABS     0x2c
#define BMI_REL     0x30
#define BNE_REL     0xd0
#define BPL_REL     0x10
#define BRK_IMP     0x00
#define BVC_REL     0x50
#define BVS_REL     0x70
#define CLC_IMP     0x18
#define CLD_IMP     0xd8
#define CLI_IMP     0x58
#define CLV_IMP     0xb8
#define CMP_IMM     0xc9
#define CMP_ZPG     0xc5
#define CMP_ZPX     0xd5
#define CMP_ABS     0xcd
#define CMP_ABX     0xdd
#define CMP_ABY     0xd9
#define CMP_INX     0xc1
#define CMP_INY     0xd1
#define CPX_IMM     0xe0
#define CPX_ZPG     0xe4
#define CPX_ABS     0xec
#define CPY_IMM     0xc0
#define CPY_ZPG     0xc4
#define CPY_ABS     0xcc
#define DEC_ZPG     0xc6
#define DEC_ZPX     0xd6
#define DEC_ABS     0xce
#define DEC_ABX     0xde
#define DEX_IMP     0xca
#define DEY_IMP     0x88
#define EOR_IMM     0x49
#define EOR_ZPG     0x45
#define EOR_ZPX     0x55
#define EOR_ABS     0x4d
#define EOR_ABX     0x5d
#define EOR_ABY     0x59
#define EOR_INX     0x41
#define EOR_INY     0x51
#define INC_ZPG     0xe6
#define INC_ZPX     0xf6
#define INC_ABS     0xee
#define INC_ABX     0xfe
#define INX_IMP     0xe8
#define INY_IMP     0xc8
#define JMP_ABS     0x4c
#define JMP_IND     0x6c
#define JSR_ABS     0x20
#define LDA_IMM     0xa9
#define LDA_ZPG     0xa5
#define LDA_ZPX     0xb5
#define LDA_ABS     0xad
#define LDA_ABX     0xbd
#define LDA_ABY     0xb9
#define LDA_INX     0xa1
#define LDA_INY     0xb1
#define LDX_IMM     0xa2
#define LDX_ZPG     0xa6
#define LDX_ZPY     0xb6
#define LDX_ABS     0xae
#define LDX_ABY     0xbe
#define LDY_IMM     0xa0
#define LDY_ZPG     0xa4
#define LDY_ZPX     0xb4
#define LDY_ABS     0xac
#define LDY_ABX     0xbc
#define LSR_ACC     0x4a
#define LSR_ZPG     0x46
#define LSR_ZPX     0x56
#define LSR_ABS     0x4e
#define LSR_ABX     0x5e
#define NOP_IMP     0xea
#define ORA_IMM     0x09
#define ORA_ZPG     0x05
#define ORA_ZPX     0x15
#define ORA_ABS     0x0d
#define ORA_ABX     0x1d
#define ORA_ABY     0x19
#define ORA_INX     0x01
#define ORA_INY     0x11
#define PHA_IMP     0x48
#define PHP_IMP     0x08
#define PLA_IMP     0x68
#define PLP_IMP     0x28
#define ROL_ACC     0x2a
#define ROL_ZPG     0x26
#define ROL_ZPX     0x36
#define ROL_ABS     0x2e
#define ROL_ABX     0x3e
#define ROR_ACC     0x6a
#define ROR_ZPG     0x66
#define ROR_ZPX     0x76
#define ROR_ABS     0x6e
#define ROR_ABX     0x7e
#define RTI_IMP     0x40
#define RTS_IMP     0x60
#define SBC_IMM     0xe9
#define SBC_ZPG     0xe5
#define SBC_ZPX     0xf5
#define SBC_ABS     0xed
#define SBC_ABX     0xfd
#define SBC_ABY     0xf9
#define SBC_INX     0xe1
#define SBC_INY     0xf1
#define SEC_IMP     0x38
#define SED_IMP     0xf8
#define SEI_IMP     0x78
#define STA_ZPG     0x85
#define STA_ZPX     0x95
#define STA_ABS     0x8d
#define STA_ABX     0x9d
#define STA_ABY     0x99
#define STA_INX     0x81
#define STA_INY     0x91
#define STX_ZPG     0x86
#define STX_ZPY     0x96
#define STX_ABS     0x8e
#define STY_ZPG     0x84
#define STY_ZPX     0x94
#define STY_ABS     0x8c
#define TAX_IMP     0xaa
#define TAY_IMP     0xa8
#define TSX_IMP     0xba
#define TXA_IMP     0x8a
#define TXS_IMP     0x9a
#define TYA_IMP     0x98
#define LAX_ZPG     0xa7
#define LAX_ZPY     0xb7
#define LAX_ABS     0xaf
#define LAX_ABY     0xbf
#define LAX_INX     0xa3
#define LAX_INY     0xb3
#define SAX_ZPG     0x87
#define SAX_ZPY     0x97
#define SAX_ABS     0x8f
#define SAX_INX     0x83
#define DCP_ZPG     0xc7
#define DCP_ZPX     0xd7
#define DCP_ABS     0xcf
#define DCP_ABX     0xdf
#define DCP_ABY     0xdb
#define DCP_INX     0xc3
#define DCP_INY     0xd3
#define ISB_ZPG     0xe7
#define ISB_ZPX     0xf7
#define ISB_ABS     0xef
#define ISB_ABX     0xff
#define ISB_ABY     0xfb
#define ISB_INX     0xe3
#define ISB_INY     0xf3
#define SLO_ZPG     0x07
#define SLO_ZPX     0x17
#define SLO_ABS     0x0f
#define SLO_ABX     0x1f
#define SLO_ABY     0x1b
#define SLO_INX     0x03
#define SLO_INY     0x13
#define RLA_ZPG     0x27
#define RLA_ZPX     0x37
#define RLA_ABS     0x2f
#define RLA_ABX     0x3f
#define RLA_ABY     0x3b
#define RLA_INX     0x23
#define RLA_INY     0x33
#define SRE_ZPG     0x47
#define SRE_ZPX     0x57
#define SRE_ABS     0x4f
#define SRE_ABX     0x5f
#define SRE_ABY     0x5b
#define SRE_INX     0x43
#define SRE_INY     0x53
#define RRA_ZPG     0x67
#define RRA_ZPX     0x77
#define RRA_ABS     0x6f
#define RRA_ABX     0x7f
#define RRA_ABY     0x7b
#define RRA_INX     0x63
#define RRA_INY     0x73
#define AAC0_IMM    0x0b
#define AAC1_IMM    0x2b
#define ASR_IMM     0x4b
#define ARR_IMM     0x6b
#define ANE_IMM     0x8b
#define ATX_IMM     0xab
#define AXS_IMM     0xcb
#define SHA_INY     0x93
#define SHA_ABY     0x9f
#define SHS_ABY     0x9b
#define SHX_ABY     0x9e
#define SHY_ABX     0x9c
#define LAS_ABY     0xbb

#endif /* _M6502ASM_H_INCLUDED_ */
