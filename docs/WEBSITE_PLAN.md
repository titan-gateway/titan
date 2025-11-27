# Titan Website & Documentation Plan

## 🎯 Technology Decision: Docusaurus

**Chosen:** Docusaurus 3.0 (React-based static site generator)

**Repository Structure:**
```
titan/
├── docs/                  # Markdown documentation
│   ├── getting-started/
│   ├── architecture/
│   ├── configuration/
│   └── deployment/
├── blog/                  # Technical blog posts
├── src/                   # Custom React components
│   ├── components/
│   │   ├── BenchmarkChart.tsx
│   │   ├── ConfigGenerator.tsx
│   │   └── ArchitectureDiagram.tsx
│   └── pages/
│       ├── index.tsx      # Homepage
│       └── benchmarks.tsx
├── static/                # Images, files
├── docusaurus.config.js   # Site configuration
└── sidebars.js           # Documentation sidebar
```

## 📊 Analytics: Google Analytics (Recommended)

### Primary Choice: **Google Analytics 4**

**Why Google Analytics:**
- ✅ **Free** - No cost, no limits
- ✅ **Powerful** - Advanced insights, funnels, cohorts
- ✅ **Easy setup** - Built into Docusaurus
- ✅ **Familiar** - Industry standard
- ✅ **Integration** - Works with Google Search Console

**Setup (2 minutes):**

1. **Create GA4 property:**
   - Go to https://analytics.google.com/
   - Create Account → Create Property
   - Get your Measurement ID (format: `G-XXXXXXXXXX`)

2. **Add to Docusaurus:**
   ```js
   // docusaurus.config.js
   presets: [
     ['classic', {
       gtag: {
         trackingID: 'G-XXXXXXXXXX',
         anonymizeIP: true,  // Optional: anonymize IPs
       },
     }],
   ],
   ```

3. **Done!** Analytics will track automatically.

**What you'll see:**
- Page views & user sessions
- Traffic sources (Google, HN, Reddit)
- User demographics & locations
- Real-time visitors
- Conversion tracking
- Search Console integration

### Alternative: **Umami Analytics** (Self-Hosted, Privacy-First)

**Why Umami:**
- ✅ **Truly open source** - MIT license, completely free
- ✅ **Self-hostable** - One Docker command
- ✅ **Privacy-first** - GDPR compliant, no cookies
- ✅ **Lightweight** - <2KB tracker script
- ✅ **Simple dashboard** - Similar to Plausible
- ✅ **Active development** - Well-maintained
- ✅ **No data limits** - Unlimited events/websites

**Quick Setup (Docker):**
```bash
docker run -d \
  --name umami \
  -p 3000:3000 \
  -e DATABASE_URL=postgresql://... \
  ghcr.io/umami-software/umami:postgresql-latest
```

**Integration:**
```js
// docusaurus.config.js
scripts: [{
  src: 'https://analytics.yourdomain.com/script.js',
  defer: true,
  'data-website-id': 'YOUR_WEBSITE_ID'
}]
```

**Free Hosting Options:**
- Railway.app (free tier)
- Fly.io (free tier)
- Your own VPS ($5/month DigitalOcean)

### Alternative 1: **GoatCounter** (Hosted Free Tier)

**Pros:**
- ✅ Free tier for non-commercial/open source
- ✅ No self-hosting needed
- ✅ Privacy-friendly, GDPR compliant
- ✅ Simple and fast

**Cons:**
- ⚠️ Less features than Umami
- ⚠️ Free tier restrictions (check current limits)

**Integration:**
```html
<script data-goatcounter="https://titan.goatcounter.com/count"
        async src="//gc.zgo.at/count.js"></script>
```

**Apply:** https://www.goatcounter.com/signup

### Alternative 2: **PostHog** (Generous Free Tier)

**Pros:**
- ✅ Free tier includes 1M events/month
- ✅ Product analytics features (funnels, retention)
- ✅ Session replay
- ✅ No credit card required

**Cons:**
- ⚠️ Heavier than simple analytics
- ⚠️ More complex than needed for docs site

**Good for:** If you want advanced product analytics later

### Alternative 3: **No Analytics** (Start Simple)

