#include "eye_scenes.h"







const ANIMATION_PROFILE_T kAnimStill = {
    {false, 4200, 140, 8},
    {false, 0, 0, 0, 0, 0, 0, 0, 0},
    {false, 0, 2400},
};

const ANIMATION_PROFILE_T kAnimIdle = {
    {true, 4200, 140, 8},
    {true, 2, 2, 3, 2, 950, 1200, 2600, 3100},
    {false, 0, 2400},
};

const ANIMATION_PROFILE_T kAnimCalm = {
    {true, 4600, 130, 8},
    {true, 1, 1, 2, 1, 1200, 1500, 3200, 3600},
    {false, 0, 2800},
};

const ANIMATION_PROFILE_T kAnimFocused = {
    {true, 5200, 120, 8},
    {true, 1, 1, 1, 1, 1400, 1700, 3600, 4000},
    {false, 0, 3000},
};

const ANIMATION_PROFILE_T kAnimExcited = {
    {true, 3000, 120, 10},
    {true, 3, 2, 4, 2, 820, 980, 2000, 2500},
    {false, 0, 1800},
};

const ANIMATION_PROFILE_T kAnimWow = {
    {false, 3200, 90, 8},
    {false, 0, 0, 0, 0, 0, 0, 0, 0},
    {true, 8, 1500},
};

const ANIMATION_PROFILE_T kAnimSleepy = {
    {true, 6200, 200, 6},
    {true, 1, 1, 1, 1, 1800, 2200, 4200, 4700},
    {false, 0, 3200},
};

const ANIMATION_PROFILE_T kAnimSleepyStill = {
    {false, 6200, 200, 6},
    {true, 1, 1, 1, 1, 2100, 2500, 4800, 5200},
    {false, 0, 3400},
};

const ANIMATION_PROFILE_T kAnimDizzy = {
    {true, 5200, 120, 8},
    {true, 4, 3, 5, 3, 700, 850, 1200, 1450},
    {false, 0, 1700},
};

const ANIMATION_PROFILE_T kAnimShake = {
    {false, 3600, 140, 8},
    {true, 6, 4, 7, 4, 260, 340, 420, 500},
    {false, 0, 1200},
};

const ANIMATION_PROFILE_T kAnimAlert = {
    {true, 3600, 110, 8},
    {true, 2, 1, 3, 1, 900, 1200, 2000, 2600},
    {false, 0, 2200},
};

const RENDER_OP_T kEveAngryHardOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "Cut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -45}, {107, 38}, 3, 31, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    {true, "Brow",  LAYER_OP_DRAW_BROW, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {8, -54}, {70, 11}, 3, 35, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveAngryHardScene = {
    "eve_angry_hard",
    {360, 360, 68, 180},
    0x06080d,
    kAnimExcited,
    kEveAngryHardOps,
    sizeof(kEveAngryHardOps) / sizeof(kEveAngryHardOps[0]),
};

const RENDER_OP_T kEveAngrySoftOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "Cut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -45}, {84, 29}, 3, 8, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveAngrySoftScene = {
    "eve_angry_soft",
    {360, 360, 68, 180},
    0x06080d,
    kAnimAlert,
    kEveAngrySoftOps,
    sizeof(kEveAngrySoftOps) / sizeof(kEveAngrySoftOps[0]),
};

const RENDER_OP_T kEveBaseOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveBaseScene = {
    "eve_base",
    {360, 360, 68, 180},
    0x06080d,
    kAnimStill,
    kEveBaseOps,
    sizeof(kEveBaseOps) / sizeof(kEveBaseOps[0]),
};

// Heavy flat upper cut + slight lower cut → half-closed, expressionless.
// Visually between IDLE and SLEEPY — bored, not tired.
const RENDER_OP_T kEveBoredOps[] = {
    {true, "Glow",      LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE,    SIDE_BOTH, true, true, {0,  0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid",       LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE,    SIDE_BOTH, true, true, {0,  0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core",      LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE,    SIDE_BOTH, true, true, {0,  0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, LV_OPA_COVER},
    {true, "CutTop",    LAYER_OP_DRAW_CUT,  PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -28}, {104, 56}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    {true, "CutBottom", LAYER_OP_DRAW_CUT,  PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0,  40}, {104, 30}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    // Pupil sits in the visible slit; rolls upward behind CutTop for eye-roll effect.
    {true, "Pupil",     LAYER_OP_DRAW_CUT,  PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0,  10}, { 26, 32}, 13, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, (lv_opa_t)120, true},
};

const EYE_SCENE_SPEC_T kEveBoredScene = {
    "eve_bored",
    {360, 360, 68, 180},
    0x06080d,
    kAnimCalm,
    kEveBoredOps,
    sizeof(kEveBoredOps) / sizeof(kEveBoredOps[0]),
};

const RENDER_OP_T kEveCallOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {82, 82}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)38},
    {true, "Ring", LAYER_OP_DRAW_SHAPE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 74}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "InnerCut", LAYER_OP_DRAW_CUT, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {48, 48}, 999, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveCallScene = {
    "eve_call",
    {360, 360, 68, 180},
    0x06080d,
    kAnimAlert,
    kEveCallOps,
    sizeof(kEveCallOps) / sizeof(kEveCallOps[0]),
};







