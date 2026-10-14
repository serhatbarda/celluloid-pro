#!/usr/bin/env python3
"""End-to-end OFX smoke test of a built ReafilmPro.ofx.bundle through the real plug-in binary
(describe, createInstance, parameters, a push button, the render action), run in CI on the shipping
platforms (macOS universal bundle, Windows Win64 DLL) and on the Linux workbench. Standard library only.

    python3 ofx_smoke.py --host <ofxhost> --bundle <ReafilmPro.ofx.bundle> --work <dir> [--gpu metal|opencl]

Checks
  1. describe: identifier, contexts, parameter count; GPU support is declared for exactly the APIs
     whose describe-time self test passed (REAFILM_LOG=1 log lines "<API> self test: ok|...").
  2. CPU render of a 64x48 test pattern (Full mode, defaults, no license key): finite, alpha passed
     through, and equal to the committed reference render within 2e-3 - the personal build (default,
     no watermark) against reference_gradient_64x48_personal.f32, the licensed build
     (-DCEL_LICENSING=ON, trial watermark) against reference_gradient_64x48.f32.
  3. Personal build: the License group is secret and LUT export writes a .cube without a key.
     Licensed build: the trial refuses LUT export with a message and writes no file.
  4. --gpu: the same frame through the plug-in's GPU path with a host-created queue and buffers
     (Metal: MTLCommandQueue + MTLBuffers; OpenCL: cl_mem) equals the CPU render within 2e-4.
"""
import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF_TRIAL = os.path.join(HERE, "reference_gradient_64x48.f32")              # licensed build, trial
REF_PERSONAL = os.path.join(HERE, "reference_gradient_64x48_personal.f32")  # personal build
PERSONAL_STATUS = "Personal build"   # licenseStatus of the personal build (src/ofx/Params.h)
W, H = 64, 48
FAILS = []


def check(cond, what, detail=""):
    detail = detail.replace("\n", " | ")
    if cond and len(detail) > 100:
        detail = ""            # long diagnostics (logs) only for failures
    print(("  ok   " if cond else "  FAIL ") + what + (" - " + detail[-600:] if detail else ""), flush=True)
    if not cond:
        FAILS.append(what)


def run(args, env_extra=None):
    env = dict(os.environ)
    env.update(env_extra or {})
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    return p.returncode, p.stdout, p.stderr


