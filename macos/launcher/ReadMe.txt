Ring Host for Mac

This disk image is a launcher. It plays a legally owned copy of the original
Xbox game Fuzion Frenzy (USA). It does not contain that game, a recompiled
binary, or any art from the disc.

On first launch, choose your own USA disc image (.iso) or a folder that
already holds default.xbe plus the rest of the extracted disc. The launcher
checks that it is the release this port supports, then compiles the game on
your Mac. The built game stays in:

  ~/Library/Application Support/RingHost

An older build under Application Support/FuzionFrenzy is left where it is.
Point Ring Host at the same disc folder and it compiles again.

You need an Apple Silicon Mac, Xcode Command Line Tools, and git, cmake,
ninja, python3, MoltenVK, and glslang. Homebrew is the straightforward way
to get the libraries:

  xcode-select --install
  brew install cmake ninja glslang molten-vk vulkan-loader vulkan-headers

The first compile also downloads SDL and needs a network connection. Later
launches reuse the build until this app's version changes.

The app is ad-hoc signed. Notarization is future work, so a copy that
arrived from the internet may need a right-click Open the first time.
