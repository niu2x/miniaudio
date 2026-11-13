/* Device-independent regression coverage for the stable-branch backports. */
#include "../../miniaudio.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define OK(x) CHECK((x) == MA_SUCCESS)

/* A real PCM WAV served through an in-memory VFS, including wide-name opens. */
static unsigned char wavData[44 + 48000 * 3 * 2];
typedef struct { size_t cursor; } memory_file;
typedef struct {
    ma_vfs_callbacks cb; ma_uint32 opens; ma_uint32 closes;
    ma_resource_manager* cancelManager; ma_result cancelResult;
} memory_vfs;
static void put32(unsigned char* p, ma_uint32 x)
{
    p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8);
    p[2] = (unsigned char)(x >> 16); p[3] = (unsigned char)(x >> 24);
}
static void make_wav(void)
{
    size_t i;
    memcpy(wavData, "RIFF", 4); put32(wavData + 4, sizeof(wavData) - 8);
    memcpy(wavData + 8, "WAVEfmt ", 8); put32(wavData + 16, 16);
    wavData[20] = 1; wavData[22] = 1; put32(wavData + 24, 48000);
    put32(wavData + 28, 96000); wavData[32] = 2; wavData[34] = 16;
    memcpy(wavData + 36, "data", 4); put32(wavData + 40, sizeof(wavData) - 44);
    for (i = 44; i < sizeof(wavData); i += 2) { wavData[i] = 0; wavData[i+1] = 32; }
}
static ma_result mem_open(ma_vfs* v, const char* path, ma_uint32 mode, ma_vfs_file* out)
{
    memory_file* f = (memory_file*)calloc(1, sizeof(*f));
    (void)path; (void)mode;
    if (f == NULL) return MA_OUT_OF_MEMORY;
    *out = f; ma_atomic_fetch_add_32(&((memory_vfs*)v)->opens, 1); return MA_SUCCESS;
}
static ma_result mem_open_w(ma_vfs* v, const wchar_t* path, ma_uint32 mode, ma_vfs_file* out)
{ (void)path; return mem_open(v, "sound", mode, out); }
static ma_result mem_close(ma_vfs* v, ma_vfs_file file)
{ free(file); ma_atomic_fetch_add_32(&((memory_vfs*)v)->closes, 1); return MA_SUCCESS; }
static ma_result mem_read(ma_vfs* v, ma_vfs_file file, void* out, size_t count, size_t* readCount)
{
    memory_file* f = (memory_file*)file;
    if (v != NULL && ((memory_vfs*)v)->cancelManager != NULL && f->cursor >= 44) {
        memory_vfs* mv = (memory_vfs*)v;
        ma_resource_manager* manager = mv->cancelManager;
        mv->cancelManager = NULL;
        mv->cancelResult = ma_resource_manager_unregister_file(manager, "sound.wav");
    }
    if (count > sizeof(wavData) - f->cursor) count = sizeof(wavData) - f->cursor;
    memcpy(out, wavData + f->cursor, count); f->cursor += count;
    if (readCount != NULL) *readCount = count;
    return count ? MA_SUCCESS : MA_AT_END;
}
static ma_result mem_seek(ma_vfs* v, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
    memory_file* f = (memory_file*)file;
    ma_int64 pos = offset;
    (void)v;
    if (origin == ma_seek_origin_current) pos += (ma_int64)f->cursor;
    if (origin == ma_seek_origin_end) pos += sizeof(wavData);
    if (pos < 0 || (ma_uint64)pos > sizeof(wavData)) return MA_BAD_SEEK;
    f->cursor = (size_t)pos; return MA_SUCCESS;
}
static ma_result mem_tell(ma_vfs* v, ma_vfs_file file, ma_int64* cursor)
{ (void)v; *cursor = (ma_int64)((memory_file*)file)->cursor; return MA_SUCCESS; }
static ma_result mem_info(ma_vfs* v, ma_vfs_file file, ma_file_info* info)
{ (void)v; (void)file; info->sizeInBytes = sizeof(wavData); return MA_SUCCESS; }
static void init_vfs(memory_vfs* v)
{
    memset(v, 0, sizeof(*v));
    v->cb.onOpen = mem_open; v->cb.onOpenW = mem_open_w; v->cb.onClose = mem_close;
    v->cb.onRead = mem_read; v->cb.onSeek = mem_seek; v->cb.onTell = mem_tell; v->cb.onInfo = mem_info;
}

