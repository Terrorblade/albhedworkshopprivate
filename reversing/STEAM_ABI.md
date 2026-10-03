# Steam ABI for Al Bhed Workshop

What this is: the evidence behind `loader/workshop/include/workshop/SteamAbi.h`. That
header declares the Steamworks interfaces we call inside the FFX HD process, without an
SDK. This file says where each fact came from and how much to trust it.

The rule this exists to satisfy: do not write Steam interface code from memory, check
the header. This document is the check.

Date of investigation: 2026-10-02.

---

## The headline correction

The shipped `steam_api.dll` is **Steamworks SDK 1.31**, not 1.34-1.37 as we had
estimated going in.

I pinned that down by sweeping Valve's own version-pinned SDK trees and reading the
`STEAMCLIENT_INTERFACE_VERSION` out of each one:

| SDK | STEAMCLIENT_INTERFACE_VERSION | STEAMNETWORKING_INTERFACE_VERSION |
|---|---|---|
| 1.25 - 1.28 | SteamClient012 | SteamNetworking005 |
| 1.29 | SteamClient014 | SteamNetworking005 |
| 1.30 | SteamClient015 | SteamNetworking005 |
| **1.31** | **SteamClient016** | **SteamNetworking005** |
| 1.32 - 1.42 | SteamClient017 | SteamNetworking005 |

The game's DLL contains `SteamClient016`, so it is SDK 1.31.

**This does not matter for networking, and that is the important part.**
`isteamnetworking.h` is byte-for-byte identical across every SDK from 1.25 to 1.42
(md5 `300006747a99ccf176155b1333f6a87f` on all ten trees I downloaded), and all of them
declare `SteamNetworking005`. The interface simply did not change over that span. So
the vtable in our header is not sensitive to exactly which of those SDKs Square Enix
built against.

It does matter for `ISteamClient`, which changed a lot across those versions. See the
`GetISteamGenericInterface` section below.

---

## Sources

**[S1] Valve's own SDK headers, version-pinned, from Valve's Proton repository.**
`https://raw.githubusercontent.com/ValveSoftware/Proton/proton_8.0/lsteamclient/steamworks_sdk_131/`
with `isteamnetworking.h`, `isteamclient.h`, `steamclientpublic.h`, `steam_api.h`,
`steamtypes.h`. Proton vendors complete per-version copies of the real SDK headers in
`lsteamclient/steamworks_sdk_<version>/`, which is why this is usable as a primary
source rather than a mirror: it is Valve publishing Valve's own SDK. I downloaded the
`125` through `142` trees to do the version sweep above.

**[S2] SteamRE/open-steamworks.** `ISteamNetworking005.h` and `ISteamClient016.h` from
`https://raw.githubusercontent.com/SteamRE/open-steamworks/master/Open%20Steamworks/`.
These are reverse engineered from the real shipping `steamclient.dll`, not copied from
a header, and they are versioned per interface revision. That makes them genuinely
independent of [S1]: if [S1] and [S2] agree on method order, a header typo or a
Proton-side edit is ruled out.

**[S3] The MSVC x86 compiler on this machine.** `cl.exe` 19.29.30159 for x86, from
`VC/Tools/MSVC/14.29.30133/bin/Hostx86/x86`. I used `-d1reportSingleClassLayout<name>`
to make it print the vtable order and struct offsets it actually produces, and `-FAs`
to make it print the actual call sequences. This is the strongest source available for
anything about MSVC's own ABI choices, because it is the same compiler that will build
the mod. Not an opinion about what MSVC does, a transcript of what it did.

**[S4] A newer-SDK mirror, as a sanity cross-check only.**
`julianxhokaxhiu/SteamworksSDKCI` at master, which is `SteamNetworking006`. Used only
to confirm the method ordering is stable into the next interface revision.

Not used: `C:\Program Files (x86)\Steam\steamclient.dll` and the IDA MCP server. The web
sources left no real conflict on anything we need, so opening a second 21 MB database
was not justified. The FFX database was not touched.

---

## Item-by-item confidence

### 1. ISteamNetworking005 vtable, 22 slots - VERIFIED, three independent sources

