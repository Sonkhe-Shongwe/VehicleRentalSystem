# Rental Studio R2 | opt-in Inter font rendering trial

Official open-licensed font source: Google Fonts, [Inter variable font](https://github.com/google/fonts/blob/38411257417e616c5d4daa1b9e91a3dbcebe1c2e/ofl/inter/Inter%5Bopsz%2Cwght%5D.ttf), mirrored from the original [rsms/inter](https://github.com/rsms/inter) project.

- Source repository: `google/fonts`, pinned commit `38411257417e616c5d4daa1b9e91a3dbcebe1c2e`
- Source filename: `ofl/inter/Inter[opsz,wght].ttf`
- Exact original Git blob SHA-1 (Git blob-header encoding): `047c92f6e2212473dc436020afed689527076d44`
- Expected original binary size: **876576 bytes**
- Licence: SIL Open Font License 1.1, full source in `INTER_OFL.txt`. No licence claimed for Windows Segoe UI (OS-provided only).
- Local opt-in source fetch/verify: `tools/fetch_r2_inter_trial.py` downloads the pinned binary during `BUILD_R2_INTER_FONT_TRIAL=ON` CMake configuration, checks exact bytes and Git blob SHA and embeds it into the built Qt executable's qrc resources.
- The font is **not fetched at application runtime** and the built Qt executable operates offline. Normal desktop builds default to `BUILD_R2_INTER_FONT_TRIAL=OFF` and retain Segoe UI / system sans fallback. Build-time opt-in requires network only when the pinned source is not already cached locally.
- Test and interactive opt-in: compiled trial executable `--r2-font-inter` selects registered Inter via QFontDatabase; `--r2-font-trial-smoke --r2-font-inter` is the dedicated real Qt screenshot proof. Neither is a default theme switch or final owner font approval.
