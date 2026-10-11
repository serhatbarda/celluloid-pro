#!/usr/bin/env python3
"""Celluloid Pro render tests (SPEC §6 / §10) driven through the headless OFX host.

    run_all.py --host ofxhost --bundle Celluloid.ofx.bundle --out OUTDIR [--quick|--full] [--no-gpu]

Everything is discovered from `ofxhost --list-params-json` (parameter IDs are the permanent
SPEC §7 IDs; choice options are located by label patterns), so the suite adapts to new stocks,
presets and output spaces without edits. Results: OUTDIR/report.json, OUTDIR/report.md,
OUTDIR/contact_*.png, renders in OUTDIR/renders, host logs in OUTDIR/host.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time
import traceback

import numpy as np

sys.dont_write_bytecode = True   # keep the source tree clean
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from celrt import analysis as A          # noqa: E402
from celrt import colour as C            # noqa: E402
from celrt import luts                   # noqa: E402
from celrt import scenes as S            # noqa: E402
from celrt import sheets                 # noqa: E402
from celrt.host import Host, HostError, Params, job_error, job_frames, set_args  # noqa: E402

PARITY_TOL = 2e-4       # SPEC §6: CPU vs OpenCL max abs diff on encoded output
LUT_TOL = 2e-4          # .cube vs Color Only render at lattice points

# SPEC §7: id, kind, default, min, max  (kind: double/int/bool/choice/string/button)
# (v1.2 owner decision: default stock pair Kodak Vision3 250D 5207 + P6 Modern Print, SPEC §13)
SPEC7 = [
    ("lookPreset", "choice", "Custom", None, None), ("mode", "choice", "Full", None, None),
    ("inputSpace", "choice", r"F-?Log2\s*C", None, None), ("exposure", "double", 0.0, -4, 4),
    ("negStock", "choice", "Vision3 250D 5207", None, None), ("pushPull", "double", 0.0, -2, 3),
    ("interlayer", "double", 1.0, 0, 2), ("negBleach", "double", 0.0, 0, 1), ("neutralize", "double", 0.0, 0, 1),
    ("process", "choice", "Print", None, None), ("printStock", "choice", "P6 Modern Print", None, None),
    ("printerR", "double", 0.0, -12, 12), ("printerG", "double", 0.0, -12, 12), ("printerB", "double", 0.0, -12, 12),
    ("printExposure", "double", 0.0, -12, 12), ("printBleach", "double", 0.0, 0, 1),
    ("halationAmount", "double", 0.5, 0, 2), ("halationRadius", "double", 20, 2, 80),
    ("halationThreshold", "double", 3.5, 0, 6), ("halationHue", "double", 0.0, -1, 1),
    ("bloomAmount", "double", 0.15, 0, 2), ("bloomRadius", "double", 40, 5, 200),
    ("grainEnable", "bool", True, None, None), ("grainFormat", "choice", r"S(uper)?\s*35", None, None),
    ("grainAmount", "double", 1.0, 0, 3), ("grainSize", "double", 1.0, 0.5, 3), ("grainSoftness", "double", 0.5, 0, 1),
    ("grainAnimated", "bool", True, None, None), ("grainSeed", "int", 0, 0, 9999),
    ("acutanceAmount", "double", 0.3, -1, 2), ("acutanceRadius", "double", 2, 0.5, 10),
    ("weaveAmount", "double", 0.0, 0, 2), ("weaveSpeed", "double", 1.0, 0.25, 4), ("vignette", "double", 0.0, 0, 1),
    ("outputSpace", "choice", None, None, None), ("hdrPeakNits", "double", 1000, 300, 4000), ("hdrUnroll", "double", 1.0, 0, 1),
    ("lutSize", "choice", "33", None, None), ("lutPath", "string", None, None, None), ("lutExport", "button", None, None, None),
    ("licenseStatus", "string", None, None, None), ("licenseKey", "string", None, None, None),
    ("licenseActivate", "button", None, None, None), ("licenseRemove", "button", None, None, None),
    # v1.1 module switches (default on: projects saved before they existed render unchanged)
    ("exposureEnable", "bool", True, None, None), ("developmentEnable", "bool", True, None, None),
    ("printEnable", "bool", True, None, None), ("halationEnable", "bool", True, None, None),
    ("bloomEnable", "bool", True, None, None), ("acutanceEnable", "bool", True, None, None),
    ("weaveEnable", "bool", True, None, None), ("vignetteEnable", "bool", True, None, None),
    ("hdrUnrollEnable", "bool", True, None, None),
]
# v1.1 module switches: (switch, module values used for the "on" render, neutral values the "off"
# render must reproduce bit for bit, mode, scene)
MODULES = [
    ("exposureEnable", {"exposure": 1.5}, {"exposure": 0.0}, "full", "natural"),
    ("developmentEnable", {"pushPull": 2.0, "interlayer": 1.7, "negBleach": 0.4, "neutralize": 0.6},
     {"pushPull": 0.0, "interlayer": 1.0, "negBleach": 0.0, "neutralize": 0.0}, "full", "natural"),
    ("printEnable", {"printerR": 4.0, "printerG": -3.0, "printerB": 2.5, "printExposure": -3.0, "printBleach": 0.5},
     {"printerR": 0.0, "printerG": 0.0, "printerB": 0.0, "printExposure": 0.0, "printBleach": 0.0}, "full", "natural"),
    ("halationEnable", {"halationAmount": 1.5}, {"halationAmount": 0.0}, "full", "highlights"),
    ("bloomEnable", {"bloomAmount": 1.5}, {"bloomAmount": 0.0}, "full", "highlights"),
    ("acutanceEnable", {"acutanceAmount": 1.5}, {"acutanceAmount": 0.0}, "full", "natural"),
    ("weaveEnable", {"weaveAmount": 2.0}, {"weaveAmount": 0.0}, "full", "natural"),
    ("vignetteEnable", {"vignette": 1.0}, {"vignette": 0.0}, "full", "natural"),
    ("grainEnable", {"grainAmount": 2.0}, {"grainAmount": 0.0}, "full", "natural"),
    ("halationEnable", {"halationAmount": 1.5}, {"halationAmount": 0.0}, "texture", "display"),
    # v1.1 film damage (master + sub-switches) and film blur; damage frame 2 holds dust, scratches and a hair
    ("damageEnable", {"dustAmount": 2.0, "scratchAmount": 2.0, "hairAmount": 2.0},
     {"dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 0.0}, "full", "natural"),
    ("dustEnable", {"damageEnable": True, "dustAmount": 2.0, "scratchAmount": 0.0, "hairAmount": 0.0},
     {"damageEnable": True, "dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 0.0}, "full", "natural"),
    ("scratchEnable", {"damageEnable": True, "dustAmount": 0.0, "scratchAmount": 2.0, "hairAmount": 0.0},
     {"damageEnable": True, "dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 0.0}, "full", "natural"),
    ("hairEnable", {"damageEnable": True, "dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 2.0},
     {"damageEnable": True, "dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 0.0}, "full", "natural"),
    ("damageEnable", {"dustAmount": 2.0, "scratchAmount": 2.0, "hairAmount": 2.0},
     {"dustAmount": 0.0, "scratchAmount": 0.0, "hairAmount": 0.0}, "texture", "display"),
    ("filmBlurEnable", {"filmBlurFrequency": 1.0, "filmBlurBreathing": 1.0, "filmBlurRadius": 12.0},
     {"filmBlurFrequency": 0.0, "filmBlurBreathing": 0.0, "filmBlurRadius": 12.0}, "full", "natural"),
]
SPEC_NEGS = ["CN 50D Fine Daylight", "CN 200T Studio Tungsten", "CN 250D Soft Daylight", "CN 500T High Speed",
             "CN 800T No-Backing", "CN Vintage 85", "C41 Portrait 400", "C41 Consumer 200", "RV 100 Slide",
             "BW 400 Panchromatic", "BW 100 Fine"]
SPEC_PRINTS = ["P1 Standard Projection", "P2 Low Contrast", "P3 Rich Contrast", "P4 Faded Archive", "P5 Silver Retention"]
GRAIN_FORMATS = [("Super 8", r"8"), ("16 mm", r"16"), ("Super 35", r"35(?!\s*mm\s*St)"), ("65 mm", r"65"),
                 ("35 mm Stills", r"still")]
# real stocks from public data sheets are part of the product (owner decision, SPEC §1 v1.2)
REAL_STOCK = r"\b(vision\s*3|2383|eterna)\b"

DAMAGE_ALL = {"damageEnable": True, "dustAmount": 2.0, "scratchAmount": 2.0, "hairAmount": 2.0, "scratchEmulsion": 0.5,
              "filmBlurEnable": True, "filmBlurFrequency": 1.0, "filmBlurBreathing": 1.0, "filmBlurRadius": 12.0}
TEX_OFF = {"halationAmount": 0.0, "bloomAmount": 0.0, "grainEnable": False, "acutanceAmount": 0.0,
           "weaveAmount": 0.0, "vignette": 0.0}


class Suite:
    def __init__(self, a):
        self.a = a
        self.out = os.path.abspath(a.out)
        self.rdir = os.path.join(self.out, "renders")
        self.sdir = os.path.join(self.out, "scenes")
        for d in (self.out, self.rdir, self.sdir):
            os.makedirs(d, exist_ok=True)
        env_cfg = os.path.join(self.out, "_config")
        if os.path.isdir(env_cfg):
            shutil.rmtree(env_cfg)
        os.makedirs(env_cfg, exist_ok=True)
        # isolate the plug-in's per-user license store from the developer's own activation
        os.environ["XDG_CONFIG_HOME"] = env_cfg
        self.host = Host(a.host, a.bundle, os.path.join(self.out, "host"), threads=a.threads, verbose=a.verbose)
        self.results = []
        self.data = {}
        self.scenes = {}
        self.sheets = []
        self.t0 = time.time()
        self.gpu = not a.no_gpu
        self.licensed = False
        self.personal = False      # personal build (default): no licensing at all

    # ------------------------------------------------------------------ bookkeeping
    def rec(self, group, name, status, measured="", expected="", detail=""):
        self.results.append(dict(group=group, name=name, status=status, measured=measured, expected=expected, detail=detail))
        if self.a.verbose or status == "fail":
            print("  %-5s %-12s %s: %s %s" % (status.upper(), group, name, measured, ("(" + detail + ")") if detail else ""))

    def check(self, group, name, ok, measured="", expected="", detail=""):
        self.rec(group, name, "pass" if ok else "fail", measured, expected, detail)
        return ok

    def info(self, group, name, measured="", detail=""):
        self.rec(group, name, "info", measured, "", detail)

    def skip(self, group, name, why):
        self.rec(group, name, "skip", "", "", why)

    # ------------------------------------------------------------------ scenes / jobs
    def scene(self, name, img):
        if name not in self.scenes:
            path = os.path.join(self.sdir, name + ".f32")
            S.write_f32(path, img)
            self.scenes[name] = (path, img.shape[1], img.shape[0], img)
        return self.scenes[name]

    def has(self, p):
        return self.P.has(p)

    def values(self, overrides=None, base=True):
        v = dict(self.base) if base else {}
        for k, x in (overrides or {}).items():
            v[k] = x
        return {k: x for k, x in v.items() if self.has(k)}

    def line(self, label, scene, overrides=None, out=True, png=False, frames=None, time_=None, scale=None, gpu=False,
             repeat=None, reset=True, extra=(), post=None):
        """One batch job: reset, base + overrides, then `post` values (applied last, in order)."""
        path, w, h, _ = self.scenes[scene]
        args = ["--reset-params"] if reset else []
        args += set_args(self.values(overrides))
        for k, v in (post or []):
            if self.has(k):
                args += set_args({k: v})
        args += ["--in", path, "--width", w, "--height", h, "--label", label]
        if frames:
            args += ["--frames", frames]
        if time_ is not None:
            args += ["--time", time_]
        if scale:
            args += ["--render-scale", scale]
        if gpu:
            args += ["--gpu", "opencl"]
        if repeat:
            args += ["--repeat", repeat]
        if out:
            args += ["--out", os.path.join(self.rdir, label)]
            if not png:
                args += ["--no-png"]
        else:
            args += ["--render"]
        args += list(extra)
        return args

    def load(self, label, scene, frame=None, scale=1.0):
        _, w, h, _ = self.scenes[scene]
        w2, h2 = int(round(w * scale)), int(round(h * scale))
        path = os.path.join(self.rdir, label + (".%04d" % frame if frame is not None else "") + ".f32")
        return S.read_f32(path, w2, h2).astype(np.float64)

    def batch(self, name, lines, check=True, group=None):
        """Run a group's jobs on one instance; every failed job is recorded as a failure.
        (A license activation persists in the isolated per-user config dir for all batches.)"""
        rep = self.host.batch(name, lines, check=False)
        for j in rep.get("jobs", []):
            if j.get("error"):
                self.check(group or name, "job '%s' ran" % j["label"], False, j["error"][:300])
        if not rep.get("jobs") and rep.get("_exit"):
            raise HostError("%s: ofxhost exit %d: %s" % (name, rep["_exit"], rep.get("_stderr", "")[-1500:]))
        self.last_stderr = rep.get("_stderr", "")
        return rep

    def opt(self, param, *patterns, default=None, exclude=None):
        i = self.P.find_option(param, *patterns, exclude=exclude)
        if i is None:
            return default
        return i

    # ------------------------------------------------------------------ setup
    def setup(self):
        desc = self.host.describe()
        self.desc = desc
        self.P = Params(desc)
        P = self.P
        defidx = lambda p: int(P.default(p) or 0) if P.has(p) else 0
        self.ix = dict(
            neg=self.opt("negStock", r"200T", default=defidx("negStock")),
            p1=self.opt("printStock", r"^P1\b", default=self.opt("printStock", "standard", default=defidx("printStock"))),
            p4=self.opt("printStock", r"^P4\b", default=self.opt("printStock", "fad", default=None)),
            proc_print=self.opt("process", "print", default=0),
            proc_scan=self.opt("process", "scan", default=None),
            out709=self.opt("outputSpace", r"709", r"2\.4", default=self.opt("outputSpace", r"709", default=0)),
            inp=self.opt("inputSpace", r"F-?Log2\s*C", default=defidx("inputSpace")),
            full=self.opt("mode", r"^full", default=0),
            color=self.opt("mode", r"colou?r", default=1),
            texture=self.opt("mode", r"texture", default=2),
        )
        self.base = {"inputSpace": ("index", self.ix["inp"]), "outputSpace": ("index", self.ix["out709"]),
                     "negStock": ("index", self.ix["neg"]), "printStock": ("index", self.ix["p1"]),
                     "process": ("index", self.ix["proc_print"]), "mode": ("index", self.ix["color"]),
                     "exposure": 0.0, "printerR": 0.0, "printerG": 0.0, "printerB": 0.0, "printExposure": 0.0}
        self.negs = P.options("negStock")
        self.prints = P.options("printStock")
        self.outputs = P.options("outputSpace")
        self.out_spaces = [C.OutputSpace(o) for o in self.outputs]
        self.out709 = self.out_spaces[self.ix["out709"]] if self.out_spaces else C.OutputSpace("Rec.709 2.4")
        self.data["plugin"] = desc.get("plugin", {})
        self.data["choices"] = {k: P.options(k) for k in ("negStock", "printStock", "outputSpace", "inputSpace", "mode",
                                                           "process", "lookPreset", "grainFormat", "lutSize")}
        self.data["indices"] = self.ix
        # scenes
        self.chart = S.analysis_chart()
        self.scene("chart", self.chart.image())
        self.scene("natural", S.natural(480, 270))
        self.scene("natural_sheet", S.natural(640, 360))
        img, self.hl_points = S.highlights(480, 270)
        self.scene("highlights", img)
        self.scene("grey", S.grey_field(960, 540))   # HD-like frame: grain sizes are relative to the frame
        self.scene("display", S.display_scene(480, 270))
        self.scene("natural960", S.natural(960, 540))
        img, self.hal_regions = S.halation_daylight(1920, 1080)
        self.scene("halation_day", img)

    # ================================================================== groups
    def g_conformance(self):
        g = "params"
        P = self.P
        for pid, kind, dflt, mn, mx in SPEC7:
            p = P.get(pid)
            if p is None:
                self.check(g, pid, False, "missing", "present")
                continue
            t = p["type"]
            okt = {"double": t == "Double", "int": t == "Integer", "bool": t == "Boolean", "choice": t in ("Choice", "StrChoice"),
                   "string": t == "String", "button": t == "PushButton"}[kind]
            det = ""
            okd = True
            if dflt is not None:
                if kind == "choice":
                    opts = p.get("options", [])
                    di = p.get("default")
                    lab = opts[di] if isinstance(di, int) and 0 <= di < len(opts) else str(di)
                    okd = re.search(dflt if not dflt[0].isalnum() or "\\" in dflt else re.escape(dflt), lab, re.I) is not None
                    det = "default '%s'" % lab
                elif kind in ("double", "int"):
                    okd = abs(float(p.get("default")) - float(dflt)) < 1e-6
                    det = "default %s" % p.get("default")
                elif kind == "bool":
                    okd = bool(p.get("default")) == dflt
                    det = "default %s" % p.get("default")
            okr = True
            if mn is not None:
                lo, hi = (p.get("min") or [None])[0], (p.get("max") or [None])[0]
                okr = lo is not None and hi is not None and abs(lo - mn) < 1e-6 and abs(hi - mx) < 1e-6
                det += " range [%s, %s]" % (lo, hi)
            hint = bool(p.get("hint", "").strip())
            self.check(g, pid, okt and okd and okr and hint, "%s %s" % (t, det.strip()),
                       "%s default=%s range=%s" % (kind, dflt, "" if mn is None else "[%s,%s]" % (mn, mx)),
                       "" if hint else "no hint")
        # choice contents
        negs = P.options("negStock")
        missing = [n for n in SPEC_NEGS if not any(re.sub(r"\W", "", n.lower()) == re.sub(r"\W", "", o.lower()) for o in negs)]
        self.check(g, "negStock options = SPEC §4.6 (11)", not missing and len(negs) >= 11, "%d options" % len(negs), "11 named stocks",
                   "missing: " + ", ".join(missing) if missing else "")
        prints = P.options("printStock")
        missing = [n for n in SPEC_PRINTS if not any(re.sub(r"\W", "", n.lower()) == re.sub(r"\W", "", o.lower()) for o in prints)]
        self.check(g, "printStock options = SPEC §4.6 (5)", not missing and len(prints) >= 5, "%d options" % len(prints), "5 named prints",
                   "missing: " + ", ".join(missing) if missing else "")
        modes = P.options("mode")
        self.check(g, "mode options Full / Color Only / Texture Only", len(modes) == 3 and self.ix["color"] is not None, ", ".join(modes))
        self.check(g, "process options Print / Lab Scan", self.ix["proc_scan"] is not None, ", ".join(P.options("process")))
        gf = P.options("grainFormat")
        okf = len(gf) == 5 and all(any(re.search(pt, o, re.I) for o in gf) for _, pt in GRAIN_FORMATS)
        self.check(g, "grainFormat options (5 formats)", okf, ", ".join(gf))
        self.check(g, "lutSize options 17/33/65", [re.sub(r"\D", "", o) for o in P.options("lutSize")][:3] == ["17", "33", "65"],
                   ", ".join(P.options("lutSize")))
        self.check(g, "lookPreset has 'Custom' + presets", len(P.options("lookPreset")) >= 2 and re.search("custom", (P.options("lookPreset") or [""])[0], re.I) is not None,
                   "%d options" % len(P.options("lookPreset")))
        outs = P.options("outputSpace")
        self.check(g, "outputSpace offers Rec.709 2.4, PQ and HLG",
                   self.opt("outputSpace", "709", r"2\.4") is not None and self.opt("outputSpace", "pq|2084") is not None and
                   self.opt("outputSpace", "hlg") is not None, ", ".join(outs))
        # real data-sheet stocks are offered (owner decision v1.2: real stock names are allowed)
        real = [o for pid in ("negStock", "printStock") for o in P.options(pid) if re.search(REAL_STOCK, o, re.I)]
        self.check(g, "data-sheet stocks offered (Vision3 / 2383 / Eterna)", len(real) >= 3, "%d real stocks" % len(real), ">= 3",
                   "; ".join(real[:8]))
        no_hint = [p["name"] for p in P.value_params() if not p.get("secret") and not p.get("hint", "").strip()]
        self.check(g, "every visible parameter has a hint", not no_hint, "%d without hint" % len(no_hint), "0", ", ".join(no_hint[:10]))
        pl = self.desc.get("plugin", {})
        self.check(g, "plug-in identifier", pl.get("id") == "de.serhatbarda.CelluloidPro", pl.get("id"), "de.serhatbarda.CelluloidPro")
        self.check(g, "contexts Filter + General", all(any(c.endswith(x) for c in pl.get("contexts", [])) for x in ("Filter", "General")),
                   ", ".join(pl.get("contexts", [])))
        self.check(g, "supportsTiles=false, supportsMultiResolution=false",
                   pl.get("supportsTiles") is False and pl.get("supportsMultiResolution") is False,
                   "tiles=%s multires=%s" % (pl.get("supportsTiles"), pl.get("supportsMultiResolution")))
        gpu = pl.get("gpu", {})
        self.check(g, "declares OpenCL buffer rendering", gpu.get("openclBuffers") == "true", json.dumps(gpu))
        self.info(g, "declared GPU APIs on Linux", json.dumps(gpu))

    # ------------------------------------------------------------------ license / watermark
    def g_license(self):
        g = "license"
        if not self.has("licenseKey") or not self.has("licenseActivate"):
            self.skip(g, "activation", "license params missing")
            return
        key = self.a.license_key or os.environ.get("CEL_LICENSE_KEY", "")
        if not key and self.a.dev_license:
            script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "licensegen", "celluloid_license.py")
            priv = os.path.join(os.path.dirname(script), "dev-keys", "celluloid-dev.private.key")
            try:
                key = subprocess.run([sys.executable, script, "sign", "--key", priv, "--licensee", "Celluloid render tests",
                                      "--edition", "pro", "--id", "RENDER-TESTS"], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, check=True).stdout.strip()
            except Exception as ex:  # cryptography missing etc.
                self.info(g, "development key", "could not be generated: %s" % ex)
        lines = [self.line("trial_natural", "natural", {"mode": ("index", self.ix["full"]), **TEX_OFF}, png=True,
                           extra=["--list-params-json", os.path.join(self.out, "host", "license_before.json")])]
        act = []
        if key:
            act = ["--set", "licenseKey=" + key, "--press", "licenseActivate"]
            lines.append(act + ["--list-params-json", os.path.join(self.out, "host", "license_after.json")])
            lines.append(self.line("licensed_natural", "natural", {"mode": ("index", self.ix["full"]), **TEX_OFF}, png=True))
        if not key:
            # personal build check (the default build): pressing Activate with any text is a no-op
            lines.append(["--set", "licenseKey=CELL1-NOT-A-KEY", "--press", "licenseActivate",
                          "--list-params-json", os.path.join(self.out, "host", "license_after.json")])
            lines.append(self.line("personal_natural", "natural", {"mode": ("index", self.ix["full"]), **TEX_OFF}, png=True))
        rep = self.host.batch("license", lines, check=False)
        before = json.load(open(os.path.join(self.out, "host", "license_before.json")))
        bparams = {p["name"]: p for p in before["params"]}
        st0 = bparams.get("licenseStatus", {}).get("value", "")
        self.info(g, "status before activation", st0)
        if "Personal build" in st0:
            # personal build (default, SPEC §9): no license check, no watermark, LUT export always on;
            # the License group stays (saved projects) but is secret
            lic = [n for n, p in bparams.items() if n == "groupLicense" or p.get("parent") == "groupLicense"]
            self.check(g, "personal build: License group kept and secret",
                       len(lic) == 5 and all(bparams[n].get("secret") for n in lic), ", ".join(lic))
            ver = str(bparams.get("aboutVersion", {}).get("value", ""))
            self.check(g, "personal build: About shows 'Personal build'", "Personal build" in ver, ver)
            try:
                a0 = self.load("trial_natural", "natural")
                a1 = self.load("personal_natural", "natural")
                after = json.load(open(os.path.join(self.out, "host", "license_after.json")))
                st1 = {p["name"]: p for p in after["params"]}.get("licenseStatus", {}).get("value", "")
                same = st1 == st0 and float(np.abs(a0 - a1).max()) == 0.0
                self.check(g, "personal build: Activate is a no-op (status and render unchanged)", same,
                           "status after: %s" % st1)
            except Exception as ex:
                self.check(g, "personal build: renders", False, str(ex))
            self.licensed = True   # never watermarked: LUT export and every measured image are clean
            self.personal = True
            return
        if not key:
            self.skip(g, "activation", "no key (use --license-key or --dev-license)")
            self.info(g, "WARNING", "renders are unlicensed: the trial watermark is part of every measured image")
            return
        msgs = [m["text"] for m in rep["messages"]]
        after = json.load(open(os.path.join(self.out, "host", "license_after.json")))
        st1 = {p["name"]: p for p in after["params"]}.get("licenseStatus", {}).get("value", "")
        self.info(g, "status after activation", st1, " | ".join(m.replace("\n", " ")[:120] for m in msgs[-2:]))
        try:
            a0 = self.load("trial_natural", "natural")
            a1 = self.load("licensed_natural", "natural")
        except Exception as ex:
            self.check(g, "licensed render", False, str(ex))
            return
        d = np.abs(a0 - a1)[..., :3]
        h, w = d.shape[:2]
        br = d[int(h * 0.75):, int(w * 0.5):].max()
        self.licensed = st1 != st0 and br > 0.01
        self.check(g, "trial watermark present before / absent after activation", self.licensed,
                   "max diff bottom-right %.4f, frame %.4f" % (br, d.max()), "> 0.01 (watermark removed)")
        if not self.licensed:
            self.info(g, "WARNING", "activation failed - renders below carry the trial watermark")

    # ------------------------------------------------------------------ stock pairs
    def g_stocks(self):
        g = "stocks"
        negs = list(range(len(self.negs)))
        prints = list(range(len(self.prints)))
        if self.a.quick:
            negs = sorted(set([self.ix["neg"], 0, len(self.negs) - 1, self.opt("negStock", "RV|slide", default=8)]))
            prints = sorted(set([self.ix["p1"], self.ix["p4"] or 0]))
        lines, keys = [], []
        for n in negs:
            for p in prints:
                lab = "pair_n%02d_p%02d" % (n, p)
                lines.append(self.line(lab, "chart", {"negStock": ("index", n), "printStock": ("index", p)}))
                keys.append((n, "print", p, lab))
            if self.ix["proc_scan"] is not None:
                lab = "pair_n%02d_scan" % n
                lines.append(self.line(lab, "chart", {"negStock": ("index", n), "process": ("index", self.ix["proc_scan"])}))
                keys.append((n, "scan", self.ix["p1"], lab))
                lab2 = "pair_n%02d_scan_p%02d" % (n, prints[-1])
                lines.append(self.line(lab2, "chart", {"negStock": ("index", n), "process": ("index", self.ix["proc_scan"]),
                                                       "printStock": ("index", prints[-1])}))
                keys.append((n, "scan_alt", prints[-1], lab2))
        self.batch("stocks", lines)
        self.pairs = {}
        rows = []
        all_mono, all_finite, all_range = [], [], []
        for n, proc, p, lab in keys:
            smp = self.chart.sample(self.load(lab, "chart"))
            self.pairs[(n, proc, p)] = smp
            if proc == "scan_alt":
                continue
            m = A.wedge_metrics(self.chart, smp)
            t = A.torture_metrics(self.chart, smp, self.out709)
            fin = t["nan"] == 0 and t["inf"] == 0
            rng = t["below0"] == 0 and t["above1"] == 0 and m["max"] <= 1.0 + 1e-6
            all_mono.append((A.monotonic(m), "%s/%s" % (self.negs[n], "Lab Scan" if proc == "scan" else self.prints[p])))
            all_finite.append(fin)
            all_range.append(rng)
            rows.append(dict(neg=self.negs[n], process=proc, print=self.prints[p] if proc == "print" else "Lab Scan",
                             grey=m["grey"], neutral=m["neutral"], minus6=m["minus6"], plus6=m["plus6"], plus8=m["plus8"],
                             monotonic=A.monotonic(m), min_step=m["min_step"], finite=fin, in_range=rng,
                             max_hue_jump=t["max_hue_jump"], max_step=t["max_step"]))
        self.data["stock_pairs"] = rows
        bad = [s for ok, s in all_mono if not ok]
        self.check(g, "monotonic -10..+10 stops, every channel, every stock pair", not bad,
                   "%d/%d pairs monotonic" % (len(all_mono) - len(bad), len(all_mono)), "all", "; ".join(bad[:6]))
        self.check(g, "no NaN/Inf for any pair (incl. torture inputs)", all(all_finite), "%d/%d" % (sum(all_finite), len(all_finite)))
        self.check(g, "SDR output within [0,1] for every pair", all(all_range), "%d/%d" % (sum(all_range), len(all_range)))
        # reversal stocks ignore print + process
        for n in negs:
            if not re.search(r"\bRV\b|slide|reversal", self.negs[n], re.I):
                continue
            ref = self.pairs.get((n, "print", prints[0]))
            diffs = [float(np.max(np.abs(v - ref))) for (nn, proc, p), v in self.pairs.items() if nn == n and v is not ref]
            self.check(g, "reversal '%s' ignores print stock and process" % self.negs[n], max(diffs or [0]) <= 1e-5,
                       "max diff %.2e" % max(diffs or [0]), "<= 1e-5")
        # lab scan ignores the print stock
        if self.ix["proc_scan"] is not None:
            dd = []
            for n in negs:
                a, b = self.pairs.get((n, "scan", self.ix["p1"])), self.pairs.get((n, "scan_alt", prints[-1]))
                if a is not None and b is not None:
                    dd.append(float(np.max(np.abs(a - b))))
            self.check(g, "Lab Scan output independent of the print stock", max(dd or [0]) <= 1e-5, "max diff %.2e" % max(dd or [0]), "<= 1e-5")
        # black & white: neutral image (print base tint only)
        for n in negs:
            if not re.search(r"\bBW\b|b&w|mono", self.negs[n], re.I):
                continue
            labs = A.chart_labs(self.chart, self.pairs[(n, "print", self.ix["p1"] if self.ix["p1"] in prints else prints[0])], self.out709)
            cmax = float(np.max(np.hypot(labs[:, 1], labs[:, 2])))
            self.check(g, "B&W '%s' renders a neutral image" % self.negs[n], cmax <= 8.0, "max chroma C*ab %.2f" % cmax,
                       "<= 8 (print base tint only)")
        # distinctness: Delta E 2000 on the 24 patch chart
        labs = {self.negs[n]: A.chart_labs(self.chart, self.pairs[(n, "print", self.ix["p1"])], self.out709)
                for n in negs if (n, "print", self.ix["p1"]) in self.pairs}
        names, M = A.de_matrix(labs)
        self.data["de_negatives"] = dict(names=names, matrix=M.tolist())
        off = M[~np.eye(len(names), dtype=bool)] if len(names) > 1 else np.array([99.0])
        k = np.unravel_index(int(np.argmin(M + np.eye(len(names)) * 1e9)), M.shape) if len(names) > 1 else (0, 0)
        self.check(g, "negative stocks clearly distinct (mean dE2000, P1)", off.min() >= 2.0, "min %.2f (%s vs %s)" %
                   (off.min(), names[k[0]], names[k[1]]), ">= 2.0")
        labs = {self.prints[p]: A.chart_labs(self.chart, self.pairs[(self.ix["neg"], "print", p)], self.out709)
                for p in prints if (self.ix["neg"], "print", p) in self.pairs}
        names, M = A.de_matrix(labs)
        self.data["de_prints"] = dict(names=names, matrix=M.tolist())
        if len(names) > 1:
            off = M[~np.eye(len(names), dtype=bool)]
            k = np.unravel_index(int(np.argmin(M + np.eye(len(names)) * 1e9)), M.shape)
            self.check(g, "print stocks clearly distinct (mean dE2000, CN 200T)", off.min() >= 2.0, "min %.2f (%s vs %s)" %
                       (off.min(), names[k[0]], names[k[1]]), ">= 2.0")

    # ------------------------------------------------------------------ §6 core
    def g_section6(self):
        g = "section6"
        base = self.pairs.get((self.ix["neg"], "print", self.ix["p1"]))
        if base is None:
            self.skip(g, "all", "default pair not rendered")
            return
        m = A.wedge_metrics(self.chart, base)
        self.data["default_wedge"] = m
        grey = np.array(m["grey"])
        self.check(g, "18% grey -> 0.40 +- 0.03 (Rec.709 2.4)", abs(grey.mean() - 0.40) <= 0.03, "%.4f %.4f %.4f" % tuple(grey), "0.37..0.43")
        self.check(g, "18% grey neutral within 0.005", m["neutral"] <= 0.005, "%.4f" % m["neutral"], "<= 0.005")
        self.check(g, "monotonic tone scale -10..+10 (CN 200T + P1)", A.monotonic(m), "min step %.2e" % m["min_step"], ">= 0")
        p6 = np.array(m["plus6"])
        self.check(g, "+6 stops >= 0.93", p6.min() >= 0.93, "%.4f %.4f %.4f" % tuple(p6), ">= 0.93")
        sl = np.array(m["slope_7_8"])
        p8 = np.array(m["plus8"])
        self.check(g, "no hard clip before +8 stops", sl.min() > 1e-4 and p8.max() < 1.0, "rise +7..+8 %.4f, +8 = %.4f" % (sl.min(), p8.max()),
                   "still rising, < 1.0", "first flat stop: %s" % m["first_flat_stop"])
        allmax = max(float(np.nanmax(v[:, :3])) for (n, proc, p), v in self.pairs.items())
        self.check(g, "SDR output never > 1.0 (all pairs, all chart inputs)", allmax <= 1.0 + 1e-6, "max %.6f" % allmax, "<= 1.0")
        m6 = np.array(m["minus6"])
        self.check(g, "-6 stops in 0.03..0.10 (P1)", m6.min() >= 0.03 and m6.max() <= 0.10, "%.4f %.4f %.4f" % tuple(m6), "0.03..0.10")
        if self.ix["p4"] is not None and (self.ix["neg"], "print", self.ix["p4"]) in self.pairs:
            m4 = A.wedge_metrics(self.chart, self.pairs[(self.ix["neg"], "print", self.ix["p4"])])
            v = np.array(m4["minus6"])
            self.check(g, "P4 lifted blacks >= 0.10 at -6 stops", v.min() >= 0.10, "%.4f %.4f %.4f" % tuple(v), ">= 0.10",
                       "-10 stops: %.4f" % np.min(m4["minus10"]))
        else:
            self.skip(g, "P4 lifted blacks", "P4 not found/rendered")
        sk = A.skin_metrics(self.chart, base, self.out709)
        self.data["skin"] = sk
        self.check(g, "skin hue within +-8 deg (CIE LCh) of input", sk["max_abs_shift"] <= 8.0,
                   "max |dh| %.2f deg" % sk["max_abs_shift"], "<= 8",
                   ", ".join("%+.1f" % x for x in sk["hue_shift"]))
        self.check(g, "skin warm, no green/magenta cast (a*>0, b*>0, hue 20..80 deg)",
                   sk["warm"] and all(20 <= h <= 80 for h in sk["hue_out"]),
                   "hues " + ", ".join("%.0f" % h for h in sk["hue_out"]))
        t = A.torture_metrics(self.chart, base, self.out709)
        self.data["torture"] = t
        self.check(g, "saturated / out-of-gamut / negative / >1 / huge inputs: no NaN/Inf", t["nan"] == 0 and t["inf"] == 0,
                   "NaN %d Inf %d" % (t["nan"], t["inf"]), "0")
        self.check(g, "... no hue flips (max hue jump per 1/4 stop, C*>8)", t["max_hue_jump"] <= 20.0,
                   "%.1f deg" % t["max_hue_jump"], "<= 20", t["worst_hue"] or "")
        self.check(g, "... smooth (max channel step per 1/4 stop)", t["max_step"] <= 0.12, "%.4f" % t["max_step"], "<= 0.12",
                   t["worst_step"] or "")
        self.check(g, "... SDR range [0,1]", t["below0"] == 0 and t["above1"] == 0, "below %d above %d" % (t["below0"], t["above1"]))
        # exposure slide
        lines = [self.line("ev_m2", "chart", {"exposure": -2.0}), self.line("ev_p2", "chart", {"exposure": 2.0}),
                 self.line("ev_p1", "chart", {"exposure": 1.0})]
        lines.append(self.line("full_grey", "grey", {"mode": ("index", self.ix["full"])}))
        self.batch("section6", lines)
        idx = self.chart.group("wedge")
        stops = np.array([self.chart.patches[i]["stop"] for i in idx])
        wedge = base[idx, :3]
        res = []
        for lab, ev in (("ev_m2", -2.0), ("ev_p2", 2.0), ("ev_p1", 1.0)):
            smp = self.chart.sample(self.load(lab, "chart"))
            gidx = idx[int(np.argmin(np.abs(stops - 0.0)))]
            target = wedge[int(np.argmin(np.abs(stops - ev)))]
            d = float(np.max(np.abs(smp[gidx, :3] - target)))
            # chart at exposure ev vs chart scaled by 2^ev in the scene (patch group chart24_ev+-2)
            if ev in (-2.0, 2.0):
                labs_param = A.chart_labs(self.chart, smp, self.out709, "chart24")
                labs_scene = A.chart_labs(self.chart, base, self.out709, "chart24_ev%+d" % int(ev))
                de = float(np.mean(C.delta_e2000(labs_param, labs_scene)))
            else:
                de = float("nan")
            res.append((ev, smp[gidx, :3].mean(), d, de))
            self.check(g, "exposure %+g stops = slide along the curve (grey vs wedge)" % ev, d <= 0.01,
                       "grey %.4f, |d| %.4f; chart dE %.2f" % (smp[gidx, :3].mean(), d, de), "|d| <= 0.01")
        self.data["exposure_slide"] = res
        # Full mode default: grain is zero-mean -> grey field mean close to Color Only grey
        fg = self.load("full_grey", "grey")[..., :3]
        mean = fg.reshape(-1, 3).mean(axis=0)
        self.check(g, "Full mode (default texture) 18% grey mean ~ Color Only grey", np.max(np.abs(mean - grey)) <= 0.01,
                   "%.4f %.4f %.4f (std %.4f)" % (mean[0], mean[1], mean[2], fg.std()), "|d| <= 0.01")

    # ------------------------------------------------------------------ input spaces
    def g_inputs(self):
        """Grey wedge -6..+6 stops encoded for every camera log: same film rendering as F-Log2 C."""
        g = "inputs"
        opts = self.P.options("inputSpace")
        stops = np.arange(-6.0, 6.0 + 1e-9, 0.5)
        lin = 0.18 * 2.0 ** stops
        L, plan = [], []
        for i, o in enumerate(opts):
            enc = C.log_encoder_for(o)
            if enc is None:
                self.skip(g, o, "no reference encoder for this label")
                continue
            img = np.zeros((2, len(stops) * 2, 4), np.float32)
            for k, v in enumerate(enc(lin)):
                img[:, 2 * k:2 * k + 2, :3] = np.float32(v)
            img[..., 3] = 1.0
            self.scene("inwedge_%02d" % i, img)
            L.append(self.line("in_%02d" % i, "inwedge_%02d" % i, {"inputSpace": ("index", i)}))
            plan.append((i, o))
        self.batch("inputs", L)
        ref = None
        res = {}
        for i, o in plan:
            out = self.load("in_%02d" % i, "inwedge_%02d" % i)
            v = out[0, 0::2, :3]
            res[o] = v
            if i == self.ix["inp"]:
                ref = v
        if ref is None:
            self.skip(g, "all", "F-Log2 C reference not rendered")
            return
        rows = []
        for i, o in plan:
            v = res[o]
            d = float(np.max(np.abs(v - ref)))
            neu = float(np.max(v.max(axis=1) - v.min(axis=1)))
            g18 = v[int(np.argmin(np.abs(stops)))].mean()
            rows.append(dict(input=o, max_diff=d, grey=float(g18), neutral=neu))
            if i == self.ix["inp"]:
                continue
            self.check(g, "%s: decodes to the same tone scale as F-Log2 C" % o, d <= 0.01,
                       "max |d| %.4f over -6..+6 stops, grey %.4f, neutral %.4f" % (d, g18, neu), "<= 0.01")
        self.data["inputs"] = rows

    # ------------------------------------------------------------------ outputs
    def g_outputs(self):
        g = "outputs"
        lines = []
        for i, o in enumerate(self.outputs):
            lines.append(self.line("out_%02d" % i, "chart", {"outputSpace": ("index", i)}))
            lines.append(self.line("outnat_%02d" % i, "natural_sheet", {"outputSpace": ("index", i), "mode": ("index", self.ix["full"])}, png=True))
        pq = self.opt("outputSpace", "pq|2084")
        if pq is not None:
            for peak in (600.0, 1000.0, 4000.0):
                for unroll in (0.0, 1.0):
                    lines.append(self.line("pq_%d_%d" % (peak, unroll * 100), "chart",
                                           {"outputSpace": ("index", pq), "hdrPeakNits": peak, "hdrUnroll": unroll}))
        self.batch("outputs", lines)
        rows = []
        for i, o in enumerate(self.outputs):
            sp = self.out_spaces[i]
            smp = self.chart.sample(self.load("out_%02d" % i, "chart"))
            m = A.wedge_metrics(self.chart, smp)
            t = A.torture_metrics(self.chart, smp, sp)
            gi = self.chart.group("wedge")[int(np.argmin(np.abs(np.array(m["stops"]))))]
            nits = float(sp.nits(smp[gi, :3]))
            mono = A.monotonic(m)
            row = dict(output=o, kind=sp.kind, grey=m["grey"], grey_nits=nits, monotonic=mono, max=m["max"],
                       nan=t["nan"] + t["inf"], peak_nits=float(np.max(sp.nits(np.array(m["values"])))))
            rows.append(row)
            self.check(g, "%s: monotonic, finite" % o, mono and t["nan"] == 0 and t["inf"] == 0,
                       "min step %.1e, NaN/Inf %d" % (m["min_step"], t["nan"] + t["inf"]))
            if not sp.hdr:
                self.check(g, "%s: encoded range [0,1]" % o, m["max"] <= 1.0 + 1e-6 and t["above1"] == 0 and t["below0"] == 0,
                           "max %.5f" % m["max"])
            if sp.prim == "p3dci":
                xyz = sp.to_xyz(smp[gi, :3])
                xy = xyz[:2] / xyz.sum()
                self.check(g, "%s: neutrals stay D65 (white simulated)" % o, np.hypot(xy[0] - 0.3127, xy[1] - 0.3290) <= 0.004,
                           "grey xy (%.4f, %.4f)" % tuple(xy), "D65 +- 0.004")
            if sp.kind == "hlg":
                self.info(g, "%s: 18%% grey" % o, "%.1f nits (1000-nit reference display)" % nits)
                self.check(g, "%s: within the 1000-nit reference display" % o, row["peak_nits"] <= 1010.0,
                           "peak %.0f nits" % row["peak_nits"], "<= 1000")
            if sp.kind == "pq":
                self.check(g, "%s: mid grey ~ 15 nits" % o, 10.0 <= nits <= 20.0, "%.2f nits" % nits, "~15 (10..20)")
        self.data["outputs"] = rows
        if pq is not None:
            sp = self.out_spaces[pq]
            res = {}
            for peak in (600.0, 1000.0, 4000.0):
                for unroll in (0.0, 1.0):
                    smp = self.chart.sample(self.load("pq_%d_%d" % (peak, unroll * 100), "chart"))
                    m = A.wedge_metrics(self.chart, smp)
                    n = sp.nits(np.array(m["values"]))
                    res[(peak, unroll)] = n
                    self.check(g, "PQ peak %d unroll %g: monotonic, <= peak" % (peak, unroll),
                               A.monotonic(m) and n.max() <= peak * 1.01 + 1, "max %.0f nits" % n.max(), "<= %d" % peak)
            for peak in (600.0, 1000.0, 4000.0):
                a, b = res[(peak, 0.0)], res[(peak, 1.0)]
                self.check(g, "PQ %d: unroll expands highlights toward peak" % peak, b.max() > a.max() * 1.2 and b.max() >= 0.6 * peak,
                           "unroll0 %.0f -> unroll1 %.0f nits" % (a.max(), b.max()), "> 1.2x and >= 60% of peak")
                self.info(g, "PQ %d mid grey (unroll 0/1)" % peak, "%.2f / %.2f nits" % (a[80], b[80]))
            self.data["pq_curves"] = {"%d_%g" % k: v.tolist() for k, v in res.items()}
        items = []
        for i, o in enumerate(self.outputs):
            img = self.load("outnat_%02d" % i, "natural_sheet")
            sp = self.out_spaces[i]
            if sp.hdr:
                img = img.copy()
                lin = sp.to_xyz(img[..., :3])
                rgb = C.xyz_to_rgb(lin, "rec709") * 100.0
                img[..., :3] = sheets.hdr_preview(rgb)
                o = o + " (tone-mapped preview)"
            items.append((o, img))
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_outputs.png"), "Output spaces (Full mode)", cols=3))

    # ------------------------------------------------------------------ modes
    def g_modes(self):
        g = "modes"
        heavy = {"halationAmount": 2.0, "bloomAmount": 2.0, "grainAmount": 3.0, "acutanceAmount": 2.0, "weaveAmount": 2.0,
                 "vignette": 1.0, "grainEnable": True}
        full = ("index", self.ix["full"])
        lines = [
            self.line("mode_full", "natural_sheet", {"mode": full}, png=True),
            self.line("mode_color", "natural_sheet", {"mode": ("index", self.ix["color"])}, png=True),
            self.line("mode_color_heavy", "natural_sheet", {"mode": ("index", self.ix["color"]), **heavy}),
            self.line("mode_full_textureoff", "natural_sheet", {"mode": full, **TEX_OFF}),
            self.line("mode_texture", "display", {"mode": ("index", self.ix["texture"])}, png=True),
            self.line("mode_texture_off", "display", {"mode": ("index", self.ix["texture"]), **TEX_OFF}),
            self.line("mode_texture_grain", "display", {"mode": ("index", self.ix["texture"]), **dict(TEX_OFF, grainEnable=True)}),
        ]
        self.batch("modes", lines)
        co = self.load("mode_color", "natural_sheet")
        d = float(np.max(np.abs(co - self.load("mode_color_heavy", "natural_sheet"))))
        self.check(g, "Color Only ignores all texture parameters", d <= 1e-6, "max diff %.2e" % d, "<= 1e-6")
        d = float(np.max(np.abs(co - self.load("mode_full_textureoff", "natural_sheet"))))
        self.check(g, "Full with every texture effect off == Color Only", d <= PARITY_TOL, "max diff %.2e" % d, "<= %.0e" % PARITY_TOL)
        src = self.scenes["display"][3].astype(np.float64)
        to = self.load("mode_texture_off", "display")
        d = float(np.max(np.abs(to[..., :3] - src[..., :3])))
        self.check(g, "Texture Only with texture off leaves the image unchanged", d <= 2e-3, "max diff %.2e" % d, "<= 2e-3")
        tx = self.load("mode_texture", "display")
        tg = self.load("mode_texture_grain", "display")
        dm = float(np.abs(tg[..., :3].mean(axis=(0, 1)) - src[..., :3].mean(axis=(0, 1))).max())
        self.check(g, "Texture Only grain is zero-mean on display-referred input", dm <= 0.005 and float(np.abs(tg - src).mean()) > 1e-4,
                   "max channel mean shift %.4f, mean |grain| %.4f" % (dm, float(np.abs(tg - src).mean())), "<= 0.005")
        dm = float(np.abs(tx[..., :3].mean(axis=(0, 1)) - src[..., :3].mean(axis=(0, 1))).max())
        self.info(g, "Texture Only (default texture) mean change", "max channel %.4f (halation/bloom add light)" % dm)
        fu = self.load("mode_full", "natural_sheet")
        self.check(g, "Full mode adds texture vs Color Only", float(np.abs(fu - co).mean()) > 1e-3,
                   "mean |Full - Color Only| %.4f" % float(np.abs(fu - co).mean()))
        for lab, sc in (("mode_full", "natural_sheet"), ("mode_texture", "display")):
            st = A.finite_stats(self.load(lab, sc))
            self.check(g, "%s finite and in range" % lab, st["nan"] == 0 and st["inf"] == 0 and st["max"] <= 1.0 + 1e-6 and st["min"] >= -1e-6,
                       "min %.4f max %.4f NaN %d" % (st["min"], st["max"], st["nan"]))
        self.sheets.append(sheets.contact_sheet(
            [("Full", fu), ("Color Only", co), ("Texture Only (display-referred input)", tx), ("Texture Only input", src)],
            os.path.join(self.out, "contact_modes.png"), "Modes", cols=2))

    # ------------------------------------------------------------------ module switches (v1.1)
    def g_modules(self):
        """Every module switch: off == the module's neutral values (bit-identical render, so a switched-off
        module costs nothing and changes nothing), on == the module's values (visible change)."""
        g = "modules"
        base = dict(TEX_OFF)
        L, todo = [], []
        cases = list(MODULES)
        pq = self.opt("outputSpace", "pq|2084")
        if pq is not None:   # the unroll only exists for HDR outputs
            cases.append(("hdrUnrollEnable", {"hdrUnroll": 1.0, "outputSpace": ("index", pq)},
                          {"hdrUnroll": 0.0, "outputSpace": ("index", pq)}, "full", "highlights"))
        for i, (sw, on, neutral, mode, sc) in enumerate(cases):
            if not self.has(sw):
                self.check(g, sw, False, "missing", "present")
                continue
            m = {"mode": ("index", self.ix[mode])}
            others = dict(base, grainEnable=sw == "grainEnable")
            L.append(self.line("mod%02d_on" % i, sc, dict(others, **m, **on, **{sw: True}), time_=2))
            L.append(self.line("mod%02d_off" % i, sc, dict(others, **m, **on, **{sw: False}), time_=2))
            L.append(self.line("mod%02d_neutral" % i, sc, dict(others, **m, **neutral, **{sw: True}), time_=2))
            todo.append((i, sw, mode, sc))
        if not L:
            return
        self.batch("modules", L)
        for i, sw, mode, sc in todo:
            on, off, neu = (self.load("mod%02d_%s" % (i, k), sc) for k in ("on", "off", "neutral"))
            d_off = float(np.max(np.abs(off - neu)))
            d_on, m_on = float(np.abs(on - neu).mean()), float(np.abs(on - neu).max())
            self.check(g, "%s (%s): off == neutral values" % (sw, mode), d_off == 0.0, "max diff %.2e" % d_off, "0 (bit-identical)")
            self.check(g, "%s (%s): on changes the image" % (sw, mode), d_on > 1e-4 or m_on > 0.01,
                       "mean diff %.5f, max %.4f" % (d_on, m_on), "mean > 1e-4 or max > 0.01")

    # ------------------------------------------------------------------ film damage / film blur (v1.1)
    def g_damage(self):
        """Film damage + film blur through the real bundle: off by default, per-frame dust, persistent
        scratches, frozen / seeded damage, physical placement (negative dust prints white, print dust
        dark), soft moments that come and go, finite output in every mode; contact sheet."""
        g = "damage"
        if not self.has("damageEnable"):
            self.check(g, "damageEnable present", False, "missing")
            return
        full = ("index", self.ix["full"])
        off = dict(TEX_OFF, mode=full)
        dmg = dict(off, damageEnable=True, dustAmount=1.5, scratchAmount=1.5, hairAmount=1.5)
        L = [self.line("dm_default", "natural", {"mode": full}, frames="0:1"),
             self.line("dm_off", "natural", off, time_=5),
             self.line("dm_anim", "natural", dmg, frames="0:5"),
             self.line("dm_again", "natural", dmg, time_=3),
             self.line("dm_frozen", "natural", dict(dmg, damageAnimated=False), frames="0:2"),
             self.line("dm_seed", "natural", dict(dmg, damageSeed=123), time_=3),
             self.line("dm_grey", "grey", off, time_=4),
             self.line("dm_white", "grey", dict(off, damageEnable=True, dustAmount=2.0, dustNegative=1.0, scratchAmount=0.0,
                                                 hairAmount=0.0), time_=4),
             self.line("dm_dark", "grey", dict(off, damageEnable=True, dustAmount=2.0, dustNegative=0.0, scratchAmount=0.0,
                                                hairAmount=0.0), time_=4),
             self.line("dm_blur", "natural", dict(off, filmBlurEnable=True, filmBlurFrequency=0.5, filmBlurDuration=12.0,
                                                   filmBlurRadius=8.0, filmBlurBreathing=0.0), frames="0:47"),
             self.line("dm_texonly", "display", dict(DAMAGE_ALL, mode=("index", self.ix["texture"])), time_=2),
             self.line("dm_half", "natural", dict(DAMAGE_ALL, mode=full), time_=2, scale=0.5)]
        sheet = [("off", off), ("dust (white + dark)", dict(off, damageEnable=True, dustAmount=2.0, scratchAmount=0.0, hairAmount=0.0)),
                 ("scratches", dict(off, damageEnable=True, dustAmount=0.0, scratchAmount=2.0, scratchEmulsion=0.6, hairAmount=0.0)),
                 ("hair", dict(off, damageEnable=True, dustAmount=0.0, scratchAmount=0.0, hairAmount=2.0)),
                 ("all damage", dict(DAMAGE_ALL, mode=full, filmBlurEnable=False)), ("film blur peak", dict(off, filmBlurEnable=True,
                  filmBlurFrequency=1.0, filmBlurBreathing=1.0, filmBlurRadius=6.0))]
        for i, (lab, ov) in enumerate(sheet):
            L.append(self.line("dms_%d" % i, "natural_sheet", ov, png=True, time_=2))
        presets = self.P.options("lookPreset")
        for name in ("Worn Release Print", "Found Footage 16"):
            if name in presets:
                L.append(self.line("dms_p_%s" % name.split()[0], "natural_sheet", {"mode": full}, png=True, time_=2,
                                   post=[("lookPreset", ("index", presets.index(name)))]))
        self.batch("damage", L)
        d0 = self.load("dm_default", "natural", 0)
        self.check(g, "film damage / blur off by default", self.P.default("damageEnable") in (False, 0) and
                   self.P.default("filmBlurEnable") in (False, 0), "defaults %s / %s" % (self.P.default("damageEnable"),
                                                                                         self.P.default("filmBlurEnable")))
        fr = [self.load("dm_anim", "natural", t) for t in range(6)]
        base = self.load("dm_off", "natural")
        diffs = [float(np.abs(f - base).max()) for f in fr]
        self.check(g, "damage visible on every frame", min(diffs) > 0.05, "min max|damage - clean| %.3f" % min(diffs))
        ch = [float((np.abs(fr[t] - fr[t + 1]).max(axis=2) > 0.02).mean()) for t in range(5)]
        self.check(g, "damage changes from frame to frame (dust lives one frame)", min(ch) > 1e-4, "min changed share %.5f" % min(ch))
        d = float(np.abs(self.load("dm_again", "natural") - fr[3]).max())
        self.check(g, "deterministic: frame 3 rendered again is identical", d == 0.0, "max diff %.2e" % d)
        fz = [self.load("dm_frozen", "natural", t) for t in range(3)]
        d = max(float(np.abs(fz[0] - fz[t]).max()) for t in (1, 2))
        self.check(g, "Animated Damage off: frozen", d == 0.0, "max diff %.2e" % d)
        d = float(np.abs(self.load("dm_seed", "natural") - fr[3]).mean())
        self.check(g, "Damage Seed changes the damage", d > 1e-4, "mean diff %.5f" % d)
        grey = self.load("dm_grey", "grey")[..., :3].mean(axis=2)
        wh = self.load("dm_white", "grey")[..., :3].mean(axis=2) - grey
        dk = self.load("dm_dark", "grey")[..., :3].mean(axis=2) - grey
        self.check(g, "negative dust prints WHITE (only brighter specks)", wh.max() > 0.2 and wh.min() > -0.02,
                   "speck change %+.3f .. %+.3f" % (wh.min(), wh.max()))
        self.check(g, "print dust is DARK (only darker specks)", dk.min() < -0.2 and dk.max() < 0.02,
                   "speck change %+.3f .. %+.3f" % (dk.min(), dk.max()))
        bl = [self.load("dm_blur", "natural", t)[..., 1] for t in range(48)]
        sharp = self.load("dm_off", "natural")[..., 1]
        hf = lambda im: float(np.abs(np.diff(im, axis=1)).mean())
        ratio = np.array([hf(b) / hf(sharp) for b in bl])
        soft = int((ratio < 0.8).sum())
        steps = float(np.abs(np.diff(ratio)).max())
        self.data["film_blur_ratio"] = [round(float(x), 3) for x in ratio]
        self.check(g, "film blur: soft some of the time, sharp the rest", 4 <= soft <= 44 and ratio.max() > 0.97,
                   "%d / 48 soft frames, detail ratio %.2f..%.2f" % (soft, ratio.min(), ratio.max()))
        self.check(g, "film blur eases in and out", steps < 0.25, "max frame-to-frame detail change %.3f" % steps)
        for lab, sc, sca in (("dm_texonly", "display", 1.0), ("dm_half", "natural", 0.5)):
            st = A.finite_stats(self.load(lab, sc, scale=sca))
            self.check(g, "%s finite and in range" % lab, st["nan"] == 0 and st["inf"] == 0 and st["max"] <= 1.0 + 1e-6 and
                       st["min"] >= -1e-6, "min %.4f max %.4f NaN %d" % (st["min"], st["max"], st["nan"]))
        items = [(lab, self.load("dms_%d" % i, "natural_sheet")) for i, (lab, _) in enumerate(sheet)]
        for name in ("Worn Release Print", "Found Footage 16"):
            if name in presets:
                items.append(("preset " + name, self.load("dms_p_%s" % name.split()[0], "natural_sheet")))
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_damage.png"),
                                                "Film damage and film blur (Full mode, frame 2)", cols=4))

    # ------------------------------------------------------------------ texture features
    def g_texture(self):
        g = "texture"
        full = ("index", self.ix["full"])
        off = dict(TEX_OFF, mode=full)
        L = []
        L.append(self.line("tx_base_hl", "highlights", off))
        L.append(self.line("tx_base_nat", "natural", off, png=True))
        L.append(self.line("tx_halation", "highlights", dict(off, halationAmount=1.0)))
        L.append(self.line("tx_halation_hue", "highlights", dict(off, halationAmount=1.0, halationHue=1.0)))
        L.append(self.line("tx_halation_r", "highlights", dict(off, halationAmount=1.0, halationRadius=60.0)))
        L.append(self.line("tx_bloom", "highlights", dict(off, bloomAmount=1.0)))
        L.append(self.line("tx_acut", "highlights", dict(off, acutanceAmount=1.5)))
        L.append(self.line("tx_acut_neg", "highlights", dict(off, acutanceAmount=-1.0)))
        L.append(self.line("tx_vig", "natural", dict(off, vignette=1.0)))
        L.append(self.line("tx_grey_base", "grey", off))
        fmts = self.P.options("grainFormat")
        for i, f in enumerate(fmts):
            L.append(self.line("tx_grain_f%d" % i, "grey", dict(off, grainEnable=True, grainAmount=1.0, grainFormat=("index", i))))
        L.append(self.line("tx_grain_soft0", "grey", dict(off, grainEnable=True, grainSoftness=0.0)))
        L.append(self.line("tx_grain_soft1", "grey", dict(off, grainEnable=True, grainSoftness=1.0)))
        L.append(self.line("tx_grain_size2", "grey", dict(off, grainEnable=True, grainSize=2.0)))
        L.append(self.line("tx_grain_amt2", "grey", dict(off, grainEnable=True, grainAmount=2.0)))
        bw = self.opt("negStock", r"\bBW\b|b&w")
        if bw is not None:
            L.append(self.line("tx_grain_bw", "grey", dict(off, grainEnable=True, negStock=("index", bw))))
            L.append(self.line("tx_grey_base_bw", "grey", dict(off, negStock=("index", bw))))
        for feat, ov in (("halation", dict(halationAmount=1.0)), ("bloom", dict(bloomAmount=1.0)),
                         ("grain", dict(grainEnable=True, grainAmount=1.5)), ("acutance", dict(acutanceAmount=1.5)),
                         ("weave", dict(weaveAmount=2.0)), ("vignette", dict(vignette=1.0))):
            L.append(self.line("txs_" + feat, "natural_sheet", dict(off, **ov), png=True, time_=3))
        L.append(self.line("txs_all", "natural_sheet", {"mode": full, "halationAmount": 1.0, "bloomAmount": 0.6, "grainAmount": 1.5,
                                                        "acutanceAmount": 0.8, "weaveAmount": 1.0, "vignette": 0.5}, png=True, time_=3))
        L.append(self.line("txs_off", "natural_sheet", off, png=True, time_=3))
        # halation acceptance at the DEFAULT halation settings (only halation on)
        hal_def = {k: v for k, v in off.items() if k != "halationAmount"}
        L.append(self.line("tx_hd_off", "halation_day", off))
        L.append(self.line("tx_hd_def", "halation_day", hal_def))
        self.batch("texture", L)
        d = self.load("tx_hd_def", "halation_day")[..., :3] - self.load("tx_hd_off", "halation_day")[..., :3]
        rg = d[..., 0] - d[..., 1]
        wr, sr = self.hal_regions["window"], self.hal_regions["skin"]
        cx, cy, r, s = self.hal_regions["practical"]
        pr = float(d[int(cy), int(cx + r + 6 * s), 0])
        self.check(g, "halation (default): overcast sky does not flood a dark window red", float(rg[wr].max()) <= 0.02,
                   "window max d(R-G) %.4f" % float(rg[wr].max()), "<= 0.02")
        self.check(g, "halation (default): skin against the sky keeps its colour", float(d[..., 0][sr].max()) <= 0.03,
                   "skin edge max dR %.4f" % float(d[..., 0][sr].max()), "<= 0.03")
        self.check(g, "halation (default): +8-stop practical glows clearly", pr >= 0.08,
                   "dR 6 px outside the source %.3f" % pr, ">= 0.08")
        base = self.load("tx_base_hl", "highlights")
        h, w = base.shape[:2]
        r_spot = max(1.5, h * 0.01)
        # halation: energy outside the spot core, red dominant
        def halo(lab):
            d = self.load(lab, "highlights") - base
            e = np.zeros(3)
            for cx, cy, ev in self.hl_points:
                e += A.ring_energy(d, cx, cy, r_spot * 1.6, h * 0.12)
            return e, d
        e, d = halo("tx_halation")
        self.check(g, "halation adds light around highlights", e.sum() > 1e-3, "ring energy R %.4f G %.4f B %.4f" % tuple(e), "> 0")
        self.check(g, "halation is red/orange (R > G > B)", e[0] > e[1] > e[2] - 1e-9, "R/G/B %.4f/%.4f/%.4f" % tuple(e))
        dl = d[..., :3].mean(axis=2)
        self.check(g, "halation only adds light (blur - local >= 0)", float(np.percentile(dl, 0.5)) >= -2e-3,
                   "0.5th percentile of luminance diff %.4f" % float(np.percentile(dl, 0.5)), ">= -0.002")
        e2, _ = halo("tx_halation_hue")
        self.check(g, "halationHue shifts the tint (more green toward orange)", e2[1] / max(e2[0], 1e-9) > e[1] / max(e[0], 1e-9),
                   "G/R %.3f -> %.3f" % (e[1] / max(e[0], 1e-9), e2[1] / max(e2[0], 1e-9)))
        er, _ = halo("tx_halation_r")
        far = lambda lab: sum(A.ring_energy(self.load(lab, "highlights") - base, cx, cy, h * 0.06, h * 0.12).sum() for cx, cy, _ in self.hl_points)
        self.check(g, "halationRadius widens the halo", far("tx_halation_r") > far("tx_halation"),
                   "far-ring energy %.4f -> %.4f" % (far("tx_halation"), far("tx_halation_r")))
        eb, db = halo("tx_bloom")
        neutral = eb.max() > 1e-4 and (eb.max() - eb.min()) / max(eb.max(), 1e-9) < 0.5
        self.check(g, "bloom adds a wide, neutral glow", neutral, "ring energy R %.4f G %.4f B %.4f" % tuple(eb))
        # acutance: edge between 0.05 and 0.5 linear at x = w/2, y >= 0.6 h
        y0, y1, xe = int(h * 0.65), int(h * 0.95), w // 2
        c0, g0 = A.edge_contrast(base, y0, y1, xe, 6)
        c1, g1 = A.edge_contrast(self.load("tx_acut", "highlights"), y0, y1, xe, 6)
        c2, g2 = A.edge_contrast(self.load("tx_acut_neg", "highlights"), y0, y1, xe, 6)
        self.check(g, "acutance > 0 sharpens edges, < 0 softens", g1 > g0 * 1.02 and g2 < g0 * 0.98,
                   "edge gradient %.4f (neg) / %.4f (0) / %.4f (pos)" % (g2, g0, g1))
        # vignette
        vb = self.load("tx_base_nat", "natural")
        vv = self.load("tx_vig", "natural")
        hh, ww = vb.shape[:2]
        cen = slice(hh // 2 - 4, hh // 2 + 4), slice(ww // 2 - 4, ww // 2 + 4)
        cor = (slice(0, 6), slice(0, 6))
        rc = float(vv[cen][..., :3].mean() / max(vb[cen][..., :3].mean(), 1e-9))
        rk = float(vv[cor][..., :3].mean() / max(vb[cor][..., :3].mean(), 1e-9))
        self.check(g, "vignette darkens corners, keeps the centre", rk < 0.9 and abs(rc - 1) < 0.02, "centre x%.3f, corner x%.3f" % (rc, rk))
        # grain
        gb = self.load("tx_grey_base", "grey")[..., :3]
        stats = {}
        for i, f in enumerate(fmts):
            n = self.load("tx_grain_f%d" % i, "grey")[..., :3] - gb
            stats[f] = dict(std=float(n.std()), mean=float(n.mean()), corr_len=A.autocorr_length(n.mean(axis=2)),
                            **A.channel_corr(n))
        self.data["grain_formats"] = stats
        s35 = next((f for f in fmts if re.search(GRAIN_FORMATS[2][1], f, re.I)), fmts[0] if fmts else None)
        if s35:
            st = stats[s35]
            self.check(g, "grain present, zero-mean (S35 default)", st["std"] > 0.002 and abs(st["mean"]) < 0.25 * st["std"],
                       "std %.4f mean %+.5f" % (st["std"], st["mean"]))
            cc = np.mean([st["rg"], st["rb"], st["gb"]])
            self.check(g, "grain layers strongly correlated (mostly luminance grain, 0.90 nominal)", 0.72 <= cc <= 0.97,
                       "rg %.2f rb %.2f gb %.2f" % (st["rg"], st["rb"], st["gb"]), "0.90 (+-0.15)")
        order = []
        for name, pt in GRAIN_FORMATS[:4]:
            f = next((x for x in fmts if re.search(pt, x, re.I)), None)
            if f:
                order.append((name, stats[f]["corr_len"]))
        mono = all(order[i][1] >= order[i + 1][1] - 0.05 for i in range(len(order) - 1))
        self.check(g, "grain size by format: Super 8 > 16 mm > Super 35 > 65 mm", mono and len(order) == 4 and order[0][1] > order[-1][1],
                   ", ".join("%s %.2fpx" % o for o in order))
        stl = next((x for x in fmts if re.search("still", x, re.I)), None)
        if stl and s35:
            self.info(g, "35 mm stills grain size", "%.2f px (S35 %.2f px; spec ~0.75x)" % (stats[stl]["corr_len"], stats[s35]["corr_len"]))
        n0 = self.load("tx_grain_soft0", "grey")[..., :3] - gb
        n1 = self.load("tx_grain_soft1", "grey")[..., :3] - gb
        self.check(g, "grainSoftness rolls off fine detail", A.autocorr_length(n1.mean(axis=2)) > A.autocorr_length(n0.mean(axis=2)),
                   "corr length %.2f -> %.2f px" % (A.autocorr_length(n0.mean(axis=2)), A.autocorr_length(n1.mean(axis=2))))
        nsz = self.load("tx_grain_size2", "grey")[..., :3] - gb
        self.check(g, "grainSize 2 = coarser grain", A.autocorr_length(nsz.mean(axis=2)) > stats[s35]["corr_len"] if s35 else False,
                   "corr length %.2f px (size 1: %.2f)" % (A.autocorr_length(nsz.mean(axis=2)), stats[s35]["corr_len"] if s35 else 0))
        na = self.load("tx_grain_amt2", "grey")[..., :3] - gb
        if s35:
            ratio = na.std() / max(stats[s35]["std"], 1e-9)
            self.check(g, "grainAmount scales grain (2x amount)", 1.5 <= ratio <= 2.5, "std ratio %.2f" % ratio, "~2")
        if bw is not None:
            nb = self.load("tx_grain_bw", "grey")[..., :3] - self.load("tx_grey_base_bw", "grey")[..., :3]
            cc = A.channel_corr(nb)
            self.check(g, "monochrome stock -> monochrome grain", min(cc.values()) > 0.98, "channel corr min %.3f" % min(cc.values()))
        items = []
        for feat in ("off", "halation", "bloom", "grain", "acutance", "weave", "vignette", "all"):
            items.append((feat, self.load("txs_" + feat, "natural_sheet")))
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_texture.png"), "Texture features (Full mode, frame 3)", cols=4))
        for lab in ("txs_all", "tx_halation", "tx_bloom"):
            sc = "natural_sheet" if lab.startswith("txs") else "highlights"
            st = A.finite_stats(self.load(lab, sc))
            self.check(g, "%s finite, in [0,1]" % lab, st["nan"] == 0 and st["inf"] == 0 and st["max"] <= 1 + 1e-6 and st["min"] >= -1e-6,
                       "min %.4f max %.4f" % (st["min"], st["max"]))

    # ------------------------------------------------------------------ presets
    def g_presets(self):
        g = "presets"
        opts = self.P.options("lookPreset")
        if len(opts) < 2:
            self.skip(g, "presets", "no presets")
            return
        jdir = os.path.join(self.out, "host")
        full = {"mode": ("index", self.ix["full"])}
        L = [self.line("preset_00", "natural_sheet", full, post=[("lookPreset", ("index", 0))], png=True,
                       extra=["--list-params-json", os.path.join(jdir, "preset_00.json")])]
        for i in range(1, len(opts)):
            # the preset is applied after the base values, like a user picking it in the UI
            L.append(self.line("preset_%02d" % i, "natural_sheet", full, post=[("lookPreset", ("index", i))],
                               png=True, extra=["--list-params-json", os.path.join(jdir, "preset_%02d.json" % i)]))
            # colour-only chart render of the preset (texture would bleed between the wedge patches)
            L.append(self.line("presetc_%02d" % i, "chart", {}, post=[("lookPreset", ("index", i)), ("mode", ("index", self.ix["color"]))]))
        # "(modified)" marking after a user edit
        L.append(self.line("preset_mod", "chart", {}, post=[("lookPreset", ("index", 1)), ("printerR", 1.5)], out=False,
                           extra=["--list-params-json", os.path.join(jdir, "preset_mod.json")]))
        self.batch("presets", L)
        ref = {p["name"]: p for p in json.load(open(os.path.join(jdir, "preset_00.json")))["params"]}
        items = [(opts[0], self.load("preset_00", "natural_sheet"))]
        rows = []
        base_img = self.load("preset_00", "natural_sheet")
        for i in range(1, len(opts)):
            cur = {p["name"]: p for p in json.load(open(os.path.join(jdir, "preset_%02d.json" % i)))["params"]}
            changed = [n for n, p in cur.items() if "value" in p and n in ref and p["value"] != ref[n].get("value")
                       and n not in ("lookPreset", "lookPresetStatus")]
            img = self.load("preset_%02d" % i, "natural_sheet")
            st = A.finite_stats(img)
            smp = self.chart.sample(self.load("presetc_%02d" % i, "chart"))
            m = A.wedge_metrics(self.chart, smp)
            status = cur.get("lookPresetStatus", {}).get("value", "")
            diff = float(np.abs(img - base_img).mean())
            rows.append(dict(preset=opts[i], changed=changed, status=status, grey=m["grey"], monotonic=A.monotonic(m),
                             mean_diff_vs_custom=diff))
            applied = opts[i].lower() in status.lower() and "modified" not in status.lower()
            self.check(g, "'%s' applies (status label, settings)" % opts[i], applied or len(changed) >= 1,
                       "status '%s', %d params changed, mean diff vs Custom %.4f" % (status, len(changed), diff), "",
                       ", ".join(changed[:10]))
            self.check(g, "'%s' renders finite, monotonic" % opts[i], st["nan"] == 0 and st["inf"] == 0 and A.monotonic(m),
                       "grey %.3f, min step %.1e" % (np.mean(m["grey"]), m["min_step"]))
            items.append((opts[i], img))
        self.data["presets"] = rows
        imgs = [self.load("preset_%02d" % i, "natural_sheet") for i in range(1, len(opts))]
        dmin, pair = 1e9, ""
        for i in range(len(imgs)):
            for j in range(i + 1, len(imgs)):
                d = float(np.abs(imgs[i] - imgs[j]).mean())
                if d < dmin:
                    dmin, pair = d, "%s vs %s" % (opts[i + 1], opts[j + 1])
        if len(imgs) > 1:
            self.check(g, "presets are distinct looks", dmin > 2e-3, "min mean |diff| %.4f (%s)" % (dmin, pair), "> 0.002")
        # colour distinctness (texture excluded): colour-only chart renders, mean dE2000 >= 2
        labs = []
        for i in range(1, len(opts)):
            smp = self.chart.sample(self.load("presetc_%02d" % i, "chart"))[:, :3]
            labs.append(C.xyz_to_lab(self.out709.to_xyz(smp)))
        cmin, cpair = 1e9, ""
        for i in range(len(labs)):
            for j in range(i + 1, len(labs)):
                d = float(np.mean(C.delta_e2000(labs[i], labs[j])))
                if d < cmin:
                    cmin, cpair = d, "%s vs %s" % (opts[i + 1], opts[j + 1])
        if len(labs) > 1:
            self.check(g, "presets have distinct colour (colour-only chart)", cmin >= 2.0,
                       "min mean dE2000 %.2f (%s)" % (cmin, cpair), ">= 2.0")
        mod = {p["name"]: p for p in json.load(open(os.path.join(jdir, "preset_mod.json")))["params"]}
        st = mod.get("lookPresetStatus", {}).get("value", "")
        self.check(g, "user edit after a preset marks '(modified)' (or resets to Custom)",
                   "modified" in st.lower() or mod.get("lookPreset", {}).get("value") == 0, "status '%s'" % st)
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_presets.png"), "Look presets (Full mode)", cols=4))

    # ------------------------------------------------------------------ animation
    def g_animation(self):
        g = "animation"
        full = ("index", self.ix["full"])
        off = dict(TEX_OFF, mode=full)
        L = [self.line("anim_grain", "natural", dict(off, grainEnable=True, grainAnimated=True), frames="0:3"),
             self.line("anim_static", "natural", dict(off, grainEnable=True, grainAnimated=False), frames="0:3"),
             self.line("anim_seed", "natural", dict(off, grainEnable=True, grainAnimated=False, grainSeed=17), time_=0),
             self.line("anim_weave", "natural", dict(off, weaveAmount=2.0), frames="0:5"),
             self.line("anim_weave_fast", "natural", dict(off, weaveAmount=2.0, weaveSpeed=4.0), frames="0:5"),
             self.line("anim_again", "natural", dict(off, grainEnable=True, grainAnimated=True), time_=2),
             self.line("anim_default", "natural", {"mode": full}, frames="0:2")]
        self.batch("animation", L)
        f = [self.load("anim_grain", "natural", t) for t in range(4)]
        d = min(float(np.abs(f[i] - f[i + 1]).mean()) for i in range(3))
        self.check(g, "animated grain: new pattern every frame", d > 1e-3, "min mean |frame diff| %.4f" % d, "> 1e-3")
        s = [self.load("anim_static", "natural", t) for t in range(4)]
        d = max(float(np.abs(s[0] - s[i]).max()) for i in range(1, 4))
        self.check(g, "grainAnimated off: identical frames", d <= 1e-6, "max diff %.2e" % d, "<= 1e-6")
        d = float(np.abs(self.load("anim_seed", "natural") - s[0]).mean())
        self.check(g, "grainSeed changes the pattern", d > 1e-3, "mean diff %.4f" % d)
        d = float(np.abs(self.load("anim_again", "natural") - f[2]).max())
        self.check(g, "deterministic: same frame rendered again is identical", d <= 1e-6, "max diff %.2e" % d, "<= 1e-6")
        wv = [self.load("anim_weave", "natural", t)[..., :3].mean(axis=2) for t in range(6)]
        shifts = [A.estimate_shift(wv[0], wv[t]) for t in range(1, 6)]
        mx = max(abs(x) for x, _ in shifts)
        my = max(abs(y) for _, y in shifts)
        self.data["weave_shifts"] = shifts
        self.check(g, "gate weave moves the frame over time", max(mx, my) > 0.2, "max |dx| %.2f |dy| %.2f px" % (mx, my))
        self.check(g, "gate weave mostly vertical", my >= mx, "|dy| %.2f vs |dx| %.2f" % (my, mx))
        wf = [self.load("anim_weave_fast", "natural", t)[..., :3].mean(axis=2) for t in range(6)]
        sf = [A.estimate_shift(wf[t], wf[t + 1]) for t in range(5)]
        ss = [A.estimate_shift(wv[t], wv[t + 1]) for t in range(5)]
        vf = np.mean([np.hypot(*x) for x in sf])
        vs = np.mean([np.hypot(*x) for x in ss])
        self.check(g, "weaveSpeed 4 moves faster than 1", vf > vs, "mean frame-to-frame motion %.3f -> %.3f px" % (vs, vf))
        dd = [self.load("anim_default", "natural", t) for t in range(3)]
        self.check(g, "default settings animate (grain on)", float(np.abs(dd[0] - dd[1]).mean()) > 1e-4,
                   "mean diff %.5f" % float(np.abs(dd[0] - dd[1]).mean()))

    # ------------------------------------------------------------------ render scale
    def g_scale(self):
        g = "render_scale"
        full = ("index", self.ix["full"])
        nograin = {"mode": full, "grainEnable": False, "halationAmount": 1.0, "bloomAmount": 0.6, "acutanceAmount": 0.8,
                   "vignette": 0.5, "weaveAmount": 0.0}
        L = [self.line("rs_full", "natural960", nograin), self.line("rs_half", "natural960", nograin, scale=0.5),
             self.line("rs_color_full", "natural960", {}), self.line("rs_color_half", "natural960", {}, scale=0.5),
             self.line("rs_grain_full", "natural960", {"mode": full}), self.line("rs_grain_half", "natural960", {"mode": full}, scale=0.5)]
        rep = self.batch("render_scale", L)
        fr = job_frames(rep, "rs_half")
        sz = (fr[0]["width"], fr[0]["height"]) if fr else None
        self.check(g, "render scale 0.5 -> half-size images", sz == (480, 270), str(sz), "(480, 270)")
        full_img = A.downsample2(self.load("rs_full", "natural960"))
        half = self.load("rs_half", "natural960", scale=0.5)
        dif = np.abs(full_img - half)[..., :3]
        # pointwise reference: Color Only differs only through filtering-before vs after the curve
        cf = A.downsample2(self.load("rs_color_full", "natural960"))
        ch = self.load("rs_color_half", "natural960", scale=0.5)
        dc = np.abs(cf - ch)[..., :3]
        excess_mean = float(dif.mean() - dc.mean())
        excess_p99 = float(np.percentile(dif, 99) - np.percentile(dc, 99))
        self.data["render_scale"] = dict(mean=float(dif.mean()), p99=float(np.percentile(dif, 99)), max=float(dif.max()),
                                         color_mean=float(dc.mean()), color_p99=float(np.percentile(dc, 99)))
        self.info(g, "Color Only at 0.5 vs downsampled full (pointwise baseline)", "mean %.4f p99 %.4f" % (dc.mean(), np.percentile(dc, 99)))
        self.check(g, "texture at 0.5 ~ downsampled full res (resolution independent)", excess_mean <= 0.005 and excess_p99 <= 0.05,
                   "mean %.4f p99 %.4f (excess over baseline: mean %.4f, p99 %.4f)" % (dif.mean(), np.percentile(dif, 99), excess_mean, excess_p99),
                   "excess mean <= 0.005, p99 <= 0.05", "halation 1, bloom 0.6, acutance 0.8, vignette 0.5, grain off")
        gf = self.load("rs_grain_full", "natural960")
        gh = self.load("rs_grain_half", "natural960", scale=0.5)
        cut = (slice(int(270 * 0.57), int(270 * 0.73)), slice(int(480 * 0.67), int(480 * 0.75)))   # grey card
        sd_half = float(gh[cut][..., :3].std())
        sd_full = float(A.downsample2(gf)[cut][..., :3].std())
        self.check(g, "grain at 0.5 looks like the downsampled full-res grain", 0.6 <= sd_half / max(sd_full, 1e-9) <= 1.6,
                   "grey-card std half %.4f vs full(ds) %.4f" % (sd_half, sd_full), "ratio 0.6..1.6")

    # ------------------------------------------------------------------ GPU parity
    def g_gpu(self):
        g = "gpu_parity"
        if not self.gpu:
            self.skip(g, "CPU vs OpenCL", "--no-gpu")
            return
        if self.desc.get("plugin", {}).get("gpu", {}).get("openclBuffers") != "true":
            self.check(g, "plug-in declares OpenCL", False, "openclBuffers=%s" % self.desc.get("plugin", {}).get("gpu", {}).get("openclBuffers"))
            return
        full = ("index", self.ix["full"])
        pq = self.opt("outputSpace", "pq|2084")
        cfgs = [("chart_color", "chart", {}, {}), ("nat_full_default", "natural", {"mode": full}, {"time_": 2}),
                ("nat_full_all", "natural", {"mode": full, "halationAmount": 1.5, "bloomAmount": 1.0, "grainAmount": 2.0,
                                              "acutanceAmount": 1.0, "weaveAmount": 1.5, "vignette": 0.6}, {"time_": 3}),
                ("display_texture", "display", {"mode": ("index", self.ix["texture"])}, {}),
                ("hl_full", "highlights", {"mode": full, "halationAmount": 2.0, "bloomAmount": 2.0}, {}),
                ("nat_half", "natural", {"mode": full}, {"scale": 0.5}),
                ("nat_damage_blur", "natural", dict(DAMAGE_ALL, mode=full), {"time_": 2}),
                ("display_damage_blur", "display", dict(DAMAGE_ALL, mode=("index", self.ix["texture"])), {"time_": 2})]
        if pq is not None:
            cfgs.append(("chart_pq", "chart", {"outputSpace": ("index", pq)}, {}))
        for n in sorted(set([0, len(self.negs) - 1, self.opt("negStock", "RV|slide", default=0)])):
            cfgs.append(("chart_neg%02d" % n, "chart", {"negStock": ("index", n)}, {}))
            if self.ix["proc_scan"] is not None and n == 0:
                cfgs.append(("chart_scan%02d" % n, "chart", {"negStock": ("index", n), "process": ("index", self.ix["proc_scan"])}, {}))
        if not self.a.quick:
            # SPEC §12: the real bundle renders on CPU and OpenCL for all stock pairs, modes and presets
            for n in range(len(self.negs)):
                for p in range(len(self.prints)):
                    cfgs.append(("pair_n%02d_p%02d" % (n, p), "chart", {"negStock": ("index", n), "printStock": ("index", p)}, {}))
                if self.ix["proc_scan"] is not None:
                    cfgs.append(("pair_n%02d_scan" % n, "chart", {"negStock": ("index", n), "process": ("index", self.ix["proc_scan"])}, {}))
            for i in range(len(self.P.options("mode"))):
                cfgs.append(("mode%d" % i, "natural" if i != self.ix["texture"] else "display", {"mode": ("index", i)}, {"time_": 1}))
            for i in range(1, len(self.P.options("lookPreset"))):
                cfgs.append(("preset%02d" % i, "natural", {"mode": full}, {"time_": 1, "post": [("lookPreset", ("index", i))]}))
            for i in range(len(self.outputs)):
                cfgs.append(("out%02d" % i, "natural", {"mode": full, "outputSpace": ("index", i)}, {"time_": 1}))
                # Texture Only decodes the display encoding: PQ/HLG highlights are numerically the
                # most sensitive path (pseudo-scene expansion), so every output is checked there too
                cfgs.append(("tex_out%02d" % i, "display", {"mode": ("index", self.ix["texture"]), "outputSpace": ("index", i),
                                                            "halationAmount": 1.5, "bloomAmount": 1.0}, {"time_": 1}))
            # the chart's torture patches (NaN/Inf/negative/huge) through every texture pass
            cfgs.append(("chart_full_all", "chart", {"mode": full, "halationAmount": 1.5, "bloomAmount": 1.0, "grainAmount": 2.0,
                                                     "acutanceAmount": 1.0, "weaveAmount": 1.5, "vignette": 0.6}, {"time_": 4}))
        L = []
        for name, sc, ov, kw in cfgs:
            L.append(self.line("cpu_" + name, sc, ov, **kw))
            L.append(self.line("gpu_" + name, sc, ov, gpu=True, **kw))
        rep = self.batch("gpu_parity", L, check=False, group=g)
        worst = 0.0
        rows = []
        clmsgs = [ln.strip() for ln in rep.get("_stderr", "").splitlines()
                  if re.search(r"opencl|\bgpu\b", ln, re.I) and re.search(r"fail|error|instead|fall", ln, re.I)]
        if clmsgs:
            self.info(g, "plug-in GPU diagnostics (stderr)", clmsgs[0][:300], " | ".join(m[:160] for m in clmsgs[1:3]))
        for name, sc, ov, kw in cfgs:
            fr = job_frames(rep, "gpu_" + name)
            err = job_error(rep, "gpu_" + name) or job_error(rep, "cpu_" + name)
            if err or not fr:
                continue   # recorded by batch() as a failed job
            used = fr[0].get("gpu")
            scale = kw.get("scale") or 1.0
            a = self.load("cpu_" + name, sc, scale=scale)
            b = self.load("gpu_" + name, sc, scale=scale)
            d = np.abs(a - b)[..., :3]
            mx = float(np.nan_to_num(d, nan=1.0).max())
            worst = max(worst, mx)
            withdrawn = bool(fr[0].get("gpuWithdrawn"))
            rows.append(dict(config=name, max=mx, mean=float(np.nanmean(d)), gpu=used, fallback=fr[0].get("gpuFellBack"),
                             withdrawn=withdrawn))
            self.check(g, name, mx <= PARITY_TOL and used == "opencl-buffers" and not fr[0].get("gpuFellBack") and not withdrawn,
                       "max %.2e mean %.2e (%s%s)" % (mx, float(np.nanmean(d)), used,
                                                    ", plug-in withdrew OpenCL = rendered on CPU" if withdrawn else ""),
                       "<= %.0e on the GPU path" % PARITY_TOL)
        self.data["gpu_parity"] = rows
        self.info(g, "worst max abs diff", "%.3e" % worst)

    # ------------------------------------------------------------------ LUT export
    def g_lut(self):
        g = "lut_export"
        if not (self.has("lutExport") and self.has("lutPath") and self.has("lutSize")):
            self.skip(g, "LUT export", "params missing")
            return
        if not self.licensed:
            # LUT export is a licensed feature: the trial must refuse it with a message, write nothing
            d = os.path.join(self.out, "luts", "trial")
            if os.path.isdir(d):
                shutil.rmtree(d)
            os.makedirs(d, exist_ok=True)
            rep = self.host.batch("lut_trial", [self.line("lut_trial", "natural" if "natural" in self.scenes else "chart", {},
                                                          out=False) + ["--set", "lutPath=" + d, "--press", "lutExport"]],
                                  check=False)
            msgs = [m["text"] for m in rep["messages"]]
            self.check(g, "trial: LUT export refused (licensed feature), no file written",
                       not glob.glob(os.path.join(d, "*.cube")) and any("licensed version" in m for m in msgs),
                       " | ".join(m.replace("\n", " ")[:100] for m in msgs[-2:]))
            self.skip(g, "LUT export accuracy", "needs a license (use --license-key or --dev-license)")
            return
        sizes = self.P.options("lutSize")
        want = [i for i, o in enumerate(sizes) if re.sub(r"\D", "", o) in (("17", "33", "65") if self.a.full else ("17", "33"))]
        configs = [("default", {})]
        if not self.a.quick:
            p3 = self.opt("printStock", r"^P3\b", default=None)
            configs.append(("neg0_p3_printer", {"negStock": ("index", 0), "printerR": -3.0, "printerB": 2.0,
                                                **({"printStock": ("index", p3)} if p3 is not None else {})}))
            hlg = self.opt("outputSpace", "hlg")
            if hlg is not None:
                configs.append(("hlg", {"outputSpace": ("index", hlg)}))
        L = []
        plan = []
        rnd = S.random_codes(4096)
        self.scene("lut_random", rnd)
        for si in want:
            n = int(re.sub(r"\D", "", sizes[si]))
            if "lattice%d" % n not in self.scenes:
                self.scene("lattice%d" % n, S.lattice(n))
            for cname, ov in configs:
                d = os.path.join(self.out, "luts", "%s_%d" % (cname, n))
                if os.path.isdir(d):
                    shutil.rmtree(d)
                os.makedirs(d, exist_ok=True)
                v = dict(ov, lutSize=("index", si))
                ln = self.line("lut_%s_%d" % (cname, n), "lattice%d" % n, v)
                ln += ["--set", "lutPath=" + d, "--press", "lutExport"]
                L.append(ln)
                L.append(self.line("lutr_%s_%d" % (cname, n), "lut_random", v))
                plan.append((cname, n, d))
        rep = self.batch("lut", L)
        msgs = [m["text"] for m in rep["messages"] if m["kind"] == "message"]
        rows = []
        for cname, n, d in plan:
            cubes = glob.glob(os.path.join(d, "*.cube"))
            if len(cubes) != 1:
                self.check(g, "%s %d^3: one .cube written" % (cname, n), False, "%d files in %s" % (len(cubes), d), "1",
                           " | ".join(m.replace("\n", " ")[:100] for m in msgs[-3:]))
                continue
            try:
                cube = luts.parse_cube(cubes[0])
            except Exception as ex:
                self.check(g, "%s %d^3: parses" % (cname, n), False, str(ex))
                continue
            rend = S.lattice_unpack(self.load("lut_%s_%d" % (cname, n), "lattice%d" % n), n)
            dd = np.abs(cube.table - rend)
            mx = float(dd.max())
            ok_size = cube.size == n
            fname = os.path.basename(cubes[0])
            self.check(g, "%s %d^3: .cube == Color Only render at lattice points" % (cname, n), ok_size and mx <= LUT_TOL,
                       "max %.2e mean %.2e" % (mx, float(dd.mean())), "<= %.0e" % LUT_TOL, fname)
            self.check(g, "%s %d^3: file name Celluloid_<input>_<neg>_<print>_<output>.cube" % (cname, n),
                       re.match(r"^Celluloid_.+_.+_.+_.+\.cube$", fname) is not None, fname)
            # interpolation accuracy of the LUT vs the plug-in at random code values (informational)
            rr = self.load("lutr_%s_%d" % (cname, n), "lut_random")[0, :, :3]
            ap = cube.apply(rnd[0, :, :3].astype(np.float64))
            e = np.abs(ap - rr)
            rows.append(dict(config=cname, size=n, lattice_max=mx, interp_mean=float(e.mean()), interp_max=float(e.max()), file=fname))
            self.info(g, "%s %d^3: trilinear LUT vs plug-in at random inputs" % (cname, n), "mean %.2e max %.2e" % (e.mean(), e.max()))
        self.check(g, "plug-in reports the export to the user (sendMessage)", any(".cube" in m for m in msgs),
                   (msgs[-1].replace("\n", " ")[:160] if msgs else "no message"))
        self.data["lut"] = rows

    # ------------------------------------------------------------------ performance
    def g_perf(self):
        g = "performance"
        sizes = [(1920, 1080)] + ([(3840, 2160)] if not self.a.quick else [])
        full = ("index", self.ix["full"])
        allon = {"mode": full, "halationAmount": 1.0, "bloomAmount": 0.5, "grainEnable": True, "grainAmount": 1.0,
                 "acutanceAmount": 0.5, "weaveAmount": 1.0, "vignette": 0.4}
        rows = []
        for w, h in sizes:
            name = "perf%d" % h
            self.scene(name, S.natural(w, h))
            L = [self.line("warm_cpu_%d" % h, name, allon, out=False),
                 self.line("perf_cpu_%d" % h, name, allon, out=False, repeat=3),
                 self.line("perf_cpu_color_%d" % h, name, {}, out=False, repeat=3)]
            if self.gpu:
                L += [self.line("warm_gpu_%d" % h, name, allon, out=False, gpu=True),
                      self.line("perf_gpu_%d" % h, name, allon, out=False, repeat=3, gpu=True),
                      self.line("perf_gpu_color_%d" % h, name, {}, out=False, repeat=3, gpu=True)]
            rep = self.batch("perf_%d" % h, L, check=False, group=g)
            for lab in [x[x.index("--label") + 1] for x in L]:
                if lab.startswith("warm"):
                    continue
                fr = job_frames(rep, lab)
                if not fr:
                    continue   # recorded by batch() as a failed job
                f = fr[0]
                # GPU: action + clFinish (device-complete); CPU: the render action
                med = f.get("renderFinishMsMedian", f["renderMsMedian"])
                mn = f.get("renderFinishMsMin", f["renderMsMin"])
                rows.append(dict(job=lab, width=w, height=h, render_ms=med, render_ms_min=mn, action_ms=f["renderMsMedian"],
                                 total_ms=float(np.median(f["totalMs"])), gpu=f["gpu"]))
                self.info(g, "%s %dx%d" % (lab.replace("perf_", ""), w, h),
                          "%.1f ms median (min %.1f, %s%s)" % (med, mn, f["gpu"], ", incl. clFinish" if f["gpu"] != "cpu" else ""))
        self.data["performance"] = rows
        self.info(g, "machine", "%d cores (host threads=%s); OpenCL device: POCL CPU if no GPU" % (os.cpu_count(), self.a.threads or "all"))

    # ------------------------------------------------------------------ stock contact sheet
    def g_stock_sheet(self):
        negs = list(range(len(self.negs)))
        prints = [self.ix["p1"]] if self.a.quick else list(range(len(self.prints)))
        L = []
        for n in negs:
            L.append(self.line("sheet_n%02d_p%02d" % (n, self.ix["p1"]), "natural_sheet", {"mode": ("index", self.ix["full"]),
                               "negStock": ("index", n)}, png=True))
        for p in prints:
            if p != self.ix["p1"]:
                L.append(self.line("sheet_n%02d_p%02d" % (self.ix["neg"], p), "natural_sheet", {"mode": ("index", self.ix["full"]),
                                   "printStock": ("index", p)}, png=True))
        self.batch("stock_sheet", L)
        items = [(self.negs[n] + " / " + self.prints[self.ix["p1"]], self.load("sheet_n%02d_p%02d" % (n, self.ix["p1"]), "natural_sheet")) for n in negs]
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_negatives.png"), "Negative stocks (with %s, Full mode)" % self.prints[self.ix["p1"]], cols=4))
        items = [(self.negs[self.ix["neg"]] + " / " + self.prints[p], self.load("sheet_n%02d_p%02d" % (self.ix["neg"], p), "natural_sheet")) for p in prints]
        self.sheets.append(sheets.contact_sheet(items, os.path.join(self.out, "contact_prints.png"), "Print stocks (with %s, Full mode)" % self.negs[self.ix["neg"]], cols=3))
        # tone curves sheet: wedge strips of all pairs from the stock group (Color Only)
        strips = []
        for (n, proc, p), smp in sorted(self.pairs.items()):
            if proc == "scan_alt":
                continue
            idx = self.chart.group("wedge")
            v = smp[idx, :3]
            strip = np.repeat(np.repeat(v[None, :, :], 12, axis=0), 2, axis=1)
            strips.append(("%s / %s" % (self.negs[n], "Lab Scan" if proc == "scan" else self.prints[p]), strip))
        self.sheets.append(sheets.contact_sheet(strips, os.path.join(self.out, "contact_wedges.png"),
                                                "Grey wedges -10..+10 stops (Color Only, Rec.709 2.4)", cols=3, thumb_w=322))

    # ================================================================== report
    def write_report(self):
        counts = {}
        for r in self.results:
            counts[r["status"]] = counts.get(r["status"], 0) + 1
        meta = dict(bundle=os.path.abspath(self.a.bundle), host=os.path.abspath(self.a.host), date=time.strftime("%Y-%m-%d %H:%M:%S"),
                    seconds=round(time.time() - self.t0, 1), quick=self.a.quick, full=self.a.full, gpu=self.gpu,
                    licensed=self.licensed, personal=self.personal, counts=counts, host_calls=[dict(name=n, seconds=round(s, 2), ok=o) for n, s, o in self.host.calls])
        rep = dict(meta=meta, results=self.results, data=self.data, contact_sheets=[os.path.basename(s) for s in self.sheets if s])
        with open(os.path.join(self.out, "report.json"), "w") as f:
            json.dump(rep, f, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
        md = []
        pl = self.data.get("plugin", {})
        md.append("# Celluloid Pro render test report\n")
        md.append("- Bundle: `%s`  (plug-in `%s` v%s.%s)" % (meta["bundle"], pl.get("id"), pl.get("versionMajor"), pl.get("versionMinor")))
        md.append("- Date: %s, %.0f s, mode: %s, OpenCL: %s, licensed: %s" % (meta["date"], meta["seconds"],
                  "quick" if self.a.quick else ("full" if self.a.full else "standard"), "yes" if self.gpu else "no",
                  "personal build (no watermark)" if self.personal else ("yes" if self.licensed else "NO (trial watermark in renders)")))
        md.append("- Results: " + ", ".join("%s %d" % (k, v) for k, v in sorted(counts.items())) + "\n")
        groups = []
        for r in self.results:
            if r["group"] not in groups:
                groups.append(r["group"])
        order = ["section6"] + [x for x in groups if x != "section6"]
        titles = {"section6": "SPEC §6 look targets (CN 200T + P1, Rec.709 2.4, points 0)", "params": "Parameters (SPEC §7)",
                  "stocks": "Stock pairs", "inputs": "Camera input spaces", "outputs": "Output spaces", "modes": "Modes", "texture": "Texture features",
                  "presets": "Look presets", "animation": "Animation", "render_scale": "Render scale 0.5",
                  "gpu_parity": "CPU vs OpenCL parity", "lut_export": "LUT export", "performance": "Performance",
                  "license": "License / trial watermark", "errors": "Errors"}
        for grp in order:
            if grp not in groups:
                continue
            md.append("## %s\n" % titles.get(grp, grp))
            md.append("| | check | measured | expected | detail |\n|---|---|---|---|---|")
            for r in self.results:
                if r["group"] != grp:
                    continue
                icon = {"pass": "PASS", "fail": "**FAIL**", "info": "info", "skip": "skip"}[r["status"]]
                md.append("| %s | %s | %s | %s | %s |" % (icon, r["name"].replace("|", "/"), str(r["measured"]).replace("|", "/"),
                                                        str(r["expected"]).replace("|", "/"), str(r["detail"]).replace("|", "/")))
            md.append("")
        if "stock_pairs" in self.data:
            md.append("## Stock pair tone summary (Color Only, Rec.709 2.4)\n")
            md.append("| negative | print | grey R G B | -6 stops (min) | +6 stops (min) | monotonic | max hue jump |\n|---|---|---|---|---|---|---|")
            for r in self.data["stock_pairs"]:
                md.append("| %s | %s | %.3f %.3f %.3f | %.3f | %.3f | %s | %.1f |" % (r["neg"], r["print"], r["grey"][0], r["grey"][1], r["grey"][2],
                          min(r["minus6"]), min(r["plus6"]), "yes" if r["monotonic"] else "**no**", r["max_hue_jump"]))
            md.append("")
        for key, title in (("de_negatives", "Mean ΔE2000 between negative stocks (24-patch chart, P1)"),
                           ("de_prints", "Mean ΔE2000 between print stocks (24-patch chart, CN 200T)")):
            if key in self.data and len(self.data[key]["names"]) > 1:
                names, M = self.data[key]["names"], np.array(self.data[key]["matrix"])
                short = [re.sub(r"\s+(Standard|Projection|Low|Rich|Contrast|Faded|Archive|Silver|Retention|Daylight|Tungsten|Studio|Soft|High|Speed|No-Backing|Fine|Panchromatic|Portrait|Consumer|Slide)\b", "", n) for n in names]
                md.append("## %s\n" % title)
                md.append("| | " + " | ".join(short) + " |\n|---|" + "---|" * len(names))
                for i, n in enumerate(short):
                    md.append("| %s | " % n + " | ".join("–" if i == j else "%.1f" % M[i, j] for j in range(len(names))) + " |")
                md.append("")
        if self.data.get("performance"):
            md.append("## Timings\n")
            md.append("| job | size | frame ms (median of 3; GPU incl. clFinish) | min | path |\n|---|---|---|---|---|")
            for r in self.data["performance"]:
                md.append("| %s | %dx%d | %.1f | %.1f | %s |" % (r["job"], r["width"], r["height"], r["render_ms"], r["render_ms_min"], r["gpu"]))
            md.append("")
        if self.sheets:
            md.append("## Contact sheets\n")
            for s in self.sheets:
                if s:
                    md.append("![%s](%s)" % (os.path.basename(s), os.path.basename(s)))
            md.append("")
        with open(os.path.join(self.out, "report.md"), "w") as f:
            f.write("\n".join(md) + "\n")
        # compact SPEC §6 acceptance summary (the full details are in report.json)
        s6 = [r for r in self.results if r["group"] in ("section6", "stocks", "gpu_parity") and r["status"] in ("pass", "fail")]
        with open(os.path.join(self.out, "section6.json"), "w") as f:
            json.dump(dict(meta=dict(bundle=meta["bundle"], date=meta["date"], licensed=self.licensed),
                           passed=all(r["status"] == "pass" for r in s6), checks=s6,
                           default_wedge=self.data.get("default_wedge"), skin=self.data.get("skin"),
                           de_negatives=self.data.get("de_negatives"), de_prints=self.data.get("de_prints"),
                           gpu_parity=self.data.get("gpu_parity")), f, indent=1,
                      default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
        return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="ofxhost executable")
    ap.add_argument("--bundle", required=True, help="Celluloid.ofx.bundle")
    ap.add_argument("--out", required=True)
    ap.add_argument("--quick", action="store_true", help="subset of stock pairs, 1080p timing only")
    ap.add_argument("--full", action="store_true", help="also 65^3 LUTs")
    ap.add_argument("--no-gpu", action="store_true")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--license-key", default="")
    ap.add_argument("--dev-license", action="store_true", help="sign a key with tools/licensegen/dev-keys (CEL_DEV_LICENSE builds)")
    ap.add_argument("--only", default="", help="comma separated groups")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    C.self_test()
    s = Suite(a)
    print("render tests -> %s" % s.out)
    try:
        s.setup()
    except Exception as ex:
        s.rec("errors", "setup / describe", "fail", str(ex)[:500])
        s.write_report()
        print("setup failed: %s" % ex)
        return 2
    groups = [("params", s.g_conformance), ("license", s.g_license), ("stocks", s.g_stocks), ("section6", s.g_section6),
              ("inputs", s.g_inputs), ("outputs", s.g_outputs), ("modes", s.g_modes), ("modules", s.g_modules), ("damage", s.g_damage), ("texture", s.g_texture), ("presets", s.g_presets),
              ("animation", s.g_animation), ("render_scale", s.g_scale), ("gpu_parity", s.g_gpu), ("lut_export", s.g_lut),
              ("stock_sheet", s.g_stock_sheet), ("performance", s.g_perf)]
    only = set(x for x in a.only.split(",") if x)
    deps = {"section6": "stocks", "stock_sheet": "stocks"}
    for name, fn in groups:
        if only and name not in only and not any(deps.get(o) == name for o in only):
            continue
        t0 = time.time()
        print("[%s]" % name, flush=True)
        try:
            fn()
        except Exception as ex:
            s.rec("errors", name, "fail", "%s: %s" % (type(ex).__name__, str(ex)[:400]), "", traceback.format_exc()[-800:].replace("\n", " "))
            print(traceback.format_exc())
        print("  %.1fs" % (time.time() - t0), flush=True)
    counts = s.write_report()
    print("report: %s/report.md  (%s)" % (s.out, ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    return 1 if counts.get("fail") else 0


if __name__ == "__main__":
    sys.exit(main())
