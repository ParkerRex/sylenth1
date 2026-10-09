# Contributing to Synthia

Synthia is an experiment in recreating the classic Sylenth1 workflow with owned C++/JUCE code. Contributions should improve one concrete behavior, test, or document at a time. Start with [SPEC.md](SPEC.md), [the documentation index](docs/index.md), and [AGENTS.md](AGENTS.md).

Use original or explicitly licensed code, assets, and presets. Credit the reference instrument honestly. Passing Synthia tests or producing a similar screenshot does not establish Sylenth1 audio or pixel equivalence. Do not copy vendor logos, screenshot backplates, proprietary factory presets, or algorithms extracted from binaries into the product. Read [the conformance record](docs/CLASSIC_PARITY.md) before making fidelity claims.

## Get the source

Fork [ParkerRex/synthia](https://github.com/ParkerRex/synthia) on GitHub, then replace `YOUR_USERNAME` below with your account:

```sh
git clone https://github.com/YOUR_USERNAME/synthia.git
cd synthia
git remote add upstream https://github.com/ParkerRex/synthia.git
git fetch upstream --prune
git switch -c codex/describe-your-change upstream/master
```

The upstream default branch is currently `master`. `origin` is your fork and `upstream` is Parker's repository. Use a precise branch name such as `codex/fix-program-reset`. Preserve any existing uncommitted work before switching branches or rebasing.

## Build and verify your change

Native development uses macOS, Xcode Command Line Tools, CMake 3.24 or newer, a C++20 compiler, and Python 3. CMake fetches pinned JUCE 8.0.13; pass `-DSYNTHIA_JUCE_PATH=/absolute/path/to/JUCE` to use a local checkout.

```sh
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j 6
ctest --test-dir build -C Debug --output-on-failure
```

Define the observable result first, then choose an existing check that could disprove it. [VALIDATION.md](docs/VALIDATION.md) maps behavior to tests and renders. Add a focused regression test when it protects an audible bug, persistence contract, MIDI ownership, or another consequential failure. Avoid tests that only repeat implementation details or prove unchanged deletions.

Before submitting code changes, run the default quality gate:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=6 scripts/check-quality.sh
```

For documentation-only changes, check the edited links and command paths and run `git diff --check`. A build is unnecessary when executable behavior did not change. Keep build outputs, generated reports, and WAVs under `build/`.

UI changes need inspected captures of the affected states; use [the screenshot guide](docs/SCREENSHOTS.md). Plugin-facing changes also need current Ableton AU/VST3 checks described in [VALIDATION.md](docs/VALIDATION.md). Record the tested binary, host environment, and missing checks. If you cannot run a host check, say so in the PR. Historical evidence and unit tests do not replace that check.

## Commit and prepare the PR

Update the relevant durable document when user-visible behavior changes. Keep unrelated refactors, formatting, and dependency updates out of the PR.

Review and commit only the files you intended to change:

```sh
git status -sb
git diff --check
git diff
# Stage your specific changed files, for example:
git add src/dsp/Arpeggiator.cpp tests/smoke/SynthDspCoreTest.cpp docs/VALIDATION.md
git commit -m "fix: release held notes when arp stops"
```

The paths above illustrate a DSP fix; stage the files relevant to your own change. Use a short conventional commit and PR title that names the behavior.

Fetch and rebase onto the latest upstream default branch before submitting:

```sh
git fetch upstream --prune
git rebase upstream/master
```

Resolve conflicts carefully and preserve other contributors' changes. After rebasing, repeat the checks affected by the change and record the tested revision with `git rev-parse HEAD`. Read every line of the final diff and PR description before pushing your branch to your fork:

```sh
git diff --check upstream/master...HEAD
git diff upstream/master...HEAD
git push -u origin HEAD
```

If you already pushed this branch and the rebase changed its history, use `git push --force-with-lease origin HEAD` only for your own contribution branch. Never force-push the upstream default branch.

## Open a pull request

Open a real PR, not a draft, against `ParkerRex/synthia`'s `master` branch. Use [GitHub's compare page](https://github.com/ParkerRex/synthia/compare) and select your fork and branch, or use the GitHub CLI:

```sh
gh pr create --repo ParkerRex/synthia --base master --head YOUR_USERNAME:YOUR_BRANCH --web
```

Replace both placeholders; `YOUR_BRANCH` is the branch you pushed, including its `codex/` prefix. The command opens GitHub's creation form. Submit it as a normal pull request.

Use the [PR template](.github/PULL_REQUEST_TEMPLATE.md). Keep the description short and concrete:

- **Summary:** describe the problem, resulting behavior, cause, and relevant source location. For bug fixes, give repeatable reproduction steps against a named baseline commit. Claim baseline failure only when you observed it; otherwise state the missing evidence.
- **Test plan:** list the exact commands run and their results, with pass counts when available. Name the tested revision, environment, failures, skips, and material limits. Use checked boxes only for verification you completed. Include inspected screenshots for UI changes and host evidence for plugin-facing changes.
- **Performance:** when relevant, include measured before/after results, workload/build/host conditions, and tradeoffs. Do not label estimates as measurements.
- **Disclosure:** end with a brief accurate account of AI model and harness assistance. If none was used, say so. Do not include private maintainer research or profiles.

Respond to review findings by checking the source and reproducing the issue where possible. Fix real problems; explain why a finding is false when evidence supports that conclusion. Maintainers decide when a PR is ready to merge. Agents must not merge without permission.