static ma_result decoder_read(ma_decoder* d, void* out, size_t count, size_t* n)
{ return mem_read(NULL, d->pUserData, out, count, n); }
static ma_result decoder_seek(ma_decoder* d, ma_int64 offset, ma_seek_origin origin)
{ return mem_seek(NULL, d->pUserData, offset, origin); }

static float customSamples[64];
static ma_result custom_init(void* user, ma_read_proc readProc, ma_seek_proc seekProc, ma_tell_proc tellProc,
    void* ioUser, const ma_decoding_backend_config* config, const ma_allocation_callbacks* alloc, ma_data_source** out)
{
    ma_audio_buffer_ref* b = (ma_audio_buffer_ref*)ma_malloc(sizeof(*b), alloc);
    ma_result result;
    (void)user; (void)readProc; (void)seekProc; (void)tellProc; (void)ioUser; (void)config;
    if (b == NULL) return MA_OUT_OF_MEMORY;
    result = ma_audio_buffer_ref_init(ma_format_f32, 1, customSamples, 64, b);
    if (result != MA_SUCCESS) { ma_free(b, alloc); return result; }
    b->sampleRate = 48000; *out = b; return MA_SUCCESS;
}
static void custom_uninit(void* user, ma_data_source* backend, const ma_allocation_callbacks* alloc)
{ (void)user; ma_audio_buffer_ref_uninit((ma_audio_buffer_ref*)backend); ma_free(backend, alloc); }
static ma_decoding_backend_vtable customVtable = {custom_init, NULL, NULL, NULL, custom_uninit};
static int test_decoder(void)
{
    memory_vfs v;
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoding_backend_vtable* backends[1] = {&customVtable};
    float output[8];
    ma_uint64 n;
    memory_file io = {0};
    int i;
    init_vfs(&v);
    /* Explicit Vorbis has no built-in backend in this test. */
    config.encodingFormat = ma_encoding_format_vorbis;
    for (i = 0; i < 20; ++i) {
        CHECK(ma_decoder_init_vfs(&v, "sound", &config, &decoder) == MA_NO_BACKEND);
        CHECK(ma_decoder_init_vfs_w(&v, L"sound", &config, &decoder) == MA_NO_BACKEND);
    }
    CHECK(v.opens == 40 && v.closes == 40);
    customSamples[0] = 0.75f;
    config.ppCustomBackendVTables = backends; config.customBackendCount = 1;
    OK(ma_decoder_init(decoder_read, decoder_seek, &io, &config, &decoder));
    OK(ma_decoder_read_pcm_frames(&decoder, output, 8, &n)); CHECK(n == 8 && output[0] == 0.75f);
    OK(ma_decoder_uninit(&decoder));
    OK(ma_decoder_init_vfs(&v, "sound", &config, &decoder)); OK(ma_decoder_uninit(&decoder));
    OK(ma_decoder_init_vfs_w(&v, L"sound", &config, &decoder)); OK(ma_decoder_uninit(&decoder));
    CHECK(v.opens == v.closes);
    puts("PASS: explicit-format custom decoder and narrow/wide VFS cleanup"); return 0;
}

/* Fail each allocation made during resource-manager construction and count ownership. */
typedef struct { size_t calls; size_t failAt; size_t live; } allocation_state;
static void* tracked_malloc(size_t size, void* user)
{
    allocation_state* s = (allocation_state*)user;
    void* p;
    if (++s->calls == s->failAt) return NULL;
    p = malloc(size); if (p != NULL) ++s->live; return p;
}
static void* tracked_realloc(void* old, size_t size, void* user)
{
    allocation_state* s = (allocation_state*)user;
    void* p;
    ma_bool32 isNew = old == NULL;
    if (++s->calls == s->failAt) return NULL;
    p = realloc(old, size); if (p != NULL && isNew) ++s->live; return p;
}
static void tracked_free(void* p, void* user)
{ if (p != NULL) { --((allocation_state*)user)->live; free(p); } }
static ma_result fail_device_start(ma_device* device)
{ (void)device; return MA_ERROR; }