| Slot | Method |
|---|---|
| 0 | `SendP2PPacket( CSteamID, const void*, uint32, EP2PSend, int nChannel )` |
| 1 | `IsP2PPacketAvailable( uint32*, int nChannel )` |
| 2 | `ReadP2PPacket( void*, uint32, uint32*, CSteamID*, int nChannel )` |
| 3 | `AcceptP2PSessionWithUser( CSteamID )` |
| 4 | `CloseP2PSessionWithUser( CSteamID )` |
| 5 | `CloseP2PChannelWithUser( CSteamID, int nChannel )` |
| 6 | `GetP2PSessionState( CSteamID, P2PSessionState_t* )` |
| 7 | `AllowP2PPacketRelay( bool )` |
| 8 | `CreateListenSocket( int, uint32 nIP, uint16, bool )` |
| 9 | `CreateP2PConnectionSocket( CSteamID, int, int, bool )` |
| 10 | `CreateConnectionSocket( uint32 nIP, uint16, int )` |
| 11 | `DestroySocket( SNetSocket_t, bool )` |
| 12 | `DestroyListenSocket( SNetListenSocket_t, bool )` |
| 13 | `SendDataOnSocket( SNetSocket_t, void*, uint32, bool )` |
| 14 | `IsDataAvailableOnSocket( SNetSocket_t, uint32* )` |
| 15 | `RetrieveDataFromSocket( SNetSocket_t, void*, uint32, uint32* )` |
| 16 | `IsDataAvailable( SNetListenSocket_t, uint32*, SNetSocket_t* )` |
| 17 | `RetrieveData( SNetListenSocket_t, void*, uint32, uint32*, SNetSocket_t* )` |
| 18 | `GetSocketInfo( SNetSocket_t, CSteamID*, int*, uint32*, uint16* )` |
| 19 | `GetListenSocketInfo( SNetListenSocket_t, uint32*, uint16* )` |
| 20 | `GetSocketConnectionType( SNetSocket_t )` |
| 21 | `GetMaxPacketSize( SNetSocket_t )` |

Slots 8-21 are the deprecated Berkeley-sockets API. Listed for completeness, never
called. The SDK header itself says to prefer the P2P functions and that these "will be
removed eventually".

Why declaration order is vtable order: MSVC emits virtuals in declaration order for a
class with single inheritance, no virtual bases, and no overloaded virtual names.
`ISteamNetworking` meets all three, and in particular all 22 names are distinct so
there is no overload group to be reordered. That precondition is load-bearing rather
than boilerplate, because MSVC genuinely does reorder overload groups and that is
exactly what bites in `CCallbackBase` below.

Evidence:
- [S1] `steamworks_sdk_131/isteamnetworking.h`, `class ISteamNetworking` at line 126 to
  line 261, with `#define STEAMNETWORKING_INTERFACE_VERSION "SteamNetworking005"` on
  line 262. Declaration order read straight off the file.
- [S2] `ISteamNetworking005.h`, reverse engineered from the shipping DLL: same 22
  methods, same order, same signatures.
- [S3] compiled the class and dumped `ISteamNetworking::$vftable@`, which listed slots
  0 through 21 in exactly this order, and reported `this adjustor: 0` for every method,
  so there are no adjustor thunks. Object size 4, just the vtable pointer.
- [S3] again, end to end: I compiled calls through the header's own
  `ISteamNetworking005` class and read the emitted vtable byte offsets.
  `SendP2PPacket` -> `[eax]`, `IsP2PPacketAvailable` -> `[eax+4]`,
  `ReadP2PPacket` -> `[eax+8]`, `AcceptP2PSessionWithUser` -> `[eax+12]`,
  `GetP2PSessionState` -> `[eax+24]`. Divided by 4 that is slots 0, 1, 2, 3, 6, which
  matches the documented constants.
- [S4] `SteamNetworking006` lists the same 22 in the same order.

### 2. Calling convention and CSteamID passing - VERIFIED from real codegen

`__thiscall`: interface pointer in ECX, declared arguments pushed right to left, callee
pops. A by-value `CSteamID` occupies exactly 8 bytes of the argument area, low dword at
the lower address, identical to a by-value `uint64`.

[S3] gave this for `p->SendP2PPacket( CSteamID( rawSteamId ), data, len, k_EP2PSendReliable, 0 )`:

```
mov  ecx, DWORD PTR _p$[esp-4]        ; this -> ECX
push 0                                ; nChannel (pushed first = last arg)
push 2                                ; eP2PSendType
push DWORD PTR _len$[esp+4]           ; cubData
mov  eax, DWORD PTR [ecx]             ; vtable
push DWORD PTR _data$[esp+8]          ; pubData
push DWORD PTR _rawSteamId$[esp+16]   ; CSteamID high dword
mov  eax, DWORD PTR [eax]             ; vtable[0]
push DWORD PTR _rawSteamId$[esp+16]   ; CSteamID low dword (esp has moved by 4)
call eax
ret  0                                ; no caller stack fixup -> callee pops
```

Two pushes for the 8-byte value and no `add esp` after the call. On MSVC x86 a class
passed by value is always copied onto the stack, never passed in a register and never
silently converted to a hidden pointer, regardless of constructors. So declaring these
parameters as `uint64` is byte-identical, and the header does that deliberately to
remove a category of mistake.

I also verified the raw `__thiscall` function-pointer form produces the identical
instruction sequence to the virtual call, so either spelling is safe.

No method in this interface returns a struct, so there is no hidden return-pointer
argument anywhere. `bool` returns come back in AL with the upper EAX bits
unspecified, so declare the return as `bool` and let the compiler test the byte.

### 3. CSteamID layout - VERIFIED

8 bytes. Union of a bitfield struct and a `uint64`, declared inside
`#pragma pack( push, 1 )`, no virtual functions so no vptr.

