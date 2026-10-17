#requires -Version 7.0
param([switch] $Offline)
# Keep the original probe entry point working; both use the shared lock/cache.
& "$PSScriptRoot/prepare-libvlc-probe.ps1" -Offline:$Offline
