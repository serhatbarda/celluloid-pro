// Reafilm Pro - OFX parameter registry (see Params.h). Hints are written for colourists.
#include "Params.h"
#include "Presets.h"
#include "Platform.h"
#include "../core/Core.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cel { namespace ofxp {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int indexOf(const std::vector<std::string>& v, const std::string& name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (lower(v[i]) == lower(name)) return int(i);
    return -1;
}

// real film only (Params.h): display order of the data-sheet stocks and the nearest real stock of every designed one
const char* const kRealNegOrder[] = {
    "Kodak Vision3 50D 5203", "Kodak Vision3 250D 5207", "Kodak Vision3 200T 5213", "Kodak Vision3 500T 5219",
    "Kodak Vision2 500T 5218", "Kodak Vision2 Expression 500T 5229", "Kodak Portra 400", "Kodak Ektachrome 100D 5294",
    "Kodak Ektachrome E100", "Kodachrome 64 Style", "Kodak Double-X 5222", "Fujifilm Eterna Vivid 160 8543",
    "Fujifilm Eterna 250D 8563", "Fujifilm Eterna 500 8573", "Fujifilm Reala 500D 8592"};
const char* const kNegNearest[][2] = {
    {"CN 50D Fine Daylight", "Kodak Vision3 50D 5203"}, {"CN 200T Studio Tungsten", "Kodak Vision3 200T 5213"},
    {"CN 250D Soft Daylight", "Fujifilm Eterna 250D 8563"}, {"CN 500T High Speed", "Kodak Vision3 500T 5219"},
    {"CN 800T No-Backing", "Kodak Vision3 500T 5219"}, {"CN Vintage 85", "Kodak Vision2 500T 5218"},
    {"C41 Portrait 400", "Kodak Portra 400"}, {"C41 Consumer 200", "Kodak Portra 400"},
    {"RV 100 Slide", "Kodak Ektachrome E100"}, {"BW 400 Panchromatic", "Kodak Double-X 5222"},
    {"BW 100 Fine", "Kodak Double-X 5222"}};
const char* const kRealPrintOrder[] = {"Kodak Vision 2383", "Kodak Vision Premier 2393", "Fujifilm Eterna-CP 3521XD",
                                       "Fujifilm Eterna-CP 3513DI", "Kodak Endura Premier (paper)",
                                       "Kodak Portra Endura (paper)", "Kodak Supra Endura (paper)",
                                       "Fujifilm Crystal Archive (paper)"};
const char* const kPrintNearest[][2] = {
    {"P1 Standard Projection", "Kodak Vision 2383"}, {"P2 Low Contrast", "Fujifilm Eterna-CP 3513DI"},
    {"P3 Rich Contrast", "Kodak Vision Premier 2393"}, {"P4 Faded Archive", "Kodak Vision 2383"},
    {"P5 Silver Retention", "Kodak Vision 2383"}, {"P6 Modern Print", "Kodak Vision 2383"},
    {"Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)", "Fujifilm Eterna-CP 3513DI"}};

std::vector<std::string> negStockNames() {
    std::vector<std::string> v;
    for (const auto& n : core::negativeStocks()) v.push_back(n.name);
    return v;
}
template <size_t N, size_t M>
std::vector<int> buildRealMap(const std::vector<std::string>& all, const char* const (&order)[N], const char* const (&nearest)[M][2],
                              std::vector<int>* list) {
    for (const char* n : order) {
        const int k = indexOf(all, n);
        if (k >= 0) list->push_back(k);
    }
    std::vector<int> map(all.size(), 0);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string target = all[i];
        for (const auto& pr : nearest)
            if (all[i] == pr[0]) target = pr[1];
        const int k = indexOf(all, target);
        for (size_t o = 0; o < list->size(); ++o)
            if ((*list)[o] == k) map[i] = int(o);
    }
    return map;
}
struct RealStocks {
    std::vector<int> neg, print, negMap, printMap;
    RealStocks() {
        negMap = buildRealMap(negStockNames(), kRealNegOrder, kNegNearest, &neg);
        printMap = buildRealMap(core::printStockNames(), kRealPrintOrder, kPrintNearest, &print);
    }
};
const RealStocks& realStocks() {
    static const RealStocks r;
    return r;
}

struct Builder {
    std::vector<ParamDef> defs;
    std::string group;

    ParamDef& add(ParamDef d) {
        if (d.type != PType::Group) d.group = group;
        defs.push_back(std::move(d));
        return defs.back();
    }
    void beginGroup(const char* id, const char* label, bool open, const char* hint) {
        group.clear();
        ParamDef d;
        d.id = id; d.type = PType::Group; d.label = label; d.groupOpen = open; d.hint = hint;
        add(d);
        group = id;
    }
    ParamDef& dbl(const char* id, const char* label, double def, double mn, double mx, double inc, int digits,
                  const char* hint, bool look) {
        ParamDef d;
        d.id = id; d.type = PType::Double; d.label = label; d.def = def; d.min = mn; d.max = mx;
        d.increment = inc; d.digits = digits; d.hint = hint; d.look = look;
        return add(d);
    }
    ParamDef& integer(const char* id, const char* label, int def, int mn, int mx, const char* hint, bool look) {
        ParamDef d;
        d.id = id; d.type = PType::Int; d.label = label; d.def = def; d.min = mn; d.max = mx; d.hint = hint; d.look = look;
        return add(d);
    }
    ParamDef& boolean(const char* id, const char* label, bool def, const char* hint, bool look) {
        ParamDef d;
        d.id = id; d.type = PType::Bool; d.label = label; d.def = def ? 1.0 : 0.0; d.min = 0; d.max = 1;
        d.hint = hint; d.look = look; d.animates = false;
        return add(d);
    }
    ParamDef& choice(const char* id, const char* label, std::vector<std::string> options, int def,
                     const char* hint, bool look) {
        ParamDef d;
        d.id = id; d.type = PType::Choice; d.label = label; d.options = std::move(options);
        d.def = std::max(0, def); d.min = 0; d.max = double(std::max<size_t>(1, d.options.size()) - 1);
        d.hint = hint; d.look = look; d.animates = false;
        return add(d);
    }
    ParamDef& label(const char* id, const char* labelText, const std::string& value, const char* hint) {
        ParamDef d;
        d.id = id; d.type = PType::Label; d.label = labelText; d.defString = value; d.hint = hint;
        d.animates = false; d.evaluateOnChange = false;
        return add(d);
    }
    ParamDef& str(const char* id, const char* labelText, const std::string& value, StrKind kind, const char* hint) {
        ParamDef d;
        d.id = id; d.type = PType::String; d.label = labelText; d.defString = value; d.strKind = kind; d.hint = hint;
        d.animates = false; d.evaluateOnChange = false;
        return add(d);
    }
    ParamDef& push(const char* id, const char* labelText, const char* hint) {
        ParamDef d;
        d.id = id; d.type = PType::Push; d.label = labelText; d.hint = hint; d.animates = false;
        d.evaluateOnChange = false; d.persistent = false;
        return add(d);
    }
};

