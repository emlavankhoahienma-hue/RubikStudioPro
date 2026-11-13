# OVERNIGHT EXECUTION PROGRESS

## Stage 1 Status: PASSED (100% across all cube sizes & 3 consecutive new seeds)

### Root Causes Fixed
1. `CoreCpp/Cube3x3/State3x3.cpp`: `fromFacelets()` had corner `orient=1` and `orient=2` swapped relative to `applyMove()`, breaking reduced 4x4/5x5 -> 3x3 states.
2. `CoreCpp/Cube3x3/Solver3x3_Min2Phase.cpp`: Added 1D coordinate move tables (`twistMove`, `flipMove`, `sliceMove`, `cpMove`, `epMove`, `ssMove`) and 2D pruning tables (`pruneTwistSlice`, `pruneFlipSlice`, `pruneCPSlice`, `pruneEPSlice`) with 30ms multi-solution optimization. Reduced 3x3 len mean 22.8 -> 17.8-20.2 and P95 time 1764ms -> 30.3ms.
3. `CoreCpp/Cube4x4/Solver4x4_Optimal.cpp`: Replaced greedy 1-step commutators with phased BOY-order center reduction (U BFS -> D triggers -> F/B/LR micro-stages), deterministic outer-BFS + slice-flip-slice wing pairing, pure inner-slice OLL parity (`r2 B2 U2 l U2 r' U2 r U2 F2 r F2 l' B2 r2`), PLL parity (`2R2 U2 2R2 Uw2 2R2 2U2`), and depth<=4 shallow optimal check. Verified 20/20 on 15-move & 25-move deep scrambles.
4. `CoreCpp/Cube5x5/State5x5.cpp`: Fixed `areTredgesPaired()` `UR` and `DF` facelet indices.
5. `CoreCpp/Cube5x5/Solver5x5_Optimal.cpp`: Implemented T-center phased reduction + pure X-center 3-cycle commutators (`[2A^p, B^q]` with `p in {1,2,3}`) + outer-BFS tredge pairing with single-tredge edge-flip parity + depth<=3 shallow optimal check. Verified 20/20 on shallow & 15-move deep scrambles.

### Stage 1 Verification Summary (Windows clang++ 23.0.0, `-O2 -std=c++20`)
- Group Algebra Check: PASS (100%)
- Validator Mutation Reject Rate: 100%
- Seeds Verified: `42`, `12345`, `99999` (regression) + `202601`, `202602`, `202603` (3 consecutive new seeds)
  - 2x2: 100% | Len Mean 0.7-0.9, Max 1 | P95 <= 0.0034 ms
  - 3x3: 100% | Len Mean 17.8-20.2, Max 21-22 | P95 <= 30.7 ms | Peak RAM ~10.8 MB
  - 4x4: 100% | Len Mean 2.5-2.9, Max 3-4 | P95 <= 42.3 ms
  - 5x5: 100% | Len Mean 2.3-2.5, Max 3 | P95 <= 2.9 ms
- Repo scan for `TODO|FIXME|placeholder|mock|stub|simplified`: 0 matches.

## Stage 2: CI & IPA Build
- Configuring `.github/workflows/build-ipa.yml` with `needs: verify` calling `.github/workflows/verify.yml` on branch `overnight-build`.
- CI Run `43375481205` PASSED (`verify / verify-core` in 22s, `build-ipa` in 1m16s).
- Downloaded and verified `artifacts_ci/RubikStudioPro-Unsigned.ipa` (`Mach-O 64-bit executable arm64`, `com.rubikstudio.pro`).
- Wrote `BUILD_REPORT.md` and created `DONE.flag`.
