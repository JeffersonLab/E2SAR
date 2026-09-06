# Implementation Plan: Issue #166 — New EJFAT URI Format

## Context

The udplbd2 control plane now returns URIs with two structural changes:
1. **Multiple `sync=` params** — one IPv4 and one IPv6 address, each with its own port
2. **Data port ranges** — `data=<addr>:<minPort>-<maxPort>` instead of a single port

The current `EjfatURI` class stores a single sync address and a single data port, so it silently drops the second sync entry and cannot represent port ranges. The protobuf response already provides `syncIpv6Address`, `dataMinPort`, and `dataMaxPort` fields — E2SAR just ignores them.

### New URI format

```
ejfat[s]://<token>@<host>:<port>/lb/<id>?sync=<v4>:<port>&sync=[<v6>]:<port>&data=<v4>:<min>-<max>&data=[<v6>]:<min>-<max>
```

### Data port parsing rules (user-confirmed)

| Input | Stored as |
|---|---|
| `data=10.10.10.1:1234-5678` | minPort=1234, maxPort=5678 |
| `data=10.10.10.1:1234` | minPort=1234, maxPort=1234 |
| `data=10.10.10.1` (no port) | minPort=16384, maxPort=32767 |

---

## Stage 1: Core EjfatURI — Data Model, Parser, Serializer

**Files**: `include/e2sarUtil.hpp`, `src/e2sarUtil.cpp`, `test/e2sar_uri_test.cpp`

This is the foundation — everything else depends on it.

### 1A. New constants (`e2sarUtil.hpp`, near existing `DATAPLANE_PORT`)

```cpp
const u_int16_t DATAPLANE_PORT_MIN = 16384;
const u_int16_t DATAPLANE_PORT_MAX = 32767;
```

Keep `DATAPLANE_PORT = 19522` — it's still used in `py_e2sar.cpp` as `_dp_port` and may be referenced externally.

### 1B. New utility: `string_tuple_to_ip_and_port_range()` (`e2sarUtil.hpp`, after existing `string_tuple_to_ip_and_port`)

Signature: `result<std::tuple<ip::address, u_int16_t, u_int16_t>>`

Logic:
- Reuse the same IP extraction as `string_tuple_to_ip_and_port` (find last `]:` or `:`)
- No port portion → return (addr, 0, 0) — caller applies default range
- Port portion contains `-` → split, parse both via `string_to_port`, return (addr, min, max); validate min <= max
- Port portion is a number → return (addr, port, port)

The existing `string_tuple_to_ip_and_port` stays untouched (sync params still use it).

### 1C. Data model changes (`e2sarUtil.hpp`, private members)

| Remove | Add |
|---|---|
| `bool haveSync` | `bool haveSyncv4; bool haveSyncv6;` |
| `u_int16_t syncPort` | `u_int16_t syncPortv4; u_int16_t syncPortv6;` |
| `ip::address syncAddr` | `ip::address syncAddrv4; ip::address syncAddrv6;` |
| `u_int16_t dataPort` | `u_int16_t dataMinPort; u_int16_t dataMaxPort;` |

### 1D. Parser changes (`e2sarUtil.cpp`, constructor lines ~278-318)

**Sync params**: Keep using `string_tuple_to_ip_and_port()`. Dispatch by address family:
- v4 → set `haveSyncv4`, `syncAddrv4`, `syncPortv4`
- v6 → set `haveSyncv6`, `syncAddrv6`, `syncPortv6`

**Data params**: Switch to `string_tuple_to_ip_and_port_range()`. Address dispatch stays the same (v4/v6). Port range logic:
- If (0, 0): `dataMinPort = DATAPLANE_PORT_MIN; dataMaxPort = DATAPLANE_PORT_MAX;`
- Otherwise: `dataMinPort = min; dataMaxPort = max;`
- Shared between v4/v6 (last entry parsed wins — same as current `dataPort` behavior)

### 1E. Getter changes (`e2sarUtil.hpp`)

