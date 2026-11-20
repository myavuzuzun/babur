# Build and Installation Script for babur on Windows x64
param(
    [bool]$Install = $true
)

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " Building babur (Windows x64 VeSTige / VST3)      " -ForegroundColor Cyan
Write-Host " By myavuzuzun                                    " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

if (-not (Test-Path "dist")) { New-Item -ItemType Directory -Path "dist" | Out-Null }
if (-not (Test-Path "bin")) { New-Item -ItemType Directory -Path "bin" | Out-Null }

$commonFlags = @(
    "-O3", "-std=c++17", "-Wall", "-Wextra", "-msse2", "-mfpmath=sse", "-Iinclude",
    "-fno-math-errno", "-fno-trapping-math", "-static",
    "-fno-rtti", "-fno-exceptions", "-ffunction-sections",
    "-fdata-sections", "-flto", "-Wl,--gc-sections", "-s",
    "-Wno-null-pointer-subtraction", "-Wno-unused-parameter"
)
$d2dLibs = @("-ld2d1", "-ldwrite", "-lole32", "-lgdi32", "-luser32", "-lcomdlg32", "-lshell32", "-lwinmm")

# 1. Compile babur legacy plugin DLL via open-source VeSTige header
Write-Host "`n[1/4] Compiling dist/babur.dll (VeSTige 64-bit)..." -ForegroundColor Yellow
g++ @commonFlags -shared src/vestige/BaburVestige.cpp src/dsp/BaburDSP.cpp src/vestige/BaburVestige.def @d2dLibs -o dist/babur.dll
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile dist/babur.dll"; exit 1 }
$szDll = (Get-Item "dist\babur.dll").Length
Write-Host "      -> dist/babur.dll built successfully! ($([math]::Round($szDll/1KB, 1)) KB)" -ForegroundColor Green

# 2. Compile babur VST3 (GPLv3 open-source VST3 headers)
Write-Host "[2/4] Compiling dist/babur.vst3 (VST3 64-bit)..." -ForegroundColor Yellow
g++ @commonFlags -shared src/vst3/BaburVST3.cpp src/dsp/BaburDSP.cpp src/vst3/BaburVST3.def @d2dLibs -o dist/babur.vst3
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile dist/babur.vst3"; exit 1 }
$szVst3 = (Get-Item "dist\babur.vst3").Length
Write-Host "      -> dist/babur.vst3 built successfully! ($([math]::Round($szVst3/1KB, 1)) KB)" -ForegroundColor Green

# 3. Compile Standalone Direct2D Preview
Write-Host "[3/4] Compiling bin/babur_gui.exe (Standalone Direct2D Preview)..." -ForegroundColor Yellow
g++ @commonFlags -mwindows src/ui/standalone_babur.cpp src/dsp/BaburDSP.cpp @d2dLibs -o bin/babur_gui.exe
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile bin/babur_gui.exe"; exit 1 }
$szExe = (Get-Item "bin\babur_gui.exe").Length
Write-Host "      -> bin/babur_gui.exe built successfully! ($([math]::Round($szExe/1KB, 1)) KB)" -ForegroundColor Green

# 4. Compile & Run DSP Unit Test
Write-Host "[4/4] Compiling & running bin/test_babur_dsp.exe..." -ForegroundColor Yellow
g++ -O2 -std=c++17 -Iinclude -I. -Wall -Wextra -Wno-unused-parameter -Wno-null-pointer-subtraction test_babur_dsp.cpp src/dsp/BaburDSP.cpp -lole32 -o bin/test_babur_dsp.exe
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to compile bin/test_babur_dsp.exe"; exit 1 }

# Unblock compiled binaries for Windows Smart App Control / Defender
Get-ChildItem -Path "dist\*.dll", "dist\*.vst3", "bin\*.exe" -ErrorAction SilentlyContinue | ForEach-Object { Unblock-File $_.FullName -ErrorAction SilentlyContinue }

& .\bin\test_babur_dsp.exe
if ($LASTEXITCODE -ne 0) { Write-Error "DSP verification failed!"; exit 1 }

# Run Verification Tests
Write-Host "`nRunning VeSTige legacy plugin verification tests..." -ForegroundColor Yellow
powershell -ExecutionPolicy Bypass -File .\test_vestige.ps1
if ($LASTEXITCODE -ne 0) { Write-Error "VeSTige verification failed!"; exit 1 }

Write-Host "Running VST3 verification tests..." -ForegroundColor Yellow
powershell -ExecutionPolicy Bypass -File .\test_vst3.ps1
if ($LASTEXITCODE -ne 0) { Write-Error "VST3 verification failed!"; exit 1 }

# Installation
if ($Install) {
    Write-Host "==================================================" -ForegroundColor Cyan
    Write-Host " Installing babur into Local DAW Plugin Paths     " -ForegroundColor Cyan
    Write-Host "==================================================" -ForegroundColor Cyan

    $targetLegacyDirs = @(
        "$env:USERPROFILE\Documents\lmms\plugins\vst",
        "$env:APPDATA\LMMS\vst",
        "C:\Program Files\VstPlugins",
        "C:\VstPlugins"
    )
    foreach ($dir in $targetLegacyDirs) {
        if (Test-Path (Split-Path $dir -Parent)) {
            if (-not (Test-Path $dir)) {
                try { New-Item -ItemType Directory -Path $dir -Force | Out-Null } catch {}
            }
            if (Test-Path $dir) {
                try {
                    Copy-Item "dist\babur.dll" "$dir\babur.dll" -Force -ErrorAction Stop
                    Write-Host "  -> Installed legacy .dll to $dir\babur.dll" -ForegroundColor Green
                } catch {
                    Write-Host "  [Notice] Could not copy .dll to $dir ($($_.Exception.Message))." -ForegroundColor Yellow
                }
            }
        }
    }

    $targetVst3Dirs = @(
        "$env:LOCALAPPDATA\Programs\Common\VST3",
        "C:\VstPlugins",
        "C:\Program Files\Common Files\VST3"
    )
    foreach ($dir in $targetVst3Dirs) {
        if (-not (Test-Path $dir)) {
            try { New-Item -ItemType Directory -Path $dir -Force | Out-Null } catch {}
        }
        if (Test-Path $dir) {
            try {
                Copy-Item "dist\babur.vst3" "$dir\babur.vst3" -Force -ErrorAction Stop
                Write-Host "  -> Installed VST3 to $dir\babur.vst3" -ForegroundColor Green
            } catch {
                Write-Host "  [Notice] Could not copy VST3 to $dir ($($_.Exception.Message))." -ForegroundColor Yellow
            }
        }
    }
}

Write-Host "`nBuild complete! babur (VeSTige .dll & VST3) is ready.`n" -ForegroundColor Green
