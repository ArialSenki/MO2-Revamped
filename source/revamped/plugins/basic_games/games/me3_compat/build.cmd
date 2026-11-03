@echo off
rem Builds eldenring_mo2_me3_compat.dll with Rust and MSVC.
rem build.local.cmd (not in the repository) can set CARGO_HOME and RUSTUP_HOME
rem for a portable Rust toolchain.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if exist "%~dp0build.local.cmd" call "%~dp0build.local.cmd"
if not defined CARGO_HOME set "CARGO_HOME=%USERPROFILE%\.cargo"
if not defined RUSTUP_HOME set "RUSTUP_HOME=%USERPROFILE%\.rustup"
cd /d "%~dp0"
rem no local paths in the DLL: these folders can have spaces, which RUSTFLAGS can't carry
"%CARGO_HOME%\bin\cargo.exe" --config "build.rustflags=['--remap-path-prefix=%CARGO_HOME%=cargo','--remap-path-prefix=%RUSTUP_HOME%=rustup']" build --release --target x86_64-pc-windows-msvc || exit /b 1
copy /y "target\x86_64-pc-windows-msvc\release\eldenring_mo2_me3_compat.dll" "%~dp0eldenring_mo2_me3_compat.dll" >nul
echo Built %~dp0eldenring_mo2_me3_compat.dll
