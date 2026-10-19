# KLAY

KLAY is a layer-based kick drum instrument: up to six sample layers are stacked, trimmed, pitched, stretched,
shaped with volume envelopes and run through an FX rack, then played in key from MIDI. In hosts it shows up as
**Klaymore** by **MINDBEND AD** (VST3 instrument, Windows 10+ x64).

KLAY is the project's working title. Requirements: [`docs/spec.md`](docs/spec.md); build plan:
[`docs/plan.md`](docs/plan.md).

## Build

Requirements: Windows 10+ x64, Visual Studio 2026 (MSVC, "Desktop development with C++"), CMake 3.25+, Git.
Dependencies (JUCE 8, Catch2 v3, later Signalsmith Stretch) are fetched and pinned by CMake.

```powershell
cmake --preset vs2026
cmake --build --preset release
ctest --preset release
```

The plugin bundle lands in `build/plugin/KLAY_artefacts/Release/VST3/Klaymore.vst3`.

Plugin validation (pluginval strictness 8 and the Steinberg VST3 validator; both are downloaded or built on the
first run):

```powershell
./tools/validate.ps1
```

License check (C-006, also runs as part of `ctest`):

```powershell
./tools/check-licenses.ps1
```

## Layout

| Folder | Contents |
| --- | --- |
| `core/` | JUCE-free model, render, DSP, library and randomizer code (`model`, `render`, `dsp`, `library`, `random`) |
| `adapters/` | JUCE implementations of the core's interfaces (resampler, oversampling, file I/O) |
| `plugin/` | AudioProcessor: MIDI, voice, buffer swap, parameters, host tempo |
| `ui/` | Editor |
| `tools/` | `render-cli`, validation and license scripts |
| `tests/` | Catch2 tests, including the real-time safety harness |
| `docs/` | Spec, plan, wireframes and manual check lists (`docs/checks/`) |

## Licence

GNU Affero General Public License v3.0 only (`LICENSE`), because KLAY uses JUCE under AGPLv3. Third-party
components and the GPL labelling rules (C-006) are listed in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).
