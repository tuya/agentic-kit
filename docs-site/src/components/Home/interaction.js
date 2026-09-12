// Interaction behavior ported from new-doc-demo/index-d.html.
export function mountHome(root) {
  const controller = new AbortController();
  const $ = id => root.querySelector('#' + id);
  const products = {
    toy: {
      title: '会交流的玩具，有自己的性格。',
      description: '从听清孩子的话，到生成角色回应，再让语音与设备动作一起表达。',
      input: '“今天想听一个去月球的故事。”',
      hardware: '硬件侧：麦克风、扬声器与状态灯；固件负责采集、播放与本地反馈。',
      caps: [['感知算法', 'VAD / PVAD / ASR', '组合'], ['声音表达', 'TTS / 角色音色', '组合'], ['云端智能', 'LLM / 角色设定', '组合'], ['记忆与知识', '偏好记忆 / 故事内容', '按需'], ['本地工具', '状态灯 / 表情反馈', '按需'], ['自有业务', '角色 IP / 内容服务', '自主']],
      steps: ['设备采集语音，经 SDK 上传。', '云端识别语音，按角色生成故事与语音。', '设备播放回复，固件驱动状态灯。'],
      complete: '示意完成 · 语音与灯效已响应'
    },
    camera: {
      title: '按下快门，开始一次探索。',
      description: '围绕拍照识物组合视觉理解与语音讲解，让摄像头、屏幕和扬声器一起工作。',
      input: '“这片叶子为什么会变黄？”',
      hardware: '硬件侧：摄像头、屏幕与扬声器；固件负责拍照、上传、显示与播放。',
      caps: [['感知算法', '物体识别 / 视觉理解', '组合'], ['声音表达', 'TTS / 讲解音色', '组合'], ['云端智能', '多模态模型 / 讲解', '组合'], ['记忆与知识', '学习记录 / 内容知识', '按需'], ['本地工具', '拍照 / 屏幕 / 播放', '组合'], ['自有业务', '教材 / 课程 / 账户', '自主']],
      steps: ['设备拍摄并通过 SDK 上传图片。', '云端理解画面，生成适合场景的讲解。', '屏幕展示结果，扬声器播放讲解。'],
      complete: '示意完成 · 图文与语音已返回'
    },
    pet: {
      title: '看见宠物，也知道它是谁。',
      description: '将宠物个体识别与视觉理解组合，围绕实际画面建立宠物记录和用户提醒。',
      input: '画面事件：“奶糖来到喂食区。”',
      hardware: '硬件侧：摄像头与网络连接；固件采集画面，业务系统负责事件记录与通知。',
      caps: [['感知算法', '宠物个体识别 / 视觉理解', '组合'], ['声音表达', '语音播报', '按需'], ['云端智能', '事件理解 / 摘要', '按需'], ['记忆与知识', '宠物档案 / 事件记录', '组合'], ['本地工具', '画面采集 / 设备反馈', '组合'], ['自有业务', '家庭账户 / 通知服务', '自主']],
      steps: ['设备采集画面，按方案提交视觉服务。', '识别宠物个体，生成结构化事件。', '业务系统记录事件，向用户发送提醒。'],
      complete: '示意完成 · 宠物事件已记录'
    },
    assistant: {
      title: '听懂一句话，落到设备动作。',
      description: '从语音理解到工具执行，让空间助手操作产品已授权、已接入的设备功能。',
      input: '“打开阅读灯，把亮度调到 60%。”',
      hardware: '硬件侧：语音入口与本地工具；跨设备控制另需配置设备绑定、权限与平台服务。',
      caps: [['感知算法', 'ASR / 设备名称热词', '组合'], ['声音表达', 'TTS / 执行回执', '组合'], ['云端智能', 'Agent / 物理执行能力', '组合'], ['记忆与知识', '设备上下文 / 使用偏好', '按需'], ['本地工具', 'MCP / 已授权设备动作', '组合'], ['自有业务', '空间场景 / 自动化规则', '自主']],
      steps: ['设备上传语音，AI 理解设备与动作。', '匹配已授权工具，生成亮度控制参数。', '本地执行并回传结果，播报完成。'],
      complete: '示意完成 · 阅读灯亮度 60%'
    },
    efficiency: {
      title: '把会议变成下一步行动。',
      description: '让桌面终端或随身设备完成采集、理解、知识检索与任务整理，把 AI 融入工作现场。',
      input: '“整理刚才的会议，列出负责人和截止时间。”',
      hardware: '终端侧：麦克风、屏幕或摄像头；可按需连接企业知识库、业务工具与客户自有服务。',
      caps: [['感知算法', 'ASR / 说话人识别', '组合'], ['声音表达', 'TTS / 语音提醒', '按需'], ['云端智能', 'Agent / 总结与规划', '组合'], ['记忆与知识', '会议上下文 / 企业知识', '组合'], ['本地工具', '录音 / 屏幕 / 文件', '组合'], ['自有业务', '日历 / 任务 / 工作流', '自主']],
      steps: ['终端采集会议内容并形成结构化上下文。', 'Agent 结合知识与任务规则提取行动项。', '通过开放能力写入客户工作流，等待用户确认。'],
      complete: '示意完成 · 行动项已生成'
    },
    embodied: {
      title: '让机器人理解人，也连接整个家。',
      description: '将机器人的感知与行动能力，同已接入、已授权的涂鸦家庭设备组合，用于养老陪护、家庭巡检与生活协助。',
      input: '“奶奶准备休息了，帮她关窗帘、开夜灯，再检查一下门锁。”',
      hardware: '机器人侧：麦克风、摄像头、移动与执行机构；家庭设备控制需完成设备接入、家庭绑定和用户授权。',
      caps: [['感知算法', '语音 / 视觉 / 环境感知', '组合'], ['声音表达', 'TTS / 陪伴式交互', '组合'], ['云端智能', 'Agent / 任务规划', '组合'], ['记忆与知识', '家庭上下文 / 照护偏好', '按需'], ['设备生态', '已授权设备 / 场景联动', '组合'], ['本体能力', '移动 / 抓取 / 巡检', '自主']],
      steps: ['机器人理解照护意图与当前家庭环境。', 'Agent 规划任务，调用已授权的设备与本体能力。', '窗帘、夜灯和门锁返回状态，机器人向用户确认。'],
      complete: '示意完成 · 家庭照护任务已确认'
    }
  };
  let product = 'toy',
    copyTimer,
    copyGeneration = 0;
  const listen = (target, type, handler) => target.addEventListener(type, handler, {
    signal: controller.signal
  });
  const productTabs = [...root.querySelectorAll('.product-tab')];
  function renderProduct(key) {
    product = key;
    const data = products[key];
    productTabs.forEach(tab => {
      const active = tab.dataset.product === key;
      tab.setAttribute('aria-selected', String(active));
      tab.tabIndex = active ? 0 : -1;
    });
    $('product-panel').setAttribute('aria-labelledby', 'product-' + key);
    $('product-title').textContent = data.title;
    $('product-description').textContent = data.description;
    $('product-input').textContent = data.input;
    $('hardware-needs').textContent = data.hardware;
    $('capability-rows').replaceChildren();
    data.caps.forEach(([type, name, status]) => {
      const row = document.createElement('div');
      row.className = 'cap-row ' + (status === '组合' ? 'active' : 'optional');
      [['cap-type', type], ['cap-name', name], ['cap-tag', status]].forEach(([cls, text]) => {
        const el = document.createElement('span');
        el.className = cls;
        el.textContent = text;
        row.append(el);
      });
      $('capability-rows').append(row);
    });
    $('activity-list').replaceChildren();
    data.steps.forEach((text, index) => {
      const li = document.createElement('li'),
        num = document.createElement('span'),
        label = document.createElement('span');
      num.className = 'step-number';
      num.textContent = index + 1;
      label.textContent = text;
      li.append(num, label);
      $('activity-list').append(li);
    });
  }
  function wireTabs(tabs, attribute, render) {
    tabs.forEach((tab, index) => {
      listen(tab, 'click', () => render(tab.dataset[attribute]));
      listen(tab, 'keydown', event => {
        let next;
        if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;else if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;else if (event.key === 'Home') next = 0;else if (event.key === 'End') next = tabs.length - 1;else return;
        event.preventDefault();
        tabs[next].focus();
        render(tabs[next].dataset[attribute]);
      });
    });
  }
  wireTabs(productTabs, 'product', renderProduct);
  listen($('copy-command'), 'click', async () => {
    const text = $('command-code').textContent,
      version = ++copyGeneration;
    let copied = false;
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
        copied = true;
      }
    } catch {}
    if (!copied) {
      const area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.cssText = 'position:fixed;top:0;left:-9999px';
      document.body.append(area);
      area.select();
      try {
        copied = document.execCommand('copy');
      } catch {}
      area.remove();
      $('copy-command').focus();
    }
    if (version !== copyGeneration) return;
    $('copy-command').textContent = copied ? '已复制' : '手动复制';
    $('terminal-note').textContent = copied ? '命令已复制。桌面验证完成后，再按目标系统完成硬件适配。' : '浏览器未允许复制，请选中上方命令手动复制。';
    clearTimeout(copyTimer);
    copyTimer = setTimeout(() => {
      $('copy-command').textContent = '复制命令';
    }, 2500);
  });
  const menu = root.querySelector('.menu'),
    nav = $('nav-links');
  function closeMenu() {
    nav.classList.remove('open');
    menu.setAttribute('aria-expanded', 'false');
    menu.setAttribute('aria-label', '展开导航');
  }
  listen(menu, 'click', () => {
    const active = nav.classList.toggle('open');
    menu.setAttribute('aria-expanded', String(active));
    menu.setAttribute('aria-label', active ? '收起导航' : '展开导航');
  });
  nav.querySelectorAll('a').forEach(a => listen(a, 'click', closeMenu));
  listen(root, 'keydown', e => {
    if (e.key === 'Escape' && nav.classList.contains('open')) {
      closeMenu();
      menu.focus();
    }
  });
  renderProduct('toy');
  return () => {
    controller.abort();
    copyGeneration++;
    clearTimeout(copyTimer);
  };
}
