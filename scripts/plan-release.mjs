import { appendFile } from "node:fs/promises";
import semanticRelease from "semantic-release";
import config from "../release.config.cjs";

let version = "0.0.0";
let publish = false;
if (process.env.GITHUB_EVENT_NAME === "push" && process.env.GITHUB_REF === "refs/heads/main") {
  // Calculate the version before native builds. Dry-run never creates tags/releases.
  const result = await semanticRelease({ ...config, dryRun: true, plugins: config.plugins.slice(0, 2) });
  if (result) {
    version = result.nextRelease.version;
    publish = true;
  }
}
if (!process.env.GITHUB_OUTPUT) throw new Error("Run this planner inside GitHub Actions");
await appendFile(process.env.GITHUB_OUTPUT, `version=${version}\npublish=${publish}\n`);
