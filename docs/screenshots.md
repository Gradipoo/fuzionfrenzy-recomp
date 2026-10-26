# Screenshot regression

Local only. CI does not run it and does not have the pictures.

`tools/shots/scenes.txt` names the scenes. A present index is the fixed-step
replay frame to keep. A dash means that frame has not been measured yet, and
the harness skips it. The seven scenes are the title, the board, character
select, Twisted System's countdown, Twisted System in the round, Sumo Balls,
and Jetstream.

```
SHOT_PADS=det-logs/gameplay.pads tools/shots/run.sh capture
SHOT_PADS=det-logs/gameplay.pads tools/shots/run.sh compare
```

`capture` replays the pads file with fixed-step, seed 1, muted, minimized,
on `--instance 6`. The guest runs two presents past the index. The render
thread finishes the previous present while the guest records the next, and
the frame limit exits without waiting for the present still queued, so the
extra present is what flushes `front_NNNNN_host.bmp`. That file is copied to
`screen-ref/<scene>.bmp`. `compare` does the same into `screen-out/` and
scores it against the reference. A non-zero index sets the capture interval
to that index, so only present 0 and the scene present are read back.

The score is the mean absolute difference of R, G, and B (0–255). Alpha is
ignored. The default threshold is 4 (`SHOT_THRESHOLD`). A smaller picture, or
a score above the threshold, is `FAIL`. The report is `screen-out/report.txt`
and the same lines on stdout.

`screen-ref/` and `screen-out/` are gitignored, and
`tools/hooks/forbid-game-paths.sh` rejects them. Commit the scripts and
`tools/shots/scenes.txt` only.

`python3 tools/shots/compare.py --self-check` checks the scorer on synthetic
pixels. It does not need a replay. The presents in `tools/shots/scenes.txt`
were measured on 26 Oct 2026 (Pacific) from a paced fixed-step replay of
`det-logs/gameplay.pads` (seed 1): title 200, board 1200, character select
1400, Twisted System countdown 1800, Twisted System round 2200, Sumo Balls
3400, Jetstream 5000. The recording stays out of the tree.
