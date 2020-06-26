#include "MainLoop.h"

#include <SDL2/SDL_image.h>

#include "ButtonEvent.h"
#include "EmHAL.h"
#include "EmSession.h"
#include "KeyboardEvent.h"
#include "PenEvent.h"
#include "Silkscreen.h"
#include "common.h"

constexpr int CLOCK_DIV = 4;
constexpr int MOUSE_MOVE_THROTTLE = 25;
constexpr uint8 SILKSCREEN_BACKGROUND_HUE = 0xbb;
constexpr uint32 BACKGROUND_HUE = 0xdd;
constexpr uint32 FOREGROUND_COLOR = 0x000000ff;
constexpr uint32 BACKGROUND_COLOR =
    0xff | (BACKGROUND_HUE << 8) | (BACKGROUND_HUE << 16) | (BACKGROUND_HUE << 24);

MainLoop::MainLoop(SDL_Window* window, SDL_Renderer* renderer) : renderer(renderer) {
    loadSilkscreen();

    SDL_SetRenderDrawColor(renderer, 0xdd, 0xdd, 0xdd, 0xff);
    SDL_RenderClear(renderer);
    drawSilkscreen(renderer);

    SDL_RenderPresent(renderer);

    lcdTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                                   160, 160);
}

bool MainLoop::isRunning() const { return running; }

void MainLoop::cycle() {
    const long hz = EmHAL::GetSystemClockFrequency();
    const long millis = Platform::getMilliseconds();
    if (millis - millisOffset - clockEmu > 500) clockEmu = millis - millisOffset - 10;

    const long cycles = (millis - millisOffset - clockEmu) * (hz / 1000 / CLOCK_DIV);

    if (cycles > 0) {
        long cyclesPassed = 0;

        while (cyclesPassed < cycles) cyclesPassed += gSession->RunEmulation(cycles);
        clockEmu += cyclesPassed / (hz / 1000 / CLOCK_DIV);
    }

    updateScreen();
    handleEvents(millis);
}

void MainLoop::cycleStatic(MainLoop* self) { self->cycle(); }

void MainLoop::loadSilkscreen() {
    SDL_RWops* rwops = SDL_RWFromConstMem((const void*)silkscreenPng_data, silkscreenPng_len);
    SDL_Surface* surface = IMG_LoadPNG_RW(rwops);
    SDL_RWclose(rwops);

    silkscreenTexture = SDL_CreateTextureFromSurface(renderer, surface);

    SDL_FreeSurface(surface);
}

void MainLoop::drawSilkscreen(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, SILKSCREEN_BACKGROUND_HUE, SILKSCREEN_BACKGROUND_HUE,
                           SILKSCREEN_BACKGROUND_HUE, 0xff);

    SDL_Rect rect = {.x = 0, .y = SCALE * 160, .w = SCALE * 160, .h = SCALE * 60};

    SDL_RenderFillRect(renderer, &rect);
    SDL_RenderCopy(renderer, silkscreenTexture, nullptr, &rect);
}

void MainLoop::updateScreen() {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    SDL_Rect dest = {.x = 0, .y = 0, .w = SCALE * 160, .h = SCALE * 160};

    if (gSession->IsPowerOn() && EmHAL::CopyLCDFrame(frame)) {
        if (frame.lineWidth != 160 || frame.lines != 160 || frame.bpp != 1) return;

        uint32* pixels;
        int pitch;
        uint8* buffer = frame.GetBuffer();

        SDL_LockTexture(lcdTexture, nullptr, (void**)&pixels, &pitch);

        for (int x = 0; x < 160; x++)
            for (int y = 0; y < 160; y++)
                pixels[y * pitch / 4 + x] =
                    ((buffer[y * frame.bytesPerLine + (x + frame.margin) / 8] &
                      (0x80 >> ((x + frame.margin) % 8))) == 0
                         ? BACKGROUND_COLOR
                         : FOREGROUND_COLOR);

        SDL_UnlockTexture(lcdTexture);

        SDL_RenderCopy(renderer, lcdTexture, nullptr, &dest);
    } else {
        SDL_SetRenderDrawColor(renderer, BACKGROUND_HUE, BACKGROUND_HUE, BACKGROUND_HUE, 0xff);
        SDL_RenderFillRect(renderer, &dest);
    }

    drawSilkscreen(renderer);

    SDL_RenderPresent(renderer);
}

