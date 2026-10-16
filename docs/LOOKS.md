# Reafilm Pro - Look presets

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

Contact sheet: [`docs/validation/presets.png`](validation/presets.png) (made with the private look-dev tools).

**Working with presets.** Set the Camera Log first and expose so that a grey card sits at 0 Exposure; then
pick a look. Fine-tune with Exposure (moves the image along the film curve), Print Density (brighter / denser
print) and the printer lights (colour balance in the lab's own units: +1 point = 0.025 log E; fewer blue
points = cooler, more = warmer/yellower). Printer points are relative to each stock's automatic grey balance,
so the same numbers mean the same correction on every stock. Order of the menu is permanent (projects store the
index); since 1.4 the menu is one preset per real camera stock.

Since 1.3 (real film only) every preset uses a real data-sheet stock: the designed stocks were
replaced by their nearest real stock, and the faded and silver-retention prints are Kodak 2383 with
the print controls. The intent text below was written for the designed stocks; the looks keep that
intent.

| # | Preset | Negative | Print / process | Character |
|---|---|---|---|---|
| 0 | **Signature 250D / 2383** | Kodak Vision3 250D 5207 | Kodak Vision 2383 (release print) | The owner's own setup, first in the menu (1.3) |
| 1 | **Crisp Commercial** | Kodak Vision3 50D 5203 | Kodak Vision 2383 |  |
| 2 | **Studio Modern** | Kodak Vision3 200T 5213 | Fujifilm Eterna-CP 3521XD | 
             |
| 3 | **Soft Cinema** | Fujifilm Eterna 250D 8563 | Fujifilm Eterna-CP 3513DI | 
             |
| 4 | **Warm Print** | Kodak Vision2 500T 5218 | Kodak Vision 2383 | 
             |
| 5 | **Sunlit 35mm (soft scan)** | Fujifilm Eterna Vivid 160 8543 | Kodak Vision 2383 (Telecine / Scan) | 
            
             |
| 6 | **Theatrical 2383 Warm** | Kodak Vision2 Expression 500T 5229 | Kodak Vision 2383 (Release Print (IP / IN)) | 
            
             |
| 7 | **Scope Night 500T / 2393** | Kodak Vision3 500T 5219 | Kodak Vision Premier 2393 | 
             |
| 8 | **Portra 400 Editorial** | Kodak Portra 400 | Lab Scan | 
             |
| 9 | **Super 8 Ektachrome Diary** | Kodak Ektachrome 100D 5294 | reversal (direct) | 
             |
| 10 | **E100 Slide Projector** | Kodak Ektachrome E100 | reversal (direct) | 
             |
| 11 | **Kodachrome Travelogue** | Kodachrome 64 Style | reversal (direct) | 
             |
| 12 | **Double-X Silver Print** | Kodak Double-X 5222 | Kodak Vision 2383 (Release Print (IP / IN)) | 
             |
| 13 | **Eterna 500 Skip Bleach** | Fujifilm Eterna 500 8573 | Fujifilm Eterna-CP 3521XD | 
             |
| 14 | **Reala Green Day** | Fujifilm Reala 500D 8592 | Fujifilm Eterna-CP 3513DI (Telecine / Scan) | 
             |

## 0. Signature 250D / 2383

**Intent.** The owner's own X-H2S setup: texture, feel and depth. VISION3 250D on the 2383 release print
(IP / IN), ALEXA Colour 0.5, Filmic Contrast 1.047, a long soft roll-off, a warm colour head and steel-blue
shadow toning, with Print Crossover 0.4.

**Film edge (shared since 1.3).** Integrate 0 and Digital Sharpening Removal 1.5 take the digital edge off
the footage, Acutance Radius 0.5 puts a fine film edge back, and a soft Aura (50 / 0.15) adds a wide glow
around a tight halation. Every other preset gets this edge unless it sets its own.

**Key settings.** Gauge 65 mm, ALEXA Colour on 0.5, Filmic Contrast 1.047, Roll-off Start 1.5 / Softness 1,
Dye Saturation off, Negative Kodak Vision3 250D 5207, Print Kodak Vision 2383, Print Crossover 0.4,
Rendering Release Print (IP / IN), Head Cyan 2.5, Head Yellow -3, Shadow Toning 0.5, Halation 0.76 at Radius
2, Aura 50 / 0.15, Bloom Radius 20, Glimmerglass at grade 1/8, Grain Size 0.5, Grain Softness 0, Integrate
0, Digital Sharpening Removal 1.5, Acutance Radius 0.5, Gate Weave 0.25, Vignette 0.15. Exposure, Scene
White Balance and Highlight Latitude are per shot and not part of any preset (the owner uses Exposure -0.5,
5800 K and Highlight Latitude on).

