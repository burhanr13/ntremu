#include "arm_common.h"

char* mode_name(CpuMode m) {
    switch (m) {
        case M_USER:
            return "USER";
        case M_FIQ:
            return "FIQ";
        case M_IRQ:
            return "IRQ";
        case M_SVC:
            return "SVC";
        case M_ABT:
            return "ABT";
        case M_UND:
            return "UND";
        case M_SYSTEM:
            return "SYSTEM";
        default:
            return "ILLEGAL";
    }
}

RegBank get_bank(CpuMode mode) {
    switch (mode) {
        case M_USER:
            return B_USER;
        case M_FIQ:
            return B_FIQ;
        case M_IRQ:
            return B_IRQ;
        case M_SVC:
            return B_SVC;
        case M_ABT:
            return B_ABT;
        case M_UND:
            return B_UND;
        case M_SYSTEM:
            return B_USER;
    }
    return B_USER;
}