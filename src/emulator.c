#include "emulator.h"

#include <SDL2/SDL.h>
#include <math.h>

#include "arm4_isa.h"
#include "arm5_isa.h"
#include "nds.h"
#include "thumb1_isa.h"
#include "thumb2_isa.h"

#define TRANSLATE_SPEED 5.0
#define ROTATE_SPEED 0.02

EmulatorState ntremu;

bool wireframe;
bool freecam;
mat4 freecam_mtx;

const char usage[] = "ntremu [options] <romfile>\n"
                     "-u -- run at uncapped speed\n"
                     "-d -- run the debugger\n";

int emulator_init(int argc, char** argv) {
    read_args(argc, argv);
    if (!ntremu.romfile) {
        printf(usage);
        return -1;
    }

    ntremu.bios7 = malloc(BIOS7SIZE);
    FILE* f = fopen("bios7.bin", "rb");
    if (!f) {
        printf("No BIOS found. Make sure both 'bios7.bin' and 'bios9.bin' "
               "exist.\n");
        return -1;
    }
    fread(ntremu.bios7, 1, BIOS7SIZE, f);
    fclose(f);
    ntremu.bios9 = malloc(BIOS9SIZE);
    f = fopen("bios9.bin", "rb");
    if (!f) {
        printf("No BIOS found. Make sure both 'bios7.bin' and 'bios9.bin' "
               "exist.\n");
        return -1;
    }
    fread(ntremu.bios9, 1, BIOS9SIZE, f);
    fclose(f);
    ntremu.firmware = malloc(FIRMSIZE);
    f = fopen("firmware.bin", "rb");
    if (f) {
        fread(ntremu.firmware, 1, FIRMSIZE, f);
        fclose(f);
    } else {
        printf("Firmware not found.\n");
    }

    ntremu.nds = malloc(sizeof *ntremu.nds);
    ntremu.card = create_card(ntremu.romfile);
    if (!ntremu.card) {
        free(ntremu.nds);
        printf("Invalid rom file\n");
        return -1;
    }

    arm4_generate_lookup();
    thumb1_generate_lookup();
    arm5_generate_lookup();
    thumb2_generate_lookup();
    generate_adpcm_table();

    emulator_reset();

    ntremu.romfilenodir = strrchr(ntremu.romfile, '/');
    if (ntremu.romfilenodir) ntremu.romfilenodir++;
    else ntremu.romfilenodir = ntremu.romfile;
    return 0;
}

void emulator_quit() {
    destroy_card(ntremu.card);
    free(ntremu.nds);
    free(ntremu.bios7);
    free(ntremu.bios9);
    FILE* f = fopen("firmware.bin", "wb");
    if (f) {
        fwrite(ntremu.firmware, 1, FIRMSIZE, f);
        fclose(f);
    }
    free(ntremu.firmware);
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
                    case 'u':
                        ntremu.uncap = true;
                        break;
                    case 'd':
                        ntremu.debugger = true;
                        break;
                    case 'b':
                        ntremu.bootbios = true;
                        break;
                    default:
                        printf("Invalid flag\n");
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
            wireframe = !wireframe;
            break;
        case SDLK_BACKSPACE:
            if (ntremu.nds->io7.extkeyin.hinge) {
                ntremu.nds->io7.extkeyin.hinge = 0;
                ntremu.nds->io7.ifl.unfold = 1;
                ntremu.nds->io9.ifl.unfold = 1;
            } else {
                ntremu.nds->io7.extkeyin.hinge = 1;
            }
            break;
        case SDLK_c:
            if (freecam) {
                freecam = false;
            } else {
                freecam = true;
                freecam_mtx = (mat4){0};
                freecam_mtx.p[0][0] = 1;
                freecam_mtx.p[1][1] = 1;
                freecam_mtx.p[2][2] = 1;
                freecam_mtx.p[3][3] = 1;
            }
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

void update_input_touch(NDS* nds, SDL_Rect* ts_bounds) {
    int x, y;
    bool pressed = SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT);
    x = (x - ts_bounds->x) * NDS_SCREEN_W / ts_bounds->w;
    y = (y - ts_bounds->y) * NDS_SCREEN_H / ts_bounds->h;
    if (x < 0 || x >= NDS_SCREEN_W || y < 0 || y >= NDS_SCREEN_H)
        pressed = false;
    if (!pressed) {
        x = 0;
        y = 0xff;
    }

    nds->io7.extkeyin.pen = !pressed;
    nds->tsc.x = x;
    nds->tsc.y = y;
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
        m.p[2][3] = speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_S]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[2][3] = -speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_Q]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[1][3] = speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_E]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[1][3] = -speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_A]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[0][3] = speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_D]) {
        mat4 m = {0};
        m.p[0][0] = 1;
        m.p[1][1] = 1;
        m.p[2][2] = 1;
        m.p[3][3] = 1;
        m.p[0][3] = -speed;
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
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
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
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
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_UP]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[0][0] = 1;
        m.p[1][1] = cosf(-ROTATE_SPEED);
        m.p[1][2] = -sinf(-ROTATE_SPEED);
        m.p[2][1] = sinf(-ROTATE_SPEED);
        m.p[2][2] = cosf(-ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        mat4 m = {0};
        m.p[3][3] = 1;
        m.p[0][0] = 1;
        m.p[1][1] = cosf(ROTATE_SPEED);
        m.p[1][2] = -sinf(ROTATE_SPEED);
        m.p[2][1] = sinf(ROTATE_SPEED);
        m.p[2][2] = cosf(ROTATE_SPEED);
        mat4 tmp;
        matmul2(&m, &freecam_mtx, &tmp);
        freecam_mtx = tmp;
    }
}