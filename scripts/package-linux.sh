#!/usr/bin/env bash
# Native Ubuntu 24.04 packaging; does not build the application or download tools.
# SOURCE_DIR, BUILD_DIR, OUTPUT_DIR and QT_ROOT_DIR may all be absolute paths.
# Optional APPIMAGETOOL + APPIMAGE_RUNTIME enable an additional AppImage.
set -euo pipefail
workspace=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export SOURCE_DIR=${SOURCE_DIR:-"$workspace"}
export BUILD_DIR=${BUILD_DIR:-"$SOURCE_DIR/build"}
export OUTPUT_DIR=${OUTPUT_DIR:-"$workspace/dist"}
export PACKAGING_SOURCE_DIR="$workspace"
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.parse import quote


def run(args, **kwargs):
    print('+', ' '.join(str(arg) for arg in args), flush=True)
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def capture(args, **kwargs):
    return subprocess.check_output([str(arg) for arg in args], text=True, **kwargs).strip()


if platform.system() != 'Linux':
    raise SystemExit('Run this script on native Ubuntu 24.04 Linux.')
architectures = {'x86_64': 'amd64', 'aarch64': 'arm64'}
arch = platform.machine()
if arch not in architectures:
    raise SystemExit(f'Unsupported native architecture: {arch}')
for tool in ('cmake', 'ldd', 'patchelf', 'dpkg-deb', 'dpkg-query', 'file'):
    if not shutil.which(tool):
        raise SystemExit(f'Missing packaging dependency: {tool}')
source = Path(os.environ['SOURCE_DIR']).resolve(strict=True)
build = Path(os.environ['BUILD_DIR']).resolve(strict=True)
output = Path(os.environ['OUTPUT_DIR']).resolve()
qt = Path(os.environ.get('QT_ROOT_DIR', '')).resolve(strict=True)
if not (qt / 'bin/qmake').is_file():
    raise SystemExit('Set QT_ROOT_DIR to the Qt 6.8.3 desktop kit.')
qt_version = capture([qt / 'bin/qmake', '-query', 'QT_VERSION'])
if qt_version != '6.8.3':
    raise SystemExit(f'Expected Qt 6.8.3; found {qt_version}')
match = re.search(r'project\(SerikaPhotoEdit\s+VERSION\s+([0-9.]+)',
                  (source / 'CMakeLists.txt').read_text())
if not match:
    raise SystemExit('Cannot determine the application version from SOURCE_DIR.')
version = match.group(1)
# Packaging tooling can be newer than SOURCE_DIR's immutable release tag.
if (source / 'resources/icons/serika-photoedit.png').is_file():
    source_icon = source / 'resources/icons/serika-photoedit.png'
    icon_relative = 'share/icons/hicolor/512x512/apps/serika-photoedit.png'
else:
    source_icon = source / 'resources/icons/serika-photoedit.svg'
    icon_relative = 'share/icons/hicolor/scalable/apps/serika-photoedit.svg'
if not source_icon.is_file():
    raise SystemExit(f'Missing application icon in SOURCE_DIR: {source_icon}')
tool = os.environ.get('APPIMAGETOOL')
runtime = os.environ.get('APPIMAGE_RUNTIME')
if bool(tool) != bool(runtime):
    raise SystemExit('APPIMAGETOOL and APPIMAGE_RUNTIME must both be supplied.')
stem = f'SerikaPhotoEdit-{version}-linux-{arch}'
output.mkdir(parents=True, exist_ok=True)
stage = Path(tempfile.mkdtemp(prefix=f'{stem}-stage-', dir=output))
bundle = stage / stem
run(['cmake', '--install', build, '--prefix', bundle])
binary = bundle / 'bin/SerikaPhotoEdit'
if not binary.is_file():
    raise SystemExit(f'Missing installed executable: {binary}')
lib = bundle / 'lib'
plugins = bundle / 'plugins'
lib.mkdir(exist_ok=True)
plugins.mkdir(exist_ok=True)
# Include offscreen for --batch, XCB for X11 and the desktop Wayland backends.
# Qt deployment may omit plugins that were not used during the build.
for group in ('imageformats', 'iconengines', 'xcbglintegrations',
              'wayland-decoration-client', 'wayland-graphics-integration-client',
              'wayland-shell-integration'):
    if (qt / 'plugins' / group).is_dir():
        shutil.copytree(qt / 'plugins' / group, plugins / group, dirs_exist_ok=True)
