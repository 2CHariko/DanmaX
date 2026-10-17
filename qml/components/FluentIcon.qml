import QtQuick

// Rasterize the SVG at the physical display size, including after a DPI change.
Image {
    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
    fillMode: Image.PreserveAspectFit
    Accessible.ignored: true
}
