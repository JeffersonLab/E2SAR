---
name: delete-releases
description: Delete all GitHub releases for a given E2SAR version
disable-model-invocation: true
user-invocable: true
argument-hint: <version>
allowed-tools: Bash
---

Delete all GitHub releases matching version `$ARGUMENTS`.

Releases follow the naming pattern: `E2SAR-<version>-<branch>-<os>-<distro_version>` where os is ubuntu, rocky, or debian.

Steps:

1. First list all releases matching the version to show the user what will be deleted:
   ```
   gh release list --limit 100 | grep "E2SAR-$ARGUMENTS-"
   ```

2. Show the list to the user and ask for confirmation before deleting.

3. If the user confirms, delete each matching release:
   ```
   gh release list --limit 100 | grep "E2SAR-$ARGUMENTS-" | awk '{print $1}' | xargs -I {} gh release delete {} --yes 
   ```

4. Verify deletion by listing releases again to confirm they are gone.

Important:
- Always show the user what will be deleted BEFORE deleting
- Never clean up the associated tags
- If no releases match, inform the user and do nothing
