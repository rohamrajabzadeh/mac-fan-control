#!/usr/bin/env python3
# Recursive dependency bundler for MacFans.app — a small, reliable stand-in for
# macdeployqt (which mis-resolves transitive deps against Homebrew Qt).
#
# Walks the Mach-O dependency graph of the main executable and every bundled
# Qt plugin, copies each non-system framework/dylib into Contents/Frameworks,
# rewrites install names to @rpath, strips Homebrew rpaths, and gives every
# binary an LC_RPATH resolving @rpath to Contents/Frameworks. The result runs
# with no dependency on Homebrew.
#
# Usage: bundle_deps.py /path/to/MacFans.app
import os, re, shutil, subprocess, sys

SYSTEM_PREFIXES = ("/usr/lib/", "/System/")
QT_LIB = os.path.realpath("/opt/homebrew/opt/qt6/lib")

def sh(*args):
    return subprocess.run(args, capture_output=True, text=True)

def otool_deps(path):
    deps = []
    for line in sh("otool", "-L", path).stdout.splitlines()[1:]:
        m = re.match(r"\s+(\S+)\s+\(", line)
        if m:
            deps.append(m.group(1))
    return deps

def rpaths(path):
    out = sh("otool", "-l", path).stdout.splitlines()
    res, grab = [], False
    for i, line in enumerate(out):
        if "LC_RPATH" in line:
            grab = True
        elif grab and "path " in line:
            m = re.search(r"path (.+) \(offset", line)
            if m:
                res.append(m.group(1))
            grab = False
    return res

def is_system(p):
    return p.startswith(SYSTEM_PREFIXES)

def is_framework(src):
    return ".framework/" in src

def fw_name(src):
    m = re.search(r"/([^/]+)\.framework/", src)
    return m.group(1) if m else None

def resolve(dep):
    """Resolve a dependency string to a real on-disk source path (or None)."""
    if dep.startswith("/opt/homebrew"):
        return os.path.realpath(dep) if os.path.exists(dep) else None
    if dep.startswith("@rpath/"):
        cand = os.path.join(QT_LIB, dep[len("@rpath/"):])
        return os.path.realpath(cand) if os.path.exists(cand) else None
    return None

def token_for(dep, bundled):
    """The @rpath rewrite token for a dep, if we bundle it."""
    if is_framework(dep):
        nm = fw_name(dep)
        if nm and nm in bundled:
            return f"@rpath/{nm}.framework/Versions/A/{nm}"
    else:
        base = os.path.basename(dep)
        if base in bundled:
            return f"@rpath/{base}"
    return None

def main():
    app = sys.argv[1]
    fw_dir = os.path.join(app, "Contents", "Frameworks")
    macos_bin = os.path.join(app, "Contents", "MacOS", "MacFans")
    plugins_root = os.path.join(app, "Contents", "PlugIns")
    os.makedirs(fw_dir, exist_ok=True)

    bundled = {}   # key(name) -> (dest_path, token)
    queue = []

    def copy_in(src):
        if is_framework(src):
            name = fw_name(src)
            if name in bundled:
                return
            srcfw = src.split(".framework/")[0] + ".framework"
            dstfw = os.path.join(fw_dir, f"{name}.framework")
            dest = os.path.join(dstfw, "Versions/A", name)
            if not os.path.exists(dest):
                os.makedirs(os.path.join(dstfw, "Versions/A"), exist_ok=True)
                shutil.copy2(os.path.join(srcfw, "Versions/A", name), dest)
                res = os.path.join(srcfw, "Versions/A/Resources")
                if os.path.isdir(res):
                    shutil.copytree(res, os.path.join(dstfw, "Versions/A/Resources"),
                                    dirs_exist_ok=True)
                cur = os.path.join(dstfw, "Versions/Current")
                if not os.path.islink(cur) and not os.path.exists(cur):
                    os.symlink("A", cur)
                top = os.path.join(dstfw, name)
                if not os.path.islink(top) and not os.path.exists(top):
                    os.symlink(f"Versions/Current/{name}", top)
            bundled[name] = (dest, f"@rpath/{name}.framework/Versions/A/{name}")
        else:
            base = os.path.basename(src)
            if base in bundled:
                return
            dest = os.path.join(fw_dir, base)
            if not os.path.exists(dest):
                shutil.copy2(src, dest)
            os.chmod(dest, 0o644)
            bundled[base] = (dest, f"@rpath/{base}")
        queue.append(bundled[fw_name(src) if is_framework(src) else os.path.basename(src)][0])

    # Seed: main exe + every plugin dylib.
    roots = [macos_bin]
    for dp, _, files in os.walk(plugins_root):
        roots += [os.path.join(dp, f) for f in files if f.endswith(".dylib")]

    def scan(binpath):
        for dep in otool_deps(binpath):
            if is_system(dep):
                continue
            src = resolve(dep)
            if src:
                copy_in(src)

    for r in roots:
        scan(r)
    while queue:
        scan(queue.pop())

    def rewrite(binpath, extra_rpath):
        # Ensure writable for install_name_tool, but keep the execute bit.
        os.chmod(binpath, os.stat(binpath).st_mode | 0o200)
        # rewrite dependency references
        for dep in otool_deps(binpath):
            if is_system(dep):
                continue
            t = token_for(dep, bundled)
            if t and t != dep:
                sh("install_name_tool", "-change", dep, t, binpath)
        # drop Homebrew rpaths, add the bundle one
        for rp in rpaths(binpath):
            if "/opt/homebrew" in rp or rp.endswith("/build/lib"):
                sh("install_name_tool", "-delete_rpath", rp, binpath)
        if extra_rpath not in rpaths(binpath):
            sh("install_name_tool", "-add_rpath", extra_rpath, binpath)

    # Main executable.
    rewrite(macos_bin, "@executable_path/../Frameworks")

    # Bundled libs/frameworks: also fix their id.
    for name, (dest, token) in bundled.items():
        sh("install_name_tool", "-id", token, dest)
        rel = "@loader_path/../../.." if ".framework/" in dest else "@loader_path"
        rewrite(dest, rel)

    # Plugins.
    for dp, _, files in os.walk(plugins_root):
        for f in files:
            if f.endswith(".dylib"):
                p = os.path.join(dp, f)
                rel = os.path.relpath(fw_dir, os.path.dirname(p))
                rewrite(p, f"@loader_path/{rel}")

    print(f"Bundled {len(bundled)} libraries/frameworks:")
    for k in sorted(bundled):
        print("  ", k)

if __name__ == "__main__":
    main()
