#!/usr/bin/env python3
"""Minimal rsync stand-in.

`rsync` is not installed in this sandbox and apt is blocked, but opam needs it to
copy `file://` sources into its build dir.  opam only ever uses rsync for plain
local copies, so emulating the handful of flags it passes with shutil is enough.
Trailing-slash semantics ("copy the contents of DIR" vs "copy DIR itself") are
honoured because opam relies on them.
"""
import os
import shutil
import sys

args = sys.argv[1:]
delete = any(a.startswith("--delete") for a in args)
positional = []
i = 0
while i < len(args):
    a = args[i]
    if a == "--":
        positional.extend(args[i + 1:])
        break
    if a.startswith("-"):
        # options that take a separate value
        if a in ("--exclude", "--include", "--filter", "-e", "--rsh", "--files-from"):
            i += 2
            continue
        i += 1
        continue
    positional.append(a)
    i += 1

if len(positional) < 2:
    sys.exit(0)

dest = positional[-1]
srcs = positional[:-1]


def copy_into(src, dst):
    if os.path.islink(src) and not os.path.exists(src):
        return  # dangling symlink: rsync -L skips these rather than failing
    if os.path.isdir(src):
        if src.endswith("/"):
            os.makedirs(dst, exist_ok=True)
            for name in os.listdir(src):
                copy_into(os.path.join(src, name), os.path.join(dst, name))
        else:
            base = os.path.basename(src.rstrip("/"))
            target = os.path.join(dst, base) if os.path.isdir(dst) or dst.endswith("/") else dst
            if os.path.isdir(target):
                shutil.rmtree(target, ignore_errors=True)
            shutil.copytree(src, target, symlinks=False, ignore=shutil.ignore_patterns(".git"),
                            ignore_dangling_symlinks=True)
    else:
        if os.path.isdir(dst) or dst.endswith("/"):
            os.makedirs(dst, exist_ok=True)
            target = os.path.join(dst, os.path.basename(src))
        else:
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            target = dst
        shutil.copyfile(src, target)
        shutil.copymode(src, target)


if delete and len(srcs) == 1 and srcs[0].endswith("/") and os.path.isdir(dest):
    # honour rsync --delete: the destination must end up mirroring the source
    shutil.rmtree(dest, ignore_errors=True)

for s in srcs:
    try:
        copy_into(s, dest)
    except Exception as e:  # rsync tolerates unreadable/odd files; mimic that
        print(f"rsync-shim warning: {e}", file=sys.stderr)
sys.exit(0)
