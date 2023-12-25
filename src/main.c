#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "emulator.h"
#include "nds.h"
#include "types.h"

char wintitle[200];

static inline void center_screen_in_window(int windowW, int windowH, SDL_Rect* dst) {
    if (windowW > windowH) {
        dst->h = windowH;
        dst->y = 0;
        dst->w = dst->h * NDS_SCREEN_W / NDS_SCREEN_H;
        dst->x = (windowW - dst->w) / 2;
    } else {
        dst->w = windowW;
        dst->x = 0;
        dst->h = dst->w * NDS_SCREEN_H / NDS_SCREEN_W;
        dst->y = (windowH - dst->h) / 2;
    }
}

int main(int argc, char** argv) {

    if (emulator_init(argc, argv) < 0) return -1;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);

    SDL_GameController* controller = NULL;
    if (SDL_NumJoysticks() > 0) {
        controller = SDL_GameControllerOpen(0);
    }

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_CreateWindowAndRenderer(NDS_SCREEN_W * 4, NDS_SCREEN_H * 4, SDL_WINDOW_RESIZABLE, &window,
                                &renderer);
    snprintf(wintitle, 199, "ntremu | %s | %.2lf FPS", ntremu.romfilenodir, 0.0);
    SDL_SetWindowTitle(window, wintitle);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                          NDS_SCREEN_W, NDS_SCREEN_H);

    Uint64 prev_time = SDL_GetPerformanceCounter();
    Uint64 prev_fps_update = prev_time;
    Uint64 prev_fps_frame = 0;
    const Uint64 frame_ticks = SDL_GetPerformanceFrequency() / 60;
    Uint64 frame = 0;

    ntremu.running = !ntremu.debugger;
    while (true) {
        while (ntremu.running) {
            Uint64 cur_time;
            Uint64 elapsed;
            bool play_audio = !(ntremu.pause || ntremu.mute || ntremu.uncap || ntremu.nds->stop) &&
                              (ntremu.nds->io.nr52 & (1 << 7));

            if (!(ntremu.pause || ntremu.nds->stop)) {
                do {
                    while (!ntremu.nds->stop && !ntremu.nds->ppu.frame_complete) {
                        nds_step(ntremu.nds);
                        if (ntremu.nds->apu.samples_full) {
                            if (play_audio) {
                                SDL_QueueAudio(audio, ntremu.nds->apu.sample_buf,
                                               sizeof ntremu.nds->apu.sample_buf);
                            }
                            ntremu.nds->apu.samples_full = false;
                        }
                    }
                    ntremu.nds->ppu.frame_complete = false;
                    frame++;

                    cur_time = SDL_GetPerformanceCounter();
                    elapsed = cur_time - prev_time;
                } while (ntremu.uncap && elapsed < frame_ticks);
            }

            void* pixels;
            int pitch;
            SDL_LockTexture(texture, NULL, &pixels, &pitch);
            nds_convert_screen((u16*) ntremu.nds->ppu.screen, pixels);
            SDL_UnlockTexture(texture);

            int windowW, windowH;
            SDL_GetWindowSize(window, &windowW, &windowH);
            SDL_Rect dst;
            center_screen_in_window(windowW, windowH, &dst);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, &dst);
            SDL_RenderPresent(renderer);

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) ntremu.running = false;
                if (e.type == SDL_KEYDOWN) hotkey_press(e.key.keysym.sym);
            }
            update_input_keyboard(ntremu.nds);
            if (controller) update_input_controller(ntremu.nds, controller);
            update_keypad_irq(ntremu.nds);

            cur_time = SDL_GetPerformanceCounter();
            elapsed = cur_time - prev_time;
            Sint64 wait = frame_ticks - elapsed;

            if (play_audio) {
                while (SDL_GetQueuedAudioSize(audio) >= 16 * SAMPLE_BUF_LEN) SDL_Delay(1);
            } else if (wait > 0 && !ntremu.uncap) {
                SDL_Delay(wait * 1000 / SDL_GetPerformanceFrequency());
            }
            cur_time = SDL_GetPerformanceCounter();
            elapsed = cur_time - prev_fps_update;
            if (elapsed >= SDL_GetPerformanceFrequency() / 2) {
                double fps =
                    (double) SDL_GetPerformanceFrequency() * (frame - prev_fps_frame) / elapsed;
                snprintf(wintitle, 199, "ntremu | %s | %.2lf FPS", ntremu.romfilenodir, fps);
                SDL_SetWindowTitle(window, wintitle);
                prev_fps_update = cur_time;
                prev_fps_frame = frame;
            }
            prev_time = cur_time;
        }

        if (ntremu.debugger) {
            debugger_run();
        } else {
            break;
        }
    }

    emulator_quit();

    if (controller) SDL_GameControllerClose(controller);

    SDL_CloseAudioDevice(audio);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
