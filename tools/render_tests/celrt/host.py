"""Driver for tools/ofxhost: plug-in description, batch renders on one instance, results."""
import json
import os
import re
import shlex
import subprocess
import time



class HostError(RuntimeError):
    pass


class Params:
    """Plug-in parameters as reported by `ofxhost --list-params-json` (generic access)."""

    def __init__(self, desc):
        self.desc = desc
        self.plugin = desc.get("plugin", {})
        self.by_name = {p["name"]: p for p in desc.get("params", [])}

    def has(self, name):
        return name in self.by_name

    def get(self, name):
        return self.by_name.get(name)

    def type(self, name):
        p = self.get(name)
        return p["type"] if p else None

    def options(self, name):
        p = self.get(name)
        return list(p.get("options", [])) if p else []

    def default(self, name):
        p = self.get(name)
        return None if p is None else p.get("default")

    def find_option(self, name, *patterns, exclude=None):
        """Index of the first option whose label matches ALL regex patterns (case-insensitive)."""
        for i, o in enumerate(self.options(name)):
            if all(re.search(pt, o, re.I) for pt in patterns) and not (exclude and re.search(exclude, o, re.I)):
                return i
        return None

    def value_params(self):
        return [p for p in self.desc.get("params", []) if p["type"] not in ("Group", "Page", "PushButton")]


def fmt_value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return repr(v)
    return str(v)


class Host:
    def __init__(self, exe, bundle, work, threads=0, verbose=False, timeout=3600):
        self.exe = exe
        self.bundle = bundle
        self.work = work
        self.threads = threads
        self.verbose = verbose
        self.timeout = timeout
        self.calls = []          # (name, seconds, ok)
        os.makedirs(work, exist_ok=True)

    def _run(self, args, name):
        cmd = [self.exe, self.bundle] + args
        if self.threads:
            cmd += ["--threads", str(self.threads)]
        t0 = time.time()
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=self.timeout)
        dt = time.time() - t0
        self.calls.append((name, dt, p.returncode == 0))
        with open(os.path.join(self.work, name + ".log"), "w") as f:
            f.write("$ " + " ".join(shlex.quote(c) for c in cmd) + "\n\n--- stdout\n" + p.stdout + "\n--- stderr\n" + p.stderr)
        if self.verbose:
            print("  [ofxhost %s] %.1fs exit %d" % (name, dt, p.returncode))
        return p

    def describe(self):
        path = os.path.join(self.work, "params.json")
        p = self._run(["--list-params-json", path, "--report", os.path.join(self.work, "describe_report.json")], "describe")
        if p.returncode != 0:
            raise HostError("describe failed (exit %d): %s" % (p.returncode, p.stderr.strip()[-2000:]))
        with open(path) as f:
            return json.load(f)

    def batch(self, name, lines, extra=None, check=True, keep_going=True):
        """Run job lines (lists of ofxhost job args) on one plug-in instance. Returns the report.
        With keep_going a failed job does not stop the following ones (errors are in the report)."""
        bpath = os.path.join(self.work, name + ".batch")
        rpath = os.path.join(self.work, name + ".report.json")
        with open(bpath, "w") as f:
            for ln in lines:
                f.write(" ".join(shlex.quote(str(a)) for a in ln) + "\n")
        args = ["--batch", bpath, "--report", rpath] + (["--keep-going"] if keep_going else []) + list(extra or [])
        p = self._run(args, name)
        rep = None
        if os.path.exists(rpath):
            with open(rpath) as f:
                rep = json.load(f)
        if check and p.returncode != 0:
            raise HostError("%s: ofxhost exit %d: %s" % (name, p.returncode, p.stderr.strip()[-3000:]))
        if rep is None:
            raise HostError("%s: no report written (exit %d): %s" % (name, p.returncode, p.stderr.strip()[-3000:]))
        rep["_exit"] = p.returncode
        rep["_stderr"] = p.stderr
        return rep


def set_args(values):
    """{'param': value} -> ['--set', 'param=value', ...]; choices given as ('index', n)."""
    out = []
    for k, v in values.items():
        if isinstance(v, tuple) and v[0] == "index":
            out += ["--set", "%s=index:%d" % (k, v[1])]
        else:
            out += ["--set", "%s=%s" % (k, fmt_value(v))]
    return out


def job_frames(rep, label):
    for j in rep["jobs"]:
        if j["label"] == label:
            return j["frames"]
    return []


def job_error(rep, label):
    for j in rep["jobs"]:
        if j["label"] == label:
            return j.get("error", "")
    return "job not run"
