#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "EmCommon.h"
#include "EmDevice.h"
#include "EmROMReader.h"
#include "EmSession.h"
#include "Logging.h"
#include "MainLoop.h"
#include "common.h"

using namespace std;

bool readFile(string file, unique_ptr<uint8[]>& buffer, long& len) {
    fstream stream(file, ios_base::in);
    if (stream.fail()) return false;

    stream.seekg(0, ios_base::end);
    len = stream.tellg();

    stream.seekg(0, ios_base::beg);
    buffer = make_unique<uint8[]>(len);

    stream.read((char*)buffer.get(), len);
    if (stream.gcount() != len) return false;

    return true;
}

void analyzeRom(EmROMReader& reader) {
    cout << "ROM info" << endl;
    cout << "================================================================================"
         << endl;
    cout << "Card version:          " << reader.GetCardVersion() << endl;
    cout << "Card name:             " << reader.GetCardName() << endl;
    cout << "Card manufacturer:     " << reader.GetCardManufacturer() << endl;
    cout << "Store version:         " << reader.GetStoreVersion() << endl;
    cout << "Company ID:            " << reader.GetCompanyID() << endl;
    cout << "HAL ID:                " << reader.GetHalID() << endl;
    cout << "ROM version:           " << reader.GetRomVersion() << endl;
    cout << "ROM version string:    " << reader.GetRomVersionString() << endl;

    cout << "CPU:                   ";
    if (reader.GetFlag328()) cout << "328 ";
    if (reader.GetFlagEZ()) cout << "EZ ";
    if (reader.GetFlagSZ()) cout << "SZ ";
    if (reader.GetFlagVZ()) cout << "VZ ";
    cout << endl;

    cout << "Databases:             ";
    for (auto&& database : reader.Databases()) {
        cout << database.Name() << " ";
    }
    cout << endl;

    cout << "================================================================================"
         << endl;
}

void initializeSession(string file) {
    unique_ptr<uint8[]> buffer;
    long len;

    if (!readFile(file, buffer, len)) {
        cerr << "unable to open " << file << endl;

        exit(1);
    }

    EmROMReader reader(buffer.get(), len);

    if (!reader.AcquireCardHeader() || !reader.AcquireROMHeap() || !reader.AcquireDatabases() ||
        !reader.AcquireFeatures()) {
        cerr << "unable to read ROM --- not a valid ROM image?" << endl;

        exit(1);
    }

    analyzeRom(reader);

    EmDevice* device = new EmDevice("PalmV");

    if (!device->SupportsROM(reader)) {
        cerr << "ROM not supported by Palm V" << endl;

        exit(1);
    }

    if (!gSession->Initialize(device, buffer.get(), len)) {
        cerr << "Session failed to initialize" << endl;

        exit(1);
    }
}

int main(int argc, const char** argv) {
    if (argc != 2) {
        cerr << "usage: cloudpalm <romimage.rom>" << endl;

        exit(1);
    }

    initializeSession(argv[1]);

#ifdef __EMSCRIPTEN__
    EM_ASM({ module.sessionReady(); });
#endif

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    IMG_Init(IMG_INIT_PNG);
    SDL_StartTextInput();

    SDL_Window* window;
    SDL_Renderer* renderer;

    if (SDL_CreateWindowAndRenderer(160 * SCALE, 220 * SCALE, 0, &window, &renderer) != 0) {
        cerr << "unable to create SDL window: " << SDL_GetError() << endl;
        exit(1);
    }

    MainLoop mainLoop(window, renderer);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg((em_arg_callback_func)MainLoop::cycleStatic, &mainLoop, 0, true);
#else

    while (mainLoop.isRunning()) {
        mainLoop.cycle();
    };

    SDL_Quit();
    IMG_Quit();
#endif
}

#ifdef __EMSCRIPTEN__
ButtonEvent::Button buttonFromId(string id) {
    if (id == "app1") return ButtonEvent::Button::app1;
    if (id == "app2") return ButtonEvent::Button::app2;
    if (id == "app3") return ButtonEvent::Button::app3;
    if (id == "app4") return ButtonEvent::Button::app4;
    if (id == "rockerUp") return ButtonEvent::Button::rockerUp;
    if (id == "rockerDown") return ButtonEvent::Button::rockerDown;
    if (id == "cradle") return ButtonEvent::Button::cradle;
    if (id == "power") return ButtonEvent::Button::power;

    return ButtonEvent::Button::invalid;
}

extern "C" void EMSCRIPTEN_KEEPALIVE buttonDown(const char* id) {
    gSession->QueueButtonEvent(ButtonEvent(buttonFromId(id), ButtonEvent::Type::press));
}

extern "C" void EMSCRIPTEN_KEEPALIVE buttonUp(const char* id) {
    gSession->QueueButtonEvent(ButtonEvent(buttonFromId(id), ButtonEvent::Type::release));
}
#endif