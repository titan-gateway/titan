#!/bin/bash
set -e
cd /Users/jonathanberhe/github.com/titan/website/docs

# Copy existing docs from root
if [ -f "../../docs/BUILD_GUIDE.md" ]; then
    cp "../../docs/BUILD_GUIDE.md" "getting-started/building-from-source.md"
    sed -i '' '1s/^/---\nsidebar_position: 3\ntitle: Building from Source\n---\n\n/' "getting-started/building-from-source.md"
    echo "✅ Migrated BUILD_GUIDE.md"
fi

if [ -f "../../docs/DEPLOYMENT.md" ]; then
    cp "../../docs/DEPLOYMENT.md" "deployment/overview.md"
    sed -i '' '1s/^/---\nsidebar_position: 1\ntitle: Deployment Overview\n---\n\n/' "deployment/overview.md"
    echo "✅ Migrated DEPLOYMENT.md"
fi

if [ -f "../../docs/CI_CD.md" ]; then
    cp "../../docs/CI_CD.md" "deployment/ci-cd.md"
    sed -i '' '1s/^/---\nsidebar_position: 5\ntitle: CI\/CD Pipeline\n---\n\n/' "deployment/ci-cd.md"
    echo "✅ Migrated CI_CD.md"
fi

if [ -f "../../ROADMAP.md" ]; then
    cp "../../ROADMAP.md" "contributing/roadmap.md"
    sed -i '' '1s/^/---\nsidebar_position: 2\ntitle: Roadmap\n---\n\n/' "contributing/roadmap.md"
    echo "✅ Migrated ROADMAP.md"
fi

echo "✅ All documentation migrated!"
