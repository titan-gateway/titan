import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Titan API Gateway',
  tagline: 'High-Performance API Gateway Built for Speed',
  favicon: 'favicon.ico',

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
          showLastUpdateAuthor: false,
          showLastUpdateTime: false,
          versions: {
            current: {
              label: 'Latest',
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
    image: 'img/logo/og-image.png',
    colorMode: {
      defaultMode: 'dark',
      disableSwitch: false,
      respectPrefersColorScheme: false,
    },
    navbar: {
      title: 'Titan',
      logo: {
        alt: 'Titan Logo',
        src: 'img/logo/titan-icon-light.svg',
        srcDark: 'img/logo/titan-icon-dark.svg',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'tutorialSidebar',
          position: 'left',
          label: 'Docs',
        },
        {to: '/blog', label: 'Blog', position: 'left'},
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
              to: '/docs/getting-started/installation',
            },
            {
              label: 'Configuration',
              to: '/docs/configuration/overview',
            },
            {
              label: 'Deployment',
              to: '/docs/deployment/overview',
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
        'If you like Titan, give it a star on <a target="_blank" rel="noopener noreferrer" href="https://github.com/JonathanBerhe/titan">GitHub</a>!',
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

  headTags: [
    {
      tagName: 'link',
      attributes: {
        rel: 'icon',
        href: '/titan/favicon.svg',
        type: 'image/svg+xml',
      },
    },
    {
      tagName: 'link',
      attributes: {
        rel: 'apple-touch-icon',
        href: '/titan/apple-touch-icon.png',
      },
    },
  ],
};

export default config;
