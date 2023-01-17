#
# Simple Demo Script
# Remote controlling Clumsy
###########################################

# Configuration:
###########################################

# Two alternating values
$val1 = 40
$val2 = 100

# Duration to keep each value in seconds
$staySec = 5

# Duration of value change ramp in seconds (do not make this too small)
$rampSec = 1
# Steps in which the value will be changed (do not make this too small)
$valStep = 5

# Command controlling which value to change
$cmd = "LagDelayMs "

# Clumsy executable
$clumsy = ".\clumsy.exe"


# Checks and preparations
###########################################
if ($val1 -eq $val2) {
	Write-Error "You must specify two differen values as value ramp"
	exit
}
if ($valStep -eq 0) {
	Write-Error "You must specify a ramp value step"
	exit
}
if ($rampSec -le 0) {
	Write-Error "You must specify a ramp duration"
	exit
}

$stepSleep = ($rampSec * 1000) / [Math]::Abs($val2 - $val1)


# Control loop
###########################################
Write-Host "Press Ctrl+C to exit loop" -background black -foreground cyan

while($true)
{
	# val1
	$fullCmd = $cmd + $val1
	Write-Host $fullCmd
	. $clumsy --sendcmd $fullCmd
	Start-Sleep -Seconds $staySec

	# ramp val1 -> val2
	$val = $val1
	while ($val -ne $val2)
	{
		$val += (($val1 -lt $val2) ? 1 : -1) * [Math]::Abs($valStep)
		if ([Math]::Abs($val2 - $val) -lt $valStep) { $val = $val2 }
		$fullCmd = $cmd + $val
		# Write-Host $fullCmd
		. $clumsy --sendcmd $fullCmd
		Start-Sleep -Milliseconds $stepSleep
	}

	# val2
	$fullCmd = $cmd + $val2
	Write-Host $fullCmd
	. $clumsy --sendcmd $fullCmd
	Start-Sleep -Seconds $staySec

	# ramp val2 -> val1
	$val = $val2
	while ($val -ne $val1)
	{
		$val += (($val1 -lt $val2) ? -1 : 1) * [Math]::Abs($valStep)
		if ([Math]::Abs($val1 - $val) -lt $valStep) { $val = $val1 }
		$fullCmd = $cmd + $val
		# Write-Host $fullCmd
		. $clumsy --sendcmd $fullCmd
		Start-Sleep -Milliseconds $stepSleep
	}

}

###########################################
