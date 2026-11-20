# VST3 Verification Test Script for babur
$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " Testing babur VST3                               " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

Get-ChildItem -Path "dist\*.vst3" -ErrorAction SilentlyContinue | ForEach-Object {
    Unblock-File $_.FullName -ErrorAction SilentlyContinue
}

Write-Host "Compiling VST3 test loader..." -ForegroundColor Yellow
g++ -O2 -std=c++17 -Iinclude -Wall -Wextra -Wno-unused-parameter src/vst3/test_loader.cpp -o bin/test_vst3_loader.exe
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile test_vst3_loader.exe"; exit 1 }
Unblock-File "bin\test_vst3_loader.exe" -ErrorAction SilentlyContinue

& .\bin\test_vst3_loader.exe "dist\babur.vst3"
if ($LASTEXITCODE -ne 0) { Write-Error "VST3 loader verification failed!"; exit 1 }

Write-Host "`n[SUCCESS] babur VST3 verification passed!`n" -ForegroundColor Green
