import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import styles from './index.module.css';

const features = [
  {
    title: '语音聊天',
    emoji: '🎙️',
    description: '与 AI 实时语音对话，支持 PCM/OPUS 音频格式，低延迟流式传输，TTS 语音播报。',
  },
  {
    title: '图片理解',
    emoji: '📷',
    description: '发送图片给 AI 进行识别，获取结构化 JSON 结果和语音回复。',
  },
  {
    title: '设备配网',
    emoji: '🔗',
    description: '支持多种配网方式：设备扫码、App 扫码、OpenAPI 激活、BLE 蓝牙配网。',
  },
  {
    title: 'MCP 设备命令',
    emoji: '🤖',
    description: 'AI 通过 MCP 协议控制设备执行本地操作，支持 JSON-RPC 命令与响应。',
  },
  {
    title: '多平台支持',
    emoji: '🔧',
    description: '支持 macOS、Linux、ESP-IDF (ESP32)、MIPS 等平台，PAL 可移植架构。',
  },
  {
    title: '双协议可选',
    emoji: '⚡',
    description: 'RTC TCP Client（源码级集成）和 RTC Client（预编译库），按需选择。',
  },
];

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={clsx('hero hero--primary', styles.heroBanner)}>
      <div className="container">
        <h1 className="hero__title">{siteConfig.title}</h1>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <p className={styles.heroSlogan}>让 AI 连接物理世界</p>
        <div className={styles.buttons}>
          <Link className="button button--secondary button--lg" to="/docs/tutorials/quick-start">
            快速开始
          </Link>
        </div>
      </div>
    </header>
  );
}

function Feature({title, emoji, description}: {title: string; emoji: string; description: string}) {
  return (
    <div className={clsx('col col--4')}>
      <div className="text--center" style={{fontSize: '3rem'}}>{emoji}</div>
      <div className="text--center padding-horiz--md">
        <h3>{title}</h3>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function Home(): React.JSX.Element {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout title={siteConfig.title} description={siteConfig.tagline}>
      <HomepageHeader />
      <main>
        <section className={styles.features}>
          <div className="container">
            <div className="row">
              {features.map((props, idx) => (
                <Feature key={idx} {...props} />
              ))}
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}
