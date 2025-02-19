// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/config.h"
#include "core/libraries/app_content/app_content.h"
#include "core/libraries/audio/audioin.h"
#include "core/libraries/audio/audioout.h"
#include "core/libraries/disc_map/disc_map.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/kernel/libkernel.h"
#include "core/libraries/libc/libc.h"
#include "core/libraries/libc_internal/libc_internal.h"
#include "core/libraries/libs.h"
#include "core/libraries/network/http.h"
#include "core/libraries/network/net.h"
#include "core/libraries/network/netctl.h"
#include "core/libraries/network/ssl.h"
#include "core/libraries/np_manager/np_manager.h"
#include "core/libraries/np_score/np_score.h"
#include "core/libraries/np_trophy/np_trophy.h"
#include "core/libraries/pad/pad.h"
#include "core/libraries/playgo/playgo.h"
#include "core/libraries/rtc/rtc.h"
#include "core/libraries/save_data/savedata.h"
#include "core/libraries/screenshot/screenshot.h"
#include "core/libraries/system/commondialog.h"
#include "core/libraries/system/msgdialog.h"
#include "core/libraries/system/posix.h"
#include "core/libraries/system/savedatadialog.h"
#include "core/libraries/system/sysmodule.h"
#include "core/libraries/system/systemservice.h"
#include "core/libraries/system/userservice.h"
#include "core/libraries/usbd/usbd.h"
#include "core/libraries/videoout/video_out.h"
#include "src/core/libraries/libpng/pngdec.h"

namespace Libraries {

void InitHLELibs(Core::Loader::SymbolsResolver* sym) {
    Libraries::Kernel::LibKernel_Register(sym);
    Libraries::VideoOut::RegisterLib(sym);
    Libraries::GnmDriver::RegisterlibSceGnmDriver(sym);
    if (!Config::isLleLibc()) {
        Libraries::LibC::libcSymbolsRegister(sym);
    }

    // New libraries folder from autogen
    Libraries::UserService::RegisterlibSceUserService(sym);
    Libraries::SystemService::RegisterlibSceSystemService(sym);
    Libraries::CommonDialog::RegisterlibSceCommonDialog(sym);
    Libraries::MsgDialog::RegisterlibSceMsgDialog(sym);
    Libraries::AudioOut::RegisterlibSceAudioOut(sym);
    Libraries::Http::RegisterlibSceHttp(sym);
    Libraries::Net::RegisterlibSceNet(sym);
    Libraries::NetCtl::RegisterlibSceNetCtl(sym);
    Libraries::SaveData::RegisterlibSceSaveData(sym);
    Libraries::Ssl::RegisterlibSceSsl(sym);
    Libraries::SysModule::RegisterlibSceSysmodule(sym);
    Libraries::Posix::Registerlibsceposix(sym);
    Libraries::AudioIn::RegisterlibSceAudioIn(sym);
    Libraries::SaveDataDialog::RegisterlibSceSaveDataDialog(sym);
    Libraries::NpManager::RegisterlibSceNpManager(sym);
    Libraries::NpScore::RegisterlibSceNpScore(sym);
    Libraries::NpTrophy::RegisterlibSceNpTrophy(sym);
    Libraries::ScreenShot::RegisterlibSceScreenShot(sym);
    Libraries::LibcInternal::RegisterlibSceLibcInternal(sym);
    Libraries::AppContent::RegisterlibSceAppContent(sym);
    Libraries::Rtc::RegisterlibSceRtc(sym);
    Libraries::DiscMap::RegisterlibSceDiscMap(sym);
    Libraries::PngDec::RegisterlibScePngDec(sym);
    Libraries::PlayGo::RegisterlibScePlayGo(sym);
    Libraries::Usbd::RegisterlibSceUsbd(sym);
    Libraries::Pad::RegisterlibScePad(sym);
}

} // namespace Libraries
