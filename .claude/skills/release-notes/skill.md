---
name: release-notes
description: Cut a release of this Eglantine fork: read every CHANGELOG entry since the last tag in full, write an exhaustive categorized catalog into .release/patch-notes/vX.Y.Z.md as the draft, iterate on that file with the user, distill the highlights last, merge dev into release, then tag on the user's explicit say-so. The release-binaries workflow builds every platform and publishes the file.
---

# Release notes

Turns the granular running `CHANGELOG.md` into a polished, categorized GitHub Release body, then cuts the
tag. `CHANGELOG.md` stays the source of truth (per change, written by `/version-bump`); this skill only
curates a per-release announcement from it. Ported from the ChannelGuide (Airwave) flow.

The published body comes from `.release/patch-notes/vX.Y.Z.md`. On a `vX.Y.Z` tag whose commit is on the
`release` branch, `.github/workflows/release-binaries.yml` builds the matrix and publishes the release with
that file as the body (falling back to the `## [X.Y.Z]` CHANGELOG section), followed by the download tables.

## Usage

```
/release-notes
```

Also triggered when the user says "tag a release", "cut the release" or "draft release notes".

## Process

1. **Pin the version and the range.**
   - Target version = the top entry of `CHANGELOG.md` (the latest `/version-bump`).
   - From-tag = the latest tag (`git tag --sort=-v:refname | head -1`); show it and let the user name
     another. For the very first release there is no from-tag: cover the whole changelog.
2. **Read the range in full**: every `## [x.y.z]` block between the from-tag and the target, all bullets.
3. **Write the exhaustive catalog straight to `.release/patch-notes/v<target>.md`**, the first artifact and
   the review surface:
   - a top `<!-- WORKING DRAFT -->` comment;
   - `## Highlights` with a `_(TBD, distill last.)_` placeholder;
   - the full catalog under the taxonomy below, grouping multi-version arcs into one bullet each (version
     refs like `(0.3.2)` are fine in the draft and get stripped at the end);
   - `**Full changelog:** https://github.com/Quixomatic/Eglantine/compare/<from-tag>...v<target>`.
4. **Iterate on the file with the user**: edit it in place (prune, merge, reword).
5. **Distill the highlights last**: a 1-2 sentence intro plus the headline items.
6. **Finalize and commit on `dev`**: strip the WIP comment and version refs, commit only that file as
   `docs: release notes for v<target>`, push `dev`.
7. **Merge into `release`**: `git checkout release && git merge --no-ff dev -m "release: v<target>"` and
   push `release`. (Pushing `release` builds nothing by itself.)
8. **On the user's explicit say-so, tag it**: `git tag v<target> release` then `git push origin v<target>`.
   The workflow refuses tags whose commit is not on `release`.
9. **CI takes over**: the matrix builds (about an hour) and the release appears with the notes and the
   downloads.

## Format

- A 1-2 sentence highlights intro, written last.
- Sections, omitting empty ones: `## New Features`, `## Improvements`, `## Fixes`, `## Under the hood`,
  `## Breaking / Migration`. No emoji on headings.
- Tight, user-facing bullets; dedupe and group related items.
- End with the compare link.

## Rules

- Catalog first, distill last.
- Only reorganize what is in `CHANGELOG.md`; never invent features, fixes or claims.
- Never edit `CHANGELOG.md` here.
- No em dashes in the notes (user-facing copy).
- No attribution trailers on commits.
- Tag only on explicit say-so.
- Upstream merges: summarize what they brought, don't relist upstream's commits.

## Do not

- Do NOT draft only in chat; the file is the working surface from step one.
- Do NOT tag before the notes file is committed and `release` contains it.
- Do NOT force-push or use `--no-verify`.
- Do NOT create or edit the GitHub Release with `gh` yourself; the workflow publishes it.