static int test_engine_failure(void)
{
    allocation_state s;
    ma_engine engine;
    ma_engine_config config = ma_engine_config_init();
    size_t limit, i;
    ma_result result;
    config.channels = 1; config.sampleRate = 48000; config.noDevice = MA_TRUE;
    memset(&s, 0, sizeof(s));
    config.allocationCallbacks.pUserData = &s; config.allocationCallbacks.onMalloc = tracked_malloc;
    config.allocationCallbacks.onRealloc = tracked_realloc; config.allocationCallbacks.onFree = tracked_free;
    OK(ma_engine_init(&config, &engine)); limit = s.calls; ma_engine_uninit(&engine); CHECK(s.live == 0);
    for (i = 1; i <= limit; ++i) {
        memset(&s, 0, sizeof(s)); s.failAt = i;
        result = ma_engine_init(&config, &engine);
        if (result == MA_SUCCESS) ma_engine_uninit(&engine);
        CHECK(s.live == 0);
    }
    {
        ma_context context;
        ma_device device;
        ma_backend backend = ma_backend_null;
        ma_device_config dc = ma_device_config_init(ma_device_type_playback);
        dc.playback.format = ma_format_f32; dc.playback.channels = 1; dc.sampleRate = 48000;
        OK(ma_context_init(&backend, 1, NULL, &context));
        OK(ma_device_init(&context, &dc, &device));
        context.callbacks.onDeviceStart = fail_device_start;
        memset(&s, 0, sizeof(s)); config.pDevice = &device;
        CHECK(ma_engine_init(&config, &engine) != MA_SUCCESS); CHECK(s.live == 0);
        ma_device_uninit(&device); ma_context_uninit(&context);
    }
    printf("PASS: engine allocation failure cleanup (%lu injection points)\n", (unsigned long)limit); return 0;
}

static ma_result refuse_seek(ma_data_source* source, ma_uint64 frame)
{ (void)source; (void)frame; return MA_BAD_SEEK; }

static int test_seek(void)
{
    ma_engine engine;
    ma_engine_config config = ma_engine_config_init();
    ma_audio_buffer_ref source;
    ma_sound sound;
    ma_data_source_vtable refusingVtable;
    ma_uint64 before;
    ma_int16 samples[8192];
    float output[64];
    unsigned int i;
    config.noDevice = MA_TRUE; config.channels = 1; config.sampleRate = 48000;
    for (i = 0; i < 8192; ++i) samples[i] = i < 4096 ? 16384 : -16384;
    OK(ma_engine_init(&config, &engine));
    OK(ma_audio_buffer_ref_init(ma_format_s16, 1, samples, 8192, &source)); source.sampleRate = 48000;
    OK(ma_sound_init_from_data_source(&engine, &source, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &sound));
    OK(ma_sound_start(&sound));
    for (i = 0; i < 10; ++i) OK(ma_engine_read_pcm_frames(&engine, output, 64, NULL));
    OK(ma_engine_read_pcm_frames(&engine, output, 1, NULL));
    CHECK(output[0] > 0.4f); CHECK(sound.processingCacheFramesRemaining > 0);
    OK(ma_sound_seek_to_pcm_frame(&sound, 4096));
    OK(ma_engine_read_pcm_frames(&engine, output, 64, NULL));
    /* Resetting the linear resampler introduces one silent history frame. */
    CHECK(fabs(output[0]) < 0.001f);
    for (i = 1; i < 64; ++i) CHECK(output[i] < -0.4f);
    refusingVtable = *source.ds.vtable; refusingVtable.onSeek = refuse_seek;
    source.ds.vtable = &refusingVtable;
    before = ma_node_get_time(&sound);
    OK(ma_sound_seek_to_pcm_frame(&sound, 1234));
    OK(ma_engine_read_pcm_frames(&engine, output, 64, NULL));
    CHECK(ma_node_get_time(&sound) == before + 64);
    ma_sound_uninit(&sound); ma_audio_buffer_ref_uninit(&source); ma_engine_uninit(&engine);
    puts("PASS: seek discards cached audio; failed seek preserves node time"); return 0;
}

