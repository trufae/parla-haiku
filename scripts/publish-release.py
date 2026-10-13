#!/usr/bin/env python3
"""Publish existing successful CI artifacts; never rebuild release binaries."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def gh(*args):
    return subprocess.check_output(["gh", *map(str, args)], text=True)


manifest = json.loads(Path("docs/releases/publish.json").read_text())
revision = manifest["revision"]
release_revision = os.environ["GITHUB_SHA"]
version = manifest["version"]
assert re.fullmatch(r"[0-9a-f]{40}", revision)
assert re.fullmatch(r"[0-9a-f]{40}", release_revision)
assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version)
# Tag the publication commit, including its release notes. Only publishing
# metadata may differ from the revision which passed the native runtime tests.
subprocess.run(["git", "diff", "--exit-code", revision, release_revision, "--",
                ".", ":(exclude)docs/**", ":(exclude)README.md",
                ":(exclude).github/workflows/release.yml",
                ":(exclude)scripts/publish-release.py"], check=True)
repository = os.environ["GH_REPO"]
assets = Path("release-assets")
assets.mkdir()
packages = []
for arch, workflow in (("x86", ".github/workflows/haiku-x86.yml"),
                       ("x86_64", ".github/workflows/haiku.yml")):
    run_id = int(manifest["runs"][arch])
    run = json.loads(gh("api", f"repos/{repository}/actions/runs/{run_id}"))
    assert run["conclusion"] == "success", f"{arch}: build did not pass"
    build_revision = run["head_sha"]
    # Test/CI-only fixes need not rebuild the other architecture. Verify every
    # application, dependency, resource and build-script input is unchanged.
    subprocess.run(["git", "diff", "--exit-code", build_revision, revision, "--",
                    ".", ":(exclude)docs/**", ":(exclude)README.md",
                    ":(exclude)tests/**", ":(exclude).github/workflows/**",
                    ":(exclude)scripts/publish-release.py"], check=True)
    subprocess.run(["git", "diff", "--exit-code", build_revision, revision,
                    "--", workflow], check=True)
    if arch == "x86_64":
        assert build_revision == revision, "Native tests must pass at the release revision"
    assert run["path"] == workflow, f"{arch}: wrong workflow"
    assert run["head_repository"]["full_name"] == repository
    assert run["event"] in ("push", "workflow_dispatch")
    destination = assets / arch
    gh("run", "download", run_id, "--name", f"parla-haiku-{arch}",
       "--dir", destination)
    package = destination / f"parla-{version}-1-{arch}.hpkg"
    assert package.stat().st_size > 1_000_000, f"{arch}: missing or truncated package"
    assert package.read_bytes()[:4] == b"hpkg", f"{arch}: invalid package header"
    packages.append(package)

checksums = assets / "SHA256SUMS"
checksums.write_text("".join(
    f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in packages))
tag = f"v{version}"
notes = Path(f"docs/releases/{tag}.md")
assert notes.is_file()
uploads = [*packages, checksums]
screenshot = assets / "x86_64" / "parla-native.png"
if screenshot.is_file():
    uploads.append(screenshot)
# Upload everything to a draft first, so a published release is never incomplete.
gh("release", "create", tag, *uploads, "--draft", "--prerelease",
   "--target", release_revision, "--title", f"Parla for Haiku {version} preview",
   "--notes-file", notes)
gh("release", "edit", tag, "--draft=false")
print(gh("release", "view", tag, "--json", "url,assets,isDraft,isPrerelease"))
