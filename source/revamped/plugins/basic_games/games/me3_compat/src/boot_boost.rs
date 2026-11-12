//! me3's BootBoost (crates/mod-host/src/asset_hooks.rs, MIT), adapted.
//!
//! The headers (.bhd) of the game's archives are RSA-encrypted, and the game
//! decrypts them on every start, which takes seconds. The first start keeps
//! each decrypted header, compressed, in a cache; later starts hand the game
//! that copy and skip the decryption. A cached copy is named by a hash of the
//! encrypted file, so a game update simply makes new ones. Each copy is
//! `[decrypted length: u32][xxh3 of the decrypted header: u64][deflate]`,
//! written to a temporary file and renamed into place, so a damaged or half
//! written copy is never handed to the game.
//!
//! me3 moves the mounted archives into its own file system; MO2 leaves them
//! with the game, where they would have been without the cache.

use std::{
    alloc::{GlobalAlloc, Layout},
    ffi::OsString,
    io::Write,
    mem,
    os::windows::ffi::{OsStrExt, OsStringExt},
    path::{Path, PathBuf},
    ptr::NonNull,
    slice,
    sync::{Mutex, OnceLock},
    time::{Duration, SystemTime},
};

use me3_mod_host_assets::{
    bhd5::{Bhd5Header, Bhd5Holder},
    dl_device::{self, DlDevice, DlDeviceManager, VfsMounts},
    ebl,
};
use me3_mod_host_types::alloc::{DlAllocator, DlStdAllocator};
use miniz_oxide::{
    deflate::compress_to_vec,
    inflate::stream::{inflate, InflateState},
    DataFormat, MZFlush,
};
use retour::RawDetour;
use xxhash_rust::xxh3;

use crate::mem_patch::game;

type MountEbl = unsafe extern "C" fn(
    mount_name: *const u16,
    bhd_path: *const u16,
    bdt_path: *const u16,
    allocator: DlStdAllocator,
    rsa_key: *const u8,
    key_len: usize,
) -> bool;

static TRAMPOLINE: OnceLock<MountEbl> = OnceLock::new();
static CACHE: OnceLock<PathBuf> = OnceLock::new();
/// The allocator mem_patch put in the game's place, when it did.
static MAIN_ALLOCATOR: OnceLock<usize> = OnceLock::new();

struct DeviceManager(Option<NonNull<DlDeviceManager>>);
unsafe impl Send for DeviceManager {}
unsafe impl Sync for DeviceManager {}
static DEVICE_MANAGER: OnceLock<DeviceManager> = OnceLock::new();

fn device_manager() -> Option<NonNull<DlDeviceManager>> {
    DEVICE_MANAGER
        .get_or_init(|| {
            let main = MAIN_ALLOCATOR
                .get()
                .map(|address| unsafe { &*(*address as *const DlAllocator) });
            match dl_device::find_device_manager(game(), main) {
                Ok(manager) => DeviceManager(Some(manager)),
                Err(error) => {
                    log::warn!("BootBoost: the game's device manager was not found: {error}");
                    DeviceManager(None)
                }
            }
        })
        .0
}

/// The cache files' extension; the first format, `.bhd.zz`, had no checksum.
const CACHE_EXTENSION: &str = "bhd.zz2";
const CACHE_HEADER: usize = 4 + 8;

/// Cached copies not used for this long, and not in this run, are deleted:
/// each game update leaves a new set behind.
const CACHE_MAX_AGE: Duration = Duration::from_secs(60 * 24 * 60 * 60);
/// When the old copies are looked for: the game mounts its archives in its
/// first seconds, so by then this run's copies are known.
const PRUNE_DELAY: Duration = Duration::from_secs(3 * 60);
/// The cached copies this run read or wrote.
static USED: Mutex<Vec<PathBuf>> = Mutex::new(Vec::new());

fn mark_used(path: &Path) {
    if let Ok(mut used) = USED.lock() {
        used.push(path.to_path_buf());
    }
}

/// A copy read again counts as new, so one a game still uses never ages out.
fn touch(path: &Path) {
    let _ = std::fs::File::options()
        .write(true)
        .open(path)
        .and_then(|file| file.set_modified(SystemTime::now()));
}

