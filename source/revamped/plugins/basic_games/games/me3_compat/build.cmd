@echo off
rem Builds eldenring_mo2_me3_compat.dll with Rust and MSVC.
rem build.local.cmd (not in the repository) can set CARGO_HOME and RUSTUP_HOME
rem for a portable Rust toolchain.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if exist "%~dp0build.local.cmd" call "%~dp0build.local.cmd"
if not defined CARGO_HOME set "CARGO_HOME=%USERPROFILE%\.cargo"
cd /d "%~dp0"
"%CARGO_HOME%\bin\cargo.exe" build --release --target x86_64-pc-windows-msvc || exit /b 1
copy /y "target\x86_64-pc-windows-msvc\release\eldenring_mo2_me3_compat.dll" "%~dp0eldenring_mo2_me3_compat.dll" >nul
echo Built %~dp0eldenring_mo2_me3_compat.dll
