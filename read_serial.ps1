$port = New-Object System.IO.Ports.SerialPort('COM4', 115200)
$port.Open()
$end = (Get-Date).AddSeconds(50)
while ((Get-Date) -lt $end) {
    if ($port.BytesToRead -gt 0) {
        $line = $port.ReadLine()
        if ($line -match 'GitHub|Fetch|WiFi|Body|error|Error') {
            Write-Output $line
        }
    } else {
        Start-Sleep -Milliseconds 50
    }
}
$port.Close()