std::vector<ParamDef> build() {
    Builder b;

    // ---------------------------------------------------------------- [Look]
    std::vector<std::string> presetNames{"Custom"};
    std::string presetHint =
        "Curated starting points. Choosing a look sets the stock pair, development, print, halation, bloom, "
        "grain and lens settings in one go (input/output colour space, exposure and HDR settings are kept). "
        "Any later tweak keeps your changes; the status line below then shows \"(modified)\". "
        "Choose \"Custom\" to leave all settings as they are.";
    for (const LookPreset& p : lookPresets()) {
        presetNames.push_back(p.name);
        presetHint += "\n\n" + p.name + ": " + p.description;
    }
    b.beginGroup(id::gLook, "Look", true, "Look presets and processing mode.");
    b.choice(id::lookPreset, "Look Preset", presetNames, 0, presetHint.c_str(), false);
    b.label(id::lookPresetStatus, "Preset Status", "Custom",
            "Which look preset the current settings come from. \"(modified)\" means you have changed at least "
            "one look setting since applying it.")
        .persistent = false;   // derived from the settings, recomputed when the instance is created
    b.choice(id::mode, "Mode", {"Full", "Color Only", "Texture Only"}, 0,
             "Full: complete film emulation (colour + halation, bloom, grain, lens and gate effects).\n"
             "Color Only: the colour pipeline alone - exactly what an exported LUT contains.\n"
             "Texture Only: for footage that is ALREADY graded/display-referred in the selected Output space; "
             "adds only the photographic texture (halation, bloom, grain, acutance, weave, vignette) and leaves "
             "the colour untouched. Use it after your own grade or after another film look.",
             false);
    {   // v1.3 WP-1: modular pipeline
        const std::vector<std::string> core = core::stageNames();   // Full .. Print Only, then ALEXA Rec.709
        std::vector<std::string> stages(core.begin(), core.begin() + int(core::Stage::AlexaRec709));
        stages.push_back("Texture Only");
        stages.push_back("Negative Only (clean)");
        stages.push_back(core[size_t(core::Stage::AlexaRec709)]);   // appended in v1.3 (StageChoice::AlexaRec709)
        b.choice(id::pipelineStage, "Pipeline Stage", stages, 0,
                 "Where the film chain stops (or starts), so one node can do one job and you can build the chain "
                 "from several nodes or finish with your own display transform (OpenDRT, 2499 DRT, ARRI REVEAL, "
                 "ACES).\n"
                 "Full Chain: camera -> negative -> print -> display (the complete look).\n"
                 "Negative -> Log Scan: stops after the developed negative and writes it as a Cineon-style log "
                 "scan (printing density as the selected Print Stock sees it, 0.002 density per 10-bit code value, "
                 "clear film at code 95). Feed it to a second node set to Print Only.\n"
                 "Negative -> Scene: the developed negative turned back into scene light (grey-balanced, film "
                 "colour, curve shape, halation and grain kept, print and display left out), encoded in Scene "
                 "Encoding (Output group). Put your own DRT after the node.\n"
                 "Print Only: the input is a Log Scan from this plug-in (or any Cineon printing-density scan); "
                 "prints it on the Print Stock and renders the display. Camera Log and Sensor Match are not "
                 "used.\n"
                 "Texture Only: same as Mode = Texture Only.\n"
                 "Negative Only (clean): Negative -> Scene without any texture (no halation, bloom, grain, "
                 "acutance, weave, vignette, damage) - the node that only flips the negative stock.\n"
                 "ALEXA Rec.709: no film - the camera rendered the way the ALEXA's classic LogC3 -> Rec.709 look "
                 "renders it (our own fitted model, see ALEXA Rendering). Textures (grain, halation, bloom, "
                 "damage...) still apply. Rec.709 Gamma 2.4 is the reference display; other outputs get the same "
                 "rendering re-encoded (HDR: the SDR rendering placed with grey at 15 nits).\n"
                 "Grain, halation and damage always act in the film domain of the stage: in the negative for the "
                 "negative exits, on the scanned negative's density for Print Only. Reversal (slide) stocks have "
                 "no negative stage and always render the full chain.",
                 false);
    }

    // v1.2: the gauge (ID grainFormat, moved here from [Grain]) couples every texture to the film format
    b.choice(id::grainFormat, "Gauge", {"Super 8", "16 mm", "Super 35", "65 mm", "35 mm Stills"}, 2,
             "Film gauge the image was 'shot' on. It sets the whole texture ladder at once, like changing the "
             "camera: grain size and strength (Super 8 very coarse, 16 mm clearly visible, Super 35 the cinema "
             "standard, 65 mm very fine; 35 mm Stills between Super 35 and 65 mm), the film's own sharpness "
             "(Film Resolution), and the size of the halation ring and of the acutance edge effect.",
             true);
    // v1.3 P1: user presets - the complete parameter set as a JSON file in the user's Reafilm folder
    b.str(id::userPresetName, "User Preset", "", StrKind::SingleLine,
          "Name of your own preset. Save User Preset writes every setting of this node (all groups, including "
          "input, output and every module switch; not the License, the LUT folder or False Colour) as "
          "<name>.json to the Reafilm Presets folder of your user account (macOS: ~/Library/Application "
          "Support/Reafilm/Presets, Windows: %APPDATA%\\Reafilm\\Presets, Linux: ~/.config/reafilm/presets). "
          "Load User Preset reads it back. Presets are plain text and can be copied between computers.");
    b.push(id::userPresetSave, "Save User Preset",
           "Saves all current settings under the User Preset name (an existing preset of that name is replaced).");
    b.push(id::userPresetLoad, "Load User Preset",
           "Loads the preset named in User Preset. Settings missing from the file (e.g. from an older version) "
           "return to their defaults; entries this version does not know are ignored and reported.");

    // ---------------------------------------------------------------- [Input]
    const std::vector<std::string> inputs = core::inputSpaceNames();
    b.beginGroup(id::gInput, "Input", true, "What the camera recorded.");
    b.choice(id::inputSpace, "Camera Log", inputs, 0,
             "The log encoding and gamut of the incoming footage. Set this to match your camera (default: Fujifilm "
             "F-Log2 C / F-Gamut C). In a colour-managed Resolve project, use a timeline/working space that keeps "
             "the camera log intact for this node, or convert to the matching log space before this node. "
             "Wrong settings show up as overly flat or harsh, colour-shifted images. For footage without a log "
             "profile (camcorders, phones, JPEG stills) pick Rec.709 video or sRGB: the clipped highlights are "
             "expanded back to about +4.5 stops so the film still has a shoulder to roll into. Blackmagic BRAW: in "
             "the Camera Raw panel choose Color Science Gen 5, Gamut Blackmagic Design, Gamma Blackmagic Design "
             "Film, then pick Blackmagic Film Gen 5 here.",
             false);
    b.boolean(id::stillPhoto, "Still Photo", false,
              "For a single photograph (RAW debayered by Resolve, or a JPEG with the sRGB input) instead of "
              "moving footage. The texture is frozen to one frame - grain, dust and the gate stay as they are - "
              "and everything that only makes sense over time is left out: gate weave, Film Blur and Film "
              "Breath. Off = normal moving-picture behaviour.",
              false);
    b.boolean(id::exposureEnable, "Enable Exposure", true,
              "Switches the Exposure trim below on or off without losing its value. Off = the footage reaches the "
              "film exactly as it was recorded (0 stops).",
              false);
    b.dbl(id::exposure, "Exposure", 0.0, -4.0, 4.0, 0.05, 2,
          "Camera exposure correction in stops, applied to the scene before it reaches the film. Because it acts "
          "before the negative, +/- exposure slides the image along the film's characteristic curve: highlights "
          "roll off and shadows build density the way a real over/under-exposed negative does.",
          false);
    // v1.3 P1: scene white balance (new module, OFF by default; a camera setting like Exposure, not part of looks)
    b.boolean(id::sceneWbEnable, "Enable Scene White Balance", false,
              "Corrects the white balance of the footage in scene light, before it reaches the film (as if the "
              "camera had been set differently). Off = the footage's own white balance.",
              false);
    b.dbl(id::sceneWbTemperature, "Scene Temperature (K)", 6500.0, 2000.0, 15000.0, 50.0, 0,
          "Colour temperature of the light the footage is assumed to be balanced for. 6500 = unchanged. Higher "
          "values warm the image, lower values cool it (set 3200 for daylight-balanced footage shot under "
          "tungsten light). Applied before the negative, so the film responds to the corrected light.",
          false);
    b.dbl(id::sceneWbTint, "Scene Tint", 0.0, -100.0, 100.0, 1.0, 0,
          "Green-magenta correction: positive values add magenta (remove a green cast, e.g. from fluorescent "
          "or LED light), negative values add green. 0 = unchanged.",
          false);

    // ---------------------------------------------------------------- [Highlight Latitude] (v1.3)
    b.beginGroup(id::gHighlightLatitude, "Highlight Latitude", true,
                 "Gives the camera's highlights the latitude of a camera with more headroom. Works on the scene "
                 "light right after the F-Log2 decode, before Sensor Match, the negative and the ALEXA rendering: "
                 "it finds where the X-H2S clips, rebuilds clipped colour channels so blown highlights go white "
                 "instead of cyan, yellow or magenta, and stretches the last stops below the clip so they roll "
                 "into the film's (or the ALEXA's) shoulder instead of stopping at a wall. Fujifilm F-Log2 C / "
                 "F-Log2 footage only; everything below the shoulder is untouched.");
    b.boolean(id::latitudeEnable, "Enable Highlight Latitude", false,
              "Switches Highlight Latitude on or off without losing its settings. Off = the camera's highlights "
              "exactly as recorded.",
              false);
    b.dbl(id::latitudeStops, "Latitude", 2.5, 0.0, 4.0, 0.05, 2,
          "Extra headroom in stops: the camera's clip level is placed this much higher, and the stops below it "
          "are stretched smoothly up to it, so highlights reach the shoulder of the film or the ALEXA rendering "
          "the way they would on a camera with this many more stops above grey. 0 = no extension (clip "
          "reconstruction only).",
          false);
    b.dbl(id::latitudeSoftness, "Shoulder Softness", 0.4, 0.0, 1.0, 0.01, 2,
          "How far below the clip the extension starts: 0 = only the last half stop (hardest), 1 = the last "
          "three stops (softest, longest roll). Everything below that point is unchanged.",
          false);
    b.dbl(id::latitudeReconstruct, "Clip Reconstruction", 1.0, 0.0, 1.0, 0.01, 2,
          "Rebuilds colour channels that clip before the others. Near-white highlights where one or two channels "
          "have clipped go smoothly to white instead of turning cyan (sky), yellow or magenta (skin, "
          "practicals); strongly coloured lights where only their own channel clips keep their colour. "
          "0 = off.",
          false);
    b.dbl(id::latitudeDesaturate, "Highlight Desaturation", 0.3, 0.0, 1.0, 0.01, 2,
          "Desaturates the stretched top stops toward white, the way a negative's colour thins out as it nears "
          "its shoulder. 0 = keep the colour of the highlights (only the clip reconstruction acts).",
          false);
    b.dbl(id::latitudeClip, "Sensor Clip", 0.854, 0.6, 1.0, 0.001, 3,
          "F-Log2 code value (0..1, full range) at which the camera's channels clip. 0.854 (10-bit 874) is "
          "where the X-H2S clips in F-Log2 / F-Log2 C, measured on its footage: about 6.3 stops above 18 % "
          "grey. Lower it if a clip still shows colour casts; raise it for footage that clips higher.",
          false);

    // ---------------------------------------------------------------- [Camera Emulation] (v1.2)
    // v1.3: shown as "Sensor Match" (IDs unchanged); the ALEXA look itself is the ALEXA Rendering group
    b.beginGroup(id::gCameraEmulation, "Sensor Match", true,
                 "The subtle, sensor-level part of looking like another camera: what that camera's sensor would "
                 "have recorded of the same scene, before any look or rendering. It does not add the camera's "
                 "display look (for the ALEXA look see ALEXA Rendering).");
    b.boolean(id::cameraEmulationEnable, "Enable Sensor Match", true,
              "Switches the sensor match on or off without losing its settings. Off = the camera's own colour "
              "as decoded from its log encoding. It only acts on footage of the source camera (Fujifilm F-Log2 C / "
              "F-Log2 / F-Log input); other cameras pass through unchanged.",
              false);
    b.choice(id::cameraEmulation, "Sensor", {"Fujifilm X-H2S -> ARRI ALEXA / AMIRA"}, 0,
             "Fujifilm X-H2S -> ARRI ALEXA / AMIRA: renders the colours the ALEXA's ALEV III sensor would have "
             "recorded of the same scene. Fitted from measured spectral responsivities (ARRI ALEXA; Fujifilm X-Trans "
             "X-T4 as the X-H2S proxy) over about 950 natural and test reflectances under daylight, tungsten, LED and "
             "fluorescent light: a smooth, exposure-independent correction in scene-linear light that leaves greys "
             "untouched. It changes skin, foliage, skies and saturated light the way the two sensors differ; colours "
             "two sensors see identically stay identical (sensor metamerism a 3-channel correction cannot undo).",
             false);
    b.dbl(id::cameraEmulationStrength, "Strength", 1.0, 0.0, 1.0, 0.01, 2,
          "Blend between the camera's own colour (0) and the full sensor match (1).", false);

    // ---------------------------------------------------------------- [ALEXA Rendering] (v1.3)
    b.beginGroup(id::gAlexaRendering, "ALEXA Rendering", true,
                 "Our own parametric model of the ALEXA's classic LogC3 -> Rec.709 rendering (tone curve, "
                 "hue- and brightness-dependent saturation and hue, skin), fitted to ARRI's published look. Two "
                 "uses: Pipeline Stage = ALEXA Rec.709 renders the camera that way instead of a film print; ALEXA "
                 "Colour brings only its colour into the film chain.");
    b.boolean(id::alexaDisplayEnable, "Enable ALEXA Rec.709", true,
              "Only matters when Pipeline Stage = ALEXA Rec.709. Off = that node renders the Full Chain instead "
              "(quick A/B between the ALEXA rendering and the film look).",
              false);
    b.boolean(id::alexaColourEnable, "Enable ALEXA Colour", false,
              "Switches ALEXA Colour on or off without losing its value. Off = the film sees the camera's colour "
              "unchanged.",
              true);
    b.dbl(id::alexaColour, "ALEXA Colour", 1.0, 0.0, 1.0, 0.01, 2,
          "How much of the ALEXA rendering's colour (its hue shifts and saturation per hue, skin) the scene "
          "gets before the negative, matched through the film: cherry reds, greens without a blue lean, cohesive "
          "skin, neutral whites and slightly cool shadows. Applied in scene light, so mid grey and exposure stay "
          "where they are and the film still does the tone and the print look. 0 = none, 1 = the full ALEXA colour.",
          true);

    // ---------------------------------------------------------------- [Tone] (v1.3)
    b.beginGroup(id::gTone, "Tone", true,
                 "Zone exposure, filmic contrast, highlight roll-off and dye saturation - the film responds to them "
                 "physically.");
    b.boolean(id::toneEnable, "Enable Zones & Contrast", true,
              "Switches the zone exposure and Filmic Contrast below on or off without losing their values. Both "
              "act on the scene light after Exposure and before the negative, so the film's toe and shoulder "
              "still shape the result. At their defaults (0 stops, contrast 1) they change nothing.",
              true);
    const char* zoneNote =
        " Like Exposure, but only for that part of the tone scale: it acts on the scene light before the film, "
        "ratio-preserving (colours keep their hue and saturation, greys stay neutral), with a smooth transition "
        "set by Zone Falloff. Tones can never cross: a strong pull is softened so a brighter area always stays "
        "brighter.";
    std::string hSh = std::string("Exposure in stops of everything BELOW Shadows Range (dark areas).") + zoneNote;
    std::string hLi = std::string("Exposure in stops of everything ABOVE Lights Range (bright mid-tones and "
                                  "above).") + zoneNote;
    std::string hHi = std::string("Exposure in stops of everything ABOVE Highlights Range (skies, windows, "
                                  "bright walls). Pull it down (-) to bring sky detail out of the print's shoulder "
                                  "into its straight part while faces stay where they are.") + zoneNote;
    std::string hSp = std::string("Exposure in stops of everything ABOVE Specular Range (practicals, the sun, "
                                  "glints).") + zoneNote;
    b.dbl(id::zoneShadows, "Shadows", 0.0, -4.0, 4.0, 0.05, 2, hSh.c_str(), false);
    b.dbl(id::zoneShadowsRange, "Shadows Range", -2.0, -8.0, 2.0, 0.1, 1,
          "Where the Shadows zone ends, in stops relative to 18 % grey of the scene (after Exposure).", false);
    b.dbl(id::zoneLights, "Lights", 0.0, -4.0, 4.0, 0.05, 2, hLi.c_str(), false);
    b.dbl(id::zoneLightsRange, "Lights Range", 1.0, -2.0, 6.0, 0.1, 1,
          "Where the Lights zone begins, in stops relative to 18 % grey of the scene.", false);
    b.dbl(id::zoneHighlights, "Highlights", 0.0, -4.0, 4.0, 0.05, 2, hHi.c_str(), false);
    b.dbl(id::zoneHighlightsRange, "Highlights Range", 3.0, -1.0, 8.0, 0.1, 1,
          "Where the Highlights zone begins, in stops relative to 18 % grey of the scene (a bright sky is "
          "typically +3 to +5 stops).", false);
    b.dbl(id::zoneSpecular, "Specular", 0.0, -4.0, 4.0, 0.05, 2, hSp.c_str(), false);
    b.dbl(id::zoneSpecularRange, "Specular Range", 5.0, 0.0, 10.0, 0.1, 1,
          "Where the Specular zone begins, in stops relative to 18 % grey of the scene.", false);
    b.dbl(id::zoneFalloff, "Zone Falloff", 1.0, 0.25, 4.0, 0.05, 2,
          "How softly every zone fades in around its range, in stops. Small = a tight zone, large = a gentle, "
          "wide blend.", false);
    b.dbl(id::filmContrast, "Filmic Contrast", 1.0, 0.5, 2.0, 0.01, 2,
          "Contrast of the scene in log exposure about the Contrast Pivot, before the negative: the film's toe and "
          "shoulder still roll the ends off, so blacks and whites never clip. 1 = off, higher = punchier, lower = "
          "flatter. The colour density of the stock is kept (the channels follow half of the contrast change, so "
          "saturation neither drains nor goes neon).", true);
    b.dbl(id::contrastPivot, "Contrast Pivot", 0.0, -3.0, 3.0, 0.05, 2,
          "The tone that Filmic Contrast leaves unchanged, in stops relative to 18 % grey (0 = middle grey).", true);
    b.boolean(id::rolloffEnable, "Enable Film Roll-off", true,
              "Switches the Film Roll-off below on or off without losing its values. At Softness 0 it changes "
              "nothing.",
              true);
    b.dbl(id::rolloffStart, "Roll-off Start", 3.0, 0.5, 8.0, 0.1, 1,
          "Where the reshaped highlight shoulder of the print begins, in stops above 18 % grey of the scene. "
          "Everything below stays exactly as the print stock renders it.", true);
    b.dbl(id::rolloffSoftness, "Roll-off Softness", 0.0, -1.0, 1.0, 0.01, 2,
          "Shape of the print's highlight shoulder above Roll-off Start: + = a longer, softer shoulder (highlights "
          "compress gently and keep detail, white is reached later), - = a shorter, crisper shoulder (highlights "
          "reach print white sooner). It reshapes the print stock's own curve, so print white stays the clear "
          "print and printer points slide along the new curve. 0 = the stock. Print process only.", true);
    b.boolean(id::dyeSatEnable, "Enable Dye Saturation", true,
              "Switches Dye Saturation below on or off without losing its values. At Saturation 1 and Balance 0 it "
              "changes nothing.",
              true);
    b.dbl(id::dyeSaturation, "Dye Saturation", 1.0, 0.0, 2.0, 0.01, 2,
          "Saturation the way dye film makes it: more saturation means more dye in the layers that carry the "
          "colour, so saturated colours get deeper, denser and darker instead of brighter and neon (unlike RGB "
          "saturation). It scales each colour's dye difference from a neutral of the same brightness in the "
          "projected film (print, reversal original or scan), so greys, print white and black stay exactly as "
          "they are. 1 = the stock, 0 = black & white dyes.", true);
    b.dbl(id::dyeRichness, "Richness", 0.25, 0.0, 1.0, 0.01, 2,
          "How much denser (darker) a colour gets as it gains saturation: the extra dye a richer colour carries. "
          "0 = the dye differences are only spread (the colour keeps its brightness), 0.25 = film-like (strong "
          "colours deepen a little, skin barely darkens), 0.5 = rich, 1 = very dense. Lowering "
          "saturation does the reverse. Only acts when Dye Saturation or Balance moves.", true);
    b.dbl(id::dyeSatBalance, "Saturation Balance", 0.0, -1.0, 1.0, 0.01, 2,
          "Where the dye saturation acts most: + = more saturation in the highlights and less in the shadows, "
          "- = the reverse (up to x2 / x0.5 at the ends). 0 = even.", true);

    // ---------------------------------------------------------------- [Negative]
    const std::vector<core::NegativeStockInfo> negs = core::negativeStocks();
    std::vector<std::string> negNames;
    std::string negHint = "The camera negative (or reversal / black & white stock). It defines spectral sensitivity, "
                          "tone curve, colour crosstalk, grain character and halation behaviour.";
    for (const auto& n : negs) {
        negNames.push_back(n.name);
        if (!n.description.empty()) negHint += "\n\n" + n.name + ": " + n.description;
    }
    // v1.2: the default look (ARRI-organic, Referent-dense) starts from the real VISION3 250D negative
    int negDefault = indexOf(negNames, "Kodak Vision3 250D 5207");
    b.beginGroup(id::gNegative, "Negative", true, "Camera film stock and its development.");
    b.choice(id::negStock, "Film Stock (all)", negNames, negDefault < 0 ? 0 : negDefault, negHint.c_str(), true).secret = true;
    {   // real film only: the visible list (a view of negStock)
        std::vector<std::string> realNames;
        std::string realHint = "The camera negative (or reversal / black & white stock), from the manufacturer's data "
                               "sheet. It defines spectral sensitivity, tone curve, colour crosstalk, grain character and "
                               "halation behaviour.";
        for (int k : realNegStocks()) {
            realNames.push_back(negs[size_t(k)].name);
            if (!negs[size_t(k)].description.empty()) realHint += "\n\n" + negs[size_t(k)].name + ": " + negs[size_t(k)].description;
        }
        b.choice(id::filmStock, "Film Stock", realNames, realNegFor(negDefault < 0 ? 0 : negDefault), realHint.c_str(), false)
            .persistent = false;
    }
    b.boolean(id::developmentEnable, "Enable Development", true,
              "Switches the development controls below (Push / Pull, Shot Under / Over, Color Density, Neg Bleach "
              "Bypass, Neutralize) on or off without losing their values. Off = the stock's standard development: no push or pull, "
              "nominal colour density, normal bleach, the stock's own curves. The film stock itself always stays "
              "active - it is the look.",
              true);
    b.dbl(id::pushPull, "Push / Pull", 0.0, -2.0, 3.0, 0.1, 2,
          "Development in stops. Push (+) raises contrast and effective speed, lifts fog and makes the grain "
          "coarser (about x1.25 per stop); pull (-) gives a softer, lower-contrast, finer-grained negative. "
          "Unlike Exposure this changes the curve shape, not the position on it.",
          true);
    b.boolean(id::pushUnder, "Shot Under / Over", false,
              "Push / Pull as a photographer does it: the film was rated faster (push) or slower (pull) in the "
              "camera, so a push of +2 also means the scene was exposed 2 stops under, and a pull of -1 that it was "
              "exposed 1 stop over. Pushing then gives thin, grainy shadows and hard highlights; pulling a dense, "
              "soft negative. Off = Push / Pull changes the development only and the exposure stays where it is.",
              true);
    b.dbl(id::interlayer, "Color Density", 1.0, 0.0, 2.0, 0.01, 2,
          "Interlayer (DIR) effect: how strongly the dye layers inhibit each other. 1 = the stock's nominal "
          "colour density; higher values give denser, more separated colours, 0 a flatter, purer-channel "
          "rendering. Neutrals stay neutral at any setting.",
          true);
    b.dbl(id::negBleach, "Neg Bleach Bypass", 0.0, 0.0, 1.0, 0.01, 2,
          "Skips (partially) the bleach step of the negative so silver stays in the film alongside the dyes. "
          "The extra silver density builds with exposure: higher contrast, harder highlights and muted colour, "
          "strongest in the bright parts of the image. 0 = normal processing.",
          true);
    b.dbl(id::neutralize, "Neutralize", 0.0, 0.0, 1.0, 0.01, 2,
          "Removes the stock's per-layer curve differences (the subtle colour crossover between shadows and "
          "highlights). 0 = the stock's natural character, 1 = perfectly neutral greys from black to white.",
          true);
    b.boolean(id::crossProcess, "Cross Process", false,
              "Slide (reversal) film developed in negative chemistry (E-6 film in C-41), then printed or scanned "
              "like a negative - the cross-processed look: very high contrast, saturated colour and strong, "
              "uneven colour casts from the missing orange mask. Use Process and Print Stock as for a negative; "
              "the Printer Lights correct the cast. Slide stocks only.",
              true);
    b.dbl(id::filmAge, "Expired", 0.0, 0.0, 1.0, 0.01, 2,
          "Film past its date. Base fog builds up (fastest in the blue-sensitive layer), so the shadows lift into "
          "a coloured haze while grey stays balanced; the film loses speed and contrast and the grain gets "
          "coarser. 0 = fresh film, 0.3 = a year or two out of date, 1 = about ten years, stored warm. On slide "
          "film the blacks fade instead.",
          true);
    b.label(id::negProvenance, "Stock Data", negs.empty() ? std::string() : negs[size_t(std::max(negDefault, 0))].provenance,
            "Where the selected film stock's data comes from (v1.3): a manufacturer data sheet (fitted, not measured "
            "from film), partly borrowed from a named proxy stock, a Reafilm design, or a style approximation.")
        .persistent = false;   // derived from Film Stock, recomputed when the instance is created

    // ---------------------------------------------------------------- [Print]
    const std::vector<std::string> prints = core::printStockNames();
    int printDefault = indexOf(prints, "Kodak Vision 2383");   // real film only (was the designed P6 Modern Print)
    b.beginGroup(id::gPrint, "Print", true, "How the negative becomes a positive image.");
    b.choice(id::process, "Process", {"Print", "Lab Scan"}, 0,
             "Print: the negative is printed onto print stock and projected - the classic cinema look with dense "
             "blacks and rich colour.\nLab Scan: the negative is scanned and inverted by a lab scanner - a flatter, "
             "more neutral rendering that keeps the negative's character without print-stock contrast.\n"
             "Reversal (slide) stocks are direct positives and ignore this setting.",
             true);
    b.choice(id::printStock, "Print Stock (all)", prints, printDefault < 0 ? 0 : printDefault,
             "The print film the negative is printed on (Process = Print). It sets print contrast, black density, "
             "highlight roll-off and the colour of the projected image.",
             true).secret = true;
    {   // real film only: the visible list (a view of printStock)
        std::vector<std::string> realPrints;
        for (int k : realPrintStocks()) realPrints.push_back(prints[size_t(k)]);
        b.choice(id::printFilm, "Print Stock", realPrints, realPrintFor(printDefault < 0 ? 0 : printDefault),
                 "The print film the negative is printed on (Process = Print), from the manufacturer's data sheet. It "
                 "sets print contrast, black density, highlight roll-off and the colour of the projected image.\n\n"
                 "Kodak Vision 2383: the standard release print.\nKodak Vision Premier 2393: higher D-max, denser "
                 "blacks, more saturated.\nFujifilm Eterna-CP 3521XD: deep blacks, rich contrast.\nFujifilm Eterna-CP "
                 "3513DI: neutral and gentle, made for digital intermediates.\n\nPhoto papers (RA-4 colour prints, viewed "
                 "by reflection: the paper white is the display white, softer blacks than projected film):\nKodak Endura "
                 "Premier: modern, bright and wide-gamut.\nKodak Portra Endura: low-contrast portrait paper.\nKodak Supra "
                 "Endura: punchier contrast and colour.\nFujifilm Crystal Archive: clean whites, the minilab print look.",
                 false)
            .persistent = false;
    }
    b.boolean(id::printEnable, "Enable Print Timing", true,
              "Switches the lab timing controls below (Printer Red / Green / Blue, Print Density, Print Bleach "
              "Bypass) on or off without losing their values. Off = the automatically grey-balanced light at 0 "
              "points and normal print processing. Process and Print Stock always stay active - they are the look.",
              true);
    const char* pointsNote =
        " Printer points as in a film lab: 1 point = 0.025 log exposure on that print layer, 0 = the stock's "
        "automatically grey-balanced light.";
    std::string hr = std::string("Red printer light. More red light (+) adds cyan to the print: the image gets LESS "
                                 "red / more cyan. Fewer points (-) warm it towards red.") + pointsNote;
    std::string hg = std::string("Green printer light. More green light (+) adds magenta to the print: the image gets "
                                 "LESS green / more magenta. Fewer points (-) push it towards green.") + pointsNote;
    std::string hb = std::string("Blue printer light. More blue light (+) adds yellow to the print: the image gets LESS "
                                 "blue / more yellow. Fewer points (-) cool it towards blue.") + pointsNote;
    b.dbl(id::printerR, "Printer Red", 0.0, -12.0, 12.0, 0.5, 1, hr.c_str(), true);
    b.dbl(id::printerG, "Printer Green", 0.0, -12.0, 12.0, 0.5, 1, hg.c_str(), true);
    b.dbl(id::printerB, "Printer Blue", 0.0, -12.0, 12.0, 0.5, 1, hb.c_str(), true);
    b.dbl(id::printExposure, "Print Density", 0.0, -12.0, 12.0, 0.5, 1,
          "Overall printer light in points on all three layers. Positive values make the print BRIGHTER "
          "(less print density), negative values darker and denser. This is the lab's way to brighten or darken "
          "a print: highlights and blacks respond like real print film, not like a gain.",
          true);
    b.dbl(id::printBleach, "Print Bleach Bypass", 0.0, 0.0, 1.0, 0.01, 2,
          "Bleach bypass on the print: retained silver deepens the blacks and shadows, raises contrast and "
          "desaturates - the classic gritty, metallic release-print look. 0 = normal processing.",
          true);

    b.dbl(id::printCrossover, "Print Crossover", 0.0, -1.0, 1.0, 0.01, 2,
          "Warm highlights and cool shadows the way many release prints render them (the classic 2383 chain): "
          "positive values add yellow and remove cyan in the bright end of the print and do the opposite in the "
          "shadows, growing from the middle of the scale outward, while middle grey stays neutral. Negative values "
          "reverse it (cool highlights, warm shadows). 0 = the print stock exactly as designed. Print process only.",
          true);

    // v1.3 print lab (WP-2): how the print is made and shown. Physical lab parameters, not a colour twist.
    {
        const std::vector<std::string> pprov = core::printStockProvenance();
        b.label(id::printProvenance, "Print Stock Data", pprov.empty() ? std::string() : pprov[size_t(std::max(printDefault, 0))],
                "Where the selected print stock's data comes from: a manufacturer data sheet (fitted, not measured "
                "from film), partly borrowed from a named proxy stock, a Reafilm design, or a style approximation.")
            .persistent = false;   // derived from Print Stock, recomputed when the instance is created
        b.boolean(id::printLabEnable, "Enable Print Lab", true,
                  "Switches the print lab controls below (Projection White, Process Aim, Print Contrast, Rendering) on "
                  "or off without losing their values. Off = a contact print at the nominal process aim, adapted to "
                  "D65 - exactly the v1.2 print.",
                  true);
        b.choice(id::projectionWhite, "Projection White", core::projectionWhiteNames(), 0,
                 "The white the projected print is shown at.\nD65 (Adapted): the eye adapted to the projector "
                 "lamp, mapped to the D65 white of a video display (the classic film-out preview).\nTheatre Xenon "
                 "(Unadapted): the print as a colorimeter reads it off a xenon-lit screen - the clear print base and "
                 "the lamp give a warmer, slightly yellow white.\nD60 Sim: adapted to the ACES D60 white and "
                 "shown on a D65 display (a slightly warm white, as in DCI / ACES D60 sim mastering).",
                 true);
        b.choice(id::printAim, "Process Aim", core::printAimNames(), 0,
                 "Where the lab runs its print process within its tolerance band (ECP-2 development).\nNominal: the "
                 "data-sheet aim.\nRich: one unit more development - higher print gamma, deeper D-max, slightly "
                 "more fog; denser blacks and more saturated colour.\nSoft: one unit less - lower gamma and D-max, "
                 "lifted blacks, gentler colour. The lab re-times the print, so middle grey stays put.",
                 true);
        b.dbl(id::printContrast, "Print Contrast", 0.0, -1.0, 1.0, 0.01, 2,
              "Fine control of print development in the same units as Process Aim (added to it): + raises the print's "
              "gamma and D-max (more contrast, deeper blacks), - lowers them. Each layer reacts by its own "
              "development sensitivity (the bottom yellow layer most). Middle grey stays at the print's aim.",
              true);
        b.choice(id::printRender, "Rendering", core::printRenderNames(), 0,
                 "How the print is made and viewed.\nContact Print: the camera negative printed directly (answer / "
                 "show print).\nRelease Print (IP / IN): the negative is duplicated twice on VISION Color "
                 "Intermediate 2242 (interpositive, internegative) at the lab aims before printing - slightly "
                 "softer toe and shoulder and a subtle colour shift, as in a theatrical release print.\nTelecine / "
                 "Scan: the print scanned on a telecine; every layer is balanced between print D-min and D-max and "
                 "the black is set just above D-max - neutral blacks and highlights.",
                 true);
    }
    // Lab & Gate: the colour timer's controls in print density (print process only)
    {
        b.boolean(id::colourHeadEnable, "Enable Colour Head", true,
                  "Switches the Colour Head (Cyan / Magenta / Yellow filtration, Preserve Exposure) on or off without "
                  "losing its values. Off = no extra filtration.",
                  true);
        const char* ccNote =
            " In CC (colour compensating units: 10 CC = 0.10 density) of an idealised dichroic filter in the printer's "
            "colour head, relative to the balanced filter pack (0). The filter is integrated over the printer lamp, the "
            "negative and the print layers' sensitivities, so it acts like a real filter (with its small cross-talk), "
            "not like a channel gain. Print process only.";
        std::string hc = std::string("Cyan filtration: holds back red printer light, so the print gets LESS cyan dye - the "
                                     "image turns more red. Negative values remove cyan from the pack (image more cyan).") + ccNote;
        std::string hm = std::string("Magenta filtration: holds back green printer light - the image turns more green. "
                                     "Negative values: more magenta.") + ccNote;
        std::string hy = std::string("Yellow filtration: holds back blue printer light - the image turns more blue. "
                                     "Negative values: warmer, more yellow. (To remove a cast, add the filter of the "
                                     "cast's colour, as in a darkroom.)") + ccNote;
        b.dbl(id::headCyan, "Head Cyan", 0.0, -60.0, 60.0, 1.0, 1, hc.c_str(), true);
        b.dbl(id::headMagenta, "Head Magenta", 0.0, -60.0, 60.0, 1.0, 1, hm.c_str(), true);
        b.dbl(id::headYellow, "Head Yellow", 0.0, -60.0, 60.0, 1.0, 1, hy.c_str(), true);
        b.dbl(id::headPreserve, "Preserve Exposure", 1.0, 0.0, 1.0, 0.01, 2,
              "Filters also take light away. 1 = the printer's overall light is corrected so the print's middle grey keeps "
              "its brightness (what a colour timer does); 0 = the filtration's light loss stays (more filtration = a "
              "lighter print, as on an enlarger without compensation).",
              true);
        b.label(id::printLights, "Printer Lights", "R 25.0  G 25.0  B 25.0",
                "The printer lights in the lab convention: 1 point = 0.025 log exposure, nominal 25-25-25 = the stock's "
                "grey-balanced light. Higher numbers = more light on that layer = more of its dye (a darker, less red / "
                "green / blue print). Shows Printer Red / Green / Blue and Print Density, and the Colour Head filtration as "
                "its equivalent in points (before Preserve Exposure).")
            .persistent = false;   // derived from the timing controls
        b.boolean(id::printEndsEnable, "Enable Print Black / White", true,
                  "Switches Print Black, Print White and De-Fog on or off without losing their values.", true);
        b.dbl(id::printBlack, "Print Black", 0.0, -1.5, 0.25, 0.01, 2,
              "Density at the black end of the print (its D-max), in density units of the print: + = deeper, denser "
              "blacks; - = a lifted, milky black like a flashed or under-developed print. The tones above the shadows are "
              "left alone (middle grey moves less than half a percent). This is the print's density, not a video lift. "
              "Print process only.",
              true);
        b.dbl(id::printWhite, "Print White", 0.0, -0.15, 0.4, 0.005, 3,
              "Density at the white end of the print (its D-min): + = greyer, dimmer whites, as a print on stock with more "
              "base fog or a slightly over-printed answer print; - = cleaner whites that reach the clear base sooner. "
              "Acts only near white. Print process only.",
              true);
        b.dbl(id::printDefog, "De-Fog", 0.0, 0.0, 1.0, 0.01, 2,
              "Removes this share of the print stock's own fog density from its white end (anti-fog in the developer): "
              "clearer, slightly brighter and more neutral whites. Fog on the NEGATIVE is not offered: the lab's grey "
              "balance times it out completely. Print process only.",
              true);
        b.boolean(id::printToneEnable, "Enable Print Toning", true,
                  "Switches Print Toning (shadow and highlight toning) on or off without losing its values.", true);
        b.dbl(id::toneShadowAmount, "Shadow Toning", 0.0, 0.0, 1.0, 0.01, 2,
              "Dye toning of the print's dense tones, like a selenium or blue toner on a print: the shadows take on the "
              "Shadow Hue while middle grey and the whites stay neutral. 1 = 0.10 density of toning. 0 = off.",
              true);
        b.dbl(id::toneShadowHue, "Shadow Hue", 220.0, 0.0, 360.0, 1.0, 0,
              "Colour of the shadow toning, in degrees: 0 red, 30 sepia / orange, 60 yellow, 120 green, 180 cyan, 220 "
              "steel blue, 240 blue, 300 magenta.",
              true);
        b.dbl(id::toneHighlightAmount, "Highlight Toning", 0.0, 0.0, 1.0, 0.01, 2,
              "Dye toning of the print's light tones (just above white, never the clear base itself): a warm or cool "
              "cast in the highlights while middle grey and black stay neutral. 1 = 0.10 density. 0 = off.",
              true);
        b.dbl(id::toneHighlightHue, "Highlight Hue", 50.0, 0.0, 360.0, 1.0, 0,
              "Colour of the highlight toning in degrees (see Shadow Hue). 40 .. 60 = a warm, golden print.",
              true);
    }

    // ---------------------------------------------------------------- [Halation]
    b.beginGroup(id::gHalation, "Halation", true, "Red-orange glow around bright light sources.");
    b.boolean(id::halationEnable, "Enable Halation", true,
              "Switches halation on or off without losing its settings. Off skips the halation passes entirely "
              "(no render cost).",
              true);
    b.dbl(id::halationAmount, "Halation Amount", 0.5, 0.0, 2.0, 0.01, 2,
          "Strength of halation: light that passes through the emulsion, reflects off the film base and re-exposes "
          "the red (and a little of the green) layer, creating a warm ring that hugs bright edges and practicals. "
          "It is added to the film's exposure, so the film curve shapes it like the image. Scaled by the stock "
          "(no-backing stocks glow much more). 0 = off.",
          true);
    b.dbl(id::halationRadius, "Halation Radius", 20.0, 2.0, 80.0, 0.5, 1,
          "Size of the halation ring in pixels at 1920 px image width for Super 35 (automatically scaled for other "
          "resolutions, proxy render scales and the Gauge: smaller gauges enlarge the ring).",
          true);
    b.dbl(id::halationThreshold, "Halation Threshold (0 = physical)", 0.0, 0.0, 6.0, 0.05, 2,
          "0 (default) = physical halation, no threshold: light reflects off the film base, so bright edges get a "
          "thin warm ring. Practicals, lamps, the sun, speculars and clipped highlights get the full share; a "
          "bright sky edge (about 4 stops above middle grey) still gets about three quarters of it, while skin "
          "and mid-tones hardly halate. Flat areas stay clean because the ring only shows where light spills "
          "beyond an edge. Any value above 0 switches to a different, artistic mode: a gate where only "
          "highlights brighter than this many stops above middle grey halate (the v1.1 behaviour; the glow "
          "eases in over about a stop and a half). Small values therefore halate MORE of the image than 0.",
          true);
    b.dbl(id::halationHue, "Halation Hue", 0.0, -1.0, 1.0, 0.01, 2,
          "Colour of the glow: how much of the reflected light re-exposes the green layer as well as the red. "
          "-1 deep red, 0 the classic red-orange, +1 orange-yellow.", true);
    // v1.3 chroma halation + Aura
    b.dbl(id::halationSaturation, "Halation Saturation", 1.0, 0.0, 2.0, 0.01, 2,
          "Colour strength of the glow. 0 = a neutral, uncoloured glow, 1 = the film's natural red-orange, "
          "2 = deeper colour. The brightness of the glow stays the same.", true);
    b.dbl(id::halationBackground, "Halation Background", 0.0, -1.0, 1.0, 0.01, 2,
          "How the glow's colour reacts to what is behind it. Above 0 the glow is more colourful over dark "
          "backgrounds and paler over bright ones (as on a projected print); below 0 the other way round. "
          "0 = the same colour everywhere.", true);
    b.boolean(id::auraEnable, "Enable Aura", false,
              "Adds a second, much wider and fainter glow around strong light sources: light that bounces "
              "more than once inside the film base. It gives lamps and the sun a soft warm haze around the "
              "ring. Needs Halation on. Off (the default) costs nothing.",
              true);
    b.dbl(id::auraRadius, "Aura Radius", 150.0, 40.0, 400.0, 1.0, 0,
          "Size of the aura in pixels at 1920 px image width for Super 35 (scaled automatically for other "
          "resolutions and the Gauge).", true);
    b.dbl(id::auraStrength, "Aura Strength", 0.5, 0.0, 2.0, 0.01, 2,
          "Brightness of the aura, relative to the halation amount. 0 = none.", true);
    b.dbl(id::auraGreen, "Aura Green", 0.35, 0.0, 1.0, 0.01, 2,
          "Share of the green layer in the aura: 0 = deep red, higher values turn it orange and yellow.", true);
    b.choice(id::halationShow, "Show Halation (diagnostic)", {"Off", "Effect Only", "Mask"}, 0,
             "DIAGNOSTIC VIEW - switch off before rendering. Effect Only: only the light halation (and Aura) adds, 8x brighter, on black. Mask: which "
             "parts of the image feed halation (red = their full share, over a dim grey picture). If several "
             "Show controls are on, the one furthest down the pipeline is shown.",
             false);

    // ---------------------------------------------------------------- [Bloom]
    b.beginGroup(id::gBloom, "Bloom", false, "Neutral diffusion of highlights.");
    b.boolean(id::bloomEnable, "Enable Bloom", true,
              "Switches bloom on or off without losing its settings. Off skips the bloom passes entirely "
              "(no render cost).",
              true);
    b.dbl(id::bloomAmount, "Bloom Amount", 0.15, 0.0, 2.0, 0.01, 2,
          "Gentle, neutral glow of bright areas from lens and emulsion scatter - softens harsh digital "
          "highlights without the warm tint of halation. Scaled by the stock. 0 = off.",
          true);
    b.dbl(id::bloomRadius, "Bloom Radius", 40.0, 5.0, 200.0, 0.5, 1,
          "Spread of the bloom in pixels at 1920 px image width (scaled automatically for other resolutions).",
          true);
    // v1.3 P1: bloom threshold and Keep Highlights (neutral defaults = the bloom as before)
    b.dbl(id::bloomThreshold, "Bloom Threshold", 0.0, 0.0, 8.0, 0.1, 1,
          "Limits the bloom to bright areas: only levels from about this many stops above 18 % grey glow "
          "(soft onset). 0 = off, every level contributes as before (diffusion is linear, so the glow follows "
          "the brightest parts anyway).",
          true);
    b.dbl(id::bloomKeepHighlights, "Bloom Keep Highlights", 0.0, 0.0, 1.0, 0.01, 2,
          "0 = energy preserving (as before): the light that spreads into the glow is taken from the bright "
          "source, so highlights soften a little and overall brightness stays the same. Higher values keep the "
          "source at full brightness and add the glow on top (brighter, more 'mist').",
          true);
    // v1.3 reference look: lens diffusion filter (append-only options; Classic = the bloom as before)
    b.choice(id::bloomFilter, "Bloom Filter",
             {"Classic", "Black Pro-Mist", "Pro-Mist (white)", "Glimmerglass", "Hollywood Black Magic", "CineBloom"}, 0,
             "Classic: the bloom as before (Bloom Amount sets it). The others emulate a diffusion filter in front "
             "of the lens, modelled from the physics of each filter type (our own model, not measured filter data): "
             "a fixed share of ALL light is scattered into a soft halo, so highlights glow, every bright edge gets "
             "a soft halo and the scattered light lifts the shadows. The share is set by Filter Grade; Bloom "
             "Amount is not used.\nBlack Pro-Mist: halo with a restrained veil, keeps the blacks fairly deep.\n"
             "Pro-Mist (white): more light in a wide veil - milky, lower contrast.\nGlimmerglass: tight, bright "
             "halo around highlights, little veil.\nHollywood Black Magic: fine, close diffusion that softens "
             "detail more than it glows.\nCineBloom: soft, medium glow without a wide tail.",
             true);
    b.dbl(id::bloomFilterGrade, "Filter Grade", 0.25, 0.125, 2.0, 0.005, 3,
          "Strength of the diffusion filter in filter grades: 0.125 = 1/8, 0.25 = 1/4, 0.5 = 1/2, 1, 2. "
          "Bloom Radius scales the size of its halo. Classic ignores it.",
          true);
    b.boolean(id::bloomShow, "Show Bloom (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows where the bloom or diffusion filter brightens the picture (lighter than grey, e.g. "
              "the shadow lift of a mist filter) or takes light from highlights (darker): grey = unchanged, white = +1 stop.",
              false);

    // ---------------------------------------------------------------- [Grain]
    b.beginGroup(id::gGrain, "Grain", true, "Film grain, generated in the film's density domain.");
    b.boolean(id::grainEnable, "Enable Grain", true,
              "Adds film grain: dye clouds of a fast (coarse) and a slow (fine) emulsion in every colour layer, "
              "developed from each layer's own exposure (granularity measured from published film data). Coarse, "
              "sparse grain lives in the shadows and lower mid-tones, finer grain in the highlights, and the film "
              "curves shape it - it sits inside the image instead of on top of it. Off skips the grain passes "
              "entirely and keeps the grain settings.",
              true);
    b.dbl(id::grainAmount, "Grain Amount", 1.0, 0.0, 3.0, 0.01, 2,
          "Grain strength. 1 = the stock's nominal granularity for the chosen Gauge and development; push "
          "processing increases it automatically.",
          true);
    b.dbl(id::grainSize, "Grain Size", 1.0, 0.25, 3.0, 0.01, 2,
          "Grain size relative to the stock and format (1 = nominal; down to 0.25 for very fine grain, which resolves "
          "best on a 4K-6K timeline). Resolution independent: it looks the same at HD, UHD and in proxy renders.",
          true);
    b.dbl(id::grainSoftness, "Grain Softness", 0.5, 0.0, 1.0, 0.01, 2,
          "0 = crisp, sharply defined dye clouds (like a sharp scan), 1 = soft clumps with soft edges (like an "
          "optical print or a softer scan). Integrate (Film Resolution) softens grain and image together.",
          true);
    b.boolean(id::grainAnimated, "Animated Grain", true,
              "On: a new grain pattern every frame, like real film. Off: a frozen pattern (useful for stills or "
              "for matching a static texture).",
              true);
    b.integer(id::grainSeed, "Grain Seed", 0, 0, 9999,
              "Changes the random grain pattern. Give different shots or layers different seeds so their grain "
              "never lines up.",
              false);
    // v1.3 grain colour and tonal balance
    b.dbl(id::grainColour, "Grain Colour", 1.0, 0.0, 1.5, 0.01, 2,
          "How independent the grain of the three colour layers is. 0 = the layers share one grain pattern "
          "(neutral, almost black-and-white grain), 1 = the stock's natural, slightly coloured grain, 1.5 = "
          "more colour in the grain (as in an underexposed or pushed negative).", true);
    b.dbl(id::grainShadows, "Grain Shadows", 1.0, 0.0, 2.0, 0.01, 2,
          "Grain strength in the shadows (about 1.5 stops under middle grey and darker), relative to the "
          "stock. 1 = nominal.", true);
    b.dbl(id::grainMids, "Grain Mids", 1.0, 0.0, 2.0, 0.01, 2,
          "Grain strength in the mid-tones, relative to the stock. 1 = nominal.", true);
    b.dbl(id::grainHighlights, "Grain Highlights", 1.0, 0.0, 2.0, 0.01, 2,
          "Grain strength in the highlights (about 2 stops over middle grey and brighter), relative to the "
          "stock. 1 = nominal.", true);
    b.boolean(id::grainShow, "Show Grain (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows only the grain the film adds, 4x stronger, around middle grey (grey = no grain). View "
              "it at 100 % zoom: where the image is brighter or darker the grain changes as on film.",
              false);

    // ---------------------------------------------------------------- [Film Resolution] (v1.2)
    b.beginGroup(id::gFilmResolution, "Film Resolution", false,
                 "The film's own sharpness, so grain and picture share one resolution.");
    b.boolean(id::filmResolutionEnable, "Enable Film Resolution", true,
              "Gives the image the modulation transfer of the camera negative for the chosen Gauge (measured from "
              "published film data: the red layer softest, blue sharpest). Digital micro-contrast is finer than "
              "any film grain; without this the grain looks laid on top of a too-sharp picture. Off skips the "
              "passes (Integrate still applies).",
              true);
    b.dbl(id::integrate, "Integrate", 0.5, 0.0, 1.0, 0.01, 2,
          "The print and scan resolution that picture AND grain share: higher values soften both together, so the "
          "grain is part of the image instead of an overlay (the colourist's 'defocus under the grain', done "
          "physically). 0 = off.",
          true);
    b.dbl(id::sharpenRemoval, "Digital Sharpening Removal", 0.0, 0.0, 1.5, 0.01, 2,
          "Undoes the edge sharpening of mirrorless cameras before the film resolution: the fine bright and dark "
          "halos along edges are pulled back, so film edges replace digital ones. 1 = removes a typical "
          "in-camera sharpening (strength 0.5 at the camera's own pixel size); frame relative, so a downscaled "
          "timeline gets correspondingly less. 0 = off.",
          true);
    b.boolean(id::filmResolutionShow, "Show Removed Detail (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows the detail Film Resolution and Digital Sharpening Removal take away, 4x stronger, "
              "around middle grey (grey = unchanged; bright and dark lines = softened edges and texture).",
              false);

    // ---------------------------------------------------------------- [Lens & Gate]
    b.beginGroup(id::gLens, "Lens & Gate", false, "Sharpness, gate weave and vignetting.");
    b.boolean(id::acutanceEnable, "Enable Acutance", true,
              "Switches acutance (Acutance, Acutance Radius) on or off without losing its settings. Off = the "
              "image's own sharpness; the passes are skipped.",
              true);
    b.dbl(id::acutanceAmount, "Acutance", 0.35, -1.0, 2.0, 0.01, 2,
          "Edge contrast from development adjacency effects: crisp, film-like apparent sharpness without the "
          "halos of digital sharpening. Negative values soften the image.",
          true);
    b.dbl(id::acutanceRadius, "Acutance Radius", 2.0, 0.5, 10.0, 0.05, 2,
          "Size of the adjacency effect in pixels at 1920 px width for Super 35 (scaled with the Gauge: the effect "
          "is a fixed distance on the film). Small = fine micro-contrast, larger = broader edge enhancement.",
          true);
    b.boolean(id::acutanceShow, "Show Acutance (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows the edge contrast acutance adds (brighter than grey) or takes away (darker), 4x "
              "stronger, around middle grey (grey = unchanged).",
              false);
    b.boolean(id::weaveEnable, "Enable Gate Weave", true,
              "Switches gate weave (Gate Weave, Weave Speed) on or off without losing its settings. Off = a rock "
              "steady frame; the pass is skipped.",
              true);
    b.dbl(id::weaveAmount, "Gate Weave", 0.0, 0.0, 2.0, 0.01, 2,
          "Subtle frame-to-frame movement of the film in the camera/projector gate, mostly vertical. 1 = typical "
          "35 mm projection; keep it low for a believable result. 0 = off.",
          true);
    b.dbl(id::weaveSpeed, "Weave Speed", 1.0, 0.25, 4.0, 0.01, 2,
          "How quickly the gate weave wanders (1 = natural; lower is a slow drift, higher a nervous jitter).",
          true);
    b.boolean(id::vignetteEnable, "Enable Vignette", true,
              "Switches the vignette on or off without losing its setting. Off = even illumination; the pass is "
              "skipped.",
              true);
    b.dbl(id::vignette, "Vignette", 0.0, 0.0, 1.0, 0.01, 2,
          "Natural lens light fall-off towards the corners, applied to the scene light before the film (so it "
          "darkens like an optical vignette, not a grey overlay). 0 = off.",
          true);
    // Lab & Gate: weave style, mechanical vignette
    b.choice(id::weaveStyle, "Weave Style", {"Projector", "Camera (Pin-Registered)", "Telecine (Float)"}, 0,
             "The kind of gate weave (Gate Weave sets its size).\nProjector: the classic 35 mm projection weave, mostly "
             "vertical, wandering slowly with a little frame-to-frame jitter.\nCamera (Pin-Registered): a registration-pin "
             "camera - tiny, almost only frame-to-frame jitter.\nTelecine (Float): a film scanned on a capstan telecine - a "
             "slow, smooth float with more sideways and rotational drift.",
             true);
    b.dbl(id::vignetteMechanical, "Mechanical Vignette", 0.0, 0.0, 1.0, 0.01, 2,
          "Mechanical vignetting (a lens hood, matte box or the lens barrel cutting the light cone): a darker rim with a "
          "defined edge, on top of the natural cos^4 fall-off (Vignette). Applied to the scene light before the film. "
          "0 = off.",
          true);
    b.dbl(id::vignetteMechSize, "Mechanical Size", 1.0, 0.5, 1.5, 0.01, 2,
          "Where the mechanical vignette starts, as a share of the half diagonal (1 = it just reaches into the corners; "
          "smaller = a tighter circle).",
          true);
    b.dbl(id::vignetteMechSoftness, "Mechanical Softness", 0.35, 0.05, 1.0, 0.01, 2,
          "Width of the mechanical vignette's edge: small = a hard edge (a hood far from the lens), large = a gentle "
          "roll-off.",
          true);

    // ---------------------------------------------------------------- [Film Gate] (Lab & Gate)
    b.beginGroup(id::gFilmGate, "Film Gate", false,
                 "The film gate's frame line as a density event on the film: aspect, rounded corners, soft edge, gate hair.");
    b.boolean(id::gateEnable, "Enable Film Gate", false,
              "Shows the edge of the film gate (the frame line). Off = the full picture, as before.", true);
    b.choice(id::gateAspect, "Gate Aspect", {"Full Frame", "1.33 Academy", "1.66 European", "1.85 Flat", "2.39 Scope",
                                             "1.43 IMAX", "Super 16 (1.66)", "1.78 (16:9)",
                                             "1.50 (35 mm Stills)", "1.00 (6x6)", "1.25 (6x7)"}, 0,
             "The aperture of the gate. Full Frame: the gate around the whole picture (only the Overscan shows). Every other "
             "aspect is fitted inside the frame (letterbox or pillarbox); the image outside it is masked like a hard matte "
             "in the gate. Super 16 also uses the smaller gauge's tighter corners.",
             true);
    b.choice(id::gateSource, "Gate", {"Camera Gate (Rebate Prints Black)", "Printer Gate (Clear Print Border)"}, 0,
             "Which gate makes the edge.\nCamera Gate: the negative outside the camera aperture stays unexposed (clear "
             "D-min), so it PRINTS BLACK.\nPrinter Gate: "
             "the print outside the printer aperture stays unexposed, so the border is the CLEAR print base (white on the "
             "screen; dark on a reversal original, which has no print).\nEither way the border stays clean: grain, halation, bloom, dust, weave and projection blur only act on the picture; the frame line stays steady while the picture weaves.",
             true);
    b.dbl(id::gateOverscan, "Overscan", 0.03, 0.0, 0.25, 0.001, 3,
          "How much of the area outside the gate is visible: the gate is inset by this share of the frame height on every "
          "side (0 = the gate edge sits exactly at the frame edge).",
          true);
    b.dbl(id::gateCorner, "Corner Radius", 14.0, 0.0, 80.0, 0.5, 1,
          "Radius of the gate's rounded corners in pixels at 1920 px width (filed aperture plates have rounded corners).",
          true);
    b.dbl(id::gateSoftness, "Edge Softness", 2.0, 0.0, 20.0, 0.1, 1,
          "Blur of the gate edge in pixels at 1920 px width: the aperture is slightly out of focus on the film.",
          true);
    b.boolean(id::gateHair, "Gate Hair", false,
              "A hair caught in the gate: a thin curved fibre reaching in from the gate edge, steady from frame to frame."
              " Camera gate: prints black; printer gate: prints clear.",
              true);
    b.integer(id::gateHairSeed, "Hair Seed", 1, 1, 9999, "Picks the shape and position of the gate hair.", true);
    b.choice(id::filmEdge, "Film Edge", {"Off", "35 mm Still Negative", "35 mm Motion Picture", "120 Medium Format"}, 0,
             "Draws the film strip around the gate, like a scan that shows the whole negative: perforations (bright, "
             "where the scanner's light passes straight through) and the edge print (frame numbers and a bar code) in "
             "the dark rebate.\n35 mm Still Negative: perforations above and below the frame (use Gate Aspect 1.50).\n"
             "35 mm Motion Picture: perforations left and right.\n120 Medium Format: no perforations, frame numbers "
             "only (Gate Aspect 1.00 or 1.25).\nSized from the real film, so raise Overscan (about 0.15 - 0.2) until "
             "the edge is in the picture. Off = the plain black border.",
             false);
    b.integer(id::frameNumber, "Frame Number", 12, 0, 99, "The frame number printed on the film edge.", false);
    b.boolean(id::gateShow, "Show Gate Mask (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows the gate's transmission: white = picture, black = gate "
              "(and hair).",
              false);

    // ---------------------------------------------------------------- [Film Damage] (v1.1)
    b.beginGroup(id::gFilmDamage, "Film Damage", false,
                 "Dust, scratches and hair on the negative and on the print - procedural, different every frame.");
    b.boolean(id::damageEnable, "Enable Film Damage", false,
              "Adds the dirt and wear of a real film copy: dust, scratches and the occasional hair. The damage is "
              "placed physically: dirt on the camera NEGATIVE holds back the printer light and prints as white "
              "specks in the print's own colour and grain, dirt on the PRINT shows dark on the screen. Off (the "
              "default) skips everything.",
              true);
    b.boolean(id::dustEnable, "Enable Dust", true,
              "Dust and dirt specks, a new pattern on every frame (each frame of film collects its own dirt).", true);
    b.dbl(id::dustAmount, "Dust Amount", 0.6, 0.0, 2.0, 0.01, 2,
          "How dirty the film is: the number of specks per frame. 0.5 = a clean projection copy with an "
          "occasional speck, 1 = a well-used print, 2 = a neglected reel.",
          true);
    b.dbl(id::dustSize, "Dust Size", 1.0, 0.5, 3.0, 0.01, 2,
          "Size of the specks and lint fibres relative to a 35 mm frame (1 = nominal). Resolution independent.",
          true);
    b.dbl(id::dustNegative, "White Dust Mix", 0.35, 0.0, 1.0, 0.01, 2,
          "Share of the dust that sat on the camera negative. Negative dirt prints as bright WHITE specks (slightly "
          "soft, out of the printer's focus); print dirt shows as sharp DARK specks. 0 = only dark specks, 1 = only "
          "white specks. Reversal (slide) film has no print: all of its dirt is dark.",
          true);
    b.boolean(id::scratchEnable, "Enable Scratches", true,
              "Vertical scratches along the film's travel, cut by grit in a camera or projector gate.", true);
    b.dbl(id::scratchAmount, "Scratch Amount", 0.5, 0.0, 2.0, 0.01, 2,
          "How many scratches run through the picture at a time (1 = one or two, 2 = a badly worn copy).", true);
    b.dbl(id::scratchPersistence, "Scratch Persistence", 0.6, 0.0, 1.0, 0.01, 2,
          "How long a scratch stays and how continuous it is. Low = short, broken flicks that come and go within "
          "a few frames; high = long continuous lines that stay for seconds, wandering slightly with the film.",
          true);
    b.dbl(id::scratchEmulsion, "Emulsion Scratches", 0.3, 0.0, 1.0, 0.01, 2,
          "Share of the scratches that cut into the emulsion instead of only the base. Base scratches scatter light "
          "and are neutral (dark on the print, white when on the negative). Emulsion scratches remove dye layers "
          "from the top and are coloured: on the print green, then yellow, then clear white as they get deeper; "
          "on the negative they print yellow, then red, then dark.",
          true);
    b.dbl(id::scratchNegative, "White Scratch Mix", 0.25, 0.0, 1.0, 0.01, 2,
          "Share of the scratches on the camera negative (printed in: white or warm-coloured lines) instead of on "
          "the print (dark or green lines).",
          true);
    b.boolean(id::hairEnable, "Enable Hair", true,
              "The occasional hair or fibre: on the film for a single frame, or caught in the projector gate - then "
              "it sticks out of the frame edge, slightly out of focus, fluttering for a few seconds.",
              true);
    b.dbl(id::hairAmount, "Hair Amount", 0.5, 0.0, 2.0, 0.01, 2,
          "How often hairs appear (1 = a hair on the film about every second and now and then one in the gate).",
          true);
    b.boolean(id::damageAnimated, "Animated Damage", true,
              "On: the damage changes from frame to frame like real film running through a projector. Off: frozen "
              "(the damage of frame 0), for stills.",
              true);
    b.integer(id::damageSeed, "Damage Seed", 0, 0, 9999,
              "Changes the random dust, scratches and hairs. Give different shots different seeds so the damage "
              "never repeats.",
              false);

    // ---------------------------------------------------------------- [Film Blur] (v1.1)
    b.beginGroup(id::gFilmBlur, "Film Blur", false,
                 "Momentary softness of projected film: focus drift and film breathing in the gate.");
    b.boolean(id::filmBlurEnable, "Enable Film Blur", false,
              "Now and then the picture goes soft and comes back, like film buckling in a warm projector gate or a "
              "focus that drifts. The blur acts on the projected image, so grain and damage soften with it. Off "
              "(the default) skips it; frames without blur cost nothing.",
              true);
    b.dbl(id::filmBlurRadius, "Blur Radius", 3.0, 0.5, 12.0, 0.05, 2,
          "How soft the picture gets at the peak of a soft moment: radius of the out-of-focus blur in pixels at "
          "1920 px width (scaled automatically for other resolutions).",
          true);
    b.dbl(id::filmBlurFrequency, "Blur Frequency", 0.3, 0.0, 1.0, 0.01, 2,
          "How often the picture goes soft: roughly the share of the time spent in a soft moment. 0 = never "
          "(only Focus Breathing), 1 = almost always drifting.",
          true)
        .animates = false;   // the event timeline is derived from it: keyframes would make the blur jump
    b.dbl(id::filmBlurDuration, "Blur Duration", 24.0, 12.0, 240.0, 1.0, 0,
          "Average length of a soft moment in frames, including easing in and out (individual moments vary from "
          "half to one and a half times this, and last at least 18 frames so they can ease in and out).",
          true)
        .animates = false;
    b.dbl(id::filmBlurBreathing, "Focus Breathing", 0.2, 0.0, 1.0, 0.01, 2,
          "A continuous, gentle in-and-out drift of focus between the soft moments (share of the Blur Radius). "
          "0 = sharp between them.",
          true);
    b.dbl(id::filmBlurChromatic, "Chromatic Softness", 0.3, 0.0, 1.0, 0.01, 2,
          "Colour of the defocus: red and especially blue go softer than green, as with a real projection lens "
          "out of focus (faint coloured fringes on edges). 0 = neutral blur.",
          true);
    b.integer(id::filmBlurSeed, "Blur Seed", 0, 0, 9999,
              "Changes when the soft moments happen. Give different shots different seeds.", false);

    // ---------------------------------------------------------------- [Film Breath] (v1.3)
    b.beginGroup(id::gFilmBreath, "Film Breath", false,
                 "Small changes from frame to frame in exposure, development and printing, as on real film.");
    b.boolean(id::breathEnable, "Enable Film Breath", false,
              "Real film never repeats exactly: the camera shutter, the lab's development and the printer "
              "lights vary a little over time. Film Breath adds these slow, smooth changes in brightness, "
              "contrast and colour balance. The same Seed always gives the same result. Off (the default) "
              "leaves every frame the same.",
              true);
    b.choice(id::breathProfile, "Breath Profile", {"Lab", "Projector", "Old Camera", "Hand-cranked"}, 0,
             "Character of the changes.\nLab: slow, gentle drift between processing batches.\nProjector: "
             "faster lamp and shutter flicker.\nOld Camera: a worn camera motor - uneven exposure with some "
             "frame-to-frame flicker.\nHand-cranked: strong, uneven flicker from frame to frame.",
             true);
    b.dbl(id::breathExposure, "Exposure Variation", 0.05, 0.0, 0.5, 0.005, 3,
          "Size of the brightness changes in stops (typical size; the profile can make it larger). 0 = none.",
          true);
    b.dbl(id::breathDevelopment, "Development Variation", 0.03, 0.0, 0.3, 0.005, 3,
          "Size of the contrast changes from the developer (share of the film contrast). 0 = none.", true);
    b.dbl(id::breathPrinter, "Printer Light Variation", 0.5, 0.0, 3.0, 0.05, 2,
          "Size of the colour balance changes in printer points (needs the Print stage). 0 = none.", true);
    b.dbl(id::breathPeriod, "Breath Period", 24.0, 4.0, 240.0, 1.0, 0,
          "Typical length of one slow change in frames. Short = restless, long = slow drift.", true)
        .animates = false;
    b.integer(id::breathSeed, "Breath Seed", 0, 0, 9999,
              "Changes the random sequence. Give different shots different seeds.", false);

    // ---------------------------------------------------------------- [Light Leaks] (v1.10)
    b.beginGroup(id::gLightLeaks, "Light Leaks", false,
                 "Stray light on the negative: warm glows from the frame edges and roll-end flashes.");
    b.boolean(id::leakEnable, "Enable Light Leaks", false,
              "Light that reached the film through a worn camera door or at the end of a roll: red-orange to yellow "
              "glows reaching in from the edges of the frame. The light is added before the film, so the film burns it "
              "like any over-exposure (it halates and grains like light). Moving pictures: leaks come and go (a fast rise, "
              "a slow fade, a little flicker). Still Photo: one leak, always there, picked by the Seed. Off (the default) "
              "adds nothing.",
              true);
    b.dbl(id::leakAmount, "Leak Amount", 1.0, 0.0, 2.0, 0.01, 2,
          "Brightness of the leaks. 1 = a strong leak that burns out where it is brightest; 0.3 = a faint warm haze.", true);
    b.dbl(id::leakFrequency, "Leak Frequency", 0.3, 0.0, 1.0, 0.01, 2,
          "How often a leak appears (moving pictures; about every 3 seconds at 24 fps there is this chance of one, in "
          "two overlapping lanes). 0 = never, 1 = nearly always one on screen.",
          true);
    b.dbl(id::leakSize, "Leak Size", 1.0, 0.3, 2.0, 0.01, 2,
          "Size of the glows (1 = about a third to most of the frame height).", true);
    b.boolean(id::leakRollEnd, "Roll-End Flashes", true,
              "Now and then (more often at a higher Frequency) a short, bright flash floods the frame from one edge, "
              "like the fogged frames at the start or end of a roll.",
              true);
    b.integer(id::leakSeed, "Leak Seed", 1, 1, 9999,
              "Picks where, when and in which colour the leaks appear. Give different shots different seeds.", false);

    // ---------------------------------------------------------------- [Output]
    const std::vector<std::string> outputs = core::outputSpaceNames();
    b.beginGroup(id::gOutput, "Output", true, "Display encoding of the result.");
    b.choice(id::outputSpace, "Output", outputs, 0,
             "The display the result is encoded for. Rec.709 Gamma 2.4 for broadcast/HD grading monitors, sRGB "
             "for web/computer displays, P3 for cinema (DCI) or Apple/P3 displays, PQ or HLG for HDR deliverables. "
             "Your timeline/output colour space should match. In Texture Only mode this is the encoding of the "
             "incoming (already graded) image.",
             false);
    b.dbl(id::hdrPeakNits, "HDR Peak (nits)", 1000.0, 300.0, 4000.0, 10.0, 0,
          "PQ output only: peak luminance of the HDR master. The film shoulder is unrolled smoothly so the "
          "brightest highlights reach this level.",
          false);
    b.boolean(id::hdrUnrollEnable, "Enable Highlight Unroll", true,
              "PQ/HLG only: switches the HDR Highlight Unroll on or off without losing its value. Off = the SDR "
              "print look placed in the HDR container unchanged (as Unroll 0).",
              false);
    b.dbl(id::hdrUnroll, "HDR Highlight Unroll", 1.0, 0.0, 1.0, 0.01, 2,
          "PQ/HLG only: 0 places the SDR print look in HDR unchanged (soft film highlights), 1 expands the print "
          "shoulder to use the full HDR range while keeping mid-tones and colour consistent with SDR.",
          false);
    b.choice(id::sceneEncoding, "Scene Encoding", core::sceneEncodingNames(), 0,
             "Pipeline Stage = Negative -> Scene / Negative Only (clean) only: the scene-referred encoding of the "
             "result. Set the next node (or Resolve's colour management) to read this encoding, then apply your "
             "display transform. ARRI LogC4 / AWG4 is the default; ACEScct for ACES pipelines, DaVinci "
             "Intermediate for DaVinci Wide Gamut timelines, Linear Rec.2020 for compositing.",
             false);
    // v1.3 P1: diagnostic view, OFF by default, never part of looks, user presets or LUT exports
    b.boolean(id::falseColourEnable, "Enable False Colour (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Replaces the image with exposure zones of the scene "
              "light that reaches the film, in stops relative to 18 % grey: purple below -5, blue -5 to -3, teal "
              "-3 to -1.5, grey -1.5 to -0.5, green +/-0.5 (grey card), pink +0.5 to +1.5 (skin), grey +1.5 "
              "to +3, yellow +3 to +4.5, orange +4.5 to +6, red above +6. Never saved in user presets and never "
              "written into exported LUTs.",
              false);
    // v1.3: diagnostic views of the texture as a whole (the modules have their own Show controls)
    b.boolean(id::textureDiffShow, "Show Texture Difference (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Shows the final picture minus the picture with every texture module off, 4x stronger, "
              "around middle grey: everything halation, bloom, grain, sharpness, weave and damage change.",
              false);
    b.boolean(id::diagWipe, "Before / After Wipe (diagnostic)", false,
              "DIAGNOSTIC VIEW - switch off before rendering. Splits the viewer: the input on the left, shown as a plain Rec.709 view "
              "(the camera through the neutral ALEXA rendering, in the output space - not the flat log), the result on the right, with a white "
              "line at Wipe Position. While a Show view is on, that view fills the whole viewer instead.",
              false);
    b.dbl(id::diagWipePosition, "Wipe Position", 0.5, 0.0, 1.0, 0.01, 2,
          "DIAGNOSTIC VIEW - where the before / after line sits, as a fraction of the image width (0 = all after, 1 = all before).",
          false);

    // ---------------------------------------------------------------- [LUT Export]
    b.beginGroup(id::gLut, "LUT Export", false, "Export the colour pipeline as a 3D LUT (.cube).");
    b.choice(id::lutSize, "LUT Size", {"17", "33", "65"}, 1,
             "Lattice size of the exported 3D LUT. 33 is the usual choice; 65 is the most accurate (larger file); "
             "17 for devices with small LUT memory.",
             false)
        .evaluateOnChange = false;
    // empty default (a per-user absolute path would be baked into the cached descriptor and into saved
    // projects, and point at a foreign home folder on another machine)
    b.str(id::lutPath, "Folder", "", StrKind::Directory,
          "Folder the LUT is written to (created if needed). Leave empty for the default folder "
          "<Documents>/Reafilm LUTs of the user who exports. File name: "
          "Reafilm_<input>_<negative>_<print>_<output>.cube. Add this folder to Resolve's LUT list "
          "(Project Settings > Color Management > Open LUT Folder) to use the LUTs elsewhere.");
    // the licensed build gates LUT export (SPEC §9); the personal build always exports
#define CEL_LUT_EXPORT_HINT                                                                                  \
    "Writes the current colour settings (Color Only: input space, exposure, stocks, development, print, "   \
    "output; no grain or other texture) as a .cube 3D LUT for on-set monitoring, cameras or other "        \
    "applications. The LUT expects the selected camera log as input (Linear Rec.2020: the file carries a " \
    "log2 shaper)."
    b.push(id::lutExport, "Export LUT", kPersonalBuild ? CEL_LUT_EXPORT_HINT : "Licensed version only. " CEL_LUT_EXPORT_HINT);
#undef CEL_LUT_EXPORT_HINT

    // ---------------------------------------------------------------- [License]
    // Personal build: the group and its parameters are still defined (IDs are permanent, saved
    // projects stay compatible) but secret, so the host does not show them.
    const size_t licenseFirst = b.defs.size();
    b.beginGroup(id::gLicense, "License", false,
                 kPersonalBuild ? "Not used in the personal build (hidden; kept so saved projects stay compatible)."
                                : "Activation.");
    b.label(id::licenseStatus, "License Status",
            kPersonalBuild ? kPersonalLicenseStatus : "Trial - renders carry a watermark",
            kPersonalBuild ? "Personal build for non-commercial use: no license key, no watermark, LUT export "
                             "always available."
                           : "Current license state. The trial includes every feature except LUT export; renders "
                             "carry a small watermark until a license key is activated.");
    {
        ParamDef& st = b.defs.back();
        st.evaluateOnChange = true;   // re-render (remove/add the watermark) when the state changes
        st.persistent = false;        // machine state, not project data; refreshed per instance
    }
    {
        ParamDef& k = b.str(id::licenseKey, "License Key", "", StrKind::SingleLine,
                            "Paste the license key you received by e-mail (starts with CELL1-), then press "
                            "Activate. The key is stored for your user account, not in the project; the field is "
                            "cleared after activation.");
        k.persistent = false;
    }
    b.push(id::licenseActivate, "Activate", "Verifies the pasted key and activates Reafilm Pro on this computer.");
    b.push(id::licenseRemove, "Deactivate",
           "Removes the license from this computer (for example before moving it to another machine). Renders "
           "return to trial mode.");
    if constexpr (kPersonalBuild)
        for (size_t i = licenseFirst; i < b.defs.size(); ++i) b.defs[i].secret = true;

    // ---------------------------------------------------------------- [About]
    b.beginGroup(id::gAbout, "About", false, "Version information.");
    b.label(id::aboutVersion, "Version", "", "Plugin version, build variant and build.")   // value set from Version.h
        .persistent = false;
    b.label(id::aboutManual, "Manual",
            "See the User Manual (MANUAL.md in the plugin bundle's Resources folder).",
            "The user manual explains every control, recommended Resolve setups (YRGB, RCM, ACES) and "
            "workflow recipes. It is installed inside the plugin bundle: ReafilmPro.ofx.bundle/Contents/Resources.");

    return std::move(b.defs);
}

} // namespace

