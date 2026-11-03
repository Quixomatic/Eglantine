---
name: version-bump
description: Record a change in this Eglantine fork: pick the next semver version, write a CHANGELOG.md entry from the actual diff, commit with the entry as the commit body, and push dev. Use after each completed task.
---

# Version Bump

Records a change: writes a `CHANGELOG.md` entry for the next version, commits it (with the code change or
on its own) using the entry as the commit body, and pushes `dev`. Ported from the ChannelGuide (Airwave)
flow. This repo has no version files to edit: the version lives in `CHANGELOG.md` and, at release time, in
the `vX.Y.Z` tag.

## Usage

```
/version-bump [patch|minor] <short-narrative>
```

Examples:
```
/version-bump fix route replay skipping a checkpoint
/version-bump add the debugger tools to the MCP
/version-bump minor start the 0.2 line      (only when the user asks for a minor bump)
```

**Every bump is a patch bump (x.y.Z)**, whatever the change: fixes, features, upstream merges, CI. The
line started at 0.1.x.

- **minor** (x.Y.0): ONLY when the user explicitly says so. Never infer it from the size of a change.
- **major** (X.0.0): never, unless the user explicitly asks for it.

**Exactly three segments, never a fourth** (no `0.6.7.1`).

## Branches

- `nx2`: mirror of upstream; never commit here.
- `dev`: the work branch. Version bumps are committed and pushed here.
- `release`: gets `dev` merged in when cutting a release (see the `release-notes` skill).

## Instructions

1. **Read `CHANGELOG.md`** for the most recent version (top entry).
2. **Pick the new version** from the tier and the previous version.
3. **Inspect what is shipping**: `git status`, `git diff`, `git diff --staged`, and the commits since the
   last entry. Do not write the entry from the narrative alone.
4. **Draft the entry** in the established format (Keep a Changelog style):

   ```markdown
   ## [X.Y.Z] - YYYY-MM-DD

   <one short paragraph on what this change is about>

   ### Added / Changed / Fixed / Removed / Internal
   - **Bold lead-in.** What changed, naming files, tools, settings or surfaces, and why.

   ### Breaking / Migration   (only when relevant)
   ```

   Use only the sections that apply. Insert it at the top, above the previous entry. For an upstream merge,
   summarize what came in (with the upstream commit) instead of listing every upstream commit.
5. **Show the user the proposed entry** before writing it, unless told to proceed autonomously.
6. **Write the entry**, then **commit and push**:
   - Stage `CHANGELOG.md` together with the change it describes (or alone if the change is committed).
   - Subject: `<type>: vX.Y.Z — <short narrative>`: `feat` (minor), `fix` (patch), `chore`, `ci`,
     `refactor`, `docs`, `merge` as fitting. Lowercase after the colon, imperative, at most 72 chars,
     em dash between version and narrative.
   - Body: the changelog entry, passed via a HEREDOC.
   - `git push origin dev`.
7. **Confirm** the new version, the commit hash and the pushed branch.

## Cadence

During multi-task work, bump + commit + push after EACH completed task, not batched at the end. Once the
cadence is set for a session, do it autonomously per task; show the entry only if it is non-trivial or
risky.

## Do not

- Do NOT add attribution trailers (`Co-Authored-By`, `Claude-Session:`, `Assisted-by`, `Signed-off-by`).
- Do NOT amend to fold a bump into an earlier commit unless asked.
- Do NOT use `--no-verify` or force-push.
- Do NOT commit to or push `nx2`.
- Do NOT propose a fourth version segment.
- Do NOT `git tag` on your own initiative: tags are the user's explicit call every time (a `v*` tag on
  `release` builds and publishes binaries).