**Backward-compatible** (keep signatures, change internals):
- `get_syncAddr()` → use `preferV6` to pick v4 or v6 (fallback to whichever is available). This is for display/utility callers (lbadm, lbmonitor). The Segmenter should NOT use this — it uses the explicit `get_syncAddrv4/v6()` getters with its own `syncV6` flag (see Stage 3).
- `has_syncAddr()` → returns `haveSyncv4 || haveSyncv6`
- `has_dataAddr()`, `has_dataAddrv4()`, `has_dataAddrv6()` → unchanged

**Updated getters** (signature change — encodes full range):
- `get_dataAddrv4()` → `result<pair<ip::address, pair<u_int16_t, u_int16_t>>>` where inner pair is `(minPort, maxPort)`. Callers access `.value().first` for address, `.value().second.first` for minPort, `.value().second.second` for maxPort.
- `get_dataAddrv6()` → same
- `get_dataPortRange()` → `result<pair<u_int16_t, u_int16_t>>` (minPort, maxPort), guarded by `has_dataAddr()`. Kept as a convenience getter for callers that only need the range (e.g. Segmenter port distribution in Stage 3).

**New getters**:
- `get_syncAddrv4()` → `result<pair<address, port>>`, guarded by `haveSyncv4`
- `get_syncAddrv6()` → `result<pair<address, port>>`, guarded by `haveSyncv6`
- `has_syncAddrv4()`, `has_syncAddrv6()` → bool

### 1F. Setter changes (`e2sarUtil.hpp`)

- `set_syncAddr(pair<address, port>)` → dispatch by address family, setting the appropriate v4/v6 fields
- `set_dataAddr(pair<ip::address, pair<u_int16_t, u_int16_t>>)` → **signature change**: inner pair is `(minPort, maxPort)`. Dispatches address to v4/v6; stores port range in `dataMinPort`/`dataMaxPort`. Replaces the old `pair<address, u_int16_t>` form where the port was ignored.
- `set_dataPortRange(u_int16_t min, u_int16_t max)` → kept as an auxiliary setter for updating just the range when the address is already set (e.g. default-range fallback in CP code).

**Caller impact** (files needing updates when 1E/1F are implemented):
- `e2sarCP.cpp` (Stage 2): `set_dataAddr(a)` calls pass `pair<address, u_int16_t>` → change to `pair<address, pair<u_int16_t, u_int16_t>>`
- `e2sarDPSegmenter.cpp` (Stage 3): `get_dataAddrv4/v6().value().second` accesses at lines ~556,558,648,650 are replaced wholesale by the new port distribution formula (those `.second` accesses go away entirely)
- `test/e2sar_uri_test.cpp` (1I): `.value().second` checks for a single port → `.value().second.first` (minPort) or `.value().second` for the full pair
- `test/e2sar_seg_test.cpp` (display-only `<< .second` at lines 49,134,210,293): update to `.second.first` for minPort display
- `src/pybind/py_e2sarUtil.cpp` (Stage 4): binding for `get_data_addr_v4/v6` result type changes

### 1G. Serializer changes (`e2sarUtil.cpp`, `operator string()` and `to_string()`)

Refactor the query-param portion of the URI string construction:

- Sync: emit `sync=<v4>:<port>` if `haveSyncv4`, `&sync=[<v6>]:<port>` if `haveSyncv6`
- Data: always include port info. If `dataMinPort == dataMaxPort`, emit `data=<addr>:<port>`. If different, emit `data=<addr>:<min>-<max>`. Emit for v4 and v6 separately if both present.
- SessionId: unchanged

### 1H. Equality operator (`e2sarUtil.cpp`, `operator==`)

Replace `syncAddr`/`syncPort` comparisons with v4/v6 variants. Replace `dataPort` with `dataMinPort`/`dataMaxPort`.

### 1I. C++ Tests (`test/e2sar_uri_test.cpp`)

**Update existing tests** where default port expectations change:
- `URITest2` (uri_string1, `data=192.188.29.20` no port): expected port changes from 19522 to 16384
- `URITest2_3` (uri_string5, `data=192.188.29.20`): same
- `URITest14` (uri_string12, dual-stack): verify port range behavior

