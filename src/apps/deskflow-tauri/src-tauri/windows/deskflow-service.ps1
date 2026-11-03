# Installs or removes the Deskflow service for the Tauri build, which ships its own
# core and daemon. Run by the NSIS installer hooks (windows/hooks.nsh), elevated.
#
#   -Action Install    removes the classic Deskflow (its MSI), points the "Deskflow"
#                      service at this install's daemon, opens the firewall for this
#                      install's core and starts the service
#   -Action Uninstall  stops and removes the service and the firewall rules
#   -DryRun            only prints what it would do
#
# Settings (Deskflow.conf), the server layout and the TLS certificate in
# C:\ProgramData\Deskflow are never touched: the new core reads the same files.

param(
    [Parameter(Mandatory)][ValidateSet('Install', 'Uninstall')][string]$Action,
    [Parameter(Mandatory)][string]$InstallDir,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$serviceName = 'Deskflow'
$firewallRule = 'Deskflow Tauri'
$daemon = Join-Path $InstallDir 'deskflow-daemon.exe'
$core = Join-Path $InstallDir 'deskflow-core.exe'

function Step([string]$what, [scriptblock]$do) {
    if ($DryRun) {
        Write-Host "[dry-run] $what"
    } else {
        Write-Host $what
        & $do
    }
}

function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments | Out-Host
    return $LASTEXITCODE
}

function Get-ClassicDeskflow {
    # the MSI from deskflow.org: exactly "Deskflow" by "Deskflow Devs" (not this app,
    # which is "Deskflow Tauri")
    Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'Deskflow' -and $_.Publisher -eq 'Deskflow Devs' -and $_.WindowsInstaller -eq 1 }
}

function Stop-DeskflowService {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne 'Stopped') {
        Step "Stopping the $serviceName service" {
            Stop-Service -Name $serviceName -Force
            $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
        }
    }
    Step 'Closing Deskflow processes' {
        Get-Process -Name 'deskflow-core', 'deskflow-daemon', 'deskflow' -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
    }
}

if ($Action -eq 'Install') {
    foreach ($file in $daemon, $core) {
        if (-not (Test-Path $file)) { throw "Missing $file" }
    }

    Stop-DeskflowService

    # The classic install owns the same service name and would delete it when it is
    # uninstalled later, and its window stops cores of another version; it goes first.
    foreach ($classic in Get-ClassicDeskflow) {
        $code = $classic.PSChildName
        Step "Uninstalling the classic Deskflow $($classic.DisplayVersion) ($code)" {
            $p = Start-Process -FilePath 'msiexec.exe' -ArgumentList '/x', $code, '/qn', '/norestart' -Wait -PassThru
            # 3010: done, reboot suggested; the service was removed either way
            if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) {
                throw "Uninstalling the classic Deskflow failed (msiexec exit code $($p.ExitCode))"
            }
        }
    }

    # Same settings as the classic MSI (deploy/windows/wix-patch.xml.in)
    $binPath = '"{0}"' -f $daemon
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
        Step "Pointing the $serviceName service at $daemon" {
            if ((Run 'sc.exe' @('config', $serviceName, 'binPath=', $binPath, 'start=', 'auto', 'obj=', 'LocalSystem', 'DisplayName=', 'Deskflow')) -ne 0) {
                throw 'sc config failed'
            }
        }
    } else {
        Step "Creating the $serviceName service for $daemon" {
            if ((Run 'sc.exe' @('create', $serviceName, 'binPath=', $binPath, 'start=', 'auto', 'obj=', 'LocalSystem', 'DisplayName=', 'Deskflow')) -ne 0) {
                throw 'sc create failed'
            }
        }
    }
    Step 'Setting the service description and restart on failure' {
        Run 'sc.exe' @('description', $serviceName, 'Runs the Core process on secure desktops (UAC prompts, login screen, etc).') | Out-Null
        Run 'sc.exe' @('failure', $serviceName, 'reset=', '86400', 'actions=', 'restart/1000/restart/1000//') | Out-Null
    }

    # Server mode needs incoming connections to the core (the classic MSI's rules
    # name the classic path)
    Step "Allowing $core through the firewall" {
        Get-NetFirewallRule -DisplayName $firewallRule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        New-NetFirewallRule -DisplayName $firewallRule -Direction Inbound -Action Allow -Program $core -Profile Any | Out-Null
    }

    Step "Starting the $serviceName service" {
        Start-Service -Name $serviceName
    }
} else {
    Stop-DeskflowService
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
        $current = (Get-CimInstance Win32_Service -Filter "Name='$serviceName'").PathName
        if ($current -and $current.Trim('"') -ieq $daemon) {
            Step "Removing the $serviceName service" {
                Run 'sc.exe' @('delete', $serviceName) | Out-Null
            }
        } else {
            Write-Host "Leaving the $serviceName service alone: it runs $current"
        }
    }
    Step 'Removing the firewall rule' {
        Get-NetFirewallRule -DisplayName $firewallRule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
}
