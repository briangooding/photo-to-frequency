# Working on this repository

- Each new version requested is a **new plugin variant** with the next number
  in its name (Photo Freq 2, Photo Freq 3, Photo Freq 4, ...), unless the
  request says otherwise. Add it as the next `PTF_VARIANT` in
  `Source/PluginConfig.h` and `CMakeLists.txt`, building on the previous one;
  older variants stay in the sources unchanged.
- Only test the newest variant locally. No need to build or check the older ones.
- CI (`.github/workflows/build.yml`) builds, tests and ships **only the newest
  variant, Windows only**. Update the `env:` block there (and the HostCheck
  dependency in `CMakeLists.txt`) when adding a version.
- Document each new version in its own section of `README.md`.
