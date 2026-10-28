param([string]$Compiler='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
if (!$Compiler) {
 $command=Get-Command g++.exe -ErrorAction SilentlyContinue
 if (!$command) { throw 'MinGW-w64 g++.exe was not found. Add it to PATH or specify -Compiler.' }
 $Compiler=$command.Source
}
$outDir=if($OutputDirectory){$OutputDirectory}else{Join-Path $PSScriptRoot 'dist'}
$objDir=Join-Path $PSScriptRoot 'scratch\build'
New-Item -ItemType Directory -Force $outDir,$objDir | Out-Null
Push-Location (Join-Path $PSScriptRoot 'src')
try {
 & (Join-Path (Split-Path $Compiler) 'windres.exe') app.rc -O coff -o (Join-Path $objDir 'app.o')
 if($LASTEXITCODE){throw 'Resource compilation failed'}
 & $Compiler -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -Wno-missing-field-initializers -Wno-cast-function-type -static -static-libgcc -static-libstdc++ -municode -mwindows -s main.cpp (Join-Path $objDir 'app.o') -o (Join-Path $outDir 'LightTimer.exe') -luuid -lgdiplus -ldwmapi -luser32 -lgdi32 -lshell32 -lole32 -loleacc -loleaut32 -ladvapi32 -lpsapi -lwinmm -lcomdlg32
 if($LASTEXITCODE){throw 'C++ compilation failed'}
} finally {Pop-Location}
$check=Start-Process (Join-Path $outDir 'LightTimer.exe') -ArgumentList '--self-test' -PassThru -Wait -WindowStyle Hidden
if($check.ExitCode){throw "Engine tests failed: $($check.ExitCode)"}
Get-Item (Join-Path $outDir 'LightTimer.exe') | Select-Object FullName,Length


