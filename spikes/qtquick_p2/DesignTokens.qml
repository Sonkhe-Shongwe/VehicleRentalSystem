import QtQuick

// Owner-approved Navy/Emerald/Cloud palette. Source: design/r1/OWNER_THEME_DIRECTION.
// No guessed typography licensing, motion preference or WCAG signoff.
QtObject {
    readonly property color navy: "#17324D"
    readonly property color teal: "#0F766E"
    readonly property color canvas: "#F7F8FA"
    readonly property color surface: "#FFFFFF"
    readonly property color ice: "#EAF0F4"
    readonly property color mint: "#E8F5F0"
    readonly property color ink: "#5F6F82"
    readonly property color border: "#D8E2E8"
    readonly property color rented: "#1D4ED8"
    // Precision Automotive Light: semantic shell roles derived from the
    // selected Navy/Emerald tokens. Keep these consistent across workspaces.
    readonly property color sidebarSelected: "#D7F0E5"
    readonly property color quietSurface: "#F1F5F8"
    readonly property color pageHeading: "#14202A"
    readonly property color secondaryText: "#5F6F78"
    // Editorial Contrast R2: dark shell around a LIGHT desktop workspace.
    readonly property color shell: "#151B1B"
    readonly property color shellText: "#F7FAF9"
    readonly property color shellMuted: "#B9C9C5"
    readonly property color shellBorder: "#2C3B39"
    readonly property color shellHover: "#263734"
    readonly property color shellSelected: "#D7F0E5"
    readonly property color shellSelectedInk: "#0B4B3D"
    readonly property color shellSelectedBorder: "#9DD4BF"
    readonly property color shellFocus: "#78E6CF"
    readonly property color shellAction: "#087A5C"
    // M2: white command text needs >=4.5:1 contrast on hover.
    // #07805F maintains restrained emerald feedback (about 4.93:1).
    readonly property color shellActionHover: "#07805F"
    // M2 shared feedback: no geometric movement, no change to focus borders.
    readonly property color shellActionPressed: "#06674E"
    readonly property color shellNavPressed: "#355048"
    readonly property color shellSelectedPressed: "#BFE7D6"
    readonly property int hoverFeedbackMs: 100
    readonly property int pressFeedbackMs: 70
    readonly property color shellFooterWave: "#0B3E33"
    // The explicit R2 typography trial uses the C++-registered Inter family.
    // Normal builds keep Windows Segoe UI with system sans fallback unchanged.
    readonly property string shellFontFamily:
        r2FontTrialFamily !== "" ? r2FontTrialFamily : "Segoe UI"
    readonly property int sidebarExpandedWidth: 218
    readonly property int sidebarCollapsedWidth: 76
    readonly property int pageHeadingSizeWide: 32
    readonly property int pageHeadingSizeCompact: 28
    readonly property int pageSubtitleSize: 15
    readonly property int sidebarActionHeight: 48
    readonly property int componentRadius: 10
    // Owner-approved UX-CTRL-002 semantic states, not a new competing theme.
    readonly property color primaryHover: "#0C625C"
    readonly property color warningSurface: "#FFF2D5"
    readonly property color warningInk: "#965A0A"
    readonly property color danger: "#B42318"
    readonly property color dangerHover: "#912018"
    readonly property color dangerPressed: "#771B16"
    readonly property color dangerSoft: "#FEECEB"
    readonly property color neutralHover: "#DCE8EF"
    readonly property color disabledSurface: "#E6EAEE"
    readonly property color disabledInk: "#6B7580"
    readonly property int focusWidth: 3
    readonly property int selectedWidth: 2
    readonly property int minimumTouchTarget: 44
}