const RENDER_OP_T kEveConfusedOps[] = {
    {true, "LeftGlow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_LEFT, false, false, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "LeftMid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_LEFT, false, false, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "LeftCore", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_LEFT, false, false, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "RightGlow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_RIGHT, false, false, {0, 6}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "RightMid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_RIGHT, false, false, {0, 6}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "RightCore", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_RIGHT, false, false, {0, 6}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "RightTopCut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_RIGHT, false, false, {0, -36}, {88, 24}, 4, -8, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveConfusedScene = {
    "eve_confused",
    {360, 360, 68, 180},
    0x06080d,
    kAnimFocused,
    kEveConfusedOps,
    sizeof(kEveConfusedOps) / sizeof(kEveConfusedOps[0]),
};

// X-eyes: two crossing DrawCut bars at ±42° carve a dark X through each iris.
// Leaves four bright lobes (top/bottom/left/right) → classic cartoon KO/dizzy look.
// mirror_angle_for_right=true ensures right eye gets the same X (angles flip but cross stays).
const RENDER_OP_T kEveDizzyOps[] = {
    {true, "Glow",  LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999,  0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true},  (lv_opa_t)38},
    {true, "Mid",   LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999,  0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true},  (lv_opa_t)100},
    {true, "Core",  LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999,  0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true},  LV_OPA_COVER},
    {true, "CutX1", LAYER_OP_DRAW_CUT,  PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, 0}, {120, 18},   9, 42, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    {true, "CutX2", LAYER_OP_DRAW_CUT,  PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, 0}, {120, 18},   9, -42, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveDizzyScene = {
    "eve_dizzy",
    {360, 360, 68, 180},
    0x06080d,
    kAnimDizzy,
    kEveDizzyOps,
    sizeof(kEveDizzyOps) / sizeof(kEveDizzyOps[0]),
};

const RENDER_OP_T kEveExcitedOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)44},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)114},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "ExciteCut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, 28}, {92, 20}, 8, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveExcitedScene = {
    "eve_excited",
    {360, 360, 68, 180},
    0x06080d,
    kAnimExcited,
    kEveExcitedOps,
    sizeof(kEveExcitedOps) / sizeof(kEveExcitedOps[0]),
};

// Flat upper cut (angle 0) → no angry tilt, just a narrowed intense stare.
// Pupil layer (DrawCut ellipse) creates dark centre on glowing iris → piercing focus effect.
const RENDER_OP_T kEveFocusedOps[] = {
    {true, "Glow",  LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid",   LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core",  LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE,     SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, LV_OPA_COVER},
    {true, "Cut",   LAYER_OP_DRAW_CUT,  PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -40}, {90, 26}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    // mirror_x_for_right=false so both pupils scan in the same direction (left→right reading sweep).
    {true, "Pupil", LAYER_OP_DRAW_CUT,  PRIMITIVE_ELLIPSE,     SIDE_BOTH, false, false, {0,   4}, {28, 34}, 14, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, (lv_opa_t)120, true},
};

const EYE_SCENE_SPEC_T kEveFocusedScene = {
    "eve_focused",
    {360, 360, 68, 180},
    0x06080d,
    kAnimFocused,
    kEveFocusedOps,
    sizeof(kEveFocusedOps) / sizeof(kEveFocusedOps[0]),
};







const RENDER_OP_T kEveGlitchOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0xff0040, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0xff0040, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0xff0040, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveGlitchScene = {
    "eve_glitch",
    {360, 360, 68, 180},
    0x06080d,
    kAnimStill,
    kEveGlitchOps,
    sizeof(kEveGlitchOps) / sizeof(kEveGlitchOps[0]),
};







const RENDER_OP_T kEveHappyOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "Cut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, 36}, {101, 47}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveHappyScene = {
    "eve_happy",
    {360, 360, 68, 180},
    0x06080d,
    kAnimIdle,
    kEveHappyOps,
    sizeof(kEveHappyOps) / sizeof(kEveHappyOps[0]),
};

// Same eye shape as HAPPY — tongue overlay is rendered as a separate LVGL widget
// in DeskRoboMVP_nextgen.cpp::update_tongue_overlay().
const EYE_SCENE_SPEC_T kEveHappyTongueScene = {
    "eve_happy_tongue",
    {360, 360, 68, 180},
    0x06080d,
    kAnimIdle,
    kEveHappyOps,
    sizeof(kEveHappyOps) / sizeof(kEveHappyOps[0]),
};

