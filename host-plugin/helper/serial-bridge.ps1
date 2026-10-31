# serial-bridge.ps1 - INTERNAL helper process for the dsh-esp32-touch-bridge DSH plugin.
#
# Windows PowerShell 5.1 only (it ships System.IO.Ports.SerialPort). This file is
# not a standalone tool: it has no bin entry, no npm script and no documented
# start command. The plugin starts it automatically when a serial port is
# configured and reaps it when the parent closes stdin (the read loop below
# exits on EOF), so a killed host leaves no stray process.
#
# Wire protocol (JSON lines, docs/12-dsh-plugin.md section 4):
#   stdin  <- {"op":"open","port":"COM7","baud":115200}
#             {"op":"write","data":"<base64>"}  {"op":"close"}  {"op":"ping"}
#   stdout -> {"op":"ready"}  {"op":"line","data":"<base64 device bytes>"}
#             {"op":"state","connected":true|false}
#             {"op":"error","message":"..."}
#
# The port is retried every 2 s while it cannot be opened or after a write/read
# failure. Device bytes are base64 so console encoding can never corrupt them.

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$script:stdout = [Console]::Out

function Send-Message {
    param([hashtable]$Message)
    $json = $Message | ConvertTo-Json -Compress -Depth 6
    $script:stdout.WriteLine($json)
    $script:stdout.Flush()
}

$script:port = $null
$script:baud = 115200
$script:serial = $null
$script:connected = $false
$script:lastState = $null
$script:errorReported = $false
$script:nextOpenAt = 0

function Send-State {
    param([bool]$Connected)
    if ($script:lastState -eq $Connected) { return }
    $script:lastState = $Connected
    Send-Message @{ op = 'state'; connected = $Connected }
}

function Close-Serial {
    param([switch]$Silent)
    $sp = $script:serial
    $script:serial = $null
    if ($null -ne $sp) {
        try { if ($sp.IsOpen) { $sp.Close() } } catch { }
        try { $sp.Dispose() } catch { }
    }
    $script:connected = $false
    if (-not $Silent) { Send-State $false }
}

function Open-Serial {
    param([string]$Port, [int]$Baud)
    Close-Serial -Silent
    $script:port = $Port
    $script:baud = $Baud
    $sp = $null
    try {
        $sp = New-Object System.IO.Ports.SerialPort
        $sp.PortName = $Port
        $sp.BaudRate = $Baud
        $sp.Parity = [System.IO.Ports.Parity]::None
        $sp.DataBits = 8
        $sp.StopBits = [System.IO.Ports.StopBits]::One
        $sp.Handshake = [System.IO.Ports.Handshake]::None
        $sp.ReadTimeout = 100
        $sp.WriteTimeout = 1000
        # DTR/RTS are deliberately left at their defaults: on ESP32-S3
        # USB-Serial-JTAG they are wired to the reset circuit, and toggling them
        # would reboot the pet every time the plugin reconnects.
        $sp.Open()
        $script:serial = $sp
        $script:connected = $true
        $script:errorReported = $false
        Send-State $true
    } catch {
        try { if ($null -ne $sp -and $sp.IsOpen) { $sp.Close() } } catch { }
        try { if ($null -ne $sp) { $sp.Dispose() } } catch { }
        $script:serial = $null
        $script:connected = $false
        Send-State $false
        if (-not $script:errorReported) {
            Send-Message @{ op = 'error'; message = ('open ' + $Port + ' failed: ' + $_.Exception.Message) }
            $script:errorReported = $true
        }
        $script:nextOpenAt = [Environment]::TickCount64 + 2000
    }
}

function Read-Available {
    if (-not $script:connected) { return }
    $sp = $script:serial
    if ($null -eq $sp) { return }
    try {
        $available = $sp.BytesToRead
        while ($available -gt 0) {
            $buf = New-Object byte[] $available
            $read = $sp.Read($buf, 0, $available)
            if ($read -le 0) { break }
            $bytes = $buf
            if ($read -lt $available) { $bytes = [byte[]]($buf[0..($read - 1)]) }
            Send-Message @{ op = 'line'; data = [Convert]::ToBase64String($bytes) }
            $available = $sp.BytesToRead
        }
    } catch {
        $message = $_.Exception.Message
        Close-Serial
        Send-Message @{ op = 'error'; message = ('read failed: ' + $message) }
        $script:nextOpenAt = [Environment]::TickCount64 + 2000
    }
}

function Write-Data {
    param([string]$Base64)
    if (-not $script:connected -or $null -eq $script:serial) {
        Send-Message @{ op = 'error'; message = 'write rejected: serial not connected' }
        return
    }
    try {
        $bytes = [Convert]::FromBase64String($Base64)
        $script:serial.Write($bytes, 0, $bytes.Length)
    } catch {
        $message = $_.Exception.Message
        Close-Serial
        Send-Message @{ op = 'error'; message = ('write failed: ' + $message) }
        $script:nextOpenAt = [Environment]::TickCount64 + 2000
    }
}

function Handle-Line {
    param([string]$Line)
    $request = $null
    try {
        $request = $Line | ConvertFrom-Json
    } catch {
        Send-Message @{ op = 'error'; message = 'invalid json from host' }
        return
    }
    $operation = [string]$request.op
    if ($operation -eq 'open') {
        $port = [string]$request.port
        if ([string]::IsNullOrWhiteSpace($port)) {
            Send-Message @{ op = 'error'; message = 'open rejected: port is required' }
            return
        }
        $baud = 115200
        if ($null -ne $request.baud) {
            try { $baud = [int]$request.baud } catch { $baud = 115200 }
        }
        if ($baud -le 0) { $baud = 115200 }
        Open-Serial -Port $port -Baud $baud
        return
    }
    if ($operation -eq 'write') {
        Write-Data -Base64 ([string]$request.data)
        return
    }
    if ($operation -eq 'close') {
        Close-Serial
        return
    }
    if ($operation -eq 'ping') {
        Send-State $script:connected
        return
    }
    Send-Message @{ op = 'error'; message = ('unknown op: ' + $operation) }
}

Send-Message @{ op = 'ready'; protocol = 1 }

$stdin = [Console]::OpenStandardInput()
$buffer = New-Object byte[] 4096
$pending = $null
$builder = New-Object System.Text.StringBuilder

try {
    while ($true) {
        # Non-blocking stdin read: an outstanding ReadAsync task is polled, so
        # the loop can service the serial port in the same thread.
        if ($null -eq $pending) {
            $pending = $stdin.ReadAsync($buffer, 0, $buffer.Length)
        }
        if ($pending.IsCompleted) {
            $read = $pending.Result
            $pending = $null
            if ($read -le 0) { break }  # parent closed stdin -> exit
            [void]$builder.Append([System.Text.Encoding]::UTF8.GetString($buffer, 0, $read))
            while ($true) {
                $text = $builder.ToString()
                $newline = $text.IndexOf("`n")
                if ($newline -lt 0) { break }
                $line = $text.Substring(0, $newline).Trim()
                [void]$builder.Remove(0, $newline + 1)
                if ($line.Length -gt 0) { Handle-Line -Line $line }
            }
        }

        Read-Available

        $now = [Environment]::TickCount64
        if (-not $script:connected -and $null -ne $script:port -and $now -ge $script:nextOpenAt) {
            Open-Serial -Port $script:port -Baud $script:baud
        }

        [System.Threading.Thread]::Sleep(25)
    }
} catch {
    Send-Message @{ op = 'error'; message = ('bridge loop failed: ' + $_.Exception.Message) }
} finally {
    Close-Serial -Silent
}

exit 0