# Third-party licences

KLAY (shown in hosts as **Klaymore** by MINDBEND AD) is published under the GNU Affero General Public
License v3.0 (see `LICENSE`), because it is built on JUCE 8 under AGPLv3.

Requirement C-006: every file, module or dependency under a GPL-family licence (GPL, LGPL, AGPL) carries an
SPDX header, the searchable marker comment `LICENSE-GPL`, and a row in the table below, so it can be found and
replaced for a closed commercial release. `tools/check-licenses.ps1` enforces this (it runs as a ctest and in CI):

- every file in the tree with a GPL-family `SPDX-License-Identifier` must contain `LICENSE-GPL` and match a glob in
  the **Files** column of a GPL-family row whose **Licence** cell names the licence(s) of its SPDX expression;
- every dependency fetched in CMake (`FetchContent_Declare(<name> …)`) must have a row whose **Component** cell
  equals that name (case-insensitive).

Rows are machine-read: keep one component per row, globs in backticks, separated by commas.

| Component | Licence | Version | Files | Replacement option |
| --- | --- | --- | --- | --- |
| JUCE | AGPL-3.0-only (or JUCE commercial licence) | 8.0.16-security | `build/_deps/juce-src/**` (fetched, not in the repo) | Buy a JUCE commercial licence; no code change needed |
| KLAY own source | AGPL-3.0-only | — | `core/**`, `adapters/**`, `plugin/**`, `ui/**`, `tools/**`, `tests/**`, `cmake/**`, `.github/**`, `CMakeLists.txt` | Owner relicenses own code; JUCE-dependent layers (`plugin/`, `ui/`, `adapters/`) need a JUCE commercial licence (C-007) |
| vst3sdk | MIT (VST3 SDK, since 3.8) | v3.8.0_build_66 | `build/vst3-validator/_deps/vst3sdk-src/**` (fetched by `tools/vst3-validator`; validator only, not linked into the plugin) | Not GPL; keep |
| signalsmith-stretch | MIT | 1.4.0 | fetched from M1 on | Not GPL; keep (attribution notice required) |
| Catch2 | BSL-1.0 | v3.16.1 | `build/_deps/catch2-src/**` (fetched; tests only) | Not GPL; keep |
| pluginval | GPL-3.0 | v1.0.4 | `tools/bin/pluginval/**` (downloaded binary, tooling only, never shipped or linked) | Tooling only; nothing to replace |

## Notices

- **Signalsmith Stretch** — Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd., MIT licence. Its notice is
  shipped with the plugin once it is linked (M1).
- **VST** is a trademark of Steinberg Media Technologies GmbH.
