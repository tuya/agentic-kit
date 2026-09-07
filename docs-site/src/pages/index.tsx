import React, {useState} from 'react';
import Link from '@docusaurus/Link';
import Layout from '@theme/Layout';
import styles from './index.module.css';

type Product = {
  title: string;
  description: string;
  prompt: string;
  hardware: string;
  capabilities: [string, string, string][];
  steps: string[];
};

const products: Record<string, Product> = {
  toy: {
    title: '会交流的玩具，有自己的性格。',
    description: '从听清孩子的话，到生成角色回应，再让语音与设备动作一起表达。',
    prompt: '"今天想听一个去月球的故事。"',
    hardware: '硬件侧：麦克风、扬声器与状态灯；固件负责采集、播放与本地反馈。',
    capabilities: [['感知算法', 'VAD / PVAD / ASR', '组合'], ['声音表达', 'TTS / 角色音色', '组合'], ['云端智能', 'LLM / 角色设定', '组合'], ['记忆与知识', '偏好记忆 / 故事内容', '按需'], ['本地工具', '状态灯 / 表情反馈', '按需'], ['自有业务', '角色 IP / 内容服务', '自主']],
    steps: ['设备采集语音，经 SDK 上传。', '云端识别语音，按角色生成故事与语音。', '设备播放回复，固件驱动状态灯。'],
  },
  camera: {
    title: '按下快门，开始一次探索。',
    description: '围绕拍照识物组合视觉理解与语音讲解，让摄像头、屏幕和扬声器一起工作。',
    prompt: '"这片叶子为什么会变黄？"',
    hardware: '硬件侧：摄像头、屏幕与扬声器；固件负责拍照、上传、显示与播放。',
    capabilities: [['感知算法', '物体识别 / 视觉理解', '组合'], ['声音表达', 'TTS / 讲解音色', '组合'], ['云端智能', '多模态模型 / 讲解', '组合'], ['记忆与知识', '学习记录 / 内容知识', '按需'], ['本地工具', '拍照 / 屏幕 / 播放', '组合'], ['自有业务', '教材 / 课程 / 账户', '自主']],
    steps: ['设备拍摄并通过 SDK 上传图片。', '云端理解画面，生成适合场景的讲解。', '屏幕展示结果，扬声器播放讲解。'],
  },
  assistant: {
    title: '听懂一句话，落到设备动作。',
    description: '从语音理解到工具执行，让空间助手操作产品已授权、已接入的设备功能。',
    prompt: '"打开阅读灯，把亮度调到 60%。"',
    hardware: '硬件侧：语音入口与本地工具；跨设备控制另需配置设备绑定、权限与平台服务。',
    capabilities: [['感知算法', 'ASR / 设备名称热词', '组合'], ['声音表达', 'TTS / 执行回执', '组合'], ['云端智能', 'Agent / 物理执行能力', '组合'], ['记忆与知识', '设备上下文 / 使用偏好', '按需'], ['本地工具', 'MCP / 已授权设备动作', '组合'], ['自有业务', '空间场景 / 自动化规则', '自主']],
    steps: ['设备上传语音，AI 理解设备与动作。', '匹配已授权工具，生成亮度控制参数。', '本地执行并回传结果，播报完成。'],
  },
  efficiency: {
    title: '把会议变成下一步行动。',
    description: '让桌面终端或随身设备完成采集、理解、知识检索与任务整理，把 AI 融入工作现场。',
    prompt: '"整理刚才的会议，列出负责人和截止时间。"',
    hardware: '终端侧：麦克风、屏幕或摄像头；可按需连接企业知识库、业务工具与客户自有服务。',
    capabilities: [['感知算法', 'ASR / 说话人识别', '组合'], ['声音表达', 'TTS / 语音提醒', '按需'], ['云端智能', 'Agent / 总结与规划', '组合'], ['记忆与知识', '会议上下文 / 企业知识', '组合'], ['本地工具', '录音 / 屏幕 / 文件', '组合'], ['自有业务', '日历 / 任务 / 工作流', '自主']],
    steps: ['终端采集会议内容并形成结构化上下文。', 'Agent 结合知识与任务规则提取行动项。', '通过开放能力写入客户工作流，等待用户确认。'],
  },
};

