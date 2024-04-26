#include "debugger.h"

#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "arm.h"
#include "arm_core.h"
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
    char* end;
    long tmp = strtol(str, &end, 0);
    if (end == str) return -1;
    *res = tmp;
    return 0;
}

bool interrupted;
void ctrlchandler() {
    interrupted = true;
}

void debugger_run() {

    printf("ntremu Debugger\n");
    cpu_print_state(ntremu.nds->cur_cpu);
    cpu_print_cur_instr(ntremu.nds->cur_cpu);

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
        if (!com) {
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
                    if (ntremu.nds->cur_cpu_type == CPU7) {
                        printf("CPU7\n");
                    } else {
                        printf("CPU9\n");
                    }
                }
                cpu_print_cur_instr(ntremu.nds->cur_cpu);
                break;
            case 's': {
                u32 next_instr_addr = ntremu.nds->cur_cpu->next_instr_addr;

                struct sigaction old;
                struct sigaction sa = {};
                sa.sa_handler = ctrlchandler;
                sa.sa_flags = SA_RESTART;
                sigaction(SIGINT, &sa, &old);
                interrupted = false;
                while (ntremu.nds->cur_cpu->cur_instr_addr != next_instr_addr &&
                       !interrupted)
                    nds_step(ntremu.nds);
                sigaction(SIGINT, &old, NULL);
                cpu_print_cur_instr(ntremu.nds->cur_cpu);
                break;
            }
            case 'f':
                while (!nds_step(ntremu.nds)) {}
                cpu_print_state(ntremu.nds->cur_cpu);
                cpu_print_cur_instr(ntremu.nds->cur_cpu);
                break;
            case 'i':
                switch (com[1]) {
                    case 'p':
                        printf("fifo 7to9: {");
                        FIFO_foreach(i, ntremu.nds->ipcfifo7to9) {
                            printf("%x ", ntremu.nds->ipcfifo7to9.d[i]);
                        }
                        printf("}\n");
                        printf("fifo 9to7: {");
                        FIFO_foreach(i, ntremu.nds->ipcfifo9to7) {
                            printf("%x ", ntremu.nds->ipcfifo9to7.d[i]);
                        }
                        printf("}\n");
                        break;
                    default:
                        cpu_print_state(ntremu.nds->cur_cpu);
                        break;
                }
                break;
            case 'r': {
                if (com[1] == 'e' || com[1] == '\0') {
                    printf("Reset emulation? ");
                    char ans = getchar();
                    while (getchar() != '\n') {}
                    if (ans == 'y') {
                        emulator_reset();
                        free(buf);
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
                        printf("[%08x] = 0x%02x\n", addr,
                               ntremu.nds->cur_cpu->read8(ntremu.nds->cur_cpu,
                                                          addr, false));
                        break;
                    case 'h':
                    case '1':
                        printf("[%08x] = 0x%04x\n", addr,
                               ntremu.nds->cur_cpu->read16(ntremu.nds->cur_cpu,
                                                           addr, false));
                        break;
                    case 'w':
                    case '3':
                        printf("[%08x] = 0x%08x\n", addr,
                               ntremu.nds->cur_cpu->read32(ntremu.nds->cur_cpu,
                                                           addr));
                        break;
                    case 'm': {
                        u32 n;
                        if (read_num(strtok(NULL, " "), &n) < 0) n = 8;
                        printf("[%08x] = ", addr);
                        for (int i = 0; i < n; i++) {
                            if (i > 0 && !(i & 7)) printf("             ");
                            printf("0x%08x ",
                                   ntremu.nds->cur_cpu->read32m(
                                       ntremu.nds->cur_cpu, addr, i));
                            if ((i & 7) == 7) printf("\n");
                        }
                        if (n & 7) printf("\n");
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
                        ntremu.nds->cur_cpu->write8(ntremu.nds->cur_cpu, addr,
                                                    data);
                        printf("[%08x] = 0x%02x\n", addr,
                               ntremu.nds->cur_cpu->read8(ntremu.nds->cur_cpu,
                                                          addr, false));
                        break;
                    case 'h':
                    case '1':
                        ntremu.nds->cur_cpu->write16(ntremu.nds->cur_cpu, addr,
                                                     data);
                        printf("[%08x] = 0x%4x\n", addr,
                               ntremu.nds->cur_cpu->read16(ntremu.nds->cur_cpu,
                                                           addr, false));
                        break;
                    case 'w':
                    case '3':
                        ntremu.nds->cur_cpu->write32(ntremu.nds->cur_cpu, addr,
                                                     data);
                        printf("[%08x] = 0x%08x\n", addr,
                               ntremu.nds->cur_cpu->read32(ntremu.nds->cur_cpu,
                                                           addr));
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
                for (int i = 0; i < 2 * lines; i++) {
                    if (i == lines) printf("-> ");
                    else printf("   ");
                    if (ntremu.nds->cur_cpu->cpsr.t) {
                        u32 addr = ntremu.nds->cur_cpu->cur_instr_addr +
                                   ((i - lines) << 1);
                        printf("%03x: ", addr & 0xfff);
                        thumb_disassemble(
                            (ThumbInstr){ntremu.nds->cur_cpu->fetch16(
                                ntremu.nds->cur_cpu, addr)},
                            addr, stdout);
                        printf("\n");
                    } else {
                        u32 addr = ntremu.nds->cur_cpu->cur_instr_addr +
                                   ((i - lines) << 2);
                        printf("%03x: ", addr & 0xfff);
                        arm_disassemble((ArmInstr){ntremu.nds->cur_cpu->fetch32(
                                            ntremu.nds->cur_cpu, addr)},
                                        addr, stdout);
                        printf("\n");
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