## 1. Crisp Commercial

**Intent.** 

**Key settings.** `negStock` Kodak Vision3 50D 5203, `interlayer` 1.1, `process` Print, `printStock` Kodak Vision 2383, `halationAmount` 0.25, `bloomAmount` 0.1, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.6, `grainSoftness` 0.4, `acutanceAmount` 0.45, `acutanceRadius` 1.5.

## 2. Studio Modern

**Intent.** 
            

**Key settings.** `negStock` Kodak Vision3 200T 5213, `process` Print, `printStock` Fujifilm Eterna-CP 3521XD, `dyeSaturation` 0.75, `rolloffSoftness` 0.3, `halationAmount` 0.4, `bloomAmount` 0.12, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.7, `grainSoftness` 0.55, `acutanceAmount` 0.25.

## 3. Soft Cinema

**Intent.** 
            

**Key settings.** `negStock` Fujifilm Eterna 250D 8563, `process` Print, `printStock` Fujifilm Eterna-CP 3513DI, `halationAmount` 0.3, `bloomAmount` 0.2, `bloomRadius` 60, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.6, `grainSoftness` 0.65, `acutanceAmount` 0.15.

## 4. Warm Print

**Intent.** 
            

**Key settings.** `negStock` Kodak Vision2 500T 5218, `process` Print, `printStock` Kodak Vision 2383, `printCrossover` 0.5, `toneShadowAmount` 0.2, `halationAmount` 0.55, `bloomAmount` 0.18, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.9, `grainSoftness` 0.5, `acutanceAmount` 0.3.

## 5. Sunlit 35mm (soft scan)

**Intent.** 
            
            

**Key settings.** `negStock` Fujifilm Eterna Vivid 160 8543, `process` Print, `printStock` Kodak Vision 2383, `printRender` Telecine / Scan, `printAim` Soft, `printCrossover` 1, `printerR` 0.5, `printerG` -1, `printerB` 2, `filmContrast` 0.88, `rolloffSoftness` 0.4, `dyeSaturation` 0.9, `toneHighlightHue` 55, `toneHighlightAmount` 0.2, `bloomFilter` Black Pro-Mist, `bloomFilterGrade` 0.125, `halationAmount` 0.7, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.75, `grainSoftness` 0.55, `integrate` 0.5, `sharpenRemoval` 0.75, `acutanceAmount` 0.15, `weaveStyle` Telecine (Float), `weaveAmount` 0.15, `vignette` 0.1.

## 6. Theatrical 2383 Warm

**Intent.** 
            
            

**Key settings.** `negStock` Kodak Vision2 Expression 500T 5229, `process` Print, `printStock` Kodak Vision 2383, `printRender` Release Print (IP / IN), `printCrossover` 0.4, `headCyan` 5, `headYellow` -6, `toneHighlightHue` 50, `toneHighlightAmount` 0.35, `halationAmount` 0.55, `bloomAmount` 0.15, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.9, `grainSoftness` 0.5, `acutanceAmount` 0.3, `weaveAmount` 0.25, `vignette` 0.15.

## 7. Scope Night 500T / 2393

**Intent.** 
            

**Key settings.** `negStock` Kodak Vision3 500T 5219, `pushPull` 0.5, `process` Print, `printStock` Kodak Vision Premier 2393, `printBlack` -0.3, `headCyan` -3, `toneShadowHue` 195, `toneShadowAmount` 0.35, `halationAmount` 1, `halationRadius` 28, `halationSaturation` 1.2, `bloomFilter` Hollywood Black Magic, `bloomFilterGrade` 0.25, `bloomAmount` 0.25, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 1.1, `grainShadows` 1.3, `acutanceAmount` 0.3, `weaveStyle` Camera (Pin-Registered), `weaveAmount` 0.15, `vignette` 0.2, `gateEnable` true, `gateAspect` 2.39 Scope.

## 8. Portra 400 Editorial

**Intent.** 
            

**Key settings.** `negStock` Kodak Portra 400, `process` Lab Scan, `zoneShadows` 0.3, `filmContrast` 0.92, `rolloffSoftness` 0.5, `dyeSaturation` 0.9, `dyeRichness` 0.35, `halationAmount` 0.35, `bloomFilter` Pro-Mist (white), `bloomFilterGrade` 0.25, `bloomAmount` 0.2, `grainEnable` true, `grainFormat` 35 mm Stills, `grainAmount` 0.8, `grainColour` 0.8, `acutanceAmount` 0.3.

## 9. Super 8 Ektachrome Diary

**Intent.** 
            

