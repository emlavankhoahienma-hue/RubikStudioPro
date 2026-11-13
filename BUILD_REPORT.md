# BUILD REPORT — RubikStudioPro (Unsigned IPA)

- **Branch**: `overnight-build`
- **Commit**: `93fe8ca`
- **GitHub Actions Run**: `43375481205` (`Build Unsigned IPA (Overnight Gate)`)
- **Workflow**: `.github/workflows/build-ipa.yml` (`build-ipa` with `needs: verify` calling `.github/workflows/verify.yml`)

## Stage 1 Gate (`verify / verify-core` on `macos-latest`)
- **Group Algebra Check**: PASS (100%)
- **Cube 2x2**: Success 100% | Len Mean/P95/Max `0.7 / 1 / 1` | Time P50/P95 `0.000292 / 0.012 ms`
- **Cube 3x3**: Success 100% | Len Mean/P95/Max `19.4 / 21 / 21` | Time P50/P95 `7.985 / 30.400 ms`
- **Cube 4x4**: Success 100% | Len Mean/P95/Max `2.5 / 3 / 3` | Time P50/P95 `0.0448 / 0.4686 ms`
- **Cube 5x5**: Success 100% | Len Mean/P95/Max `2.3 / 3 / 3` | Time P50/P95 `0.0317 / 1.0847 ms`

## Stage 2 Artifact (`RubikStudioPro-Unsigned.ipa`)
- **Downloaded Path**: `artifacts_ci/RubikStudioPro-Unsigned.ipa` (`331,055` bytes)
- **Executable**: `Payload/RubikStudioPro.app/RubikStudioPro` (`1,385,056` bytes, `Mach-O 64-bit executable arm64`)
- **Bundle Metadata**: `Payload/RubikStudioPro.app/Info.plist` (`757` bytes, `CFBundleIdentifier = com.rubikstudio.pro`, `CFBundleName = RubikStudioPro`)
- **Sideload**: Ready for Sideloadly / TrollStore.
