#!/bin/bash
# Setup Docusaurus website for Titan documentation
# This script initializes a new Docusaurus project in the website/ directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WEBSITE_DIR="$PROJECT_ROOT/website"

echo "=========================================="
echo "Titan Documentation Site Setup"
echo "=========================================="
echo ""

# Check if Node.js is installed
if ! command -v node &> /dev/null; then
    echo "❌ Node.js is not installed"
    echo "   Please install Node.js 18+ from https://nodejs.org/"
    exit 1
fi

NODE_VERSION=$(node -v | cut -d'v' -f2 | cut -d'.' -f1)
if [ "$NODE_VERSION" -lt 18 ]; then
    echo "❌ Node.js version is too old ($NODE_VERSION)"
    echo "   Please upgrade to Node.js 18 or higher"
    exit 1
fi

echo "✅ Node.js $(node -v) detected"
echo ""

# Check if website directory already exists
if [ -d "$WEBSITE_DIR" ]; then
    echo "⚠️  Website directory already exists at: $WEBSITE_DIR"
    read -p "   Remove and recreate? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "$WEBSITE_DIR"
        echo "   Removed existing website directory"
    else
        echo "   Aborting setup"
        exit 0
    fi
fi

echo "📦 Creating new Docusaurus site..."
echo ""

# Create website directory
cd "$PROJECT_ROOT"
npx create-docusaurus@latest website classic --typescript

echo ""
echo "✅ Docusaurus site created"
echo ""

# Customize docusaurus.config.ts
echo "⚙️  Configuring Docusaurus..."

cd "$WEBSITE_DIR"

# Create custom config
cat > docusaurus.config.ts <<'EOF'
import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Titan API Gateway',
  tagline: 'The Fastest API Gateway Written in C++23',
  favicon: 'img/favicon.ico',

  url: 'https://JonathanBerhe.github.io',
  baseUrl: '/titan/',

  organizationName: 'JonathanBerhe',
  projectName: 'titan',

  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          editUrl: 'https://github.com/JonathanBerhe/titan/tree/main/website/',
          showLastUpdateAuthor: true,
          showLastUpdateTime: true,
          versions: {
            current: {
              label: 'Next 🚧',
            },
          },
        },
        blog: {
          showReadingTime: true,
          editUrl: 'https://github.com/JonathanBerhe/titan/tree/main/website/',
          blogTitle: 'Titan Blog',
          blogDescription: 'Technical insights into high-performance API gateway development',
          postsPerPage: 10,
          blogSidebarTitle: 'Recent posts',
          blogSidebarCount: 5,
        },
        theme: {
          customCss: './src/css/custom.css',
        },
        gtag: {
          trackingID: 'G-XXXXXXXXXX',  // Replace with your GA4 Measurement ID
          anonymizeIP: true,  // Optional: anonymize IPs for privacy
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/titan-social-card.jpg',
    navbar: {
      title: 'Titan',
      logo: {
        alt: 'Titan Logo',
        src: 'img/logo.svg',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'tutorialSidebar',
          position: 'left',
          label: 'Docs',
        },
        {to: '/blog', label: 'Blog', position: 'left'},
        {to: '/benchmarks', label: 'Benchmarks', position: 'left'},
        {
          type: 'docsVersionDropdown',
          position: 'right',
        },
        {
          href: 'https://github.com/JonathanBerhe/titan',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Documentation',
          items: [
            {
              label: 'Getting Started',
              to: '/docs/getting-started',
            },
            {
              label: 'Configuration',
              to: '/docs/configuration',
            },
            {
              label: 'Deployment',
              to: '/docs/deployment',
            },
          ],
        },
        {
          title: 'Community',
          items: [
            {
              label: 'GitHub Discussions',
              href: 'https://github.com/JonathanBerhe/titan/discussions',
            },
            {
              label: 'Issues',
              href: 'https://github.com/JonathanBerhe/titan/issues',
            },
          ],
        },
        {
          title: 'More',
          items: [
            {
              label: 'Blog',
              to: '/blog',
            },
            {
              label: 'GitHub',
              href: 'https://github.com/JonathanBerhe/titan',
            },
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} Titan Contributors. Built with Docusaurus.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['bash', 'json', 'cpp'],
    },
    algolia: {
      // The application ID provided by Algolia
      appId: 'YOUR_APP_ID',
      // Public API key: safe to commit
      apiKey: 'YOUR_SEARCH_API_KEY',
      indexName: 'titan',
      // Optional: see doc section below
      contextualSearch: true,
    },
    announcementBar: {
      id: 'star_us',
      content:
        '⭐️ If you like Titan, give it a star on <a target="_blank" rel="noopener noreferrer" href="https://github.com/JonathanBerhe/titan">GitHub</a>! ⭐️',
      backgroundColor: '#fafbfc',
      textColor: '#091E42',
      isCloseable: true,
    },
  } satisfies Preset.ThemeConfig,

  // Analytics options (uncomment one):

  // Option 1: Umami (self-hosted, recommended)
  // scripts: [{
  //   src: 'https://analytics.yourdomain.com/script.js',
  //   defer: true,
  //   'data-website-id': 'YOUR_WEBSITE_ID'
  // }],

  // Option 2: GoatCounter (free tier)
  // scripts: [{
  //   'data-goatcounter': 'https://titan.goatcounter.com/count',
  //   src: '//gc.zgo.at/count.js',
  //   async: true
  // }],

  // Option 3: PostHog (advanced analytics)
  // scripts: [{
  //   src: 'https://app.posthog.com/static/array.js',
  //   async: true
  // }],

  // Option 4: Google Analytics (not recommended for privacy)
  // Already configured above in gtag section
};

