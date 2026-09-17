---
title: Introduction to Tuya Agentic-kit
sidebar_label: Introduction
sidebar_position: 1
slug: /intro
---

import Link from '@docusaurus/Link';

# Introduction to Tuya Agentic-kit

<div className="doc-lead">Agentic-kit is an atomic, lightweight C SDK for Physical AI devices. Integrate it into an existing firmware, operating system, or application to establish device identity, IoT management, and real-time multimodal channels between the device and the Tuya Physical AI platform.</div>

<div className="doc-summary">
  <div><b>WHERE IT RUNS</b><span>MCUs, RTOSes, Linux SoCs, PCs, mobile apps, and browser environments with a native adaptation layer</span></div>
  <div><b>WHAT IT CONNECTS</b><span>Audio, images, video, text, events, commands, and tool calls</span></div>
  <div><b>HOW IT INTEGRATES</b><span>Chip-independent, platform-abstracted, and atomically selectable, while preserving your existing product engineering and core business logic</span></div>
</div>

## What problems does it solve? {#它解决什么问题}

Physical AI products need real hardware to continuously perceive the environment, understand users, and take action. Devices must contend with differences in resources, power consumption, networks, and peripherals, while the cloud handles real-time communication, Agent orchestration, models, memory, knowledge, and industry services. Agentic-kit sits between the two, consolidating the identity, connection, Session, and data interaction needed for integration into a lightweight set of interfaces.

<div className="doc-callout"><b>Capability boundary</b><p>Agentic-kit handles device-side integration and the reliable flow of data, events, and results. Models, algorithms, memory, and industry services can be deployed and combined on the device, on the Tuya platform, or in a customer's own services according to product requirements.</p></div>

## Core capabilities {#核心能力}

<div className="doc-card-grid doc-card-grid--three">
  <article><span>01 / IDENTITY</span><h3>Device Activation and identity</h3><p>Use the product PID and device authorization code to complete Activation and obtain the official credentials required for the device to connect to the platform.</p></article>
  <article><span>02 / REALTIME</span><h3>Real-time tRTC interaction</h3><p>Establish an encrypted, persistent device-cloud connection to stream multimodal data and receive AI results and Session events.</p></article>
  <article><span>03 / MULTIMODAL</span><h3>Multimodal data</h3><p>Send audio, images, video, text, and events uplink; receive speech, text, images, video, and commands downlink.</p></article>
  <article><span>04 / TOOLS</span><h3>Device-side MCP</h3><p>Register device capabilities such as reading sensors and controlling peripherals as tools that an Agent can call according to an explicit Schema.</p></article>
  <article><span>05 / IOT</span><h3>Device management and DPs</h3><p>Provides foundational IoT capabilities including Activation, MQTT connections, Session tokens, and Data Point uplink and downlink.</p></article>
  <article><span>06 / PORTABLE</span><h3>Cross-platform adaptation with the PAL</h3><p>Adapt to different chips and operating systems through networking, threading, mutex, time, and memory interfaces.</p></article>
</div>

## One integration model, many device types {#一套入口多种终端}

Agentic-kit does not constrain the product form factor. The same integration model can support voice toys, camera-based learning devices, pet companions, productivity devices, spatial assistants, and embodied robots. You can also validate it first on macOS or Linux, then port it to the target hardware.

| Device type | Typical input | Typical output or action | Composable platform capabilities |
| --- | --- | --- | --- |
| Voice and companion devices | Audio, buttons, status events | TTS, text, lighting effects, and expressions | ASR, VAD, Agent, long-term memory |
| Vision and learning devices | Images, video, voice | Recognition results, explanations, screen content | Visual understanding, object recognition, knowledge services |
| Productivity devices and applications | Meeting audio, files, screen events | Summaries, action items, tool calls | Agent, knowledge base, customer business tools |
| Embodied robots | Voice, vision, environment, and robot state | Movement, grasping, device linkage, acknowledgements | Task planning, PAM, MCP, authorized device ecosystem |

