#include "debugger.h"

#include <stdio.h>
#include <string.h>

#include "bus7.h"
#include "bus9.h"
#include "emulator.h"
#include "nds.h"
#include "scheduler.h"

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
                if (ntremu.nds->cur_cpu) {
                    print_cpu7_state(&ntremu.nds->cpu7);
                } else {
                    print_cpu9_state(&ntremu.nds->cpu9);
                }
                break;
            case 'r':
                switch (com[1]) {
                    case 'b': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            printf("[%08x] = 0x%02x\n", addr, bus9_read8(ntremu.nds, addr));
                        }
                        break;
                    }
                    case 'h': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            printf("[%08x] = 0x%04x\n", addr, bus9_read16(ntremu.nds, addr));
                        }
                        break;
                    }
                    case 'w': {
                        u32 addr;
                        if (read_num(strtok(NULL, " \t\n"), &addr) < 0) {
                            printf("Invalid address\n");
                        } else {
                            printf("[%08x] = 0x%08x\n", addr, bus9_read32(ntremu.nds, addr));
                        }
                        break;
                    }
                    default:
                        printf("Reset emulation? ");
                        char ans[5];
                        fgets(ans, 5, stdin);
                        if (ans[0] == 'y') {
                            init_nds(ntremu.nds, ntremu.card, ntremu.bios7, ntremu.bios9);
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
                for (int i = 0; i < 2 * lines; i++) {
                    if (i == lines) printf("-> ");
                    else printf("   ");
                    u32 addr = ntremu.nds->cpu9.cur_instr_addr + ((i - lines) << 2);
                    arm5_disassemble((Arm5Instr){bus9_read32(ntremu.nds, addr)}, addr, stdout);
                    printf("\n");
                }
                break;
            default:
                printf("Invalid command\n");
        }
    }
}