const std::vector<ParamDef>& paramDefs() {
    static const std::vector<ParamDef> defs = build();
    return defs;
}

const std::vector<int>& realNegStocks() { return realStocks().neg; }
const std::vector<int>& realPrintStocks() { return realStocks().print; }
int realNegFor(int negStock) {
    const auto& m = realStocks().negMap;
    return negStock >= 0 && size_t(negStock) < m.size() ? m[size_t(negStock)] : 0;
}
int realPrintFor(int printStock) {
    const auto& m = realStocks().printMap;
    return printStock >= 0 && size_t(printStock) < m.size() ? m[size_t(printStock)] : 0;
}

const ParamDef* findParam(const std::string& pid) {
    for (const ParamDef& d : paramDefs())
        if (d.id == pid) return &d;
    return nullptr;
}

int choiceIndex(const ParamDef& d, const std::string& labelOrIndex) {
    int i = indexOf(d.options, labelOrIndex);
    if (i >= 0) return i;
    // decimal index
    if (labelOrIndex.empty()) return -1;
    for (char c : labelOrIndex)
        if (c < '0' || c > '9') return -1;
    long v = std::strtol(labelOrIndex.c_str(), nullptr, 10);
    return (v >= 0 && v < long(d.options.size())) ? int(v) : -1;
}

double numericDefault(const ParamDef& d) { return d.def; }

}} // namespace cel::ofxp
