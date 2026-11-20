/* Regression coverage for selected fixes from upstream 65c1fd32. */
#define MA_NO_DEVICE_IO
#include "../../miniaudio.c"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

typedef struct
{
    const unsigned char* data;
    size_t size;
    size_t cursor;
    ma_bool32 forwardOnly;
    ma_bool32 failRestore;
    ma_bool32 negativeTell;
    unsigned int absoluteSeeks;
    unsigned int endSeeks;
    unsigned int tells;
} wav_io;

static void put32(unsigned char* p, ma_uint32 n)
{
    p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8);
    p[2] = (unsigned char)(n >> 16); p[3] = (unsigned char)(n >> 24);
}

static size_t wav_read(void* user, void* out, size_t n)
{
    wav_io* io = (wav_io*)user;
    if (n > io->size - io->cursor) n = io->size - io->cursor;
    memcpy(out, io->data + io->cursor, n);
    io->cursor += n;
    return n;
}

static ma_bool32 wav_seek(void* user, int offset, ma_dr_wav_seek_origin origin)
{
    wav_io* io = (wav_io*)user;
    ma_int64 pos = offset;
    if (origin == MA_DR_WAV_SEEK_SET) {
        io->absoluteSeeks += 1;
        if (io->forwardOnly || (io->failRestore && io->endSeeks > 0)) return MA_FALSE;
    } else if (origin == MA_DR_WAV_SEEK_CUR) {
        if (io->forwardOnly && offset < 0) return MA_FALSE;
        pos += (ma_int64)io->cursor;
    } else {
        io->endSeeks += 1;
        if (io->forwardOnly) return MA_FALSE;
        pos += (ma_int64)io->size;
    }
    if (pos < 0 || (ma_uint64)pos > io->size) return MA_FALSE;
    io->cursor = (size_t)pos;
    return MA_TRUE;
}

static ma_bool32 wav_tell(void* user, ma_int64* cursor)
{
    wav_io* io = (wav_io*)user;
    io->tells += 1;
    *cursor = io->negativeTell ? -1 : (ma_int64)io->cursor;
    return MA_TRUE;
}

static void make_wav(unsigned char* data, ma_bool32 unknownLength)
{
    /* Six s16 samples; a valid INFO title precedes the data chunk. */
    memset(data, 0, 80);
    memcpy(data, "RIFF", 4); put32(data + 4, 72);
    memcpy(data + 8, "WAVEfmt ", 8); put32(data + 16, 16);
    data[20] = 1; data[22] = 1; put32(data + 24, 48000);
    put32(data + 28, 96000); data[32] = 2; data[34] = 16;
    memcpy(data + 36, "LIST", 4); put32(data + 40, 16);
    memcpy(data + 44, "INFOINAM", 8); put32(data + 52, 4); memcpy(data + 56, "abc", 4);
    memcpy(data + 60, "data", 4); put32(data + 64, unknownLength ? 0xFFFFFFFFU : 12);
    data[69] = 32; data[71] = 64; data[73] = 96;
    data[75] = 128; data[77] = 160; data[79] = 192;
}

static int check_wav_samples(ma_dr_wav* wav)
{
    static const ma_int16 expected[6] = {8192, 16384, 24576, -32768, -24576, -16384};
    ma_int16 samples[6];
    CHECK(ma_dr_wav_read_pcm_frames_s16(wav, 6, samples) == 6);
    CHECK(memcmp(samples, expected, sizeof(samples)) == 0);
    CHECK(ma_dr_wav_read_pcm_frames_s16(wav, 1, samples) == 0);
    return 0;
}

