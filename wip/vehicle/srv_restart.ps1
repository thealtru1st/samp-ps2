# Stops and restarts only the private vehicle test server in wip/vehicle/srv.
# Goes by the process path, so it can never touch the shared server on 7777.
#   srv_restart.ps1          restart, bound to the WSL gateway, for live_run.sh
#   srv_restart.ps1 ingame   restart on all addresses, so the PS2 can reach it too
#   srv_restart.ps1 stop     just stop it
param([string]$mode = "restart")
$dir = Join-Path $PSScriptRoot "srv"
Get-Process omp-server -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $dir "omp-server.exe") } | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
if ($mode -eq "stop") { exit 0 }
$cfg = Join-Path $dir "config.json"
$bind = if ($mode -eq "ingame") { "" } else { "172.21.144.1" }
$text = [IO.File]::ReadAllText($cfg) -replace '"bind":\s*"[^"]*"', ('"bind": "' + $bind + '"')
[IO.File]::WriteAllText($cfg, $text)
$p = Start-Process -FilePath (Join-Path $dir "omp-server.exe") -WorkingDirectory $dir -WindowStyle Hidden -RedirectStandardOutput (Join-Path $dir "server.out") -PassThru
Write-Output "started $($p.Id)"
