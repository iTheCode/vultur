import assert from "node:assert/strict";
import { test } from "node:test";
import { mkdtemp, mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { analyzeCommits } from "@semantic-release/commit-analyzer";
import { generateNotes } from "@semantic-release/release-notes-generator";
import config from "../release.config.cjs";
import { platforms, verifyRelease } from "../scripts/verify-release.mjs";

// Exercise the asset resolver of the locked GitHub plugin without publishing.
const { default: globAssets } = await import(new URL("lib/glob-assets.js", import.meta.resolve("@semantic-release/github")));

const analyzer = config.plugins.find(([name]) => name === "@semantic-release/commit-analyzer")[1];
const notesOptions = config.plugins.find(([name]) => name === "@semantic-release/release-notes-generator")[1];
const logger = { log() {} };

for (const [message, expected] of [
  ["fix: improve echo filtering", "patch"],
  ["perf: reduce effect CPU usage", "patch"],
  ["feat: add tempo-synced effects", "minor"],
  ["feat!: change effect parameter format", "major"],
  ["fix: migrate parameters\n\nBREAKING CHANGE: existing sessions need migration", "major"],
  ["docs: explain installation", null],
  ["ci: configure VST3 releases", null],
]) {
  test(`release classification: ${message.split("\n")[0]}`, async () => {
    const result = await analyzeCommits(analyzer, {
      cwd: process.cwd(), logger, commits: [{ hash: "abc1234", message }],
    });
    assert.equal(result, expected);
  });
}

test("release notes describe the plugin changes", async () => {
  const notes = await generateNotes(notesOptions, {
    cwd: process.cwd(), logger,
    options: { repositoryUrl: "https://github.com/example/vultur.git" },
    commits: [{ hash: "0123456789012345678901234567890123456789", message: "feat: add tempo-synced effects" }],
    lastRelease: { version: "1.0.0", gitTag: "v1.0.0" },
    nextRelease: { version: "1.1.0", gitTag: "v1.1.0" },
  });
  assert.match(notes, /1\.1\.0/);
  assert.match(notes, /add tempo-synced effects/);
});

test("GitHub plugin resolves all native archives and checksums", async () => {
  const cwd = await mkdtemp(join(tmpdir(), "vultur-release-assets-"));
  await mkdir(join(cwd, "dist"));
  const expected = [];
  for (const platform of platforms) {
    const zip = `dist/Vultur-1.2.3-${platform}.zip`;
    await writeFile(join(cwd, zip), "archive fixture");
    await writeFile(join(cwd, `${zip}.sha256`), "checksum fixture");
    expected.push(zip, `${zip}.sha256`);
  }
  await writeFile(join(cwd, "dist/unrelated.txt"), "not a release asset");
  const githubOptions = config.plugins.find(([name]) => name === "@semantic-release/github")[1];
  const assets = await globAssets({ cwd }, githubOptions.assets);
  assert.deepEqual(assets.map((asset) => asset.path).sort(), expected.sort());
});

async function packages(change = () => {}) {
  const directory = await mkdtemp(join(tmpdir(), "vultur-native-packages-"));
  for (const platform of platforms) {
    const stem = `Vultur-1.2.3-${platform}`;
    const content = Buffer.from(`native package fixture: ${platform}`);
    const sha256 = createHash("sha256").update(content).digest("hex");
    const metadata = { version: "1.2.3", platform, commit: "tested-commit", archive: `${stem}.zip`, sha256,
      formats: platform.startsWith("macOS") ? ["VST3", "AU", "Standalone"] : ["VST3", "Standalone"] };
    change(metadata);
    await writeFile(join(directory, `${stem}.zip`), content);
    await writeFile(join(directory, `${stem}.zip.sha256`), `${sha256}  ${stem}.zip\n`);
    await writeFile(join(directory, `${stem}.json`), JSON.stringify(metadata));
  }
  return directory;
}

test("publication accepts three packages built from the exact version and commit", async () => {
  await verifyRelease("1.2.3", await packages(), "tested-commit");
});
test("publication refuses a different commit", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages(), "untested-commit"), /mismatch/);
});
test("publication refuses a changed embedded version declaration", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.version = "1.2.2"; }), "tested-commit"), /mismatch/);
});
test("publication refuses missing AU in a macOS package", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.formats = ["VST3", "Standalone"]; }), "tested-commit"), /format/);
});
test("publication refuses a package without VST3", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.formats = m.formats.filter((format) => format !== "VST3"); }), "tested-commit"), /format/);
});
test("publication refuses a package without the standalone app", async () => {
  await assert.rejects(verifyRelease("1.2.3", await packages((m) => { m.formats = m.formats.filter((format) => format !== "Standalone"); }), "tested-commit"), /format/);
});
test("publication refuses corrupted bytes", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Vultur-1.2.3-windows-x64.zip"), "corrupted");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Checksum/);
});
test("publication refuses absent builds", async () => {
  const directory = await mkdtemp(join(tmpdir(), "vultur-missing-platform-"));
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /ENOENT/);
});
test("publication refuses stale extra archives", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Vultur-1.2.2-macOS-arm64.zip"), "old package");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Unexpected archive/);
});
test("publication refuses stale extra checksums", async () => {
  const directory = await packages();
  await writeFile(join(directory, "Vultur-1.2.2-windows-x64.zip.sha256"), "old checksum");
  await assert.rejects(verifyRelease("1.2.3", directory, "tested-commit"), /Unexpected archive/);
});
for (const event of ["pull_request", "workflow_dispatch"]) {
  test(`${event} creates only a preview version plan`, async () => {
    const directory = await mkdtemp(join(tmpdir(), "vultur-plan-"));
    const output = join(directory, "output");
    const result = spawnSync(process.execPath, ["scripts/plan-release.mjs"], { encoding: "utf8",
      env: { ...process.env, GITHUB_EVENT_NAME: event, GITHUB_REF: "refs/heads/main", GITHUB_OUTPUT: output } });
    assert.equal(result.status, 0, result.stderr);
    const { readFile } = await import("node:fs/promises");
    assert.equal(await readFile(output, "utf8"), "version=0.0.0\npublish=false\n");
  });
}