**GitHub provides built-in analytics:**
- ✅ Traffic stats (views, clones, referrers)
- ✅ Stars, forks, watchers
- ✅ Repository insights

**For early stage, this might be enough!** Add analytics later when you need detailed insights.

### Comparison Table

| Tool | Cost | Self-Host | Setup | Privacy | Features | Recommended |
|------|------|-----------|-------|---------|----------|-------------|
| **Google Analytics** | Free | No | ⭐⭐⭐ Easy | ⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ **YES** |
| **Umami** | Free (self-host) | Required | ⭐⭐ Medium | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | If privacy matters |
| **GoatCounter** | Free tier | Optional | ⭐⭐⭐ Easy | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | Simple alternative |
| **PostHog** | Free tier (1M) | Optional | ⭐⭐⭐ Easy | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | Advanced features |
| **GitHub Stats** | Free | N/A | ⭐⭐⭐ None | ⭐⭐⭐⭐⭐ | ⭐ | Early stage |

### Recommendation

**Use Google Analytics 4 - it's the practical choice.**

Reasoning:
1. **Free & unlimited** - No usage caps
2. **2-minute setup** - Already integrated in Docusaurus
3. **Powerful insights** - Understand your audience deeply
4. **Industry standard** - Everyone knows how to use it
5. **SEO integration** - Connect with Google Search Console

## 🚀 Content Strategy

### Homepage Elements

1. **Hero Section**
   ```
   TITAN - The Fastest API Gateway
   190,000 req/s | HTTP/2 | TLS 1.3 | Written in C++23

   [Get Started] [View Benchmarks] [GitHub]
   ```

2. **Live Demo** (Interactive config generator)
   - User inputs: upstream servers, routes
   - Generates: config.json
   - Shows: Architecture diagram

3. **Feature Grid**
   - 🚀 Ultra-High Performance (190k req/s)
   - 🔒 TLS 1.3 with ALPN
   - 🔄 HTTP/2 Multiplexing
   - 💾 SIMD-Optimized Router
   - 🎯 Thread-Per-Core Architecture
   - 🔌 Connection Pooling
   - 📊 Prometheus Metrics
   - ⚡ Zero-Copy Proxying

4. **Benchmark Section** (Interactive charts)
   - Titan vs Nginx vs Envoy vs HAProxy
   - Latency percentiles (P50, P99, P999)
   - Throughput comparison
   - CPU efficiency

5. **Quick Start**
   ```bash
   # Auto-install optimal binary
   curl -fsSL https://titan.dev/install.sh | bash

   # Configure
   cat > config.json <<EOF
   {
     "server": {"port": 8080},
     "upstreams": [{
       "name": "api",
       "load_balancing": "round_robin",
       "backends": [
         {"host": "api1.internal", "port": 8080},
         {"host": "api2.internal", "port": 8080}
       ]
     }],
     "routes": [
       {"path": "/api/*", "upstream": "api"}
     ]
   }
   EOF

   # Run
   titan --config config.json
   ```

6. **Use Cases**
   - Microservices API Gateway
   - Kubernetes Ingress Controller
   - Edge Proxy / CDN Origin
   - Load Balancer
   - Service Mesh Data Plane

7. **Testimonials / Stats**
   - GitHub Stars
   - Docker Pulls
   - Production Deployments (when available)

### Documentation Structure