/// Deletes the copies older than CACHE_MAX_AGE that this run did not use.
fn prune_cache(folder: &Path) {
    let Ok(entries) = std::fs::read_dir(folder) else {
        return;
    };
    let now = SystemTime::now();
    let used = USED.lock().map(|used| used.clone()).unwrap_or_default();
    let mut removed = 0usize;
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_ascii_lowercase();
        if !name.ends_with(&format!(".{CACHE_EXTENSION}")) {
            continue;
        }
        let path = entry.path();
        if used.iter().any(|kept| kept == &path) {
            continue;
        }
        let old = entry
            .metadata()
            .and_then(|metadata| metadata.modified())
            .ok()
            .and_then(|modified| now.duration_since(modified).ok())
            .is_some_and(|age| age > CACHE_MAX_AGE);
        if old && std::fs::remove_file(&path).is_ok() {
            removed += 1;
        }
    }
    if removed > 0 {
        log::info!("BootBoost: deleted {removed} cached header(s) unused for 60 days");
    }
}

fn schedule_cache_prune(folder: PathBuf) {
    let started = std::thread::Builder::new()
        .name("bootboost-cache-prune".into())
        .spawn(move || {
            std::thread::sleep(PRUNE_DELAY);
            // nothing here may end the game
            let _ = std::panic::catch_unwind(|| prune_cache(&folder));
        });
    if let Err(error) = started {
        log::warn!("BootBoost: old cached headers are kept: {error}");
    }
}

/// Where the decrypted headers are kept, beside MO2 Revamped's other data.
fn cache_folder() -> Option<PathBuf> {
    let base = std::env::var_os("LOCALAPPDATA")?;
    let folder = Path::new(&base).join("MO2 Revamped").join("BootBoost");
    std::fs::create_dir_all(&folder).ok()?;
    // copies in the first format and leftovers of an interrupted write
    if let Ok(entries) = std::fs::read_dir(&folder) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_ascii_lowercase();
            if name.ends_with(".bhd.zz") || name.ends_with(".tmp") {
                let _ = std::fs::remove_file(entry.path());
            }
        }
    }
    Some(folder)
}

/// Hooks the game's MountEbl, once the game's code can be read.
/// `main_allocator`: mem_patch's allocator, when it replaced the game's.
pub fn install(main_allocator: Option<*const ()>) -> Result<(), String> {
    if let Some(allocator) = main_allocator {
        let _ = MAIN_ALLOCATOR.set(allocator as usize);
    }
    let cache = cache_folder().ok_or("no cache folder")?;
    schedule_cache_prune(cache.clone());
    let _ = CACHE.set(cache);
    let mount = ebl::mount_ebl(game()).ok_or("MountEbl not found")?;
    unsafe {
        let detour = RawDetour::new(mount as *const (), mount_ebl_hook as *const ())
            .map_err(|error| error.to_string())?;
        // before the hook can run: a game thread may mount right away
        let _ = TRAMPOLINE.set(mem::transmute::<*const (), MountEbl>(detour.trampoline() as *const ()));
        detour.enable().map_err(|error| error.to_string())?;
        // lives as long as the game
        mem::forget(detour);
    }
    Ok(())
}

unsafe extern "C" fn mount_ebl_hook(
    mount_name: *const u16,
    bhd_path: *const u16,
    bdt_path: *const u16,
    allocator: DlStdAllocator,
    rsa_key: *const u8,
    key_len: usize,
) -> bool {
    let trampoline = *TRAMPOLINE.get().expect("hook without trampoline");
    let mount = |path: *const u16| unsafe {
        trampoline(mount_name, path, bdt_path, allocator, rsa_key, key_len)
    };
    // a panic must not cross into the game: it would end the process
    let cached = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        load_cached(bhd_path, allocator, rsa_key, key_len, &mount)
    }));
    match cached {
        Ok(Ok(())) => true,
        Ok(Err(error)) => {
            log::warn!("BootBoost: {error}; the game decrypts this archive itself");
            mount(bhd_path)
        }
        Err(_) => {
            log::error!("BootBoost: panicked; the game decrypts this archive itself");
            mount(bhd_path)
        }
    }
}

/// An `EblFileDevice`'s BHD5 holder (ELDEN RING and later; me3's
/// `EblFileDeviceSdt`).
unsafe fn bhd_holder(device: NonNull<DlDevice>) -> NonNull<Bhd5Holder> {
    unsafe { device.cast::<u8>().byte_add(0xb0).cast() }
}

fn wide_null(path: &Path) -> Vec<u16> {
    path.as_os_str().encode_wide().chain(Some(0)).collect()
}

fn wide_len(path: *const u16) -> usize {
    let mut length = 0;
    while unsafe { *path.add(length) } != 0 {
        length += 1;
    }
    length
}