const productLabels: [string, string][] = [['toy', 'AI 玩具'], ['camera', '拍学设备'], ['assistant', '空间助手'], ['efficiency', 'AI 效率']];

function Arrow() {
  return <span aria-hidden="true" className={styles.arrow}>-&gt;</span>;
}

function Eyebrow({children}: {children: React.ReactNode}) {
  return <span className={styles.eyebrow}>{children}</span>;
}

function NetworkDiagram() {
  return <div className={styles.network} role="img" aria-label="AI 硬件通过 Agentic-Kit 连接 Tuya Physical AI 平台的示意图">
    <div className={styles.networkHead}><span>ANY HARDWARE · ONE REAL-TIME LINK</span><span className={styles.connected}>CONNECTED</span></div>
    <div className={styles.networkCanvas}>
      <svg className={styles.networkLines} viewBox="0 0 620 347" preserveAspectRatio="none" aria-hidden="true">
        <path d="M202 43 C268 43 228 155 323 173" /><path d="M202 119 C262 119 247 160 323 173" /><path d="M202 195 C265 195 253 180 323 173" /><path d="M202 271 C270 271 232 193 323 173" /><path d="M386 173 C430 173 437 173 475 173" />
      </svg>
      {['AI 玩具', 'AI 效率终端', 'AI 穿戴', '具身机器人'].map((name, index) => <div className={`${styles.hardwareNode} ${styles[`node${index + 1}`]}`} key={name}><span>{['TOY', 'WORK', 'WEAR', 'BOT'][index]}</span><strong>{name}</strong></div>)}
      <div className={styles.kitCore}><strong>Agentic<br />Kit</strong><small>C SDK · tRTC · MCP</small></div>
      <div className={styles.cloudNode}><span>TUYA PHYSICAL AI</span><h3>Tuya AI 智能平台</h3>{['感知算法', 'Agent 编排', '记忆 · 知识', '模型 · 技能', 'MCP / A2A'].map((item) => <div key={item}>{item}</div>)}</div>
    </div>
    <div className={styles.networkFoot}><span>上行 音频 · 图像 · 视频 · 文字 · 事件</span><span>下行 语音 · 指令 · 工具调用</span></div>
  </div>;
}

function ProductComposer() {
  const [selected, setSelected] = useState('toy');
  const product = products[selected];
  return <>
    <div className={styles.productTabs} role="tablist" aria-label="选择硬件品类">
      {productLabels.map(([key, label], index) => <button className={selected === key ? styles.selectedTab : ''} key={key} role="tab" aria-selected={selected === key} onClick={() => setSelected(key)}><span>{label}</span><small>0{index + 1}</small></button>)}
    </div>
    <div className={styles.composer}>
      <div><div className={styles.composerHead}><span>CAPABILITY SELECTION</span><small>按场景组合示意</small></div>
        <div className={styles.capabilities}>{product.capabilities.map(([type, name, status]) => <div className={styles.capability} key={type}><span>{type}</span><strong>{name}</strong><small className={status === '组合' ? styles.activeCapability : ''}>{status}</small></div>)}</div>
        <p className={styles.composerFoot}>{product.hardware}</p>
      </div>
      <div className={styles.experience}><div className={styles.experienceTop}><span>PRODUCT EXPERIENCE</span><span>SCENARIO FLOW</span></div><div className={styles.experienceBody}><h3>{product.title}</h3><p>{product.description}</p><div className={styles.prompt}>{product.prompt}</div><ol>{product.steps.map((step, index) => <li key={step}><span>{index + 1}</span>{step}</li>)}</ol></div></div>
    </div>
  </>;
}