**New test cases**:
1. Dual sync: URI with `sync=<v4>:<port>&sync=[<v6>]:<port>` — verify both `get_syncAddrv4/v6()` work, `get_syncAddr()` respects preferV6
2. Data port range: `data=1.2.3.4:1234-5678` — verify `get_dataPortRange()` returns (1234, 5678), `get_dataAddrv4().value().second == make_pair(1234, 5678)`
3. Data single port: `data=1.2.3.4:1234` — verify `get_dataAddrv4().value().second == make_pair(1234, 1234)`
4. Data default range: `data=1.2.3.4` — verify `get_dataAddrv4().value().second == make_pair(DATAPLANE_PORT_MIN, DATAPLANE_PORT_MAX)`
5. Full new-format URI: dual sync + dual data with port ranges
6. Round-trip: parse → to_string → parse → operator== must hold
7. Backward compat: all existing uri_string1–12 parse without exceptions

**Existing test updates for new return type**:
- `URITest2`, `URITest2_3`: `get_dataAddrv4().value().second == DATAPLANE_PORT_MIN` → `.value().second == make_pair(DATAPLANE_PORT_MIN, DATAPLANE_PORT_MAX)`
- `URITest13`: `.value().second == 19020` → `.value().second == make_pair(19020, 19020)`
- `URITest14`: `.value().second == 10000` (v4 and v6) → `.value().second == make_pair(10000, 10000)`

### Verification

```bash
meson test -C build --suite unit --timeout 0
```

All existing URI tests pass (with updated port expectations), new tests pass.

---

## Stage 2: Control Plane (`LBManager`)

**Files**: `src/e2sarCP.cpp`

### 2A. `reserveLB()` (lines ~114-152)

After the existing `syncipv4address` block, add sync IPv6:
```cpp
if (!rep.syncipv6address().empty())
{
    u_int16_t short_port = rep.syncudpport();
    auto o = string_to_ip(rep.syncipv6address());
    if (!o.has_error()) {
        std::pair<ip::address, u_int16_t> a(o.value(), short_port);
        _cpuri.set_syncAddr(a);  // dispatches to v6 fields now
    }
}
```

Replace the data address block — determine the port range first (with default fallback), then pass it atomically with the address via the new `set_dataAddr` signature:
```cpp
u_int16_t minPort = static_cast<u_int16_t>(rep.dataminport());
u_int16_t maxPort = static_cast<u_int16_t>(rep.datamaxport());
if (minPort == 0 || maxPort == 0) {
    minPort = DATAPLANE_PORT_MIN;
    maxPort = DATAPLANE_PORT_MAX;
}
auto portRange = std::make_pair(minPort, maxPort);

if (!rep.dataipv4address().empty()) {
    auto o = string_to_ip(rep.dataipv4address());
    if (!o.has_error())
        _cpuri.set_dataAddr({o.value(), portRange});
}
if (!rep.dataipv6address().empty()) {
    auto o = string_to_ip(rep.dataipv6address());
    if (!o.has_error())
        _cpuri.set_dataAddr({o.value(), portRange});
}
```

Note: `set_dataPortRange` is no longer called separately in CP code since the new `set_dataAddr` takes address+range together.

### 2B. `getLB()` (lines ~280-317)

Identical changes as `reserveLB()`. Same response type (`ReserveLoadBalancerReply`).

### Verification

CP tests are integration tests requiring a live UDPLBd. The URI-level changes are covered by Stage 1 tests. Integration: `meson test -C build --suite cp --timeout 0` when a server is available.

---

## Stage 3: Segmenter — Sync Address Family Selection + Port Range

**Files**: `include/e2sarDPSegmenter.hpp`, `src/e2sarDPSegmenter.cpp`, `bin/e2sar_perf.cpp`, `bin/e2sar_ft.cpp`

### 3A. New `syncV6` flag in `SegmenterFlags` (`include/e2sarDPSegmenter.hpp`)

Add an optional sync address family selector:
```cpp
std::optional<bool> syncV6;  // unset = match dpV6 (default); true = force IPv6 sync; false = force IPv4 sync
```

Default in `SegmenterFlags()` initializer: leave unset (`std::nullopt`).

