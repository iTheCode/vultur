"""Check that CI packaging refuses invalid inputs without touching existing files."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('vultur_build', Path(__file__).resolve().parents[1] / 'scripts/build.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BuildToolsTests(unittest.TestCase):
    def test_vst3_manifest_version_with_sdk_trailing_commas(self):
        manifest = '{\n  "Name": "Vultur",\n  "Version": "1.2.3",\n  "Factory Info": {"Flags": 16,},\n}\n'
        self.assertEqual(builder.manifest_version(manifest), '1.2.3')
        with self.assertRaisesRegex(ValueError, 'no version'):
            builder.manifest_version('{"Name": "Vultur"}')

    def args(self, **changes):
        values = dict(version='1.2.3', arch='arm64', build_dir='build', dist_dir='dist', jobs=2,
                      package=True, juce_source=None)
        values.update(changes)
        return SimpleNamespace(**values)

    def test_invalid_versions_do_not_run_commands(self):
        for version in ('1', '1.2', '01.2.3', '1.2.3-beta', '1.2.3; echo invalid'):
            with self.subTest(version=version), patch.object(builder, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'MAJOR.MINOR.PATCH'):
                    builder.build(self.args(version=version))
                run.assert_not_called()

    def test_existing_package_files_are_preserved_before_building(self):
        for suffix in ('.zip', '.zip.sha256', '.json'):
            with self.subTest(suffix=suffix):
                # Retain the fixture, matching the project's no-deletion policy.
                root = Path(tempfile.mkdtemp(prefix='vultur-build-test-'))
                (root / 'dist').mkdir()
                existing = root / 'dist' / ('Vultur-1.2.3-macOS-arm64' + suffix)
                existing.write_bytes(b'existing user package')
                with patch.object(builder, 'ROOT', root), patch.object(builder.platform, 'system', return_value='Darwin'), patch.object(builder, 'run') as run:
                    with self.assertRaises(FileExistsError):
                        builder.build(self.args())
                    run.assert_not_called()
                    self.assertEqual(existing.read_bytes(), b'existing user package')

    def test_unsupported_platform_does_not_build(self):
        with patch.object(builder.platform, 'system', return_value='Linux'), patch.object(builder, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'native macOS or Windows'):
                builder.build(self.args())
            run.assert_not_called()

    def test_windows_arm64_is_not_labelled_as_x64(self):
        with patch.object(builder.platform, 'system', return_value='Windows'), patch.object(builder, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Supported'):
                builder.build(self.args())
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