(plugins / 'platforms').mkdir(exist_ok=True)
for name in ('libqoffscreen.so', 'libqminimal.so', 'libqxcb.so',
             'libqwayland-generic.so', 'libqwayland-egl.so'):
    origin = qt / 'plugins/platforms' / name
    if origin.is_file():
        shutil.copy2(origin, plugins / 'platforms' / name)
for name in ('libqoffscreen.so', 'libqxcb.so'):
    if not (plugins / 'platforms' / name).is_file():
        raise SystemExit(f'Required Qt platform plugin missing: {name}')

licenses = bundle / 'share/serika-photoedit/licenses/linux'
licenses.mkdir(parents=True, exist_ok=True)
common = Path('/usr/share/common-licenses')
if common.is_dir():
    shutil.copytree(common, licenses / 'common-licenses', symlinks=False)
# Notices may be newer than the immutable C++ release checkout.
packaging_notices = Path(os.environ['PACKAGING_SOURCE_DIR']) / 'resources/licenses'
shutil.copytree(packaging_notices, bundle / 'share/serika-photoedit/licenses', dirs_exist_ok=True)
if runtime:
    runtime_notice = None
    for directory in (source, Path(os.environ['PACKAGING_SOURCE_DIR'])):
        candidate = directory / 'resources/licenses/AppImage-type2-runtime.txt'
        if candidate.is_file():
            runtime_notice = candidate
            break
    if runtime_notice is None:
        raise SystemExit('Missing resources/licenses/AppImage-type2-runtime.txt notice.')
    shutil.copy2(runtime_notice, licenses / 'AppImage-type2-runtime.txt')
    runtime_dependencies = bundle / 'share/serika-photoedit/licenses/AppImage-runtime-DEPENDENCY-NOTICES.txt'
    if not runtime_dependencies.is_file():
        raise SystemExit('Missing AppImage runtime dependency license notices.')
dependency_env = dict(os.environ)
initial_libdirs = [str(lib), str(qt / 'lib')]
initial_libdirs.extend(str(p.parent) for p in bundle.rglob('*.so*') if p.is_file())
dependency_env['LD_LIBRARY_PATH'] = ':'.join(dict.fromkeys(initial_libdirs))
# glibc and its loader must match the host; GL/Vulkan drivers also belong to it.
host_library = re.compile(
    r'^(?:ld-linux.*|ld64.*|lib(?:c|m|dl|pthread|rt|resolv|util|anl|nss_[^.]+)\.so.*|'
    r'lib(?:GL|EGL|GLX|OpenGL|GLESv[12]|GLdispatch|vulkan)\.so.*|'
    r'lib(?:GLX|EGL)_[^.]+\.so.*)$')
host_dependencies = set()
packages = {}
bundled_libraries = []


