#!/usr/bin/env python3
"""Generate the "Controls reference" of docs/MANUAL.md from the real plug-in.

The reference is built from the parameter description the plug-in itself reports (labels, ranges,
defaults, choice options and the tool-tip hints written in src/ofx/Params.cpp), so the manual can
never drift from what Resolve shows. Only the text between the two markers

    <!-- BEGIN GENERATED: controls (tools/docs/gen_manual_controls.py) -->
    <!-- END GENERATED: controls -->

is rewritten; everything else in MANUAL.md is hand-written.

Usage (Linux workbench, after building the bundle and tools/ofxhost):

    python3 tools/docs/gen_manual_controls.py --bundle <build>/Celluloid.ofx.bundle \
        --ofxhost <build>/tools/ofxhost/ofxhost            # rewrite docs/MANUAL.md
    python3 tools/docs/gen_manual_controls.py ... --check  # exit 1 if the manual is out of date
    python3 tools/docs/gen_manual_controls.py --params params.json ...   # use a saved
                                                     # `ofxhost <bundle> --list-params-json FILE`
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

BEGIN = "<!-- BEGIN GENERATED: controls (tools/docs/gen_manual_controls.py) -->"
END = "<!-- END GENERATED: controls -->"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# read-only labels whose value depends on the machine or the build, not on the plug-in design
MACHINE_STATE = {"licenseStatus", "aboutVersion"}


def num(x):
    if isinstance(x, bool):
        return "on" if x else "off"
    if isinstance(x, (int, float)):
        v = float(x)
        return str(int(v)) if v == int(v) else ("%g" % v)
    return str(x)


def split_hint(hint):
    """Hints list sub-items either as "\n\nName: text" blocks or as "\nName: text" lines."""
    hint = hint.strip()
    if "\n\n" in hint:
        parts = [p.strip() for p in hint.split("\n\n") if p.strip()]
        return parts[0], parts[1:]
    lines = [l.strip() for l in hint.split("\n") if l.strip()]
    if len(lines) > 1 and all(re.match(r"^[^:]{1,40}:", l) for l in lines[:-1]):
        # Mode / Process style: every line is "Option: explanation", a trailing note may follow
        items = [l for l in lines if re.match(r"^[^:]{1,40}:", l)]
        rest = [l for l in lines if l not in items]
        return "", items + rest      # a trailing general note becomes the last bullet
    return " ".join(lines), []


def code_placeholders(text):
    """`<Documents>`-style placeholders would vanish as HTML tags in rendered Markdown: put tokens
    that contain one in code spans (keeping trailing punctuation outside)."""
    def rep(m):
        tok, tail = m.group(1), ""
        while tok and tok[-1] in ".,;:)":
            tok, tail = tok[:-1], tok[-1] + tail
        return "`%s`%s" % (tok, tail)
    return re.sub(r"(?<![`\w])([^\s`]*<[A-Za-z][^>\s]*>[^\s`]*)", rep, text)


def item(text):
    m = re.match(r"^([^:]{1,40}):\s*(.*)$", text, re.S)
    if m:
        return "*%s* — %s" % (m.group(1).strip(), m.group(2).strip())
    return text


def describe(p):
    t = p["type"]
    facts = []
    if t in ("Double", "Integer"):
        facts.append("%s … %s" % (num(p["min"][0]), num(p["max"][0])))
        facts.append("default %s" % num(p["default"]))
    elif t == "Boolean":
        facts.append("default %s" % num(p["default"]))
    elif t == "Choice":
        opts = p.get("options", [])
        d = p.get("default", 0)
        if 0 <= d < len(opts):
            facts.append("default: %s" % opts[d])
    elif t == "String":
        mode = p.get("stringMode", "")
        if mode == "OfxParamStringIsLabel":
            facts.append("read-only")
            if p["name"] not in MACHINE_STATE and p.get("default"):
                facts.append("shows “%s” until a preset is chosen" % p["default"]
                             if p["name"] == "lookPresetStatus" else "“%s”" % p["default"])
        elif mode == "OfxParamStringIsDirectoryPath":
            facts.append("folder; default empty")
        else:
            facts.append("text field")
    elif t == "PushButton":
        facts.append("button")
    if p.get("animates") and t in ("Double", "Integer"):
        facts.append("keyframable")
    return facts


def render(params):
    by_name = {p["name"]: p for p in params}
    out = []
    page = next((p for p in params if p["type"] == "Page"), None)
    groups = page["children"] if page else [p["name"] for p in params if p["type"] == "Group"]
    for g in groups:
        gp = by_name[g]
        out.append("### %s" % gp["label"])
        out.append("")
        if gp.get("hint"):
            out.append("*%s*" % gp["hint"].strip())
            out.append("")
        for p in params:
            if p.get("parent") != g or p.get("secret"):
                continue
            head, items = split_hint(p.get("hint", ""))
            facts = describe(p)
            line = "- **%s** (%s)" % (p["label"], "; ".join(["`%s`" % p["name"]] + facts))
            if head:
                line += " — " + code_placeholders(head)
            out.append(line)
            for it in items:
                out.append("  - " + code_placeholders(item(it)))
            if p["type"] == "Choice" and not items:
                opts = p.get("options", [])
                if opts:
                    out.append("  - Options: " + " · ".join(opts))
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def load_params(args):
    if args.params:
        with open(args.params, encoding="utf-8") as f:
            return json.load(f)["params"]
    if not (args.bundle and args.ofxhost):
        sys.exit("give --params FILE, or --bundle and --ofxhost")
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "params.json")
        # a clean environment: no license key from the shell, so nothing machine-specific leaks in
        env = {k: v for k, v in os.environ.items() if not k.startswith("CELLULOID_")}
        subprocess.run([args.ofxhost, args.bundle, "--list-params-json", path], check=True,
                       stdout=subprocess.DEVNULL, env=env)
        with open(path, encoding="utf-8") as f:
            return json.load(f)["params"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--params", help="JSON written by `ofxhost <bundle> --list-params-json FILE`")
    ap.add_argument("--bundle", help="path of Celluloid.ofx.bundle")
    ap.add_argument("--ofxhost", help="path of the ofxhost binary")
    ap.add_argument("--manual", default=os.path.join(ROOT, "docs", "MANUAL.md"))
    ap.add_argument("--check", action="store_true", help="do not write; exit 1 when the manual is stale")
    args = ap.parse_args()

    body = render(load_params(args))
    with open(args.manual, encoding="utf-8") as f:
        text = f.read()
    i, j = text.find(BEGIN), text.find(END)
    if i < 0 or j < i:
        sys.exit("markers not found in %s" % args.manual)
    new = text[: i + len(BEGIN)] + "\n\n" + body + "\n" + text[j:]
    if args.check:
        if new != text:
            print("%s: controls reference is out of date (run without --check)" % args.manual)
            return 1
        print("controls reference is up to date")
        return 0
    if new != text:
        with open(args.manual, "w", encoding="utf-8") as f:
            f.write(new)
        print("updated %s" % args.manual)
    else:
        print("no change")
    return 0


if __name__ == "__main__":
    sys.exit(main())
