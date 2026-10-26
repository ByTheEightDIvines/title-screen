#include "global.h"
// The legacy game headers map nullptr to the integer NULL macro. Keep the
// real C++ keyword before including the modern MSVC standard library.
#ifdef nullptr
#undef nullptr
#endif
#include "f_op/f_op_actor_mng.h"
#include "d/d_drawlist.h"
// The title actor exposes its state-machine methods but keeps the animation controllers private.
// Include its dependencies first; this access change affects only the title actor declaration.
#define private public
#include "d/actor/d_a_title.h"
#undef private
#include "d/d_com_inf_game.h"
#include "d/d_pane_class_alpha.h"
#include "f_op/f_op_overlap_mng.h"
#include "m_Do/m_Do_Reset.h"
#include "m_Do/m_Do_controller_pad.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "mods/svc/resource.h"
#include "audio_timing.h"
#include "reference_timing.h"
#include "pcm_player.h"
#include "audio_clock.h"
#include "music_return.h"
#include "d/d_camera.h"
#include <cstring>
#include <cstdio>

DEFINE_MOD();
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ResourceService, svc_resource);
DEFINE_HOOK(&daTitle_c::Execute, TitleExecute);
DEFINE_HOOK(&daTitle_c::Draw, TitleDraw);
DEFINE_HOOK(&daTitle_c::Delete, TitleDelete);
DEFINE_HOOK(&Z2SeqMgr::processBgmFramework, AudioFramework);
DEFINE_HOOK(&Z2SeqMgr::bgmStart, NativeBgmStart);
DEFINE_HOOK(&Z2SeqMgr::bgmStreamPrepare, NativeStreamPrepare);
DEFINE_HOOK(&Z2SeqMgr::bgmStreamPlay, NativeStreamPlay);
DEFINE_HOOK(&dCamera_c::Run, MoatCameraRun);
DEFINE_HOOK(&dCamera_c::NotRun, MoatCameraNotRun);

