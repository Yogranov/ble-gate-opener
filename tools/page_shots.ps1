# Takes phone-sized screenshots of the page against tools/mock_api.py.
param([string]$OutDir = "$env:TEMP\yg_shots", [int]$Port = 8765)

New-Item -ItemType Directory -Force $OutDir | Out-Null
$root = Split-Path $PSScriptRoot -Parent
$server = Start-Process python -ArgumentList "`"$root\tools\mock_api.py`"", $Port -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
$shots = @(
    @{ name = "status"; query = "tab=status&h=760" },
    @{ name = "beacon"; query = "tab=beacon&h=800" },
    @{ name = "settings"; query = "tab=settings&h=2300" },
    @{ name = "logs"; query = "tab=logs&h=1000&click=.trip" },
    @{ name = "system"; query = "tab=system&h=800" }
)
try {
    foreach ($shot in $shots) {
        $file = Join-Path $OutDir "$($shot.name).png"
        $h = [int]($shot.query -replace '.*h=(\d+).*', '$1')
        & $edge --headless=new --disable-gpu --hide-scrollbars --user-data-dir="$OutDir\profile" `
            --blink-settings=preferredColorScheme=1 --window-size=500,$h --virtual-time-budget=15000 `
            --screenshot="$file" "http://localhost:$Port/phone?$($shot.query)" 2>$null | Out-Null
        Start-Sleep -Milliseconds 800
        "$file $((Get-Item $file -ErrorAction SilentlyContinue).Length) bytes"
    }
} finally {
    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}
