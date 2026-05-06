---
name: changelog-update
description: Generate or update CHANGELOG.md by mining git tags, GitHub PRs, and closed issues. Use when the user asks to "create a changelog", "update CHANGELOG.md", "regenerate the changelog", or wants release notes derived from PR/issue history. Produces a Keep-a-Changelog-style file that is PR-centric, attributes contributors with @mentions, and skips housekeeping commits.
---

# changelog-update

## Purpose

Build or extend a CHANGELOG.md that is brief, accurate, PR-centric, and
linked to the GitHub issues each PR closes. Output is meant for end-users
of the project, not commit archaeology.

## Triggers

- "Create / generate / write a CHANGELOG.md"
- "Update the changelog for the upcoming release"
- "Add release notes for vX.Y.Z"
- "Backfill the changelog from git history"

## Conventions

These are the rules baked into this skill. Apply them every time.

### Format

- File name: `CHANGELOG.md` at repo root.
- Top-level header: `# Changelog`, then a short prose blurb naming the
  format (Keep a Changelog) and the versioning scheme (SemVer).
- One **H2** per version, in reverse chronological order.
  - Released: `## [X.Y.Z] - YYYY-MM-DD` (ISO date, hyphen separator, ASCII).
  - Unreleased work: `## [Unreleased]`.
  - Untagged early history: `## Pre-tag history` (or similar). **Never invent
    a fake `[0.1.0]` tag** and never put a fabricated date next to a heading
    that has no real release.
- Optional one-paragraph milestone summary directly under the H2 (e.g.
  "First public SALT-FM milestone...").
- **Single bullet list under each H2.** Do not split into
  Added/Changed/Fixed subsections; keep one flat list per version.
- Link references for `[Unreleased]` / `[X.Y.Z]` at the bottom of the file
  using GitHub `compare/...` URLs. Drop the link ref for any retroactive
  section that has no underlying tag.

### Bullet structure (PR-centric)

- Default: **one top-level bullet per merged or open PR** that delivered
  user-visible change. Format: short imperative summary, then the
  citation: `([#NN](https://github.com/<owner>/<repo>/pull/NN) by @author)`.
