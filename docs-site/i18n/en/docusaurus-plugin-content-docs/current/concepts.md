---
title: Core Concepts
sidebar_label: Core Concepts
sidebar_position: 2
slug: /concepts
---

# Core Concepts

<div className="doc-lead">Understand the relationships among identity, connections, Sessions, and capability calls before moving on to specific APIs. Agentic-kit is designed to keep devices lightweight while placing platform capabilities and customer-owned capabilities behind clear, replaceable boundaries.</div>

## Identity and Activation {#身份与激活}

<div className="doc-terms">
  <div><div><strong>Authorization code</strong><small>LICENSE</small></div><section><p>The identity credential held by each device at the factory, consisting of a globally unique <code>uuid</code> and its paired <code>authkey</code>. It proves that the device is authorized for Activation; it is not the same as the device ID obtained after Activation.</p><code>Factory identity: uuid + authkey -&gt; successful Activation -&gt; official cloud identity: devid + secret_key + local_key</code></section></div>
  <div><div><strong>Product PID</strong><small>PRODUCT ID</small></div><section><p>Identifies the shared configuration for a product family. A PID can define Data Points, a panel, and a default AI Agent. Devices of the same product share a PID but have different authorization codes and device IDs.</p></section></div>
  <div><div><strong>Device Activation</strong><small>ACTIVATION</small></div><section><p>The process of moving a device from its factory state to being networked, bound to a user, and registered with the cloud. Activation is normally performed only once. The device should securely persist the returned credentials and use them directly on subsequent starts.</p></section></div>
</div>

## Agents and Sessions {#agent-与会话}

<div className="doc-terms">
  <div><div><strong>AI Agent</strong><small>BEHAVIOR</small></div><section><p>Defines how the product understands, decides, and expresses itself. On the platform, you can configure the system prompt, language, TTS, workflows, and callable tools. A product can use a default Agent or switch Agents according to project configuration.</p><code>A PID answers "What product is this?" An Agent answers "How does this product communicate and act?"</code></section></div>
  <div><div><strong>Session</strong><small>SESSION</small></div><section><p>The runtime context for one continuous AI interaction. It contains connection state, input and output streams, callback events, and optional Session attributes. The SDK carries Session data; the application and platform configuration jointly define the specific business semantics.</p></section></div>
  <div><div><strong>Agent Core</strong><small>PLATFORM RUNTIME</small></div><section><p>The Agent orchestration and runtime layer in the Tuya Physical AI platform. It handles context assembly, tool orchestration, model routing, the memory framework, and runtime assurance. It is a cloud platform capability and is not compiled into the device with the device-side SDK.</p></section></div>
</div>

## The tRTC real-time channel {#trtc-实时通道}

tRTC is the encrypted real-time channel between a device and the Tuya AI cloud. Agentic-kit currently provides two client implementations: an open-source TCP implementation and a UDP precompiled static library. They differ in delivery model, platform support, and reconnect ownership. Choose according to the target chip, weak-network requirements, and requirements for control over the source code.

| Implementation | Transport | Integration form | Main characteristics |
| --- | --- | --- | --- |
| `rtc-tcp-client` | TCP | Open-source code + PAL | Supports source-level control and cross-platform porting; the application handles disconnections and reconnects as documented |
| `rtc-client` | UDP | Precompiled static library supplied for each chip platform | Suitable for rapid integration and weak-network scenarios; contact Tuya sales for the library matching the target platform |

<div className="doc-callout doc-callout--info"><b>Confirm the delivery model before choosing</b><p>If you need source-level control or must adapt to a new system, evaluate the open-source TCP implementation first. If you need the UDP channel, first confirm the chip, operating system, toolchain, and library version, then obtain the matching precompiled static library from Tuya sales.</p></div>

<div className="doc-callout doc-callout--neutral"><b>A real-time channel is not an AI model</b><p>The channel sends audio, images, video, text, events, and tool results to the appropriate capabilities, and returns replies and commands to the device. Models and algorithms can run on the Tuya platform, in the customer's cloud, or on the device.</p></div>

## Multimodal streams and events {#多模态流与事件}

<div className="doc-card-grid doc-card-grid--two">
  <article><span>UPLINK / DEVICE -&gt; PLATFORM</span><h3>Input and state</h3><ul><li>Streaming audio and audio end</li><li>Image and video frames</li><li>Text, device Events, and tool execution results</li><li>Device state and Data Point Reports</li></ul></article>
  <article><span>DOWNLINK / PLATFORM -&gt; DEVICE</span><h3>Results and actions</h3><ul><li>TTS audio and text replies</li><li>Image and video results</li><li>Session, VAD, and interruption Events</li><li>MCP tool calls and device commands</li></ul></article>
</div>

The device application needs explicit state machines for capture, buffering, playback, interruption, and failure recovery. For example, when using cloud VAD, the device continuously sends audio and ends the current uplink turn after receiving the server endpoint Event. Local VAD is an optional optimization for scenarios with battery, bandwidth, or high-interactivity requirements.

## PAL: Platform Abstraction Layer {#pal平台抽象层}

The PAL is Agentic-kit's portability boundary. Rather than binding directly to a particular RTOS or chip, the SDK uses TCP, threading, mutex, time, and memory through function pointers. Official POSIX and FreeRTOS implementations are provided as references for adapting new platforms.

| Category | Typical interfaces | Engineering concerns |
| --- | --- | --- |
| Network | `connect / send / recv / close / poll` | Timeouts, partial sends and receives, TLS dependencies, network recovery |
| Concurrency | `thread_create / join`, mutex | Thread stack, callback context, lock granularity |
| System | `time_ms`, `malloc / free` | Clock monotonicity, peak memory use, and fragmentation |

