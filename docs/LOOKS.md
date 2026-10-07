# Celluloid Pro - Look presets

Twelve curated looks, chosen in the **Look Preset** menu (Look group). A preset is a complete recipe: it sets
every look parameter (stocks, development, print, printer lights, halation, bloom, grain, lens and gate);
parameters a preset does not mention go back to their defaults, so a preset always produces the same image.
Input colour space, Exposure, Mode, Output, HDR, LUT export, license and the grain seed are never touched.
After any change the Preset Status line reads "<name> (modified)".

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

## 1. Studio Classic

*The house look: balanced tungsten negative on the standard projection print - natural skin, dense mid-tones, fine Super 35 grain.*

**Intent.** The reference cinema look and the best neutral starting point: a balanced camera negative printed the way a lab would deliver a first answer print.

**When to use.** Any scene, especially interiors and dialogue scenes with skin as the priority. Use it to judge exposure and lighting, then build on it.

**Key settings.** Negative CN 200T Studio Tungsten, Process Print, Print P1 Standard Projection, Halation 0.5, Hal. Threshold 3, Bloom 0.15, Format Super 35, Grain Amount 0.9, Acutance 0.3.

## 2. Tungsten Night Drama

*High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool: deep blacks, warm practicals, moody night interiors.*

**Intent.** A brooding night look: the high-speed negative pushed half a stop for density and grain, printed on the rich-contrast stock and timed slightly cool so practical lamps stay warm against blue-green shadows.

**When to use.** Night interiors and exteriors lit by tungsten practicals, thrillers, moody dialogue. On daylight scenes it reads as day-for-night-ish cool.

**Key settings.** Negative CN 500T High Speed, Push/Pull 0.5, Process Print, Print P3 Rich Contrast, Printer R 0.5, Printer B -1, Print Density 0.5, Halation 0.8, Hal. Radius 26, Hal. Threshold 3.2, Bloom 0.25, Bloom Radius 50, Format Super 35, Grain Amount 1, Grain Softness 0.55, Acutance 0.3, Vignette 0.2.

## 3. Soft Daylight Pastel

*Soft daylight negative on the low-contrast print, printed light and warm: airy pastel colour, open shadows, gentle bloom.*

**Intent.** Light, airy and forgiving: the soft daylight negative on the low-contrast print, printed 1.5 points lighter and a little warm, with a wide gentle bloom. Shadows stay open, colours turn pastel.

**When to use.** Fashion, lifestyle and romance, bright exteriors, window-lit interiors; footage with harsh contrast that needs taming.

**Key settings.** Negative CN 250D Soft Daylight, Color Density 0.95, Process Print, Print P2 Low Contrast, Printer R -0.5, Printer G 0.5, Printer B 1, Print Density 1.5, Halation 0.35, Hal. Threshold 3.5, Bloom 0.35, Bloom Radius 70, Format 35 mm Stills, Grain Amount 0.8, Grain Softness 0.7, Acutance 0.15.

## 4. Archive 1978

*Vintage negative on a faded archive print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.*

**Intent.** An aged release print: the vintage negative's golden warmth on the faded archive print (cyan dye loss, lifted brownish blacks), printed denser and warmer, with 16 mm grain, slow gate weave and lens fall-off.

**When to use.** Flashbacks, period pieces, found-footage and memory sequences. Strong by design - lower Print Density or Grain for a subtler age.

**Key settings.** Negative CN Vintage 85, Process Print, Print P4 Faded Archive, Printer R -1, Printer B 1.5, Print Density -1, Halation 0.7, Hal. Radius 28, Hal. Threshold 2.8, Hal. Hue 0.2, Bloom 0.3, Bloom Radius 60, Format 16 mm, Grain Amount 0.8, Grain Softness 0.75, Acutance 0.1, Gate Weave 0.5, Weave Speed 0.8, Vignette 0.3.

## 5. Silver Retention Thriller

*Pushed tungsten negative with partial bleach bypass on the silver-retention print: hard, metallic, desaturated contrast with crisp edges.*

**Intent.** Hard and metallic: the tungsten negative pushed one stop with a quarter bleach bypass, printed on the silver-retention print. Inky blacks, desaturated colour, bright speculars and crisp edges.

**When to use.** Action, war and crime, gritty urban exteriors, high-contrast lighting. Keep faces lit - shadows go to black quickly.

**Key settings.** Negative CN 200T Studio Tungsten, Push/Pull 1, Color Density 0.85, Neg Bleach 0.25, Process Print, Print P5 Silver Retention, Printer R 0.5, Printer B -0.5, Print Density 0.5, Halation 0.35, Hal. Threshold 3.5, Bloom 0.1, Format Super 35, Grain Amount 1.15, Grain Softness 0.4, Acutance 0.6, Acut. Radius 1.6, Vignette 0.15.

## 6. Neon Nocturne

*Fast no-backing tungsten negative for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.*

