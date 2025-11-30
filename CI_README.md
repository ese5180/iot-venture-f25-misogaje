# Continuous Integration (CI) Setup

This directory contains all the CI tooling for the misogate/misonode TBM navigation project.

## Overview

The CI setup includes:

| Component | Purpose | Location |
|-----------|---------|----------|
| Pre-commit hook | Runs clang-format + cppcheck on staged files | `githooks/pre-commit` |
| Pre-push hook | Builds & tests in Docker before push | `githooks/pre-push` |
| GitHub Actions | Cloud-based CI on push/PR to main | `.github/workflows/ci.yml` |
| clang-format config | Zephyr-compatible code style | `.clang-format` |
| Unit tests | Ztest suite for native_sim | `tests/unit_test/` |

## Quick Start

### 1. Install Git Hooks (Local CI)

```bash
# From repository root
cd githooks
chmod +x install-hooks.sh
./install-hooks.sh
```

This installs:
- **pre-commit**: Checks code formatting and runs static analysis on staged files
- **pre-push**: Runs full Docker-based build and tests before pushing

### 2. Install Local Tools (Optional, for pre-commit)

```bash
# Ubuntu/Debian
sudo apt-get install clang-format cppcheck

# macOS
brew install clang-format cppcheck
```

### 3. Enable GitHub Actions

The workflows are automatically enabled when you push `.github/workflows/ci.yml` to your repository. No additional setup needed!

## Directory Structure

```
your-repo/
├── .clang-format              # Code style configuration
├── .github/
│   └── workflows/
│       ├── ci.yml             # Main CI workflow (Zephyr Docker)
│       └── ci-ncs.yml         # Alternative for nRF Connect SDK
├── githooks/
│   ├── pre-commit             # Local pre-commit hook
│   ├── pre-push               # Local pre-push hook
│   └── install-hooks.sh       # Hook installer script
├── tests/
│   └── unit_test/
│       ├── CMakeLists.txt     # Test build configuration
│       ├── prj.conf           # Zephyr test config
│       └── src/
│           ├── main.c         # Test entry point
│           └── test_json_payload.c  # Sample test suite
├── misogate/                  # Gateway application
├── misonode/                  # Node application
└── west.yml                   # West manifest (for CI)
```

## Component Details

### Pre-commit Hook (3.8.2 & 3.8.3)

Runs automatically before each `git commit`:

1. **clang-format check**: Verifies code follows Zephyr coding style
2. **cppcheck**: Static analysis for common C bugs

```bash
# Manual run (for debugging)
./githooks/pre-commit
```

If checks fail, the commit is blocked. Fix issues with:
```bash
# Auto-format a file
clang-format -i path/to/file.c

# Format all staged C files
git diff --cached --name-only | grep -E '\.(c|h)$' | xargs clang-format -i
```

### Pre-push Hook (3.8.2)

Runs automatically before each `git push`:

1. Pulls Zephyr Docker image (if needed)
2. Builds unit tests for `native_sim/native/64`
3. Runs all Ztests
4. Builds misogate for `nrf7002dk/nrf5340/cpuapp`
5. Builds misonode for `nrf7002dk/nrf5340/cpuapp`

```bash
# Manual run (for debugging)
./githooks/pre-push
```

**Requirements**: Docker must be installed and running.

### GitHub Actions (3.8.4)

Triggered on:
- Push to `main` or `master` branch
- Pull requests to `main` or `master`
- Manual dispatch (Actions tab → Run workflow)

Jobs:
| Job | Description | Artifacts |
|-----|-------------|-----------|
| `unit-tests` | Builds & runs Ztests on native_sim | Test logs |
| `build-misogate` | Builds gateway for nRF7002DK | `.hex`, `.bin`, `.elf` |
| `build-misonode` | Builds node for nRF7002DK | `.hex`, `.bin`, `.elf` |
| `static-analysis` | Runs clang-format + cppcheck | Analysis reports |

**View Results**: Go to your GitHub repo → Actions tab

### Unit Tests

The sample test suite demonstrates Ztest patterns. **Replace with your TDD tests!**

```bash
# Build tests locally (in Docker)
docker run --rm -v $(pwd):/workdir -w /workdir \
  ghcr.io/zephyrproject-rtos/zephyr-build:main \
  bash -c "west build -b native_sim/native/64 tests/unit_test --pristine"

# Run tests
docker run --rm -v $(pwd):/workdir -w /workdir \
  ghcr.io/zephyrproject-rtos/zephyr-build:main \
  bash -c "west build -t run tests/unit_test"
```

## Adding Your Own Tests

1. Create test files in `tests/unit_test/src/test_*.c`
2. Use the Ztest API:

```c
#include <zephyr/ztest.h>

ZTEST(my_suite, test_something)
{
    int result = function_under_test();
    zassert_equal(result, expected_value, "Should match");
}

ZTEST_SUITE(my_suite, NULL, NULL, NULL, NULL, NULL);
```

3. Add source files to `tests/unit_test/CMakeLists.txt`
4. Push to trigger CI

## Troubleshooting

### Docker image pull fails
```bash
# Manually pull the image
docker pull ghcr.io/zephyrproject-rtos/zephyr-build:main
```

### Pre-commit: "clang-format not found"
Install clang-format or the hook will skip format checking (with warning).

### GitHub Actions: West init fails
Ensure `west.yml` is in your repo root with correct manifest format.

### Tests pass locally but fail in CI
Check that all dependencies are listed in `prj.conf` and the test doesn't rely on hardware.

## Bypassing Hooks (Emergency Only!)

```bash
git commit --no-verify -m "Emergency fix"
git push --no-verify
```

**Use sparingly!** This defeats the purpose of CI.

## References

- [Zephyr CI Docker Image](https://github.com/zephyrproject-rtos/docker-image)
- [Zephyr Ztest Framework](https://docs.zephyrproject.org/latest/develop/test/ztest.html)
- [clang-format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
