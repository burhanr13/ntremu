#include "emulator.h"

#include <SDL2/SDL.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arm4_isa.h"
#include "arm5_isa.h"
#include "emulator_state.h"
#include "nds.h"
#include "thumb.h"

#define TRANSLATE_SPEED 5.0
#define ROTATE_SPEED 0.02

EmulatorState ntremu;

const char usage[] = "ntremu [options] <romfile>\n"
                     "-b -- boot from firmware\n"
                     "-d -- run the debugger\n"
                     "-p <path> -- path to bios/firmware files\n";

int emulator_init(int argc, char** argv) {
    read_args(argc, argv);
    if (!ntremu.romfile) {
        eprintf(usage);
        return -1;
    }

    int dirfd = AT_FDCWD;
    if (ntremu.biosPath) {
        dirfd = open(ntremu.biosPath, O_RDONLY | O_DIRECTORY);
        if (dirfd < 0) {
            eprintf("Invalid bios path\n");
            return -1;
        }
    }

    int bios7fd = openat(dirfd, "bios7.bin", O_RDONLY);
    int bios9fd = openat(dirfd, "bios9.bin", O_RDONLY);
    int firmwarefd = openat(dirfd, "firmware.bin", O_RDWR);

    close(dirfd);

    if (bios7fd < 0 || bios9fd < 0 || firmwarefd < 0) {
        eprintf("Missing bios or firmware. Make sure 'bios7.bin','bios9.bin', "
                "and 'firmware.bin' exist.\n");
        return -1;
    }

    ntremu.bios7 =
        mmap(NULL, BIOS7SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, bios7fd, 0);
    ntremu.bios9 =
        mmap(NULL, BIOS9SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, bios9fd, 0);
    ntremu.firmware = mmap(NULL, FIRMWARESIZE, PROT_READ | PROT_WRITE,
                           MAP_SHARED, firmwarefd, 0);

    close(bios7fd);
    close(bios9fd);
    close(firmwarefd);

    ntremu.nds = malloc(sizeof *ntremu.nds);
    ntremu.card = create_card(ntremu.romfile);
    if (!ntremu.card) {
        eprintf("Invalid rom file\n");
        return -1;
    }

    if (ntremu.sd_path) {
        ntremu.dldi_sd_fd = open(ntremu.sd_path, O_RDWR);
        if (ntremu.dldi_sd_fd >= 0) {
            struct stat st;
            fstat(ntremu.dldi_sd_fd, &st);
            if (S_ISBLK(st.st_mode)) {
                ntremu.dldi_sd_size = lseek(fd, 0, SEEK_END);
            } else {
                ntremu.dldi_sd_size = st.st_size;
            }
        }
    } else {
        ntremu.dldi_sd_fd = -1;
    }

    arm4_generate_lookup();
    arm5_generate_lookup();
    thumb_generate_lookup();
    generate_adpcm_table();

    emulator_reset();

    ntremu.romfilenodir = strrchr(ntremu.romfile, '/');
    if (ntremu.romfilenodir) ntremu.romfilenodir++;
    else ntremu.romfilenodir = ntremu.romfile;
    return 0;
}

void emulator_quit() {
    close(ntremu.dldi_sd_fd);
    destroy_card(ntremu.card);
    free(ntremu.nds);
    munmap(ntremu.bios7, BIOS7SIZE);
    munmap(ntremu.bios9, BIOS9SIZE);
    munmap(ntremu.firmware, FIRMWARESIZE);
}

void emulator_reset() {
    init_nds(ntremu.nds, ntremu.card, ntremu.bios7, ntremu.bios9,
             ntremu.firmware, ntremu.bootbios);
}

void read_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (char* f = &argv[i][1]; *f; f++) {
                switch (*f) {
                    case 'd':
                        ntremu.debugger = true;
                        break;
                    case 'b':
                        ntremu.bootbios = true;
                        break;
                    case 'p':
                        if (!f[1] && i + 1 < argc) {
                            ntremu.biosPath = argv[++i];
                        } else {
                            eprintf("Missing argument for '-p'\n");
                        }
                        break;
                    case 's':
                        if (!f[1] && i + 1 < argc) {
                            ntremu.sd_path = argv[++i];
                        } else {
                            eprintf("Missing argument for '-s'\n");
                        }
                        break;
                    default:
                        eprintf("Invalid argument\n");
                }
            }
        } else {
            ntremu.romfile = argv[i];
        }
    }
}

