import React, {useState} from 'react';
import Link from '@docusaurus/Link';
import Translate, {translate} from '@docusaurus/Translate';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import styles from './index.module.css';

type CapabilityStatus = 'included' | 'optional' | 'customer';

type Product = {
  title: string;
  description: string;
  prompt: string;
  hardware: string;
  capabilities: [string, string, CapabilityStatus][];
  steps: string[];
};

function getProducts(): Record<string, Product> {
  return {
    toy: {
      title: translate({id: 'homepage.products.toy.title', message: '会交流的玩具，有自己的性格。', description: 'AI 玩具场景标题'}),
      description: translate({id: 'homepage.products.toy.description', message: '从听清孩子的话，到生成角色回应，再让语音与设备动作一起表达。', description: 'AI 玩具场景描述'}),
      prompt: translate({id: 'homepage.products.toy.prompt', message: '“今天想听一个去月球的故事。”', description: 'AI 玩具场景示例提示词'}),
      hardware: translate({id: 'homepage.products.toy.hardware', message: '硬件侧：麦克风、扬声器与状态灯；固件负责采集、播放与本地反馈。', description: 'AI 玩具硬件说明'}),
      capabilities: [
        [translate({id: 'homepage.capability.perception', message: '感知算法', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.perception', message: 'VAD / PVAD / ASR', description: 'AI 玩具感知能力'}), 'included'],
        [translate({id: 'homepage.capability.voice', message: '声音表达', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.voice', message: 'TTS / 角色音色', description: 'AI 玩具声音能力'}), 'included'],
        [translate({id: 'homepage.capability.cloud', message: '云端智能', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.cloud', message: 'LLM / 角色设定', description: 'AI 玩具云端能力'}), 'included'],
        [translate({id: 'homepage.capability.memory', message: '记忆与知识', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.memory', message: '偏好记忆 / 故事内容', description: 'AI 玩具记忆能力'}), 'optional'],
        [translate({id: 'homepage.capability.localTools', message: '本地工具', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.localTools', message: '状态灯 / 表情反馈', description: 'AI 玩具本地工具'}), 'optional'],
        [translate({id: 'homepage.capability.customerBusiness', message: '自有业务', description: '能力类型'}), translate({id: 'homepage.products.toy.capability.customerBusiness', message: '角色 IP / 内容服务', description: 'AI 玩具客户业务'}), 'customer'],
      ],
      steps: [
        translate({id: 'homepage.products.toy.step.upload', message: '设备采集语音，经 SDK 上传。', description: 'AI 玩具场景步骤'}),
        translate({id: 'homepage.products.toy.step.generate', message: '云端识别语音，按角色生成故事与语音。', description: 'AI 玩具场景步骤'}),
        translate({id: 'homepage.products.toy.step.play', message: '设备播放回复，固件驱动状态灯。', description: 'AI 玩具场景步骤'}),
      ],
    },
    camera: {
      title: translate({id: 'homepage.products.camera.title', message: '按下快门，开始一次探索。', description: '拍学设备场景标题'}),
      description: translate({id: 'homepage.products.camera.description', message: '围绕拍照识物组合视觉理解与语音讲解，让摄像头、屏幕和扬声器一起工作。', description: '拍学设备场景描述'}),
      prompt: translate({id: 'homepage.products.camera.prompt', message: '“这片叶子为什么会变黄？”', description: '拍学设备场景示例提示词'}),
      hardware: translate({id: 'homepage.products.camera.hardware', message: '硬件侧：摄像头、屏幕与扬声器；固件负责拍照、上传、显示与播放。', description: '拍学设备硬件说明'}),
      capabilities: [
        [translate({id: 'homepage.capability.perception', message: '感知算法', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.perception', message: '物体识别 / 视觉理解', description: '拍学设备感知能力'}), 'included'],
        [translate({id: 'homepage.capability.voice', message: '声音表达', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.voice', message: 'TTS / 讲解音色', description: '拍学设备声音能力'}), 'included'],
        [translate({id: 'homepage.capability.cloud', message: '云端智能', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.cloud', message: '多模态模型 / 讲解', description: '拍学设备云端能力'}), 'included'],
        [translate({id: 'homepage.capability.memory', message: '记忆与知识', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.memory', message: '学习记录 / 内容知识', description: '拍学设备记忆能力'}), 'optional'],
        [translate({id: 'homepage.capability.localTools', message: '本地工具', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.localTools', message: '拍照 / 屏幕 / 播放', description: '拍学设备本地工具'}), 'included'],
        [translate({id: 'homepage.capability.customerBusiness', message: '自有业务', description: '能力类型'}), translate({id: 'homepage.products.camera.capability.customerBusiness', message: '教材 / 课程 / 账户', description: '拍学设备客户业务'}), 'customer'],
      ],
      steps: [
        translate({id: 'homepage.products.camera.step.upload', message: '设备拍摄并通过 SDK 上传图片。', description: '拍学设备场景步骤'}),
        translate({id: 'homepage.products.camera.step.explain', message: '云端理解画面，生成适合场景的讲解。', description: '拍学设备场景步骤'}),
        translate({id: 'homepage.products.camera.step.present', message: '屏幕展示结果，扬声器播放讲解。', description: '拍学设备场景步骤'}),
      ],
    },
    assistant: {
      title: translate({id: 'homepage.products.assistant.title', message: '听懂一句话，落到设备动作。', description: '空间助手场景标题'}),
      description: translate({id: 'homepage.products.assistant.description', message: '从语音理解到工具执行，让空间助手操作产品已授权、已接入的设备功能。', description: '空间助手场景描述'}),
      prompt: translate({id: 'homepage.products.assistant.prompt', message: '“打开阅读灯，把亮度调到 60%。”', description: '空间助手场景示例提示词'}),
      hardware: translate({id: 'homepage.products.assistant.hardware', message: '硬件侧：语音入口与本地工具；跨设备控制另需配置设备绑定、权限与平台服务。', description: '空间助手硬件说明'}),
      capabilities: [
        [translate({id: 'homepage.capability.perception', message: '感知算法', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.perception', message: 'ASR / 设备名称热词', description: '空间助手感知能力'}), 'included'],
        [translate({id: 'homepage.capability.voice', message: '声音表达', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.voice', message: 'TTS / 执行回执', description: '空间助手声音能力'}), 'included'],
        [translate({id: 'homepage.capability.cloud', message: '云端智能', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.cloud', message: 'Agent / 物理执行能力', description: '空间助手云端能力'}), 'included'],
        [translate({id: 'homepage.capability.memory', message: '记忆与知识', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.memory', message: '设备上下文 / 使用偏好', description: '空间助手记忆能力'}), 'optional'],
        [translate({id: 'homepage.capability.localTools', message: '本地工具', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.localTools', message: 'MCP / 已授权设备动作', description: '空间助手本地工具'}), 'included'],
        [translate({id: 'homepage.capability.customerBusiness', message: '自有业务', description: '能力类型'}), translate({id: 'homepage.products.assistant.capability.customerBusiness', message: '空间场景 / 自动化规则', description: '空间助手客户业务'}), 'customer'],
      ],
      steps: [
        translate({id: 'homepage.products.assistant.step.understand', message: '设备上传语音，AI 理解设备与动作。', description: '空间助手场景步骤'}),
        translate({id: 'homepage.products.assistant.step.match', message: '匹配已授权工具，生成亮度控制参数。', description: '空间助手场景步骤'}),
        translate({id: 'homepage.products.assistant.step.execute', message: '本地执行并回传结果，播报完成。', description: '空间助手场景步骤'}),
      ],
    },
    efficiency: {
      title: translate({id: 'homepage.products.efficiency.title', message: '把会议变成下一步行动。', description: 'AI 效率场景标题'}),
      description: translate({id: 'homepage.products.efficiency.description', message: '让桌面终端或随身设备完成采集、理解、知识检索与任务整理，把 AI 融入工作现场。', description: 'AI 效率场景描述'}),
      prompt: translate({id: 'homepage.products.efficiency.prompt', message: '“整理刚才的会议，列出负责人和截止时间。”', description: 'AI 效率场景示例提示词'}),
      hardware: translate({id: 'homepage.products.efficiency.hardware', message: '终端侧：麦克风、屏幕或摄像头；可按需连接企业知识库、业务工具与客户自有服务。', description: 'AI 效率硬件说明'}),
      capabilities: [
        [translate({id: 'homepage.capability.perception', message: '感知算法', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.perception', message: 'ASR / 说话人识别', description: 'AI 效率感知能力'}), 'included'],
        [translate({id: 'homepage.capability.voice', message: '声音表达', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.voice', message: 'TTS / 语音提醒', description: 'AI 效率声音能力'}), 'optional'],
        [translate({id: 'homepage.capability.cloud', message: '云端智能', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.cloud', message: 'Agent / 总结与规划', description: 'AI 效率云端能力'}), 'included'],
        [translate({id: 'homepage.capability.memory', message: '记忆与知识', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.memory', message: '会议上下文 / 企业知识', description: 'AI 效率记忆能力'}), 'included'],
        [translate({id: 'homepage.capability.localTools', message: '本地工具', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.localTools', message: '录音 / 屏幕 / 文件', description: 'AI 效率本地工具'}), 'included'],
        [translate({id: 'homepage.capability.customerBusiness', message: '自有业务', description: '能力类型'}), translate({id: 'homepage.products.efficiency.capability.customerBusiness', message: '日历 / 任务 / 工作流', description: 'AI 效率客户业务'}), 'customer'],
      ],
      steps: [
        translate({id: 'homepage.products.efficiency.step.capture', message: '终端采集会议内容并形成结构化上下文。', description: 'AI 效率场景步骤'}),
        translate({id: 'homepage.products.efficiency.step.extract', message: 'Agent 结合知识与任务规则提取行动项。', description: 'AI 效率场景步骤'}),
        translate({id: 'homepage.products.efficiency.step.write', message: '通过开放能力写入客户工作流，等待用户确认。', description: 'AI 效率场景步骤'}),
      ],
    },
  };
}

const productLabels: [string, () => string][] = [
  ['toy', () => translate({id: 'homepage.products.toy.label', message: 'AI 玩具', description: '产品场景页签'})],
  ['camera', () => translate({id: 'homepage.products.camera.label', message: '拍学设备', description: '产品场景页签'})],
  ['assistant', () => translate({id: 'homepage.products.assistant.label', message: '空间助手', description: '产品场景页签'})],
  ['efficiency', () => translate({id: 'homepage.products.efficiency.label', message: 'AI 效率', description: '产品场景页签'})],
];

const capabilityStatusLabels: Record<CapabilityStatus, () => string> = {
  included: () => translate({id: 'homepage.capability.status.included', message: '组合', description: '已组合的平台能力状态'}),
  optional: () => translate({id: 'homepage.capability.status.optional', message: '按需', description: '可按需选用的平台能力状态'}),
  customer: () => translate({id: 'homepage.capability.status.customer', message: '自主', description: '由客户自主实现的能力状态'}),
};

function Arrow() {
  return <span aria-hidden="true" className={styles.arrow}>-&gt;</span>;
}

function Eyebrow({children}: {children: React.ReactNode}) {
  return <span className={styles.eyebrow}>{children}</span>;
}

function NetworkDiagram() {
  const hardware = [
    {code: translate({id: 'homepage.network.hardware.toy.code', message: 'TOY', description: '网络图 AI 玩具代码'}), label: translate({id: 'homepage.network.hardware.toy', message: 'AI 玩具', description: '网络图硬件类型'})},
    {code: translate({id: 'homepage.network.hardware.work.code', message: 'WORK', description: '网络图 AI 效率终端代码'}), label: translate({id: 'homepage.network.hardware.work', message: 'AI 效率终端', description: '网络图硬件类型'})},
    {code: translate({id: 'homepage.network.hardware.wearable.code', message: 'WEAR', description: '网络图 AI 穿戴代码'}), label: translate({id: 'homepage.network.hardware.wearable', message: 'AI 穿戴', description: '网络图硬件类型'})},
    {code: translate({id: 'homepage.network.hardware.robot.code', message: 'BOT', description: '网络图具身机器人代码'}), label: translate({id: 'homepage.network.hardware.robot', message: '具身机器人', description: '网络图硬件类型'})},
  ];
  const cloudCapabilities = [
    translate({id: 'homepage.network.cloud.perception', message: '感知算法', description: '网络图云端能力'}),
    translate({id: 'homepage.network.cloud.agent', message: 'Agent 编排', description: '网络图云端能力'}),
    translate({id: 'homepage.network.cloud.memory', message: '记忆 · 知识', description: '网络图云端能力'}),
    translate({id: 'homepage.network.cloud.models', message: '模型 · 技能', description: '网络图云端能力'}),
    translate({id: 'homepage.network.cloud.protocols', message: 'MCP / A2A', description: '网络图云端能力'}),
  ];
  return <div className={styles.network} role="img" aria-label={translate({id: 'homepage.network.ariaLabel', message: 'AI 硬件通过 Agentic-Kit 连接 Tuya Physical AI 平台的示意图', description: '首页网络示意图的无障碍标签'})}>
    <div className={styles.networkHead}><span><Translate id="homepage.network.heading" description="网络图标题">任意硬件 · 一条实时链路</Translate></span><span className={styles.connected}><Translate id="homepage.network.connected" description="网络连接状态">已连接</Translate></span></div>
    <div className={styles.networkCanvas}>
      <svg className={styles.networkLines} viewBox="0 0 620 347" preserveAspectRatio="none" aria-hidden="true">
        <path d="M202 43 C268 43 228 155 323 173" /><path d="M202 119 C262 119 247 160 323 173" /><path d="M202 195 C265 195 253 180 323 173" /><path d="M202 271 C270 271 232 193 323 173" /><path d="M386 173 C430 173 437 173 475 173" />
      </svg>
      {hardware.map((item, index) => <div className={`${styles.hardwareNode} ${styles[`node${index + 1}`]}`} key={index}><span>{item.code}</span><strong>{item.label}</strong></div>)}
      <div className={styles.kitCore}><strong><Translate id="homepage.network.kit.title.lineOne" description="网络图 Agentic-Kit 名称第一行">Agentic</Translate><br /><Translate id="homepage.network.kit.title.lineTwo" description="网络图 Agentic-Kit 名称第二行">Kit</Translate></strong><small><Translate id="homepage.network.kit.capabilities" description="网络图 Agentic-Kit 能力标签">C SDK · tRTC · MCP</Translate></small></div>
      <div className={styles.cloudNode}><span><Translate id="homepage.network.cloud.brand" description="网络图云端平台品牌">TUYA PHYSICAL AI</Translate></span><h3><Translate id="homepage.network.cloud.title" description="网络图云端平台名称">Tuya AI 智能平台</Translate></h3>{cloudCapabilities.map((item, index) => <div key={index}>{item}</div>)}</div>
    </div>
    <div className={styles.networkFoot}><span><Translate id="homepage.network.uplink" description="网络图上行数据类型">上行 音频 · 图像 · 视频 · 文字 · 事件</Translate></span><span><Translate id="homepage.network.downlink" description="网络图下行数据类型">下行 语音 · 指令 · 工具调用</Translate></span></div>
  </div>;
}

function ProductComposer() {
  const [selected, setSelected] = useState('toy');
  const product = getProducts()[selected];
  return <>
    <div className={styles.productTabs} role="tablist" aria-label={translate({id: 'homepage.products.tabs.ariaLabel', message: '选择硬件品类', description: '产品场景页签组的无障碍标签'})}>
      {productLabels.map(([key, getLabel], index) => {
        const label = getLabel();
        return <button className={selected === key ? styles.selectedTab : ''} key={key} role="tab" aria-selected={selected === key} onClick={() => setSelected(key)}><span>{label}</span><small>0{index + 1}</small></button>;
      })}
    </div>
    <div className={styles.composer}>
      <div><div className={styles.composerHead}><span><Translate id="homepage.products.capabilitySelection" description="能力选择面板标题">能力选择</Translate></span><small><Translate id="homepage.products.combinationNote" description="能力选择面板说明">按场景组合示意</Translate></small></div>
        <div className={styles.capabilities}>{product.capabilities.map(([type, name, status], index) => <div className={styles.capability} key={index}><span>{type}</span><strong>{name}</strong><small className={status === 'included' ? styles.activeCapability : ''}>{capabilityStatusLabels[status]()}</small></div>)}</div>
        <p className={styles.composerFoot}>{product.hardware}</p>
      </div>
      <div className={styles.experience}><div className={styles.experienceTop}><span><Translate id="homepage.products.experience" description="产品体验面板标题">产品体验</Translate></span><span><Translate id="homepage.products.scenarioFlow" description="场景流程面板标题">场景流程</Translate></span></div><div className={styles.experienceBody}><h3>{product.title}</h3><p>{product.description}</p><div className={styles.prompt}>{product.prompt}</div><ol>{product.steps.map((step, index) => <li key={index}><span>{index + 1}</span>{step}</li>)}</ol></div></div>
    </div>
  </>;
}

function CodeExample() {
  const command = `${translate({id: 'homepage.code.cloneComment', message: '# 获取源码与子模块', description: '终端示例中获取源码命令前的注释'})}
git clone https://github.com/tuya/agentic-kit.git
cd agentic-kit
git submodule update --init --recursive

${translate({id: 'homepage.code.runComment', message: '# 编译并运行 POSIX 文本对话示例', description: '终端示例中构建命令前的注释'})}
cmake -S examples/posix -B build-examples
cmake --build build-examples
./build-examples/text_chat_demo`;
  const [copyState, setCopyState] = useState<'idle' | 'copied' | 'failed'>('idle');
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(command);
      setCopyState('copied');
      window.setTimeout(() => setCopyState('idle'), 2500);
    } catch {
      setCopyState('failed');
    }
  };
  const copyLabel = copyState === 'copied'
    ? translate({id: 'homepage.code.copySuccessButton', message: '已复制', description: '命令复制成功后的按钮文字'})
    : copyState === 'failed'
      ? translate({id: 'homepage.code.copyFailureButton', message: '复制失败', description: '命令复制失败后的按钮文字'})
      : translate({id: 'homepage.code.copyButton', message: '复制命令', description: '复制终端命令的按钮文字'});
  const note = copyState === 'copied'
    ? translate({id: 'homepage.code.copySuccessNote', message: '命令已复制。桌面验证完成后，再按目标系统完成硬件适配。', description: '命令复制成功后的说明'})
    : copyState === 'failed'
      ? translate({id: 'homepage.code.copyFailureNote', message: '无法复制命令，请手动选择并复制终端内容。', description: '命令复制失败后的说明'})
      : translate({id: 'homepage.code.defaultNote', message: '桌面示例用于验证交互链路；接入产品时，再按目标系统适配平台、外设与设备身份。', description: '终端示例默认说明'});
  return <div><div className={styles.terminal}><div className={styles.terminalHead}><span><Translate id="homepage.code.terminalTitle" description="终端示例标题">终端 · macOS / Linux</Translate></span><button type="button" onClick={copy}>{copyLabel}</button></div><pre><code>{command}</code></pre><div className={styles.terminalFoot}><Translate id="homepage.code.requirements" description="终端示例构建环境要求">CMake ≥ 3.20 / Python 3 / C 与 C++ 工具链</Translate></div></div><p className={styles.terminalNote}>{note}</p></div>;
}

export default function Home(): React.JSX.Element {
  const {i18n: {currentLocale}} = useDocusaurusContext();
  const tuyaOsUrl = currentLocale === 'en' ? 'https://www.tuya.com/platform/productdev/tuyaos' : 'https://www.tuya.com/cn/platform/productdev/tuyaos';
  const metrics = [
    [translate({id: 'homepage.metrics.sdk.value', message: '约 120 KB', description: 'SDK 体积指标值'}), translate({id: 'homepage.metrics.sdk.label', message: 'SDK 体积', description: 'SDK 体积指标名称'}), translate({id: 'homepage.metrics.sdk.detail', message: '低算力设备也能接入', description: 'SDK 体积指标说明'})],
    [translate({id: 'homepage.metrics.memory.value', message: '约 60 KB', description: '运行内存指标值'}), translate({id: 'homepage.metrics.memory.label', message: '运行内存', description: '运行内存指标名称'}), translate({id: 'homepage.metrics.memory.detail', message: '为业务逻辑保留资源', description: '运行内存指标说明'})],
    [translate({id: 'homepage.metrics.loss.value', message: '30-70%', description: '弱网丢包指标值'}), translate({id: 'homepage.metrics.loss.label', message: '弱网丢包可用', description: '弱网丢包指标名称'}), translate({id: 'homepage.metrics.loss.detail', message: 'T-RTC 抗弱网能力', description: '弱网丢包指标说明'})],
    [translate({id: 'homepage.metrics.latency.value', message: '不高于 86 ms', description: '通信时延指标值'}), translate({id: 'homepage.metrics.latency.label', message: '全球平均通信时延', description: '通信时延指标名称'}), translate({id: 'homepage.metrics.latency.detail', message: '端云实时传输', description: '通信时延指标说明'})],
    [translate({id: 'homepage.metrics.voice.value', message: '约 1.5 s', description: '语音时延指标值'}), translate({id: 'homepage.metrics.voice.label', message: '语音端到端', description: '语音时延指标名称'}), translate({id: 'homepage.metrics.voice.detail', message: '含长记忆与知识库', description: '语音时延指标说明'})],
  ];
  const roles = [
    {tag: translate({id: 'homepage.roles.endpoint.tag', message: '你的 AI 终端', description: 'AI 终端角色标签'}), title: translate({id: 'homepage.roles.endpoint.title', message: '你的 AI 硬件终端', description: 'AI 终端角色标题'}), entries: [translate({id: 'homepage.roles.endpoint.chip', message: '芯片、BSP 与外设驱动', description: 'AI 终端职责'}), translate({id: 'homepage.roles.endpoint.capture', message: '麦克风 / 摄像头采集', description: 'AI 终端职责'}), translate({id: 'homepage.roles.endpoint.output', message: '扬声器 / 屏幕 / 执行器', description: 'AI 终端职责'}), translate({id: 'homepage.roles.endpoint.logic', message: '自有固件与产品逻辑', description: 'AI 终端职责'})]},
    {tag: translate({id: 'homepage.roles.kit.tag', message: 'AGENTIC-KIT', description: 'Agentic-Kit 角色标签'}), title: translate({id: 'homepage.roles.kit.title', message: '端云实时交互套件', description: 'Agentic-Kit 角色标题'}), entries: [translate({id: 'homepage.roles.kit.provisioning', message: '配网、激活与鉴权', description: 'Agentic-Kit 职责'}), translate({id: 'homepage.roles.kit.transport', message: '实时多模态数据传输', description: 'Agentic-Kit 职责'}), translate({id: 'homepage.roles.kit.session', message: '会话事件与回调', description: 'Agentic-Kit 职责'}), translate({id: 'homepage.roles.kit.mcp', message: '设备侧 MCP 命令交互', description: 'Agentic-Kit 职责'})]},
    {tag: translate({id: 'homepage.roles.platform.tag', message: 'TUYA PHYSICAL AI', description: 'Tuya Physical AI 角色标签'}), title: translate({id: 'homepage.roles.platform.title', message: 'Tuya Physical AI 平台', description: 'Tuya Physical AI 角色标题'}), entries: [translate({id: 'homepage.roles.platform.perception', message: '语音与视觉感知算法', description: 'Tuya Physical AI 职责'}), translate({id: 'homepage.roles.platform.models', message: '大模型、Agent 与记忆', description: 'Tuya Physical AI 职责'}), translate({id: 'homepage.roles.platform.services', message: '设备控制与场景服务', description: 'Tuya Physical AI 职责'}), translate({id: 'homepage.roles.platform.operations', message: '全球连接与持续运营', description: 'Tuya Physical AI 职责'})]},
  ];
  const embedded = [
    [translate({id: 'homepage.embedded.cloud.title', message: '把重智能放在云端，把端侧做轻', description: '嵌入式设计原则标题'}), translate({id: 'homepage.embedded.cloud.text', message: '端侧保留采集、播放、协议与本地控制，复杂推理按需交给云端。在有限 RAM 与 Flash 下，也能构建完整 AI 体验。', description: '嵌入式设计原则说明'}), translate({id: 'homepage.embedded.cloud.code', message: '轻量端侧 / 云端智能', description: '嵌入式设计原则关键词'})],
    [translate({id: 'homepage.embedded.pal.title', message: '用适配层隔离芯片差异', description: '嵌入式设计原则标题'}), translate({id: 'homepage.embedded.pal.text', message: 'PAL 将网络、线程、时间与存储等平台能力抽象出来。更换芯片或操作系统时，业务逻辑无需随底层一起重写。', description: '嵌入式设计原则说明'}), translate({id: 'homepage.embedded.pal.code', message: 'PAL / 一次移植 / 保留业务逻辑', description: '嵌入式设计原则关键词'})],
    [translate({id: 'homepage.embedded.network.title', message: '弱网下，交互仍要连续', description: '嵌入式设计原则标题'}), translate({id: 'homepage.embedded.network.text', message: '围绕音频、图像、视频、文字与事件建立实时链路，并处理流式传输、打断、重连与网络波动。', description: '嵌入式设计原则说明'}), translate({id: 'homepage.embedded.network.code', message: '流式传输 / 打断 / 重连', description: '嵌入式设计原则关键词'})],
    [translate({id: 'homepage.embedded.execution.title', message: '物理动作，由端侧把住执行边界', description: '嵌入式设计原则标题'}), translate({id: 'homepage.embedded.execution.text', message: 'AI 通过 MCP 下发工具意图，由本地固件决定是否执行、完成动作并回传结果。', description: '嵌入式设计原则说明'}), translate({id: 'homepage.embedded.execution.code', message: 'MCP / 本地决策 / 执行结果', description: '嵌入式设计原则关键词'})],
  ];
  const algorithms = [
    [translate({id: 'homepage.algorithms.device.tag', message: '01 / 端侧感知', description: '算法能力标签'}), translate({id: 'homepage.algorithms.device.title', message: '端侧感知，从硬件条件出发。', description: '算法能力标题'}), translate({id: 'homepage.algorithms.device.text', message: '关键词唤醒与视觉预处理可由端侧方案提供；VAD 可按产品需要选择端侧或云端。', description: '算法能力说明'}), translate({id: 'homepage.algorithms.device.chips', message: 'KWS · VAD · 视觉预处理', description: '算法能力关键词'})],
    [translate({id: 'homepage.algorithms.cloud.tag', message: '02 / 涂鸦云端算法', description: '算法能力标签'}), translate({id: 'homepage.algorithms.cloud.title', message: '云端算法，持续优化产品体验。', description: '算法能力标题'}), translate({id: 'homepage.algorithms.cloud.text', message: '音频、图像与视频通过实时链路连接云端算法，获得文字、语音或结构化结果。', description: '算法能力说明'}), translate({id: 'homepage.algorithms.cloud.chips', message: 'Nomo-ASR · Nomo-PVAD · Pet ID', description: '算法能力关键词'})],
    [translate({id: 'homepage.algorithms.vertical.tag', message: '03 / 垂直模型', description: '算法能力标签'}), translate({id: 'homepage.algorithms.vertical.title', message: '垂直算法，更懂真实世界。', description: '算法能力标题'}), translate({id: 'homepage.algorithms.vertical.text', message: '围绕远场拾音、多人干扰、宠物个体与复杂画面等真实设备问题持续优化。', description: '算法能力说明'}), translate({id: 'homepage.algorithms.vertical.chips', message: '语音模型 · 声音模型 · 视觉模型', description: '算法能力关键词'})],
  ];
  const choices = [
    {tag: translate({id: 'homepage.choices.tuyaos.tag', message: '标准化 / 商业产品底座', description: 'TuyaOS 定位标签'}), title: translate({id: 'homepage.choices.tuyaos.title', message: 'TuyaOS（Wukong）', description: 'TuyaOS 产品名称'}), text: translate({id: 'homepage.choices.tuyaos.text', message: '采用涂鸦标准化产品框架，与连接、安全、设备模型和量产体系协同。', description: 'TuyaOS 说明'}), linkLabel: translate({id: 'homepage.choices.tuyaos.link', message: '了解 TuyaOS（Wukong）', description: 'TuyaOS 链接文字'}), link: tuyaOsUrl},
    {tag: translate({id: 'homepage.choices.tuyaopen.tag', message: '标准化 / 开源技术栈', description: 'TuyaOpen 定位标签'}), title: translate({id: 'homepage.choices.tuyaopen.title', message: 'TuyaOpen', description: 'TuyaOpen 产品名称'}), text: translate({id: 'homepage.choices.tuyaopen.text', message: '采用涂鸦标准化分层框架，同时开放源码并封装丰富的 AI 与 IoT 组件。', description: 'TuyaOpen 说明'}), linkLabel: translate({id: 'homepage.choices.tuyaopen.link', message: '了解 TuyaOpen', description: 'TuyaOpen 链接文字'}), link: 'https://www.tuyaopen.ai/'},
    {tag: translate({id: 'homepage.choices.agenticKit.tag', message: '原子 SDK / 适配你的技术栈', description: 'Agentic-Kit 定位标签'}), title: translate({id: 'homepage.choices.agenticKit.title', message: 'Agentic-Kit', description: 'Agentic-Kit 产品名称'}), text: translate({id: 'homepage.choices.agenticKit.text', message: '不改变客户已有技术栈和方案选型，按需选用，面向资源更紧或需要深度定制的产品。', description: 'Agentic-Kit 说明'}), linkLabel: translate({id: 'homepage.choices.agenticKit.link', message: '了解 Agentic-Kit', description: 'Agentic-Kit 链接文字'}), link: '/docs/intro'},
  ];

  return <Layout title={translate({id: 'homepage.meta.title', message: 'Agentic-Kit', description: '首页元数据标题'})} description={translate({id: 'homepage.meta.description', message: '面向 AI 硬件的轻量端云实时交互套件', description: '首页元数据描述'})}>
    <main>
      <section className={styles.hero}><div className={styles.wrap}><div className={styles.heroTop}><Eyebrow><Translate id="homepage.hero.eyebrow" description="首页首屏眉题">为 Physical AI 而生</Translate></Eyebrow><span><Translate id="homepage.hero.infrastructure" description="首页首屏平台定位">涂鸦 / 嵌入式 AI 基础设施</Translate></span></div><div className={styles.heroGrid}><div className={styles.heroCopy}><h1><Translate id="homepage.hero.title.lineOne" description="首页主标题第一行">让 AI，</Translate><br /><Translate id="homepage.hero.title.lineTwo" description="首页主标题第二行">连接</Translate><span><Translate id="homepage.hero.title.highlight" description="首页主标题强调文字">物理世界。</Translate></span></h1><p><strong><Translate id="homepage.hero.summary" description="首页首屏产品摘要">Agentic-Kit 是面向 Physical AI 的原子级轻量端侧 SDK。</Translate></strong><br /><Translate id="homepage.hero.description" description="首页首屏产品描述">覆盖低算力 MCU、Linux SoC、PC、手机 App 与浏览器，从语音玩具到具身机器人，通过一套接入体系实现端云实时交互，并按需连接感知算法、Agent、记忆与模型。</Translate></p><div className={styles.actions}><Link className={styles.primaryButton} to="/docs/tutorials/quick-start"><Translate id="homepage.hero.startCta" description="首页首屏快速开始按钮">接入你的硬件</Translate> <Arrow /></Link><a className={styles.secondaryButton} href="#composition"><Translate id="homepage.hero.exploreCta" description="首页首屏能力组合按钮">探索能力组合</Translate> <span aria-hidden="true">↓</span></a></div><small><Translate id="homepage.hero.features" description="首页首屏产品特性">原子级能力开放 · 芯片无关 · 云与模型无关 · 零业务侵入 · 按需选用</Translate></small></div><NetworkDiagram /></div><div className={styles.metrics}>{metrics.map(([value, label, detail], index) => <div className={index > 1 ? styles.platformMetric : ''} key={index}><strong>{value}</strong><span>{label}</span><small>{detail}</small></div>)}</div><p className={styles.metricNote}><Translate id="homepage.metrics.note" description="指标免责声明">典型指标；实际表现受芯片、功能配置、网络与产品方案影响。</Translate></p></div></section>

      <section className={`${styles.section} ${styles.white}`}><div className={styles.wrap}><Eyebrow><Translate id="homepage.foundation.eyebrow" description="平台能力章节眉题">01 / 一套套件，一整套底座</Translate></Eyebrow><div className={styles.sectionHead}><h2><Translate id="homepage.foundation.title.lineOne" description="平台能力章节标题第一行">原子能力开放，</Translate><br /><Translate id="homepage.foundation.title.lineTwo" description="平台能力章节标题第二行">释放你的创新力。</Translate></h2><p><Translate id="homepage.foundation.description" description="平台能力章节描述">Agentic-Kit 是 Tuya Physical AI 创新平台面向端侧的开放入口。平台为 AI 创新伙伴提供研发、量产与全球增长的一站式支持。</Translate></p></div><div className={styles.roles}>{roles.map((role, index) => <article className={index === 1 ? styles.roleAccent : ''} key={index}><small>{role.tag}</small><h3>{role.title}</h3><ul>{role.entries.map((entry, entryIndex) => <li key={entryIndex}>{entry}</li>)}</ul></article>)}</div><p className={styles.scopeNote}><strong><Translate id="homepage.foundation.scope.title" description="平台能力边界说明标题">按需组合，边界清晰。</Translate></strong> <Translate id="homepage.foundation.scope.text" description="平台能力边界说明">平台模型、算法、记忆与行业服务按需选用；客户核心云服务也可通过开放能力接入。</Translate></p></div></section>

      <section className={styles.section} id="embedded"><div className={styles.wrap}><Eyebrow><Translate id="homepage.embedded.eyebrow" description="嵌入式设计章节眉题">02 / 围绕嵌入式约束设计</Translate></Eyebrow><div className={styles.sectionHead}><h2><Translate id="homepage.embedded.title.lineOne" description="嵌入式设计章节标题第一行">懂 AI，</Translate><br /><Translate id="homepage.embedded.title.lineTwo" description="嵌入式设计章节标题第二行">也懂硬件的约束。</Translate></h2><p><Translate id="homepage.embedded.description" description="嵌入式设计章节描述">真正的端侧能力，不止是能接入，还要在有限资源、异构芯片与波动网络中保持可用。</Translate></p></div><div className={styles.embedded}>{embedded.map(([title, text, code], index) => <article key={index}><span>/ 0{index + 1}</span><div><h3>{title}</h3><p>{text}</p><code>{code}</code></div></article>)}</div><div className={styles.platforms}><span><Translate id="homepage.embedded.platforms.label" description="已支持平台标签">已提供的平台与适配路径</Translate></span><div><Translate id="homepage.embedded.platforms.list" description="已支持平台列表">ESP32 / ESP-IDF · FreeRTOS · ARM · MIPS · Linux</Translate></div></div></div></section>

      <section className={`${styles.section} ${styles.composition}`} id="composition"><div className={styles.wrap}><Eyebrow><Translate id="homepage.composition.eyebrow" description="能力组合章节眉题">03 / 组合产品所需的智能</Translate></Eyebrow><div className={styles.sectionHead}><h2><Translate id="homepage.composition.title.lineOne" description="能力组合章节标题第一行">同一套底座，</Translate><br /><Translate id="homepage.composition.title.lineTwo" description="能力组合章节标题第二行">组合出不同的 AI 硬件。</Translate></h2><p><Translate id="homepage.composition.description" description="能力组合章节描述">从陪伴、学习与效率终端，到具身机器人。对话、感知、记忆、执行与设备生态能力，都可以围绕产品场景组合。</Translate></p></div><ProductComposer /><p className={styles.compositionNote}><strong><Translate id="homepage.composition.note.title" description="能力组合边界说明标题">组合的是平台能力，运行的是你的产品。</Translate></strong> <Translate id="homepage.composition.note.text" description="能力组合边界说明">Agentic-Kit 提供连接和数据交互，算法、记忆与行业服务需按产品方案配置。</Translate></p></div></section>

      <section className={`${styles.section} ${styles.white}`}><div className={styles.wrap}><Eyebrow><Translate id="homepage.algorithms.eyebrow" description="感知算法章节眉题">04 / 面向真实设备的感知</Translate></Eyebrow><div className={styles.sectionHead}><h2><Translate id="homepage.algorithms.title.lineOne" description="感知算法章节标题第一行">算法的价值，</Translate><br /><Translate id="homepage.algorithms.title.lineTwo" description="感知算法章节标题第二行">要在真实设备上体现。</Translate></h2><p><Translate id="homepage.algorithms.description" description="感知算法章节描述">Physical AI 的体验，来自端侧感知、云端算法与真实硬件协同。Agentic-Kit 让算法按产品需要部署和组合。</Translate></p></div><div className={styles.algorithms}>{algorithms.map(([tag, title, text, chips], index) => <article key={index}><small>{tag}</small><h3>{title}</h3><p>{text}</p><span>{chips}</span></article>)}</div></div></section>

      <section className={`${styles.section} ${styles.white}`} id="start"><div className={styles.wrap}><Eyebrow><Translate id="homepage.start.eyebrow" description="快速开始章节眉题">05 / 你好，物理世界</Translate></Eyebrow><div className={styles.startGrid}><div><h2><Translate id="homepage.start.title.lineOne" description="快速开始章节标题第一行">先跑起来。</Translate><br /><Translate id="homepage.start.title.lineTwo" description="快速开始章节标题第二行">再做成你的产品。</Translate></h2><p><Translate id="homepage.start.description" description="快速开始章节描述">无需先选定芯片或准备开发板。在 macOS 或 Linux 上运行文本对话示例，先验证端云链路，再把确认过的交互带到你的硬件。</Translate></p><ol className={styles.startSteps}><li><span>01</span><div><strong><Translate id="homepage.start.steps.desktop.title" description="快速开始第一步标题">在电脑上验证</Translate></strong><br /><Translate id="homepage.start.steps.desktop.text" description="快速开始第一步说明">获取源码，运行 POSIX 示例</Translate></div></li><li><span>02</span><div><strong><Translate id="homepage.start.steps.hardware.title" description="快速开始第二步标题">带入目标硬件</Translate></strong><br /><Translate id="homepage.start.steps.hardware.text" description="快速开始第二步说明">适配平台、音视频与本地工具</Translate></div></li><li><span>03</span><div><strong><Translate id="homepage.start.steps.product.title" description="快速开始第三步标题">走向产品与量产</Translate></strong><br /><Translate id="homepage.start.steps.product.text" description="快速开始第三步说明">配置 PID、设备身份与产品能力</Translate></div></li></ol><Link className={styles.primaryButton} to="/docs/tutorials/quick-start"><Translate id="homepage.start.guideCta" description="完整接入指南按钮">打开完整接入指南</Translate> <Arrow /></Link></div><CodeExample /></div><div className={styles.sdkLinks}><Link to="/docs/reference/rtc-tcp-client"><strong>RTC TCP Client -&gt;</strong><span><Translate id="homepage.start.rtcTcp.description" description="RTC TCP Client 链接说明">源码级集成 · PAL 移植</Translate></span></Link><Link to="/docs/reference/rtc-client"><strong>RTC Client -&gt;</strong><span><Translate id="homepage.start.rtc.description" description="RTC Client 链接说明">预编译库集成 · 目标平台匹配</Translate></span></Link></div></div></section>

      <section className={`${styles.section} ${styles.choices}`} id="openness"><div className={styles.wrap}><Eyebrow><Translate id="homepage.choices.eyebrow" description="技术选型章节眉题">06 / 你的 AI，你的方式</Translate></Eyebrow><div className={styles.sectionHead}><h2><Translate id="homepage.choices.title.lineOne" description="技术选型章节标题第一行">从标准化快速开发，</Translate><br /><Translate id="homepage.choices.title.lineTwo" description="技术选型章节标题第二行">到原子级灵活接入。</Translate></h2><p><Translate id="homepage.choices.description" description="技术选型章节描述">TuyaOS、TuyaOpen 与 Agentic-Kit 分别提供不同的端侧框架和接入方式，适配不同的项目阶段与技术栈。</Translate></p></div><div className={styles.choiceGrid}>{choices.map((choice, index) => <article className={index === 2 ? styles.featuredChoice : ''} key={index}><small>{choice.tag}</small><h3>{choice.title}</h3><p>{choice.text}</p><ul><li><Translate id="homepage.choices.benefit.framework" description="技术选型共同优势">匹配目标项目的开发框架</Translate></li><li><Translate id="homepage.choices.benefit.boundary" description="技术选型共同优势">按产品需求选择能力边界</Translate></li><li><Translate id="homepage.choices.benefit.production" description="技术选型共同优势">支持商业化产品开发与量产</Translate></li></ul>{choice.link.startsWith('/') ? <Link to={choice.link}>{choice.linkLabel} <Arrow /></Link> : <a href={choice.link} target="_blank" rel="noreferrer">{choice.linkLabel} <Arrow /></a>}</article>)}</div></div></section>
      <section className={styles.closing}><div className={styles.wrap}><div><h2><Translate id="homepage.closing.title" description="首页结尾行动标题">你的下一款 AI 硬件，从这里开始。</Translate></h2><p><Translate id="homepage.closing.description" description="首页结尾行动说明">从嵌入式接入，到按需组合平台能力。</Translate></p></div><Link className={styles.closingButton} to="/docs/intro"><Translate id="homepage.closing.cta" description="首页结尾行动按钮">了解 Agentic-Kit</Translate> <Arrow /></Link></div></section>
    </main>
  </Layout>;
}
