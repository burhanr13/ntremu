#include "debugger.h"

#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <string.h>

#include "arm4_isa.h"
#include "arm5_isa.h"
#include "bus7.h"
#include "bus9.h"
#include "emulator.h"
#include "nds.h"
#include "scheduler.h"
#include "thumb.h"

const char* help = "Debugger commands:\n"
                   "c -- continue emulation\n"
                   "a -- advance single frame\n"
                   "n -- next instruction\n"
                   "f -- fast forward to next event and switch CPU\n"
                   "b [addr] -- set or check breakpoint\n"
                   "i -- cpu state info\n"
                   "e -- scheduler events info\n"
                   "r<b/h/w> <addr> -- read from memory\n"
                   "w<b/h/w> <addr> <data> -- write to memory\n"
                   "l -- show code\n"
                   "r -- reset\n"
                   "q -- quit debugger\n"
                   "h -- help\n";

int read_num(char* str, u32* res) {
    if (!str) return -1;
    if (sscanf(str, "0x%x", res) < 1) {
        if (sscanf(str, "%d", res) < 1) return -1;
    }
    return 0;
}

void debugger_run() {

    printf("ntremu Debugger\n");
    if (ntremu.nds->cur_cpu) {
        print_cpu7_state(&ntremu.nds->cpu7);
        print_cur_instr7(&ntremu.nds->cpu7);
    } else {
        print_cpu9_state(&ntremu.nds->cpu9);
        print_cur_instr9(&ntremu.nds->cpu9);
    }

    using_history();

    char* buf = NULL;
    while (true) {

        char* tmp = readline("> ");

        if (!tmp) break;
        if (!buf || tmp[0]) {
            free(buf);
            buf = tmp;
            add_history(buf);
        } else {
            free(tmp);
        }

        char* com = strtok(buf, " ");
        if (!(com && *com)) {
            continue;
        }

        switch (com[0]) {
            case 'q':
                ntremu.debugger = false;
                free(buf);
                return;
            case 'c':
                nds_step(ntremu.nds);
                ntremu.running = true;
                free(buf);
                return;
            case 'h':
                printf("%s", help);
                break;
            case 'n':
                if (nds_step(ntremu.nds)) {
                    if (ntremu.nds->cur_cpu) {
                        printf("CPU7\n");
                    } else {
                        printf("CPU9\n");
                    }
                }
                if (ntremu.nds->cur_cpu) {
                    print_cur_instr7(&ntremu.nds->cpu7);
                } else {
                    print_cur_instr9(&ntremu.nds->cpu9);
                }
                break;
            case 's': {
                u32 next_instr_addr;
                u32* watch;
                if (ntremu.nds->cur_cpu) {
                    next_instr_addr = ntremu.nds->cpu7.cur_instr_addr;
                    watch = &ntremu.nds->cpu7.cur_instr_addr;
                    if (ntremu.nds->cpu7.cpsr.t) {
                        next_instr_addr += 2;
                    } else {
                        next_instr_addr += 4;
                    }
                } else {
                    next_instr_addr = ntremu.nds->cpu9.cur_instr_addr;
                    watch = &ntremu.nds->cpu9.cur_instr_addr;
                    if (ntremu.nds->cpu9.cpsr.t) {
                        next_instr_addr += 2;
                    } else {
                        next_instr_addr += 4;
                    }
                }
                while (*watch != next_instr_addr) nds_step(ntremu.nds);
                if (ntremu.nds->cur_cpu) {
                    print_cur_instr7(&ntremu.nds->cpu7);
                } else {
                    print_cur_instr9(&ntremu.nds->cpu9);
                }
                break;
            }
            case 'f':
                while (!nds_step(ntremu.nds)) {
                }
                if (ntremu.nds->cur_cpu) {
                    print_cpu7_state(&ntremu.nds->cpu7);
                    print_cur_instr7(&ntremu.nds->cpu7);
                } else {
                    print_cpu9_state(&ntremu.nds->cpu9);
                    print_cur_instr9(&ntremu.nds->cpu9);
                }
                break;
            case 'i':
                switch (com[1]) {
                    case 'p':
                        printf("fifo 7to9: {");
                        for (int i = 0; i < ntremu.nds->ipcfifo7to9_size; i++) {
                            printf("%x ", ntremu.nds->ipcfifo7to9[i]);
                        }
                        printf("}\n");
                        printf("fifo 9to7: {");
                        for (int i = 0; i < ntremu.nds->ipcfifo9to7_size; i++) {
                            printf("%x ", ntremu.nds->ipcfifo9to7[i]);
                        }
                        printf("}\n");
                        break;
                    default:
                        if (ntremu.nds->cur_cpu) {
                            print_cpu7_state(&ntremu.nds->cpu7);
                        } else {
                            print_cpu9_state(&ntremu.nds->cpu9);
                        }
                        break;
                }
                break;
            case 'r': {
                if (com[1] == 'e' || com[1] == '\0') {
                    printf("Reset emulation? ");
                    char ans[5];
                    fgets(ans, 5, stdin);
                    if (ans[0] == 'y') {
                        emulator_reset();
                        return;
                    }
                    break;
                }
                u32 addr;
                if (read_num(strtok(NULL, " "), &addr) < 0) {
                    printf("Invalid address\n");
                    break;
                }
                switch (com[1]) {
                    case 'b':
                    case '8':
                        if (ntremu.nds->cur_cpu) {
                            printf("[%08x] = 0x%02x\n", addr,
                                   bus7_read8(ntremu.nds, addr));
                        } else {
                            printf("[%08x] = 0x%02x\n", addr,
                                   bus9_read8(ntremu.nds, addr));
                        }
                        break;
                    case 'h':
                    case '1':
                        if (ntremu.nds->cur_cpu) {
                            printf("[%08x] = 0x%04x\n", addr,
                                   bus7_read16(ntremu.nds, addr));
                        } else {
                            printf("[%08x] = 0x%04x\n", addr,
                                   bus9_read16(ntremu.nds, addr));
                        }
                        break;
                    case 'w':
                    case '3':
                        if (ntremu.nds->cur_cpu) {
                            printf("[%08x] = 0x%08x\n", addr,
                                   bus7_read32(ntremu.nds, addr));
                        } else {
                            printf("[%08x] = 0x%08x\n", addr,
                                   bus9_read32(ntremu.nds, addr));
                        }
                        break;
                    case 'm': {
                        u32 n;
                        if (read_num(strtok(NULL, " "), &n) < 0) n = 8;
                        printf("[%08x] = ", addr);
                        if (ntremu.nds->cur_cpu) {
                            for (int i = 0; i < n; i++) {
                                if (i > 0 && !(i & 7)) printf("             ");
                                printf("0x%08x ",
                                       bus7_read32(ntremu.nds, addr + 4 * i));
                                if ((i & 7) == 7) printf("\n");
                            }
                            if (n & 7) printf("\n");
                        } else {
                            for (int i = 0; i < n; i++) {
                                if (i > 0 && !(i & 7)) printf("             ");
                                printf("0x%08x ",
                                       bus9_read32(ntremu.nds, addr + 4 * i));
                                if ((i & 7) == 7) printf("\n");
                            }
                            if (n & 7) printf("\n");
                        }
                        break;
                    }
                    default:
                        printf("Invalid read command.\n");
                        break;
                }
                break;
            }
            case 'w': {
                u32 addr;
                if (read_num(strtok(NULL, " "), &addr) < 0) {
                    printf("Invalid address\n");
                    break;
                }
                u32 data;
                if (read_num(strtok(NULL, " "), &data) < 0) {
                    printf("Invalid data\n");
                    break;
                }
                switch (com[1]) {
                    case 'b':
                    case '8':
                        if (ntremu.nds->cur_cpu) {
                            bus7_write8(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%02x\n", addr,
                                   bus7_read8(ntremu.nds, addr));
                        } else {
                            bus9_write8(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%02x\n", addr,
                                   bus9_read8(ntremu.nds, addr));
                        }
                        break;
                    case 'h':
                    case '1':
                        if (ntremu.nds->cur_cpu) {
                            bus7_write16(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%04x\n", addr,
                                   bus7_read16(ntremu.nds, addr));
                        } else {
                            bus9_write16(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%04x\n", addr,
                                   bus9_read16(ntremu.nds, addr));
                        }
                        break;
                    case 'w':
                    case '3':
                        if (ntremu.nds->cur_cpu) {
                            bus7_write32(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%08x\n", addr,
                                   bus7_read32(ntremu.nds, addr));
                        } else {
                            bus9_write32(ntremu.nds, addr, data);
                            printf("[%08x] = 0x%08x\n", addr,
                                   bus9_read32(ntremu.nds, addr));
                        }
                        break;
                    default:
                        printf("Invalid write command.\n");
                        break;
                }
                break;
            }
            case 'e':
                print_scheduled_events(&ntremu.nds->sched);
                break;
            case 'b':
                if (read_num(strtok(NULL, " "), &ntremu.breakpoint) < 0)
                    printf("Current breakpoint: 0x%08x\n", ntremu.breakpoint);
                else printf("Breakpoint set: 0x%08x\n", ntremu.breakpoint);
                break;
            case 'a':
                ntremu.running = true;
                ntremu.frame_adv = true;
                free(buf);
                return;
            case 'l': {
                u32 lines;
                if (read_num(strtok(NULL, " "), &lines) < 0) lines = 5;
                if (ntremu.nds->cur_cpu) {
                    for (int i = 0; i < 2 * lines; i++) {
                        if (i == lines) printf("-> ");
                        else printf("   ");
                        if (ntremu.nds->cpu7.cpsr.t) {
                            u32 addr = ntremu.nds->cpu7.cur_instr_addr +
                                       ((i - lines) << 1);
                            printf("%03x: ", addr & 0xfff);
                            thumb_disassemble(
                                (ThumbInstr){bus7_read16(ntremu.nds, addr)},
                                addr, stdout);
                            printf("\n");
                        } else {
                            u32 addr = ntremu.nds->cpu7.cur_instr_addr +
                                       ((i - lines) << 2);
                            printf("%03x: ", addr & 0xfff);
                            arm_disassemble(
                                (ArmInstr){bus7_read32(ntremu.nds, addr)}, addr,
                                stdout);
                            printf("\n");
                        }
                    }
                } else {
                    for (int i = 0; i < 2 * lines; i++) {
                        if (i == lines) printf("-> ");
                        else printf("   ");
                        if (ntremu.nds->cpu9.cpsr.t) {
                            u32 addr = ntremu.nds->cpu9.cur_instr_addr +
                                       ((i - lines) << 1);
                            printf("%03x: ", addr & 0xfff);
                            thumb_disassemble((ThumbInstr){cpu9_fetch16(
                                                  &ntremu.nds->cpu9, addr)},
                                              addr, stdout);
                            printf("\n");
                        } else {
                            u32 addr = ntremu.nds->cpu9.cur_instr_addr +
                                       ((i - lines) << 2);
                            printf("%03x: ", addr & 0xfff);
                            arm_disassemble((ArmInstr){cpu9_fetch32(
                                                &ntremu.nds->cpu9, addr)},
                                            addr, stdout);
                            printf("\n");
                        }
                    }
                }
                break;
            }
            case 't':
                printf("ITCM: base=%08x, size=%08x\n", 0,
                       ntremu.nds->cpu9.itcm_virtsize);
                printf("DTCM: base=%08x, size=%08x\n",
                       ntremu.nds->cpu9.dtcm_base,
                       ntremu.nds->cpu9.dtcm_virtsize);
                break;
            default:
                printf("Invalid command\n");
        }
    }

    free(buf);
}