- [S1] `steamworks_sdk_131/steamclientpublic.h`, `class CSteamID` at line 402, inside a
  `#pragma pack( push, 1 )` that opens at line 397.
- [S3] `class CSteamID size(8): 0 | SteamID_t m_steamid`.

Bit layout: account id bits 0-31, account instance bits 32-51, account type bits 52-55,
universe bits 56-63. Build one from a raw `uint64` by assignment, it is the same 8
bytes. The `pack(1)` is not cosmetic, see item 6.

### 4. EP2PSend - VERIFIED, three sources agree

`k_EP2PSendUnreliable = 0`, `k_EP2PSendUnreliableNoDelay = 1`,
**`k_EP2PSendReliable = 2`**, `k_EP2PSendReliableWithBuffering = 3`.

[S1] `isteamnetworking.h` lines 33-56. Corroborated by [S2] and [S4].

### 5. P2PSessionState_t - VERIFIED, 20 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | uint8 | m_bConnectionActive |
| 1 | uint8 | m_bConnecting |
| 2 | uint8 | m_eP2PSessionError |
| 3 | uint8 | m_bUsingRelay |
| 4 | int32 | m_nBytesQueuedForSend |
| 8 | int32 | m_nPacketsQueuedForSend |
| 12 | uint32 | m_nRemoteIP |
| 16 | uint16 | m_nRemotePort |
| 18 | - | 2 bytes tail padding |

Total 20.

Field order and types from [S1] `isteamnetworking.h` lines 68-78. The struct sits in a
packing block that is `pack(4)` under `VALVE_CALLBACK_PACK_SMALL` and `pack(8)` under
`VALVE_CALLBACK_PACK_LARGE`. Which applies is decided in [S1] `isteamclient.h` lines
24-33: SMALL on `__linux__` or `__APPLE__`, LARGE otherwise. We are Windows, so
`pack(8)`, which changes nothing here since no member needs more than 4-byte alignment.
Offsets and the total size confirmed by [S3].

`m_nRemoteIP` can be a Steam relay address rather than the peer's real address. The SDK
header calls this struct debug-only, so treat it as diagnostics and not as control flow.

### 6. P2PSessionRequest_t, P2PSessionConnectFail_t, callback ids - VERIFIED

**Networking callback base is 1200.** [S1] `isteamclient.h` declares the bases as a run
of single-enumerator enums, including `enum { k_iSteamNetworkingCallbacks = 1200 };`.
For context: User 100, GameServer 200, Friends 300, Matchmaking 500, Utils 700,
UserStats 1100, Networking 1200, ClientRemoteStorage 1300.

| Struct | k_iCallback | Size | Fields |
|---|---|---|---|
| `SocketStatusCallback_t` | 1200 + 1 = **1201** | - | legacy socket API, unused |
| `P2PSessionRequest_t` | 1200 + 2 = **1202** | **8** | `CSteamID m_steamIDRemote` at +0 |
| `P2PSessionConnectFail_t` | 1200 + 3 = **1203** | **9** | `CSteamID m_steamIDRemote` at +0, `uint8 m_eP2PSessionError` at +8 |

[S1] `isteamnetworking.h` lines 275-302. Sizes and offsets confirmed by [S3].

**`P2PSessionConnectFail_t` really is 9 bytes, not 12 and not 16.** This is the subtle
one. `CSteamID` was declared under `pack(1)` at its own definition, so its alignment
requirement is 1, and the enclosing `pack(8)` can only lower alignment, never raise it.
So the `uint8` lands at +8 with no tail padding. If someone declares this struct with a
plain `uint64` instead of a `pack(1)` type, the `uint64`'s 8-byte alignment makes the
struct 16 bytes, and then `GetCallbackSizeBytes()` reports 16 to Steam and Steam copies
16 bytes into a 9 byte expectation. That is why the header's `SteamId` type is `pack(1)`
and why there is a `static_assert` on both sizes.

### 7. CCallbackBase and SteamAPI_RegisterCallback - layout VERIFIED, runtime INFERRED

Object layout, from [S1] `steam_api.h` class at line 162 and confirmed by [S3]:

```
class CCallbackBase  size(12):
 0   | {vfptr}
 4   | m_nCallbackFlags        (uint8, then 3 bytes padding)
 8   | m_iCallback             (int)
```

No `#pragma pack` anywhere in `steam_api.h`, so this is default packing. There is
deliberately no virtual destructor, the header says so in a comment. `GetICallback()`
is **not** virtual, so it takes no slot. Exactly three virtual slots.

**The dangerous part, and the one real trap in this whole job: `Run` is overloaded, and
MSVC emits an overload group in REVERSE declaration order.** So the vtable is *not*
`[Run(void*), Run(void*,bool,uint64), GetCallbackSizeBytes]`.

I did not want to rely on knowing that rule, so I asked [S3] directly: I gave the two
overrides distinguishable bodies and read the mangled names out of the real object file.