def package_notice(origin, name):
    resolved = origin.resolve()
    if resolved.is_relative_to(qt):
        return {'library': name, 'origin': 'Qt 6.8.3 desktop kit'}
    candidates = [str(origin), str(resolved)]
    candidates.extend(p[4:] for p in list(candidates) if p.startswith('/usr/lib/'))
    owner = None
    for candidate in candidates:
        result = subprocess.run(['dpkg-query', '-S', candidate], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if result.returncode == 0:
            owner = result.stdout.splitlines()[0].split(': ', 1)[0]
            break
    if not owner:
        raise SystemExit(f'No Ubuntu package/license owner found for bundled {origin}')
    if owner not in packages:
        fields = capture(['dpkg-query', '-W', '-f',
                          '${binary:Package}\t${Version}\t${source:Package}\t${source:Version}',
                          owner]).split('\t')
        package, package_version, source_package, source_version = fields
        copyright_file = Path('/usr/share/doc') / owner.split(':')[0] / 'copyright'
        if not copyright_file.is_file():
            raise SystemExit(f'Missing copyright/license notice for {owner}')
        destination = licenses / owner.replace(':', '_')
        destination.mkdir(exist_ok=True)
        shutil.copy2(copyright_file, destination / 'copyright')
        packages[owner] = {
            'package': package, 'version': package_version,
            'source_package': source_package, 'source_version': source_version,
            'source_url': 'https://launchpad.net/ubuntu/+source/' +
                          quote(source_package, safe='') + '/' + quote(source_version, safe=''),
            'copyright': str((destination / 'copyright').relative_to(bundle)),
        }
    return {'library': name, 'ubuntu_package': owner}


def is_elf(path):
    if not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


# Inventory libraries already staged by Qt/CMake as well as our later additions.
ldconfig = next((p for p in ('/usr/sbin/ldconfig', '/sbin/ldconfig') if Path(p).is_file()), None)
if not ldconfig:
    raise SystemExit('ldconfig is required to identify original system libraries.')
system_origins = {}
for line in capture([ldconfig, '-p']).splitlines():
    if ' => ' in line:
        name = line.split()[0]
        origin = Path(line.rsplit(' => ', 1)[1])
        if origin.is_file():
            system_origins.setdefault(name, origin)
            system_origins.setdefault(origin.resolve().name, origin)
recorded = set()
for item in bundle.rglob('*'):
    if item == binary or not is_elf(item):
        continue
    if host_library.match(item.name):
        host_dependencies.add(item.name)
        item.unlink()
        continue
    origin = None
    if item.is_relative_to(plugins):
        candidate = qt / 'plugins' / item.relative_to(plugins)
        if candidate.is_file():
            origin = candidate
    if origin is None and (qt / 'lib' / item.name).is_file():
        origin = qt / 'lib' / item.name
    if origin is None:
        origin = system_origins.get(item.name)
    if origin is None:
        raise SystemExit(f'Cannot identify the source/license of staged shared library {item}')
    if item.name not in recorded:
        bundled_libraries.append(package_notice(origin, item.name))
        recorded.add(item.name)
pending = [p for p in bundle.rglob('*') if is_elf(p)]
seen = set()
while pending:
    item = pending.pop()
    key = str(item.resolve())
    if key in seen:
        continue
    seen.add(key)
    result = subprocess.run(['ldd', str(item)], env=dependency_env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode or 'not found' in result.stdout:
        raise SystemExit(f'Unresolved dependencies for {item}:\n{result.stdout}{result.stderr}')
    for line in result.stdout.splitlines():
        # Both "libname => /path (...)" and the direct ELF loader form.
        fields = line.strip().split()
        if not fields:
            continue
        if len(fields) >= 3 and fields[1] == '=>':
            name, path = fields[0], fields[2]
        elif fields[0].startswith('/'):
            path = fields[0]
            name = Path(path).name
        else:
            continue
        if host_library.match(name):
            host_dependencies.add(name)
            continue
        origin = Path(path)
        target = lib / name
        if not target.exists():
            if name not in recorded:
                bundled_libraries.append(package_notice(origin, name))
                recorded.add(name)
            shutil.copy2(origin.resolve(), target)
            pending.append(target)
# Rewrite every ELF search path; no build machine or Qt installation path remains.
for item in bundle.rglob('*'):
    if is_elf(item):
        relative = os.path.relpath(lib, item.parent)
        rpath = '$ORIGIN' if relative == '.' else '$ORIGIN/' + relative
        run(['patchelf', '--set-rpath', rpath, item])
(bundle / 'bin/qt.conf').write_text('[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nTranslations=translations\n')
launcher = bundle / 'SerikaPhotoEdit'
launcher.write_text('''#!/usr/bin/env sh
set -eu
entry=$(readlink -f -- "$0")
root=$(CDPATH= cd -- "$(dirname -- "$entry")" && pwd)
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$root/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$root/plugins/platforms"
exec "$root/bin/SerikaPhotoEdit" "$@"
''')
launcher.chmod(0o755)
build_info = build / 'RELEASE-BUILD-INFO.txt'
if build_info.is_file():
    shutil.copy2(build_info, bundle / 'share/serika-photoedit/RELEASE-BUILD-INFO.txt')
metadata = {
    'application_version': version, 'qt_version': qt_version, 'architecture': arch,
    'compatibility_target': 'Ubuntu 24.04, glibc 2.39 or newer; native architecture',
    'host_dependencies': sorted(host_dependencies),
    'bundled_libraries': sorted(bundled_libraries, key=lambda item: item['library']),
    'ubuntu_packages': sorted(packages.values(), key=lambda item: item['package']),
}
if runtime:
    metadata['appimage_runtime'] = {
        'sha256': hashlib.sha256(Path(runtime).read_bytes()).hexdigest(),
        'source_url': os.environ.get('APPIMAGE_RUNTIME_SOURCE_URL',
                      'https://github.com/AppImage/type2-runtime/tree/8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa'),
        'license_notices': ['share/serika-photoedit/licenses/linux/AppImage-type2-runtime.txt',
                            'share/serika-photoedit/licenses/AppImage-runtime-DEPENDENCY-NOTICES.txt'],
    }
(licenses / 'BundledLinuxLibraries.json').write_text(json.dumps(metadata, indent=2) + '\n')
(bundle / 'RUNNING.txt').write_text(
    f'Serika PhotoEdit {version} ({arch})\n\nRun ./SerikaPhotoEdit from this directory.\n'
    'Built for Ubuntu 24.04 (glibc 2.39+) on the indicated native architecture.\n'
    'Other distributions are unverified. glibc, graphics drivers, an X11/Wayland\n'
    'session and installed system fonts remain host requirements.\n'
    'Qt and other bundled shared-library licenses and source provenance are in\n'
    'share/serika-photoedit/licenses/. RAW and PDF support are retained when built.\n')

# Check the actual staged bundle without Qt/build paths, inherited library paths,
# user settings or an inherited graphical session. Reopen the generated SPE twice.
smoke = stage / 'smoke'
smoke.mkdir()
(smoke / 'home').mkdir()
(smoke / 'runtime').mkdir(mode=0o700)
(smoke / 'input').mkdir()
(smoke / 'input/pixels.ppm').write_bytes(b'P6\n2 2\n255\n' + bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255)))
(smoke / 'resize.speaction').write_text(json.dumps({'version': 1, 'steps': [
    {'command': 'resize', 'parameters': {'width': 4, 'height': 3}}]}))
