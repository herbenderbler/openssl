# find-doc-nits: Check env vars in .t/.pl files (issue #28110)

## Problem

The `find-doc-nits` script’s `-a` option (list undocumented environment variables) only scanned **C** source (`.c` and `.in`) and explicitly **excluded** the `test/` directory. Environment variables used in **Perl** test and utility code (`.t` and `.pl` files) were therefore never checked, so undocumented variables in those files could go unnoticed. The goal was to extend the check to `.t` and `.pl` so we cover as many places as possible and catch undocumented env vars there too.

## Solution

### Implementation details and reasons

1. **Include `.t` and `.pl` in the scanned file set**  
   The file list is built via `git ls-files` (and fallback `find`) with a pattern; it was extended from `/\.c$|\.in$/` to `/\.(?:c|in|t|pl)$/` so Perl test and script files are collected. This addresses both the issue description and reviewer feedback (jogme: “Could you please also add *.t files?”).

2. **Stop excluding `test/` for env-var collection**  
   `"$config{sourcedir}/test"` was removed from `@except_dirs`. We still exclude `demos/`. Without this change, `test/**/*.t` and `test/**/*.pl` would remain skipped even after adding the new extensions.

3. **Single improved `$ENV{...}` regex (no redundant block)**  
   Levitte’s feedback on PR #28211 was that a second, Perl-only `$ENV{...}` block was redundant with the existing one; the existing pattern should be improved instead. The pattern was changed from `/\$ENV\{([^}"']+)\}/` to `/\$ENV\{['"]?([^}'"]+)['"]?\}/` so one pattern handles:
   - `$ENV{NAME}`
   - `$ENV{'NAME'}`
   - `$ENV{"NAME"}`
   for both `.in` and Perl (`.t`/`.pl`) files. No duplicate logic.

4. **Capture all occurrences per line**  
   The script used a single match per line, so only the first `$ENV{...}` on a line was seen (e.g. `$ENV{A} || $ENV{B}` only recorded A). It was changed to a **`while` loop with `/g`** so every `$ENV{...}` on the line is recorded.

5. **Skip Perl specials**  
   Code like `return $ENV{$1} if $val =~ /.../` was incorrectly reported as an env var named `$1`. We only push to the env list when the captured name does not look like a Perl special: `unless $1 =~ /^\$/`.

6. **Run C-only patterns only for C/in files**  
   For `.t` and `.pl` files, patterns for `getenv(...)` and the C ternary `env(...)?...:...` never match. They were wrapped in `if ($filename !~ /\.(?:t|pl)$/) { ... }` so we don’t run those regexes on every line of Perl files (reduces redundant work and clarifies intent).

## Tests

- **New regression test:** `test/recipes/80-test_find_doc_nits.t`  
  It runs `find-doc-nits -a` from the build directory (so `configdata` is available) and asserts that the output contains **ECSTRESS**. That variable is only referenced in `test/recipes/99-test_ecstress.t` and is not in `openssl-env.pod`, so it appears in the undocumented list as long as `.t` (and `.pl`) files are scanned. The test therefore guards against regressions that would stop scanning Perl/test files.

- Run with the rest of the 80- recipes, e.g. `perl test/run_tests.pl 80`.

## Documentation

- **Help text** (`util/find-doc-nits`): The `-a` option line was updated from “List undocumented env vars” to “List undocumented env vars (scans .c, .in, .t, and .pl files)” so users know which file types are included.

- **HOWTO** (`doc/HOWTO/documenting-functions-and-macros.md`): A short paragraph was added after the HISTORY section: the `-a` option checks for undocumented environment variables by scanning C (.c, .in) and Perl (.t, .pl) source files, and any variables found must be documented in the appropriate manual (e.g. L<openssl-env(7)>).

## Files changed

| File | Change |
|------|--------|
| `util/find-doc-nits` | Extend file pattern to `.t` and `.pl`; remove `test/` from `@except_dirs`; improve single `$ENV{...}` regex to handle quoted keys and use `/g` loop; skip captures that look like Perl `$1`; run getenv/ternary patterns only for non–Perl files. Update help for `-a`. |
| `doc/HOWTO/documenting-functions-and-macros.md` | Document that `-a` scans .c, .in, .t, and .pl for undocumented env vars and references openssl-env(7). |
| `test/recipes/80-test_find_doc_nits.t` | New recipe: run `find-doc-nits -a` and assert output contains ECSTRESS (regression test for .t/.pl scanning). |

## Note

With this change, `make doc-nits` will report many additional undocumented environment variables (those used only in test/build/tooling code). Handling those (e.g. documenting test-only vars or introducing an allowlist) is left to a follow-up; it is outside the scope of issue #28110, which is only to extend the check to .t/.pl files.

Fixes #28110