**Intent.** The no-backing tungsten negative for city nights: strong red-orange halation around neon signs, street lamps and headlights, coarse but soft grain, a touch of cool timing. Halation threshold is raised so bright skies do not glow.

**When to use.** Night city streets, bars, neon and LED signage, car interiors at night. Lower Halation Amount on very bright day scenes.

**Key settings.** Negative CN 800T No-Backing, Process Print, Print P1 Standard Projection, Printer B -0.5, Halation 0.75, Hal. Radius 24, Hal. Threshold 3.6, Hal. Hue -0.2, Bloom 0.3, Bloom Radius 45, Format Super 35, Grain Amount 0.85, Grain Softness 0.55, Acutance 0.25.

## 7. Golden Hour Portrait

*Portrait colour negative through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.*

**Intent.** Flattering skin first: the portrait colour negative through the lab scanner (flatter, no print contrast), timed warm, with soft orange-leaning halation and fine stills grain.

**When to use.** Portraits, interviews, weddings and warm-light exteriors; anything shot near sunset. Works well on slightly over-exposed footage.

**Key settings.** Negative C41 Portrait 400, Process Lab Scan, Printer R -0.5, Printer B 1, Halation 0.45, Hal. Threshold 3.2, Hal. Hue 0.3, Bloom 0.3, Bloom Radius 60, Format 35 mm Stills, Grain Amount 0.9, Grain Softness 0.6, Acutance 0.2.

## 8. Crisp Commercial

*Fine-grain daylight negative with extra colour density on the standard print: clean, saturated, sharp - restrained halation and grain.*

**Intent.** Clean and modern: the fine-grain daylight negative with 10 % extra colour density on the standard print, restrained halation and grain, tighter acutance.

**When to use.** Commercials, product and food, sports and bright daytime work where colour and sharpness should pop without looking digital.

**Key settings.** Negative CN 50D Fine Daylight, Color Density 1.1, Process Print, Print P1 Standard Projection, Halation 0.25, Hal. Threshold 3.8, Bloom 0.1, Format Super 35, Grain Amount 0.6, Grain Softness 0.4, Acutance 0.45, Acut. Radius 1.5.

## 9. Summer Snapshot

*Consumer colour negative scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.*

**Intent.** A holiday photo from the lab: the consumer colour negative scanned (open shadows, punchy warm colour), visible 35 mm stills grain and a light vignette.

**When to use.** Travel and lifestyle content, music videos, nostalgic personal stories, beach and street scenes in sun.

**Key settings.** Negative C41 Consumer 200, Process Lab Scan, Printer R -0.5, Printer B 0.5, Halation 0.45, Hal. Threshold 3, Bloom 0.25, Format 35 mm Stills, Grain Amount 1.1, Grain Softness 0.5, Acutance 0.3, Vignette 0.25.

## 10. Slide Show Brilliance

*Daylight reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.*

**Intent.** Projected slide film: the reversal stock's brilliant saturation, deep blacks and short latitude, fine stills grain and a projector-like vignette. Print settings do not apply to reversal film.

**When to use.** Saturated travel, nature and architecture in good light. Expose carefully - highlights and shadows clip sooner than on negative film.

**Key settings.** Negative RV 100 Slide, Halation 0.3, Hal. Threshold 3.5, Bloom 0.15, Format 35 mm Stills, Grain Amount 0.8, Grain Softness 0.45, Acutance 0.4, Vignette 0.3.

## 11. Monochrome Noir

*Panchromatic black & white negative pushed one stop on the rich-contrast print: inky blacks, glowing highlights, strong vignette.*

**Intent.** Classic black & white with bite: the panchromatic negative pushed one stop on the rich-contrast print, glowing bloom on highlights, Super 35 grain and a strong vignette.

**When to use.** Noir and drama, portraits with hard light, night streets. Light for separation: colour contrast becomes tonal contrast.

**Key settings.** Negative BW 400 Panchromatic, Push/Pull 1, Process Print, Print P3 Rich Contrast, Halation 0.45, Hal. Threshold 3, Bloom 0.3, Bloom Radius 55, Format Super 35, Grain Amount 1.1, Grain Softness 0.5, Acutance 0.45, Vignette 0.35.

## 12. Home Movie Super 8

*Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.*

**Intent.** A family film: reversal colour in the Super 8 gauge with big soft grain, warm halation and bloom, softened edges, nervous gate weave and heavy vignetting.

**When to use.** Home-movie inserts, memories, music videos, titles. Combine with a 4:3 crop and frame-rate reduction in Resolve for the full effect.

**Key settings.** Negative RV 100 Slide, Halation 0.8, Hal. Radius 30, Hal. Threshold 2.6, Hal. Hue 0.3, Bloom 0.45, Bloom Radius 70, Format Super 8, Grain Amount 0.9, Grain Softness 0.75, Acutance -0.15, Gate Weave 1.1, Weave Speed 1.4, Vignette 0.45.
