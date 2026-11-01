@echo off
rem Builds eldenring_mo2_me3_compat.dll with the portable Rust toolchain and MSVC.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CARGO_HOME=D:\Mods Nexus\Herramientas\rust\cargo"
set "RUSTUP_HOME=D:\Mods Nexus\Herramientas\rust\rustup"
cd /d "%~dp0"
"%CARGO_HOME%\bin\cargo.exe" build --release --target x86_64-pc-windows-msvc || exit /b 1
copy /y "target\x86_64-pc-windows-msvc\release\eldenring_mo2_me3_compat.dll" "%~dp0eldenring_mo2_me3_compat.dll" >nul
echo Built %~dp0eldenring_mo2_me3_compat.dll
