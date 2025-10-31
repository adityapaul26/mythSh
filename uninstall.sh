#!/usr/bin/env bash
set -e

echo "🧹 Uninstalling MythSh...byee"
rm -f "$HOME/.local/bin/mythsh"
sed -i '/\.local\/bin/d' "$HOME/.bashrc" 2>/dev/null || true
sed -i '/\.local\/bin/d' "$HOME/.zshrc" 2>/dev/null || true
echo "✅ MythSH has been uninstalled successfully!"

