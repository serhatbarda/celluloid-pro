// Celluloid Pro - OFX parameter registry (see Params.h). Hints are written for colourists.
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
        std::vector<std::string> stages = core::stageNames();
        stages.push_back("Texture Only");
        stages.push_back("Negative Only (clean)");
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
                 "prints it on the Print Stock and renders the display. Camera Log and Camera Emulation are not "
                 "used.\n"
                 "Texture Only: same as Mode = Texture Only.\n"
                 "Negative Only (clean): Negative -> Scene without any texture (no halation, bloom, grain, "
                 "acutance, weave, vignette, damage) - the node that only flips the negative stock.\n"
                 "Grain, halation and damage always act in the film domain of the stage: in the negative for the "
                 "negative exits, on the scanned negative's density for Print Only. Reversal (slide) stocks have "
                 "no negative stage and always render the full chain.",
                 false);
    }

    // ---------------------------------------------------------------- [Input]
    const std::vector<std::string> inputs = core::inputSpaceNames();
    b.beginGroup(id::gInput, "Input", true, "What the camera recorded.");
    b.choice(id::inputSpace, "Camera Log", inputs, 0,
             "The log encoding and gamut of the incoming footage. Set this to match your camera (default: Fujifilm "
             "F-Log2 C / F-Gamut C). In a colour-managed Resolve project, use a timeline/working space that keeps "
             "the camera log intact for this node, or convert to the matching log space before this node. "
             "Wrong settings show up as overly flat or harsh, colour-shifted images.",
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

    // ---------------------------------------------------------------- [Camera Emulation] (v1.2)
    b.beginGroup(id::gCameraEmulation, "Camera Emulation", true,
                 "Makes the footage look as if it had been shot on another camera, before it reaches the film.");
    b.boolean(id::cameraEmulationEnable, "Enable Camera Emulation", true,
              "Switches the camera emulation on or off without losing its settings. Off = the camera's own colour "
              "as decoded from its log encoding. It only acts on footage of the source camera (Fujifilm F-Log2 C / "
              "F-Log2 / F-Log input); other cameras pass through unchanged.",
              false);
    b.choice(id::cameraEmulation, "Emulation", {"Fujifilm X-H2S -> ARRI ALEXA / AMIRA"}, 0,
             "Fujifilm X-H2S -> ARRI ALEXA / AMIRA: renders the colours the ALEXA's ALEV III sensor would have "
             "recorded of the same scene. Fitted from measured spectral responsivities (ARRI ALEXA; Fujifilm X-Trans "
             "X-T4 as the X-H2S proxy) over about 950 natural and test reflectances under daylight, tungsten, LED and "
             "fluorescent light: a smooth, exposure-independent correction in scene-linear light that leaves greys "
             "untouched. It changes skin, foliage, skies and saturated light the way the two sensors differ; colours "
             "two sensors see identically stay identical (sensor metamerism a 3-channel correction cannot undo).",
             false);
    b.dbl(id::cameraEmulationStrength, "Strength", 1.0, 0.0, 1.0, 0.01, 2,
          "Blend between the camera's own colour (0) and the full emulation (1).", false);

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
    b.choice(id::negStock, "Film Stock", negNames, negDefault < 0 ? 0 : negDefault, negHint.c_str(), true);
    b.boolean(id::developmentEnable, "Enable Development", true,
              "Switches the development controls below (Push / Pull, Color Density, Neg Bleach Bypass, Neutralize) "
              "on or off without losing their values. Off = the stock's standard development: no push or pull, "
              "nominal colour density, normal bleach, the stock's own curves. The film stock itself always stays "
              "active - it is the look.",
              true);
    b.dbl(id::pushPull, "Push / Pull", 0.0, -2.0, 3.0, 0.1, 2,
          "Development in stops. Push (+) raises contrast and effective speed, lifts fog and makes the grain "
          "coarser (about x1.25 per stop); pull (-) gives a softer, lower-contrast, finer-grained negative. "
          "Unlike Exposure this changes the curve shape, not the position on it.",
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

    // ---------------------------------------------------------------- [Print]
    const std::vector<std::string> prints = core::printStockNames();
    int printDefault = indexOf(prints, "P6 Modern Print");
    b.beginGroup(id::gPrint, "Print", true, "How the negative becomes a positive image.");
    b.choice(id::process, "Process", {"Print", "Lab Scan"}, 0,
             "Print: the negative is printed onto print stock and projected - the classic cinema look with dense "
             "blacks and rich colour.\nLab Scan: the negative is scanned and inverted by a lab scanner - a flatter, "
             "more neutral rendering that keeps the negative's character without print-stock contrast.\n"
             "Reversal (slide) stocks are direct positives and ignore this setting.",
             true);
    b.choice(id::printStock, "Print Stock", prints, printDefault < 0 ? 0 : printDefault,
             "The print film the negative is printed on (Process = Print). It sets print contrast, black density, "
             "highlight roll-off and the colour of the projected image.",
             true);
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

    // ---------------------------------------------------------------- [Halation]
    b.beginGroup(id::gHalation, "Halation", true, "Red-orange glow around bright light sources.");
    b.boolean(id::halationEnable, "Enable Halation", true,
              "Switches halation on or off without losing its settings. Off skips the halation passes entirely "
              "(no render cost).",
              true);
    b.dbl(id::halationAmount, "Halation Amount", 0.5, 0.0, 2.0, 0.01, 2,
          "Strength of halation: light that passes through the emulsion, reflects off the film base and re-exposes "
          "the red layer, creating a warm glow that hugs bright edges and practicals. Scaled by the stock "
          "(no-backing stocks glow much more). 0 = off.",
          true);
    b.dbl(id::halationRadius, "Halation Radius", 20.0, 2.0, 80.0, 0.5, 1,
          "Spread of the glow in pixels at 1920 px image width (automatically scaled for other resolutions "
          "and proxy render scales).",
          true);
    b.dbl(id::halationThreshold, "Halation Threshold", 3.5, 0.0, 6.0, 0.05, 2,
          "How bright a highlight must be before it halates, in stops above middle grey. The glow eases in over "
          "about a stop and a half above it, so broad bright areas (an overcast sky) barely halate while light "
          "sources and speculars glow fully. Lower values let more of the image glow.",
          true);
    b.dbl(id::halationHue, "Halation Hue", 0.0, -1.0, 1.0, 0.01, 2,
          "Colour of the glow: -1 deep red, 0 the classic red-orange, +1 orange-yellow.", true);

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

    // ---------------------------------------------------------------- [Grain]
    b.beginGroup(id::gGrain, "Grain", true, "Film grain, generated in the film's density domain.");
    b.boolean(id::grainEnable, "Enable Grain", true,
              "Adds film grain. Grain is added to the dye densities of the negative, so it behaves like real "
              "grain: strongest in the mid-tones and shadows of the negative, varying per colour layer. Off skips "
              "the grain passes entirely and keeps the grain settings.",
              true);
    b.choice(id::grainFormat, "Grain Format", {"Super 8", "16 mm", "Super 35", "65 mm", "35 mm Stills"}, 2,
             "Film gauge the image was 'shot' on. Smaller formats enlarge the grain more: Super 8 is very coarse, "
             "16 mm clearly visible, Super 35 the cinema standard, 65 mm very fine; 35 mm Stills (36x24 full "
             "frame) is finer than Super 35.",
             true);
    b.dbl(id::grainAmount, "Grain Amount", 1.0, 0.0, 3.0, 0.01, 2,
          "Grain strength. 1 = the stock's nominal granularity for the chosen format and development; push "
          "processing increases it automatically.",
          true);
    b.dbl(id::grainSize, "Grain Size", 1.0, 0.5, 3.0, 0.01, 2,
          "Grain size relative to the stock and format (1 = nominal). Resolution independent: it looks the same "
          "at HD, UHD and in proxy renders.",
          true);
    b.dbl(id::grainSoftness, "Grain Softness", 0.5, 0.0, 1.0, 0.01, 2,
          "0 = crisp, sharply defined grains (like a sharp scan), 1 = soft clumps (like an optical print or a "
          "softer scan).",
          true);
    b.boolean(id::grainAnimated, "Animated Grain", true,
              "On: a new grain pattern every frame, like real film. Off: a frozen pattern (useful for stills or "
              "for matching a static texture).",
              true);
    b.integer(id::grainSeed, "Grain Seed", 0, 0, 9999,
              "Changes the random grain pattern. Give different shots or layers different seeds so their grain "
              "never lines up.",
              false);

    // ---------------------------------------------------------------- [Lens & Gate]
    b.beginGroup(id::gLens, "Lens & Gate", false, "Sharpness, gate weave and vignetting.");
    b.boolean(id::acutanceEnable, "Enable Acutance", true,
              "Switches acutance (Acutance, Acutance Radius) on or off without losing its settings. Off = the "
              "image's own sharpness; the passes are skipped.",
              true);
    b.dbl(id::acutanceAmount, "Acutance", 0.3, -1.0, 2.0, 0.01, 2,
          "Edge contrast from development adjacency effects: crisp, film-like apparent sharpness without the "
          "halos of digital sharpening. Negative values soften the image.",
          true);
    b.dbl(id::acutanceRadius, "Acutance Radius", 2.0, 0.5, 10.0, 0.05, 2,
          "Size of the adjacency effect in pixels at 1920 px width. Small = fine micro-contrast, larger = broader "
          "edge enhancement.",
          true);
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
          "<Documents>/Celluloid LUTs of the user who exports. File name: "
          "Celluloid_<input>_<negative>_<print>_<output>.cube. Add this folder to Resolve's LUT list "
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
    b.push(id::licenseActivate, "Activate", "Verifies the pasted key and activates Celluloid Pro on this computer.");
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
            "workflow recipes. It is installed inside the plugin bundle: Celluloid.ofx.bundle/Contents/Resources.");

    return std::move(b.defs);
}

} // namespace

const std::vector<ParamDef>& paramDefs() {
    static const std::vector<ParamDef> defs = build();
    return defs;
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
