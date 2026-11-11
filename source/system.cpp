// SPDX-License-Identifier: GPL-3.0-or-later
#include "system.hpp"

#include <switch.h>

#include <algorithm>
#include <cstring>

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

std::string current_network() {
    std::string name;
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) {
        return name;
    }
    NifmNetworkProfileData profile = {};
    if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&profile))) {
        const NifmWirelessSettingData& wifi = profile.wireless_setting_data;
        std::size_t ssid_len = std::min<std::size_t>(wifi.ssid_len, sizeof(wifi.ssid) - 1);
        if (ssid_len > 0) {
            name.assign(wifi.ssid, ssid_len);
        } else {
            name.assign(profile.network_name, strnlen(profile.network_name,
                                                      sizeof(profile.network_name)));
        }
    }
    nifmExit();
    return name;
}

}  // namespace app