clean = {'PATH': '/usr/bin:/bin', 'HOME': str(smoke / 'home'), 'LANG': 'C.UTF-8',
         'XDG_RUNTIME_DIR': str(smoke / 'runtime'), 'QT_QPA_PLATFORM': 'offscreen'}
smoke_log = output / f'{stem}-smoke.log'


def check_application(entry, args, env=clean):
    result = run([entry, *args], env=env, cwd=smoke, text=True,
                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    with smoke_log.open('a') as stream:
        stream.write(f'{entry} {args}\n{result.stdout}\n')
    return result.stdout


def check_png(path):
    if not path.is_file() or path.stat().st_size < 1000 or path.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'Screenshot smoke check failed: {path}')


if version not in check_application(launcher, ['--version']):
    raise SystemExit('Packaged executable version does not match the source version.')
check_application(launcher, ['--demo', '--screenshot', str(smoke / 'demo.png')])
check_png(smoke / 'demo.png')
check_application(launcher, ['--batch', str(smoke / 'resize.speaction'),
                            str(smoke / 'input'), str(smoke / 'first')])
first = smoke / 'first/pixels.spe'
if not first.is_file() or first.stat().st_size == 0:
    raise SystemExit('Batch did not create the native document.')
check_application(launcher, ['--batch', str(smoke / 'resize.speaction'),
                            str(smoke / 'first'), str(smoke / 'reopened')])
if not (smoke / 'reopened/pixels.spe').is_file():
    raise SystemExit('Batch could not reopen the generated native document.')
check_application(launcher, [str(first), '--screenshot', str(smoke / 'reopen.png')])
check_png(smoke / 'reopen.png')
if not shutil.which('xvfb-run'):
    raise SystemExit('Install xvfb and xauth for the required XCB desktop smoke check.')
xcb = dict(clean, QT_QPA_PLATFORM='xcb')
result = run(['xvfb-run', '-a', launcher, '--demo', '--screenshot', smoke / 'xcb.png'],
             env=xcb, cwd=smoke, text=True, stdout=subprocess.PIPE,
             stderr=subprocess.STDOUT, timeout=90)
with smoke_log.open('a') as stream:
    stream.write('Xvfb XCB demo\n' + result.stdout + '\n')
check_png(smoke / 'xcb.png')

archive = output / f'{stem}.tar.gz'
with tarfile.open(archive, 'w:gz') as tar:
    tar.add(bundle, arcname=stem)
