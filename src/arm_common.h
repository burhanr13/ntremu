#ifndef ARM_COMMON_H
#define ARM_COMMON_H

#define HISTORY_SIZE (1<<20)

typedef enum { B_USER, B_FIQ, B_SVC, B_ABT, B_IRQ, B_UND, B_CT } RegBank;
typedef enum {
    M_USER = 0b10000,
    M_FIQ = 0b10001,
    M_IRQ = 0b10010,
    M_SVC = 0b10011,
    M_ABT = 0b10111,
    M_UND = 0b11011,
    M_SYSTEM = 0b11111
} CpuMode;

typedef enum { I_RESET, I_UND, I_SWI, I_PABT, I_DABT, I_ADDR, I_IRQ, I_FIQ } CpuInterrupt;

enum { C_EQ, C_NE, C_CS, C_CC, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };

enum {
    A_AND,
    A_EOR,
    A_SUB,
    A_RSB,
    A_ADD,
    A_ADC,
    A_SBC,
    A_RSC,
    A_TST,
    A_TEQ,
    A_CMP,
    A_CMN,
    A_ORR,
    A_MOV,
    A_BIC,
    A_MVN
};

enum { S_LSL, S_LSR, S_ASR, S_ROR };

enum {
    T_AND,
    T_EOR,
    T_LSL,
    T_LSR,
    T_ASR,
    T_ADC,
    T_SBC,
    T_ROR,
    T_TST,
    T_NEG,
    T_CMP,
    T_CMN,
    T_ORR,
    T_MUL,
    T_BIC,
    T_MVN
};

char* mode_name(CpuMode m);
RegBank get_bank(CpuMode mode);

#endif