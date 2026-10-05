<# Build the payload and installable PS4 launcher locally. Requires Docker.
   -Diag builds the diagnostic package instead (Control4Free-<version>-diag.pkg). #>
param([switch]$Diag)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$flag = if ($Diag) { 'C4F_DIAG=1' } else { '' }
$suffix = if ($Diag) { '-diag' } else { '' }
Push-Location $projectRoot
try {
    docker build -t control4free-build -f docker/Dockerfile docker
    if ($LASTEXITCODE -ne 0) { throw 'Payload toolchain build failed.' }
    docker build -t control4free-launcher-build -f docker/Dockerfile.launcher docker
    if ($LASTEXITCODE -ne 0) { throw 'Launcher toolchain build failed.' }
    docker run --rm --network none -v "${projectRoot}:/src" -w /src control4free-launcher-build bash -lc "make $flag && make -C launcher $flag"
    if ($LASTEXITCODE -ne 0) { throw 'Launcher package build failed.' }
    $version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
    Get-Item -LiteralPath (Join-Path $projectRoot "build/Control4Free-$version$suffix.pkg") | Select-Object FullName,Length
} finally {
    Pop-Location
}
