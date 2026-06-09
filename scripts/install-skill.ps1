param(
  [string]$CodexHome = $env:CODEX_HOME
)

$ErrorActionPreference = "Stop"

if (-not $CodexHome -or $CodexHome.Trim().Length -eq 0) {
  $CodexHome = Join-Path $HOME ".codex"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot "skills/gil-editing"
$targetRoot = Join-Path $CodexHome "skills"
$target = Join-Path $targetRoot "gil-editing"

if (-not (Test-Path -LiteralPath $source)) {
  throw "Skill source not found: $source"
}

New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
if (Test-Path -LiteralPath $target) {
  Remove-Item -LiteralPath $target -Recurse -Force
}

Copy-Item -LiteralPath $source -Destination $target -Recurse
Write-Output "Installed gil-editing skill to $target"