/* Interleave cancellation in the middle of a page read, without timing assumptions. */
static int test_cancel_during_page(void)
{
    memory_vfs v;
    ma_resource_manager manager;
    ma_resource_manager_config config = ma_resource_manager_config_init();
    ma_job freeJob, unexpectedJob;
    init_vfs(&v); config.pVFS = &v; config.jobThreadCount = 0;
    config.flags = MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING;
    OK(ma_resource_manager_init(&config, &manager));
    OK(ma_resource_manager_register_file(&manager, "sound.wav", MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE));
    OK(ma_resource_manager_process_next_job(&manager)); /* Decoder initialization. */
    v.cancelManager = &manager;
    CHECK(ma_resource_manager_process_next_job(&manager) == MA_UNAVAILABLE);
    CHECK(v.cancelManager == NULL && v.cancelResult == MA_SUCCESS);
    OK(ma_resource_manager_next_job(&manager, &freeJob));
    CHECK(freeJob.toc.breakup.code == MA_JOB_TYPE_RESOURCE_MANAGER_FREE_DATA_BUFFER_NODE);
    /* There must be no new page job queued behind the free job. */
    CHECK(ma_resource_manager_next_job(&manager, &unexpectedJob) == MA_NO_DATA_AVAILABLE);
    OK(ma_job_process(&freeJob));
    ma_resource_manager_uninit(&manager); CHECK(v.opens == v.closes);
    puts("PASS: cancellation during a page read cannot enqueue work after free"); return 0;
}

static int test_resources(void)
{
    memory_vfs v;
    ma_resource_manager manager;
    ma_resource_manager_config config = ma_resource_manager_config_init();
    ma_resource_manager_data_buffer buffer;
    ma_resource_manager_data_stream stream;
    ma_resource_manager_pipeline_notifications notifications;
    ma_fence initFence, doneFence;
    unsigned int i;
    init_vfs(&v); config.pVFS = &v; config.jobThreadCount = 4;
    OK(ma_resource_manager_init(&config, &manager));
    for (i = 0; i < 500; ++i) {
        ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC;
        if (i % 2) flags |= MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_WAIT_INIT;
        if (i % 4 >= 2) flags |= MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_UNKNOWN_LENGTH;
        OK(ma_resource_manager_data_buffer_init(&manager, "sound.wav", flags, NULL, &buffer));
        OK(ma_resource_manager_data_buffer_uninit(&buffer));
    }
    OK(ma_fence_init(&initFence)); OK(ma_fence_init(&doneFence));
    memset(&notifications, 0, sizeof(notifications));
    notifications.init.pFence = &initFence; notifications.done.pFence = &doneFence;
    for (i = 0; i < 20; ++i) {
        OK(ma_resource_manager_data_stream_init(&manager, "sound.wav", MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC, &notifications, &stream));
        OK(ma_fence_wait(&initFence));
        /* A stream must never acquire a done fence. Avoid blocking on a regression. */
        CHECK(ma_atomic_load_32(&doneFence.counter) == 0); OK(ma_fence_wait(&doneFence));
        OK(ma_resource_manager_data_stream_uninit(&stream));
    }
    ma_fence_uninit(&doneFence); ma_fence_uninit(&initFence);
    ma_resource_manager_uninit(&manager); CHECK(v.opens == v.closes);
    puts("PASS: 500 async buffer cancellations (4 workers), stream fences and VFS cleanup"); return 0;
}