```
??_7MyCb@@6B@ DD FLAT:??_R4MyCb@@6B@             ; MyCb::`vftable'
    DD FLAT:?Run@MyCb@@UAEXPAX_N_K@Z             ; slot 0
    DD FLAT:?Run@MyCb@@UAEXPAX@Z                 ; slot 1
    DD FLAT:?GetCallbackSizeBytes@MyCb@@UAEHXZ   ; slot 2
```

`PAX_N_K` is `(void*, bool, unsigned __int64)`, plain `PAX` is `(void*)`. So the real
order is:

| Slot | Method |
|---|---|
| 0 | `Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall )` (3 args) |
| 1 | `Run( void *pvParam )` (1 arg) |
| 2 | `GetCallbackSizeBytes()` |

The three-argument CallResult form is **first**. Ordinary callbacks like
`P2PSessionRequest_t` are delivered through **slot 1**.

The practical consequence is a little counterintuitive and the header spells it out: if
you declare the two overloads in the SDK's own declaration order, MSVC applies the same
reversal to your class that it applied to Valve's, so the reversal cancels and your
vtable matches what the DLL expects. **Declare them in SDK order and do not "fix" it.**
The only time you must hand-write the reversed order is if you build the vtable
yourself as an explicit array of function pointers.

`SteamAPI_RegisterCallback( CCallbackBase*, int )` is `extern "C" __cdecl`. [S1]
`steam_api.h` line 151, with `S_API` = `extern "C"` (line 41) and `S_CALLTYPE` =
`__cdecl` ([S1] `steamtypes.h` line 13). It ORs `k_ECallbackFlagsRegistered` (0x01)
into `m_nCallbackFlags` itself, so initialise that to 0 and set `m_iCallback` yourself.

**Confidence split.** The layout and the slot order are *verified* - layout from Valve's
own header, slot order from the real compiler. What is **inferred** is that the shipped
`steam_api.dll` actually dispatches through these slots as expected at runtime, because
confirming that needs the game running. The layout is right and is consistent with the
header the DLL was built from, but nobody has executed it.

**Recommendation: skip callbacks entirely.** Both peers already know each other's
SteamID before the session starts, because that is how we set the session up. The SDK
header for `AcceptP2PSessionWithUser` states that calling `SendP2PPacket` to a peer
implicitly accepts that peer's pending session request, and that
`AcceptP2PSessionWithUser` is safe to call more than once for the same user. So have
every peer proactively call `AcceptP2PSessionWithUser` on every other peer at session
start, then poll `IsP2PPacketAvailable` / `ReadP2PPacket` on our own tick. That needs no
callback registration, no hand-rolled vtable, no object-lifetime hazard and no thread
question, and at 3 players it costs 2 extra calls per peer. The header ships the
callback shim anyway, with the reversal documented, but the polling route is the one to
take. This is the cheaper path *and* it retires the only item on this list whose runtime
behaviour is unverified.

### 8. ISteamClient016::GetISteamGenericInterface - slot 12, VERIFIED by two sources

`GetISteamGenericInterface` is **slot 12**. `GetISteamNetworking` is **slot 16**.

- [S1] `steamworks_sdk_131/isteamclient.h`, which is the SDK whose
  `STEAMCLIENT_INTERFACE_VERSION` is exactly the `SteamClient016` string in the game's
  DLL, so this is the matching revision and not a near miss. Counting virtuals in
  declaration order from 0 puts it at 12.
- [S2] `ISteamClient016.h`, reverse engineered from the shipping `steamclient.dll`, gives
  the same two indices independently.

**Why pinning the version mattered.** In the older `SteamClient009` this interface had
`GetISteamMasterServerUpdater` at slot 11, which was later removed, shifting
`GetISteamGenericInterface` from 13 down to 12. An `isteamclient.h` grabbed from a random
SDK gives an index that is wrong by one, and off-by-one on a vtable index means calling
`GetISteamMatchmakingServers` with a mismatched argument list. I hit this: my first
cross-check source (`hl2sdk-csgo`, see disagreements) is `SteamClient009` and reports 13.

**Honest limit.** Slots 0-22 are identical between [S1] and [S2]. From slot 23 up they
**disagree**: Valve's 1.31 header has `GetISteamPS3OverlayRender()` at 23 while
open-steamworks' `ISteamClient016` goes straight to `GetISteamHTTP`. I did not resolve
that and did not need to. **Treat any `ISteamClient016` slot >= 23 as unverified.**

Separately, and this is not an ABI question: the shipped `steam_api.dll` contains only
the `SteamNetworking005` version string. The modern interfaces live in the installed
Steam client's `steamclient.dll`, so whether asking this pipe for
`SteamNetworkingSockets012` returns a non-null pointer is an empirical question about
the running client. Check for null. And the **vtable layout of whatever comes back is
not described in our header** - having a pointer is not permission to call through it.

### 9. Packet size and channel limits at 3-player scale - VERIFIED from the SDK header

This is the answer to "will reliable mode fragment above 1200 bytes".

| Mode | Max payload | Notes |
|---|---|---|
| `k_EP2PSendUnreliable` (0) | **1200 bytes** | "Packets can't be bigger than 1200 bytes (your typical MTU size)". Batched while NAT traversal runs. |
| `k_EP2PSendUnreliableNoDelay` (1) | **1200 bytes** | Thrown away rather than batched if the connection is not up. Header warns the first packet to a host "almost guarantees the packet will be dropped". Voice only, do not use. |
| `k_EP2PSendReliable` (2) | **1 MB per message** | **Yes, it fragments for you.** Header: "Does fragmentation/re-assembly of messages under the hood, as well as a sliding window for efficient sends of large chunks of data." |
| `k_EP2PSendReliableWithBuffering` (3) | **1 MB per message** | As reliable, plus Nagle: accumulates until ~1 MTU or ~200 ms. Flush by following with a plain `k_EP2PSendReliable`. |

So: **the 1200 byte cap is required for the two unreliable modes and is not required for
the two reliable ones.** Keep capping the frequent unreliable traffic at 1200 to stay
inside one datagram. Reliable control messages can safely exceed it up to 1 MB, which is
far more headroom than 3 players need. Quotes are from [S1] `isteamnetworking.h` lines
35-54, the per-enumerator comments.

Channels: `nChannel` is a plain `int` and **no maximum channel count is documented** in
any source I checked, so I am not going to invent one. What the header does say is that
"using different channels to talk to the same user will still use the same underlying
p2p connection, saving on resources", so a handful of channels is cheap. Nothing in the
API imposes a per-peer limit that bites at 3 peers.

Two other operational notes worth having: packet relay through Steam's servers is
**allowed by default**, so `AllowP2PPacketRelay` does not need calling to get relaying.
And `k_EP2PSessionErrorTimeout` usually means the peer never called
`AcceptP2PSessionWithUser`, though the header notes corporate firewalls cause it too, in
which case UDP ports 3478, 4379 and 4380 need to be open outbound.

---

## Where sources disagreed

**1. SDK dating, resolved.** We came in believing 1.34-1.37. It is 1.31. Caught by the
`SteamClient016` sweep. No effect on networking, since `isteamnetworking.h` is identical
across 1.25-1.42.

**2. `hl2sdk-csgo` is `SteamNetworking003`. Do not use it.** My first cross-check fetch
landed on `pmrowla/hl2sdk-csgo`, which returned a 20-slot table with **no**
`CloseP2PChannelWithUser`, **no** `AllowP2PPacketRelay`, and **no** `nChannel` parameter
on the first three methods. That is a genuinely different and incompatible interface, not
a variant spelling. Taking slot indices from it would put `GetP2PSessionState` at 5
instead of 6 and `CreateListenSocket` at 6 instead of 8. Resolved by version-pinning to
[S1] 1.31 rather than trusting a repo's default branch. Same repo also gave
`SteamClient009` and therefore the wrong `GetISteamGenericInterface` index of 13.
**Lesson: for this job a source is worthless unless you know which interface revision it
is.**

**3. `SteamNetworking006` differs only in the socket half.** [S4] gives the same 22
methods in the same order, but slots 8, 10, 18 and 19 take `SteamIPAddress_t` instead of
`uint32 nIP`. `SteamIPAddress_t` is a larger struct, so those four signatures are not
interchangeable between 005 and 006. The P2P half, slots 0-7, is unchanged. We call only
0-7, so this never bites us, but it is another reason not to touch the socket API.

**4. `nChannel` defaults: a difference that is actually informative.** [S1] declares
`int nChannel = 0` on slots 0, 1 and 2. [S2], working from the binary, shows the same
parameters as mandatory and names them `iVirtualPort`. Both are right and they are
describing the same thing from two directions: **a default argument is a source-level
convenience the caller fills in, it does not exist in the binary, and the callee always
expects the argument to be present.** Our header therefore declares `nChannel` with no
default, so the compiler forces every call site to pass it explicitly. Omitting it from
a hand-written thunk would shift the whole argument list.

**5. `ISteamClient016` slots >= 23, unresolved.** Described in item 8. Flagged as
unverified rather than guessed.

**6. A process note, not a source conflict.** My first two attempts to read headers via
the summarising fetch tool were refused on copyright grounds when I asked for file
contents. Rewording to ask for the specific ABI facts worked, and then direct retrieval
of Valve's own repository made the question moot. Worth knowing for next time: fetch the
file and read it locally rather than asking a summariser to reproduce it.

---

## Confidence summary

| Item | Rating |
|---|---|
| ISteamNetworking005 vtable, 22 slots, order and signatures | **Verified.** Valve's own 1.31 header, plus an independent RE source, plus the real compiler's own vtable dump, plus emitted call-site offsets. |
| `__thiscall` convention, callee-pops | **Verified** from real codegen. |
| `CSteamID` by value == 8 stack bytes, low dword first | **Verified** from real codegen. |
| `CSteamID` layout, size 8, pack(1), no vptr | **Verified.** Valve header plus compiler. |
| `EP2PSend` values | **Verified.** Three sources agree. |
| `EP2PSessionError` values | **Verified.** Valve 1.31 header. |
| `P2PSessionState_t` fields, offsets, size 20 | **Verified.** Valve header plus compiler offsets. |
| Networking callback base 1200 | **Verified.** Valve 1.31 `isteamclient.h`. |
| `P2PSessionRequest_t` = 1202, size 8 | **Verified.** Valve header plus compiler. |
| `P2PSessionConnectFail_t` = 1203, size 9 | **Verified.** Valve header plus compiler. The 9 is deliberate. |
| `CCallbackBase` offsets and size 12 | **Verified.** Valve header plus compiler. |
| `CCallbackBase` vtable order with reversed `Run` overloads | **Verified** by compiler experiment reading mangled names from the object file. |
| Shipped DLL actually dispatches callbacks through those slots at runtime | **Inferred.** Needs the game running. Avoid by polling instead. |
| `ISteamClient016::GetISteamGenericInterface` = 12 | **Verified.** Two independent sources, on the exact matching interface revision. |
| `ISteamClient016::GetISteamNetworking` = 16 | **Verified.** Same two sources. |
| `ISteamClient016` slots >= 23 | **Unverified.** Sources disagree. Not needed. |
| Per-mode max payload, 1200 unreliable / 1 MB reliable with fragmentation | **Verified.** Valve 1.31 header comments. |
| Max channel count | **Not established.** No source states a limit. Not inventing one. |
| Whether a modern `ISteamNetworkingSockets` is reachable via this pipe | **Unknown and untested.** Empirical question about the installed client, not an ABI fact. Its vtable is not described in our header. |

## Validation performed on the delivered header

`loader/workshop/include/workshop/SteamAbi.h` compiles clean with 32-bit MSVC at `/W4`
with `-permissive-`, no warnings and no errors, and every `static_assert` in it passes.
Those asserts cover: pointer size 4, `SteamId` 8, `P2PSessionState_t` 20 plus all eight
field offsets, `P2PSessionRequest_t` 8, `P2PSessionConnectFail_t` 9 plus both offsets,
both callback ids, the `CCallbackBase` prefix offsets on the shim, and
`k_EP2PSendReliable == 2`. Calls written against the header were compiled and the
emitted vtable byte offsets read back as 0, 4, 8, 12 and 24, matching documented slots
0, 1, 2, 3 and 6.

---

# Addendum: ISteamUser017 and ISteamFriends014

Added after the decision to skip callbacks entirely and use proactive
`AcceptP2PSessionWithUser` plus polling. That design needs a client to know the host's
SteamID up front, and nothing in the process could previously tell a player their own
SteamID to pass on. Same sources as above: [S1] Valve's version-pinned Proton SDK trees,
[S2] SteamRE/open-steamworks reverse engineered from the shipping `steamclient.dll`,
[S3] the real MSVC x86 compiler.

## The version mapping got stronger

Sweeping the version strings across SDK trees pins SDK 1.31 harder than before:

| SDK | CLIENT | USER | FRIENDS | NETWORKING |
|---|---|---|---|---|
| 1.25 | SteamClient012 | SteamUser017 | SteamFriends013 | SteamNetworking005 |
| 1.28 - 1.30 | 012 / 014 / 015 | SteamUser017 | SteamFriends014 | SteamNetworking005 |
| **1.31** | **SteamClient016** | **SteamUser017** | **SteamFriends014** | **SteamNetworking005** |
| 1.32 - 1.36 | SteamClient017 | SteamUser018 | SteamFriends015 | SteamNetworking005 |
| 1.37 - 1.42 | SteamClient017 | SteamUser019 | SteamFriends015 | SteamNetworking005 |

SDK 1.31 is the **only** tree where all four strings simultaneously match what is in the
game's DLL. `SteamUtils007` and `SteamMatchMaking009` also match 1.31, though those two
are stable across 1.30-1.32 so they do not discriminate on their own. That is six
version strings consistent with 1.31 and no tree that fits better. The 1.31 pin is solid.

`isteamuser.h` is byte-identical across SDK 1.28 to 1.31 (md5
`b406ee3d5f340c1401616d8a0bb79abe`), so `SteamUser017` is stable over that window too.

## The big one: returning a CSteamID by value - VERIFIED, and it is NOT EAX:EDX

This was flagged as the thing most likely to be got wrong, and the instinct was right.

**An 8-byte class or struct return on 32-bit MSVC uses a hidden return-buffer pointer,
not the EAX:EDX register pair.** The caller allocates an 8-byte temporary, passes its
address as an invisible extra argument on the stack, and the callee fills it in and
returns that same pointer in EAX.

I compiled three variants, all 8 bytes, and read the real codegen from [S3]:

- **(A)** `CSteamID` exactly as the SDK declares it: `pack(1)`, with user-declared constructors
- **(B)** a trivial `pack(1)` POD holding one `uint64`, no constructors
- **(C)** a plain `uint64`

**(A) and (B) produced byte-identical code.** So at this size, whether the type has
constructors makes no difference, which is worth knowing because the usual folklore rule
("non-trivial types get the hidden pointer, trivial 8-byte ones go in EAX:EDX") would
have predicted a difference. Both used the hidden buffer:

```
mov  ecx, DWORD PTR _p$[esp-4]   ; this -> ECX
lea  edx, DWORD PTR $T1[esp]     ; address of the caller's 8-byte temporary
sub  esp, 8                      ; reserve it
mov  eax, DWORD PTR [ecx]        ; vtable
push edx                         ; HIDDEN return-buffer pointer
call DWORD PTR [eax]
mov  edx, eax                    ; callee handed the buffer pointer back in EAX
mov  eax, DWORD PTR [edx]        ; low dword
mov  edx, DWORD PTR [edx+4]      ; high dword
add  esp, 8
ret  0                           ; callee popped the hidden pointer
```

**(C), the `uint64` version, was a completely different ABI** - no buffer at all, result
straight back in EAX:EDX, and MSVC even tail-called it:

```
mov  ecx, DWORD PTR _p$[esp-4]
mov  eax, DWORD PTR [ecx]
mov  eax, DWORD PTR [eax]
jmp  eax
```

**Consequence, and it is an asymmetry worth stating plainly: for CSteamID *arguments*,
declaring `uint64` is byte-identical and safe, which is what the whole header does. For
CSteamID *return values* it is not safe and would corrupt the call.** Those are two
different conventions, and the same shortcut works for one and breaks the other.

Because variant (B) is exactly our `SteamId` type - a trivial `pack(1)` POD holding a
`uint64` - declaring these methods as returning `SteamId` is correct, and MSVC sets up
the hidden buffer automatically. That is what the header does. If anyone hand-rolls a
thunk instead, the correct spelling is:

```cpp
typedef SteamId *(__thiscall *FnGetSteamID)( void *self, SteamId *pRetBuf );
```

**Where the hidden pointer sits when there are also real arguments: FIRST, ahead of
them.** Verified on `GetFriendByIndex(int, int)`, see below.

## 1. ISteamUser017 - VERIFIED

| Slot | Method | Source |
|---|---|---|
| 0 | `HSteamUser GetHSteamUser()` | [S1] + [S2] |
| **1** | **`bool BLoggedOn()`** | [S1] + [S2] + [S3] |
| **2** | **`CSteamID GetSteamID()`** | [S1] + [S2] + [S3] |

Both methods asked for are in the first three slots, so this was cheap. [S1]
`steamworks_sdk_131/isteamuser.h` with `STEAMUSER_INTERFACE_VERSION "SteamUser017"` at
line 208, and [S2] `ISteamUser017.h` lists the same methods in the same order.

No overloaded virtual names in this interface - I checked for duplicates explicitly
rather than assuming, because that is exactly what caught us out in `CCallbackBase`. So
declaration order is vtable order and no reversal applies.

[S3] end-to-end verification: I compiled a real `GetMySteamId` helper against the
delivered header and read the emitted offsets. `BLoggedOn` -> `[eax+4]`, and
`GetSteamID` -> `[eax+8]` preceded by `lea ecx, _me$[...]` / `push ecx`. Divided by 4
that is slots 1 and 2, with the hidden return buffer visible in the call setup.

**`BLoggedOn` is worth having and is the right instinct.** If it returns false,
`GetSteamID` has nothing meaningful to give, so refusing politely beats handing a player
a garbage number to paste to friends.

**Honest note on the slot count, which matters if anyone extends this.** On Windows this
interface has **24 slots, 0 to 23**, ending at `GetPlayerSteamLevel`. The four methods
that appear after it in the file - `LogOn`, `LogOnAndLinkSteamAccountToPSN`,
`LogOnAndCreateNewSteamAccountIfNeeded`, `GetConsoleSteamID` - are inside a
`#ifdef _PS3` block ([S1] `isteamuser.h` lines 169-204) and are **not** in the Windows
vtable. A naive count of `virtual` lines in the file gives 28 and a wrong picture. This
does not affect slots 1 and 2.

