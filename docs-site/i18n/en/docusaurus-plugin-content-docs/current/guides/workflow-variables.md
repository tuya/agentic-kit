---
title: Device Workflow Variables
sidebar_label: Workflow Variables
---

# Device Workflow Variables {#workflow-variables}

`workflow_variables_demo` uses the source-based RTC TCP Client (`tai_*`) to send typed workflow inputs. By default it sends three sequential Events in one Session: `chat`, `generate-img`, then `decide`. Each `--case ACTION COUNT` overrides the defaults with one Event per pair. `count` remains an integer workflow input, **not the number of sends**; all three defaults use `1`. The example receives text and images without a microphone, Opus, or the prebuilt RTC library.

:::warning Updated graph, not a live execution record
The defaults match the supplied workflow graph: plain text for `chat`, an image for `generate-img`, and JSON with a boolean `result` for `decide`. This version has not been run against the cloud. Earlier `sing` observations below concern an older workflow, not these branches.
:::

## Prerequisites and Platform Configuration {#platform}

- Use an activated device's `devid`, `secret_key`, and `local_key`, not its pre-activation UUID/authkey.
- The supplied workflow is in the China data center; defaults are `AY` and production (`PROD`). Devices elsewhere require the matching region. Selecting a region does not migrate a workflow.
- The host needs synchronized UTC, a C compiler, CMake 3.20+, Python 3, and initialized dependency submodules.
- The device product's Agent must be associated with the published workflow, and its text entry point must route to that workflow. See [Create an Agent](./create-agent.md) and [Create a Workflow](./create-workflow.md). Omit Agent code to use the product-bound Agent. `--agent-code` only forwards a value to the token API; it does not create or change a product association.

Confirm the following graph structure on the platform, including exact spelling and case:

| Node | Configuration |
|---|---|
| Start | `action`: string; `count`: integer |
| Selector, first branch | `action == "chat"`: LLM output as plain text |
| Selector, second branch | `action == "generate-img"`: image generation output |
| Selector, third branch | `action == "decide"`: LLM JSON output with boolean `result` |
| Fallback | Any other action; the demo requires nonempty text |
| Output / End | Configure delivery to the device, not only internal return values |

```text
Device action.value(string), count.value(integer)
    -> EventStart custom.param
    -> Cloud unwraps workflow variables
    -> Selector: chat / generate-img / decide / fallback
    -> Output node -> End (internal results alone are not device output)
    -> Device text or image callback, then EventEnd
```

The graph establishes structure, not end-to-end success. Check the published version, product/device Agent association, and text entry routing. The current screenshot does not show Start inputs; `count` is retained from the earlier demo, with no claim that the new branches use it. This guide does not invent UI button paths. The editor URL's `workflowId` is not an SDK routing parameter: do not use it as an Agent code, Agent token, or `sys.workflow` value.

Internal output-node fields such as `output` and `another`, and End return values, are not emitted text. In this run the sing node had an internal result but `outputContent:""` and no NLG/response queues; End also produced no text queue. Nonempty output content/a template must be configured, with changes and publication only after workflow-owner approval, then device text delivery must be retested. Do not change a production workflow merely to reproduce the example; no production configuration was changed in this run.

## Keep Inputs Separate from Prompt Text {#data-flow}

The application constructs the inner `custom.param` object, with a `value` wrapper for every parameter:

```json
{"action":{"value":"chat"},"count":{"value":1}}
```

The Omni parser requires `Map<String,CustomParamValue>`, not a scalar map such as `{"action":"sing","count":2}`. The root cause is fixed in the demo's `build_workflow_params()`: it uses vendored cJSON to construct each wrapper, adds `value` with `cJSON_AddStringToObject()` or `cJSON_AddNumberToObject()`, then serializes with `cJSON_PrintUnformatted()`, automatically escaping action. `action.value` is a JSON string; `count.value` is a JSON number, not `"2"`. Each wrapper may also contain optional `effectiveCount`, omitted here; it is separate from the workflow input `count` and must not be confused with it. The SDK's raw JSON pass-through behavior is unchanged.

The SDK adds the wrapper in EventStart's UserData Attribute 111. With default attributes it has this shape:

```json
{
  "chatAttributes": "{\"sys.workflow\":\"asr-llm-tts\",\"asr.enableVad\":true,\"tts.alternate\":true,\"processing.interrupt\":true}",
  "sessionAttributes": {
    "custom.param": {"action":{"value":"chat"},"count":{"value":1}}
  }
}
```

`chatAttributes` is a JSON-encoded **string**; `sessionAttributes` and the value of `custom.param` are **objects**. `custom.param` is one literal key containing a dot, not `param` nested under `custom`. The caller must supply each parameter's `value` wrapper, but must not add the outer `sessionAttributes`/`custom.param` layers or encode the custom object as a string. The SDK adds only the outer layers: it neither supplies `value` wrappers nor validates the custom JSON's object shape or field types.

