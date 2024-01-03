#include "debugger.h"

#include <stdio.h>
#include <string.h>

#include "arm4_isa.h"
#include "arm5_isa.h"
#include "bus7.h"
#include "bus9.h"
#include "emulator.h"
#include "nds.h"
#include "scheduler.h"
#include "thumb1_isa.h"
#include "thumb2_isa.h"

const char* help = "Debugger commands:\n"
                   "c -- continue emulation\n"
                   "a -- advance single frame\n"
                   "n -- next instruction\n"
                   "f -- fast forward to next event and switch CPU\n"
                   "b [addr] -- set or check breakpoint\n"
                   "i -- cpu state info\n"
                   "e -- scheduler events info\n"
                   "r<b/h/w> <addr> -- read from memory\n"
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
    static char prev_line[100];
    static char buf[100];

    printf("ntremu Debugger\n");
    if (ntremu.nds->cur_cpu) {
        print_cpu7_state(&ntremu.nds->cpu7);
        print_cur_instr7(&ntremu.nds->cpu7);
    } else {
        print_cpu9_state(&ntremu.nds->cpu9);
        print_cur_instr9(&ntremu.nds->cpu9);
    }

    while (true) {
        memcpy(prev_line, buf, sizeof buf);
        printf("> ");
        fgets(buf, 100, stdin);
        if (buf[0] == '\n') {
            memcpy(buf, prev_line, sizeof buf);
        }

        char* com = strtok(buf, " \t\n");
        if (!(com && *com)) {
            continue;
        }

        switch (com[0]) {
            case 'q':
                ntremu.debugger = false;
                return;
            case 'c':
                nds_step(ntremu.nds);
                ntremu.running = true;
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
            case 'r':
                switch (com[1]) {
                    case 'b': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            if (ntremu.nds->cur_cpu) {
                                printf("[%08x] = 0x%02x\n", addr, bus7_read8(ntremu.nds, addr));
                            } else {
                                printf("[%08x] = 0x%02x\n", addr, bus9_read8(ntremu.nds, addr));
                            }
                        }
                        break;
                    }
                    case 'h': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            if (ntremu.nds->cur_cpu) {
                                printf("[%08x] = 0x%04x\n", addr, bus7_read16(ntremu.nds, addr));
                            } else {
                                printf("[%08x] = 0x%04x\n", addr, bus9_read16(ntremu.nds, addr));
                            }
                        }
                        break;
                    }
                    case 'w': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            if (ntremu.nds->cur_cpu) {
                                printf("[%08x] = 0x%08x\n", addr, bus7_read32(ntremu.nds, addr));
                            } else {
                                printf("[%08x] = 0x%08x\n", addr, bus9_read32(ntremu.nds, addr));
                            }
                        }
                        break;
                    }
                    default:
                        printf("Reset emulation? ");
                        char ans[5];
                        fgets(ans, 5, stdin);
                        if (ans[0] == 'y') {
                            init_nds(ntremu.nds, ntremu.card, ntremu.bios7, ntremu.bios9,
                                     ntremu.firmware);
                            return;
                        }
                        break;
                }
                break;
            case 'e':
                print_scheduled_events(&ntremu.nds->sched);
                break;
            case 'b':
                if (read_num(strtok(NULL, " \t\n"), &ntremu.breakpoint) < 0)
                    printf("Current breakpoint: 0x%08x\n", ntremu.breakpoint);
                else printf("Breakpoint set: 0x%08x\n", ntremu.breakpoint);
                break;
            case 'a':
                ntremu.running = true;
                ntremu.frame_adv = true;
                return;
            case 'l':
                u32 lines;
                if (read_num(strtok(NULL, " \t\n"), &lines) < 0) lines = 5;
                if (ntremu.nds->cur_cpu) {
                    for (int i = 0; i < 2 * lines; i++) {
                        if (i == lines) printf("-> ");
                        else printf("   ");
                        if (ntremu.nds->cpu7.cpsr.t) {
                            u32 addr = ntremu.nds->cpu7.cur_instr_addr + ((i - lines) << 1);
                            printf("%03x: ", addr & 0xfff);
                            thumb1_disassemble((Thumb1Instr){bus7_read16(ntremu.nds, addr)}, addr,
                                               stdout);
                            printf("\n");
                        } else {
                            u32 addr = ntremu.nds->cpu7.cur_instr_addr + ((i - lines) << 2);
                            printf("%03x: ", addr & 0xfff);
                            arm4_disassemble((Arm4Instr){bus7_read32(ntremu.nds, addr)}, addr,
                                             stdout);
                            printf("\n");
                        }
                    }
                } else {
                    for (int i = 0; i < 2 * lines; i++) {
                        if (i == lines) printf("-> ");
                        else printf("   ");
                        if (ntremu.nds->cpu9.cpsr.t) {
                            u32 addr = ntremu.nds->cpu9.cur_instr_addr + ((i - lines) << 1);
                            printf("%03x: ", addr & 0xfff);
                            thumb2_disassemble((Thumb2Instr){bus9_read16(ntremu.nds, addr)}, addr,
                                               stdout);
                            printf("\n");
                        } else {
                            u32 addr = ntremu.nds->cpu9.cur_instr_addr + ((i - lines) << 2);
                            printf("%03x: ", addr & 0xfff);
                            arm5_disassemble((Arm5Instr){bus9_read32(ntremu.nds, addr)}, addr,
                                             stdout);
                            printf("\n");
                        }
                    }
                }
                break;
            case 't':
                printf("ITCM: base=%08x, size=%08x\n", 0, ntremu.nds->cpu9.itcm_virtsize);
                printf("DTCM: base=%08x, size=%08x\n", ntremu.nds->cpu9.dtcm_base,
                       ntremu.nds->cpu9.dtcm_virtsize);
                break;
            default:
                printf("Invalid command\n");
        }
    }
}