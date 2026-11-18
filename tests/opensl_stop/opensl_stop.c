/* Android OpenSL stop/restart regression. Run only on a connected test device. */
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void require_true(int condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        fflush(stderr);
        _exit(2);
    }
}

int main(int argc, char** argv)
{
    ma_backend                      backend = ma_backend_opensl;
    ma_context                      context;
    ma_engine                       engine;
    ma_sound                        sound;
    ma_engine_config                config;
    ma_device*                      device;
    SLAndroidSimpleBufferQueueItf   queue;
    SLPlayItf                       player;
    SLAndroidSimpleBufferQueueState queueState;
    ma_uint64                       before;
    ma_timer                        timer;
    double                          elapsed;
    const char*                     mode = argc > 1 ? argv[1] : "normal";

    require_true(argc > 2, "test WAV path supplied");
    require_true(strcmp(mode, "normal") == 0 || strcmp(mode, "empty-queue") == 0
                     || strcmp(mode, "paused-player") == 0 || strcmp(mode, "stopped-player") == 0,
                 "known test mode");
    require_true(ma_context_init(&backend, 1, NULL, &context) == MA_SUCCESS, "OpenSL context");
    config          = ma_engine_config_init();
    config.pContext = &context;
    require_true(ma_engine_init(&config, &engine) == MA_SUCCESS, "engine starts");
    require_true(ma_sound_init_from_file(&engine, argv[2], MA_SOUND_FLAG_STREAM, NULL, NULL, &sound)
                     == MA_SUCCESS,
                 "load test WAV");
    ma_sound_set_looping(&sound, MA_TRUE);
    require_true(ma_sound_start(&sound) == MA_SUCCESS, "play test WAV");
    device = ma_engine_get_device(&engine);
    require_true(device != NULL && device->pContext->backend == ma_backend_opensl,
                 "real OpenSL backend");
    queue  = (SLAndroidSimpleBufferQueueItf)device->opensl.pBufferQueuePlayback;
    player = (SLPlayItf)device->opensl.pAudioPlayer;
    ma_sleep(100);
    require_true((*queue)->GetState(queue, &queueState) == SL_RESULT_SUCCESS,
                 "initial queue state");
    printf("INITIAL mode=%s queued=%u device_state=%d clock=%llu\n",
           mode,
           queueState.count,
           (int)ma_device_get_state(device),
           (unsigned long long)ma_engine_get_time_in_pcm_frames(&engine));
    fflush(stdout);
    if (strcmp(mode, "empty-queue") == 0) {
        require_true((*queue)->Clear(queue) == SL_RESULT_SUCCESS, "clear underlying queue");
    } else if (strcmp(mode, "paused-player") == 0) {
        require_true((*player)->SetPlayState(player, SL_PLAYSTATE_PAUSED) == SL_RESULT_SUCCESS,
                     "pause underlying player");
    } else if (strcmp(mode, "stopped-player") == 0) {
        require_true((*player)->SetPlayState(player, SL_PLAYSTATE_STOPPED) == SL_RESULT_SUCCESS,
                     "stop underlying player");
    }
    ma_sleep(200);
    before = ma_engine_get_time_in_pcm_frames(&engine);
    ma_sleep(100);
    require_true((*queue)->GetState(queue, &queueState) == SL_RESULT_SUCCESS,
                 "disrupted queue state");
    printf("BEFORE_STOP queued=%u device_state=%d clock1=%llu clock2=%llu\n",
           queueState.count,
           (int)ma_device_get_state(device),
           (unsigned long long)before,
           (unsigned long long)ma_engine_get_time_in_pcm_frames(&engine));
    fflush(stdout);
    ma_timer_init(&timer);
    require_true(ma_engine_stop(&engine) == MA_SUCCESS, "engine stops");
    elapsed = ma_timer_get_time_in_seconds(&timer);
    printf("STOP_RETURNED seconds=%.3f state=%d\n", elapsed, (int)ma_device_get_state(device));
    fflush(stdout);
    require_true(elapsed < 2.5, "stop has a finite wait");
    require_true(ma_device_get_state(device) == ma_device_state_stopped, "device stopped");
    require_true((*queue)->GetState(queue, &queueState) == SL_RESULT_SUCCESS
                     && queueState.count == 0,
                 "queue cleared");
    require_true(ma_engine_start(&engine) == MA_SUCCESS, "engine restarts");
    before = ma_engine_get_time_in_pcm_frames(&engine);
    ma_sleep(100);
    require_true(ma_engine_get_time_in_pcm_frames(&engine) > before, "output callbacks resume");
    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
    ma_context_uninit(&context);
    printf("PASS %s\n", mode);
    return 0;
}
