# Celluloid Pro - Look presets

Fourteen curated looks, chosen in the **Look Preset** menu (Look group). A preset is a complete recipe: it sets
every look parameter (stocks, development, print, printer lights, halation, bloom, grain, lens and gate, film
damage and film blur);
parameters a preset does not mention go back to their defaults, so a preset always produces the same image.
Input colour space, Exposure, Mode, Output, HDR, LUT export, license and the grain / damage / blur seeds are never
touched.
After any change the Preset Status line reads "<name> (modified)".

**Module switches (v1.1).** Every module has an Enable checkbox (Development, Print Timing, Halation,
Bloom, Grain, Acutance, Gate Weave, Vignette). The switches of the development, print and texture modules are
part of a preset like the module's sliders. The twelve original looks keep every one of these modules
switched on: a module a look does not use sits at its neutral value (for example Vignette 0), exactly as in
version 1.0, so a project saved with 1.0 that uses a look still shows the look's name without "(modified)"
after the update. Switching a module off in a look marks it "(modified)" like any other edit. The new Film Damage
and Film Blur modules are off by default, so the twelve original looks show no damage; looks 13 and 14 switch them
on.

All looks are original designs made for this plug-in on our own stocks; none imitates a particular film
title, production or commercial product. They were designed and checked on real-world-like F-Log2 C test
scenes (skin under tungsten and daylight at -2/0/+2 stops, golden hour, night city with neon and sodium light,
foliage, overcast exterior, high-key and low-key interiors) rendered through the real plug-in.

Contact sheet: [`docs/validation/presets.png`](validation/presets.png) (regenerate with
`python3 tools/look/preset_sheet.py --dev-license`; scenes from `tools/look/lookscenes.py`).

**Working with presets.** Set the Camera Log first and expose so that a grey card sits at 0 Exposure; then
pick a look. Fine-tune with Exposure (moves the image along the film curve), Print Density (brighter / denser
print) and the printer lights (colour balance in the lab's own units: +1 point = 0.025 log E; fewer blue
points = cooler, more = warmer/yellower). Printer points are relative to each stock's automatic grey balance,
so the same numbers mean the same correction on every stock. Order of the menu is permanent (projects store the
index): new looks will be appended.

| # | Preset | Negative | Print / process | Character |
|---|---|---|---|---|
| 1 | **Studio Classic** | CN 200T Studio Tungsten | P1 Standard Projection | The house look |
| 2 | **Tungsten Night Drama** | CN 500T High Speed | P3 Rich Contrast | High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool |
| 3 | **Soft Daylight Pastel** | CN 250D Soft Daylight | P2 Low Contrast | Soft daylight negative on the low-contrast print, printed light and warm |
| 4 | **Archive 1978** | CN Vintage 85 | P4 Faded Archive | Vintage negative on a faded archive print |
| 5 | **Silver Retention Thriller** | CN 200T Studio Tungsten | P5 Silver Retention | Pushed tungsten negative with partial bleach bypass on the silver-retention print |
| 6 | **Neon Nocturne** | CN 800T No-Backing | P1 Standard Projection | Fast no-backing tungsten negative for city nights |
| 7 | **Golden Hour Portrait** | C41 Portrait 400 | Lab Scan | Portrait colour negative through the lab scanner, timed warm |
| 8 | **Crisp Commercial** | CN 50D Fine Daylight | P1 Standard Projection | Fine-grain daylight negative with extra colour density on the standard print |
| 9 | **Summer Snapshot** | C41 Consumer 200 | Lab Scan | Consumer colour negative scanned like a lab print, timed warm |
| 10 | **Slide Show Brilliance** | RV 100 Slide | reversal (direct) | Daylight reversal (slide) film projected directly |
| 11 | **Monochrome Noir** | BW 400 Panchromatic | P3 Rich Contrast | Panchromatic black & white negative pushed one stop on the rich-contrast print |
| 12 | **Home Movie Super 8** | RV 100 Slide | reversal (direct) | Reversal film in the Super 8 gauge |
| 13 | **Worn Release Print** | CN 500T High Speed | P1 Standard Projection | A well-run 35 mm release print, timed warm, with light film damage (v1.1) |
| 14 | **Found Footage 16** | CN 250D Soft Daylight | P4 Faded Archive | A battered, faded 16 mm reel with heavy film damage and drifting focus (v1.1) |

