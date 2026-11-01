//! MO2 Revamped loads this DLL into Elden Ring for mods that rely on what
//! me3 does beside loading them (ELDEN RING Reforged):
//! - `disable_arxan`: `dearxan` neutralizes Arxan, which otherwise reacts to
//!   the code those mods patch;
//! - me3's `oversized_regulation_fix`: the game keeps the raw regulation.bin
//!   it loaded and writes it into every save, and a regulation larger than
//!   the game's own (Reforged's) no longer fits, so every save after the
//!   first fails. me3 frees it once the regulation step is idle; this clears
//!   it as soon as it appears.

use std::ffi::c_void;
use std::fs::File;
use std::io::Write;
use std::ptr::NonNull;
use std::sync::Mutex;
use std::time::Duration;

use from_singleton::FromSingleton;

struct FileLog(Mutex<Option<File>>);

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
                let _ = writeln!(file, "[{}] {}", record.level(), record.args());
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
                    (*manager).raw_regulation = None;
                    (*manager).raw_regulation_len = 0;
                    log::info!("raw regulation ({length} bytes) kept out of the save");
                }
            }
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

const DLL_PROCESS_ATTACH: u32 = 1;

#[no_mangle]
pub extern "system" fn DllMain(_module: *mut c_void, reason: u32, _reserved: *mut c_void) -> i32 {
    if reason == DLL_PROCESS_ATTACH {
        open_log();
        log::info!("MO2 me3 compatibility 0.2.0 (dearxan 0.5.2, from-singleton 3.0.1) loaded");
        unsafe {
            dearxan::disabler::neuter_arxan(|result| {
                match result {
                    Ok(status) => log::info!(
                        "Arxan detected: {}; before the entry point: {}",
                        status.is_arxan_detected,
                        status.is_executing_entrypoint
                    ),
                    Err(error) => log::error!("Arxan could not be neutralized: {error}"),
                }
                // the game's code is decrypted now: scanning it for the
                // singletons is safe
                std::thread::spawn(keep_regulation_out_of_saves);
            });
        }
    }
    1
}
