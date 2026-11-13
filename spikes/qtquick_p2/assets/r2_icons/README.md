# R2 local Lucide icon source and packaging inventory

**Embedded icon names (CMake order)** are listed below and must agree with `R2_ICON_NAMES` in `spikes/qtquick_p2/CMakeLists.txt`, the P8-F source register and the P8-G QA notice bundle.

Embedded icon names (CMake order): layout-grid car-front clipboard-list plus layers key-round database circle-check chevron-right x

**Pinned upstream source:** Official [Lucide repository](https://github.com/lucide-icons/lucide) at commit [`53fab88ff9d1b48f4704b5b5b560d6b4d560bf4c`](https://github.com/lucide-icons/lucide/commit/53fab88ff9d1b48f4704b5b5b560d6b4d560bf4c) under `icons/<name>.svg`. The original nine names were previously recorded in this source notice. The tenth name, **`x`**, has now been independently retrieved at [exact pinned upstream `icons/x.svg`](https://github.com/lucide-icons/lucide/blob/53fab88ff9d1b48f4704b5b5b560d6b4d560bf4c/icons/x.svg) and compared against all three locally bundled tone variants. The SVG path elements are identical: `<path d="M18 6 6 18" />` and `<path d="m6 6 12 12" />`. All three local versions retain the upstream 24x24 viewBox, stroke width 2, round caps/joins and only replace `currentColor` with the declared static tone. **This resolves the previous `x` source-identity gap, not a full upstream byte-for-byte audit of every other icon or final distribution clearance.**

The ten locally bundled names each have `active`, `inverse` and `emerald` SVG variants, for 30 files. Tone replacements: active `#0B4B3D`, inverse `#F7FAF9`, emerald `#0F766E`. The [source-pinned official licence](https://github.com/lucide-icons/lucide/blob/53fab88ff9d1b48f4704b5b5b560d6b4d560bf4c/LICENSE) is ISC; the bundled [LICENSE-LUCIDE.txt](LICENSE-LUCIDE.txt) retains ISC and the inherited Feather MIT notices.

The icons are bundled as offline Qt resources. The Rental Studio brand uses separate original artwork.