## 1. Studio Classic

*The house look: balanced tungsten negative on the standard projection print - natural skin, dense mid-tones, fine Super 35 grain.*

**Intent.** The reference cinema look and the best neutral starting point: a balanced camera negative printed the way a lab would deliver a first answer print.

**When to use.** Any scene, especially interiors and dialogue scenes with skin as the priority. Use it to judge exposure and lighting, then build on it.

**Key settings.** Negative CN 200T Studio Tungsten, Process Print, Print P1 Standard Projection, Halation 0.5, Bloom 0.15, Format Super 35, Grain Amount 0.9, Acutance 0.3.

## 2. Tungsten Night Drama

*High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool: deep blacks, warm practicals, moody night interiors.*

**Intent.** A brooding night look: the high-speed negative pushed half a stop for density and grain, printed on the rich-contrast stock and timed slightly cool so practical lamps stay warm against blue-green shadows.

**When to use.** Night interiors and exteriors lit by tungsten practicals, thrillers, moody dialogue. On daylight scenes it reads as day-for-night-ish cool.

**Key settings.** Negative CN 500T High Speed, Push/Pull 0.5, Process Print, Print P3 Rich Contrast, Printer R 0.5, Printer B -1, Print Density 0.5, Halation 0.8, Hal. Radius 26, Bloom 0.25, Bloom Radius 50, Format Super 35, Grain Amount 1, Grain Softness 0.55, Acutance 0.3, Vignette 0.2.

## 3. Soft Daylight Pastel

*Soft daylight negative on the low-contrast print, printed light and warm: airy pastel colour, open shadows, gentle bloom.*

