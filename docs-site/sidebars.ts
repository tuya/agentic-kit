import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  docs: [
    'intro',
    'architecture',
    {
      type: 'category',
      label: '教程',
      items: [
        'tutorials/quick-start',
        {
          type: 'category',
          label: '配网',
          items: [
            'tutorials/scan-by-device',
            'tutorials/scan-by-app',
            'tutorials/openapi-activate',
            'tutorials/pair-by-ble',
          ],
        },
        {
          type: 'category',
          label: 'AI功能',
          items: [
            'tutorials/chat',
            'tutorials/edu-camera',
          ],
        },
      ],
    },
    {
      type: 'category',
      label: '用户指南',
      items: [
        {
          type: 'category',
          label: '云端配置',
          items: [
            'guides/create-agent',
            'guides/create-workflow',
          ],
        },
        {
          type: 'category',
          label: '端侧处理',
          items: [
            'guides/audio-format',
            'guides/vad-and-interrupt',
            'guides/porting-to-new-platform',
          ],
        },
      ],
    },
    {
      type: 'category',
      label: 'SDK 参考',
      items: [
        'reference/rtc-tcp-client',
        'reference/rtc-client',
        'reference/iot-client',
      ],
    },
    'faq',
  ],
};

export default sidebars;
