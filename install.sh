#!/usr/bin/env bash
set -e

REPO_URL="https://github.com/adityapaul26/mythSh.git"
INSTALL_DIR="$HOME/.local/bin"
TEMP_DIR="$(mktemp -d)"

echo "🚀 Installing MythSH..."

# Check dependencies
for cmd in git gcc make; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "❌ Missing dependency: $cmd"
        echo "Please install it using your package manager."
        exit 1
    fi
done

# Clone repo
echo "📦 Cloning repository..."
git clone --depth=1 "$REPO_URL" "$TEMP_DIR/mythsh" >/dev/null 2>&1

cd "$TEMP_DIR/mythsh"

# Build (assuming a Makefile)
if [ -f Makefile ]; then
    echo "⚙️ Building MythSH..."
    make >/dev/null
else
    echo "⚠️ No Makefile found — skipping build step."
fi

# Install
mkdir -p "$INSTALL_DIR"
if [ -f mythsh ]; then
    cp mythsh "$INSTALL_DIR/"
else
    echo "❌ No binary found. Build failed?"
    exit 1
fi

# Add to PATH (if needed)
if ! echo "$PATH" | grep -q "$HOME/.local/bin"; then
    SHELL_RC="$HOME/.bashrc"
    [ -f "$HOME/.zshrc" ] && SHELL_RC="$HOME/.zshrc"
    echo "export PATH=\"\$HOME/.local/bin:\$PATH\"" >> "$SHELL_RC"
    echo "✅ Added ~/.local/bin to PATH in $SHELL_RC"
fi

echo "✅ MythSH installed successfully!"
echo "👉 Restart your terminal or run: source ~/.bashrc"
echo "   Then type: mythsh"

# Cleanup
rm -rf "$TEMP_DIR"
