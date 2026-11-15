param(
    [string]$Server = "http://127.0.0.1:8090",
    [string]$CameraId = "room-1",
    [string]$Token = "change-me",
    [string]$JpegPath = ""
)

if ([string]::IsNullOrWhiteSpace($JpegPath)) {
    $JpegPath = Join-Path $PSScriptRoot "sample.jpg"
}

if (!(Test-Path $JpegPath)) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 640, 360
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::FromArgb(32, 96, 160))
    $font = New-Object System.Drawing.Font "Arial", 28
    $brush = [System.Drawing.Brushes]::White
    $graphics.DrawString(("ESP32 Test Frame " + (Get-Date -Format "HH:mm:ss")), $font, $brush, 42, 150)
    $bitmap.Save($JpegPath, [System.Drawing.Imaging.ImageFormat]::Jpeg)
    $graphics.Dispose()
    $bitmap.Dispose()
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $JpegPath))
$headers = @{ "X-Stream-Token" = $Token }
Invoke-RestMethod -Method Post -Uri "$Server/api/cameras/$CameraId/frame" -Headers $headers -ContentType "image/jpeg" -Body $bytes
