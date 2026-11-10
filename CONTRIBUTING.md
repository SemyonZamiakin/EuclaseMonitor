# Contributing to Euclase Monitor

Thanks for your interest. Bug reports, hardware test reports and focused pull requests are welcome.

## Before you start

- For anything larger than a small fix, open an issue first so the approach can be agreed.
- Keep changes focused: one topic per pull request.

## Build and test

```
./build.ps1        # builds build\EuclaseMonitor.exe and runs all tests
```

Requirements: Windows, MSYS2 mingw64 `g++`. Documentation examples in `docs/CUSTOMIZE*.md` are compiled and run by `tests/test_docs.cpp`, so a changed example must stay valid.

A pull request is expected to keep the build warning-free (`-Wall -Wextra`) and the test suite green. New behaviour needs a test.

If you add a metric, add it to `docs/METRICS.md`; `tests/check_metrics_doc.ps1` verifies that every documented metric exists in the running app.

## Style

- Match the surrounding code: naming, formatting and comment density.
- No new third-party dependencies without discussion.
- Do not commit binaries, build output or secrets.

## Commits and pull requests

- Write the commit subject in the imperative mood ("Fix X", "Add Y"), under about 72 characters, with a short body that explains why.
- Describe in the pull request what you tested and on what hardware or OS.

## Reporting results from your hardware

For hardware-dependent behaviour, the most useful report contains: the exact device model, OS version, what you expected, what happened, and logs where available.

## License

By contributing you agree that your contribution is licensed under the MIT license of this repository.
