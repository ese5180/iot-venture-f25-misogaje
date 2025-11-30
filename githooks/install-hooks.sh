#!/usr/bin/env bash
# =============================================================================
# Git Hooks Installation Script
# Run this from the repository root to install pre-commit and pre-push hooks
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo "")"

if [ -z "$REPO_ROOT" ]; then
    echo "ERROR: Not in a git repository"
    exit 1
fi

GIT_HOOKS_DIR="$REPO_ROOT/.git/hooks"

echo "========================================"
echo "Installing Git Hooks"
echo "========================================"
echo "  Source: $SCRIPT_DIR"
echo "  Target: $GIT_HOOKS_DIR"
echo ""

# Install pre-commit hook
if [ -f "$SCRIPT_DIR/pre-commit" ]; then
    cp "$SCRIPT_DIR/pre-commit" "$GIT_HOOKS_DIR/pre-commit"
    chmod +x "$GIT_HOOKS_DIR/pre-commit"
    echo "✓ Installed pre-commit hook"
else
    echo "⚠ pre-commit hook not found in $SCRIPT_DIR"
fi

# Install pre-push hook
if [ -f "$SCRIPT_DIR/pre-push" ]; then
    cp "$SCRIPT_DIR/pre-push" "$GIT_HOOKS_DIR/pre-push"
    chmod +x "$GIT_HOOKS_DIR/pre-push"
    echo "✓ Installed pre-push hook"
else
    echo "⚠ pre-push hook not found in $SCRIPT_DIR"
fi

echo ""
echo "========================================"
echo "✓ Git hooks installed successfully!"
echo "========================================"
echo ""
echo "Hooks will run automatically:"
echo "  • pre-commit: clang-format + cppcheck on staged files"
echo "  • pre-push:   Docker build + Ztest execution"
echo ""
echo "To bypass hooks (emergency only):"
echo "  git commit --no-verify"
echo "  git push --no-verify"
