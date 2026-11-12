#!/usr/bin/env python3
"""Native macOS/Windows builds, tests and versioned plugin packages."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def run(*command):
    print('+ ' + ' '.join(map(str, command)), flush=True)
    subprocess.run([str(item) for item in command], cwd=ROOT, check=True)


def cmake_tool():
    found = shutil.which(os.environ.get('CMAKE_BIN', 'cmake'))
    local = ROOT / '.tools/venv' / ('Scripts/cmake.exe' if os.name == 'nt' else 'bin/cmake')
    if found:
        return Path(found)
    if local.is_file():
        return local
    raise RuntimeError('Install CMake >= 3.22 or set CMAKE_BIN to its executable.')


def manifest_version(contents):
    # The pinned VST3 SDK emits trailing commas, accepted by its own parser but
    # rejected by Python's strict JSON decoder. Read the version field directly.
    match = re.search(r'^\s*"Version"\s*:\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*,?\s*$', contents, re.MULTILINE)
    if not match:
        raise ValueError('VST3 manifest has no version field')
    return match.group(1)


def build(args):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', args.version):
        raise ValueError('Version must be MAJOR.MINOR.PATCH')
    system = platform.system()
    if system not in ('Darwin', 'Windows'):
        raise RuntimeError('Use a native macOS or Windows runner to build the plugins.')
    architecture = args.arch or ('arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x86_64')
    if architecture not in ('arm64', 'x86_64') or (system == 'Windows' and architecture != 'x86_64'):
        raise ValueError('Supported: macOS arm64/x86_64 and Windows x86_64')
    platform_name = f'macOS-{architecture}' if system == 'Darwin' else 'windows-x64'
    build_dir = (ROOT / args.build_dir).resolve()
    output = (ROOT / args.dist_dir).resolve()
    stem = f'Vultur-{args.version}-{platform_name}'
    archive = output / (stem + '.zip')
    if args.package and any((output / (stem + suffix)).exists() for suffix in ('.zip', '.zip.sha256', '.json')):
        raise FileExistsError(f'Package already exists: {archive}. Use a different --dist-dir; no files were removed.')
    cmake = cmake_tool()
    config = [cmake, '-S', ROOT, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Release',
              '-DVULTUR_VERSION=' + args.version, '-DBUILD_TESTING=ON', '-DVULTUR_BUILD_PREVIEW=OFF']
    if system == 'Darwin':
        config += ['-DCMAKE_OSX_ARCHITECTURES=' + architecture]
    else:
        config += ['-G', 'Visual Studio 17 2022', '-A', 'x64']
    if args.juce_source:
        config += ['-DFETCHCONTENT_SOURCE_DIR_JUCE=' + str(Path(args.juce_source).resolve())]
    run(*config)
    run(cmake, '--build', build_dir, '--config', 'Release', '--parallel', str(args.jobs))
    ctest = cmake.with_name('ctest.exe' if os.name == 'nt' else 'ctest')
    run(ctest, '--test-dir', build_dir, '-C', 'Release', '--output-on-failure')

    formats = {'VST3': 'Vultur.vst3'}
    if system == 'Darwin':
        formats['AU'] = 'Vultur.component'
    formats['Standalone'] = 'Vultur.app' if system == 'Darwin' else 'Vultur.exe'
    products = build_dir / 'Vultur_artefacts/Release'
    for kind, filename in formats.items():
        bundle = products / kind / filename
        if not bundle.exists():
            raise FileNotFoundError(bundle)
        if system == 'Darwin':
            with (bundle / 'Contents/Info.plist').open('rb') as stream:
                info = plistlib.load(stream)
            if info['CFBundleShortVersionString'] != args.version:
                raise RuntimeError(f'Incorrect embedded version: {bundle}')
            run('lipo', bundle / 'Contents/MacOS/Vultur', '-verify_arch', architecture)
            # Local ad-hoc sealing does not notarize the application.
            run('codesign', '--force', '--sign', '-', bundle)
            run('codesign', '--verify', '--deep', '--strict', bundle)
    manifest = products / 'VST3/Vultur.vst3/Contents/Resources/moduleinfo.json'
    if manifest_version(manifest.read_text(encoding='utf-8')) != args.version:
        raise RuntimeError('VST3 manifest version does not match the requested version')
    if not args.package:
        return

    output.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)
    # Keep staging separate from the final archive; never touch existing packages.
    stage = Path(tempfile.mkdtemp(prefix='package-', dir=build_dir))
    for kind, filename in formats.items():
        source = products / kind / filename
        destination = stage / kind / filename
        destination.parent.mkdir(parents=True, exist_ok=True)
        if system == 'Darwin':
            run('ditto', source, destination)
        elif source.is_dir():
            shutil.copytree(source, destination)
        else:
            shutil.copy2(source, destination)
    licenses = stage / 'licenses/fonts'
    licenses.mkdir(parents=True)
    for filename in ('LICENSE.txt', 'NOTICE.txt'):
        shutil.copy2(ROOT / 'assets/fonts' / filename, licenses / filename)
    if system == 'Darwin':
        run('ditto', '-c', '-k', '--sequesterRsrc', stage, archive)
    else:
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as zipped:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    zipped.write(file, file.relative_to(stage).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / (stem + '.zip.sha256')).write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
    metadata = {'version': args.version, 'platform': platform_name, 'formats': list(formats),
                'commit': os.environ.get('GITHUB_SHA', 'local'), 'archive': archive.name, 'sha256': digest}
    (output / (stem + '.json')).write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'Package ready: {archive}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--arch', choices=('arm64', 'x86_64'))
    parser.add_argument('--build-dir', default=os.environ.get('VULTUR_BUILD_DIR', 'build'))
    parser.add_argument('--dist-dir', default='dist')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--package', action='store_true')
    parser.add_argument('--juce-source', help='Use an existing checkout of the pinned JUCE revision')
    build(parser.parse_args())