static int test_wav(void)
{
    unsigned char data[80];
    unsigned int unknown, tell, metadata;
    ma_dr_wav wav;
    wav_io io;
    for (unknown = 0; unknown < 2; ++unknown) {
        make_wav(data, unknown);
        memset(&io, 0, sizeof(io)); io.data = data; io.size = sizeof(data); io.forwardOnly = MA_TRUE;
        CHECK(ma_dr_wav_init_ex(&wav, wav_read, wav_seek, wav_tell, NULL, &io, NULL, MA_DR_WAV_SEQUENTIAL, NULL));
        CHECK(io.absoluteSeeks == 0 && io.endSeeks == 0 && io.tells == 0);
        CHECK(io.cursor == 68); /* Initialization must not consume PCM, including unknown-length PCM. */
        CHECK(check_wav_samples(&wav) == 0);
        ma_dr_wav_uninit(&wav);
        for (tell = 0; tell < 2; ++tell) {
            for (metadata = 0; metadata < 2; ++metadata) {
                ma_uint32 flags = metadata ? MA_DR_WAV_WITH_METADATA : 0;
                memset(&io, 0, sizeof(io)); io.data = data; io.size = sizeof(data);
                CHECK(ma_dr_wav_init_ex(&wav, wav_read, wav_seek, tell ? wav_tell : NULL, NULL, &io, NULL, flags, NULL));
                CHECK(wav.totalPCMFrameCount == 6 && wav.dataChunkDataSize == 12);
                if (metadata) {
                    CHECK(wav.metadataCount == 1);
                    CHECK(wav.pMetadata[0].type == ma_dr_wav_metadata_type_list_info_title);
                }
                CHECK(check_wav_samples(&wav) == 0);
                CHECK(ma_dr_wav_seek_to_pcm_frame(&wav, 0));
                CHECK(check_wav_samples(&wav) == 0);
                ma_dr_wav_uninit(&wav);
            }
        }
    }
    make_wav(data, MA_FALSE);
    put32(data + 64, 1000); /* Declared data larger than the actual stream. */
    CHECK(ma_dr_wav_init_memory(&wav, data, sizeof(data), NULL));
    CHECK(wav.totalPCMFrameCount == 6); CHECK(check_wav_samples(&wav) == 0); ma_dr_wav_uninit(&wav);
    put32(data + 64, 12);
    memset(&io, 0, sizeof(io)); io.data = data; io.size = sizeof(data); io.negativeTell = MA_TRUE;
    CHECK(ma_dr_wav_init(&wav, wav_read, wav_seek, wav_tell, &io, NULL));
    CHECK(wav.totalPCMFrameCount == 6); CHECK(check_wav_samples(&wav) == 0); ma_dr_wav_uninit(&wav);
    memset(&io, 0, sizeof(io)); io.data = data; io.size = sizeof(data); io.failRestore = MA_TRUE;
    CHECK(!ma_dr_wav_init(&wav, wav_read, wav_seek, wav_tell, &io, NULL));
    /* Every truncated prefix before a complete data header must fail cleanly. */
    for (tell = 0; tell < 68; ++tell) {
        CHECK(!ma_dr_wav_init_memory(&wav, data, tell, NULL));
    }
    puts("PASS: sequential/unknown-length WAV, metadata, clipping, failed seek/tell and truncation");
    return 0;
}

static int test_flac(void)
{
    unsigned char bytes[5] = {0, 0x80, 0x12, 0x34, 0xFF};
    ma_uint32 be = ma_dr_flac__be2host_32_ptr_unaligned(bytes + 1);
    ma_uint32 le = ma_dr_flac__le2host_32_ptr_unaligned(bytes + 1);
    CHECK(be == 0x801234FFU && le == 0xFF341280U);
    memset(bytes, 0xFF, sizeof(bytes));
    CHECK(ma_dr_flac__be2host_32_ptr_unaligned(bytes + 1) == 0xFFFFFFFFU);
    CHECK(ma_dr_flac__le2host_32_ptr_unaligned(bytes + 1) == 0xFFFFFFFFU);
    puts("PASS: unaligned FLAC endian conversions with the high bit set (run with UBSan)");
    return 0;
}

static int test_s24(void)
{
    unsigned char packed[15];
    ma_int32 values[5] = {0, 0x12345600, 0x7FFFFF00, (-2147483647 - 1), -256};
    ma_int16 actual16[5]; ma_int32 actual32[5]; float actualFloat[5];
    unsigned int i, j;
    for (i = 0; i < 5; ++i) {
        ma_uint32 v = (ma_uint32)values[i];
        for (j = 0; j < 3; ++j) {
            unsigned int shift = ma_dr_wav__is_little_endian() ? (j + 1) * 8 : (3 - j) * 8;
            packed[i*3+j] = (unsigned char)(v >> shift);
        }
    }
    ma_dr_wav_s24_to_s16(actual16, packed, 5);
    ma_dr_wav_s24_to_s32(actual32, packed, 5);
    ma_dr_wav_s24_to_f32(actualFloat, packed, 5);
    for (i = 0; i < 5; ++i) {
        CHECK(actual16[i] == (ma_int16)(values[i] >> 16));
        CHECK(actual32[i] == values[i]);
        CHECK(fabs(actualFloat[i] - values[i] / 2147483648.0) < 0.0000001);
    }
    puts("PASS: native-endian 24-bit WAV conversion edge values"); return 0;
}

