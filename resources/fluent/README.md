# Fluent System Icons

Source: https://github.com/microsoft/fluentui-system-icons
License: MIT; see LICENSE. Exact upstream commit, source URLs and SHA-256 hashes are in manifest.json.

The application embeds original 20 regular SVGs and derived dark SVGs. Dark assets preserve the official geometry and replace #212121 with #DEDEDE. No PNG icon textures, icon fonts, runtime downloads or build-time conversion are required. Standard controls use public icon.source and official style tinting; decorative FluentIcon images request physical display dimensions and update after DPI changes. Brand artwork and covers retain their original formats.

Verify all vendored and derived assets offline:

```powershell
./scripts/verify-fluent-assets.ps1
./scripts/prepare-fluent-assets.ps1 -Offline
```

The prepare script can restore a missing official source only in its explicit online mode; downloads go to .cache/downloads/fluent/<commit>/ and must pass the manifest hash before copying. Derived assets are hash-checked before writing. Configuration/build never invokes this preparation step.

QtSvg is a locked runtime dependency. Dynamic deployment includes Qt6Svg and qsvg/qsvgicon; the static Qt lock includes qtsvg and the application explicitly imports both SVG plugins. Decorative icons are omitted in high contrast where applicable; standard controls retain style tinting and accessible names. Real high-contrast and mixed-DPI behavior require separate verification.