## Integration flow {#接入流程}

<div className="doc-flow">
  <div><strong>Create a product</strong><span>Obtain a product PID on the Tuya IoT Platform and configure a default or switchable AI Agent.</span></div>
  <div><strong>Prepare the identity</strong><span>Claim a uuid / authkey during testing; obtain authorization codes for the project during mass production.</span></div>
  <div><strong>Activate and connect</strong><span>Complete Provisioning, persist devid, secret_key, and local_key, and establish the IoT and tRTC channels.</span></div>
  <div><strong>Integrate product logic</strong><span>Handle streaming data, Session events, and tool calls, then connect models and business services as needed.</span></div>
</div>

<div className="doc-callout doc-callout--info"><b>You can start without hardware</b><p>The official examples let you validate voice, image, and MCP flows directly on macOS or Linux. Run the POSIX examples first, then implement the PAL for the target platform to separate cloud configuration issues from hardware porting issues more quickly.</p></div>

## What you need before starting {#开始前需要什么}

| Information | Purpose | Source |
| --- | --- | --- |
| Product PID | Identifies a product family and its DPs, panel, and bound Agent | Create a product on the Tuya IoT Platform |
| `uuid` / `authkey` | Factory identity credentials held independently by each device | Test authorization codes or mass-production authorization |
| Network and system adaptation | Provides PAL capabilities such as TCP, threading, and memory, or uses an implementation for a supported platform | POSIX, FreeRTOS, or a custom PAL |
| Product interaction design | Defines strategies for capture, playback, interruption, tool execution, and failure recovery | Customer product and firmware engineering |

## From the SDK to platform capabilities {#从-sdk-连接到平台能力}

The Tuya Physical AI platform organizes device-cloud communication, Agent orchestration, models, memory, knowledge, device control, and vertical perception into composable capabilities. For example, a voice flow can combine streaming ASR, device-side or cloud VAD, hotwords, and TTS; a vision product can connect to object recognition, individual pet recognition, or scene understanding; and customers can integrate their own models, private-domain knowledge, and core cloud services.

<div className="doc-boundary">
  <div><h3>Agentic-kit provides</h3><ul><li>Device identity, Activation, and IoT connectivity</li><li>Real-time multimodal device-cloud communication</li><li>Session events, data callbacks, and tool interaction</li><li>An adaptation boundary across chips and operating systems</li></ul></div>
  <div><h3>Compose for each product</h3><ul><li>Device-side or cloud perception algorithms</li><li>Agents, models, memory, and knowledge bases</li><li>MCP / A2A and customer-owned services</li><li>Device control, industry services, and global operations capabilities</li></ul></div>
</div>