export default config;
EOF

echo "✅ Configuration updated"

# Create initial documentation structure
echo "📝 Creating documentation structure..."

mkdir -p docs/getting-started
mkdir -p docs/architecture
mkdir -p docs/configuration
mkdir -p docs/deployment
mkdir -p docs/benchmarks

# Create intro page
cat > docs/intro.md <<'EOF'
---
sidebar_position: 1
slug: /
---

# Introduction

**Titan** is a next-generation, high-performance API Gateway written in C++23.

## Why Titan?

- ⚡ **Ultra-Fast**: 190,000 req/s throughput, <1ms P99 latency
- 🔒 **Modern Protocols**: HTTP/2, TLS 1.3, ALPN
- 🧵 **Thread-Per-Core**: Zero-contention architecture
- 💾 **SIMD-Optimized**: Vectorized routing and parsing
- 🔌 **Connection Pooling**: Efficient backend connection reuse
- 📊 **Observable**: Prometheus metrics, structured logging
- 🎯 **Kubernetes-Ready**: Helm charts, health checks, graceful shutdown

## Quick Start

Get started with Titan in under 5 minutes:

```bash
# Install
curl -fsSL https://titan.dev/install.sh | bash

# Create config
cat > config.json <<EOF
{
  "server": {"port": 8080},
  "upstreams": [{
    "name": "api",
    "backends": [{"host": "localhost", "port": 3000}]
  }],
  "routes": [{"path": "/*", "upstream": "api"}]
}
EOF

# Run
titan --config config.json
```

## Next Steps

- 📚 [Getting Started Guide](./getting-started/installation)
- 🏗️ [Architecture Overview](./architecture/thread-per-core)
- ⚙️ [Configuration Reference](./configuration/schema)
- 🚀 [Deployment Guide](./deployment/docker)
EOF

# Create Getting Started guide
cat > docs/getting-started/installation.md <<'EOF'
---
sidebar_position: 1
---

# Installation

Install Titan on your system using one of the following methods.

## Binary Installation (Recommended)

The easiest way to install Titan is using our auto-detect installer:

```bash
curl -fsSL https://titan.dev/install.sh | bash
```

This script will:
1. Detect your CPU architecture
2. Download the optimal binary variant
3. Install to `/usr/local/bin/titan`
4. Verify the installation

