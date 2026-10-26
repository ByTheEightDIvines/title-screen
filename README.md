# Hero of Legend Title Screen

One combined Dusklight mod containing the ALttP-inspired logo introduction and the custom castle-moat title scene.

- Music and sword impact synchronized to the logo animation.
- Centered Triforce; sword renders behind the lettering.
- Native Twilight Princess title music returns five seconds after the custom track finishes.
- Fixed camera, one dense treeline, plain green bushes and the selected native snowy mountain backdrop.
- Hyrule Field daytime sky and title-only environment settings.
- Original animated water over a submerged blue depth layer, hiding the unwanted underwater terrain.
- Standing player start with the opening horse ride disabled; the custom camera activates before the title UI becomes ready.

The background changes are scoped to the dedicated `F_SP102` title map. Normal gameplay maps and saves are not modified.

## Install

Download `mod-combined` from the successful GitHub Actions run and install the enclosed `.dusk` in Dusklight's mod manager. Replace the previous Hero of Legend Title Screen package; do not enable two copies with the same mod ID.

## Build pipeline

The workflow uses the same eight-platform build and `symgen modmeta --check` combine procedure as the other ByTheEightDIvines Dusklight mods. It pins the SDK to Dusklight v2.0.3. Every platform includes the identical assets listed in `assets.sha256.json`.

Windows retains the waveOut audio backend used by the approved local build. Linux, macOS, iOS and Android use a dedicated miniaudio PCM output device. The callback's consumed-frame count drives animation timing; actual sound-device latency varies by platform. Audio is preloaded PCM16 stereo at 44.1 kHz. The OS/application volume controls this custom track; the in-game music slider does not control the separate PCM device.

The Windows scene and audio were approved in-game by the author. Other platform bundles require in-game testing; a successful compile is not a claim of visual or audio validation on those devices.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python tools/verify_release_assets.py build/mods/hero_of_legend_title.dusk
```

Use `-DDUSKLIGHT_DIR=/path/to/dusklight` to build against an existing SDK checkout. Windows builds require a Visual Studio developer environment. Apple and Android targets use the platform toolchains configured in `.github/workflows/build.yml`.

Push a branch to build platform artifacts and `mod-combined`. Push a version tag to attach the combined bundle to a GitHub release.

## Source notes

The checked-in overlays are the approved final assets, not temporary scouting renders. No ISO, emulator save, local configuration, build cache or reference video is included. Generation and exploratory tools remain in the original local workspace.

The portable audio backend uses miniaudio 0.11.23, bundled under `vendor/miniaudio` with its upstream license in the header. The pipeline fetch and merge helpers follow the author's existing mod pipeline.