function CodeExample() {
  const command = `# 获取源码与子模块\ngit clone https://github.com/tuya/agentic-kit.git\ncd agentic-kit\ngit submodule update --init --recursive\n\n# 编译并运行 POSIX 文本对话示例\ncmake -S examples/posix -B build-examples\ncmake --build build-examples\n./build-examples/text_chat_demo`;
  const [copied, setCopied] = useState(false);
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(command);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2500);
    } catch {
      setCopied(false);
    }
  };
  return <div><div className={styles.terminal}><div className={styles.terminalHead}><span>TERMINAL · macOS / Linux</span><button type="button" onClick={copy}>{copied ? '已复制' : '复制命令'}</button></div><pre><code>{command}</code></pre><div className={styles.terminalFoot}>CMake ≥ 3.20 / Python 3 / C &amp; C++ toolchain</div></div><p className={styles.terminalNote}>{copied ? '命令已复制。桌面验证完成后，再按目标系统完成硬件适配。' : '桌面示例用于验证交互链路；接入产品时，再按目标系统适配平台、外设与设备身份。'}</p></div>;
}

export default function Home(): React.JSX.Element {
  return <Layout title="Agentic-Kit" description="面向 AI 硬件的轻量端云实时交互套件">
    <main>
      <section className={styles.hero}><div className={styles.wrap}><div className={styles.heroTop}><Eyebrow>Built for Physical AI</Eyebrow><span>TUYA / EMBEDDED AI INFRASTRUCTURE</span></div><div className={styles.heroGrid}><div className={styles.heroCopy}><h1>让 AI，<br />连接<span>物理世界。</span></h1><p><strong>Agentic-Kit 是面向 Physical AI 的原子级轻量端侧 SDK。</strong><br />覆盖低算力 MCU、Linux SoC、PC、手机 App 与浏览器，从语音玩具到具身机器人，通过一套接入体系实现端云实时交互，并按需连接感知算法、Agent、记忆与模型。</p><div className={styles.actions}><Link className={styles.primaryButton} to="/docs/tutorials/quick-start">接入你的硬件 <Arrow /></Link><a className={styles.secondaryButton} href="#composition">探索能力组合 <span aria-hidden="true">↓</span></a></div><small>原子级能力开放 · 芯片无关 · 云与模型无关 · 零业务侵入 · 按需选用</small></div><NetworkDiagram /></div><div className={styles.metrics}>{[['≈120 KB', 'SDK 体积', '低算力设备也能接入'], ['≈60 KB', '运行内存', '为业务逻辑保留资源'], ['30-70%', '弱网丢包可用', 'T-RTC 抗弱网能力'], ['≤86 ms', '全球平均通信时延', '端云实时传输'], ['≈1.5 s', '语音端到端', '含长记忆与知识库']].map(([value, label, detail], index) => <div className={index > 1 ? styles.platformMetric : ''} key={label}><strong>{value}</strong><span>{label}</span><small>{detail}</small></div>)}</div><p className={styles.metricNote}>典型指标；实际表现受芯片、功能配置、网络与产品方案影响。</p></div></section>

      <section className={`${styles.section} ${styles.white}`}><div className={styles.wrap}><Eyebrow>01 / One kit. A whole foundation.</Eyebrow><div className={styles.sectionHead}><h2>原子能力开放，<br />释放你的创新力。</h2><p>Agentic-Kit 是 Tuya Physical AI 创新平台面向端侧的开放入口。平台为 AI 创新伙伴提供研发、量产与全球增长的一站式支持。</p></div><div className={styles.roles}>{[['YOUR AI ENDPOINT', '你的 AI 硬件终端', '芯片、BSP 与外设驱动|麦克风 / 摄像头采集|扬声器 / 屏幕 / 执行器|自有固件与产品逻辑'], ['AGENTIC-KIT', '端云实时交互套件', '配网、激活与鉴权|实时多模态数据传输|会话事件与回调|设备侧 MCP 命令交互'], ['TUYA PHYSICAL AI', 'Tuya Physical AI 平台', '语音与视觉感知算法|大模型、Agent 与记忆|设备控制与场景服务|全球连接与持续运营']].map(([tag, title, entries], index) => <article className={index === 1 ? styles.roleAccent : ''} key={title}><small>{tag}</small><h3>{title}</h3><ul>{entries.split('|').map((entry) => <li key={entry}>{entry}</li>)}</ul></article>)}</div><p className={styles.scopeNote}><strong>按需组合，边界清晰。</strong> 平台模型、算法、记忆与行业服务按需选用；客户核心云服务也可通过开放能力接入。</p></div></section>

      <section className={styles.section} id="embedded"><div className={styles.wrap}><Eyebrow>02 / Designed around embedded constraints</Eyebrow><div className={styles.sectionHead}><h2>懂 AI，<br />也懂硬件的约束。</h2><p>真正的端侧能力，不止是能接入，还要在有限资源、异构芯片与波动网络中保持可用。</p></div><div className={styles.embedded}>{[['把重智能放在云端，把端侧做轻', '端侧保留采集、播放、协议与本地控制，复杂推理按需交给云端。在有限 RAM 与 Flash 下，也能构建完整 AI 体验。', 'LIGHTWEIGHT EDGE / CLOUD INTELLIGENCE'], ['用适配层隔离芯片差异', 'PAL 将网络、线程、时间与存储等平台能力抽象出来。更换芯片或操作系统时，业务逻辑无需随底层一起重写。', 'PAL / PORT ONCE / KEEP YOUR LOGIC'], ['弱网下，交互仍要连续', '围绕音频、图像、视频、文字与事件建立实时链路，并处理流式传输、打断、重连与网络波动。', 'STREAMING / INTERRUPTION / RECONNECT'], ['物理动作，由端侧把住执行边界', 'AI 通过 MCP 下发工具意图，由本地固件决定是否执行、完成动作并回传结果。', 'MCP / LOCAL DECISION / EXECUTION RESULT']].map(([title, text, code], index) => <article key={title}><span>/ 0{index + 1}</span><div><h3>{title}</h3><p>{text}</p><code>{code}</code></div></article>)}</div><div className={styles.platforms}><span>已提供的平台与适配路径</span><div>ESP32 / ESP-IDF · FreeRTOS · ARM · MIPS · Linux</div></div></div></section>

      <section className={`${styles.section} ${styles.composition}`} id="composition"><div className={styles.wrap}><Eyebrow>03 / Compose the intelligence your product needs</Eyebrow><div className={styles.sectionHead}><h2>同一套底座，<br />组合出不同的 AI 硬件。</h2><p>从陪伴、学习与效率终端，到具身机器人。对话、感知、记忆、执行与设备生态能力，都可以围绕产品场景组合。</p></div><ProductComposer /><p className={styles.compositionNote}><strong>组合的是平台能力，运行的是你的产品。</strong> Agentic-Kit 提供连接和数据交互，算法、记忆与行业服务需按产品方案配置。</p></div></section>

      <section className={`${styles.section} ${styles.white}`}><div className={styles.wrap}><Eyebrow>04 / Perception built for real devices</Eyebrow><div className={styles.sectionHead}><h2>算法的价值，<br />要在真实设备上体现。</h2><p>Physical AI 的体验，来自端侧感知、云端算法与真实硬件协同。Agentic-Kit 让算法按产品需要部署和组合。</p></div><div className={styles.algorithms}>{[['01 / ON-DEVICE PERCEPTION', '端侧感知，从硬件条件出发。', '关键词唤醒与视觉预处理可由端侧方案提供；VAD 可按产品需要选择端侧或云端。', 'KWS · VAD · 视觉预处理'], ['02 / TUYA CLOUD ALGORITHMS', '云端算法，持续优化产品体验。', '音频、图像与视频通过实时链路连接云端算法，获得文字、语音或结构化结果。', 'Nomo-ASR · Nomo-PVAD · Pet ID'], ['03 / VERTICAL MODELS', '垂直算法，更懂真实世界。', '围绕远场拾音、多人干扰、宠物个体与复杂画面等真实设备问题持续优化。', '语音模型 · 声音模型 · 视觉模型']].map(([tag, title, text, chips]) => <article key={tag}><small>{tag}</small><h3>{title}</h3><p>{text}</p><span>{chips}</span></article>)}</div></div></section>

      <section className={`${styles.section} ${styles.white}`} id="start"><div className={styles.wrap}><Eyebrow>05 / Hello, physical world.</Eyebrow><div className={styles.startGrid}><div><h2>先跑起来。<br />再做成你的产品。</h2><p>无需先选定芯片或准备开发板。在 macOS 或 Linux 上运行文本对话示例，先验证端云链路，再把确认过的交互带到你的硬件。</p><ol className={styles.startSteps}><li><span>01</span><div><strong>在电脑上验证</strong><br />获取源码，运行 POSIX 示例</div></li><li><span>02</span><div><strong>带入目标硬件</strong><br />适配平台、音视频与本地工具</div></li><li><span>03</span><div><strong>走向产品与量产</strong><br />配置 PID、设备身份与产品能力</div></li></ol><Link className={styles.primaryButton} to="/docs/tutorials/quick-start">打开完整接入指南 <Arrow /></Link></div><CodeExample /></div><div className={styles.sdkLinks}><Link to="/docs/reference/rtc-tcp-client"><strong>RTC TCP Client -&gt;</strong><span>源码级集成 · PAL 移植</span></Link><Link to="/docs/reference/rtc-client"><strong>RTC Client -&gt;</strong><span>预编译库集成 · 目标平台匹配</span></Link></div></div></section>

      <section className={`${styles.section} ${styles.choices}`} id="openness"><div className={styles.wrap}><Eyebrow>06 / Your AI. Your way.</Eyebrow><div className={styles.sectionHead}><h2>从标准化快速开发，<br />到原子级灵活接入。</h2><p>TuyaOS、TuyaOpen 与 Agentic-Kit 分别提供不同的端侧框架和接入方式，适配不同的项目阶段与技术栈。</p></div><div className={styles.choiceGrid}>{[['STANDARD / COMMERCIAL FOUNDATION', 'TuyaOS（Wukong）', '采用涂鸦标准化产品框架，与连接、安全、设备模型和量产体系协同。', '了解 TuyaOS（Wukong）', 'https://www.tuya.com/cn/platform/productdev/tuyaos'], ['STANDARD / OPEN SOURCE STACK', 'TuyaOpen', '采用涂鸦标准化分层框架，同时开放源码并封装丰富的 AI 与 IoT 组件。', '了解 TuyaOpen', 'https://www.tuyaopen.ai/'], ['ATOMIC SDK / FIT YOUR STACK', 'Agentic-Kit', '不改变客户已有技术栈和方案选型，按需选用，面向资源更紧或需要深度定制的产品。', '了解 Agentic-Kit', '/docs/intro']].map(([tag, title, text, linkLabel, link], index) => <article className={index === 2 ? styles.featuredChoice : ''} key={title}><small>{tag}</small><h3>{title}</h3><p>{text}</p><ul><li>匹配目标项目的开发框架</li><li>按产品需求选择能力边界</li><li>支持商业化产品开发与量产</li></ul>{link.startsWith('/') ? <Link to={link}>{linkLabel} <Arrow /></Link> : <a href={link} target="_blank" rel="noreferrer">{linkLabel} <Arrow /></a>}</article>)}</div></div></section>
      <section className={styles.closing}><div className={styles.wrap}><div><h2>你的下一款 AI 硬件，从这里开始。</h2><p>从嵌入式接入，到按需组合平台能力。</p></div><Link className={styles.closingButton} to="/docs/intro">了解 Agentic-Kit <Arrow /></Link></div></section>
    </main>
  </Layout>;
}
