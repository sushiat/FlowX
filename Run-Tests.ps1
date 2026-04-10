# Run-Tests.ps1
# Runs each doctest test case in its own subprocess with a timeout.
# Usage:  .\Run-Tests.ps1 [-Exe <path>] [-TimeoutSec <seconds>] [-Filter <group>]
#
# -Filter  When specified, only tests whose group label matches the value are run,
#          and stdout is always printed (even on pass) so route output is visible.
#          Example:  .\Run-Tests.ps1 -Filter TaxiRoute

param(
    [string]$Exe        = "$PSScriptRoot\Debug\FlowXTests.exe",
    [double]$TimeoutSec = 5.0,
    [string]$Filter     = ""
)

if (-not (Test-Path $Exe)) {
    Write-Error "Test binary not found: $Exe"
    exit 1
}

# ── Discover test cases ───────────────────────────────────────────────────────
$allTests = & $Exe --list-test-cases --no-intro 2>&1 |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne '' -and $_ -notmatch '^\[doctest\]' -and $_ -notmatch '^=+$' }

if (-not $allTests) {
    Write-Error "No test cases found in $Exe"
    exit 1
}

# Apply group filter if requested.
$tests = if ($Filter) {
    $allTests | Where-Object {
        if ($_ -match '^(.+?) - ') { $Matches[1] -eq $Filter } else { $false }
    }
} else { $allTests }

if (-not $tests) {
    Write-Error "No tests matched filter '$Filter'"
    exit 1
}

$timeoutMs     = [int]($TimeoutSec * 1000)
$groupTimeouts = @{ 'TaxiRoute' = 30.0 }
$filterActive  = [bool]$Filter

$label = if ($filterActive) { "filter '$Filter'" } else { "timeout ${TimeoutSec}s each" }
Write-Host "Found $($tests.Count) test(s)  |  $label`n"

# ── Run each test in its own process ─────────────────────────────────────────
# Use System.Diagnostics.Process directly: Start-Process -ArgumentList joins
# array elements with spaces without quoting, breaking test names that contain
# spaces.  ProcessStartInfo.Arguments lets us build the command line ourselves.

$pass     = 0
$fail     = 0
$timedOut = 0

foreach ($test in $tests) {
    # Extract the prefix before " - " as a group label (e.g. "TaxiGraph::FindRoute",
    # "HaversineM").  Falls back to the full name if no separator is found.
    if ($test -match '^(.+?) - ') {
        $group    = $Matches[1]
        $testName = $test.Substring($group.Length + 3)   # skip " - "
    } else {
        $group    = ''
        $testName = $test
    }

    $effectiveTimeoutMs = if ($groupTimeouts.ContainsKey($group)) {
        [int]($groupTimeouts[$group] * 1000)
    } else { $timeoutMs }

    $psi                        = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $Exe
    $psi.Arguments              = "--test-case=`"$test`" --no-intro"
    $psi.WorkingDirectory       = Split-Path $Exe -Parent
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $null = $proc.Start()

    # Begin async read before blocking — avoids pipe-buffer deadlock.
    $outputTask = $proc.StandardOutput.ReadToEndAsync()

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $completed = $proc.WaitForExit($effectiveTimeoutMs)
    if (-not $completed) { $proc.Kill() }
    $proc.WaitForExit()   # drain so ExitCode and streams are finalised
    $sw.Stop()

    $output   = $outputTask.GetAwaiter().GetResult()
    $exitCode = $proc.ExitCode
    $proc.Dispose()

    # Right-align elapsed ms in a 5-digit field: "[   5ms]" / "[1000ms]"
    $timeStr  = '[{0,5}ms]' -f $sw.ElapsedMilliseconds
    $groupStr = if ($group) { "[$group]  " } else { '' }

    if (-not $completed) {
        Write-Host "[TIMEOUT] $timeStr  $groupStr$testName" -ForegroundColor Yellow
        $timedOut++
    }
    elseif ($exitCode -eq 0) {
        Write-Host "[PASS   ] $timeStr  $groupStr$testName" -ForegroundColor Green
        # When a filter is active, always show stdout so route details are visible.
        if ($filterActive) {
            foreach ($line in ($output -split "`n")) {
                $l = $line.TrimEnd()
                if ($l -ne '') { Write-Host "          $l" } else { Write-Host "" }
            }
        }
        $pass++
    }
    else {
        Write-Host "[FAIL   ] $timeStr  $groupStr$testName" -ForegroundColor Red
        foreach ($line in ($output -split "`n")) {
            $l = $line.TrimEnd()
            if ($l -ne '') { Write-Host "          $l" } else { Write-Host "" }
        }
        $fail++
    }
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "$($pass + $fail + $timedOut) tests" -NoNewline
Write-Host "  $pass passed"        -ForegroundColor Green  -NoNewline
Write-Host "  $fail failed"        -ForegroundColor $(if ($fail     -gt 0) { 'Red'    } else { 'Gray' }) -NoNewline
Write-Host "  $timedOut timed out" -ForegroundColor $(if ($timedOut -gt 0) { 'Yellow' } else { 'Gray' })

exit ($fail + $timedOut)
