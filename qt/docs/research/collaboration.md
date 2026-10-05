# Collaboration, E2E encryption and agents as collaborators (research, 2026-10)

Research for a far-future idea of the author (2026-10-05): E2E-encrypted, peer-to-peer collaboration with optional
servers ("maybe just a simple discovery server"), a permission model over operations and tools, and agents as
collaborators through a protocol or MCP (teacher mode, rubber duck, colleague), with voice input and spoken
commentary. Nothing here is implemented or decided.

- **Checked** means read on the project's page or in a search result quoting it (2026-10-05). **Not opened** means
  the page itself was blocked by this machine's proxy and only search summaries were read. **To check** marks
  anything that must be verified before deciding. Sizes use the scale of [ideas-2026-10.md](../ideas-2026-10.md)
  (S 1–2 days, M 3–5 days, L more); they are estimates, not measured.
- The combined app is GPL-3.0-or-later ([FORK.md](../../../FORK.md)), so MIT, BSD, MPL-2.0 and Apache-2.0
  libraries can be linked ([FSF](https://www.gnu.org/licenses/license-list.html),
  [Apache](https://www.apache.org/licenses/GPL-compatibility.html)).

## Summary

| Question | Recommendation |
| --- | --- |
| Sync model | Upstream's model stays the truth; a **sync layer beside it**, keyed by stable IDs. Ink: a map of immutable elements with per-field last-writer-wins (LWW) and tombstones, a small CRDT the fork owns in C++. Only Markdown text needs a sequence CRDT (Loro, Yrs or Automerge, all Rust), later. |
| Stable IDs | `xqt-id` on elements, layers and pages (a seam like `xqt-group`), with a content-hash fallback for files saved by Xournal++. |
| Transport | LAN first (mDNS, no server); then **libdatachannel** (C++, WebRTC data channels) or iroh if Rust is accepted; a tiny self-hostable rendezvous/relay that sees only ciphertext. |
| Encryption | Like Excalidraw: a **per-document key** in the invitation link/QR, plus every operation **signed by a device key**; rotation on removal. MLS/Keyhive later if needed. Vendored primitives (Monocypher). |
| Permissions | **Signed capabilities checked by every peer** against each incoming operation. Roles (teacher, student, reviewer, agent) are presets over operation types. |
| Agents | First the **MCP server over stdio** (idea B2); later the agent joins live sessions as a capability-limited peer. Modes are UX on top. |
| First useful step | Either the read-only MCP server (M), or **read-only live sharing in a classroom over LAN** (M–L). Both need stable IDs first (S–M). |

## 1. Sync model

### What the document is, seen by a sync layer

Upstream's model is `Document` → `XojPage` (background, size) → `Layer` → `Element` (`Stroke`, `Text`, `Image`,
`TexImage`, `Link`; `src/core/model/`). The fork adds Markdown boxes (texts in a layer named `Markdown`), sticky notes
(a layer each), groups, bookmarks, voice memos and `.md` documents. Elements have **no identity today**: undo
actions hold pointers, `DocumentListener` reports changes per page, the eraser replaces a stroke by its pieces.

| Part | Operations | Fitting structure |
| --- | --- | --- |
| Strokes, images, TeX, links | insert (immutable points), delete, transform (move/scale/rotate), restyle (colour, width, fill), group | map `id → element`; per-field LWW registers (geometry, style, group); tombstones |
| Eraser | whole-stroke erase = delete; splitting = delete + insert pieces with new IDs | same map; concurrent move + split: the split wins (the move is lost), acceptable for ink |
| Order in a layer (z-order) | insert at top, "bring to front" | a fractional index per element (LWW), not a list CRDT |
| Layers, sticky notes | add, rename, hide, delete, reorder | map + fractional index |
| Pages | insert, delete, **move**, background change | map `id → page` + an order. Moves are the hard case: Yjs dropped its move ([Yjs forum](https://discuss.yjs.dev/t/moving-elements-in-lists/92), [Sypytkowski](https://www.bartoszsypytkowski.com/replacing-yjs-move-feature/)); Loro has a movable list and a movable tree ([Loro](https://loro.dev/blog/movable-tree)); for Automerge, move is a research extension ([Da & Kleppmann](https://arxiv.org/abs/2311.14007); in the shipped library: to check). A fractional index per page also works: concurrent moves of one page resolve by LWW. |
| Markdown boxes, `.md` files, typed text | character edits by two people at once | a text CRDT (Yjs/Yrs, Automerge text with Peritext marks ([Peritext](https://www.inkandswitch.com/peritext/)), Loro text) |
| PDF backgrounds, images, recordings | shared once, never edited | content-addressed blobs (hash → bytes), fetched on demand, chunked |

Most of the document is **immutable records with a few mutable fields**, the easy case for CRDTs. Only text is
hard.

### The candidates

| Option | What it is | For | Against |
| --- | --- | --- | --- |
| **Operational transform** | a server orders operations and transforms concurrent ones (Google Docs) | mature for text | needs a central sequencer: against "P2P first"; transform functions per operation pair |
| **Server-ordered op log** | one server stamps the order (Stylus Labs Write's [stylusboard](https://github.com/styluslabs/stylusboard); tldraw sync, not opened) | simplest; easy undo | the server is required and sees the order (content can still be encrypted); offline edits need rebasing |
| **Custom element-map CRDT** | IDs, Lamport clocks, per-field LWW, tombstones, fractional indices | small (estimate 2–4k lines of C++), no Rust, fits upstream's model exactly, the fork owns the format | text is not covered; the fork maintains a distributed algorithm (property tests needed) |
| **Automerge 3** | JSON-like CRDT, Rust core, C bindings in `rust/automerge-c` ([GitHub](https://github.com/automerge/automerge)); 3.0 (July 2025) uses the compressed form at runtime, about 10× less memory ([blog](https://automerge.org/blog/automerge-3/)) | full history, rich text (Peritext), the sync protocol Keyhive builds on | Rust toolchain; C API maturity to check; every point of every stroke in a CRDT is a lot of history unless strokes are stored as opaque bytes |
| **Yjs / Yrs** | Rust port of Yjs with a C FFI (`yffi`, `libyrs.h`) ([y-crdt](https://github.com/y-crdt/y-crdt)); LibreOffice vendors it for collaborative editing (to check how far) | fast, widely used, `UndoManager` | no list move; Rust toolchain |
| **Loro** | Rust CRDT library, 1.0 in 2024: rich text, movable list/tree, `UndoManager` scoped to the local peer ([Loro 1.0](https://loro.dev/blog/v1.0)) | the best fit for pages that move and for local undo | official bindings are UniFFI (Swift, others) ([loro-ffi](https://github.com/loro-dev/loro-ffi)); the C/C++ bindings found are a third-party project ([loro-c](https://github.com/gsfjohnson/loro-c)): to check |

**Recommendation.** Do not make a CRDT library the document model: upstream's tools, undo actions and renderer work
on `Document`, and upstream merges must stay cheap. Put a **sync layer beside the model**:

- **Local → ops:** after each undo step or page change, diff the affected layers by ID and content hash. No change
  to upstream's tools.
- **Ops → model:** remote ops are applied by ID under the document's exclusive lock; the page revision is bumped
  (thumbnails and tiles follow, as today).
- **Ink, layers, pages:** the custom element-map CRDT; a stroke's points are one opaque value.
- **Text:** a library only when concurrent Markdown editing is wanted; until then a Markdown box is one LWW value
  (the losing edit is kept as a "conflict copy" sticky note).

### Stable IDs, compatibly with `.xopp`

- `xqt-id="…"` (random 128 bits, about 22 characters) on `stroke`, `text`, `image`, `teximage`, `link`, `layer`
  and `page`, through the seam `xqt-group` uses ([groups.md](../groups.md)). Xournal++ ignores it and drops it on
  save.
- A file back from Xournal++ without IDs gets **content-derived IDs** (a hash of type, points, style), so unchanged
  elements map onto the same CRDT entries. Paste and duplicate give new IDs, like `groups::renumber`.
- Cost: about 30 bytes per element. The same seam can carry `xqt-created` (B9 Level 2).

### Undo, offline, history, and the saved form

- **Undo undoes my own operations only** (Yjs's and Loro's undo managers; the principles of
  [Stewen & Kleppmann 2024](https://arxiv.org/abs/2404.11308)). Upstream's pointer-based stack cannot survive a
  remote delete, so in a shared document undo works by ID (inverse ops of the sync layer).
- **Offline edits** are normal: ops carry a version vector; on reconnect peers send each other what is missing.
- **The saved form stays the file** (`.xopp` or the [hybrid PDF](../hybrid-pdf.md)). The op history lives in the
  **app cache**, keyed by a document ID in the file: folders stay clean and shared files carry no erased ink (as a
  PDF attachment it would have B9 Level 3's privacy cost).
- **PDF backgrounds** are blobs named by hash (the hybrid PDF's clean copy), sent once in chunks; pages refer to
  `hash#page`. Peers that have the file send nothing.

## 2. Transport: peer to peer first

| Option | What it gives | Fit |
| --- | --- | --- |
| **LAN only**: mDNS/DNS-SD + a direct connection | no server, offline, lowest latency | mjansson's [mdns](https://github.com/mjansson/mdns) (one public-domain C header); Qt Network sockets. School Wi-Fi may isolate clients (to check), so a relay fallback is still needed |
| **WebRTC data channels** via [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | C++17, MPL-2.0; ICE with STUN and TURN, WebSockets; all five platforms; needs OpenSSL, GnuTLS or Mbed TLS plus bundled usrsctp, libjuice, plog (checked in its README) | best fit for C++/CMake; talks to browsers (a **web viewer** for students later). Needs signalling and a TURN server |
| **[iroh](https://github.com/n0-computer/iroh)** (Rust, MIT/Apache-2.0) | dial by public key; QUIC hole punching, else relays forwarding encrypted traffic; DNS and mDNS discovery; a raw C API ([iroh-c-ffi](https://github.com/n0-computer/iroh-c-ffi)) | 1.0 in June 2026 (search results, not opened); browsers only via relay ([docs](https://docs.iroh.computer/deployment/wasm-browser-support), not opened). Best relay story; Rust toolchain; default relays run by n0 (self-hostable) |
| **libp2p** (Rust, Go, JS; cpp-libp2p) | DHT discovery, Noise XX handshakes ([spec](https://github.com/libp2p/specs/blob/master/noise/README.md)), relays | general and heavy; the DHT is not needed when invitations carry the addresses |
| **Bluetooth / Wi-Fi Direct** | classrooms without a network | per-platform code; iOS's peer-to-peer Wi-Fi tops out around 8 peers ([Apple forums](https://developer.apple.com/forums/thread/785308)): not for a class of 30 |

**What a minimal server must do** (one small self-hostable binary, optional):

1. **Rendezvous:** a peer registers under a topic (the hash of a document ID and a key-derived secret, not the
   name) and learns the addresses of the others; it carries WebRTC offers/answers (signalling).
2. **NAT help:** STUN (tells a peer its public address) and a relay (TURN, or an iroh-style relay) when hole
   punching fails.
3. **Optional mailbox:** stores encrypted ops for peers that are offline, so a student who opens the document at
   home gets the teacher's changes without the teacher's device being on (what Beelay does for Keyhive, below).

It **can see** IP addresses, who joins which topic when, message sizes and timing. It **cannot see** content,
names, operations or roles. That metadata is the remaining privacy cost.

**Mobile.** Android: background sync needs a foreground service; `dataSync` services get 6 hours in 24 from
Android 15 ([Android docs](https://developer.android.com/develop/background-work/services/fgs/timeout)). iOS stops
an app's low-level networking once it is suspended ([Apple forums](https://developer.apple.com/forums/thread/760431)),
so an iPad cannot stay reachable as a peer: the mailbox matters most there.

**Recommendation:** a `CollabTransport` interface with a LAN implementation first (mdns.h, Qt sockets, no server),
then libdatachannel with a small rendezvous/TURN server. Revisit iroh if Rust enters the build for another reason
(the text CRDT).

## 3. End-to-end encryption and identity

- **Device identity:** each installation makes an Ed25519 key pair (kept in the platform's key store where there is
  one). A **user** is a display name plus a set of device keys that vouch for each other (pair a new device by QR).
  Others verify a person once, by a QR or a short "safety number", as messengers do.
- **Document key (start here):** a random symmetric key per shared document; ops and blobs are encrypted with an
  AEAD (XChaCha20-Poly1305) and **signed by the author's device key** (Ed25519, cheap per stroke). This is
  Excalidraw's design (the key travels in the link's `#fragment`, the relay sees only ciphertext;
  [overview](https://mintlify.wiki/excalidraw/excalidraw/concepts/collaboration), not opened) plus signatures, which
  the permission model needs.
- **Connections** between peers use a Noise handshake (XX, mutual authentication, as libp2p does) or DTLS inside
  WebRTC, bound to the device keys.
- **Invitations:** a link or QR with a document ID, a rendezvous hint and an **invite secret** that expires (one or N
  uses). For a code read aloud in class, a **PAKE** turns a short code such as `7-tiger-river` into a strong key
  with one online guess per attempt, no offline dictionary attack (magic-wormhole uses SPAKE2
  ([docs](https://magic-wormhole.readthedocs.io/en/latest/welcome.html)); the CFRG recommends CPace
  ([CFRG](https://github.com/cfrg/pake-selection/blob/master/README.md))). The newcomer then gets the document key
  over that channel and a capability signed by the owner.
- **Removing someone:** a new document key, sent to each remaining member pairwise (O(n), fine for 30–100). What the
  removed person already has stays with them.
- **Group key agreement:** **MLS** (RFC 9420) gives forward secrecy and post-compromise security with logarithmic
  updates; [OpenMLS](https://github.com/openmls/openmls) (Rust, 0.8.x, RFC status) and Cisco's
  [MLSpp](https://github.com/cisco/mlspp) (C++17, OpenSSL/BoringSSL) implement it. MLS assumes commits arrive in an
  agreed order (a delivery service), which clashes with pure P2P. Ink & Switch's **Keyhive** targets exactly this:
  "convergent capabilities", the **BeeKEM** group key agreement for concurrent, decentralised updates, and **Beelay**,
  a sync relay for E2E-encrypted Automerge data ([Keyhive](https://www.inkandswitch.com/keyhive/notebook/), not
  opened). `keyhive_core` is at 0.4 (June 2026), an early preview without a security audit (lib.rs summary, not
  opened). Background: the [local-first software](https://www.inkandswitch.com/local-first/) essay (Kleppmann et al.,
  2019; not opened).
- **Matrix as the transport** (vodozemac, Olm/Megolm, [matrix-rust-sdk](https://matrix-org.github.io/matrix-rust-sdk/matrix_sdk_crypto_ffi/index.html)):
  for: mature E2EE, device verification, federation, offline delivery, rooms with power levels. Against: a homeserver
  and accounts (not P2P), a large Rust SDK, room events as a carrier for streams of ink ops (rate limits, history
  kept forever; to check). At most a later bridge.
- **Cryptography:** vendored [Monocypher](https://monocypher.org/) (CC0/BSD-2, under 2,000 lines, X25519,
  XChaCha20-Poly1305, Ed25519, BLAKE2b) covers everything above except MLS; whether OpenSSL is already in the bundle
  (through qpdf or Qt) is to check. Composition of primitives (the protocol) still needs an outside review before
  anyone relies on it.

**Recommendation:** document key + signed ops + rotation on removal first; keep the key-management behind an
interface so MLS or Keyhive can replace it when groups get large or the threat model includes compromised devices.

## 4. Permissions

- **Server-enforced** permissions need a trusted server that can read ops: not available with E2E and P2P.
- **Capabilities checked by every peer:** the document's genesis record names the owner's key; the owner signs
  **delegations** `{to: device key, document, allowed: [operation patterns], until, may delegate}`, like UCAN's
  delegation/invocation split ([UCAN spec](https://ucan.xyz/specification/), 1.0 release candidate). Every op is
  signed by its author; each peer checks it against the author's chain and **drops, never forwards** what fails. A
  malicious peer can edit its own copy, but nobody takes its ops.
- **Revocation** races with concurrent ops. Rule: ops concurrent with or after a revocation are rejected by everyone
  who has seen it (Keyhive's "convergent capabilities"; Matrix's auth rules over its event graph).
- **Operations, not tools, are checked**: `stroke.insert{layer, tool}`, `element.delete{author}`,
  `element.transform`, `markdown.edit`, `page.insert/delete/move`, `background.set`, `layer.add`. The UI only hides
  tools. Laser, cursors and "follow my view" are **ephemeral** messages with their own permission.

| Role | Allowed |
| --- | --- |
| Teacher / owner | everything; delegates; removes members |
| Student | ink, text and Markdown **on their own layer** (a layer created for their key); delete/move only their own elements; laser; no page ops |
| Reviewer | sticky notes and comments only; their own notes may be edited by them |
| Agent | a dedicated agent layer, sticky notes, **suggestions** (ops that stay proposals until a human with the right capability accepts them); no deletes of others' elements |
| Viewer | read only (ops never accepted from them) |

In Xournal++, per-person layers are ordinary layers.

## 5. Agents as collaborators

### Two ways in

1. **The app as an MCP server** (idea B2): `xournal-qt-cli mcp` over stdio (MCP's transports are stdio and
   Streamable HTTP; current revision 2026-07-28 per search results, not opened). Reads: `search` (fuzzy, handwriting
   included), `read_page` (text, handwriting readings, Markdown, annotations), `render_page` (PNG for vision models),
   `outline`, `backlinks`. Writes: `add_sticky_note`, `add_markdown_box`, `highlight_region`, `add_shapes`,
   `propose` (a suggestion). Writes reach the running app through `SingleInstance`, else the file.
2. **The agent as a peer** in a live session: the same CLI joins with an **agent capability** and offers MCP to the
   agent. One permission path for people and agents; the agent has a cursor and a layer and can be removed.

For the named modes the app must also **call a model itself** (an in-app agent loop with a provider-neutral
endpoint, local or remote). MCP sampling (the server using the client's model) is an alternative where clients
support it (to check).

### Modes as UX patterns

| Mode | What it does | Trigger | Output |
| --- | --- | --- | --- |
| **Rubber duck** | listens while the user explains (voice or writing), asks short questions, does not solve | explicit start; then a pause after a question (a `?` in the handwriting reading) or after speech ends | one short spoken or sticky-note question at a time |
| **Teacher** | checks a step or a page, gives graduated hints (hint → stronger hint → solution only on request), points at the step that is wrong | "Check this" on a lasso selection or page; never unasked | a circle or underline on the agent layer, a hint note, optionally spoken |
| **Colleague / brainstorm** | adds ideas, counter-arguments, references from the library (search + backlinks) | on request, or opt-in at pauses with a rate limit | sticky notes in the margin, a Markdown box, suggestions |

Proactive triggers (pauses, question marks) should be opt-in, rate-limited, and visible ("the agent is looking at
page 3").

### Privacy and latency

- **What leaves the device:** page pictures, handwriting readings, Markdown, audio: per-document consent, a scope
  (this page or selection only), an indicator.
- **Local vs cloud:** local models keep student work on the device (ONNX Runtime is already vendored); which local
  speech, vision and language models suffice is to check. Cloud models are a data-protection question (minors).
- **Latency:** spoken turn-taking needs streaming in and out; page checks may take seconds. Vendor capabilities,
  latencies and prices: to check.

### Audio

- **Input:** push-to-talk first (no always-on microphone). Either speech-to-text then a text model, or a model that
  takes audio natively (to check per provider/local model).
- **Output:** generated speech. Locally this needs a vendored TTS model (Qt TextToSpeech needs speech-dispatcher on
  Linux, against "self-contained", as idea A15 notes).
- **Synchronised with ink:** the conversation is recorded as `qt/audio` recordings (mono Ogg Vorbis, attachments in
  the PDF with notes; [audio.md](../audio.md)), strokes and agent notes carry upstream's `ts`/`fn`, and the
  transcript is kept as Markdown. The **B9 timeline** then replays the session: who wrote what, what the agent said.

## 6. What fits VISION, sizes, and a roadmap

- **Self-contained:** all of it runs in the app; the server is optional and separate.
- **Vendored:** Monocypher and mdns.h are single files; libdatachannel is a CMake project with small submodules.
  **Rust is the fork in the road:** Automerge, Yrs, Loro, iroh, OpenMLS and Keyhive mean cargo in every platform's
  build, `cargo vendor` trees in `qt/3rdparty` (iroh's is large; to check) and
  [Corrosion](https://github.com/corrosion-rs/corrosion) for CMake.
- **Folders clean, Xournal++ compatible:** history and keys in the app's data/cache; files gain only ignored attributes.

| Building block | Size (estimate) |
| --- | --- |
| `xqt-id` (and `xqt-created`) seam, content-hash fallback | S–M |
| MCP server, read-only (B2) | M (+ the CLI search if not there) |
| MCP writes: agent layer, sticky notes, suggestions with accept/reject UI | M |
| Sync layer: diff by ID, element-map CRDT, apply remote ops, ID-based undo | L (3–5 weeks) |
| LAN transport + discovery + Noise/AEAD + invitations (QR, code with PAKE) | M–L |
| Capabilities and op validation, roles UI | M–L |
| libdatachannel transport + rendezvous/TURN/mailbox server | L |
| Text CRDT for Markdown (a Rust library via C FFI) | L, plus the toolchain decision |
| In-app agent modes with voice | L |

**Staged roadmap**

1. **IDs** (S–M): useful on their own (B9 Level 2, links to elements later).
2. **MCP server for local agents** (M, then M): the cheapest real step to "agents as collaborators", no network.
3. **Read-only live sharing over LAN** (M–L): the teacher's document streams to students' devices (snapshot, then
   ops); students follow the teacher's page or browse freely and may write privately on their own copy. One-way, so
   no CRDT, no permissions beyond "read", no undo issues.
4. **Two-way co-editing on LAN** with roles (L): the element-map CRDT, signed capabilities, ID-based undo.
5. **Over the internet** (L): libdatachannel, the small server, the mailbox; mobile behaviour.
6. **Agent as a peer, modes, voice** (L).
7. **Markdown co-editing** with a text CRDT; MLS/Keyhive if groups or threats need it.

## 7. Risks and open questions

- **Pen feel:** local strokes never wait for the network; remote ops are applied off the input path, coalesced per
  frame; others' ink in progress goes on the ephemeral channel.
- **Divergence:** upstream tools mutate elements in place; a change the diff misses means replicas drift. Property
  tests (random ops on several replicas converge) are required.
- **Security:** a protocol mistake defeats E2E (review); a valid member flooding ops (limits per author).
- **Classroom networks:** client isolation, blocked UDP; a relay over TCP 443 may be the only way (to check).
- **Data protection:** student work, minors, audio, cloud agents: consent and a "local only" mode.
- **History:** erased ink survives in op history; keep it out of shared files.
- **Protocol versions:** old peers must reject what they do not understand, not corrupt it.

**Not verified here:** iroh 1.0's date and claims, the MCP revision date, Keyhive's version and status, the state of
Automerge's C API and list move, Loro's C bindings, binary sizes of every library, OpenSSL in the bundle, school
network behaviour, and all model capabilities.

## 8. The input path, and Rust in the build (the author's questions, 2026-10-05)

**Local input does not change.** Pen and keys go the way they go today: the stroke is drawn by the canvas overlay
while the pen moves and committed to the document on pen-up as one undo step; a key edits the text element. The sync
layer sits beside the document and only *observes* committed changes:

| Moment | Work for collaboration | Where | Cost (estimate) |
| --- | --- | --- | --- |
| Pen moving | none; optionally the points of the stroke in progress are pushed into a queue for the others' live preview | input thread: a queue push only; batching (every 30–50 ms), encryption and sending on a worker | negligible |
| Pen up | the new element gets its ID (and `xqt-created`), is diffed, encoded, signed (Ed25519), encrypted (XChaCha20-Poly1305), sent | worker thread, after the commit | tens of µs for a 1–5 KB stroke |
| A key typed in Markdown | the same edit mirrored into the text CRDT (the Qt text stays the truth; the CRDT follows) | after the edit, batched per frame | µs per operation (Yrs/Loro handle millions per second) |
| Network slow or gone | nothing waits: operations queue and go when the peer is back | worker | none |

**What can cost smoothness is the others' work arriving**, not one's own: applying remote operations changes the
document while one writes. Rules: remote operations are applied in batches once per frame with a time budget
(e.g. 2 ms), holding the document lock only to insert or remove elements (never while drawing); the page is re-drawn
only in the tiles they touch, as the timeline replay does; one's own stroke in progress is drawn above everything and
is never disturbed; the others' strokes in progress are an overlay (ephemeral, not in the document); a burst (someone
pastes 50 pages) is applied in the background with a progress mark. A benchmark guards it: pen-down to pixel latency
(`InputLog`) with three simulated peers writing on the same page must stay what it is alone.

**Text** is the same: one's own typing never goes through the network or the CRDT first. Concurrent typing in the
same Markdown box merges character by character (a text CRDT); a remote edit moves one's cursor only by the
characters inserted before it.

**Rust in the CMake build**, if a Rust library (Loro, Yrs, Automerge, iroh, OpenMLS) is chosen:
- [Corrosion](https://github.com/corrosion-rs/corrosion) imports a crate into CMake (`corrosion_import_crate(MANIFEST_PATH ...)`)
  and links it like any library; cargo runs as part of the build.
- **One wrapper crate of our own** (`qt/rust/xqt-sync`): it depends on the chosen libraries and exports a small C
  API (opaque handles, byte buffers, a header generated by cbindgen), so C++ sees only plain C functions. Only one Rust
  static library is linked: several would each carry Rust's standard library (duplicate symbols, size). For a richer
  C++ API the `cxx` crate is the alternative; a plain C API is simpler to keep stable.
- **Threads:** the Rust side runs on the sync worker thread only, behind the same queue as the C++ side.
- **Per platform:** Linux `x86_64-unknown-linux-gnu`; Android `aarch64-linux-android` with the NDK's linker (Corrosion
  follows the CMake toolchain); macOS `aarch64-apple-darwin`; Windows (MSYS2 UCRT64) MSYS2's own Rust package or the
  `x86_64-pc-windows-gnullvm`/`-gnu` target, whichever matches UCRT64 (to check).
- **Reproducible and offline:** `Cargo.lock` committed and `cargo vendor` into `qt/3rdparty/rust` (builds with
  `--offline`, as the pinned qpdf); the CI installs a pinned rustc with rustup, since Ubuntu 22.04's packaged rustc
  is likely older than these libraries need (to check per library).
- **Size:** a text CRDT adds roughly 1–3 MB to the app; iroh with its QUIC and TLS stack considerably more (to
  measure). Building cargo adds minutes to a clean CI build (cached afterwards).
- **Without Rust:** the plan's recommendation stands: ink sync as our own small C++ CRDT (no Rust), so Rust only
  enters if Markdown co-editing is wanted, and then as one wrapper crate.

## Decisions for the author

1. **Is collaboration worth its weight at all**, given it is the largest feature yet? *Recommendation:* yes as a
   direction, but build stages 1–3 first and decide on two-way editing after using them.
2. **Rust in the build?** *Recommendation:* not for ink sync (own C++ element-map CRDT); decide only when Markdown
   co-editing is wanted, then pick one Rust library (Loro first to evaluate, for moves and local undo).
3. **CRDT beside the model, not as the model.** *Recommendation:* beside it.
4. **`xqt-id` always, or only in shared documents?** *Recommendation:* only in documents that are shared (or that
   need it for another feature), with the content-hash fallback; revisit if element links come.
5. **Transport:** *Recommendation:* LAN (no server) first, then libdatachannel; iroh only if Rust is in anyway.
6. **Servers:** *Recommendation:* one small self-hostable binary (rendezvous, TURN-like relay, mailbox); the user
   enters its address. Whether the project runs a public one (cost, abuse) is a later choice.
7. **Encryption:** *Recommendation:* document key + signed ops + rotation; MLS/Keyhive later behind an interface.
8. **Roles:** *Recommendation:* the five presets above over operation-level capabilities; tools are only the UI.
9. **Undo in shared documents:** *Recommendation:* my own operations only, by ID.
10. **Agents:** *Recommendation:* MCP server (B2) first; the agent as a capability-limited peer later; a
    provider-neutral model endpoint; local models where good enough.
11. **Agent behaviour:** *Recommendation:* explicit triggers only at first; proactive modes opt-in and rate-limited;
    push-to-talk, no always-on microphone.
12. **Matrix:** *Recommendation:* not as the base; maybe a bridge later.
