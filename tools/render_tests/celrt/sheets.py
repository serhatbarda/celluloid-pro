"""Contact sheets (Pillow)."""
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def _font(size):
    for name in ("DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "LiberationSans-Regular.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def to_pil(img):
    """Display-encoded float RGBA/RGB array -> 8-bit PIL image (clamped, no transfer applied)."""
    a = np.asarray(img)
    if a.dtype != np.uint8:
        a = np.nan_to_num(a[..., :3].astype(np.float64), nan=0.0)
        a = np.floor(np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)
    return Image.fromarray(a[..., :3], "RGB")


def contact_sheet(items, path, title, cols=4, thumb_w=320, label_h=34):
    """items: list of (label, float image). Writes a PNG and returns its path."""
    if not items:
        return None
    thumbs = []
    for label, img in items:
        im = to_pil(img)
        h = int(round(im.height * thumb_w / im.width))
        thumbs.append((label, im.resize((thumb_w, h), Image.LANCZOS)))
    th = max(t.height for _, t in thumbs)
    rows = (len(thumbs) + cols - 1) // cols
    pad, head = 8, 40
    W = cols * (thumb_w + pad) + pad
    H = head + rows * (th + label_h + pad) + pad
    sheet = Image.new("RGB", (W, H), (24, 24, 26))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 10), title, fill=(235, 235, 235), font=_font(18))
    f = _font(12)
    for i, (label, t) in enumerate(thumbs):
        r, c = divmod(i, cols)
        x = pad + c * (thumb_w + pad)
        y = head + r * (th + label_h + pad)
        sheet.paste(t, (x, y))
        lines = _wrap(label, 46)
        for k, ln in enumerate(lines[:2]):
            d.text((x + 2, y + th + 3 + 14 * k), ln, fill=(210, 210, 210), font=f)
    sheet.save(path)
    return path


def _wrap(s, n):
    words, lines, cur = s.split(), [], ""
    for w in words:
        if len(cur) + len(w) + 1 > n and cur:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    if cur:
        lines.append(cur)
    return lines or [""]


def hdr_preview(nits, peak=1000.0):
    """Tone-map HDR nits for a thumbnail (simple, for contact sheets only)."""
    x = np.asarray(nits, np.float64) / 100.0
    y = x / (1.0 + x / (peak / 100.0)) * (1.0 + 1.0 / (peak / 100.0))
    return np.power(np.clip(y, 0, 1), 1 / 2.4)