// HOLLOW: DrawCut ellipse removes the iris interior → void/dark core.
// Remaining Glow + Mid rings form a glowing ring around empty eyes.
const RENDER_OP_T kEveSunglassesRoundOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid",  LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, LV_OPA_COVER},
    // Hollow cut: ~10 px glowing ring remains visible around the void.
    {true, "Void", LAYER_OP_DRAW_CUT,  PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {54,  88},  27, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveSunglassesRoundScene = {
    "eve_hollow",
    {360, 360, 68, 180},
    0x06080d,
    kAnimIdle,
    kEveSunglassesRoundOps,
    sizeof(kEveSunglassesRoundOps) / sizeof(kEveSunglassesRoundOps[0]),
};

const RENDER_OP_T kEveIdleOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveIdleScene = {
    "eve_idle",
    {360, 360, 68, 180},
    0x06080d,
    kAnimIdle,
    kEveIdleOps,
    sizeof(kEveIdleOps) / sizeof(kEveIdleOps[0]),
};







const RENDER_OP_T kEveSadOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "Cut", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -45}, {107, 38}, 3, -31, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveSadScene = {
    "eve_sad",
    {360, 360, 68, 180},
    0x06080d,
    kAnimCalm,
    kEveSadOps,
    sizeof(kEveSadOps) / sizeof(kEveSadOps[0]),
};

// Pupil jitter is driven by state.pupil_x/y — set per-frame in DeskRoboMVP_nextgen for SHAKE.
const RENDER_OP_T kEveShakeOps[] = {
    {true, "Glow",  LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid",   LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core",  LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE,      0x0fdaff, true}, LV_OPA_COVER},
    {true, "Pupil", LAYER_OP_DRAW_CUT,  PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {28, 34},  14,  0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, (lv_opa_t)120, true},
};

const EYE_SCENE_SPEC_T kEveShakeScene = {
    "eve_shake",
    {360, 360, 68, 180},
    0x06080d,
    kAnimShake,
    kEveShakeOps,
    sizeof(kEveShakeOps) / sizeof(kEveShakeOps[0]),
};







const RENDER_OP_T kEveSleepyOps[] = {
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 113}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "CutBottom", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, 36}, {101, 47}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
    {true, "CutTop", LAYER_OP_DRAW_CUT, PRIMITIVE_ROUNDED_RECT, SIDE_BOTH, true, true, {0, -27}, {104, 70}, 3, 0, {COLOR_MODE_SCENE_BACKGROUND, 0x06080d, false}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveSleepyScene = {
    "eve_sleepy",
    {360, 360, 68, 180},
    0x06080d,
    kAnimSleepyStill,
    kEveSleepyOps,
    sizeof(kEveSleepyOps) / sizeof(kEveSleepyOps[0]),
};

const RENDER_OP_T kEveWinkOps[] = {
    // Both eyes — base eye shape. Right eye opacity is animated by the wink system.
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, false, false, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid",  LAYER_OP_DRAW_MID,  PRIMITIVE_ELLIPSE, SIDE_BOTH, false, false, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, false, false, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveWinkScene = {
    "eve_wink",
    {360, 360, 68, 180},
    0x06080d,
    kAnimIdle,
    kEveWinkOps,
    sizeof(kEveWinkOps) / sizeof(kEveWinkOps[0]),
};







const RENDER_OP_T kEveWowOps[] = {
    {true, "Glow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {74, 114}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)42},
    {true, "Mid", LAYER_OP_DRAW_MID, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {68, 108}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)110},
    {true, "Core", LAYER_OP_DRAW_CORE, PRIMITIVE_ELLIPSE, SIDE_BOTH, true, true, {0, 0}, {62, 104}, 999, 0, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveWowScene = {
    "eve_wow",
    {360, 360, 68, 180},
    0x06080d,
    kAnimWow,
    kEveWowOps,
    sizeof(kEveWowOps) / sizeof(kEveWowOps[0]),
};







const RENDER_OP_T kEveXXOps[] = {
    {true, "LeftGlow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ROUNDED_RECT, SIDE_LEFT, false, false, {0, 0}, {86, 86}, 12, 35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)32},
    {true, "LeftBarA", LAYER_OP_DRAW_SHAPE, PRIMITIVE_ROUNDED_RECT, SIDE_LEFT, false, false, {0, 0}, {74, 14}, 7, 35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "LeftBarB", LAYER_OP_DRAW_SHAPE, PRIMITIVE_ROUNDED_RECT, SIDE_LEFT, false, false, {0, 0}, {74, 14}, 7, -35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "RightGlow", LAYER_OP_DRAW_GLOW, PRIMITIVE_ROUNDED_RECT, SIDE_RIGHT, false, false, {0, 0}, {86, 86}, 12, -35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, (lv_opa_t)32},
    {true, "RightBarA", LAYER_OP_DRAW_SHAPE, PRIMITIVE_ROUNDED_RECT, SIDE_RIGHT, false, false, {0, 0}, {74, 14}, 7, 35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
    {true, "RightBarB", LAYER_OP_DRAW_SHAPE, PRIMITIVE_ROUNDED_RECT, SIDE_RIGHT, false, false, {0, 0}, {74, 14}, 7, -35, {COLOR_MODE_RUNTIME_EYE, 0x0fdaff, true}, LV_OPA_COVER},
};

const EYE_SCENE_SPEC_T kEveXXScene = {
    "eve_xx",
    {360, 360, 68, 180},
    0x06080d,
    kAnimStill,
    kEveXXOps,
    sizeof(kEveXXOps) / sizeof(kEveXXOps[0]),
};