typedef struct { unsigned int ape; unsigned int id3; size_t apeSize; } tag_result;
static void on_meta(void* user, const ma_dr_mp3_metadata* metadata)
{
    tag_result* result = (tag_result*)user;
    if (metadata->type == MA_DR_MP3_METADATA_TYPE_APE) {
        result->ape += 1; result->apeSize = metadata->rawDataSize;
    } else if (metadata->type == MA_DR_MP3_METADATA_TYPE_ID3V1) {
        result->id3 += 1;
    }
}

static size_t append_ape(unsigned char* p, ma_bool32 withHeader)
{
    size_t offset = withHeader ? 32 : 0;
    memset(p, 0, offset + 50);
    /* APE item: value size, flags, zero-terminated key, value. */
    put32(p + offset, 4); memcpy(p + offset + 8, "Title\0test", 10);
    memcpy(p + offset + 18, "APETAGEX", 8); put32(p + offset + 26, 2000);
    put32(p + offset + 30, 50); put32(p + offset + 34, 1);
    if (withHeader) {
        p[offset + 41] = 0x80;
        memcpy(p, p + offset + 18, 32); p[23] |= 0x20;
    }
    return offset + 50;
}

static size_t append_id3(unsigned char* p)
{ memset(p, 0, 128); memcpy(p, "TAG", 3); return 128; }

typedef struct { wav_io io; tag_result tags; } mp3_io;
static ma_bool32 mp3_seek(void* user, int offset, ma_dr_mp3_seek_origin origin)
{
    ma_dr_wav_seek_origin wavOrigin = MA_DR_WAV_SEEK_SET;
    if (origin == MA_DR_MP3_SEEK_CUR) wavOrigin = MA_DR_WAV_SEEK_CUR;
    if (origin == MA_DR_MP3_SEEK_END) wavOrigin = MA_DR_WAV_SEEK_END;
    return wav_seek(user, offset, wavOrigin);
}
static void on_callback_meta(void* user, const ma_dr_mp3_metadata* metadata)
{ on_meta(&((mp3_io*)user)->tags, metadata); }

static int test_mp3_frames(const unsigned char* original, size_t originalSize)
{
    unsigned char frames[4096];
    float pcm[MA_DR_MP3_MAX_SAMPLES_PER_FRAME];
    unsigned int mode;
    int frameBytes;
    CHECK(originalSize > 23);
    frameBytes = ma_dr_mp3_hdr_frame_bytes(original + 19, 0) + ma_dr_mp3_hdr_padding(original + 19);
    CHECK(frameBytes > 0 && (size_t)frameBytes <= originalSize - 19 && frameBytes * 2 + 128 < (int)sizeof(frames));
    memcpy(frames, original + 19, (size_t)frameBytes);
    memcpy(frames + frameBytes, original + 19, (size_t)frameBytes);
    for (mode = 0; mode < 2; ++mode) {
        ma_dr_mp3dec decoder;
        ma_dr_mp3dec_frame_info info;
        int bytes = frameBytes * 2 + (mode ? 128 : 16);
        memset(frames + frameBytes * 2, 0, 128);
        if (mode) memcpy(frames + frameBytes * 2, "TAG", 3);
        ma_dr_mp3dec_init(&decoder);
        CHECK(ma_dr_mp3dec_decode_frame(&decoder, frames, bytes, pcm, &info) > 0);
        CHECK(info.frame_bytes == frameBytes);
    }
    puts("PASS: short MP3 streams ending in zero padding or an ID3v1 tag retain their first frame");
    return 0;
}