**Key settings.** `negStock` Kodak Ektachrome 100D 5294, `dyeRichness` 0.4, `toneHighlightHue` 45, `toneHighlightAmount` 0.2, `halationAmount` 0.8, `halationRadius` 14, `bloomAmount` 0.25, `grainEnable` true, `grainFormat` Super 8, `grainAmount` 1.1, `grainSoftness` 0.6, `integrate` 0.6, `acutanceAmount` 0.2, `weaveStyle` Projector, `weaveAmount` 0.7, `weaveSpeed` 1.5, `vignetteMechanical` 0.45, `vignetteMechSoftness` 0.6, `gateEnable` true, `gateAspect` 1.33 Academy, `gateCorner` 40, `gateSoftness` 6, `breathEnable` true, `breathProfile` Old Camera.

## 10. E100 Slide Projector

**Intent.** 
            

**Key settings.** `negStock` Kodak Ektachrome E100, `projectionWhite` Theatre Xenon (Unadapted), `dyeSaturation` 1.1, `halationAmount` 0.2, `bloomAmount` 0.1, `grainEnable` true, `grainFormat` 35 mm Stills, `grainAmount` 0.6, `grainSoftness` 0.4, `acutanceAmount` 0.4, `vignette` 0.3.

## 11. Kodachrome Travelogue

**Intent.** 
            

**Key settings.** `negStock` Kodachrome 64 Style, `dyeRichness` 0.5, `toneHighlightHue` 40, `toneHighlightAmount` 0.15, `halationAmount` 0.4, `bloomAmount` 0.12, `grainEnable` true, `grainFormat` 16 mm, `grainAmount` 0.9, `grainSoftness` 0.35, `acutanceAmount` 0.4, `weaveStyle` Projector, `weaveAmount` 0.4, `vignetteMechanical` 0.25, `gateEnable` true, `gateAspect` 1.33 Academy, `gateSource` Printer Gate (Clear Print Border), `breathEnable` true, `breathProfile` Projector.

## 12. Double-X Silver Print

**Intent.** 
            

**Key settings.** `negStock` Kodak Double-X 5222, `pushPull` 1, `process` Print, `printStock` Kodak Vision 2383, `printRender` Release Print (IP / IN), `filmContrast` 1.1, `toneShadowHue` 215, `toneShadowAmount` 0.15, `toneHighlightHue` 40, `toneHighlightAmount` 0.15, `halationAmount` 0.3, `halationSaturation` 0, `bloomFilter` Hollywood Black Magic, `bloomAmount` 0.2, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 1.3, `grainSoftness` 0.4, `acutanceAmount` 0.5, `weaveAmount` 0.3, `vignette` 0.25, `gateEnable` true, `gateAspect` 1.33 Academy, `damageEnable` true, `dustAmount` 0.3, `scratchAmount` 0.15, `hairAmount` 0.1.

## 13. Eterna 500 Skip Bleach

**Intent.** 
            

**Key settings.** `negStock` Fujifilm Eterna 500 8573, `process` Print, `printStock` Fujifilm Eterna-CP 3521XD, `printBleach` 0.45, `dyeSaturation` 0.85, `toneShadowHue` 205, `toneShadowAmount` 0.2, `halationAmount` 0.45, `bloomFilter` Glimmerglass, `bloomFilterGrade` 0.25, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 1.2, `grainSoftness` 0.3, `acutanceAmount` 0.6, `weaveStyle` Camera (Pin-Registered), `weaveAmount` 0.1, `gateEnable` true, `gateAspect` 1.85 Flat.

## 14. Reala Green Day

**Intent.** 
            

**Key settings.** `negStock` Fujifilm Reala 500D 8592, `process` Print, `printStock` Fujifilm Eterna-CP 3513DI, `printRender` Telecine / Scan, `headMagenta` -3, `printBlack` -0.15, `toneShadowHue` 165, `toneShadowAmount` 0.2, `dyeSatBalance` 0.2, `halationAmount` 0.4, `bloomFilter` Pro-Mist (white), `bloomFilterGrade` 0.125, `bloomAmount` 0.18, `grainEnable` true, `grainFormat` Super 35, `grainAmount` 0.8, `grainSoftness` 0.6, `acutanceAmount` 0.25, `vignette` 0.1.

## Revision notes (v1.4)

The table was rebuilt on the owner's request: one preset per real camera stock (15), each on its own negative +
print pairing. Signature 250D / 2383 is unchanged. Crisp Commercial and Soft Cinema keep their recipes; Studio Modern
(now 200T), Warm Print (now Vision2 500T), Theatrical 2383 Warm (now Expression 500T) and Sunlit 35mm (soft scan) (now
Eterna Vivid 160 on a telecine scan of 2383, golden highlight toning, telecine float) keep their character on a new
stock. The other presets were replaced. Projects store the menu index, so an older project may show a different
preset name; its saved values and its render are unchanged.

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