void MainLoop::handleEvents(long millis) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = true;
                    lastMouseMove = millis;
                    updatePenPosition();

                    handlePenDown();
                }

                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = false;
                    lastMouseMove = millis;
                    updatePenPosition();

                    handlePenUp();
                }

                break;

            case SDL_MOUSEMOTION:
                if (mouseDown && (millis - lastMouseMove > MOUSE_MOVE_THROTTLE) &&
                    updatePenPosition()) {
                    lastMouseMove = millis;

                    handlePenMove();
                }

                break;

            case SDL_TEXTINPUT:
                handleTextInput(event);
                break;

            case SDL_KEYDOWN:
                handleKeyDown(event);
                break;

            case SDL_KEYUP:
                if ((event.key.keysym.mod & KMOD_SHIFT) && (event.key.keysym.mod & KMOD_CTRL))
                    handleButtonKey(event, ButtonEvent::Type::release);
                break;
        }
        if (event.type == SDL_QUIT) running = false;
    }
}

bool MainLoop::updatePenPosition() {
    int x, y;
    SDL_GetMouseState(&x, &y);

    x /= SCALE;
    y /= SCALE;

    bool changed = x != penX || y != penY;

    penX = x;
    penY = y;

    return changed;
}

void MainLoop::handlePenDown() { gSession->QueuePenEvent(PenEvent::down(penX, penY)); }

void MainLoop::handlePenMove() { gSession->QueuePenEvent(PenEvent::down(penX, penY)); }

void MainLoop::handlePenUp() { gSession->QueuePenEvent(PenEvent::up()); }

void MainLoop::handleKeyDown(SDL_Event event) {
    if ((event.key.keysym.mod & KMOD_SHIFT) && (event.key.keysym.mod & KMOD_CTRL) &&
        (!event.key.repeat))
        return handleButtonKey(event, ButtonEvent::Type::press);

    switch (event.key.keysym.sym) {
        case SDLK_RETURN:
            gSession->QueueKeyboardEvent('\n');
            break;

        case SDLK_LEFT:
            gSession->QueueKeyboardEvent(chrLeftArrow);
            break;

        case SDLK_RIGHT:
            gSession->QueueKeyboardEvent(chrRightArrow);
            break;

        case SDLK_UP:
            gSession->QueueKeyboardEvent(chrUpArrow);
            break;

        case SDLK_DOWN:
            gSession->QueueKeyboardEvent(chrDownArrow);
            break;

        case SDLK_BACKSPACE:
            gSession->QueueKeyboardEvent(chrBackspace);
            break;

        case SDLK_TAB:
            gSession->QueueKeyboardEvent(chrHorizontalTabulation);
            break;
    }
}

void MainLoop::handleButtonKey(SDL_Event event, ButtonEvent::Type type) {
    switch (event.key.keysym.sym) {
        case SDLK_u:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::app1, type));
            break;

        case SDLK_i:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::app2, type));
            break;

        case SDLK_o:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::app3, type));
            break;

        case SDLK_p:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::app4, type));
            break;

        case SDLK_UP:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::rockerUp, type));
            break;

        case SDLK_DOWN:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::rockerDown, type));
            break;

        case SDLK_j:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::cradle, type));
            break;

        case SDLK_k:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::contrast, type));
            break;

        case SDLK_l:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::antenna, type));
            break;

        case SDLK_m:
            gSession->QueueButtonEvent(ButtonEvent(ButtonEvent::Button::power, type));
            break;
    }
}

void MainLoop::handleTextInput(SDL_Event event) {
    const char* text = event.text.text;
    char c = 0;

    if ((text[0] & 0x80) == 0) {
        // U+0000 -- U+007F: ASCII
        c = text[0];
    } else if ((text[0] & 0xfc) == 0xc0) {
        // U+0080 -- U+00FF: LATIN-1
        c = ((text[0] & 0x03) << 6) | (text[1] & 0x3f);
    }

    if (c) gSession->QueueKeyboardEvent(c);
}