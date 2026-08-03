# History rewrite: webhook endpoint scrubbed from all of git history

Date: 2026-08-03 11:20

`git-filter-repo --replace-text --replace-message` rewrote `refs/heads/main`
(225 commits) replacing the real webhook hostname with
`telegraf.example.com` in every blob and commit message, following the
security-gate advisory (unauthenticated ingest endpoint documented in-repo).
The `v5-firmware` tag predates the string and was verified clean unrewritten.

## The provenance consequence, handled

This project stamps the build commit into firmware (`fw_commit`, verbose
schema 3) and names release images by it. Rewriting history CHANGES every
commit id, so ids recorded on the wire, in captures, and in older docs no
longer exist under their old names. The complete old->new map is kept OFFLINE alongside the pre-rewrite backup
bundle -- deliberately NOT in this repo: the old full shas in it are exactly
the handles that can still fetch orphaned pre-rewrite objects from a remote
until garbage collection. The two ids that matter (short form, which remotes
refuse to resolve for orphaned objects):

| where it lives | old (on the wire / in filenames) | new (in this history) |
|---|---|---|
| deployed firmware (gisebo-05, flashed 2026-08-02) | `95232b` | `2e66d1f` |
| item-32 image built 2026-08-03, not yet flashed | `1912e4` | `292df3b` |

DEV-era captures containing `fw_commit: "95232b"` therefore refer to the
commit now named `2e66d1f`. The PROD unit does not transmit `fw_commit`
(verbose is DEV-only), so nothing in current field operation is affected.
The not-yet-flashed item-32 image was REBUILT after the rewrite so its
embedded hash names a commit that exists; the stale binary was deleted.
Older commit ids quoted in historical dev-notes resolve through the map.

## Residual exposure, stated

A force-push replaces the branch on GitHub, but GitHub retains orphaned
commits fetchable by hash until garbage collection (contact GitHub support
to expedite), and any existing clone/fork keeps the old history. The
scrubbed value is a hostname (not a credential); the defence that matters
is authenticating the ingest endpoint itself.