```
📚 Documentation

Getting Started
├── 🚀 Quick Start (5 minutes)
├── 📦 Installation
│   ├── Binary Download
│   ├── Docker
│   ├── Kubernetes (Helm)
│   └── Build from Source
├── 🎯 First Proxy
└── 📖 Core Concepts

Architecture
├── 🧵 Thread-Per-Core Design
├── 🔌 Connection Pooling
├── 💾 Memory Management (Arena Allocators)
├── ⚡ SIMD Optimizations
├── 🔄 HTTP/2 Multiplexing
└── 📊 Performance Characteristics

Configuration Reference
├── 📝 Configuration Schema
├── 🌐 Server Settings
├── 🎯 Upstreams & Backends
├── 🛣️ Routes & Routing
├── 🔧 Middleware
│   ├── CORS
│   ├── Rate Limiting
│   ├── Logging
│   └── Custom Middleware
├── 🔒 TLS Configuration
└── 📊 Metrics & Observability

Deployment Guide
├── 🐋 Docker Deployment
├── ☸️ Kubernetes (Helm Charts)
├── 🖥️ Bare Metal (systemd)
├── ☁️ Cloud Platforms
│   ├── AWS (ECS, EKS)
│   ├── GCP (GKE, Cloud Run)
│   └── Azure (AKS)
└── 🔧 Production Hardening

Benchmarks
├── 📊 Benchmark Methodology
├── 🆚 Titan vs Competitors
├── 📈 Performance Results
└── ⚡ Performance Tuning Guide

API Reference
├── 🔌 Control Plane API
├── 📊 Metrics (Prometheus)
├── 💚 Health Checks
└── 🔄 Hot Reload (SIGHUP)

Advanced Topics
├── 🔐 Security Best Practices
├── 🐛 Debugging & Profiling
├── 🧪 Load Testing
└── 🔧 Contributing Guide
```

### Blog Topics (SEO & Thought Leadership)

**Technical Deep Dives:**
1. "Building the Fastest API Gateway: C++23 Lessons Learned"
2. "Zero-Copy Proxying: How Titan Achieves 190k req/s"
3. "Thread-Per-Core vs Thread Pools: A Performance Comparison"
4. "SIMD Router Optimization: 20% Speedup from Vectorization"
5. "Connection Pooling Done Right: MSG_PEEK Health Checks"
6. "HTTP/2 Multiplexing Performance: When to Use vs HTTP/1.1"
7. "Memory Management for High-Performance Proxies"

**Benchmarks & Comparisons:**
8. "Benchmarking Titan vs Nginx vs Envoy: The Numbers"
9. "Why Titan is 63% Faster Than Nginx (And How We Measured It)"
10. "Latency Breakdown: Where Does Your API Gateway Spend Time?"

**Use Cases:**
11. "Deploying Titan as a Kubernetes Ingress Controller"
12. "Using Titan for High-Traffic Microservices"
13. "Titan at the Edge: CDN Origin Use Case"

**Release Announcements:**
14. "Titan v1.0: Production-Ready API Gateway"
15. "What's New in Titan v0.2: HTTP/2 & TLS Support"

## 🎨 Design & Branding

**Color Scheme:**
- Primary: Deep blue/purple (tech, trust)
- Accent: Electric blue (speed, performance)
- Dark mode: Essential for developers

**Logo Concept:**
- Titan (mythology) - Strong, powerful
- Geometric shape (C++, performance)
- Speed lines or circuit patterns

**Typography:**
- Headings: Bold, modern sans-serif (Inter, Montserrat)
- Body: Readable sans-serif (Inter, System UI)
- Code: JetBrains Mono, Fira Code

## 🔧 Interactive Components

### 1. Config Generator
React component that generates `config.json` from form inputs:
```tsx
<ConfigGenerator
  onGenerate={(config) => download(config, 'config.json')}
/>
```

### 2. Benchmark Comparison Chart
Interactive chart comparing Titan vs competitors:
```tsx
<BenchmarkChart
  data={{
    titan: { throughput: 190423, latency_p99: 0.642 },
    nginx: { throughput: 120000, latency_p99: 2.1 },
    envoy: { throughput: 100000, latency_p99: 3.5 }
  }}
/>
```

### 3. Architecture Diagram
Animated SVG showing request flow:
```tsx
<ArchitectureDiagram
  highlightPath={true}
  showMetrics={true}
/>
```

### 4. Live Playground (Future)
WebAssembly build of Titan running in browser:
```tsx
<TitanPlayground
  initialConfig={exampleConfig}
  allowRequests={true}
/>
```

## 📈 SEO Strategy

### Target Keywords

**Primary:**
- "fastest api gateway"
- "high performance reverse proxy"
- "c++ api gateway"
- "nginx alternative"

**Secondary:**
- "http2 load balancer"
- "thread per core proxy"
- "kubernetes ingress controller performance"
- "low latency api gateway"

**Long-tail:**
- "how to build high performance api gateway"
- "api gateway benchmark comparison"
- "zero copy http proxy"

