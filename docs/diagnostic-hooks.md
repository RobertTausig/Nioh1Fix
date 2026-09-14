# Diagnostic Hook Guide

This guide is for agents investigating new Nioh timing paths. The reusable
callback trampoline is implemented in `src/diagnostic_hooks.cpp` and declared
in `src/diagnostic_hooks.hpp`. It is compiled into the plugin but installs
nothing unless an investigation explicitly calls `InstallCallbackDiagnostic`.

The facility is intentionally source-driven. Adding a diagnostic requires a
DLL rebuild; do not add runtime-configurable byte patterns or bare-RVA hooks.

## Non-Negotiable Rules

1. Read `AGENTS.md` and, for GitHub issues, `docs/issue-investigation.md`.
2. Never use an RVA as a runtime patch target or compatibility fallback.
3. Add a unique relocation-aware full signature for every new hook.
4. Resolve and validate every diagnostic in the complete compatibility plan
   before the first write. A missing or ambiguous signature must fail closed.
5. Hook only complete instructions. The copied overwrite block must contain no
   RIP-relative operands, relative calls, jumps, or branches because the
   generic trampoline replays its bytes without relocating them.
6. Diagnostics must observe state, not alter game behavior.
7. Do not log from a hot callback. Record bounded atomic state and report it
   from the existing periodic diagnostics.
8. Do not launch Nioh. Install only after confirming that no Nioh process is
   running, then ask the user to perform the runtime test.

## API Contract

`InstallCallbackDiagnostic` takes the resolved executable block, a `HookSpec`,
a `HookState`, shared `HookResources`, and `CallbackDiagnosticOptions`.

The generated trampoline:

1. Replays the overwritten instructions.
2. Aligns the stack and reserves Windows x64 callback shadow space.
3. Preserves `RAX`, `RCX`, `RDX`, `R8` through `R11`, and `RFLAGS`.
4. Optionally preserves up to four registers selected from XMM0 through XMM5.
5. Calls the capture callback with the register state produced by the replayed
   block.
6. Restores state, optionally increments the selected counter, and continues
   after the overwritten block.

Windows x64 callbacks must use a signature matching the live argument
positions. For example, `void Capture(void*, float) noexcept` reads its first
argument from `RCX` and its second from `XMM1`. List XMM1 in `preservedXmm` if
the original code needs that value after the callback. XMM6 through XMM15 and
other nonvolatile registers remain protected by the Windows x64 ABI.

`stackAlignedAfterBlock` describes `RSP` after the copied instructions run:

- Use `true` when `RSP % 16 == 0`.
- Use `false` when `RSP % 16 == 8`.

Determine this from disassembly; never guess. A wrong value can crash inside
the callback. The callback must be `noexcept` in practice because the generated
stub has no unwind metadata.

`counterIndex = -1` disables the automatic counter. Otherwise the counter is a
`LONG` slot in `HookResources::data` and is read with `Counter(index)`.

## Resource Allocation

`EnsureHookResources` allocates one 4096-byte near-code page and one 4096-byte
data page. Before choosing offsets, audit all current users in
`src/signatures.hpp`, `src/hooks.cpp`, and `src/projectile_posture.cpp`.

At the time of writing:

- Code below offset 908 is reserved by production relays and timing stubs.
- A callback diagnostic may require up to 384 bytes of code.
- Offset 1024 is the first suitable diagnostic slot; additional slots must be
  at least 384 bytes apart and remain within the 4096-byte page.
- Data counters 0 through 8 are assigned to production diagnostics. Audit the
  current code before using index 9 or higher.

Never overlap a stub, relay, or counter. These values are implementation
details, not permanent ABI guarantees.

## Adding a Diagnostic

### 1. Define the signature and hook specification

Add the full signature near related patterns in `src/signatures.hpp`. Wildcard
only audited address displacements.