## Docker

Pull the official Docker image:

```bash
docker pull ghcr.io/JonathanBerhe/titan:latest
```

Run Titan in a container:

```bash
docker run -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json \
  ghcr.io/JonathanBerhe/titan:latest \
  --config /etc/titan/config.json
```

## Kubernetes (Helm)

Install using Helm:

```bash
helm install titan oci://ghcr.io/JonathanBerhe/charts/titan \
  --namespace titan \
  --create-namespace
```

## Build from Source

Requirements:
- Clang 18+
- CMake 3.28+
- vcpkg

```bash
git clone https://github.com/JonathanBerhe/titan.git
cd titan
cmake --preset=release
cmake --build --preset=release
sudo install -m 755 build/release/src/titan /usr/local/bin/
```

## Verify Installation

```bash
titan --version
# Titan API Gateway v0.1.0
```

## Next Steps

- [Quick Start](./quickstart) - Create your first proxy
- [Configuration](../configuration/schema) - Learn about config options
EOF

echo "✅ Documentation structure created"

# Create first blog post
echo "📰 Creating first blog post..."

mkdir -p blog
cat > blog/2025-01-01-introducing-titan.md <<'EOF'
---
slug: introducing-titan
title: Introducing Titan - The Fastest API Gateway
authors: [titan-team]
tags: [announcement, performance, c++]
---

We're excited to introduce **Titan** - a next-generation API Gateway built for extreme performance.

<!-- truncate -->

## Why We Built Titan

Modern applications need API gateways that can handle massive traffic while maintaining low latency. Existing solutions like Nginx and Envoy are excellent, but we wanted to push the boundaries even further.

## Key Features

- **190,000 req/s** throughput (vs Nginx's ~120k)
- **<1ms P99 latency** under load
- **HTTP/2 multiplexing** with full TLS 1.3 support
- **Thread-per-core** architecture for zero contention
- **SIMD-optimized** routing and parsing

## Benchmarks

![Titan vs Competitors](./benchmarks.png)

Our benchmarks show Titan outperforming Nginx by 63% in throughput while maintaining lower latency.

## Get Started

Try Titan today:

```bash
curl -fsSL https://titan.dev/install.sh | bash
```

Check out our [Getting Started Guide](/docs/getting-started) to learn more.
EOF

echo "✅ Blog post created"

# Update package.json with useful scripts
echo "📦 Adding npm scripts..."

npx json -I -f package.json -e 'this.scripts.format="prettier --write \"**/*.{ts,tsx,md,mdx,json}\""'
npx json -I -f package.json -e 'this.scripts.lint="eslint src --ext ts,tsx"'

echo "✅ Scripts added"

echo ""
echo "=========================================="
echo "✅ Docusaurus Setup Complete!"
echo "=========================================="
echo ""
echo "Website location: $WEBSITE_DIR"
echo ""
echo "Next steps:"
echo ""
echo "  1. Setup Google Analytics:"
echo "     - Go to https://analytics.google.com/"
echo "     - Create property and get Measurement ID (G-XXXXXXXXXX)"
echo "     - See docs/GOOGLE_ANALYTICS_SETUP.md for detailed guide"
echo ""
echo "  2. Update configuration:"
echo "     - Edit website/docusaurus.config.ts"
echo "     - Replace 'JonathanBerhe' with your GitHub org"
echo "     - Add your GA4 Measurement ID (replace G-XXXXXXXXXX)"
echo ""
echo "  3. Start development server:"
echo "     cd website"
echo "     npm start"
echo ""
echo "  4. Build for production:"
echo "     npm run build"
echo ""
echo "  5. Deploy to GitHub Pages:"
echo "     npm run deploy"
echo ""
echo "  6. Migrate existing documentation:"
echo "     - Copy markdown files from docs/ to website/docs/"
echo "     - Update frontmatter and links"
echo ""
echo "📚 Documentation: https://docusaurus.io/docs"
echo "🎨 Themes: https://docusaurus.io/showcase"
echo ""