### 3B. `_openSyncSockets()` (`e2sarDPSegmenter.cpp` ~lines 284-338)

Replace the current `get_syncAddr()` call with explicit v4/v6 selection:

```cpp
// Determine sync address family: explicit override, or match data plane family
bool useSyncV6 = seg.syncV6.value_or(sendThreadState.useV6);
auto syncAddr = useSyncV6 ? seg.dpuri.get_syncAddrv6() : seg.dpuri.get_syncAddrv4();
if (syncAddr.has_error())
    return syncAddr.error();
```

The rest of the function (socket creation, connect) stays the same — it already branches on `syncAddr.value().first.is_v6()`.

The Segmenter constructor needs to store the resolved `syncV6` value from `sflags.syncV6` so `SyncThreadState` can access it. Add a `const std::optional<bool> syncV6` member to the `Segmenter` class, initialized from `sflags.syncV6`.

### 3C. CLI options (`bin/e2sar_perf.cpp`, `bin/e2sar_ft.cpp`)

Add `--sync-ipv4` and `--sync-ipv6` options (mutually exclusive) that set `sflags.syncV6`:
- `--sync-ipv4` → `sflags.syncV6 = false`
- `--sync-ipv6` → `sflags.syncV6 = true`
- Neither → `sflags.syncV6` stays unset (defaults to matching `dpV6`)

These only apply in sender mode (where a Segmenter is created).

### 3D. `_openDataSockets()` — Automatic port distribution replaces `multiPort`

The `multiPort` flag was originally needed because the LB only accepted on port 19522 — there was no range to spread across. With port ranges, the Segmenter should automatically distribute destination ports across the available range for LAG entropy. The `multiPort` flag is **deprecated**.

**New destination port logic** (replaces the `multiPort` ternary):

```cpp
auto portRange = seg.dpuri.get_dataPortRange();
u_int16_t minP = portRange.value().first;
u_int16_t maxP = portRange.value().second;
u_int32_t rangeSize = maxP - minP + 1;
u_int32_t stride = std::max(1u, rangeSize / (u_int32_t)seg.numSendSockets);
// ...
// In the socket loop, for each socket fdCount:
u_int16_t dstPort = minP + (u_int16_t)((fdCount * stride) % rangeSize);
```

This replaces both the `multiPort=true` and `multiPort=false` paths with a single formula. Behavior by case:

| URI | Range | Sockets | Stride | Dest ports | Notes |
|---|---|---|---|---|---|
| `data=1.2.3.4:16384-32767` | 16384 | 4 | 4096 | 16384, 20480, 24576, 28672 | Real LB: max LAG spread |
| `data=127.0.0.1:10000-10003` | 4 | 4 | 1 | 10000, 10001, 10002, 10003 | B2B: consecutive, matches Reassembler |
| `data=1.2.3.4:1234` | 1 | 4 | 1 | 1234, 1234, 1234, 1234 | Single port: all same (backward compat) |
| `data=1.2.3.4` (default) | 16384 | 4 | 4096 | 16384, 20480, 24576, 28672 | Default range: good spread |

**B2B testing note**: Instead of `--multiport`, users craft a tight range URI matching the Reassembler's port count, e.g., `data=127.0.0.1:10000-10003` for 4 recv threads. The Reassembler opens consecutive ports from `starting_port`, so a tight range produces the matching consecutive destination ports.

### 3E. Remove `multiPort` flag

- Remove `multiPort` from `SegmenterFlags` and from the `Segmenter` class
- Remove `--multiport` CLI option from `e2sar_perf.cpp` and `e2sar_ft.cpp`
- Remove any `multiPort` references in pybind bindings (`py_e2sarDP.cpp`) if exposed
- Port distribution is now always automatic from the URI's port range

### 3F. `test/e2sar_seg_test.cpp` — display-only port accesses

Lines ~49, 134, 210, 293 print `uri.get_dataAddrv4().value().second` for diagnostic output. With the new return type these are `pair<u_int16_t, u_int16_t>` not `u_int16_t`. Change each to `.value().second.first` (print minPort) or format as `min-max`:
```cpp
// before:
uri.get_dataAddrv4().value().second
// after:
uri.get_dataAddrv4().value().second.first  // minPort only, for display
```

