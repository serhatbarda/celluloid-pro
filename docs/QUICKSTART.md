# Reafilm Pro — Quick Start

One page to get from installed plug-in to a film look. Every control is described in
[MANUAL.md](MANUAL.md); the looks are compared in [LOOKS.md](LOOKS.md).

## 1. Put it on a node

1. In Resolve's Color page, add a **serial node** and drag **Reafilm Pro** (OpenFX → Reafilm) onto it.
2. The node must receive the **camera's log image** (or the display image for Rec.709 / sRGB sources):
   - *DaVinci YRGB (not colour managed)*: nothing to do — the clip arrives as recorded.
   - *Colour managed (RCM / ACES)*: set the clip's input to bypass, or put a Color Space Transform node in
     front that converts back to the camera log you pick in **Input → Camera Log**.
3. Grade exposure and white balance **before** this node (or with Input → Exposure and Scene White Balance).
   Put nothing that changes the look after it: Reafilm Pro already outputs the finished display image.

## 2. Pick the input

**Input → Camera Log** must match the footage. If the image looks very flat, or harsh and colour-shifted,
it is set wrong.

| Footage | Camera Log |
|---|---|
| Fujifilm X-H2S F-Log2 C | Fujifilm F-Log2 C / F-Gamut C (default) |
| Fujifilm F-Log2 / F-Log | the matching Fujifilm entry |
| Blackmagic BRAW (e.g. Pocket 6K) | Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5 — see §6 |
| ARRI, Sony, Panasonic, Canon | the matching log / gamut entry |
| Phone, camcorder, any Rec.709 video | Rec.709 video (camcorder / phone) |
| JPEG / PNG photo | sRGB (photos / JPEG) |

Turn on **Input → Still Photo** for a single photograph: the grain and dust are frozen and nothing moves.

## 3. Pick a look

**Look → Look Preset** loads a complete negative + print + texture look; each preset is one real camera
stock with its usual print (see [LOOKS.md](LOOKS.md)). Change anything afterwards — the Preset Status then
reads "(modified)". **Save User Preset** keeps your own version.

The four controls that matter most after a preset:

- **Input → Exposure** — moves the image along the film's curve (over/under-exposure the film way).
- **Print → Printer Red / Green / Blue** — the colour timing of the print (warm/cool, magenta/green).
- **Grain → Grain Amount / Grain Size** — grain strength; **Gauge** sets the film format (Super 8 … 65 mm).
- **Halation → Halation Amount** — the red glow around bright lights.

**Mode → Color Only** shows the colour alone (what a LUT contains); **Texture Only** adds just grain,
halation and gate effects to an image you graded yourself.

## 4. Pick the output

**Output → Output** must match the timeline / monitor: *Rec.709 (Gamma 2.4)* for a normal grading monitor,
*Rec.709 (sRGB)* for web delivery watched on computers, *P3* or *Rec.2100* for those deliveries.

## 5. Monitoring LUT (on set)

**LUT Export → Export LUT** writes the current colour (no grain or texture) as a `.cube` for the selected
Camera Log — LUT Size **33** suits monitors and recorders (Atomos, SmallHD, Blackmagic Video Assist), 17
for devices with little LUT memory. Load it on the monitor fed with the camera's log signal; the camera
keeps recording clean log, and the same look is applied in Resolve afterwards with the plug-in.

## 6. Blackmagic Pocket 6K (BRAW)

Resolve develops BRAW itself, so set the clip up before the node:

1. **Camera Raw** panel (Color page), *Decode Using*: **Clip**.
2. *Color Science*: **Gen 5**; *Gamut*: **Blackmagic Design**; *Gamma*: **Blackmagic Design Film**.
3. *Highlight Recovery*: **on** — Resolve rebuilds the clipped channels from BRAW, so blown skies go
   white instead of magenta. (Reafilm Pro's Highlight Latitude is for Fujifilm footage only.)
4. Set ISO, white balance and exposure in the Camera Raw panel — they act on the raw data, before the film.
5. In Reafilm Pro: **Camera Log → Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5**.

Notes:

- **Sensor Match** (X-H2S → ALEXA) is fitted to the Fujifilm sensor and stays off for BRAW. A Pocket 6K
  match needs a colour chart filmed with the camera (no measured sensor data is published).
- **Monitoring LUTs on the camera itself**: the Pocket 6K's own screen shows its in-camera Gen 4 film
  curve, not Gen 5, so an exported LUT is only exact on a monitor or in Resolve, not loaded into the camera.

## 7. Stills

- **RAW**: Resolve develops DNG and BRAW (convert Fujifilm RAF to DNG first, e.g. with Adobe DNG Converter).
  For DNG set the Camera Raw panel to *Color Space* **Rec.2020** and *Gamma* **Linear** and pick Camera Log
  **Linear Rec.2020**; for BRAW follow §6. Turn on **Still Photo**.
- **JPEG / PNG**: Camera Log **sRGB (photos / JPEG)** and **Still Photo**. The plug-in expands the clipped
  JPEG highlights so they still roll off like film.
- Export at full resolution from the Deliver page (*TIFF* or *DPX* for a 16-bit file).
