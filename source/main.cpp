// SPDX-License-Identifier: GPL-3.0-or-later
// Dreadovania for Switch: the app skeleton (ADR 0004). It starts, shows its
// version and exits; pairing and seeds come in the next pull requests.
#include <switch.h>

#include <cstdio>
#include <string>

#include "dreadovania/paths.hpp"
#include "dreadovania/version.hpp"

int main(int /*argc*/, char** /*argv*/) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    const std::string version(dreadovania::kVersion);
    std::printf("Dreadovania %s\n\n", version.c_str());
    std::printf("This early build only checks that the app starts.\n");
    std::printf("Pairing with your server comes in the next version.\n\n");
    std::printf("Press + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
