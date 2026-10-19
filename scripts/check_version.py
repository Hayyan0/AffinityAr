#!/usr/bin/env python3
import os
import re
import json
import subprocess
import sys

def get_content_from_git(ref, filepath):
    try:
        cmd = ["git", "show", f"{ref}:{filepath}"]
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return None

def get_content_from_disk(filepath):
    if os.path.exists(filepath):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                return f.read()
        except Exception:
            return None
    return None

def extract_version(ref=None):
    # 1. version.txt
    txt = get_content_from_git(ref, "version.txt") if ref else get_content_from_disk("version.txt")
    if txt and txt.strip():
        return txt.strip().splitlines()[0].strip()

    # 2. installer/src-tauri/Cargo.toml
    cargo = get_content_from_git(ref, "installer/src-tauri/Cargo.toml") if ref else get_content_from_disk("installer/src-tauri/Cargo.toml")
    if cargo:
        m = re.search(r'(?m)^version\s*=\s*"([^"]+)"', cargo)
        if m:
            return m.group(1).strip()

    # 3. installer/src-tauri/tauri.conf.json
    tauri = get_content_from_git(ref, "installer/src-tauri/tauri.conf.json") if ref else get_content_from_disk("installer/src-tauri/tauri.conf.json")
    if tauri:
        try:
            data = json.loads(tauri)
            if "version" in data and data["version"]:
                return str(data["version"]).strip()
        except Exception:
            pass

    return "1.0.0"

def main():
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    ref_type = os.environ.get("GITHUB_REF_TYPE", "")
    ref_name = os.environ.get("GITHUB_REF_NAME", "")
    before_sha = os.environ.get("GITHUB_EVENT_BEFORE", "")

    current_version = extract_version(None)
    version_shifted = False
    reason = ""

    if event_name == "workflow_dispatch":
        version_shifted = True
        reason = "Manual workflow dispatch trigger"
    elif ref_type == "tag":
        version_shifted = True
        tag_clean = ref_name.lstrip("v")
        if tag_clean:
            current_version = tag_clean
        reason = f"Tag push triggered ({ref_name})"
    else:
        # Determine previous commit SHA
        prev_ref = before_sha
        zero_sha = "0000000000000000000000000000000000000000"
        if not prev_ref or prev_ref == zero_sha:
            try:
                subprocess.check_call(["git", "rev-parse", "--verify", "HEAD~1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                prev_ref = "HEAD~1"
            except Exception:
                prev_ref = None

        if not prev_ref:
            version_shifted = True
            reason = "Initial commit or no previous commit found in history"
        else:
            prev_version = extract_version(prev_ref)
            print(f"Previous version ({prev_ref}): {prev_version}")
            print(f"Current version (HEAD): {current_version}")
            if current_version != prev_version:
                version_shifted = True
                reason = f"Version shifted from {prev_version} to {current_version}"
            else:
                version_shifted = False
                reason = f"Version did not change ({current_version} == {prev_version})"

    tag_name = f"v{current_version}"
    release_title = f"Affinity Arabic {tag_name}"

    print(f"Result: version_shifted={str(version_shifted).lower()}")
    print(f"Reason: {reason}")
    print(f"Resolved Version: {current_version}")
    print(f"Resolved Tag: {tag_name}")
    print(f"Resolved Title: {release_title}")

    # Set GitHub Actions step outputs if running in Actions environment
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output and os.path.exists(github_output):
        with open(github_output, "a", encoding="utf-8") as f:
            f.write(f"version_shifted={str(version_shifted).lower()}\n")
            f.write(f"version={current_version}\n")
            f.write(f"tag_name={tag_name}\n")
            f.write(f"release_title={release_title}\n")

    github_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary and os.path.exists(github_summary):
        with open(github_summary, "a", encoding="utf-8") as f:
            if version_shifted:
                f.write(f"### :rocket: Version shifted to `{current_version}`\n")
                f.write(f"- **Trigger**: {reason}\n")
                f.write(f"- **Draft Release Tag**: `{tag_name}`\n")
                f.write("Proceeding to build hook and pack installer.\n")
            else:
                f.write(f"### :fast_forward: Version unchanged (`{current_version}`)\n")
                f.write(f"- **Reason**: {reason}\n")
                f.write("Skipping installer build and release for this commit.\n")

if __name__ == "__main__":
    main()
