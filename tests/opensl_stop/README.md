# Android OpenSL stop/restart regression

Build `opensl_stop.c` using an Android NDK compiler with OpenSL enabled, link
`OpenSLES`, `log`, `android` and `m`, and run on a real test device:

```sh
./opensl_stop normal /path/to/test.wav
./opensl_stop empty-queue /path/to/test.wav
./opensl_stop paused-player /path/to/test.wav
./opensl_stop stopped-player /path/to/test.wav
```

Use a short generated WAV. Each mode runs in a separate process and should have
an external timeout (8 seconds is used during regression). The sample loops so
that the engine clock provides evidence of actual data callbacks after restart;
an empty graph does not advance this clock.

The last three modes deliberately disrupt the underlying OpenSL queue/player
without updating miniaudio's state. This is fault injection, not a reproduction
of a specific application's production incident. The paused/stopped player
modes retain queued buffers and reproduced an unbounded stop on the prior
header; the bounded drain must return, clear the queue, and restart callbacks.
No game, microphone, networking or graphical application is required.