def read_f32(path):
    with open(path, "rb") as f:
        b = f.read()
    return list(struct.unpack("<%df" % (len(b) // 4), b))


def maxdiff(a, b):
    return max(abs(x - y) for x, y in zip(a, b)) if len(a) == len(b) else float("inf")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--bundle", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--gpu", choices=["none", "metal", "opencl"], default="none")
    ap.add_argument("--write-reference", action="store_true", help="store the CPU render as the reference")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    cfg = os.path.join(a.work, "cfg")
    os.makedirs(cfg, exist_ok=True)
    # no license key: the licensed build runs as a trial (watermark is part of its reference), the
    # personal build never watermarks; keep the user's real key out
    env = {"REAFILM_LOG": "1", "XDG_CONFIG_HOME": cfg, "REAFILM_LICENSE_KEY": "", "CELLULOID_LICENSE_KEY": ""}

    print("[describe]")
    js = os.path.join(a.work, "describe.json")
    rc, out, err = run([a.host, a.bundle, "--list-params-json", js], env)
    check(rc == 0, "ofxhost loads the bundle and describes the plug-in", "rc=%d %s" % (rc, err.strip()[-400:]))
    if rc != 0:
        return finish()
    d = json.load(open(js, encoding="utf-8"))
    pl = d.get("plugin", {})
    check(pl.get("id") == "de.serhatbarda.CelluloidPro", "identifier", str(pl.get("id")))
    check(all(any(c.endswith(x) for c in pl.get("contexts", [])) for x in ("Filter", "General")), "contexts Filter + General")
    check(len(d.get("params", [])) >= 50, "parameters", "%d" % len(d.get("params", [])))
    params = {p.get("name"): p for p in d.get("params", [])}
    status = params.get("licenseStatus", {})
    personal = PERSONAL_STATUS in str(status.get("value", status.get("default", "")))
    print("  build variant: %s" % ("personal" if personal else "licensed"))
    if personal:
        lic = [n for n, p in params.items() if n == "groupLicense" or p.get("parent") == "groupLicense"]
        check(len(lic) == 5 and all(params[n].get("secret") for n in lic),
              "personal build: License group kept (saved projects) and secret", ", ".join(lic))
        # real film only: the full stock lists stay saved but hidden (Film Stock / Print Stock show the real stocks)
        full = ["negStock", "printStock"]
        check(all(params.get(n, {}).get("secret") for n in full), "full stock lists are secret")
        check(not any(p.get("secret") for n, p in params.items() if n not in lic and n not in full), "no other parameter is secret")
        ver = str(params.get("aboutVersion", {}).get("value", ""))
        check("Personal build" in ver, "About shows the personal build", ver)
    REF = REF_PERSONAL if personal else REF_TRIAL
    gpu = pl.get("gpu", {})
    tests = dict(re.findall(r"(OpenCL|CUDA|Metal) self test: (ok|not usable)", err))
    print("  self tests: %s; declared: %s" % (tests, json.dumps(gpu)))
    declared = {"OpenCL": gpu.get("openclBuffers") == "true", "CUDA": gpu.get("cuda") == "true",
                "Metal": gpu.get("metal") == "true"}
    for api in ("OpenCL", "CUDA", "Metal"):
        check(declared[api] == (tests.get(api) == "ok"),
              "%s declared only after a passing self test" % api,
              "declared=%s self test=%s" % (declared[api], tests.get(api, "not compiled in")))
    check(bool(tests), "describe-time GPU self test ran (CEL_HAVE_GPU_API)", err.strip()[-300:] if not tests else "")

    print("[CPU render]")
    cpu = os.path.join(a.work, "cpu")
    rc, out, err = run([a.host, a.bundle, "--in", "pattern:gradient", "--width", str(W), "--height", str(H),
                        "--out", cpu, "--no-png"], env)
    check(rc == 0, "CPU render action", err.strip()[-400:])
    if rc != 0:
        return finish()
    c = read_f32(cpu + ".f32")
    check(len(c) == W * H * 4 and all(math.isfinite(v) for v in c), "CPU output finite, %dx%d RGBA" % (W, H))
    if a.write_reference:
        with open(REF, "wb") as f:
            f.write(struct.pack("<%df" % len(c), *c))
        print("  wrote", REF)
    elif os.path.exists(REF):
        r = read_f32(REF)
        md = maxdiff(c, r)
        check(md <= 2e-3, "CPU render equals the reference render (Linux workbench, %s)" % os.path.basename(REF),
              "max abs diff %.2e" % md)
        ad = max(abs(c[i] - r[i]) for i in range(3, len(c), 4))
        check(ad <= 1e-6, "alpha passed through", "max alpha diff %.2e" % ad)
    else:
        check(False, "reference render present", REF)

    print("[LUT export without a license key]")
    lut = os.path.join(a.work, "luts")
    if os.path.isdir(lut):
        for f in os.listdir(lut):
            os.remove(os.path.join(lut, f))
    rc, out, err = run([a.host, a.bundle, "--in", "pattern:grey", "--width", "16", "--height", "16",
                        "--set", "lutSize=index:0", "--set", "lutPath=" + lut, "--press", "lutExport"], env)
    cubes = [f for f in (os.listdir(lut) if os.path.isdir(lut) else []) if f.endswith(".cube")]
    if personal:
        ok = rc == 0 and len(cubes) == 1
        if ok:
            with open(os.path.join(lut, cubes[0]), encoding="utf-8") as f:
                rows = [l for l in f if re.match(r"^\s*-?[0-9.]", l)]
            ok = len(rows) >= 17 ** 3
        check(ok, "personal build exports a LUT without a license key", "rc=%d files=%s %s" % (rc, cubes, err.strip()[-300:]))
    else:
        check(rc == 0 and not cubes and "licensed version" in (out + err), "trial refuses LUT export with a message",
              "files=%s" % cubes)

    if a.gpu != "none":
        print("[GPU render: %s]" % a.gpu)
        if not declared["Metal" if a.gpu == "metal" else "OpenCL"]:
            check(False, "%s render path declared by the plug-in" % a.gpu, "self test: %s" % tests)
            return finish()
        g = os.path.join(a.work, a.gpu)
        rc, out, err = run([a.host, a.bundle, "--in", "pattern:gradient", "--width", str(W), "--height", str(H),
                            "--gpu", a.gpu, "--out", g, "--no-png"], env)
        print("  " + "\n  ".join(l for l in out.splitlines() if l.startswith(("frame", "Metal", "OpenCL"))))
        check(rc == 0 and ("[%s" % ("metal" if a.gpu == "metal" else "opencl")) in out, "%s render action" % a.gpu,
              err.strip()[-400:])
        if rc == 0:
            gg = read_f32(g + ".f32")
            withdrawn = "withdrew" in err
            check(not withdrawn, "plug-in kept its %s path (no CPU fallback)" % a.gpu, err.strip()[-300:] if withdrawn else "")
            md = maxdiff(gg, c)
            check(md <= 2e-4, "%s render equals the CPU render" % a.gpu, "max abs diff %.2e" % md)
    return finish()


def finish():
    print("ofx smoke: %s" % ("PASS" if not FAILS else "%d FAILURE(S): %s" % (len(FAILS), "; ".join(FAILS))))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