void hotkey_press(SDL_KeyCode key) {
    switch (key) {
        case SDLK_p:
            ntremu.pause = !ntremu.pause;
            break;
        case SDLK_m:
            ntremu.mute = !ntremu.mute;
            break;
        case SDLK_r:
            emulator_reset();
            ntremu.pause = false;
            break;
        case SDLK_TAB:
            ntremu.uncap = !ntremu.uncap;
            break;
        case SDLK_o:
            ntremu.wireframe = !ntremu.wireframe;
            break;
        case SDLK_BACKSPACE:
            if (ntremu.nds->io7.extkeyin.hinge) {
                ntremu.nds->io7.extkeyin.hinge = 0;
                ntremu.nds->io7.ifl.unfold = 1;
            } else {
                ntremu.nds->io7.extkeyin.hinge = 1;
            }
            break;
        case SDLK_c:
            if (ntremu.freecam) {
                ntremu.freecam = false;
            } else {
                ntremu.freecam = true;
                ntremu.freecam_mtx = (mat4){0};
                ntremu.freecam_mtx.p[0][0] = 1;
                ntremu.freecam_mtx.p[1][1] = 1;
                ntremu.freecam_mtx.p[2][2] = 1;
                ntremu.freecam_mtx.p[3][3] = 1;
            }
            break;
        case SDLK_u:
            ntremu.abs_touch = !ntremu.abs_touch;
            break;
        default:
            break;
    }
}

void update_input_keyboard(NDS* nds) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    nds->io7.keyinput.a = ~keys[SDL_SCANCODE_Z];
    nds->io7.keyinput.b = ~keys[SDL_SCANCODE_X];
    nds->io7.keyinput.start = ~keys[SDL_SCANCODE_RETURN];
    nds->io7.keyinput.select = ~keys[SDL_SCANCODE_RSHIFT];
    nds->io7.keyinput.left = ~keys[SDL_SCANCODE_LEFT];
    nds->io7.keyinput.right = ~keys[SDL_SCANCODE_RIGHT];
    nds->io7.keyinput.up = ~keys[SDL_SCANCODE_UP];
    nds->io7.keyinput.down = ~keys[SDL_SCANCODE_DOWN];
    nds->io7.keyinput.l = ~keys[SDL_SCANCODE_Q];
    nds->io7.keyinput.r = ~keys[SDL_SCANCODE_W];
    nds->io9.keyinput = nds->io7.keyinput;

    nds->io7.extkeyin.x = ~keys[SDL_SCANCODE_A];
    nds->io7.extkeyin.y = ~keys[SDL_SCANCODE_S];
}