The prompt is sent separately in a Text Packet as `USER_TEXT`. Defaults ask `chat` to describe a cat by a sunny window, `generate-img` to draw that scene, and `decide` whether a cat is an animal. `--text` overrides all prompts with the same text. Custom `action` selects the branch; prompt text is not a replacement for custom inputs. Unknown actions use `Please run the configured workflow.`.

## Initial Configuration and Later Events {#sdk-usage}

This is the core API sequence. Complete initialization, error handling, and waiting logic are in `examples/posix/ai/rtc-tcp-client/workflow_variables_demo.c`:

```c
static const char first[] =
    "{\"action\":{\"value\":\"chat\"},\"count\":{\"value\":1}}";
static const char next[] =
    "{\"action\":{\"value\":\"generate-img\"},\"count\":{\"value\":1}}";
static const char prompt[] = "Please run the configured workflow.";

/* Fill the remaining connection, PAL and callback fields before init. */
cfg.event_custom_param_json = first;
/* tai_ctx_init(), tai_connect(), then: */
int rc = tai_send_text(ctx, prompt, sizeof(prompt) - 1);
/* Check rc and wait for the response's EventEnd before continuing. */

/* NULL restores default chat attributes, not "leave unchanged". */
tai_set_event_params(ctx, NULL, next);
rc = tai_send_text(ctx, prompt, sizeof(prompt) - 1);
```

- `tai_set_event_params()` changes local pointers only. It sends no Packet and does not update an already-open Event. Subsequent EventStart Packets carry the new parameters.
- Supply the complete object each time: the SDK replaces, not merges. Serialize the setter with Event-producing sends in the application; the setter itself does not lock.
- Pointers are borrowed, not copied. Setter strings must survive until replacement and completion of any send using them. The example retains all serialized objects until context teardown to simplify ownership.
- `tai_set_event_params(ctx, NULL, NULL)` restores default chat attributes and omits all of `sessionAttributes`; passing `"{}"` instead sends an empty `custom.param` object. Omission does **not** promise to erase cloud-persisted variables. Cloud merge/retention semantics require separate confirmation.
- One POSIX PAL is shared by `iot_init(pal)` and TAI, with initialization preceding cJSON use. MQTT auto-connect is disabled. The IoT client is destroyed after token-derived connection values have been copied.