### On-Page SEO

1. **Meta Tags** (auto-generated by Docusaurus)
   ```html
   <title>Titan - The Fastest API Gateway | 190k req/s</title>
   <meta name="description" content="Titan is a high-performance API gateway written in C++23. 190,000 req/s throughput, HTTP/2, TLS 1.3, and thread-per-core architecture." />
   ```

2. **Open Graph** (social media previews)
   ```html
   <meta property="og:title" content="Titan API Gateway" />
   <meta property="og:image" content="https://titan.dev/img/og-image.png" />
   ```

3. **Structured Data** (JSON-LD for Google)
   ```json
   {
     "@type": "SoftwareApplication",
     "name": "Titan API Gateway",
     "applicationCategory": "DeveloperApplication",
     "operatingSystem": "Linux"
   }
   ```

4. **Sitemap** - Auto-generated by Docusaurus

5. **Algolia DocSearch** - Free search for open source
   - Apply at: https://docsearch.algolia.com/apply/

## 🔗 External Links & Backlinks

**Community Presence:**
- GitHub Discussions (Q&A)
- Reddit: r/golang, r/programming, r/devops
- Hacker News: Launch announcements
- Dev.to: Cross-post blog articles
- Medium: Syndicate top articles

**Comparison Pages:**
- "Titan vs Nginx"
- "Titan vs Envoy"
- "Titan vs HAProxy"
- "API Gateway Comparison 2025"

**Tool Listings:**
- Awesome Lists (awesome-go, awesome-cpp)
- AlternativeTo.net
- Product Hunt launch

## 🚀 Launch Strategy

### Phase 1: Soft Launch (Pre-v1.0)
- ✅ Basic documentation site (Getting Started, Architecture)
- ✅ GitHub README with badges
- ✅ Initial blog post: "Introducing Titan"
- ✅ Submit to Hacker News "Show HN"

### Phase 2: v1.0 Launch
- ✅ Full documentation site
- ✅ Benchmark comparison page
- ✅ Interactive demos
- ✅ Launch blog post: "Titan v1.0: Production-Ready"
- ✅ Submit to:
  - Hacker News
  - Reddit (r/programming, r/devops)
  - Dev.to
  - Product Hunt

### Phase 3: Growth
- ✅ Regular blog posts (1-2 per month)
- ✅ Conference talks / presentations
- ✅ YouTube demos / tutorials
- ✅ Partnerships with K8s community

## 📊 Success Metrics

**Traffic:**
- 10k monthly visitors (Year 1)
- 50k monthly visitors (Year 2)

**Engagement:**
- Avg. session duration: >3 minutes
- Bounce rate: <60%
- Pages per session: >2

**Conversion:**
- GitHub stars: 1k (Year 1), 5k (Year 2)
- Docker pulls: 10k (Year 1), 100k (Year 2)
- Documentation searches: 500/month

**SEO:**
- Rank #1 for "fastest api gateway"
- Rank top 5 for "nginx alternative"
- 100+ organic keywords ranking

## 🔧 Implementation Timeline

### Week 1-2: Setup
- [ ] Initialize Docusaurus project
- [ ] Configure CI/CD for auto-deployment
- [ ] Setup custom domain (titan.dev or similar)
- [ ] Integrate Plausible Analytics

### Week 3-4: Content Creation
- [ ] Write core documentation (Getting Started, Installation)
- [ ] Migrate existing docs from CLAUDE.md
- [ ] Create architecture diagrams
- [ ] Write first blog post

### Week 5-6: Interactive Features
- [ ] Build Config Generator component
- [ ] Build Benchmark Chart component
- [ ] Design homepage
- [ ] Add search (Algolia)

### Week 7-8: Polish & Launch
- [ ] SEO optimization
- [ ] Open Graph images
- [ ] Cross-browser testing
- [ ] Soft launch

---

**Next Steps:**
1. Choose domain name (titan.dev, gettitan.io, titan-gateway.io)
2. Initialize Docusaurus project in `/website` directory
3. Setup CI/CD workflow for automatic deployment to GitHub Pages
4. Begin migrating documentation from markdown files