```cpp
inline constexpr auto kCandidateUpdate = Pattern(
    "40534883EC20488BD94885C9740BE8????????4883C4205BC3");

inline constexpr HookSpec kCandidateDiagnostic{
    kCandidateUpdate,
    0,      // blockOffset from the unique match
    6,      // complete overwrite size, at least 5 bytes
    1024,   // non-overlapping near-code stub offset
    9,      // unique counter index, or -1
    {},
    0,
    "candidate diagnostic",
    "Enabled candidate diagnostics."
};
```

Verify uniqueness against the validated executable. The runtime must repeat
that search; an offline uniqueness check is not a compatibility substitute.

### 2. Extend the complete compatibility plan

Add a resolved block pointer to `CompatibilityPlan`. In
`ResolveCompatibility`:

- Call `FindCode(image, kCandidateUpdate)` with the other searches.
- Return `incompatible` if `count > 1`.
- Return `pending` if `count == 0` while SteamStub may still be decrypting.
- Derive `match.address + blockOffset` only after a unique match.
- Validate the complete block with `IsImageRange(...,
  IMAGE_SCN_MEM_EXECUTE)`.
- Validate every decoded relative target or object layout used by the capture.

Do not install any core or optional patch until this expanded plan is complete.

### 3. Write a bounded capture callback

Place investigation-specific state and capture code in focused temporary
files. Callbacks can run frequently and on worker threads.

```cpp
void CaptureCandidate(void* object, float delta) noexcept {
    if (!object || !std::isfinite(delta)) return;
    InterlockedExchange(&g.candidateDeltaBits, std::bit_cast<LONG>(delta));
}
```

Add the bounded capture fields, such as `candidateDeltaBits`, to `State`. The
automatic call count is available through `Counter(9)` in this example. Use
`Interlocked*` operations or a lock for shared state. Before dereferencing heap
pointers, validate that the complete range is committed, readable, and not
guarded. Keep arrays bounded and avoid allocation, disk I/O, or expensive work
in the callback.

### 4. Install and maintain the hook

Include `diagnostic_hooks.hpp` in the focused installer and configure the
register preservation explicitly:

```cpp
CallbackDiagnosticOptions options{};
options.callback = reinterpret_cast<const void*>(&CaptureCandidate);
options.preservedXmm = {1, 0, 0, 0};
options.preservedXmmCount = 1;
options.stackAlignedAfterBlock = true;
options.failure = "Could not install the candidate diagnostic.";
options.success = "Enabled the candidate diagnostic.";

state.status = InstallCallbackDiagnostic(
    image, kCandidateDiagnostic, plan.candidateBlock,
    state, patches.resources, options);
```

Store a `HookState` in `PatchSet`. Install only while its status is `pending`.
On later monitor iterations, require `IsApplied(state.patch)`; stop monitoring
if the block changes unexpectedly.

### 5. Report useful aggregates

Add compact cumulative values to `LogDiagnostics`. Compare counter deltas over
known time and framerate intervals. Distinguish absence (`0` calls), call rate,
object identity, input values, and behavior observed by the user. Do not infer
that a busy shared function is the target path without object or call-site
correlation.

## Runtime Validation Sequence

1. Cross-build the Windows plugin and run `git diff --check`. Follow any
   current user restriction on tests.
2. Confirm no Nioh process is running before installation.
3. Install the package without changing `nioh.exe` or MangoHud configuration.
4. Tell the user whether the build is diagnostic-only or behavior-changing.
5. Give a precise cap/order/scenario and state whether hits are required.
6. After the user exits, inspect `Nioh1Fix.log` and compare cumulative deltas
   between intervals rather than only final totals.
7. Record decisive findings in `docs/research.md` without referring to local or
   untracked evidence files.

## Cleanup After Investigation

Once a path is accepted or rejected:

- Remove its signature from the required compatibility plan unless production
  behavior still depends on it.
- Remove its installer, `HookState`, capture callback, temporary state, and log
  fields.
- Keep only the concise finding, relevant RVAs, and validation result in
  `docs/research.md`.
- Leave `diagnostic_hooks.cpp` and `diagnostic_hooks.hpp` intact and inactive
  for the next investigation.

This keeps release builds free of investigative overhead without losing the
safe trampoline machinery.
