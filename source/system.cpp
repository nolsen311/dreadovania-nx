// SPDX-License-Identifier: GPL-3.0-or-later
#include "system.hpp"

#include <switch.h>

namespace app {

SystemState system_state() {
    SystemState state;
    AppletType type = appletGetAppletType();
    state.app_is_application =
        type == AppletType_Application || type == AppletType_SystemApplication;
    if (state.app_is_application) {
        return state;  // the game slot is ours: nothing runs alongside
    }
    if (R_FAILED(pmdmntInitialize())) {
        return state;
    }
    u64 pid = 0;
    if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && R_SUCCEEDED(pminfoInitialize())) {
        u64 program = 0;
        if (R_SUCCEEDED(pminfoGetProgramId(&program, pid))) {
            state.running_program = program;
        }
        pminfoExit();
    }
    pmdmntExit();
    return state;
}

}  // namespace app
