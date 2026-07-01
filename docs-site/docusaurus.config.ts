import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Tuya Agentic-kit',
  tagline: 'Multimodal AI SDK for IoT Devices',
  favicon: 'img/favicon.ico',

  future: {
    v4: true,
  },

  url: 'https://agentic-kit.tuya.com',
  baseUrl: '/',

  organizationName: 'tuya',
  projectName: 'agentic-kit',

  onBrokenLinks: 'throw',

  i18n: {
    defaultLocale: 'zh-Hans',
    locales: ['zh-Hans'],
  },

  markdown: {
    mermaid: true,
    mdx1Compat: {
      admonitions: true,
    },
  },
  themes: ['@docusaurus/theme-mermaid'],

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          routeBasePath: 'docs',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    colorMode: {
      defaultMode: 'light',
      respectPrefersColorScheme: true,
    },
    navbar: {
      title: 'Tuya Agentic-kit',
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docs',
          position: 'left',
          label: '文档',
        },
        {
          href: 'https://developer.tuya.com',
          label: '涂鸦开发者',
          position: 'right',
        },
        {
          href: 'https://github.com/tuya/agentic-kit',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: '文档',
          items: [
            { label: '介绍', to: '/docs/intro' },
            { label: '快速开始', to: '/docs/tutorials/quick-start' },
            { label: 'FAQ', to: '/docs/faq' },
          ],
        },
        {
          title: 'SDK 参考',
          items: [
            { label: 'RTC TCP Client', to: '/docs/reference/rtc-tcp-client' },
            { label: 'RTC Client', to: '/docs/reference/rtc-client' },
            { label: 'IoT Client', to: '/docs/reference/iot-client' },
          ],
        },
        {
          title: '资源',
          items: [
            { label: '涂鸦 IoT 平台', href: 'https://iot.tuya.com' },
            { label: '涂鸦开发者文档', href: 'https://developer.tuya.com' },
          ],
        },
      ],
      copyright: `Copyright \u00a9 ${new Date().getFullYear()} Tuya Inc. Built with Docusaurus.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['c', 'json', 'bash', 'python'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