static int test_mp3(const char* path)
{
    unsigned char original[4096], tagged[8192];
    float expected[2048], actual[2048];
    ma_dr_mp3 mp3;
    tag_result tags;
    FILE* file = fopen(path, "rb");
    size_t originalSize, size;
    unsigned int mode;
    mp3_io callbackIO;
    CHECK(file != NULL); originalSize = fread(original, 1, sizeof(original), file); fclose(file);
    CHECK(originalSize > 0 && originalSize < sizeof(original));
    CHECK(test_mp3_frames(original, originalSize) == 0);
    CHECK(ma_dr_mp3_init_memory(&mp3, original, originalSize, NULL));
    CHECK(mp3.channels == 1);
    CHECK(ma_dr_mp3_read_pcm_frames_f32(&mp3, 2048, expected) == 2048); ma_dr_mp3_uninit(&mp3);
    /* Bare APE, APE with header, both ID3/APE orders, repeated tags, and no tags. */
    for (mode = 0; mode < 6; ++mode) {
        unsigned int expectedApe = mode == 4 ? 2 : (mode == 5 ? 0 : 1);
        unsigned int expectedID3 = mode >= 2 && mode <= 4 ? 1 : 0;
        memcpy(tagged, original, originalSize); size = originalSize;
        if (mode == 2) size += append_id3(tagged + size);
        if (mode != 5) size += append_ape(tagged + size, mode == 1);
        if (mode == 4) size += append_ape(tagged + size, MA_TRUE);
        if (mode == 3 || mode == 4) size += append_id3(tagged + size);
        memset(&tags, 0, sizeof(tags));
        CHECK(ma_dr_mp3_init_memory_with_metadata(&mp3, tagged, size, on_meta, &tags, NULL));
        CHECK(tags.ape == expectedApe && tags.id3 == expectedID3);
        if (expectedApe > 0) CHECK(tags.apeSize == (mode == 1 ? 82 : 50));
        CHECK(mp3.streamLength == originalSize);
        CHECK(ma_dr_mp3_read_pcm_frames_f32(&mp3, 2048, actual) == 2048);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        CHECK(ma_dr_mp3_seek_to_pcm_frame(&mp3, 0));
        CHECK(ma_dr_mp3_read_pcm_frames_f32(&mp3, 2048, actual) == 2048);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        ma_dr_mp3_uninit(&mp3);
        memset(&callbackIO, 0, sizeof(callbackIO)); callbackIO.io.data = tagged; callbackIO.io.size = size;
        CHECK(ma_dr_mp3_init(&mp3, wav_read, mp3_seek, wav_tell, on_callback_meta, &callbackIO, NULL));
        CHECK(mp3.streamLength == originalSize);
        CHECK(callbackIO.tags.ape == expectedApe && callbackIO.tags.id3 == expectedID3);
        CHECK(ma_dr_mp3_read_pcm_frames_f32(&mp3, 2048, actual) == 2048);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        CHECK(ma_dr_mp3_seek_to_pcm_frame(&mp3, 0));
        CHECK(ma_dr_mp3_read_pcm_frames_f32(&mp3, 2048, actual) == 2048);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        ma_dr_mp3_uninit(&mp3);
    }
    /* Invalid footer sizes must not trim audio or allocate a bogus tag buffer. */
    for (mode = 0; mode < 3; ++mode) {
        static const ma_uint32 badSizes[3] = {0, 31, 0xFFFFFFFFU};
        memcpy(tagged, original, originalSize); size = originalSize + append_ape(tagged + originalSize, MA_FALSE);
        put32(tagged + size - 20, badSizes[mode]);
        memset(&tags, 0, sizeof(tags));
        CHECK(ma_dr_mp3_init_memory_with_metadata(&mp3, tagged, size, on_meta, &tags, NULL));
        CHECK(tags.ape == 0 && mp3.streamLength == size); ma_dr_mp3_uninit(&mp3);
    }
    puts("PASS: APE footer/header lengths, mixed/repeated ID3 trailers, malformed sizes and PCM/seek equivalence");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) { puts("Usage: decoder_backports <xing-id3.mp3> [wav|flac|s24|mp3]"); return 2; }
    if ((argc < 3 || strcmp(argv[2], "wav") == 0) && test_wav()) return 1;
    if ((argc < 3 || strcmp(argv[2], "flac") == 0) && test_flac()) return 1;
    if ((argc < 3 || strcmp(argv[2], "s24") == 0) && test_s24()) return 1;
    if ((argc < 3 || strcmp(argv[2], "mp3") == 0) && test_mp3(argv[1])) return 1;
    return 0;
}
