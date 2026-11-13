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