# Check an extracted archive, preserving executable bits and symlinks.
extracted = stage / 'extracted'
with tarfile.open(archive) as tar:
    tar.extractall(extracted)
check_application(extracted / stem / 'SerikaPhotoEdit', ['--version'])
check_application(extracted / stem / 'SerikaPhotoEdit',
                  ['--demo', '--screenshot', str(smoke / 'archive.png')])
check_png(smoke / 'archive.png')

deb_root = stage / 'deb'
payload = deb_root / 'opt/serika-photoedit'
shutil.copytree(bundle, payload, symlinks=True)
(deb_root / 'usr/bin').mkdir(parents=True)
os.symlink('../../opt/serika-photoedit/SerikaPhotoEdit', deb_root / 'usr/bin/SerikaPhotoEdit')
for relative in ('share/applications/io.serika.PhotoEdit.desktop',
                 'share/mime/packages/io.serika.PhotoEdit.xml',
                 icon_relative):
    target = deb_root / 'usr' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(bundle / relative, target)
control = deb_root / 'DEBIAN'
control.mkdir()
size = sum(p.stat().st_size for p in payload.rglob('*') if p.is_file()) // 1024
(control / 'control').write_text(
    f'Package: serika-photoedit\nVersion: {version}\nArchitecture: {architectures[arch]}\n'
    'Maintainer: Serika developers\nSection: graphics\nPriority: optional\n'
    f'Installed-Size: {size}\n'
    'Depends: libc6 (>= 2.39), libgl1, libegl1, libopengl0, fontconfig, fonts-dejavu-core\n'
    'Homepage: https://github.com/serika-dev/SerikaPhotoEdit\n'
    'Description: Native photo editing with layers, masks, RAW and PSD\n'
    ' Qt and application dependencies are bundled under /opt/serika-photoedit.\n'
    ' Built and verified on Ubuntu 24.04; other distributions are unverified.\n')
deb = output / f'SerikaPhotoEdit-{version}-linux-{architectures[arch]}.deb'
run(['dpkg-deb', '--root-owner-group', '--build', deb_root, deb])
run(['dpkg-deb', '--info', deb])
deb_check = stage / 'deb-extracted'
run(['dpkg-deb', '--extract', deb, deb_check])
check_application(deb_check / 'usr/bin/SerikaPhotoEdit', ['--version'])

if tool:
    appdir = stage / 'AppDir'
    shutil.copytree(bundle, appdir / 'usr', symlinks=True)
    (appdir / 'AppRun').write_text('''#!/usr/bin/env sh
set -eu
root=${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
exec "$root/usr/SerikaPhotoEdit" "$@"
''')
    (appdir / 'AppRun').chmod(0o755)
    shutil.copy2(source / 'packaging/linux/io.serika.PhotoEdit.desktop', appdir)
    shutil.copy2(source_icon, appdir)
    os.symlink(source_icon.name, appdir / '.DirIcon')
    appimage = output / f'{stem}.AppImage'
    tool_env = dict(os.environ, ARCH=arch, APPIMAGE_EXTRACT_AND_RUN='1')
    run([Path(tool).resolve(strict=True), '--runtime-file', Path(runtime).resolve(strict=True),
         appdir, appimage], env=tool_env)
    appimage.chmod(0o755)
    check_application(appimage, ['--version'], dict(clean, APPIMAGE_EXTRACT_AND_RUN='1'))
    check_application(appimage, ['--demo', '--screenshot', str(smoke / 'appimage.png')],
                      dict(clean, APPIMAGE_EXTRACT_AND_RUN='1'))
    check_png(smoke / 'appimage.png')
with smoke_log.open('a') as stream:
    stream.write('PASS: isolated version/demo/capture/batch/native reopen/XCB/archive/DEB'
                 + ('/AppImage' if tool else '') + '\n')
artifacts = build / 'package-smoke' / arch
artifacts.mkdir(parents=True, exist_ok=True)
for item in smoke.rglob('*'):
    if item.is_file() and item.suffix in ('.png', '.spe', '.speaction', '.ppm'):
        target = artifacts / item.relative_to(smoke)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
shutil.copy2(smoke_log, artifacts / smoke_log.name)
print(f'Created {archive}\nCreated {deb}\nSmoke log: {smoke_log}\nStage retained: {stage}')
PY
