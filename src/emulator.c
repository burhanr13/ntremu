#include "emulator.h"

#include <SDL2/SDL.h>

#include "arm4_isa.h"
#include "arm5_isa.h"
#include "nds.h"
#include "thumb1_isa.h"
#include "thumb2_isa.h"

EmulatorState ntremu;

const char usage[] = "ntremu [options] <romfile>\n"
                     "-b <biosfile> -- specify bios file path\n"
                     "-f -- apply color filter\n"
                     "-u -- run at uncapped speed\n"
                     "-d -- run the debugger\n";

int emulator_init(int argc, char** argv) {
    read_args(argc, argv);
    if (!ntremu.romfile) {
        printf(usage);
        return -1;
    }
    if (!ntremu.biosfile) {
        ntremu.biosfile = "bios.bin";
    }

    ntremu.nds = malloc(sizeof *ntremu.nds);
    ntremu.cart = create_cartridge(ntremu.romfile);
    if (!ntremu.cart) {
        free(ntremu.nds);
        printf("Invalid rom file\n");
        return -1;
    }

    ntremu.bios = load_bios(ntremu.biosfile);
    if (!ntremu.bios) {
        free(ntremu.nds);
        destroy_cartridge(ntremu.cart);
        printf("Invalid or missing bios file.\n");
        return -1;
    }

    arm4_generate_lookup();
    thumb1_generate_lookup();
    arm5_generate_lookup();
    thumb2_generate_lookup();
    init_nds(ntremu.nds);

    ntremu.romfilenodir = strrchr(ntremu.romfile, '/');
    if (ntremu.romfilenodir) ntremu.romfilenodir++;
    else ntremu.romfilenodir = ntremu.romfile;
    return 0;
}

void emulator_quit() {
    destroy_cartridge(ntremu.cart);
    free(ntremu.bios);
    free(ntremu.nds);
}

void read_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (char* f = &argv[i][1]; *f; f++) {
                switch (*f) {
                    case 'u':
                        ntremu.uncap = true;
                        break;
                    case 'b':
                        ntremu.bootbios = true;
                        if (!*(f + 1) && i + 1 < argc) {
                            ntremu.biosfile = argv[i + 1];
                        }
                        break;
                    case 'f':
                        ntremu.filter = true;
                        break;
                    case 'd':
                        ntremu.debugger = true;
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
        case SDLK_ESCAPE:
            ntremu.running = false;
            break;
        case SDLK_p:
            ntremu.pause = !ntremu.pause;
            break;
        case SDLK_m:
            ntremu.mute = !ntremu.mute;
            break;
        case SDLK_f:
            ntremu.filter = !ntremu.filter;
            break;
        case SDLK_r:
            init_nds(ntremu.nds, ntremu.cart, ntremu.bios, ntremu.bootbios);
            ntremu.pause = false;
            break;
        case SDLK_TAB:
            ntremu.uncap = !ntremu.uncap;
            break;
        default:
            break;
    }
}

void update_input_keyboard(NDS* nds) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    nds->io.keyinput.a = ~keys[SDL_SCANCODE_Z];
    nds->io.keyinput.b = ~keys[SDL_SCANCODE_X];
    nds->io.keyinput.start = ~keys[SDL_SCANCODE_RETURN];
    nds->io.keyinput.select = ~keys[SDL_SCANCODE_RSHIFT];
    nds->io.keyinput.left = ~keys[SDL_SCANCODE_LEFT];
    nds->io.keyinput.right = ~keys[SDL_SCANCODE_RIGHT];
    nds->io.keyinput.up = ~keys[SDL_SCANCODE_UP];
    nds->io.keyinput.down = ~keys[SDL_SCANCODE_DOWN];
    nds->io.keyinput.l = ~keys[SDL_SCANCODE_A];
    nds->io.keyinput.r = ~keys[SDL_SCANCODE_S];
}

void update_input_controller(NDS* nds, SDL_GameController* controller) {
    nds->io.keyinput.a &= ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
    nds->io.keyinput.b &= ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
    nds->io.keyinput.start &= ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
    nds->io.keyinput.select &= ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
    nds->io.keyinput.left &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    nds->io.keyinput.right &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    nds->io.keyinput.up &= ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    nds->io.keyinput.down &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    nds->io.keyinput.l &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    nds->io.keyinput.r &=
        ~SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
}