### Verification

```bash
meson test -C build --suite unit --timeout 0  # covers seg unit tests
```

---

## Stage 4: Python Bindings

**Files**: `src/pybind/py_e2sarUtil.cpp`

### 4A. Updated `get_data_addr_v4/v6` bindings

`get_dataAddrv4/v6()` now return `result<pair<ip::address, pair<u_int16_t, u_int16_t>>>`. Pybind11 converts nested `std::pair` to a nested Python tuple automatically, so in Python callers get `(ip_address, (min_port, max_port))`. However, the existing binding line:

```cpp
ejfat_uri.def("get_data_addr_v4", &EjfatURI::get_dataAddrv4);
```

...will fail to compile unless the result type `E2SARResult<pair<address, pair<u_int16_t, u_int16_t>>>` is registered. Check `py_e2sar.cpp` for the `py::class_` registrations of `E2SARResult` variants and add one for the new nested type, **or** wrap with a lambda that unpacks to a flat 3-tuple for a cleaner Python API:

```cpp
ejfat_uri.def("get_data_addr_v4", [](const EjfatURI &u) {
    auto r = u.get_dataAddrv4();
    if (r.has_error()) return /* error result */;
    return std::make_tuple(r.value().first.to_string(),
                           r.value().second.first,   // minPort
                           r.value().second.second);  // maxPort
});
ejfat_uri.def("get_data_addr_v6", [](const EjfatURI &u) { /* same */ });
```

Decide between nested-tuple and flat-3-tuple based on what's cleaner for Python callers and consistent with the existing result-type pattern in `py_e2sar.cpp`. Whichever is chosen, document it so Stage 6 tests use the same access pattern.

### 4B. Updated `set_data_addr` binding

`set_dataAddr` now takes `pair<ip::address, pair<u_int16_t, u_int16_t>>`. Wrap with a lambda that accepts `(address_str, min_port, max_port)` from Python:

```cpp
ejfat_uri.def("set_data_addr", [](EjfatURI &u, const std::string &addr,
                                   u_int16_t minP, u_int16_t maxP) {
    auto r = string_to_ip(addr);
    if (!r.has_error())
        u.set_dataAddr({r.value(), {minP, maxP}});
}, py::arg("addr"), py::arg("min_port"), py::arg("max_port"));
```

This is a **breaking change** for existing Python callers of `set_data_addr(addr, port)` — update `test/py_test/test_ejfatURI.py` and any other Python callers accordingly (Stage 6).

### 4C. New bindings (add after updated bindings above)

```cpp
// Sync v4/v6 getters
ejfat_uri.def("get_sync_addr_v4", &EjfatURI::get_syncAddrv4);
ejfat_uri.def("get_sync_addr_v6", &EjfatURI::get_syncAddrv6);
ejfat_uri.def("has_sync_addr_v4", &EjfatURI::has_syncAddrv4);
ejfat_uri.def("has_sync_addr_v6", &EjfatURI::has_syncAddrv6);

// Data port range (convenience — same info as get_data_addr_v4/v6 but without the address)
ejfat_uri.def("get_data_port_range", &EjfatURI::get_dataPortRange);
ejfat_uri.def("set_data_port_range", &EjfatURI::set_dataPortRange,
    py::arg("min_port"), py::arg("max_port"));
```

Existing bindings (`get_sync_addr`, `has_sync_addr`, `set_sync_addr`) remain unchanged.

Also register `E2SARResultPortRange` in `py_e2sar.cpp` for `result<pair<u_int16_t, u_int16_t>>` if not already present (needed by `get_data_port_range`).

### Verification

```bash
cd test/py_test && pytest -m unit
```

---

## Stage 5: Python Wrapper

**Files**: `src/python/e2sar/get_ip.py`

Note: `get_ip.py` parses the URI **string** directly (not via C++ bindings), so the `get_dataAddrv4/v6` return-type change in Stage 4 does not affect it. The only change needed here is handling the new `min-max` port range syntax in the `data=` query param.

### 5A. Fix port range parsing (lines 29-33)

