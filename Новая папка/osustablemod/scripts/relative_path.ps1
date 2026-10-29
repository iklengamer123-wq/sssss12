# prints the path of the target file relative to the start directory
# used because cmake file(RELATIVE_PATH) produces broken windows paths, and
# the precompiled header path must stay ascii for cl.exe
param(
  [Parameter(Mandatory = $true)][string]$Start,
  [Parameter(Mandatory = $true)][string]$Target
)

$ErrorActionPreference = "Stop"

if ($Start.Length -lt 2 -or $Start.Substring(1, 1) -ne ":") { throw "start is not absolute: $Start" }
if ($Target.Length -lt 2 -or $Target.Substring(1, 1) -ne ":") { throw "target is not absolute: $Target" }
if ($Start.Substring(0, 1).ToLower() -ne $Target.Substring(0, 1).ToLower()) { throw "start and target are on different drives" }

$startParts = $Start.Replace("/", "\").TrimEnd("\").Split("\")
$targetParts = $Target.Replace("/", "\").Split("\")

$common = 0
while ($common -lt $startParts.Count -and $common -lt $targetParts.Count -and
       $startParts[$common].ToLower() -eq $targetParts[$common].ToLower()) {
  $common++
}

$parts = @()
for ($i = $common; $i -lt $startParts.Count; $i++) { $parts += ".." }
for ($i = $common; $i -lt $targetParts.Count; $i++) { $parts += $targetParts[$i] }

$parts -join "\"
