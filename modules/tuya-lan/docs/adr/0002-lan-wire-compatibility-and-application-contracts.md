# LAN wire compatibility preserves explicit application contracts

The reference baseline is TuyaOpen commit
`68f76c966ef6154c319ce122506d4e0934b2979b`. LAN 3.5 framing and cryptographic
construction are based on it; Session acceptance, error handling, application
callbacks and optional behavior follow explicit agentic-kit contracts.

## Decisions

- Successful controls receive a big-endian 32-bit return code 0. Any control
  error closes its Session immediately without queuing an error ACK. Explicit
  `tuya_lan_send()` return codes retain their big-endian 32-bit encoding. The
  pinned TuyaOpen control path always writes value 1 in native byte order;
  this difference is intentional and does not establish how an App interprets it.
- `on_query` continues to supply a `{"dps":{...}}` state document. The protocol
  layer adds an authoritative `devId` from `config.device_id` to the wire
  response; callback data cannot replace the configured device identity.
- Before authentication, only the expected START (3) and FINISH (5) are accepted
  with local_key, according to handshake state. The first non-handshake Frame,
  any TYPE4, or duplicate/out-of-order handshake command closes its Session.
  The former three-Frame skip budget and pre-authentication TYPE4-ignore policy
  are removed in favor of immediate closure on error.
- Every error associated with a Session closes its socket immediately, including
  Frame/GCM/replay, JSON/schema, callbacks, resources, unsupported commands, and
  local report/send parameter or payload-size rejection. PAL_ERR_AGAIN for
  recoverable socket would-block/interruption is the exception; positive partial
  reads/writes are progress. Error paths close without queuing an error ACK.
- Discovery supports both query replies and periodic UDP announcements. The
  default `discovery_interval_ms = 5000` attempts an announcement on the first
  process call and every five seconds thereafter, to 255.255.255.255:6667 by
  default. These announcements are an agentic-kit extension enabled by default;
  the pinned reference only replies to queries. Setting the interval to 0 or
  `broadcast_ip = NULL` disables announcements while query replies remain
  available. Replies always use the validated source IP and the App's fixed
  UDP port 7000.
- Strict lexical and canonical DP-document checks remain the agentic-kit local
  ingress policy established in ADR 0001, rather than TuyaOpen cJSON semantics.

## Consequences

- Send errors close the identified target; unknown IDs cannot close unrelated
  Sessions. An invalid broadcast report closes its authenticated targets, while
  a per-Session encoding or queue failure closes only the affected Session.
- Sockets close immediately inside callbacks too. Protocol and queue reclamation
  waits for active process/callback stacks to unwind; outside those stacks it
  completes before the API returns. This keeps callback data alive during unwinding.
- UDP discovery has no TCP Session association. Invalid discovery datagrams are
  dropped without disconnecting unrelated TCP Sessions.
- Apps receive both `dps` and the configured `devId` in successful query replies;
  products keep their existing query callbacks.
- Applications receive both discovery mechanisms by default and may disable
  unsolicited announcements without disabling App query replies.
- Compatibility documentation distinguishes reference behavior from deliberate
  SDK policy instead of claiming that every byte and accepted input matches.
