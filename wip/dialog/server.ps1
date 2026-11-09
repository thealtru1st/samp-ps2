# The private dialog test server: powershell -File wip\dialog\server.ps1 start|stop
# Lives in wip\dialog\srv, port 7791, bound to the WSL gateway, never announced. It copies
# the binaries from tools\ompsrv-run and leaves that server alone. "stop" removes the copy.
param([string]$action = 'start')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$srv = Join-Path $here 'srv'
$base = Join-Path $here '..\..\tools\ompsrv-run\Server'
$pidFile = Join-Path $srv 'my.pid'

if ($action -eq 'stop') {
    if (Test-Path $pidFile) {
        $id = [int](Get-Content $pidFile)
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($p -and $p.Path -like '*wip\dialog\srv*') {
            Stop-Process -Id $id -Force -Confirm:$false
            Start-Sleep -Seconds 1
        }
    }
    if (Test-Path $srv) { Remove-Item -Recurse -Force -Confirm:$false $srv }
    'stopped'
    return
}

$gw = ((wsl -d Ubuntu -- ip route) | Where-Object { $_ -like 'default*' } | Select-Object -First 1).Split(' ')[2]
foreach ($d in 'gamemodes', 'filterscripts', 'scriptfiles') {
    New-Item -ItemType Directory -Force (Join-Path $srv $d) | Out-Null
}
Copy-Item (Join-Path $base 'omp-server.exe') $srv
Copy-Item -Recurse -Force (Join-Path $base 'components') $srv
$c = Get-Content (Join-Path $base 'config.json') -Raw | ConvertFrom-Json
$c.announce = $false
$c.enable_query = $true
$c.network.port = 7791
$c.network.bind = $gw
$c.pawn.main_scripts = @('dlgtest 1')
$c.pawn.side_scripts = @()
$c.rcon.enable = $false
[IO.File]::WriteAllText((Join-Path $srv 'config.json'), ($c | ConvertTo-Json -Depth 10))
& (Join-Path $base 'qawno\pawncc.exe') (Join-Path $here 'dlgtest.pwn') "-i$(Join-Path $base 'qawno\include')" "-o$(Join-Path $srv 'gamemodes\dlgtest.amx')" -d3 -O1 | Out-Null
$p = Start-Process -FilePath (Join-Path $srv 'omp-server.exe') -WorkingDirectory $srv -WindowStyle Hidden -PassThru
$p.Id | Out-File -Encoding ascii $pidFile
Start-Sleep -Seconds 3
"running pid $($p.Id) on ${gw}:7791"
