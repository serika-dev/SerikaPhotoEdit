#!/usr/bin/env sh
# Run on a native macOS runner with the matching Qt kit and Xcode CLI tools.
# Tooling may be outside SOURCE_DIR, which can be an unchanged release-tag checkout.
# RELEASE_ARCH=x86_64|arm64; MACOSX_DEPLOYMENT_TARGET is a requested minimum only:
# the bundle minimum is raised when any bundled Mach-O requires a newer macOS.
set -eu
[ "$(uname -s)" = Darwin ] || { echo 'Run this script on macOS.' >&2; exit 1; }
tooling_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${SOURCE_DIR:-"$tooling_root"}
source_dir=$(CDPATH= cd -- "$source_dir" && pwd)
build_dir=${BUILD_DIR:-"$source_dir/build"}
output_dir=${OUTPUT_DIR:-"$tooling_root/dist"}
case "$build_dir" in /*) ;; *) build_dir="$source_dir/$build_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$tooling_root/$output_dir" ;; esac
arch=${RELEASE_ARCH:-$(uname -m)}
case "$arch" in x86_64|arm64) ;; *) echo "Unsupported release architecture: $arch" >&2; exit 1 ;; esac
[ "$(uname -m)" = "$arch" ] || { echo 'Use a native runner matching RELEASE_ARCH.' >&2; exit 1; }
for tool in cmake python3 ditto otool lipo install_name_tool codesign hdiutil; do
    command -v "$tool" >/dev/null || { echo "Missing packaging tool: $tool" >&2; exit 1; }
done
if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/macdeployqt" ]; then
    deploy_tool="$QT_ROOT_DIR/bin/macdeployqt"
else
    deploy_tool=$(command -v macdeployqt) || { echo 'Set QT_ROOT_DIR to the matching Qt kit.' >&2; exit 1; }
fi
qt_root=$(CDPATH= cd -- "$(dirname -- "$deploy_tool")/.." && pwd)
identity=${CODESIGN_IDENTITY:--}
if [ -n "${NOTARY_PROFILE:-}" ] && [ "$identity" = - ]; then
    echo 'Notarization requires a Developer ID identity; ad-hoc signatures cannot be notarized.' >&2
    exit 1
fi
mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
stage=$(mktemp -d "$output_dir/macos-stage.XXXXXX")
cmake --install "$build_dir" --prefix "$stage"
bundle="$stage/SerikaPhotoEdit.app"
[ -x "$bundle/Contents/MacOS/SerikaPhotoEdit" ] || { echo "Missing installed bundle: $bundle" >&2; exit 1; }
documentation="$bundle/Contents/Resources/documentation"
mkdir -p "$documentation/licenses"
if [ -d "$stage/share/serika-photoedit" ]; then
    ditto "$stage/share/serika-photoedit" "$documentation"
fi
# Ensure license texts and documentation are inside the .app, not beside it.
ditto "$source_dir/resources/licenses" "$documentation/licenses"
for document in LICENSE README.md CONTRIBUTING.md CHANGELOG.md; do
    [ ! -f "$source_dir/$document" ] || ditto "$source_dir/$document" "$documentation/$document"
done
ditto "$source_dir/docs" "$documentation/docs"
mkdir -p "$documentation/src/io"
ditto "$source_dir/src/io/README.md" "$documentation/src/io/README.md"
ditto "$source_dir/examples" "$documentation/examples"
[ ! -f "$build_dir/RELEASE-BUILD-INFO.txt" ] || ditto "$build_dir/RELEASE-BUILD-INFO.txt" "$documentation/RELEASE-BUILD-INFO.txt"
if [ -n "${ICON_PNG:-}" ]; then
    iconset="$stage/SerikaPhotoEdit.iconset"
    mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$ICON_PNG" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        doubled=$((size * 2))
        sips -z "$doubled" "$doubled" "$ICON_PNG" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$iconset" -o "$bundle/Contents/Resources/SerikaPhotoEdit.icns"
    /usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile SerikaPhotoEdit.icns' "$bundle/Contents/Info.plist" 2>/dev/null ||
        /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string SerikaPhotoEdit.icns' "$bundle/Contents/Info.plist"
fi
# New sources bundle the brand icon. Older tags retain optional ICON_PNG behavior.
if [ -f "$source_dir/packaging/macos/SerikaPhotoEdit.icns" ]; then
    [ -f "$bundle/Contents/Resources/SerikaPhotoEdit.icns" ] || { echo 'Missing bundled app icon.' >&2; exit 1; }
fi
# macdeployqt deploys Cocoa by default; --batch also needs the offscreen plugin.
mkdir -p "$bundle/Contents/PlugIns/platforms"
for plugin in libqcocoa.dylib libqoffscreen.dylib; do
    [ -f "$qt_root/plugins/platforms/$plugin" ] || { echo "Missing Qt platform plugin: $plugin" >&2; exit 1; }
    ditto "$qt_root/plugins/platforms/$plugin" "$bundle/Contents/PlugIns/platforms/$plugin"
done
# Qt 6.8.3 has no -no-codesign option; it signs only when an identity is supplied.
# The dependency rewrite below finishes before our explicit signing pass.
"$deploy_tool" "$bundle" -always-overwrite -verbose=2
cat > "$bundle/Contents/Resources/qt.conf" <<'CONF'
[Paths]
Plugins = PlugIns
CONF
# Resolve every non-system dependency, including Homebrew's transitive LibRaw
# libraries. Relative loader references make execution independent of Qt/brew.
python3 - "$bundle" "$arch" "$qt_root" "$source_dir" "${MACOSX_DEPLOYMENT_TARGET:-}" <<'PY'
import hashlib, io, json, os, pathlib, plistlib, re, shutil, stat, subprocess, sys, tarfile, urllib.request, zipfile

bundle, arch, qt_root, source_dir, requested = sys.argv[1:]
bundle, qt_root = pathlib.Path(bundle).resolve(), pathlib.Path(qt_root).resolve()
frameworks = bundle / "Contents/Frameworks"
frameworks.mkdir(parents=True, exist_ok=True)
main = bundle / "Contents/MacOS/SerikaPhotoEdit"
magic = {bytes.fromhex(h) for h in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
def run(*args):
    return subprocess.check_output(args, text=True).strip()
def macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in magic
def dependencies(path):
    return [line.strip().split(" (compatibility version", 1)[0] for line in run("otool", "-L", str(path)).splitlines()[1:] if " (compatibility version" in line]
def dylib_id(path):
    result = subprocess.run(["otool", "-D", str(path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"Cannot inspect Mach-O install ID: {path}: {result.stderr}")
    # Each file is thinned before this query. Executables and MH_BUNDLE plugins
    # have no LC_ID_DYLIB; framework/dylib binaries have one plain install name.
    names = [line.strip() for line in result.stdout.splitlines() if line.strip() and not line.rstrip().endswith(":" )]
    if len(names) > 1:
        raise RuntimeError(f"Expected one native Mach-O install ID: {path}: {names}")
    return names[0] if names else None
def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", run("otool", "-l", str(path)))
def inside(path):
    return path == bundle or bundle in path.parents
def system(reference):
    return reference.startswith(("/System/Library/", "/usr/lib/"))
origins = {}
external_sources = set()
copied = {}
manifest = []
brew_root = os.environ.get("HOMEBREW_PREFIX", "/opt/homebrew" if arch == "arm64" else "/usr/local")
def expand(reference, owner):
    return reference.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(main.parent))
def resolve(reference, owner):
    origin = origins.get(owner, owner)
    candidates = []
    if reference.startswith("@rpath/"):
        suffix = reference[len("@rpath/"):]
        candidates.append(frameworks / suffix)
        for search_owner in (owner, origin, main):
            for search in rpaths(search_owner):
                candidates.append(pathlib.Path(expand(search, search_owner)) / suffix)
        candidates.extend((qt_root / "lib" / suffix, pathlib.Path(brew_root) / "lib" / suffix))
    elif reference.startswith("@"):
        candidates.extend((pathlib.Path(expand(reference, owner)), pathlib.Path(expand(reference, origin))))
    elif reference.startswith("/"):
        candidates.append(pathlib.Path(reference))
    else:
        candidates.extend((owner.parent / reference, origin.parent / reference, frameworks / reference))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Unresolved dependency {reference!r} from {owner}")
def copy_dependency(source):
    if inside(source):
        return source
    if source in copied:
        return copied[source]
    text = str(source)
    if ".framework/" in text:
        root_text, suffix = text.split(".framework/", 1)
        root = pathlib.Path(root_text + ".framework")
        destination_root = frameworks / root.name
        if not destination_root.exists():
            subprocess.check_call(["ditto", str(root), str(destination_root)])
        destination = (destination_root / suffix).resolve()
    else:
        destination = frameworks / source.name
        if destination in origins and origins[destination] != source:
            raise RuntimeError(f"Dependency name collision: {source} and {origins[destination]}")
        if not destination.exists():
            shutil.copy2(source, destination)
    if not destination.is_file():
        raise RuntimeError(f"Copied dependency is missing: {destination}")
    copied[source] = destination
    origins[destination] = source
    external_sources.add(source)
    return destination
processed = set()
while True:
    pending = [p for p in bundle.rglob("*") if macho(p) and p not in processed]
    if not pending:
        break
    for path in pending:
        path.chmod(path.stat().st_mode | stat.S_IWUSR)
        slices = run("lipo", "-archs", str(path)).split()
        if arch not in slices:
            raise RuntimeError(f"{path} has {slices}, missing {arch}")
        if slices != [arch]:
            temporary = path.with_name(path.name + ".release-thin")
            subprocess.check_call(["lipo", str(path), "-thin", arch, "-output", str(temporary)])
            temporary.chmod(path.stat().st_mode)
            temporary.replace(path)
        identity = dylib_id(path)
        for reference in dependencies(path):
            if reference == identity or system(reference):
                continue
            target = copy_dependency(resolve(reference, path))
            replacement = "@loader_path/" + os.path.relpath(target, path.parent)
            if reference != replacement:
                subprocess.check_call(["install_name_tool", "-change", reference, replacement, str(path)])
        if identity:
            subprocess.check_call(["install_name_tool", "-id", "@rpath/" + os.path.relpath(path, frameworks), str(path)])
        for search in rpaths(path):
            if search.startswith("/") and not system(search + "/"):
                subprocess.check_call(["install_name_tool", "-delete_rpath", search, str(path)])
        processed.add(path)

minimums = []
for path in sorted(processed):
    load = run("otool", "-l", str(path))
    versions = re.findall(r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (?:MACOS|1)\s+minos ([\d.]+)", load)
    versions += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", load)
    minimums.extend(versions)
    entry = {"path": str(path.relative_to(bundle)), "architecture": run("lipo", "-archs", str(path)), "minimumMacOS": versions, "dependencies": dependencies(path)}
    for reference in entry["dependencies"]:
        if reference == dylib_id(path) or system(reference):
            continue
        target = resolve(reference, path)
        if not inside(target):
            raise RuntimeError(f"External runtime dependency remains: {path}: {reference}")
    manifest.append(entry)
def version_tuple(value):
    return tuple(int(v) for v in (value.split(".") + ["0", "0"])[:3])
plist_path = bundle / "Contents/Info.plist"
with plist_path.open("rb") as stream:
    info = plistlib.load(stream)
minimums.extend(filter(None, (requested, info.get("LSMinimumSystemVersion"))))
minimum = max(minimums, key=version_tuple)
info["LSMinimumSystemVersion"] = minimum
with plist_path.open("wb") as stream:
    plistlib.dump(info, stream)

notice_root = bundle / "Contents/Resources/documentation/licenses/homebrew"
# macdeployqt may already have copied a Homebrew library. Match its architecture's
# LC_UUID to the installed original so its notices are captured as well.
brew_environment = {**os.environ, "HOMEBREW_NO_AUTO_UPDATE": "1"}
installed = json.loads(subprocess.check_output(["brew", "info", "--json=v2", "--installed"], text=True, env=brew_environment))["formulae"]
cellar = pathlib.Path(run("brew", "--cellar"))
brew_libraries = {}
for formula in installed:
    name = formula["name"]
    for installation in formula.get("installed", []):
        prefix = cellar / name / installation["version"]
        for candidate in (prefix / "lib").rglob("*.dylib"):
            if candidate.is_file():
                brew_libraries.setdefault(candidate.name, set()).add(candidate.resolve())
def uuid(path):
    return re.findall(r"cmd LC_UUID\s+cmdsize \d+\s+uuid ([A-Fa-f0-9-]+)", run("otool", "-arch", arch, "-l", str(path)))
for path in processed:
    if path.suffix == ".dylib":
        for candidate in brew_libraries.get(path.name, []):
            if uuid(path) and uuid(path) == uuid(candidate):
                external_sources.add(candidate)
                break
formulas = {}
for source in sorted(external_sources):
    parts = source.parts
    if "Cellar" not in parts:
        continue
    index = parts.index("Cellar")
    if len(parts) <= index + 2:
        continue
    name, version = parts[index + 1:index + 3]
    prefix = pathlib.Path(*parts[:index + 3])
    key = f"{name}-{version}"
    if key in formulas:
        continue
    output = notice_root / key
    output.mkdir(parents=True, exist_ok=True)
    licenses = []
    for candidate in prefix.rglob("*"):
        if candidate.is_file() and candidate.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT")):
            relative = candidate.relative_to(prefix)
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(candidate, target)
            licenses.append(str(relative))
    receipt = prefix / "INSTALL_RECEIPT.json"
    if receipt.is_file():
        shutil.copy2(receipt, output / receipt.name)
    metadata = subprocess.run(["brew", "info", "--json=v2", name], text=True, capture_output=True, env=brew_environment)
    if metadata.returncode:
        raise RuntimeError(f"Cannot capture Homebrew source metadata for {name}: {metadata.stderr}")
    (output / "formula.json").write_text(metadata.stdout)
    if not licenses:
        formula = json.loads(metadata.stdout)["formulae"][0]
        if formula["versions"]["stable"] != version.split("_", 1)[0]:
            raise RuntimeError(f"Missing license text for {key}; current formula source is a different version")
        stable = formula["urls"]["stable"]
        request = urllib.request.Request(stable["url"], headers={"User-Agent": "SerikaPhotoEdit-release"})
        with urllib.request.urlopen(request, timeout=120) as response:
            archive = response.read(300 * 1024 * 1024 + 1)
        if len(archive) > 300 * 1024 * 1024 or hashlib.sha256(archive).hexdigest() != stable.get("checksum"):
            raise RuntimeError(f"Invalid or oversized upstream source archive for {key}")
        def license_name(value):
            return pathlib.PurePosixPath(value).name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT", "README.IJG"))
        texts = []
        if zipfile.is_zipfile(io.BytesIO(archive)):
            with zipfile.ZipFile(io.BytesIO(archive)) as source_archive:
                texts = [(entry.filename, source_archive.read(entry)) for entry in source_archive.infolist() if not entry.is_dir() and license_name(entry.filename) and entry.file_size < 10 * 1024 * 1024]
        else:
            with tarfile.open(fileobj=io.BytesIO(archive), mode="r:*") as source_archive:
                texts = [(entry.name, source_archive.extractfile(entry).read()) for entry in source_archive if entry.isfile() and license_name(entry.name) and entry.size < 10 * 1024 * 1024]
        for index, (upstream_name, content) in enumerate(texts):
            local_name = f"upstream-{index}-{pathlib.PurePosixPath(upstream_name).name}"
            (output / local_name).write_bytes(content)
            licenses.append(local_name)
        if not licenses:
            raise RuntimeError(f"No license text found in exact upstream source for {key}")
    formulas[key] = {"name": name, "version": version, "licenseFiles": licenses, "sourceMetadata": str((output / "formula.json").relative_to(bundle))}
revision = subprocess.run(["git", "-C", source_dir, "rev-parse", "HEAD"], text=True, capture_output=True)
result = {"architecture": arch, "minimumMacOS": minimum, "sourceRevision": revision.stdout.strip(), "libraries": manifest, "homebrew": formulas}
(bundle / "Contents/Resources/dependency-manifest.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"Audited {len(manifest)} native Mach-O files; minimum macOS {minimum}")
PY
# Sign inner code before its containers. '-' is an ad-hoc identity, not Developer ID.
python3 - "$bundle" "$identity" <<'PY'
import pathlib, subprocess, sys
bundle, identity = pathlib.Path(sys.argv[1]), sys.argv[2]
main_executable = bundle / "Contents/MacOS/SerikaPhotoEdit"
args = ["codesign", "--force", "--sign", identity]
args += ["--timestamp=none"] if identity == "-" else ["--options", "runtime", "--timestamp"]
magic = {bytes.fromhex(h) for h in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
for path in sorted(bundle.rglob("*"), key=lambda p: (-len(p.parts), str(p))):
    # Signing the main executable can seal the enclosing app immediately.
    # Let the final outer-app pass sign it after every nested code object.
    if path != main_executable and path.is_file() and not path.is_symlink():
        with path.open("rb") as stream:
            if stream.read(4) in magic:
                subprocess.check_call(args + [str(path)])
for path in sorted(bundle.rglob("*.framework"), key=lambda p: (-len(p.parts), str(p))):
    subprocess.check_call(args + [str(path)])
subprocess.check_call(args + [str(bundle)])
PY
codesign --verify --deep --strict "$bundle"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist")
minimum=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$bundle/Contents/Info.plist")
base="$output_dir/SerikaPhotoEdit-$version-macos-$arch"
zip="$base.zip"
ditto -c -k --sequesterRsrc --keepParent "$bundle" "$zip"
if [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$zip" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$bundle"
    ditto -c -k --sequesterRsrc --keepParent "$bundle" "$zip"
fi
payload="$stage/dmg"
mkdir -p "$payload"
ditto "$bundle" "$payload/SerikaPhotoEdit.app"
ln -s /Applications "$payload/Applications"
if [ "$identity" = - ]; then
    printf '%s\n' "Serika PhotoEdit $version ($arch)" "Requires macOS $minimum or newer." \
        'The app is ad-hoc signed, without Developer ID signing or notarization.' \
        'Review the source and release notes before approving it in macOS Privacy & Security.' > "$payload/README.txt"
fi
dmg="$base.dmg"
hdiutil create -volname 'Serika PhotoEdit' -srcfolder "$payload" -ov -format UDZO -fs HFS+ "$dmg"
if [ "$identity" != - ]; then
    codesign --force --sign "$identity" --timestamp "$dmg"
fi
if [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$dmg" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$dmg"
fi
hdiutil verify "$dmg"
shasum -a 256 "$zip" "$dmg" > "$base.sha256"
echo "Created $zip and $dmg (minimum macOS $minimum; signing identity: $identity)"
echo "Staging files retained at $stage"
