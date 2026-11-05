//! me3's BootBoost (crates/mod-host/src/asset_hooks.rs, MIT), adapted.
//!
//! The headers (.bhd) of the game's archives are RSA-encrypted, and the game
//! decrypts them on every start, which takes seconds. The first start keeps
//! each decrypted header, compressed, in a cache; later starts hand the game
//! that copy and skip the decryption. A cached copy is named by a hash of the
//! encrypted file, so a game update simply makes new ones.
//!
//! me3 moves the mounted archives into its own file system; MO2 leaves them
//! with the game, where they would have been without the cache.

use std::{
    alloc::{GlobalAlloc, Layout},
    ffi::OsString,
    fs::OpenOptions,
    io::{Read, Write},
    mem,
    os::windows::ffi::{OsStrExt, OsStringExt},
    path::{Path, PathBuf},
    ptr::NonNull,
    slice,
    sync::OnceLock,
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

/// Where the decrypted headers are kept, beside MO2 Revamped's other data.
fn cache_folder() -> Option<PathBuf> {
    let base = std::env::var_os("LOCALAPPDATA")?;
    let folder = Path::new(&base).join("MO2 Revamped").join("BootBoost");
    std::fs::create_dir_all(&folder).ok()?;
    Some(folder)
}

/// Hooks the game's MountEbl, once the game's code can be read.
/// `main_allocator`: mem_patch's allocator, when it replaced the game's.
pub fn install(main_allocator: Option<*const ()>) -> Result<(), String> {
    if let Some(allocator) = main_allocator {
        let _ = MAIN_ALLOCATOR.set(allocator as usize);
    }
    let cache = cache_folder().ok_or("no cache folder")?;
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
        load_cached(bhd_path, allocator, rsa_key, &mount)
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
fn rsa_block_size(key: *const u8) -> Result<usize, String> {
    let text = unsafe { std::ffi::CStr::from_ptr(key.cast()) }
        .to_str()
        .map_err(|_| "the key is not text")?;
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
    mount: &dyn Fn(*const u16) -> bool,
) -> Result<(), String> {
    let manager = device_manager().ok_or("no device manager")?;
    let cache = CACHE.get().ok_or("no cache folder")?;

    let disk_path = {
        let guard = DlDeviceManager::lock(manager);
        let path = unsafe { slice::from_raw_parts(bhd_path, wide_len(bhd_path)) };
        OsString::from_wide(&guard.expand_path(path))
    };
    let block_size = rsa_block_size(rsa_key)?;
    let original = std::fs::read(&disk_path)
        .map_err(|error| format!("{}: {error}", PathBuf::from(&disk_path).display()))?;
    let hash = xxh3::xxh3_128_with_seed(&original, 1);
    let cached_path = cache.join(format!("{hash:032x}.bhd.zz"));

    let mut cached = OpenOptions::new()
        .create(true)
        .read(true)
        .write(true)
        .truncate(false)
        .open(&cached_path)
        .map_err(|error| format!("cache file: {error}"))?;
    let cached_len = {
        let mut bytes = [0u8; 4];
        cached.read_exact(&mut bytes).ok().map(|_| u32::from_le_bytes(bytes))
    };

    if let Some(cached_len) = cached_len {
        // the game mounts a one-block copy, which gives the device and the
        // decrypted size without decrypting the whole header
        let stub = std::env::temp_dir().join(format!("mo2-bootboost-{hash:032x}.bhd"));
        std::fs::write(&stub, &original[..block_size.min(original.len())])
            .map_err(|error| format!("stub file: {error}"))?;
        let stub_wide = wide_null(&stub);
        let taken = mount_and_take(manager, mount, stub_wide.as_ptr());
        let _ = std::fs::remove_file(&stub);
        let (device, mounts) = taken?;
        let holder = unsafe { bhd_holder(device).as_mut() };
        let original_len = holder.bhd_header().map(Bhd5Header::file_size);
        if original_len != Some(cached_len) {
            let _ = cached.set_len(0);
            return Err("the cached header does not match; it is made again".into());
        }
        let mut compressed = Vec::new();
        cached
            .read_to_end(&mut compressed)
            .map_err(|error| format!("cache read: {error}"))?;
        let length = cached_len as usize;
        // the game's own allocator: the game frees it later
        let buffer = unsafe {
            let pointer = allocator.alloc(Layout::from_size_align_unchecked(length, 4096));
            if pointer.is_null() {
                return Err("no memory for the cached header".into());
            }
            slice::from_raw_parts_mut(pointer, length)
        };
        let mut state = InflateState::new_boxed(DataFormat::Raw);
        let result = inflate(&mut state, &compressed, buffer, MZFlush::Finish);
        if result.status.is_err() || result.bytes_written != length {
            let _ = cached.set_len(0);
            unsafe { allocator.dealloc(buffer.as_mut_ptr(), Layout::from_size_align_unchecked(length, 4096)) };
            return Err("the cached header is damaged; it is made again".into());
        }
        unsafe { holder.assign_bhd_contents(buffer.as_mut_ptr().cast()) };
        keep(manager, &mounts);
        return Ok(());
    }

    // not cached yet: the game decrypts it, and the result is kept
    let _ = cached.set_len(0);
    let (device, mounts) = mount_and_take(manager, mount, bhd_path)?;
    // mounted the game's own way: it stays, whatever happens to the cache
    keep(manager, &mounts);
    let header = unsafe { bhd_holder(device).as_ref() }
        .bhd_header()
        .ok_or("the mounted archive has no header")?;
    // mounted already: a failure to cache costs nothing more
    let written = cached
        .write_all(&header.file_size().to_le_bytes())
        .and_then(|_| cached.write_all(&compress_to_vec(header.as_slice(), 7)))
        .and_then(|_| cached.flush());
    if written.is_err() {
        let _ = std::fs::remove_file(&cached_path);
    }
    log::info!("BootBoost: cached {}", PathBuf::from(&disk_path).display());
    Ok(())
}
