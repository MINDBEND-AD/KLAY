Fixture trees for `tools/check-licenses.ps1` (C-006). `clean` must pass; every other folder must fail with the
violation its name describes. The real-tree check excludes this folder.

`short-path-root` is a clean tree with a folder name longer than 8.3; `tests/scripts/check-licenses-short-root.ps1`
passes it to the check as its 8.3 short path. `other-extensions`, `licence-mismatch` and `prefix-dep` cover the
rules added after the M0 review.