void update_input_controller(NDS* nds, SDL_GameController* controller) {
    nds->io7.keyinput.a &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B);
    nds->io7.keyinput.b &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
    nds->io7.keyinput.start &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
    nds->io7.keyinput.select &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
    nds->io7.keyinput.left &= ~SDL_GameControllerGetButton(
        controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    nds->io7.keyinput.right &= ~SDL_GameControllerGetButton(
        controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    nds->io7.keyinput.up &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    nds->io7.keyinput.down &= ~SDL_GameControllerGetButton(
        controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    nds->io7.keyinput.l &= ~SDL_GameControllerGetButton(
        controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    nds->io7.keyinput.r &= ~SDL_GameControllerGetButton(
        controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    nds->io9.keyinput = nds->io7.keyinput;

    nds->io7.extkeyin.x &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y);
    nds->io7.extkeyin.y &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
}

void update_input_touch(NDS* nds, SDL_Rect* ts_bounds,
                        SDL_GameController* controller) {
    int x, y;
    bool pressed = SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT);
    x = (x - ts_bounds->x) * NDS_SCREEN_W / ts_bounds->w;
    y = (y - ts_bounds->y) * NDS_SCREEN_H / ts_bounds->h;
    if (x < 0 || x >= NDS_SCREEN_W || y < 0 || y >= NDS_SCREEN_H)
        pressed = false;
    if (pressed) {
        nds->tsc.x = x;
        nds->tsc.y = y;
    }

    if (controller) {
        int x =
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        int y =
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        x -= 3000;
        y -= 400;
        if (x >= (1 << 15)) x = (1 << 15) - 1;
        if (x < INT16_MIN) x = INT16_MIN;
        if (y >= (1 << 15)) y = (1 << 15) - 1;
        if (y < INT16_MIN) y = INT16_MIN;
        if (abs(x) >= 3000 || abs(y) >= 3000) {
            pressed = true;

            if (ntremu.abs_touch) {
                nds->tsc.x =
                    NDS_SCREEN_W / 2 + (x * (NDS_SCREEN_W / 2 - 10) >> 15);
                nds->tsc.y =
                    NDS_SCREEN_H / 2 + (y * (NDS_SCREEN_H / 2 - 10) >> 15);
            } else {
                static int prev_x, prev_y, target_x, target_y;
                if (nds->tsc.x == (u8) -1) {
                    nds->tsc.x = NDS_SCREEN_W / 2;
                    nds->tsc.y = NDS_SCREEN_H / 2;
                    prev_x = x, prev_y = y;
                    target_x = NDS_SCREEN_W / 2;
                    target_y = NDS_SCREEN_H / 2;
                } else if (abs(x - prev_x) > 10 || abs(y - prev_y) > 10) {
                    prev_x = x, prev_y = y;
                    int tx = target_x + (x >> 12);
                    int ty = target_y + (y >> 12);
                    if (tx >= 0 && tx < NDS_SCREEN_W && ty >= 0 &&
                        ty < NDS_SCREEN_H) {
                        target_x = tx;
                        target_y = ty;
                    }
                    nds->tsc.x = (nds->tsc.x + target_x) / 2;
                    nds->tsc.y = (nds->tsc.y + target_y) / 2;
                }
            }
        }
    }

    nds->io7.extkeyin.pen = !pressed;
    if (!pressed) {
        ntremu.nds->tsc.x = -1;
        ntremu.nds->tsc.y = -1;
    }
}

void matmul2(mat4* a, mat4* b, mat4* dst) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a->p[i][k] * b->p[k][j];
            }
            dst->p[i][j] = sum;
        }
    }
}

void update_input_freecam() {
    const Uint8* keys = SDL_GetKeyboardState(NULL);

    float speed = TRANSLATE_SPEED;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) speed /= 20;

    if (keys[SDL_SCANCODE_W]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[1][3] = -speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_S]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[1][3] = speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_Q]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[0][0] = 1;
        m.p[1][1] = cosf(ROTATE_SPEED);
        m.p[1][2] = -sinf(ROTATE_SPEED);
        m.p[2][1] = sinf(ROTATE_SPEED);
        m.p[2][2] = cosf(ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_E]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[0][0] = 1;
        m.p[1][1] = cosf(-ROTATE_SPEED);
        m.p[1][2] = -sinf(-ROTATE_SPEED);
        m.p[2][1] = sinf(-ROTATE_SPEED);
        m.p[2][2] = cosf(-ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_A]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[0][3] = speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_D]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[0][3] = -speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_LEFT]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = cosf(-ROTATE_SPEED);
        m.p[2][0] = -sinf(-ROTATE_SPEED);
        m.p[0][2] = sinf(-ROTATE_SPEED);
        m.p[0][0] = cosf(-ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = cosf(ROTATE_SPEED);
        m.p[2][0] = -sinf(ROTATE_SPEED);
        m.p[0][2] = sinf(ROTATE_SPEED);
        m.p[0][0] = cosf(ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_UP]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[2][3] = speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[2][3] = -speed;
        mat4 tmp;
        matmul2(&m, &ntremu.freecam_mtx, &tmp);
        ntremu.freecam_mtx = tmp;
    }

    ntremu.nds->io7.keyinput.keys = 0x3ff;
    ntremu.nds->io9.keyinput.keys = 0x3ff;
    ntremu.nds->io7.extkeyin.x = 1;
    ntremu.nds->io7.extkeyin.y = 1;
}