static int test_wav_metadata(void)
{
    unsigned char data[80];
    ma_dr_wav wav;
    ma_int16 sample;
    memset(data, 0, sizeof(data));
    memcpy(data, wavData, 36);
    put32(data + 4, sizeof(data) - 8);
    memcpy(data + 36, "LIST", 4); put32(data + 40, 12);
    /* INAM claims 16 bytes, but the enclosing LIST ends after its header. */
    memcpy(data + 44, "INFOINAM", 8); put32(data + 52, 16);
    memcpy(data + 56, "JUNK", 4); put32(data + 60, 6);
    memcpy(data + 70, "data", 4); put32(data + 74, 2); data[79] = 32;
    CHECK(ma_dr_wav_init_memory_with_metadata(&wav, data, sizeof(data), 0, NULL));
    { ma_uint32 j; for (j = 0; j < wav.metadataCount; ++j) CHECK(wav.pMetadata[j].type != ma_dr_wav_metadata_type_list_info_title); }
    CHECK(ma_dr_wav_read_pcm_frames_s16(&wav, 1, &sample) == 1); CHECK(sample == 8192);
    ma_dr_wav_uninit(&wav);
    puts("PASS: malformed WAV LIST cannot consume metadata outside its chunk"); return 0;
}

static int test_flac_seek(const char* path)
{
    ma_dr_flac* f = ma_dr_flac_open_file(path, NULL);
    ma_int16* reference;
    ma_int16 actual[128 * 8];
    ma_uint64 positions[8], i, count;
    CHECK(f != NULL && f->channels <= 8 && f->totalPCMFrameCount > 4096);
    count = f->totalPCMFrameCount;
    reference = (ma_int16*)malloc((size_t)count * f->channels * sizeof(*reference)); CHECK(reference != NULL);
    CHECK(ma_dr_flac_read_pcm_frames_s16(f, count, reference) == count);
    positions[0] = count / 2; positions[1] = 1; positions[2] = 0; positions[3] = 4095;
    positions[4] = count - 128; positions[5] = 4096; positions[6] = 127; positions[7] = count / 3;
    /* Force the binary search path even if the fixture carries a seek table. */
    f->seekpointCount = 0;
    for (i = 0; i < 8; ++i) {
        CHECK(ma_dr_flac_seek_to_pcm_frame(f, positions[i]));
        CHECK(ma_dr_flac_read_pcm_frames_s16(f, 128, actual) == 128);
        CHECK(memcmp(actual, reference + positions[i] * f->channels, 128 * f->channels * sizeof(*actual)) == 0);
    }
    free(reference); ma_dr_flac_close(f);
    puts("PASS: FLAC binary-search seeks match sequential decoding"); return 0;
}

static int test_mp3_seek(const char* path)
{
    FILE* file = fopen(path, "rb");
    unsigned char data[4096];
    size_t size;
    ma_dr_mp3 plain, prefixed;
    float expected[2048], actual[2048];
    CHECK(file != NULL); size = fread(data, 1, sizeof(data), file); fclose(file);
    CHECK(size > 19 && size < sizeof(data));
    CHECK(ma_dr_mp3_init_memory(&plain, data + 19, size - 19, NULL));
    CHECK(ma_dr_mp3_init_memory(&prefixed, data, size, NULL));
    CHECK(prefixed.streamStartOffset == plain.streamStartOffset + 19);
    CHECK(plain.channels == 1 && prefixed.channels == 1);
    CHECK(ma_dr_mp3_read_pcm_frames_f32(&plain, 2048, expected) == 2048);
    CHECK(ma_dr_mp3_read_pcm_frames_f32(&prefixed, 2048, actual) == 2048);
    CHECK(memcmp(expected, actual, sizeof(actual)) == 0);
    CHECK(ma_dr_mp3_seek_to_pcm_frame(&prefixed, 0));
    CHECK(ma_dr_mp3_read_pcm_frames_f32(&prefixed, 2048, actual) == 2048);
    CHECK(memcmp(expected, actual, sizeof(actual)) == 0);
    ma_dr_mp3_uninit(&plain); ma_dr_mp3_uninit(&prefixed);
    puts("PASS: MP3 Xing frame with an ID3 prefix seeks to the correct audio offset"); return 0;
}

int main(int argc, char** argv)
{
    make_wav();
    if (test_wav_metadata()) return 1;
    if (argc > 1 && test_flac_seek(argv[1])) return 1;
    if (argc > 2 && test_mp3_seek(argv[2])) return 1;
    if (test_decoder() || test_engine_failure() || test_seek() || test_cancel_during_page() || test_resources()) return 1;
    return 0;
}