/// The RSA block size: the length of the modulus in the PKCS#1 public key.
fn rsa_block_size(key: *const u8, key_len: usize) -> Result<usize, String> {
    if key.is_null() || key_len == 0 {
        return Err("no key".into());
    }
    // the game's length, which may count a terminating zero
    let bytes = unsafe { slice::from_raw_parts(key, key_len) };
    let bytes = bytes.split(|&byte| byte == 0).next().unwrap_or(bytes);
    let text = std::str::from_utf8(bytes).map_err(|_| "the key is not text")?;
    let mut lines = text.lines();
    if lines.next() != Some("-----BEGIN RSA PUBLIC KEY-----") {
        return Err("malformed PEM".into());
    }
    let body: String = lines
        .take_while(|line| !line.starts_with("-----END"))
        .flat_map(|line| line.chars())
        .filter(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '/' | '='))
        .collect();
    let der = base64_decode(&body).ok_or("malformed base64")?;
    // SEQUENCE { INTEGER modulus, INTEGER exponent }
    let (_, sequence) = der_item(&der, 0x30).ok_or("malformed key")?;
    let (_, modulus) = der_item(sequence, 0x02).ok_or("malformed modulus")?;
    let leading_zero = modulus.first() == Some(&0);
    Ok(modulus.len() - usize::from(leading_zero))
}

/// One DER item of the given tag: (the rest after it, its contents).
fn der_item(data: &[u8], tag: u8) -> Option<(&[u8], &[u8])> {
    if *data.first()? != tag {
        return None;
    }
    let first = *data.get(1)? as usize;
    let (length, header) = if first < 0x80 {
        (first, 2)
    } else {
        let count = first & 0x7f;
        let mut length = 0usize;
        for i in 0..count {
            length = (length << 8) | *data.get(2 + i)? as usize;
        }
        (length, 2 + count)
    };
    let contents = data.get(header..header + length)?;
    Some((&data[header + length..], contents))
}

fn base64_decode(text: &str) -> Option<Vec<u8>> {
    let value = |c: u8| -> Option<u32> {
        Some(match c {
            b'A'..=b'Z' => (c - b'A') as u32,
            b'a'..=b'z' => (c - b'a' + 26) as u32,
            b'0'..=b'9' => (c - b'0' + 52) as u32,
            b'+' => 62,
            b'/' => 63,
            _ => return None,
        })
    };
    let bytes: Vec<u8> = text.bytes().filter(|&c| c != b'=').collect();
    let mut out = Vec::with_capacity(bytes.len() * 3 / 4);
    for chunk in bytes.chunks(4) {
        let mut buffer = 0u32;
        for (i, &c) in chunk.iter().enumerate() {
            buffer |= value(c)? << (18 - 6 * i);
        }
        let produced = chunk.len() * 6 / 8;
        for i in 0..produced {
            out.push((buffer >> (16 - 8 * i)) as u8);
        }
    }
    Some(out)
}

/// Mounts through the game and takes the new mount out of its device
/// manager: `keep` puts it back for good, and a mount that is dropped
/// instead stays out (a failed attempt then leaves no trace).
fn mount_and_take(
    manager: NonNull<DlDeviceManager>,
    mount: &dyn Fn(*const u16) -> bool,
    path: *const u16,
) -> Result<(NonNull<DlDevice>, VfsMounts), String> {
    let mut guard = DlDeviceManager::lock(manager);
    let snapshot = guard.snapshot().map_err(|error| error.to_string())?;
    if !mount(path) {
        return Err("the game could not mount the archive".into());
    }
    let new_mounts = guard.extract_new(snapshot);
    let device = new_mounts
        .devices()
        .next()
        .ok_or("the game added no device")?;
    Ok((device, new_mounts))
}

/// Back where the game put it (me3 serves it from its own file system).
fn keep(manager: NonNull<DlDeviceManager>, mounts: &VfsMounts) {
    let mut guard = DlDeviceManager::lock(manager);
    mem::forget(guard.push_vfs_mounts(mounts));
}

