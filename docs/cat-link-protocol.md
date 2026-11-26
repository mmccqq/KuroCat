# Cat Link protocol (v1)

Cat Link lets two KuroCats show each other's cat.

Cat Link uses EMQX's public servers (`broker-cn.emqx.io` and `broker.emqx.io`). They're open to anyone with no sign-up, no username and no password. EMQX runs them free so developers can test MQTT apps, a bit like a public Wi-Fi hotspot.

Both apps connect to the same MQTT server and exchange small, encrypted, anonymized activity events. The Mac version is in `Sources/CatLink.swift`; any other version (Windows, Linux) must follow this document byte for byte.

## Pairing code

```
KC-C-7KQ2-M9XA-P3TD-W8RF-Z5HN
   │ └────────── secret ────────┘
   └─ server: C = broker-cn.emqx.io (Shanghai), G = broker.emqx.io (US)
```

- The secret is 20 characters from `23456789ABCDEFGHJKMNPQRSTUVWXYZ`, which leaves out 0/O and 1/I/L. That's about 99 bits.
- Parsing: uppercase the text, keep only letters and digits, then expect `KC` + server letter + 20 secret characters (23 in total).
- Shown to users as `KC-<server>-XXXX-XXXX-XXXX-XXXX-XXXX`.

## Keys and topic

Both are derived with HKDF-SHA256 (RFC 5869):

| | Input key material | Salt | Info | Length |
|---|---|---|---|---|
| AES key | the 20 secret characters (ASCII) | `kurocat-link-v1` | `aes-gcm-key` | 32 bytes |
| Topic id | same | same | `mqtt-topic` | 16 bytes |

- Topic: `kurocat/v1/` + the 16-byte topic id as lowercase hex (32 characters).
- Windows CNG has HMAC-SHA256 (`BCRYPT_SHA256_ALGORITHM` with `BCRYPT_ALG_HANDLE_HMAC_FLAG`), and HKDF is two HMAC steps: extract, then one expand block.

## Transport

- MQTT **5** over secure WebSocket: `wss://<server>:8084/mqtt`, WebSocket subprotocol `mqtt`.
- CONNECT: clean start, keep-alive 60 s, no properties, client id `kurocat-<peer id>-<4 random digits>`.
- SUBSCRIBE to the topic with options `0x04`: QoS 0 and **No Local**, so the server doesn't echo our own messages.
- PUBLISH: QoS 0, no properties. The payload is one encrypted batch.
- Send PINGREQ every 25 s. If nothing arrives for 90 s, treat the connection as dead.
- Reconnect after 2 s, then 4, 8, … up to 60 s. Reset the delay after a successful CONNACK.

## Encryption

`payload = nonce (12 bytes) | ciphertext | tag (16 bytes)`, using AES-256-GCM with a random nonce per message and no associated data. Messages that fail to decrypt are ignored.

## Batch (the decrypted payload, UTF-8 JSON)

```json
{"v":1,"f":"3fa2c9d1","q":42,"e":[ ...events... ]}
```

| Field | Meaning |
|---|---|
| `v` | Protocol version, 1 |
| `f` | Sender's random peer id (8 lowercase hex characters, new on every app launch) |
| `q` | Sequence number, +1 per batch |
| `e` | Events in time order |

Senders flush every 100 ms, or at 40 events, and send nothing while the user is idle except `hello` every 10 s. Receivers apply each event at `arrival + 120 ms + o`, which keeps the sender's rhythm even when batches arrive unevenly.

## Events

Every event has `k` (its kind) and `o` (milliseconds after the batch's first event). All other fields are optional; leave out the ones that don't apply.

| `k` | Fields | Meaning |
|---|---|---|
| `hello` | `n` nickname (≤16 chars, optional), `coat`, `kb`, `eye` theme names, `hp` headphones on, `pl` audio playing | Who I am and how my cat looks. Sent on connect, every 10 s, and whenever any of these change |
| `kd` | `id` press id (1–9999, wraps), `s` paw: `"l"` or `"r"` | A key went down. **Never send the key itself.** `s` is the viewer's paw: `"l"` = viewer's left paw (`prefersLeftPaw`). Don't send while a password field has focus |
| `ku` | `id` | That press was released |
| `m` | `x`, `y` cursor (0…1 of its screen, **y up**), `vx`, `vy` velocity in screen-heights per second | Mouse sample, at most every 100 ms and only while moving, plus one final sample with zero velocity |
| `c` | `b` button (0 left, 1 right, 2 other), `d` 1 down / 0 up | Mouse click |
| `w` | `d` scroll delta in points | Scrolling, merged to one event per 50 ms |
| `pet` | `x`, `y` point on the **receiver's** cat canvas (360×320, y down), `d` stroke length, `h` true = head | I am petting *your* cat on my screen; apply it to your own cat |
| `bye` | | I'm disconnecting (the receiver shows the friend as offline) |

### Theme names

These are the Swift case names; the Windows enums use the same names in PascalCase:

- `coat`: `classicBlack`, `siamese`, `shorthair`, `orangeWhite`
- `kb`: `classicMilk`, `midnightHacker`, `retroCream`
- `eye`: `neonChartreuse`, `siameseBlue`, `warmAmber`, `iceCyan`, `softAmber`, `referenceHazel`

## How the receiver shows the friend's cat

- **Keys:** each `kd` picks a random typing key on that paw's side that isn't already held, and holds it until the matching `ku`. Key bubbles, if shown, display that random stand-in key, never the real one. If a `ku` never arrives, release the key after 20 s.
- **Mouse:** map `x`/`y` onto your own main screen. Multiply velocity by your screen height.
- **Presence:** the friend is online while any batch has arrived in the last 25 s. When they go offline, release everything.

## Privacy rules (must hold in every version)

- No key identities and no text are ever sent.
- No keyboard events are sent while secure input (a password field) is active.
- Sharing is opt-in, the app clearly shows that it's linked, and disconnecting stops all sending.
- Everything is encrypted with the pairing code, so the public server only sees random bytes on a random topic.
