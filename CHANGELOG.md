# Changelog

All notable changes to Celluloid Pro. Versions follow [Semantic Versioning](https://semver.org);
parameter identifiers and choice orders are permanent, so projects saved with an older version open
unchanged in a newer one.

## [1.0.0] — unreleased

First release of Celluloid Pro, the spectral film-emulation OpenFX plug-in for DaVinci Resolve.

### Platforms

- macOS 11 or later, universal binary (Apple silicon and Intel), Metal GPU rendering; OpenCL offered
  to hosts that use it.
- Windows 10/11 x64, CUDA (NVIDIA, Maxwell and newer) and OpenCL (AMD, Intel) GPU rendering.
- Multithreaded CPU fallback on both platforms, producing the same image as the GPU paths.
- Installers: macOS `.pkg` (in a `.dmg`) to `/Library/OFX/Plugins`; Windows Inno Setup installer to
  `Common Files\OFX\Plugins` (English and German) plus a zipped bundle for manual installs.
  Signing and notarization switch on when the signing certificates are configured in CI.

### Colour science

- Spectral negative/print model (380–780 nm, 5 nm): smooth spectral upsampling of the scene,
  designed layer sensitivities, characteristic curves with toe, shoulder, D-min and orange mask,
  dye absorption spectra with secondary absorptions and masking couplers.
- Development: push/pull (−2 … +3 stops), interlayer (DIR) colour density, negative bleach bypass,
  neutralize.
- Print with printer lights in lab points and automatic grey balance, print bleach bypass,
  per-pixel spectral projection; or Lab Scan (narrow-band scanner, balanced inversion); reversal
  and black & white paths.
- Inputs: Fujifilm F-Log2 C / F-Gamut C (default), F-Log2, F-Log, DaVinci Intermediate, ARRI LogC3
  and LogC4, Sony S-Log3, Panasonic V-Log, Canon Log 3, Blackmagic Film Gen 5, ACEScct, Linear
  Rec.2020.
- Outputs: Rec.709 Gamma 2.4, Rec.709 sRGB, P3-D65, DCI-P3 (D65 simulated), Rec.2100 PQ and HLG with
  smooth gamut compression and an HDR highlight unroll (peak 300–4000 nits).

### Film stocks and looks

- 11 negative stocks: CN 50D Fine Daylight, CN 200T Studio Tungsten, CN 250D Soft Daylight,
  CN 500T High Speed, CN 800T No-Backing, CN Vintage 85, C41 Portrait 400, C41 Consumer 200,
  RV 100 Slide (reversal), BW 400 Panchromatic, BW 100 Fine.
- 5 print stocks: P1 Standard Projection, P2 Low Contrast, P3 Rich Contrast, P4 Faded Archive,
  P5 Silver Retention.
- 12 look presets: Studio Classic, Tungsten Night Drama, Soft Daylight Pastel, Archive 1978, Silver
  Retention Thriller, Neon Nocturne, Golden Hour Portrait, Crisp Commercial, Summer Snapshot, Slide
  Show Brilliance, Monochrome Noir, Home Movie Super 8 — with a "(modified)" status after edits.
- All stocks and looks are original designs.

### Texture

- Halation with a soft-knee threshold in stops, multi-scale long-tail glow and hue control; neutral
  bloom; development acutance; gate weave; optical vignette.
- Format-aware film grain (Super 8, 16 mm, Super 35, 65 mm, 35 mm stills) in the negative's density
  domain: clumpy, band-limited, layer-correlated, animated or frozen, seedable; monochrome for black &
  white stocks.
- Resolution independent: radii and grain size scale with the image width, so proxies and render
  scales match the final render.
- Modes: Full, Color Only and Texture Only (texture on an already graded image).

### Workflow

- `.cube` LUT export (17³/33³/65³) of the Color Only pipeline, named after input, stocks and output;
  Linear Rec.2020 exports include a 1D log2 shaper. Licensed version only.
- Offline license keys (Ed25519): activation in the License group, per-user, machine-wide or
  environment-variable keys; no internet connection required. Without a key: full image features with
  a small trial watermark; LUT export disabled.
- User manual inside the plug-in bundle.

### Known limitations

- Not yet verified inside DaVinci Resolve (testing in progress), on NVIDIA hardware (CUDA compiled but
  not executed) or with real-GPU timings; see the manual, section 13.
- The production license public key is provisional until the owner's key ceremony; release tags are
  blocked until then.