fn load_cached(
    bhd_path: *const u16,
    allocator: DlStdAllocator,
    rsa_key: *const u8,
    key_len: usize,
    mount: &dyn Fn(*const u16) -> bool,
) -> Result<(), String> {
    let manager = device_manager().ok_or("no device manager")?;
    let cache = CACHE.get().ok_or("no cache folder")?;

    let disk_path = {
        let guard = DlDeviceManager::lock(manager);
        let path = unsafe { slice::from_raw_parts(bhd_path, wide_len(bhd_path)) };
        OsString::from_wide(&guard.expand_path(path))
    };
    let block_size = rsa_block_size(rsa_key, key_len)?;
    let original = std::fs::read(&disk_path)
        .map_err(|error| format!("{}: {error}", PathBuf::from(&disk_path).display()))?;
    let hash = xxh3::xxh3_128_with_seed(&original, 1);
    let cached_path = cache.join(format!("{hash:032x}.{CACHE_EXTENSION}"));

    // a copy too short for its own header counts as missing
    let cached = std::fs::read(&cached_path).ok().filter(|bytes| bytes.len() > CACHE_HEADER);
    if let Some(cached) = cached {
        let cached_len = u32::from_le_bytes(cached[0..4].try_into().unwrap());
        let checksum = u64::from_le_bytes(cached[4..12].try_into().unwrap());
        // the game mounts a one-block copy, which gives the device and the
        // decrypted size without decrypting the whole header
        let stub = std::env::temp_dir().join(format!("mo2-bootboost-{hash:032x}.bhd"));
        std::fs::write(&stub, &original[..block_size.min(original.len())])
            .map_err(|error| format!("stub file: {error}"))?;
        let stub_wide = wide_null(&stub);
        let taken = mount_and_take(manager, mount, stub_wide.as_ptr());
        let _ = std::fs::remove_file(&stub);
        // from here, returning an error drops the stub's mount: the game
        // then mounts the archive itself
        let (device, mounts) = taken?;
        let holder = unsafe { bhd_holder(device).as_mut() };
        let original_len = holder.bhd_header().map(Bhd5Header::file_size);
        if original_len != Some(cached_len) {
            let _ = std::fs::remove_file(&cached_path);
            return Err("the cached header does not match; it is made again".into());
        }
        let length = cached_len as usize;
        let layout = unsafe { Layout::from_size_align_unchecked(length, 4096) };
        // the game's own allocator: the game frees it later
        let buffer = unsafe {
            let pointer = allocator.alloc(layout);
            if pointer.is_null() {
                return Err("no memory for the cached header".into());
            }
            slice::from_raw_parts_mut(pointer, length)
        };
        let mut state = InflateState::new_boxed(DataFormat::Raw);
        let result = inflate(&mut state, &cached[CACHE_HEADER..], buffer, MZFlush::Finish);
        if result.status.is_err()
            || result.bytes_written != length
            || xxh3::xxh3_64(buffer) != checksum
        {
            unsafe { allocator.dealloc(buffer.as_mut_ptr(), layout) };
            let _ = std::fs::remove_file(&cached_path);
            return Err("the cached header is damaged; it is made again".into());
        }
        unsafe { holder.assign_bhd_contents(buffer.as_mut_ptr().cast()) };
        keep(manager, &mounts);
        mark_used(&cached_path);
        touch(&cached_path);
        return Ok(());
    }

    // not cached yet: the game decrypts it, and the result is kept
    let (device, mounts) = mount_and_take(manager, mount, bhd_path)?;
    // mounted the game's own way: it stays, whatever happens to the cache,
    // and no error may follow, or the hook would mount it a second time
    keep(manager, &mounts);
    let Some(header) = (unsafe { bhd_holder(device).as_ref() }).bhd_header() else {
        log::warn!("BootBoost: the mounted archive has no header; not cached");
        return Ok(());
    };
    let contents = header.as_slice();
    let mut file = Vec::with_capacity(CACHE_HEADER + contents.len() / 2);
    file.extend_from_slice(&header.file_size().to_le_bytes());
    file.extend_from_slice(&xxh3::xxh3_64(contents).to_le_bytes());
    file.extend_from_slice(&compress_to_vec(contents, 7));
    // a temporary name first: a copy cut short by a crash is never read
    let temporary = cache.join(format!("{hash:032x}.{}.tmp", std::process::id()));
    let written = std::fs::File::create(&temporary)
        .and_then(|mut out| out.write_all(&file).and_then(|_| out.sync_all()))
        .and_then(|_| std::fs::rename(&temporary, &cached_path));
    match written {
        Ok(()) => {
            mark_used(&cached_path);
            log::info!("BootBoost: cached {}", PathBuf::from(&disk_path).display())
        }
        Err(error) => {
            let _ = std::fs::remove_file(&temporary);
            log::warn!("BootBoost: {} not cached: {error}", PathBuf::from(&disk_path).display());
        }
    }
    Ok(())
}