## Physical device control: DPs and device-side MCP {#物理设备控制dp-与端侧-mcp}

Agentic-kit provides two ways to control physical devices. Customers can choose Data Points or device-side MCP based on whether the device capability is standardized, whether call parameters are dynamic, and whether an Agent must understand the context. A single product can also use each for different types of capability.

<div className="doc-card-grid doc-card-grid--two">
  <article><span>DATA POINT / DP</span><h3>Standard device capabilities and state</h3><p>Use a product Schema to describe stable capabilities such as switches, brightness, temperature, and modes. DPs suit app control, state synchronization, automation, and cross-device linkage. A device refreshes cloud state through Reports and changes local state in response to downlink DP sets.</p></article>
  <article><span>DEVICE MCP</span><h3>Dynamic tools for Agents</h3><p>Register capabilities such as reading a sensor, taking a photo, controlling a motor, or performing a task as tools with names, descriptions, and parameter Schemas. This suits AI interactions that require contextual understanding, structured parameters, or returned execution results.</p></article>
</div>

| Decision factor | Prefer DPs | Prefer device-side MCP |
| --- | --- | --- |
| Capability form | Stable, enumerable device attributes and functions | Dynamic, task-oriented tools or tools with complex parameters |
| Primary caller | App, cloud automation, device linkage | AI Agent |
| Result representation | State values and standard commands | Structured execution results, errors, and context |

<div className="doc-callout doc-callout--info"><b>The application owns state synchronization after reconnect</b><p>The SDK does not automatically Report restored local DP state again. The application should refresh the required state after every successful connection or reconnect to avoid inconsistent cloud and device displays.</p></div>

## Device-side and cloud-side MCP roles {#mcp-的设备侧与云侧角色}

MCP uses a unified protocol to describe tool names, purposes, parameters, and return values. In Physical AI, a tool can reside on a device or in a customer or third-party cloud. Device-side MCP is one of the two physical device control methods in the previous section.

<div className="doc-card-grid doc-card-grid--two">
  <article><span>DEVICE MCP</span><h3>The device is the tool provider</h3><p>The device registers capabilities such as reading sensors, controlling motors, and taking photos. An Agent initiates a call; the device validates the parameters, performs the action, and returns a structured result.</p></article>
  <article><span>CLOUD MCP</span><h3>A cloud service is the tool provider</h3><p>Cloud services perform weather, search, enterprise knowledge, or core customer business operations. They can use Tuya capabilities or integrate a customer's own implementation.</p></article>
</div>

<div className="doc-callout"><b>Physical actions require stricter boundaries</b><p>A tool declaration is only the call entry point. Before an embodied robot or spatial assistant performs actions such as operating a door lock or curtains, moving, or grasping, it must also verify device binding, user authorization, parameter ranges, current state, and failure fallback.</p></div>

## Perception, models, and memory {#感知模型与记忆}

A Physical AI experience emerges from the coordination of perception, reasoning, memory, and execution. These capabilities do not have to be deployed in the same place or provided by the same service.

<div className="doc-layers">
  <div className="doc-layer"><div className="doc-layer__label">Device-side perception<small>EDGE</small></div><div className="doc-layer__content"><div className="doc-layer__chips"><span className="doc-layer__chip">Keyword wake-up</span><span className="doc-layer__chip">Local VAD (optional)</span><span className="doc-layer__chip">Visual preprocessing</span><span className="doc-layer__chip">Sensor fusion</span><span className="doc-layer__chip">Emergency stop</span></div></div></div>
  <div className="doc-layer doc-layer--accent"><div className="doc-layer__label">Platform capabilities<small>TUYA PHYSICAL AI</small></div><div className="doc-layer__content"><div className="doc-layer__chips"><span className="doc-layer__chip">Cloud VAD / ASR / TTS</span><span className="doc-layer__chip">Vision algorithms</span><span className="doc-layer__chip">Agent Core</span><span className="doc-layer__chip">OmniMem</span><span className="doc-layer__chip">PAM</span><span className="doc-layer__chip">MCP / A2A</span></div></div></div>
  <div className="doc-layer"><div className="doc-layer__label">Customer capabilities<small>YOUR STACK</small></div><div className="doc-layer__content"><div className="doc-layer__chips"><span className="doc-layer__chip">Private models</span><span className="doc-layer__chip">Private-domain knowledge</span><span className="doc-layer__chip">Core cloud services</span><span className="doc-layer__chip">In-house algorithms</span><span className="doc-layer__chip">Product business logic</span></div></div></div>
</div>

For example, a voice flow can combine streaming ASR, dynamic hotwords, cloud VAD, and a customer's own TTS; a pet camera can connect individual pet recognition, behavior understanding, and event recording; and a robot can combine platform task planning with robot control, safety policies, and authorized home devices. Deployment location depends on latency, power consumption, bandwidth, privacy, compute capacity, and control of business logic.

## Boundary overview {#一张边界表}

| Layer | Main responsibilities | Controlled by |
| --- | --- | --- |
| Product application | Capture, playback, state machines, action execution, user experience | Customer |
| Agentic-kit | Identity, IoT, tRTC, Session Events, data, and tool interaction | Open SDK + customer integration |
| Tuya Physical AI platform | Agent orchestration, platform models, memory, knowledge, device and industry services | Selected as needed |
| Customer services | Private models, core business, private-domain knowledge, and differentiating algorithms | Customer, integrated through open capabilities |

## Next steps {#下一步}

- [System architecture](./architecture) - Learn how the modules work together
- [Quick start](./tutorials/quick-start) - Run the first example on your computer
- [Create and configure an Agent](./guides/create-agent) - Prepare your product and Agent on the IoT Platform
