#!/bin/bash

set -euo pipefail xtrace

if [[ -n $(git status --porcelain | grep -v VERSION) ]]; then
    echo "❌ There are other un-staged changes to the repository besides VERSION"
    exit 1
fi

VERSION="$(cat VERSION)"

echo "Publishing version v$VERSION..."

make version
git add --all
git commit -m "v$VERSION"
git tag v$VERSION
git push origin main v$VERSION

if grep -qE "alpha|beta" VERSION; then
    gh release create v$VERSION --title=v$VERSION --prerelease --notes=""
else
    gh release create v$VERSION --title=v$VERSION
fi


echo "✅ Published! version v$VERSION"
