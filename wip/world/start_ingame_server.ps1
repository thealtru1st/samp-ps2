# Private server for wip/world/ingame-tests.md. Copies only what it needs into a temp
# folder, compiles ingame.pwn, starts it, and records the PID.
#   powershell -File wip\world\start_ingame_server.ps1 [-Bind 127.0.0.1] [-Port 7881]
#   powershell -File wip\world\start_ingame_server.ps1 -Stop
param([string]$Bind = '127.0.0.1', [int]$Port = 7881, [switch]$Stop)

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$src = Join-Path $root 'tools\ompsrv-run\Server'
$dst = Join-Path $env:TEMP 'samp-world-ingame'
$pidFile = Join-Path $dst 'pid.txt'

if (Test-Path $pidFile) {
    $old = [int](Get-Content $pidFile)
    $p = Get-Process -Id $old -ErrorAction SilentlyContinue
    if ($p -and $p.Path -like "$dst*") {
        Stop-Process -Id $old -Force
        "stopped $old"
        Start-Sleep 1
    }
    Remove-Item $pidFile
}
if ($Stop) { return }

New-Item -ItemType Directory -Force "$dst\gamemodes", "$dst\filterscripts", "$dst\scriptfiles", "$dst\models", "$dst\npcmodes" | Out-Null
Copy-Item "$src\omp-server.exe" $dst -Force
Copy-Item "$src\components" $dst -Recurse -Force

$c = Get-Content "$src\config.json" -Raw | ConvertFrom-Json
$c.announce = $false
$c.enable_query = $true
$c.network.bind = $Bind
$c.network.port = $Port
$c.pawn.main_scripts = @('ingame 1')
$c | ConvertTo-Json -Depth 20 | Out-File -Encoding ascii "$dst\config.json"

& "$src\qawno\pawncc.exe" "-i$src\qawno\include" "-o$dst\gamemodes\ingame.amx" -d3 '-;+' '-(+' "$root\wip\world\ingame.pwn" | Select-Object -Last 2

$p = Start-Process -FilePath "$dst\omp-server.exe" -WorkingDirectory $dst -WindowStyle Hidden `
    -RedirectStandardOutput "$dst\server.out" -RedirectStandardError "$dst\server.err" -PassThru
$p.Id | Out-File -Encoding ascii $pidFile
Start-Sleep 2
Get-Content "$dst\server.out" -Tail 1
"server pid $($p.Id) on ${Bind}:$Port, log in $dst\server.out"
