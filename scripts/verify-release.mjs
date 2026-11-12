import { createHash } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

export const platforms = ["macOS-arm64", "macOS-x86_64", "windows-x64"];

export async function verifyRelease(version, directory = "dist", commit = process.env.GITHUB_SHA) {
  if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version)) throw new Error("Invalid release version");
  if (!commit) throw new Error("GITHUB_SHA is required to verify the built commit");
  const expectedArchives = [];
  for (const platform of platforms) {
    const stem = `Vultur-${version}-${platform}`;
    const archive = `${stem}.zip`;
    expectedArchives.push(archive);
    const metadata = JSON.parse(await readFile(resolve(directory, `${stem}.json`), "utf8"));
    if (metadata.version !== version || metadata.platform !== platform || metadata.commit !== commit || metadata.archive !== archive)
      throw new Error(`Version, platform or commit mismatch: ${platform}`);
    if (!Array.isArray(metadata.formats) || !metadata.formats.includes("VST3") || !metadata.formats.includes("Standalone") || (platform.startsWith("macOS") && !metadata.formats.includes("AU")))
      throw new Error(`Required plugin format is missing: ${platform}`);
    const digest = createHash("sha256").update(await readFile(resolve(directory, archive))).digest("hex");
    const checksum = await readFile(resolve(directory, `${archive}.sha256`), "utf8");
    if (digest !== metadata.sha256 || checksum.trim() !== `${digest}  ${archive}`)
      throw new Error(`Checksum mismatch: ${archive}`);
  }
  const actualArchives = (await readdir(directory)).filter((name) => name.endsWith(".zip") || name.endsWith(".zip.sha256")).sort();
  const expectedFiles = expectedArchives.flatMap((name) => [name, `${name}.sha256`]).sort();
  if (JSON.stringify(actualArchives) !== JSON.stringify(expectedFiles))
    throw new Error("Unexpected archive in release directory");
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await verifyRelease(process.argv[2]);
  console.log("Verified version, commit, formats and checksums for all three native packages.");
}