General-purpose large models remain open: customers can select leading global models on the Tuya platform or integrate private models. For real-device Physical AI scenarios, Tuya also trains and continuously optimizes vertical models for voice, vision, physical execution, and other areas to improve specific experiences such as far-field audio capture, interference from multiple speakers, complex visual scenes, and device actions. Model names, availability, and supported platforms are subject to official release information. Vertical models are listed on the [Tuya Algorithm Platform](https://www.tuya.com/model) (link retained from the Chinese source; currently returns a page-not-found message).

## Why connect to the Tuya Physical AI platform? {#为什么连接-tuya-physical-ai-平台}

Agentic-kit keeps device-side integration lightweight, while the Tuya Physical AI platform provides the shared foundation needed for everything from real-time interaction to global operation. Customers can select only the parts they need while retaining technical control over their models, data, and core business.

<div className="doc-card-grid doc-card-grid--two">
  <article><span>END-TO-END VOICE</span><h3>Complete end-to-end voice capabilities</h3><p>Organizes audio capture, VAD, streaming ASR, Agents, memory, knowledge bases, TTS, playback, and interruption into a real-time interaction flow, reducing the engineering cost of combining services from multiple providers.</p></article>
  <article><span>OPEN CLOUD &amp; MODEL</span><h3>Open cloud and model choices</h3><p>Supports leading global models as well as customer private models, private-domain knowledge, and core cloud services through open capabilities. Products can mix, replace, and evolve these choices incrementally.</p></article>
  <article><span>GLOBAL &amp; COMPLIANT</span><h3>Designed for global operation and compliance</h3><p>Supports products entering global markets with global real-time communication, device identity, encrypted transmission, and regionalized service capabilities; specific data processing and compliance strategies are implemented according to the product's markets and project configuration.</p></article>
  <article><span>VERTICAL ALGORITHMS</span><h3>Vertical models trained for real hardware</h3><p>Continuously optimizes for challenges such as far-field audio, target speakers, hotwords, individual pets, complex vision, and physical execution, turning algorithm metrics into tangible Physical AI user experiences.</p></article>
</div>

## How to choose a device-side path {#如何选择端侧路径}

**TuyaOS (Wukong), TuyaOpen, and Agentic-kit are not three overlapping solutions. They complement one another for different product foundations and degrees of openness.** Tuya defines the device-side architecture, interfaces, and component boundaries for TuyaOS (Wukong) and TuyaOpen, which include substantial connectivity, device model, AI, and general engineering code. Agentic-kit instead starts from the customer's existing architecture and provides lighter-weight atomic capabilities.

<div className="doc-card-grid doc-card-grid--three">
  <article><span>STANDARD / COMMERCIAL</span><h3>TuyaOS (Wukong)</h3><p>Uses Tuya's standardized product framework and mass-production system. Wukong is the AI-focused SDK for TuyaOS and suits products that need coordinated connectivity, security, device models, AI, and commercial delivery.</p></article>
  <article><span>STANDARD / OPEN SOURCE</span><h3>TuyaOpen</h3><p>Uses Tuya's standardized layered framework while opening the complete source code and a rich set of components. For new projects starting from scratch, it can usually move into product feature development quickly with less foundational engineering work.</p></article>
  <article><span>FIT YOUR STACK</span><h3>Agentic-kit</h3><p>Does not require adopting Tuya's complete device-side framework. It integrates into a customer's existing technology stack with fewer resources and less intrusion; customers do not need to wait for Tuya to adapt the target chip and can implement the PAL for the chip platform themselves.</p></article>
</div>

| Your starting point | Evaluate first | Why choose it |
| --- | --- | --- |
| You want Tuya to provide commercial SDK assurance | TuyaOS (Wukong) | Follow Tuya standards for product development, mass production, and dedicated AI capabilities |
| You are starting from scratch and want open source code | TuyaOpen | A more complete framework and component set reduces repeated foundational engineering |
| You have an existing technology stack or in-house framework | Agentic-kit | Adapt from the existing architecture and incrementally integrate only the device-cloud capabilities you need |
| Your current chip is not yet supported by an existing Tuya SDK | Agentic-kit | Adapt the target platform from scratch through the PAL without waiting for full framework support |
| RAM / Flash is constrained and runtime and storage footprints must be extremely low | Agentic-kit | Lighter and more atomic, so it can be trimmed to the features actually used |

<div className="doc-callout doc-callout--neutral"><b>For a new project that prioritizes speed and has no existing technology-stack requirements, evaluate TuyaOpen first; for an established technology stack that emphasizes flexible adaptation, evaluate Agentic-kit first</b><p>If the target platform already has a network stack, compiler toolchain, and required system capabilities, and the work is limited to basic PAL and communication integration, basic Agentic-kit adaptation can be completed in as little as <strong>half a day</strong>. The time required for audio/video peripherals, Provisioning, the product state machine, and mass-production validation still depends on the specific solution.</p></div>

## Next steps {#下一步}

- [Core concepts](./concepts) - Learn about device Activation, Agents, tRTC, Data Points, and MCP
- [System architecture](./architecture) - Learn about the SDK modules and project structure
- [Quick start](./tutorials/quick-start) - Run the first example
- [SDK reference](./reference/rtc-tcp-client) - Read the API documentation