Only slots 0-2 are declared in the header, deliberately: slots 3 and up are
game-connection, voice and auth-ticket machinery we have no use for, and declaring them
would mean getting a dozen more signatures right for no benefit.

## 2. ISteamFriends014 - VERIFIED, the evidence is not thin

This was asked with an explicit invitation to say no. **The answer is yes, and to the
same standard as the networking table.** All three methods are in the first eight slots.

| Slot | Method | Source |
|---|---|---|
| 0 | `const char *GetPersonaName()` | [S1] + [S2] |
| **3** | **`int GetFriendCount( int iFriendFlags )`** | [S1] + [S2] + [S3] |
| **4** | **`CSteamID GetFriendByIndex( int iFriend, int iFriendFlags )`** | [S1] + [S2] + [S3] |
| **7** | **`const char *GetFriendPersonaName( CSteamID steamIDFriend )`** | [S1] + [S2] + [S3] |

[S1] `steamworks_sdk_131/isteamfriends.h` with
`STEAMFRIENDS_INTERFACE_VERSION "SteamFriends014"`, and [S2] `ISteamFriends014.h` gives
an identical first eight slots. No overloaded virtual names, checked explicitly. No
`#ifdef` anywhere inside the class body, so there is no conditional-compilation hazard
of the kind `ISteamUser017` has.

