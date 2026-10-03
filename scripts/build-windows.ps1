param(
    [ValidateSet('default', 'avx2')][string]$Simd = 'default',
    [string]$OutputDirectory = 'dist'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if (!(Test-Path 'sqlite-vec.h') -or !(Test-Path 'vendor/sqlite3ext.h')) {
        throw 'Generate sqlite-vec.h and vendor SQLite headers before building.'
    }
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $output = (Resolve-Path $OutputDirectory).Path
    $compilerArgs = @('/nologo', '/W4', '/O2', '/fp:precise', '/LD', '/Ivendor',
        "/Fo$output/vec0.obj", "/Fe$output/vec0.dll")
    if ($Simd -eq 'avx2') {
        $compilerArgs += @('/arch:AVX2', '/DSQLITE_VEC_ENABLE_AVX')
    }
    & cl.exe @compilerArgs sqlite-vec.c /link "/IMPLIB:$output/vec0.lib"
    if ($LASTEXITCODE -ne 0) { throw "MSVC failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
