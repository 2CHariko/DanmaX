param([Parameter(Mandatory=$true)][string] $OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function Get-IconHash([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = Join-Path $root 'out'
if (-not $output.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Icon output must be below the project out/ directory.'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sourcePath = Join-Path $root 'assets/icon.png'
$icoPath = Join-Path $output 'DanmaX.ico'
$pngPath = Join-Path $output 'DanmaX.png'
$statePath = Join-Path $output 'icon-state.json'
$sourceHash = (Get-IconHash $sourcePath)
$generatorHash = (Get-IconHash $PSCommandPath)
# Check content on every build: copied images can retain older modification times.
# Leave output timestamps unchanged when valid so resource compilation remains incremental.
if ((Test-Path -LiteralPath $statePath) -and (Test-Path -LiteralPath $icoPath) -and (Test-Path -LiteralPath $pngPath)) {
    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        if ($state.source -eq $sourceHash -and $state.generator -eq $generatorHash -and
            $state.ico -eq (Get-IconHash $icoPath) -and
            $state.png -eq (Get-IconHash $pngPath)) {
            return
        }
    } catch { Write-Verbose 'Invalid icon state; regenerating.' }
}
$source = [Drawing.Image]::FromFile($sourcePath)
$frames = [Collections.Generic.List[byte[]]]::new()
$sizes = @(16,20,24,32,40,48,64,128,256)
try {
    foreach ($size in $sizes) {
        # Supersample the alpha mask as well as the source image for smooth small icons.
        $side = $size * 4
        $large = [Drawing.Bitmap]::new($side,$side)
        $graphics = [Drawing.Graphics]::FromImage($large)
        $path = [Drawing.Drawing2D.GraphicsPath]::new()
        $bitmap = [Drawing.Bitmap]::new($size,$size)
        $small = [Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $diameter = [single]($side * 0.4)
            $edge = [single]($side - $diameter)
            $path.AddArc(0,0,$diameter,$diameter,180,90)
            $path.AddArc($edge,0,$diameter,$diameter,270,90)
            $path.AddArc($edge,$edge,$diameter,$diameter,0,90)
            $path.AddArc(0,$edge,$diameter,$diameter,90,90)
            $path.CloseFigure()
            $graphics.SetClip($path)
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            # Preserve aspect ratio; non-square artwork is centered on transparent padding.
            $scale = [Math]::Min($side / $source.Width, $side / $source.Height)
            $width = [single]($source.Width * $scale)
            $height = [single]($source.Height * $scale)
            $graphics.DrawImage($source,[single](($side-$width)/2),[single](($side-$height)/2),$width,$height)
            $small.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $small.DrawImage($large,0,0,$size,$size)
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
            if ($size -eq 256) { $bitmap.Save((Join-Path $output 'DanmaX.png'),[Drawing.Imaging.ImageFormat]::Png) }
        } finally {
            $stream.Dispose(); $small.Dispose(); $bitmap.Dispose()
            $path.Dispose(); $graphics.Dispose(); $large.Dispose()
        }
    }
    $file = [IO.File]::Create((Join-Path $output 'DanmaX.ico'))
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i=0; $i -lt $sizes.Count; $i++) {
            $dimension = [byte]($sizes[$i] % 256)
            $writer.Write($dimension); $writer.Write($dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length); $writer.Write([uint32]$offset)
            $offset += $frames[$i].Length
        }
        foreach ($frame in $frames) { $writer.Write($frame) }
    } finally { $writer.Dispose() }
} finally { $source.Dispose() }
[ordered]@{
    source = $sourceHash
    generator = $generatorHash
    ico = (Get-IconHash $icoPath)
    png = (Get-IconHash $pngPath)
} | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
Write-Host 'Updated DanmaX Windows and Qt icons.'
