# Release patch notes

Curated, user-facing release notes, one file per release: `vX.Y.Z.md`.

These are not the changelog. `CHANGELOG.md` is the granular, per-change running log (written by
`/version-bump`). A file here is the polished announcement for a whole release, coalesced from every
changelog entry since the previous tag.

## How it works

1. The `/release-notes` skill reads the `CHANGELOG.md` entries since the last tag and, with a human in the
   loop, writes the curated notes to `vX.Y.Z.md` here, then commits it before the `vX.Y.Z` tag.
2. `.github/workflows/release-binaries.yml` runs on the tag (which must be on the `release` branch), builds
   every platform and publishes the GitHub Release with this file as the body, followed by the download
   tables.
3. If `vX.Y.Z.md` is missing at tag time, the workflow uses the matching `## [X.Y.Z]` section of
   `CHANGELOG.md` instead.

## Format

A 1-2 sentence highlights intro, then the sections that apply: `## New Features`, `## Improvements`,
`## Fixes`, `## Under the hood`, `## Breaking / Migration`. Tight, user-facing bullets, no emoji on
headings, and a compare link at the end:
`**Full changelog:** https://github.com/Quixomatic/Eglantine/compare/<from-tag>...vX.Y.Z`.