**Intent.** Light, airy and forgiving: the soft daylight negative on the low-contrast print, printed half a point lighter and a little warm, with a wide gentle bloom. Shadows stay open, colours turn pastel, and whites still reach a clean bright white (the low contrast comes from P2's soft toe and mids, not from a grey ceiling).

**When to use.** Fashion, lifestyle and romance, bright exteriors, window-lit interiors; footage with harsh contrast that needs taming.

**Key settings.** Negative CN 250D Soft Daylight, Color Density 1, Process Print, Print P2 Low Contrast, Printer R -0.5, Printer G 0.5, Printer B 1, Print Density 0.5, Halation 0.35, Bloom 0.25, Bloom Radius 70, Format 35 mm Stills, Grain Amount 0.8, Grain Softness 0.7, Acutance 0.15.

## 4. Archive 1978

*Vintage negative on a faded archive print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.*

**Intent.** An aged release print: the vintage negative's golden warmth on the faded archive print (cyan dye loss, lifted brownish blacks), printed denser and warmer, with 16 mm grain, slow gate weave and lens fall-off.

**When to use.** Flashbacks, period pieces, found-footage and memory sequences. Strong by design - lower Print Density or Grain for a subtler age.

**Key settings.** Negative CN Vintage 85, Process Print, Print P4 Faded Archive, Printer R -1, Printer B 1.5, Print Density -1, Halation 0.7, Hal. Radius 28, Hal. Hue 0.2, Bloom 0.3, Bloom Radius 60, Format 16 mm, Grain Amount 0.8, Grain Softness 0.75, Acutance 0.1, Gate Weave 0.4, Weave Speed 0.8, Vignette 0.3.

## 5. Silver Retention Thriller

*Pushed tungsten negative with partial bleach bypass on the silver-retention print: hard, metallic, desaturated contrast with crisp edges.*

**Intent.** Hard and metallic: the tungsten negative pushed half a stop with a light (15 %) bleach bypass, printed on the silver-retention print. Inky blacks, desaturated colour, bright speculars and crisp edges.

**When to use.** Action, war and crime, gritty urban exteriors, high-contrast lighting. Keep faces lit - shadows below about -4 stops go to black (P5's deep D-max).

**Key settings.** Negative CN 200T Studio Tungsten, Push/Pull 0.5, Color Density 0.85, Neg Bleach 0.15, Process Print, Print P5 Silver Retention, Printer R 0.5, Printer B -0.5, Print Density 0.5, Halation 0.35, Bloom 0.1, Format Super 35, Grain Amount 1.15, Grain Softness 0.4, Acutance 0.6, Acut. Radius 1.6, Vignette 0.15.

## 6. Neon Nocturne

*Fast no-backing tungsten negative for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.*

**Intent.** The no-backing tungsten negative for city nights: strong red-orange halation around neon signs, street lamps and headlights, coarse but soft grain, a touch of cool timing, printed half a point denser so neon cores stay saturated. Halation threshold is raised so bright skies do not glow.

**When to use.** Night scenes only: city streets, bars, neon and LED signage, car interiors at night. On day scenes it prints dense and glows; use Studio Classic instead.

**Key settings.** Negative CN 800T No-Backing, Process Print, Print P1 Standard Projection, Printer B -0.5, Print Density -0.5, Halation 0.6, Hal. Radius 24, Hal. Hue -0.2, Bloom 0.3, Bloom Radius 45, Format Super 35, Grain Amount 0.85, Grain Softness 0.55, Acutance 0.25.

## 7. Golden Hour Portrait

*Portrait colour negative through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.*

**Intent.** Flattering skin first: the portrait colour negative through the lab scanner (flatter, no print contrast), timed warm, with soft orange-leaning halation and fine stills grain.

**When to use.** Portraits, interviews, weddings and warm-light exteriors; anything shot near sunset. Works well on slightly over-exposed footage.

**Key settings.** Negative C41 Portrait 400, Process Lab Scan, Printer R -0.5, Printer B 1, Halation 0.45, Hal. Hue 0.3, Bloom 0.3, Bloom Radius 60, Format 35 mm Stills, Grain Amount 0.9, Grain Softness 0.6, Acutance 0.2.

## 8. Crisp Commercial

*Fine-grain daylight negative with extra colour density on the standard print: clean, saturated, sharp - restrained halation and grain.*

**Intent.** Clean and modern: the fine-grain daylight negative with 10 % extra colour density on the standard print, restrained halation and grain, tighter acutance.

**When to use.** Commercials, product and food, sports and bright daytime work where colour and sharpness should pop without looking digital.

**Key settings.** Negative CN 50D Fine Daylight, Color Density 1.1, Process Print, Print P1 Standard Projection, Halation 0.25, Bloom 0.1, Format Super 35, Grain Amount 0.6, Grain Softness 0.4, Acutance 0.45, Acut. Radius 1.5.

## 9. Summer Snapshot

*Consumer colour negative scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.*

**Intent.** A holiday photo from the lab: the consumer colour negative scanned (open shadows, punchy warm colour), visible 35 mm stills grain and a light vignette.

**When to use.** Travel and lifestyle content, music videos, nostalgic personal stories, beach and street scenes in sun.

**Key settings.** Negative C41 Consumer 200, Process Lab Scan, Printer R -0.5, Printer B 0.5, Halation 0.45, Bloom 0.25, Format 35 mm Stills, Grain Amount 1.1, Grain Softness 0.5, Acutance 0.3, Vignette 0.25.

## 10. Slide Show Brilliance

*Daylight reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.*

**Intent.** Projected slide film: the reversal stock's brilliant saturation, deep blacks and short latitude, crisp, clearly visible stills grain and a projector-like vignette. Print settings do not apply to reversal film.

**When to use.** Saturated travel, nature and architecture in good light. Expose carefully - highlights and shadows clip sooner than on negative film.

**Key settings.** Negative RV 100 Slide, Halation 0.3, Bloom 0.15, Format 35 mm Stills, Grain Amount 1.2, Grain Softness 0.45, Acutance 0.4, Vignette 0.3.

## 11. Monochrome Noir

*Panchromatic black & white negative pushed one stop on the rich-contrast print: inky blacks, glowing highlights, strong vignette.*

**Intent.** Classic black & white with bite: the panchromatic negative pushed one stop on the rich-contrast print, glowing bloom on highlights, Super 35 grain and a strong vignette.

**When to use.** Noir and drama, portraits with hard light, night streets. Light for separation: colour contrast becomes tonal contrast.

**Key settings.** Negative BW 400 Panchromatic, Push/Pull 1, Process Print, Print P3 Rich Contrast, Halation 0.45, Bloom 0.3, Bloom Radius 55, Format Super 35, Grain Amount 1.1, Grain Softness 0.5, Acutance 0.45, Vignette 0.35.

## 12. Home Movie Super 8

*Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.*

**Intent.** A family film: reversal colour in the Super 8 gauge, pushed 1.5 stops with strong interlayer effects for dense, bold colour that is clearly its own (not the Slide Show look), with big soft grain, warm halation and bloom, softened edges, nervous gate weave and heavy vignetting.

**When to use.** Home-movie inserts, memories, music videos, titles. Combine with a 4:3 crop and frame-rate reduction in Resolve for the full effect.

**Key settings.** Negative RV 100 Slide, Push/Pull 1.5, Color Density 1.9, Halation 0.8, Hal. Radius 30, Hal. Hue 0.3, Bloom 0.45, Bloom Radius 70, Format Super 8, Grain Amount 1.6, Grain Softness 0.6, Acutance -0.15, Gate Weave 1.1, Weave Speed 1.4, Vignette 0.45.

## 13. Worn Release Print

*A well-run 35 mm release print, timed warm: high-speed negative on the standard print with a few dark specks, the odd white negative speck, a persistent print scratch now and then, gentle gate weave and a rare moment of soft focus.*

**Intent.** The cinema experience of a print that has been projected a few dozen times: the picture is clean enough to watch, but it is unmistakably film running through a projector. Timed slightly warm and magenta like many release prints, with mild gate weave and the occasional soft moment.

**When to use.** Period pieces, flashbacks, "screening room" moments, trailers and title sequences. Give each shot its own Damage Seed so the dirt never repeats across a cut.

**Key settings.** Negative CN 500T High Speed, Print P1 Standard Projection, Printer R +1 / G +0.5 / B -0.5, Print Density 0.5, Halation 0.6, Bloom 0.2, Format Super 35, Grain Amount 1, Acutance 0.25, Gate Weave 0.6, Vignette 0.2; Film Damage on: Dust 0.7 (White Dust Mix 0.3), Scratches 0.5 (Persistence 0.75, Emulsion 0.35, White Scratch Mix 0.2), Hair 0.35; Film Blur on: Radius 2.5, Frequency 0.12, Duration 30, Breathing 0.15, Chromatic 0.3.

## 14. Found Footage 16

*A forgotten 16 mm reel: daylight negative on the faded print, its cyan dye gone warm-magenta, coarse grain, nervous weave, lots of dust and lint, flickering scratches, hairs in the gate and focus that drifts in and out.*

**Intent.** Footage that has been through a lot: a faded print whose colour has drifted toward warm magenta with lifted blacks, 16 mm grain, an unsteady gate, plenty of dirt from both the negative (white) and the print (dark), short flickering scratches and a projector whose focus wanders.

**When to use.** Found-footage and documentary-archive inserts, music videos, memories, horror. Lower Dust and Scratch Amount for a cleaner copy; set Film Blur Frequency to 0 if the soft moments distract.

**Key settings.** Negative CN 250D Soft Daylight, Print P4 Faded Archive, Printer R -1.5 / G +1, Print Density -1.5, Halation 0.8, Hal. Radius 30, Hal. Hue 0.25, Bloom 0.35, Bloom Radius 60, Format 16 mm, Grain Amount 1, Grain Softness 0.7, Acutance 0, Gate Weave 1 at Speed 1.2, Vignette 0.4; Film Damage on: Dust 1.4 (Size 1.2, White Dust Mix 0.4), Scratches 1.2 (Persistence 0.4, Emulsion 0.5, White Scratch Mix 0.3), Hair 0.9; Film Blur on: Radius 4, Frequency 0.3, Duration 36, Breathing 0.35, Chromatic 0.5.

## 15. Studio Modern

The v1.2 default look as a preset. Kodak Vision3 250D 5207 on P6 Modern Print: dense lower mids, true
blacks, a soft ARRI-like highlight roll-off, calm cyan-blue skies and warm, low-chroma skin. Fine Super 35
grain (0.7), light halation (0.4) and bloom (0.12). Use it as the starting point for modern digital
footage; with Fujifilm footage the camera emulation (not part of the preset) adds the ALEXA colour.

## 16. Soft Cinema

ETERNA-style softness: Fujifilm Eterna 250D 8563 on Eterna-CP 3513DI. Gentle contrast, restrained colour,
open shadows, a quiet, slightly cool palette. Softer grain (0.6, softness 0.65), soft bloom. For drama,
interiors, and anything that should feel calm rather than punchy.

## 17. Warm Print

The photochemical 2383 chain: Kodak Vision3 250D 5207 on Kodak Vision 2383 with Print Crossover 0.5 -
golden highlights, cool teal-leaning shadows, rich print contrast. Grain 0.9, halation 0.55.

Colour distance (chart + skin, colour only, mean dE2000) to the nearest other preset: Studio Modern 4.6
(Studio Classic), Soft Cinema 4.3 (Warm Print), Warm Print 4.3 (Soft Cinema); all >= 2 as required.

## Revision notes (v1.1)

* Halation no longer floods dark edges next to bright skies (new energy model, default threshold 2.5 -> 3.5
  stops); preset thresholds were raised with it (Studio Classic 3 -> 3.5, Tungsten Night Drama 3.2 -> 3.6,
  Archive 1978 2.8 -> 3.2, Summer Snapshot 3 -> 3.5, Home Movie Super 8 2.6 -> 3). On the overcast window scene
  every preset keeps the window frame within d(R-G) <= 0.021.
* Soft Daylight Pastel: Print Density 1.5 -> 0.5, Color Density 0.95 -> 1, Bloom 0.35 -> 0.25, on the re-shaped
  P2 print: whites reach 0.93 at +6 stops instead of a grey ceiling.
* Silver Retention Thriller: Push/Pull 1 -> 0.5, Neg Bleach 0.25 -> 0.15 (-4 stops 0.031 -> 0.036).
* Neon Nocturne: Print Density 0 -> -0.5, Halation 0.75 -> 0.6, on the re-shaped CN 800T (P1 +6 stops
  0.86 -> 0.96). Night scenes only.
* Slide Show Brilliance: Grain Amount 0.8 -> 1.2 (and RV 100's base grain is now that of a normal-speed
  colour stock).
* Home Movie Super 8: Push/Pull 1.5, Color Density 1.6, Grain Amount 0.9 -> 1.6, Grain Softness 0.75 -> 0.6,
  Hal. Threshold 2.6 -> 3: its own bold colour, clearly grainier than every 35 mm look.
* Every pair of presets now differs in colour alone (texture off, colour-only chart) by a mean CIEDE2000 >= 2
  (render test `presets`), so no two looks share the same colour.
* Two looks appended for film damage (feature round 1.1): Worn Release Print and Found Footage 16 (above). Every
  other preset switches Film Damage and Film Blur off (presets reset unlisted look parameters to their defaults).

## Revision notes (v1.2)

* Home Movie Super 8: Color Density 1.6 -> 1.9. Under the v1.2 hue hold it had drifted to 1.80 mean
  dE2000 from Slide Show Brilliance (colour-only chart); it is now 2.32 (>= 2 required).
* Warm Print: Print Crossover now keeps each print layer's fog and D-max (endpoint-preserving pivot,
  src/core/Core.cpp printCrossover). The earlier linear scale-and-clamp made the red channel of a grey
  ramp turn down by ~4e-5 near +5 stops (render test `presets`: monotonic). The look is unchanged near
  middle grey; the warm/cool split now fades out toward white and black.

## Revision notes (v1.2, texture)

* Every preset uses the new physical halation (their Hal. Threshold settings are gone, including those
  of Studio Modern, Soft Cinema and Warm Print: threshold 0 = a thin warm ring around every bright edge,
  strongest for practicals and speculars; flat bright areas stay clean and a bright overcast sky does
  not tint the dark objects in front of it). Halation amounts, radii and hues are unchanged; the ring radius now follows the gauge
  (16 mm presets get about 1.4x the Super 35 radius, Super 8 about 2x).
* Archive 1978: Gate Weave 0.5 -> 0.4 (<= 0.3 px rms at 1920 width). Home Movie Super 8, Worn Release
  Print and Found Footage 16 keep their deliberate weave.
* Presets do not set Film Resolution or Integrate, so every preset gets the defaults (on, 0.5): the
  negative's own sharpness for its gauge and a shared print/scan resolution for picture and grain.
* A project saved with v1.1 that uses a preset reads "(modified)" if it stored the old halation
  threshold.