[S3] end-to-end verification from a compiled friend-picker loop:

- `push 4` then `call DWORD PTR [eax+12]` -> `GetFriendCount` at slot **3**
- `lea ecx, _id$1[...]` ; `push 4` ; `push edi` ; `push ecx` ; `call DWORD PTR [eax+16]`
  -> `GetFriendByIndex` at slot **4**
- two `push`es of the 8-byte id then `call DWORD PTR [eax+28]` ->
  `GetFriendPersonaName` at slot **7**

The push order in the middle case settles the hidden-pointer question. Pushes go right
to left, so the sequence `iFriendFlags`, `iFriend`, `retbuf` means the retbuf was pushed
**last**, putting it at the lowest address, which makes it the **first** argument. Stack
layout low to high is `retbuf, iFriend, iFriendFlags`. **The hidden return-buffer
pointer comes before the declared arguments, not after.**

**Two functional caveats, both from [S1]'s own comments, both of which will produce a
confusing picker if ignored:**

1. **String lifetime.** The header says of these `char *` returns: "it's important that
   this pointer is not saved off; it will eventually be free'd or re-allocated". Copy
   the bytes out immediately. Do not store the pointer, do not hold it across another
   Steam call, do not hand it to UI code to keep.
2. **The name may not be known yet.** The header warns that "on first joining a lobby,
   chat room or game server the local user will not known the name of the other users
   automatically; that information will arrive asyncronously". For the user's own
   friends list this is normally fine since Steam already has those names, but handle an
   empty string rather than assuming.