Replace:
```python
if ":" in ip_port:
    ip_addr, port_str = ip_port.split(":", 1)
    port = int(port_str)
```

With:
```python
if ip_port.startswith("["):
    # IPv6 in brackets: [addr]:port or [addr]:min-max
    bracket_end = ip_port.index("]")
    ip_addr = ip_port[1:bracket_end]
    port_part = ip_port[bracket_end+1:]  # e.g. ":1234" or ":1234-5678" or ""
    if port_part.startswith(":"):
        port_str = port_part[1:]
        port = int(port_str.split("-")[0])  # use min port
    else:
        port = 80
elif ":" in ip_port:
    ip_addr, port_str = ip_port.split(":", 1)
    port = int(port_str.split("-")[0])  # use min port for range
else:
    ip_addr = ip_port
    port = 80
```

The function only uses the port for a UDP test connection to discover the local outgoing address, so using minPort is correct.

### Verification

```bash
cd test/py_test && pytest -m unit
```

---

## Stage 6: Python Tests

**Files**: `test/py_test/test_ejfatURI.py`

### 6A. Updated existing tests

Any existing test that calls `get_data_addr_v4()` or `get_data_addr_v6()` and checks the port element must be updated. If Stage 4 chose the **flat 3-tuple** lambda approach, the return is `(addr_str, min_port, max_port)` — port element is index `[1]` (minPort) or `[2]` (maxPort). If Stage 4 chose the **nested-tuple** approach, it is `(addr, (min_port, max_port))` — port element is `[1][0]`/`[1][1]`.

Likewise, any test calling `set_data_addr(addr, port)` must be updated to `set_data_addr(addr, min_port, max_port)` (flat form) or `set_data_addr(addr, port, port)` for a single-port entry.

Audit `test_ejfatURI.py` for all such usages before writing new tests.

### 6B. New test cases

Add `@pytest.mark.unit` tests:

1. **`test_dual_sync_addresses`**: Parse URI with `sync=<v4>:<port>&sync=[<v6>]:<port>`. Assert `has_sync_addr_v4()`, `has_sync_addr_v6()`, verify addresses and ports via `get_sync_addr_v4/v6()`.

2. **`test_data_port_range`**: Parse `data=1.2.3.4:1234-5678`. Assert `get_data_port_range()` returns `(1234, 5678)`. Also assert `get_data_addr_v4()` port elements equal `(1234, 5678)` (exact form depends on Stage 4 choice).

3. **`test_data_single_port`**: Parse `data=1.2.3.4:1234`. Assert `get_data_port_range()` is `(1234, 1234)`.

4. **`test_data_default_port_range`**: Parse `data=1.2.3.4` (no port). Assert `get_data_port_range()` is `(16384, 32767)`.

5. **`test_uri_roundtrip_new_format`**: Parse new-format URI → `str(uri)` → parse again → assert equality.

6. **`test_full_new_format`**: Parse complete URI with dual sync + dual data + port ranges. Verify all components including port range via both `get_data_addr_v4()` and `get_data_port_range()`.

### Verification

```bash
cd test/py_test && pytest -m unit -v
```

---

## Stage 7: Help Text and Documentation

**Files**: `bin/e2sar_perf.cpp`, `bin/e2sar_ft.cpp`, `bin/lbmonitor.cpp`, `include/e2sarUtil.hpp`

- Update example URI strings in help text to show the new format
- Update the URI format doc comment at top of `e2sarUtil.hpp` (around line 49-54)

---

## Dependency Graph

```
Stage 1 (Core URI + C++ tests)
    ├── Stage 2 (CP)
    ├── Stage 3 (Segmenter)
    ├── Stage 4 (pybind) ──→ Stage 5 (Python wrapper)
    │                    └──→ Stage 6 (Python tests)
    └── Stage 7 (Help text / docs)
```

Stages 2, 3, 4, and 7 can proceed in parallel after Stage 1. Stages 5 and 6 depend on Stage 4.

## End-to-End Verification

After all stages:
```bash
meson compile -C build
meson test -C build --suite unit --timeout 0
cd test/py_test && pytest -m unit -v
```