See the [RTC TCP Client reference](../reference/rtc-tcp-client.md#tai_set_event_params).

## Build and Run {#build-run}

From the repository root, build the standalone examples project. Do not share its build directory with the root project:

```shell
cmake -S examples/posix -B build-examples \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_C_FLAGS="-DAGENTIC_KIT_LOG_LEVEL=2"
cmake --build build-examples --target workflow_variables_demo -j 4
./build-examples/workflow_variables_demo --help
```

First configuration fetches QR/image dependencies even though this target does not use them, so network access may be needed. Compile all SDK C libraries at WARN level: default DEBUG ATOP logs and INFO TAI Packet logs can expose Session tokens. A same-named CMake variable alone does not define the compiler macro. The example still prints its inputs and responses; redact them before sharing.

:::warning TLS limitation
The example retains the existing POSIX TAI TLS defaults and provides no certificate-bundle callback. Encryption therefore does not establish verified server identity. Use only in a controlled development environment. For production, configure trusted peer verification as described in [TLS Certificate Verification](./tls-cert-verification.md); do not infer secure defaults from this example.
:::

Set these environment variables securely on your host. These are placeholders, not working credentials; never commit actual secrets:

```shell
export TUYA_DEVICE_ID='<activated-device-id>'
export TUYA_SECRET_KEY='<activated-secret-key>'
export TUYA_LOCAL_KEY='<activated-local-key>'

# Default: chat 1, generate-img 1, decide 1, with action-specific prompts
./build-examples/workflow_variables_demo

# Explicit sequence with more time for image generation
./build-examples/workflow_variables_demo \
  --case chat 1 --case generate-img 1 --case decide 1 --timeout 180

# Override the prompt for a single action
./build-examples/workflow_variables_demo --case decide 1 \
  --text 'Is the sun a star? Return a boolean result.'
```

| Option | Behavior |
|---|---|
| `--case ACTION COUNT` | Repeatable; nonempty action, including unknown strings for fallback; count is a complete signed 32-bit decimal integer |
| `--text TEXT` | Nonempty override shared by all cases; otherwise action-specific prompts above |
| `--agent-code CODE` | Optional nonempty code; defaults to product-bound Agent, not a workflow editor ID |
| `--region REGION` | `AY` (default), `AZ`, `UEAZ`, `EU`, `WEAZ`, `IN`, `SG`; fixed PROD |
| `--timeout SECONDS` | Monotonic per-response deadline, 1-3600 seconds, default 60; starts before sending, not a deadline for the entire bootstrap |
| `--help` | No credentials or networking needed |

Device ID must be 1-31 bytes; both keys must be 16-31 bytes. Oversized credentials are rejected, not truncated. Argument and credential validation precede networking.

## Results and Limitations {#results}

The console prints case numbers, typed input JSON, `USER_TEXT`, raw text, decoded NLG content, Event boundaries, and a summary. Raw text preserves all structured-output fields rather than assuming a bare `{"output":...}` response. Other text chunks use the shared reassembly buffer, with EventEnd flushing pending content when no text END arrived. The default MCP advertisement remains enabled, with no-tools replies to MCP requests.

Completion depends on the action:

- `chat`: nonempty plain text; no JSON parsing is attempted.
- `generate-img`: one complete, nonempty Image Packet stream (`START` through `END`, or `ONE_SHOT`), at most 16 MiB. The owning thread saves the encoded bytes after EventEnd to a unique `workflow-image-XXXXXX` file in the current directory and prints its path, byte count, and reported format. Text alone (including an image URL in text) does not count as an image. The demo does not download URLs or decode image files.
- `decide`: decoded NLG content is accumulated across the Event, then parsed as one complete JSON object with a boolean `result`, such as `{"result":true}` or `{"result":false}`. Both booleans are valid; strings like `"true"`, missing fields, code fences, and trailing prose are rejected. All top-level fields are printed with their JSON types and values under `[Workflow JSON parsed]`.
- Other actions: nonempty text, for compatibility with custom/fallback branches.

Exit 0 requires EventEnd and the expected response type for every case, plus successful image saving. It means **responses received**, not verified workflow execution or decision correctness. An empty NLG terminator does not count as meaningful text. Invalid input, token/connect/send failures, disconnect, ChatBreak, server timeout, response deadline, dropped/incomplete output, or missing expected output return nonzero. Ctrl-C/SIGTERM requests exit; cleanup follows return from any synchronous SDK call. There is no automatic reconnect or replay, avoiding duplicate execution after an ambiguous send failure.

### Historical Observations: Previous sing/move Workflow

First case `sing 2`, cn/prod, 2026-10-22 00:15 CST (UTC+8):

| Stage | Observation |
|---|---|
| Graph Chat | `customParam` was `{"count":2,"action":"sing"}`: cloud-unwrapped values, not the device wire format |
| `getVariables` | Also `{"count":2,"action":"sing"}`, confirming the integer input reached the workflow |
| Selector | Selected sing output node `output_yPA1` |
| Sing output node | Internal result `{"output":"sing","another":"2"}`; `outputContent:""`, with no NLG/response queues |
| End | Input and result both `{"output":"dumb"}`, with no text queue |
| Device | Only an empty final NLG followed by EventEnd; no nonempty text |

The demo therefore correctly exited nonzero after the first case, reporting 0/3; this does not mean all three cases executed. The remaining matrix, including `move 3`, `other 1`, and the same action with different counts, is unverified. There is no end-to-end success record. Internal output string `another:"2"` does not change the integer type of input `count`, nor does it establish delivery of that field.

The next step requires workflow-owner approval to configure nonempty output content/a template and publish, then rerun the full matrix to verify branches, count, and device-received text. An approved demonstration copy could use a deterministic template containing branch name and count; that is a recommendation, not a configuration completed in this run. Internal results and End return values cannot substitute for device text verification. No production configuration was changed.

## Troubleshooting {#troubleshooting}

| Symptom | Direction |
|---|---|
| Ordinary chat response | Check product Agent, published workflow, and text entry routing; do not put workflowId in `sys.workflow` |
| Token rejected | Read cloud code/message; check activation, region, product Agent, and privacy agreement rather than blindly repeating execution |
| Graph changes have no effect | Device may still use an older published version or another Agent |
| Missing or mistyped action/count | Check each parameter's `{"value":...}` wrapper, exact names, Start binding, and numeric `count.value`; do not double-wrap `sessionAttributes` or confuse count with `effectiveCount` |
| Missing `another` | Check output modes and device-output mappings; End's output does not automatically contain all upstream fields |
| EventEnd without expected output | `chat` needs text, `generate-img` needs a complete Image Packet stream, and `decide` needs JSON with boolean `result`; internal-only values or textual image URLs are insufficient |
| Timeout or disconnect | Check region, network, UTC, and workflow duration; `--timeout` controls application response waiting, not internal SDK connect/send timeouts |

## Related Documentation {#related}

- [Create a Workflow](./create-workflow.md)
- [Create an Agent](./create-agent.md)
- [RTC TCP Client API](../reference/rtc-tcp-client.md)
