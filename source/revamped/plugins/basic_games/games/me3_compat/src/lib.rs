//! MO2 Revamped loads this DLL into Elden Ring for mods that rely on what
//! me3 does beside loading them (ELDEN RING Reforged):
//! - `disable_arxan`: `dearxan` neutralizes Arxan, which otherwise reacts to
//!   the code those mods patch;
//! - me3's `oversized_regulation_fix`: the game keeps the raw regulation.bin
//!   it loaded and writes it into every save, and a regulation larger than
//!   the game's own (Reforged's) no longer fits, so every save after the
//!   first fails. me3 frees it once the regulation step is idle; this clears
//!   it as soon as it appears;
//! - me3's `mem_patch` (see mem_patch.rs): the game's fixed heaps replaced by
//!   mimalloc, without which Reforged's sound bank runs out of room and music
//!   and voices stop on every dodge or hit.
//!
//! MO2 loads it for every Elden Ring start for me3's BootBoost (see
//! boot_boost.rs); the rest only runs with ELDEN RING Reforged, whose natives
//! read the ME3_ATTACH_CONFIG that MO2 sets for them.

mod boot_boost;
mod mem_patch;

use std::ffi::c_void;
use std::fs::File;
use std::io::Write;
use std::ptr::NonNull;
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use from_singleton::FromSingleton;

struct FileLog(Mutex<Option<File>>);

/// When this DLL loaded: the first DLL MO2 loads, so about the game's start.
static LOADED: OnceLock<Instant> = OnceLock::new();

impl log::Log for FileLog {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= log::Level::Info
    }

    fn log(&self, record: &log::Record) {
        if !self.enabled(record.metadata()) {
            return;
        }
        if let Ok(mut file) = self.0.lock() {
            if let Some(file) = file.as_mut() {
                let elapsed = LOADED.get_or_init(Instant::now).elapsed().as_secs_f64();
                let _ = writeln!(file, "[{elapsed:8.3} s] [{}] {}", record.level(), record.args());
            }
        }
    }

    fn flush(&self) {}
}

static LOG: FileLog = FileLog(Mutex::new(None));

fn open_log() {
    // beside the MO2 bridge's log
    let path = std::env::temp_dir().join("EldenRingMO2Me3Compat.log");
    if let Ok(file) = File::create(path) {
        if let Ok(mut slot) = LOG.0.lock() {
            *slot = Some(file);
        }
        let _ = log::set_logger(&LOG);
        log::set_max_level(log::LevelFilter::Info);
    }
}

/// The start of Elden Ring's CSRegulationManager, as me3 declares it
/// (crates/mod-host/src/savefile.rs).
#[repr(C)]
struct CSRegulationManager {
    _vtable: usize,
    _regulation_step: *mut (),
    // DlVector<*mut ()>: allocator and first/last/end pointers
    _param_res_caps: [usize; 4],
    raw_regulation: Option<NonNull<u8>>,
    raw_regulation_len: usize,
}

impl FromSingleton for CSRegulationManager {}

/// Clears the raw regulation each time the game holds one. The buffer is
/// left allocated (a few MB, once per load): me3 frees it through the game's
/// allocator, which this DLL does not know.
fn keep_regulation_out_of_saves() {
    let mut found_manager = false;
    loop {
        if let Some(manager) = from_singleton::address_of::<CSRegulationManager>() {
            if !found_manager {
                found_manager = true;
                log::info!("CSRegulationManager found at {:p}", manager.as_ptr());
            }
            // SAFETY: the singleton lives for the whole process; the two
            // fields are plain words the game only reads when saving
            unsafe {
                let manager = manager.as_ptr();
                let length = (*manager).raw_regulation_len;
                if (*manager).raw_regulation.is_some() && length != 0 {
                    // the length first: a save racing these writes then sees
                    // nothing to write rather than a length without its data
                    std::ptr::write_volatile(&raw mut (*manager).raw_regulation_len, 0);
                    std::ptr::write_volatile(&raw mut (*manager).raw_regulation, None);
                    log::info!("raw regulation ({length} bytes) kept out of the save");
                }
            }
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn install_boot_boost(main_allocator: Option<*const ()>) {
    // MO2 sets "0" when the profile turned it off
    if std::env::var("ELDENRING_MO2_BOOT_BOOST").is_ok_and(|value| value == "0") {
        log::info!("BootBoost: off in this profile");
        return;
    }
    match boot_boost::install(main_allocator) {
        Ok(()) => log::info!("BootBoost: MountEbl hooked"),
        Err(error) => log::warn!("BootBoost unavailable: {error}"),
    }
}

const DLL_PROCESS_ATTACH: u32 = 1;

#[no_mangle]
pub extern "system" fn DllMain(_module: *mut c_void, reason: u32, _reserved: *mut c_void) -> i32 {
    if reason == DLL_PROCESS_ATTACH {
        LOADED.get_or_init(Instant::now);
        open_log();
        // a panic's message in the log, before it is caught or ends the game
        std::panic::set_hook(Box::new(|info| log::error!("panic: {info}")));
        log::info!("MO2 me3 compatibility 0.5.0 (dearxan 0.5.6, from-singleton 3.0.1, me3's mem_patch and BootBoost) loaded");
        // me3's asset code reads the launched game from me3's launcher, as a
        // JSON string; under MO2 there is none, so it is set here
        if std::env::var_os("ME3_GAME_LAUNCHED").is_none() {
            // still in DllMain, before any thread of this DLL runs
            std::env::set_var("ME3_GAME_LAUNCHED", "\"eldenring\"");
        }
        if std::env::var_os("ME3_ATTACH_CONFIG").is_none() {
            // not Reforged: Arxan stays as it is, and only BootBoost runs,
            // once the game's code is readable, before its main
            log::info!("ELDEN RING Reforged is not active: BootBoost only");
            unsafe {
                dearxan::disabler::schedule_after_arxan(|_, _| install_boot_boost(None));
            }
            return 1;
        }
        unsafe {
            dearxan::disabler::neuter_arxan(|result| {
                let mut before_entry = false;
                match result {
                    Ok(status) => {
                        before_entry = status.is_executing_entrypoint;
                        log::info!(
                            "Arxan detected: {}; before the entry point: {}",
                            status.is_arxan_detected,
                            status.is_executing_entrypoint
                        )
                    }
                    Err(error) => log::error!("Arxan could not be neutralized: {error}"),
                }
                // mem_patch, as me3 does before the game's main: only while the
                // game has allocated nothing yet, or mimalloc would be asked to
                // free the game's own blocks
                if before_entry {
                    match mem_patch::hook_system_allocator() {
                        Ok(()) => {
                            log::info!("mem_patch: system allocator hooked");
                            if let Err(error) = mem_patch::after_steam_init(|| {
                                match mem_patch::hook_heap_allocators() {
                                    Ok(()) => log::info!("mem_patch: heap allocators hooked"),
                                    Err(error) => log::error!("mem_patch: heap allocators not hooked: {error}"),
                                }
                            }) {
                                log::error!("mem_patch: SteamAPI_Init not hooked: {error}");
                            }
                        }
                        Err(error) => log::error!("mem_patch: system allocator not hooked: {error}"),
                    }
                } else {
                    log::warn!("mem_patch skipped: the game has already started");
                }
                install_boot_boost(before_entry.then(mem_patch::main_allocator));
                // the game's code is decrypted now: scanning it for the
                // singletons is safe
                std::thread::spawn(keep_regulation_out_of_saves);
            });
        }
    }
    1
}