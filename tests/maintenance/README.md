# Stable-branch backport regression tests

These tests require no audio hardware. The engine start failure test explicitly
uses the null backend. The other engine tests render offline; resource tests
use an in-memory WAV and a counting VFS.

```sh
cmake -S . -B build -DMINIAUDIO_BUILD_TESTS=ON -DMINIAUDIO_WARNINGS_AS_ERRORS=ON
cmake --build build --target miniaudio_maintenance
ctest --test-dir build -R miniaudio_maintenance --output-on-failure
```

Coverage:

- `ce8acb36`: explicit-format custom decoder initialization through callbacks
  and narrow/wide VFS; unsuccessful narrow/wide opens balance every close.
- `a99679e4`: fail each of six allocations in offline engine initialization;
  count outstanding allocations after cleanup. Also inject a null-backend start
  failure after the engine creates its own resource manager.
- `e47d7600`: seek from positive to negative PCM while converted input remains
  cached; verify output after the resampler reset and preserve node time when
  the data source rejects a seek.
- `f48b4a37`: deterministically unregister a file during a page's VFS read, then
  verify that no page job follows the free job. Also run 500 asynchronous
  init/uninit cycles with four workers, alternating WAIT_INIT and known/unknown
  lengths. The controlled interleaving uses a manually pumped job queue.
- `6ec7f057`: a stream releases its init fence and never acquires its done fence.
- `145ea6e3`: malformed WAV LIST lengths cannot consume the following chunk;
  FLAC binary-search seeks match sequential decoding; MP3 decoding and rewind
  match with and without an ID3 prefix before the Xing frame. FLAC/MP3 checks
  exercise normal decoding/seek behavior, not every malformed-input edge case.

The WAV, decoder cleanup, engine allocation cleanup, cached seek, cancellation,
and stream fence checks were also run against the pre-backport `cfb92b4c`
header; all six groups failed as expected. This establishes regression coverage
rather than only successful execution on the patched code.

## Platform review

The following changes were reviewed in source on macOS; Windows and Android
runtime behavior still requires the respective platforms.

- `0822f150`: GetMixFormat memory is freed after values are copied into device
  info. Property variants are initialized and cleared on both successful format
  paths and failed property reads. The property blob remains owned by its
  variant, avoiding a separate free of that alias.
- `acde317a`: reroute errors no longer clear detached flags or trigger restart.
  The default-device callback unlocks before returning on error. Start validates
  both sides of a duplex device before starting either; stop tolerates missing
  audio clients; reads/writes return an error and zero frames for missing service
  clients. Playback, capture and loopback branches were checked against the
  reinitialization/release paths. This does not establish that every pre-existing
  Windows routing race is fixed.
- `ed3c4775`: initial AAudio setup stores the requested performance profile;
  each reroute attempt copies it into the replacement configuration before
  opening streams. This preserves the profile used for performance mode and
  buffer sizing, including retries. The device layout changes when AAudio is
  enabled, so rebuild the library and consumers together.

## Fixture provenance

`data/xing-id3.mp3` is a generated 0.25-second, 440 Hz mono sine wave, not a
third-party recording. Generate its MPEG/Xing payload with:

```sh
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=44100:duration=0.25' \
  -c:a libmp3lame -q:a 5 -write_xing 1 -id3v2_version 0 -write_id3v1 0 payload.mp3
```

The fixture prefixes that payload with a 19-byte ID3v2.4 tag (a ten-byte header
and nine zero padding bytes). FFmpeg is not needed to run the tests.

## Decoder backports reviewed on 2026-11-20

`decoder_backports.c` covers the selected correctness fixes from upstream
`65c1fd32a5b89aa821eb4c50af189deacb5b7062`. This is a partial backport: the
Lyrics3v2 metadata feature, stereo header-test hoisting, and SSE output-store
optimization are excluded. The existing OpenSL stop fix remains intact.

```sh
cmake --build build --target miniaudio_decoder_backports miniaudio_maintenance
ctest --test-dir build -R 'decoder_backports|maintenance' --output-on-failure
```

The new test covers:

- WAV initialization with forward-only callbacks in sequential mode, including
  an unknown data size. Initialization must leave the PCM unread and must not
  seek backward or query the stream length.
- Known and unknown WAV sizes, with and without tell callbacks and metadata;
  clipping an oversized data declaration to actual bytes, rewind/read equivalence,
  negative tell results, failed restoration seeks, and truncated header prefixes.
- FLAC unaligned endian loads with the high bit set, under UBSan with recovery
  disabled so a signed-shift regression terminates the test.
- Native-endian 24-bit WAV conversion of zero, positive, negative and extreme
  values to s16, s32 and f32.
- MP3 short streams ending in zero padding or ID3v1, APE tags with/without
  headers, ID3/APE trailer orders and repeated trailers, invalid APE sizes,
  and equivalent PCM/rewind behavior through memory and read/seek/tell callbacks.

The forward-only WAV, FLAC shift, short MP3 trailer and APE-length regressions
were independently reproduced against pre-backport `756e2eff`. The APE check was
also run with the earlier short-stream check bypassed, to establish both failures.

Validation on macOS arm64: the ordinary suite passed all eight tests; five
relevant tests passed with ASan/UBSan; the new test passed C89 and C++11 builds
with warnings as errors. Syntax checks also passed with individual WAV/FLAC/MP3
backends, decoding, engine, resource manager, device I/O, or threading disabled
(threading disabled together with device I/O).

ARM64EC SIMD selection and big-endian WAV/W64 handling were reviewed in source;
they were not compiled or executed on those platforms. The native-endian test
above only exercises the actual host's byte order. No claim of exhaustive MP3
intensity-stereo malformed-input coverage is made.
