# third_party

Vendored dependencies live here once they are pinned and approved.

Expected first entry:

- `DPF/`

Do not add external code here casually. Record the source, version or commit, and any local patching notes when vendoring dependencies.

## Recorded vendorings

- `NeuralAmpModelerCore/` — the `NAM/` sources of https://github.com/sdatkinson/NeuralAmpModelerCore at commit `0b3d3c9`
  (v0.6.0 + WAV-as-Linear loading), MIT, unmodified, with its `LICENSE`. Used by `plugins/nam`.
- `eigen/` — Eigen 3.4.0 (https://gitlab.com/libeigen/eigen, MPL2): `Eigen/`, `unsupported/Eigen/FFT` and `src/FFT`
  only, plus `COPYING.MPL2`, `COPYING.BSD` and `COPYING.README`. NAM's own submodule pins a later Eigen commit; 3.4.0 builds and
  passes the plugin's tests. Used by `plugins/nam`.
