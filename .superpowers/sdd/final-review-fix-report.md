# Final Review Fix Report

## Findings Fixed

1. Critical: `adjust_population` actor intent parsing now treats `action adjust_population <region> <species> <amount>` as an amount-bearing verb, routes the numeric token into `ActorAction::amount`, and rejects malformed or extra tokens as value failures.
2. Important: actor-runtime `coast` responses no longer overwrite `actor.lastIntent` or increment `WakeCount()` as if a new embodied intent was accepted; they only clear the per-wake command stream.
3. Important: Anthropic Messages parsing now tolerates whitespace around JSON keys and colons, finds a `content` item whose `type` is `text`, and decodes valid `\uXXXX` escapes in the `text` payload.
4. Important: actor observations now include prior intent context via compact one-line fields so the wake packet carries the current goal and last embodied intent summary.

## Files Changed

- `src/xrEngine/xrSim/xrSimActorIntent.cpp`
- `src/xrEngine/xrSim/xrSimActorRuntime.cpp`
- `src/xrEngine/xrSim/xrSimActors.cpp`
- `src/xrEngine/xrSim/xrSimAgentProvider.cpp`
- `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`

## Tests Added

- `TestActorIntentParserRoutesAdjustPopulationAmountIntoActuator`
- `TestActorIntentParserRejectsMalformedAdjustPopulationAction`
- `TestActorRuntimeCoastPreservesPriorEmbodiedIntent`
- `TestAnthropicTextResponseParsesPrettyPrintedUnicodeTextBlock`
- `TestActorObservationIncludesLastIntentSummary`

## Verification Commands

```bash
python3 -m unittest tools.tests.test_gl_macos_soak -v
cmake --build build -j10 --target xrSimWorldStateTests
./bin/arm64/Release/xrSimWorldStateTests
cmake --build build -j10 --target xr_3da
```

## Verification Output Summary

- `python3 -m unittest tools.tests.test_gl_macos_soak -v`: PASS, 24 tests ran, `OK`
- `cmake --build build -j10 --target xrSimWorldStateTests`: PASS
- `./bin/arm64/Release/xrSimWorldStateTests`: PASS
- `cmake --build build -j10 --target xr_3da`: PASS

Notes:

- The required verification build for `xr_3da` completed successfully but still emitted pre-existing linker warnings about duplicate libraries and Homebrew dylibs built for a newer macOS version.
- During the TDD cycle, the new `xrSimWorldStateTests` regressions were run first in a failing state, then rerun green after the fixes.

## Remaining Concerns / Deferred Scope

- Minor review item intentionally deferred: `src/xrEngine/xrSim/xrSimBridge.cpp` still restores world state only in `ai.restore` and does not restore actor debug runtime state. I left that out of this fix set to keep the branch repair scoped to the Critical and Important findings.