namespace {
enum class Phase { Waiting, Playing, Ready };
constexpr u32 kFadeFrames = 30; // Custom skip/end fade; native music waits five more seconds.
daTitle_c* g_title = nullptr;
// The header-defined JASGlobalInstance<T>::sInstance creates empty DLL-local
// globals. Capture the game's native audio receivers instead of those globals.
Z2SeqMgr* g_seqMgr = nullptr;
Phase g_phase = Phase::Waiting;
PcmPlayer g_intro;
ResourceBuffer g_pcm = RESOURCE_BUFFER_INIT;
// Verified in the installed GC disc's Z2Sound.baa: title_back.ast = 0x0200007d.
constexpr u32 g_titleMusic = 0x0200007d;
u32 g_frames = 0;
MusicReturn g_musicReturn;
bool g_audioStarted = false;
std::uint64_t g_startTick = 0;
float g_animationFrame = 0;

void syncAnimation(daTitle_c* title, float frame) {
    // The sound device's consumed samples are the clock, including its startup
    // latency. All four controllers share that clock; Execute cannot accumulate drift.
    if (frame < g_animationFrame) frame = g_animationFrame;
    g_animationFrame = frame;
    const float pose = frame < 319.0f ? frame : 319.0f;
    float material = 319.0f;
    for (unsigned i = 1; i < sizeof(kRevealFrames)/sizeof(kRevealFrames[0]); ++i) {
        if (pose <= kRevealFrames[i]) {
            float u = (pose-kRevealFrames[i-1])/(kRevealFrames[i]-kRevealFrames[i-1]);
            material = kMaterialFrames[i-1] + u*(kMaterialFrames[i]-kMaterialFrames[i-1]);
            break;
        }
    }
    title->mBck.setFrame(pose * 2.0f); // BCK stores the measured 60 Hz poses.
    title->mBpk.setFrame(material < title->mBpk.getEndFrame()-1 ? material : title->mBpk.getEndFrame()-1);
    title->mBrk.setFrame(material < title->mBrk.getEndFrame()-1 ? material : title->mBrk.getEndFrame()-1);
    title->mBtk.setFrame(material < title->mBtk.getEndFrame()-1 ? material : title->mBtk.getEndFrame()-1);
}

void prompt(daTitle_c* title, bool visible) {
    title->mIsDispLogo = 1;
    title->field_0x5f9 = visible ? 1 : 0;
    title->field_0x5fa = 0;
    title->field_0x604 = 0;
    if (title->field_0x600) {
        title->field_0x600->setAlpha(visible ? 255 : 0);
        title->field_0x600->alphaAnimeStart(0);
    }
}

void stopIntro(u32 fadeFrames) {
    g_intro.fade(fadeFrames * 1000 / 30);
}

void restoreMusic() {
    if (!g_title || !g_musicReturn.ready(g_intro.active(), audioClockMilliseconds()) || !g_seqMgr) return;
    g_musicReturn.cancel(); // Let our own prepare/play calls pass through the hooks.
    g_seqMgr->bgmStreamPrepare(g_titleMusic);
    g_seqMgr->bgmStreamPlay();
    svc_log->info(mod_ctx, "Regular title music started after five seconds of silence");
}

void suppressCurrentMusic() {
    if (!g_musicReturn.blocked() || !g_seqMgr) return;
    // Native title/cutscene music may use either sequence or stream playback.
    g_seqMgr->bgmStop(0, 0);
    g_seqMgr->bgmStreamStop(0);
}

void leaveTitle() {
    g_musicReturn.cancel();
    stopIntro(0);
}

void finalLogo(daTitle_c* title) {
    title->mBck.setFrame(title->mBck.getEndFrame() - 1.0f);
    title->mBpk.setFrame(title->mBpk.getEndFrame() - 1.0f);
    title->mBrk.setFrame(title->mBrk.getEndFrame() - 1.0f);
    title->mBtk.setFrame(title->mBtk.getEndFrame() - 1.0f);
    prompt(title, true);
    title->keyWaitInit();
}

void finish(daTitle_c* title, bool skipped) {
    stopIntro(kFadeFrames);
    g_musicReturn.finish(); // The silence clock starts only once the fade has stopped.
    finalLogo(title);
    g_phase = Phase::Ready;
    svc_log->info(mod_ctx, skipped ? "Intro skipped; native music waits five seconds after custom fade"
                                 : "Intro complete; native music waits five seconds after custom playback");
}

void beginAnimation(daTitle_c* title) {
    title->mBck.setFrame(0.0f);
    title->mBpk.setFrame(0.0f);
    title->mBrk.setFrame(0.0f);
    title->mBtk.setFrame(0.0f);
    prompt(title, false);
    title->logoDispAnmInit();
    title->mBck.setPlaySpeed(0.0f);
    title->mBpk.setPlaySpeed(0.0f);
    title->mBrk.setPlaySpeed(0.0f);
    title->mBtk.setPlaySpeed(0.0f);
    g_animationFrame = 0;
    g_startTick = audioClockMilliseconds();
    g_phase = Phase::Playing;
    g_frames = 0;
}

void begin(daTitle_c* title) {
    g_audioStarted = false;
    g_musicReturn.begin();
    suppressCurrentMusic();
    // Queue on a paused output device. Reset the animation, then release audio
    // in this same Execute call; no JAIStream preparation or private layouts.
    if (g_pcm.data && g_intro.queue(g_pcm.data, g_pcm.size)) {
        beginAnimation(title);
        g_audioStarted = g_intro.play();
        if (g_audioStarted) {
            svc_log->info(mod_ctx, "Hero of Legend PCM playback and animation started in the same frame");
            return;
        }
    } else {
        beginAnimation(title);
    }
    {
        char message[120];
        std::snprintf(message, sizeof(message), "PCM playback failed (audio error %u); animation continues", g_intro.error);
        svc_log->warn(mod_ctx, message);
    }
    g_musicReturn.finish();
}

HookAction execute(ModContext*, void* args, void* retval, void*) {
    auto* title = mods::arg<daTitle_c*>(args, 0);
    // Retain vanilla loading, transition and reset guards. Do not access UI until it exists.
    if (fopOvlpM_IsPeek() || mDoRst::isReset()) {
        if (title == g_title) leaveTitle();
        return HOOK_CONTINUE;
    }
    if (title->mProcID == 0 || !title->field_0x600) return HOOK_CONTINUE;
    if (g_title != title) {
        stopIntro(0);
        g_title = title;
        g_phase = Phase::Waiting;
        g_frames = 0;
        g_musicReturn.cancel();
    }
    // Release ownership before native file-select playback, rather than letting
    // the delayed title theme follow the player out of the title screen.
    if (g_phase == Phase::Ready && (title->mProcID == 4 ||
        mDoCPd_c::getTrigStart(PAD_1) || mDoCPd_c::getTrigA(PAD_1))) leaveTitle();
    restoreMusic();
    auto skipOriginal = [&]() { *static_cast<int*>(retval) = 1; return HOOK_SKIP_ORIGINAL; };
    const bool start = mDoCPd_c::getTrigStart(PAD_1) != 0;
    switch (g_phase) {
    case Phase::Waiting:
        prompt(title, true);
        if (start) begin(title);
        return skipOriginal(); // Suppress the demo's automatic logo-start / vanilla Start skip.
    case Phase::Playing:
        if (start) { finish(title, true); return skipOriginal(); }
        {
            double seconds = (audioClockMilliseconds() - g_startTick) / 1000.0;
            if (g_audioStarted) {
                double consumed = 0;
                if (g_intro.positionSeconds(consumed)) seconds = consumed;
                else seconds = g_animationFrame / 30.0; // Keep the last measured pose on query failure.
            }
            syncAnimation(title, static_cast<float>(seconds * 30.0));
            g_frames = static_cast<u32>(g_animationFrame);
        }
        // The logo holds its finished pose until the supplied track's final fade begins.
        if (g_frames >= kIntroFrames - kFadeFrames || (g_audioStarted && !g_intro.active()) ||
            (!g_audioStarted && g_frames >= kIntroFrames - kFadeFrames)) {
            finish(title, false);
            return skipOriginal(); // Do not deliver this same frame to vanilla keyWait().
        }
        return skipOriginal(); // Manual playback also suppresses A / vanilla animation stepping.
    case Phase::Ready:
        return HOOK_CONTINUE; // Subsequent Start/A uses the ordinary file-select path.
    }
    return HOOK_CONTINUE;
}

HookAction draw(ModContext*, void* args, void* retval, void*) {
    auto* title = mods::arg<daTitle_c*>(args, 0);
    if (title->mProcID != 0 && (g_title == nullptr || title == g_title) &&
        g_phase == Phase::Waiting) {
        if (title->field_0x600) dComIfGd_set2DOpaTop(&title->mTitle);
        *static_cast<int*>(retval) = 1;
        return HOOK_SKIP_ORIGINAL; // Keep custom logo hidden until Start; retain the 2D prompt.
    }
    return HOOK_CONTINUE;
}

void audioFramework(ModContext*, void* args, void*, void*) {
    g_seqMgr = mods::arg<Z2SeqMgr*>(args, 0);
    suppressCurrentMusic(); // Also handles a receiver first discovered after Start.
}

HookAction nativeBgmRequest(ModContext*, void* args, void*, void*) {
    g_seqMgr = mods::arg<Z2SeqMgr*>(args, 0);
    return g_musicReturn.blocked() ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

HookAction deleted(ModContext*, void* args, void*, void*) {
    if (mods::arg<daTitle_c*>(args, 0) == g_title) {
        leaveTitle();
        g_title = nullptr;
        g_phase = Phase::Waiting;
        g_frames = 0;
    }
    return HOOK_CONTINUE;
}

// Runs before the native camera process stores its view matrices. This is
// restricted to the title actor lifetime; gameplay cameras are untouched.
void moatCamera(ModContext*, void* args, void*, void*) {
    if (std::strcmp(dComIfGp_getStartStageName(), "F_SP102") != 0) return;
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    const cXyz eye(-24000.0f, 200.0f, -20000.0f);
    const cXyz center(-14000.0f, 900.0f, -35000.0f);
    camera->Set(center, eye, 40.0f, static_cast<s16>(0));
    camera->Reset();
    camera->SetTrimSize(0);
}

void uninstall() {
    mods::hook::uninstall<MoatCameraNotRun>();
    mods::hook::uninstall<MoatCameraRun>();
    mods::hook::uninstall<NativeStreamPlay>();
    mods::hook::uninstall<NativeStreamPrepare>();
    mods::hook::uninstall<NativeBgmStart>();
    mods::hook::uninstall<AudioFramework>();
    mods::hook::uninstall<TitleDelete>();
    mods::hook::uninstall<TitleDraw>();
    mods::hook::uninstall<TitleExecute>();
}
} // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    if (svc_resource->load(mod_ctx, "intro.pcm", &g_pcm) != MOD_OK ||
        !g_pcm.data || g_pcm.size != kIntroBytes) {
        svc_resource->free(mod_ctx, &g_pcm);
        svc_log->error(mod_ctx, "Hero of Legend PCM resource missing or invalid");
        return MOD_UNAVAILABLE;
    }
    ModResult result = mods::hook::add_pre<TitleExecute>(execute);
    if (result == MOD_OK) result = mods::hook::add_pre<TitleDraw>(draw);
    if (result == MOD_OK) result = mods::hook::add_pre<TitleDelete>(deleted);
    if (result == MOD_OK) result = mods::hook::add_post<AudioFramework>(audioFramework);
    if (result == MOD_OK) result = mods::hook::add_pre<NativeBgmStart>(nativeBgmRequest);
    if (result == MOD_OK) result = mods::hook::add_pre<NativeStreamPrepare>(nativeBgmRequest);
    if (result == MOD_OK) result = mods::hook::add_pre<NativeStreamPlay>(nativeBgmRequest);
    if (result == MOD_OK) result = mods::hook::add_post<MoatCameraRun>(moatCamera);
    if (result == MOD_OK) result = mods::hook::add_post<MoatCameraNotRun>(moatCamera);
    if (result != MOD_OK) { uninstall(); svc_resource->free(mod_ctx, &g_pcm); return result; }
    svc_log->info(mod_ctx, "Hero of Legend title ready: Start plays intro, another Start skips");
    return MOD_OK;
}
MOD_EXPORT ModResult mod_update(ModError*) {
    g_intro.update();
    restoreMusic();
    return MOD_OK;
}
MOD_EXPORT ModResult mod_shutdown(ModError*) {
    const bool restore = g_title && g_musicReturn.blocked();
    g_musicReturn.cancel();
    stopIntro(0);
    if (g_title) {
        finalLogo(g_title);
        // Unloading removes the update callback, so relinquish music immediately.
        if (restore && g_seqMgr) {
            g_seqMgr->bgmStreamPrepare(g_titleMusic);
            g_seqMgr->bgmStreamPlay();
        }
    }
    g_title = nullptr;
    uninstall();
    svc_resource->free(mod_ctx, &g_pcm);
    return MOD_OK;
}
}
