<# Build the payload and installable PS4 launcher locally. Requires Docker. #>
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $projectRoot
try {
    docker build -t control4free-build -f docker/Dockerfile docker
    if ($LASTEXITCODE -ne 0) { throw 'Payload toolchain build failed.' }
    docker build -t control4free-launcher-build -f docker/Dockerfile.launcher docker
    if ($LASTEXITCODE -ne 0) { throw 'Launcher toolchain build failed.' }
    docker run --rm --network none -v "${projectRoot}:/src" -w /src control4free-launcher-build bash -lc 'make && make -C launcher'
    if ($LASTEXITCODE -ne 0) { throw 'Launcher package build failed.' }
    Get-Item -LiteralPath (Join-Path $projectRoot 'build/Control4Free-0.2.1.pkg') | Select-Object FullName,Length
} finally {
    Pop-Location
}