- **Themed-split exception:** if a single PR delivers two or more clearly
  distinct user-facing themes (e.g. "LLVM 22 support" + "version derivation
  from `.VERSION`"), split into separate top-level bullets, each citing the
  same PR. Do not split when the parts are facets of one feature.
- If the PR closes one issue, mention the issue inline or in the citation.
- If the PR closes **multiple** issues, indent each issue under the PR as
  a sub-bullet:

  ```markdown
  - <PR-level summary>
    ([#NN](.../pull/NN) by @author):
    - [#A](.../issues/A) - <abbreviated description of what that issue was>
    - [#B](.../issues/B) - <abbreviated description>
  ```

  The sub-bullet description must be human-readable, **not just the issue
  number**. Pull a short rephrasing from the issue title or PR body.
- Credit reporters as well as authors when relevant:
  `[#44](.../issues/44) (reported by @nchaimov) - ...`.

### What to include

- Genuine bug fixes.
- New features and capabilities.
- New supported toolchain versions (LLVM, Clang, GCC, CMake, Python, etc.).
- Build-system milestones that affect users (e.g. switching to CMake,
  raising the CMake minimum, version derivation scheme).
- Public API additions, removals, or behavior changes.
- Test-coverage improvements that close a documented gap (cite the issue).

### What to skip

- Pure formatting / whitespace / lint commits.
- CI dependency bumps that do not change supported versions
  (e.g. `actions/checkout v3 -> v6`).
- "Address Copilot review" / "address PR feedback" commits.
- Comment-only edits, README typo fixes, internal doc tweaks.
- Reverts that are part of an in-progress PR (only the net effect lands).
- Commits that exist purely to fix earlier commits in the same un-merged
  branch.

### Style

- ASCII only. No em-dash, en-dash, smart quotes, ellipsis character.
  Use `-`, `--`, straight quotes, and `...`.
- No trailing whitespace.
- Wrap prose at ~78 cols when practical, but do not break inline links.
- Use backticks for filenames, flags, identifiers, env vars, version
  strings (`v0.3.0`).

## Workflow

### 1. Survey the repo

```sh
git tag --sort=-creatordate
git for-each-ref --sort=-creatordate \
    --format='%(refname:short) %(creatordate:iso) %(subject)' refs/tags
git log --first-parent --pretty=format:"%h %ad %an %s" --date=short | head -100
```

Identify:
- Latest tag (target for `[Unreleased]` lower bound).
- Previous tags (anchor each `## [X.Y.Z]` section).
- Whether a `## Pre-tag history` section is needed for commits older than
  the earliest tag.

### 2. Pull PR + issue metadata

```sh
gh pr list --state merged --limit 200 \
    --json number,title,author,mergedAt,closingIssuesReferences \
    --jq '.[] | "\(.mergedAt[0:10]) #\(.number) @\(.author.login) :: \(.title)"'

gh pr list --state open --limit 50 \
    --json number,title,author \
    --jq '.[] | "OPEN #\(.number) @\(.author.login) :: \(.title)"'
```

For each candidate PR (open and merged):

```sh
gh pr view <N> --json number,title,author,body,closingIssuesReferences \
    --jq '{n: .number, t: .title, a: .author.login,
           closes: [.closingIssuesReferences[].number],
           body: .body[0:1500]}'
```

For closed-but-not-PR-linked issues you still want to credit:

```sh
gh issue view <N> --json number,title,author --jq '"#\(.number) @\(.author.login): \(.title)"'
```

### 3. Bucket commits by version

For each pair of adjacent tags, list the user-visible changes:

```sh
git log <prev-tag>..<this-tag> --pretty=format:"%h %ad %an %s" \
    --date=short --no-merges
```

For HEAD beyond the latest tag (Unreleased):

```sh
git log <latest-tag>..HEAD --pretty=format:"%h %ad %an %s" \
    --date=short --no-merges
```

For pre-tag history, list everything reachable from the earliest tag:

```sh
git log <earliest-tag> --pretty=format:"%h %ad %an %s" --date=short --no-merges
```

Cross-reference each commit against the merged-PR list. Group commits
that belong to the same PR (squash merges already collapse this; merge
commits do not). Drop commits that fall under "What to skip".

### 4. Draft entries

Apply the bullet structure rules above. For each PR:

- Summarize the user-facing impact in one sentence (imperative voice).
- Embed the PR citation.
- If `closingIssuesReferences` returned multiple issues, write a nested
  sub-bullet per issue with an abbreviated description. Pull descriptions
  from the issue title; rephrase if the title is jargony.
- Mention reporters when distinct from the PR author.

### 5. Verify

After writing the file, run:

```sh
grep -nP '[^\x00-\x7F]' CHANGELOG.md || echo "ASCII clean"
grep -nE ' +$' CHANGELOG.md || echo "no trailing whitespace"
```

Open in a renderer (`grip CHANGELOG.md` / `gh markdown-preview`) only if
the user wants visual confirmation.

### 6. Append link references

At the bottom of the file:

```markdown
[Unreleased]: https://github.com/<owner>/<repo>/compare/<latest-tag>...HEAD
[X.Y.Z]: https://github.com/<owner>/<repo>/compare/<prev-tag>...<this-tag>
[<earliest-tag>]: https://github.com/<owner>/<repo>/releases/tag/<earliest-tag>
```

Skip link refs for `## Pre-tag history` and any other untagged section.

## Updating an existing CHANGELOG

When adding a new release section between `[Unreleased]` and the previous
top entry:

1. Promote the `[Unreleased]` body into a new `## [X.Y.Z] - YYYY-MM-DD`
   section. Do not rewrite the bullets; just relabel and stamp the date.
2. Empty the `[Unreleased]` heading (leave it in place for future work).
3. Add a new `[Unreleased]` link ref pointing at `compare/vX.Y.Z...HEAD`
   and update the previous link ref to `compare/<prev>...vX.Y.Z`.

When the user asks to "add an entry for PR #N":

1. `gh pr view N` to fetch metadata + closing issues.
2. Insert under `## [Unreleased]` in the appropriate position
   (chronological by merge date is fine).
3. Apply the bullet structure rules. Do not duplicate an existing entry.

## In-PR changelog upkeep

Preferred workflow on active projects: each PR that ships a user-visible
change updates `CHANGELOG.md` as part of its own diff, so the entry lands
together with the code instead of being backfilled at release time.

### While the PR is in progress

1. On the feature branch, edit `## [Unreleased]` and add the new bullet(s)
   following the structure rules above. Keep the entry inside the same
   commit topic (a `docs(changelog):` commit is fine; squashing it into a
   feature commit is also fine).
2. If the PR has not been opened yet (or its number is not stable),
   draft the citation as `([#TBD](.../pull/TBD) by @author)`. Replace
   `TBD` with the real number once the PR exists.
3. If the PR closes issues, list them as sub-bullets exactly as you would
   for a finalized entry. The closing list may grow as work expands;
   re-run the pre-merge audit (below) before merging.

### Pre-merge audit

Run this just before squash-merging or rebasing the PR:

1. `gh pr view <N> --json closingIssuesReferences` to confirm the final
   list of closing issues. Update sub-bullets if any were added or
   dropped.
2. Verify each top-level bullet still describes a coherent user-visible
   theme (apply the themed-split rule if scope grew).
3. Confirm the PR number(s) and `@author` mentions resolve. Replace any
   leftover `TBD` placeholders.
4. Re-run the verification checks (ASCII, trailing whitespace).
5. Confirm `[Unreleased]` link ref still exists at the bottom of the file.

If a release is being cut at the same time, follow the
"Updating an existing CHANGELOG" steps above *after* the PR is merged,
not as part of the PR.

### Merge conflicts on `CHANGELOG.md`

When two PRs both append to `## [Unreleased]`, they will conflict on
merge. Resolve by **keeping all entries from both branches** -- never
drop a contributor's bullet to "fix" the conflict. Order entries by
merge date when known, otherwise by PR number ascending. Re-run the
verification checks after resolution.

## Anti-patterns

- Sub-headers like `### Added` / `### Changed` / `### Fixed`. The current
  convention is one flat bullet list per version.
- Bullets that say only `Closes #42` with no description.
- Inventing version tags that do not exist in `git tag`.
- Em-dashes copied from rendered prose ("LLVM 20 - support" using U+2014).
- Listing every commit in a PR. Collapse to one PR-level bullet (or
  themed bullets per the themed-split rule) plus nested issue bullets if
  the PR closes multiple issues.
- Resolving a `CHANGELOG.md` merge conflict by deleting the other
  branch's entries. Always keep both sets and re-order.
- Leaving stale `[#TBD]` placeholder citations after the PR has been
  opened.
- Silently dropping a contributor. If a PR was authored by someone other
  than the maintainer, name them with `@login`.
