# F1 05 PC: known issues shown in Discord #known-issues

How it works: every line starting with "- " is one issue (indented lines continue it). Anything else,
like these lines, is ignored. Add "(fixed-in: X.Y.Z)" at the end of an issue when a fix lands: until X.Y.Z is
released Discord shows "Fixed in X.Y.Z", after that the issue disappears. Write #channel-name for a channel.
English only, no em or en dashes. Discord is updated by the release workflow.

- With 20 cars at the start and in the pit lane the frame rate can dip briefly below 50.
- The first race after installing compiles the graphics for your card once; the cache avoids it next time.
- With the software renderer (`gs = cpu`) the pause menu sometimes does not appear (the picture stays on the last
  race frame). It does not happen with the GPU renderer, the default.
- A black screen when entering a race was reported with a profile from an older memory card (GitHub issue #2).
  Creating a new profile fixes it; if it still happens to you, attach `F1-05.log` to that issue.
- The `.exe` is not code-signed (SmartScreen warning).
- Changing the head in the player creation screen freezes the game; create the player without changing the
  head for now. (fixed-in: 1.0.2)
