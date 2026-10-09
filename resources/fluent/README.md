# Fluent System Icons

Source: https://github.com/microsoft/fluentui-system-icons
License: MIT; see LICENSE. Exact upstream commit, source URLs and SHA-256 hashes are in manifest.json.

The checked-in SVGs are original 20 regular icons. PNGs are 60 × 60 (3× logical size), rendered offline with sharp 0.35.5; dark variants invert RGB while preserving alpha. The application embeds the PNGs via Qt resources and uses public icon.source. This avoids adding QtSvg to the locked static Qt SDK. No build or runtime downloads or image conversion are required. sharp is an authoring tool, not a build/runtime dependency.

Decorative Image icons are hidden in high contrast; standard button/delegate icons use the official style's icon tint. Text and accessible names remain present.