Also: pass the **same** `iFriendFlags` to `GetFriendCount` and `GetFriendByIndex` or the
indices do not correspond. `k_EFriendFlagImmediate = 0x04` is what the header calls a
"regular" friend and is what a picker wants. `GetFriendCount` returns -1 on invalid
flags, so treat a negative result as an error rather than a count.

One design caveat that is not an ABI question: a friend picker only helps if the friend
is **already running the game**, since a SteamID alone does not make them reachable.
`GetFriendPersonaState` (slot 6) or `GetFriendGamePlayed` (slot 8) would let you filter
to friends actually in FFX, but neither is declared in the header and neither is
verified beyond appearing at those indices in both [S1] and [S2]. If you want that
filter, re-verify first.

## Confidence summary for the addendum

| Item | Rating |
|---|---|
| SDK 1.31 pin, now on six concurring version strings | **Verified.** Only tree where all four relevant strings match the DLL. |
| 8-byte class return uses a hidden buffer pointer, not EAX:EDX | **Verified** from real codegen, three variants compared. |
| Constructors make no difference to that at 8 bytes | **Verified** - variants (A) and (B) were byte-identical. |
| `uint64` return uses EAX:EDX and is therefore the wrong declaration | **Verified** from real codegen. |
| Hidden return pointer precedes the declared arguments | **Verified** from push order on `GetFriendByIndex`. |
| `ISteamUser017::BLoggedOn` = slot 1 | **Verified.** [S1] + [S2] + emitted offset. |
| `ISteamUser017::GetSteamID` = slot 2 | **Verified.** [S1] + [S2] + emitted offset. |
| `ISteamUser017` has 24 Windows slots, PS3 tail excluded | **Verified** from the `#ifdef _PS3` in [S1]. |
| `ISteamFriends014::GetFriendCount` = slot 3 | **Verified.** [S1] + [S2] + emitted offset. |
| `ISteamFriends014::GetFriendByIndex` = slot 4 | **Verified.** [S1] + [S2] + emitted offset. |
| `ISteamFriends014::GetFriendPersonaName` = slot 7 | **Verified.** [S1] + [S2] + emitted offset. |
| `EFriendFlags` / `EPersonaState` values | **Verified.** [S1] `isteamfriends.h` lines 65-78. |
| `SteamUser` / `SteamFriends` exports are `extern "C" __cdecl`, no args | **Verified.** [S1] `steam_api.h` plus `S_CALLTYPE __cdecl`. |
| `ISteamFriends014` slots 8+ and `ISteamUser017` slots 3+ | **Not declared, not verified.** Both appear consistently in [S1] and [S2] but are unexercised. Re-verify before use. |
| Whether a listed friend is actually reachable in-game | **Not an ABI question.** A SteamID does not imply the peer is running FFX. |

## Validation performed

`SteamAbi.h` still compiles clean with 32-bit MSVC at `/W4 /WX -permissive-`, warnings
as errors, with all `static_assert`s passing, now including the three interface sizes and
`k_EFriendFlagImmediate`. A realistic `GetMySteamId` plus friend-picker loop was compiled
against it and the emitted vtable byte offsets read back as 4 and 8 for User and 12, 16
and 28 for Friends, matching documented slots 1, 2 and 3, 4, 7.
