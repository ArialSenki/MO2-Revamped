//! me3's `mem_patch` for ELDEN RING, ported from me3 (MIT OR Apache-2.0):
//! crates/mod-host/src/alloc_hooks.rs and alloc_hooks/mimalloc.rs as of its
//! "Improve mem_patch stability" and later changes (one pre-committed arena,
//! no decommitting, fallback to the global heap), and the DlAllocator layout
//! from crates/mod-host-types/src/alloc.rs. The game's fixed-size heaps are
//! replaced by mimalloc. Without it Reforged's larger sound bank runs the
//! sound heap out of room, and music and voices stop for a moment on each
//! dodge or hit.

use std::{ffi::c_void, mem::ManuallyDrop, ptr::NonNull, slice, sync::{LazyLock, Mutex}};

use libmimalloc_sys::{
    mi_arena_id_t, mi_free, mi_heap_malloc_aligned, mi_heap_new_in_arena, mi_heap_realloc_aligned,
    mi_heap_t, mi_malloc_aligned, mi_manage_os_memory_ex, mi_option_set, mi_realloc_aligned,
    mi_usable_size,
};
use me3_binary_analysis::{pe, rtti};
use pelite::pe64::{Pe, PeObject, PeView};
use regex::bytes::Regex;
use retour::RawDetour;

#[repr(C)]
pub struct DlAllocator {
    vtable: NonNull<DlAllocatorVtable>,
}

unsafe impl Send for DlAllocator {}
unsafe impl Sync for DlAllocator {}

#[repr(u32)]
#[allow(dead_code)]
#[derive(Clone, Copy)]
enum DlHeapDirection {
    Front = 0,
    Back = 1,
}

#[repr(C)]
struct DlAllocatorVtable {
    dtor: unsafe extern "C" fn(NonNull<ManuallyDrop<DlAllocator>>),
    heap_id: unsafe extern "C" fn(NonNull<DlAllocator>) -> u32,
    allocator_id: unsafe extern "C" fn(NonNull<DlAllocator>) -> u32,
    capability: unsafe extern "C" fn(NonNull<DlAllocator>, NonNull<u32>, DlHeapDirection) -> NonNull<u32>,
    total_size: unsafe extern "C" fn(NonNull<DlAllocator>) -> usize,
    free_size: unsafe extern "C" fn(NonNull<DlAllocator>) -> usize,
    max_size: unsafe extern "C" fn(NonNull<DlAllocator>) -> usize,
    num_blocks: unsafe extern "C" fn(NonNull<DlAllocator>) -> usize,
    block_size: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8) -> usize,
    allocate: unsafe extern "C" fn(NonNull<DlAllocator>, usize) -> *mut u8,
    allocate_aligned: unsafe extern "C" fn(NonNull<DlAllocator>, usize, usize) -> *mut u8,
    reallocate: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8, usize) -> *mut u8,
    reallocate_aligned: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8, usize, usize) -> *mut u8,
    free: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8),
    free_all: unsafe extern "C" fn(NonNull<DlAllocator>),
    back_allocate: unsafe extern "C" fn(NonNull<DlAllocator>, usize) -> *mut u8,
    back_allocate_aligned: unsafe extern "C" fn(NonNull<DlAllocator>, usize, usize) -> *mut u8,
    back_reallocate: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8, usize) -> *mut u8,
    back_reallocate_aligned: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8, usize, usize) -> *mut u8,
    back_free: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8),
    self_diagnose: unsafe extern "C" fn(NonNull<DlAllocator>) -> bool,
    is_valid_block: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8) -> bool,
    lock: unsafe extern "C" fn(NonNull<DlAllocator>),
    unlock: unsafe extern "C" fn(NonNull<DlAllocator>),
    block_of: unsafe extern "C" fn(NonNull<DlAllocator>, *mut u8) -> *mut u8,
}

/// The allocator that takes the game's place, for code that must recognize
/// it (the device manager search in boot_boost.rs).
pub fn main_allocator() -> *const () {
    (&raw const MIMALLOC_DLALLOC).cast()
}

pub static MIMALLOC_DLALLOC: DlAllocator = DlAllocator {
    vtable: NonNull::from_ref(&MIMALLOC_DLALLOC_VTABLE),
};

const MIMALLOC_DLALLOC_VTABLE: DlAllocatorVtable = DlAllocatorVtable {
    dtor,
    heap_id,
    allocator_id,
    capability,
    total_size,
    free_size,
    max_size,
    num_blocks,
    block_size,
    allocate,
    allocate_aligned,
    reallocate,
    reallocate_aligned,
    free,
    free_all,
    back_allocate,
    back_allocate_aligned,
    back_reallocate,
    back_reallocate_aligned,
    back_free,
    self_diagnose,
    is_valid_block,
    lock,
    unlock,
    block_of,
};

unsafe extern "C" fn dtor(_: NonNull<ManuallyDrop<DlAllocator>>) {}
unsafe extern "C" fn heap_id(_: NonNull<DlAllocator>) -> u32 {
    0x401
}
unsafe extern "C" fn allocator_id(_: NonNull<DlAllocator>) -> u32 {
    0xffffffff
}
unsafe extern "C" fn capability(
    _: NonNull<DlAllocator>,
    out: NonNull<u32>,
    _: DlHeapDirection,
) -> NonNull<u32> {
    unsafe { out.write(0x7b) };
    out
}
unsafe extern "C" fn total_size(_: NonNull<DlAllocator>) -> usize {
    usize::MAX
}
unsafe extern "C" fn free_size(_: NonNull<DlAllocator>) -> usize {
    usize::MAX
}
unsafe extern "C" fn max_size(_: NonNull<DlAllocator>) -> usize {
    usize::MAX
}
unsafe extern "C" fn num_blocks(_: NonNull<DlAllocator>) -> usize {
    0
}
unsafe extern "C" fn block_size(_: NonNull<DlAllocator>, block: *mut u8) -> usize {
    if block.is_null() {
        0
    } else {
        unsafe { mi_usable_size(block as _) }
    }
}
/// ELDEN RING's heap size in me3: 12 GB, committed up front.
const HEAP_SIZE: usize = 12 * 1024 * 1024 * 1024;

/// A contiguous, pre-committed heap used in place of mimalloc's default heap,
/// as me3 does, to reduce crashes from the game's bad memory accesses; made the
/// first time the game allocates through it. Null if it could not be made: the
/// global heap is used then.
struct Heap(*mut mi_heap_t);
unsafe impl Send for Heap {}
unsafe impl Sync for Heap {}

static MI_HEAP: LazyLock<Heap> = LazyLock::new(|| unsafe {
    extern "system" {
        fn VirtualAlloc(address: *mut c_void, size: usize, kind: u32, protect: u32) -> *mut c_void;
    }
    const MEM_COMMIT: u32 = 0x1000;
    const MEM_RESERVE: u32 = 0x2000;
    const MEM_TOP_DOWN: u32 = 0x10_0000;
    const PAGE_READWRITE: u32 = 0x04;
    // keep purged pages committed: the game reads past some blocks' ends, and
    // a decommitted page there is a crash
    const MI_OPTION_PURGE_DECOMMITS: i32 = 5;
    mi_option_set(MI_OPTION_PURGE_DECOMMITS as _, 0);

    let memory = VirtualAlloc(
        std::ptr::null_mut(),
        HEAP_SIZE,
        MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN,
        PAGE_READWRITE,
    );
    if memory.is_null() {
        log::error!("mem_patch: the 12 GB arena could not be committed; mimalloc's own heap is used");
        return Heap(std::ptr::null_mut());
    }
    let mut arena_id: mi_arena_id_t = std::mem::zeroed();
    if !mi_manage_os_memory_ex(memory, HEAP_SIZE, true, false, false, -1, true, &mut arena_id) {
        log::error!("mem_patch: mimalloc did not take the arena; its own heap is used");
        return Heap(std::ptr::null_mut());
    }
    Heap(mi_heap_new_in_arena(arena_id))
});

unsafe extern "C" fn allocate(this: NonNull<DlAllocator>, size: usize) -> *mut u8 {
    unsafe { allocate_aligned(this, size, 16) }
}
unsafe extern "C" fn allocate_aligned(_: NonNull<DlAllocator>, size: usize, alignment: usize) -> *mut u8 {
    let alignment = alignment.max(16);
    let size = size.next_multiple_of(alignment);
    let heap = MI_HEAP.0;
    let mut ptr = if heap.is_null() {
        std::ptr::null_mut()
    } else {
        unsafe { mi_heap_malloc_aligned(heap, size, alignment) }
    };
    if ptr.is_null() {
        ptr = unsafe { mi_malloc_aligned(size, alignment) };
    }
    ptr as _
}
unsafe extern "C" fn reallocate(this: NonNull<DlAllocator>, old: *mut u8, new_size: usize) -> *mut u8 {
    unsafe { reallocate_aligned(this, old, new_size, 16) }
}
unsafe extern "C" fn reallocate_aligned(
    _: NonNull<DlAllocator>,
    old: *mut u8,
    new_size: usize,
    alignment: usize,
) -> *mut u8 {
    let alignment = alignment.max(16);
    let size = new_size.next_multiple_of(alignment);
    let heap = MI_HEAP.0;
    if heap.is_null() {
        unsafe { mi_realloc_aligned(old as _, size, alignment) as _ }
    } else {
        unsafe { mi_heap_realloc_aligned(heap, old as _, size, alignment) as _ }
    }
}
unsafe extern "C" fn free(_: NonNull<DlAllocator>, ptr: *mut u8) {
    unsafe { mi_free(ptr as _) }
}
unsafe extern "C" fn free_all(_: NonNull<DlAllocator>) {}
unsafe extern "C" fn back_allocate(this: NonNull<DlAllocator>, size: usize) -> *mut u8 {
    unsafe { allocate(this, size) }
}
unsafe extern "C" fn back_allocate_aligned(this: NonNull<DlAllocator>, size: usize, alignment: usize) -> *mut u8 {
    unsafe { allocate_aligned(this, size, alignment) }
}
unsafe extern "C" fn back_reallocate(this: NonNull<DlAllocator>, old: *mut u8, new_size: usize) -> *mut u8 {
    unsafe { reallocate(this, old, new_size) }
}
unsafe extern "C" fn back_reallocate_aligned(
    this: NonNull<DlAllocator>,
    old: *mut u8,
    new_size: usize,
    alignment: usize,
) -> *mut u8 {
    unsafe { reallocate_aligned(this, old, new_size, alignment) }
}
unsafe extern "C" fn back_free(this: NonNull<DlAllocator>, ptr: *mut u8) {
    unsafe { free(this, ptr) }
}
unsafe extern "C" fn self_diagnose(_: NonNull<DlAllocator>) -> bool {
    false
}
unsafe extern "C" fn is_valid_block(_: NonNull<DlAllocator>, _: *mut u8) -> bool {
    true
}
unsafe extern "C" fn lock(_: NonNull<DlAllocator>) {}
unsafe extern "C" fn unlock(_: NonNull<DlAllocator>) {}
unsafe extern "C" fn block_of(_: NonNull<DlAllocator>, _: *mut u8) -> *mut u8 {
    std::ptr::null_mut()
}

#[repr(C)]
struct CSMemoryVtable {
    drop: unsafe extern "C" fn(NonNull<()>),
    init: unsafe extern "C" fn(NonNull<()>),
    deinit: unsafe extern "C" fn(NonNull<()>),
}

/// The detours stay installed for the whole process.
static DETOURS: Mutex<Vec<RawDetour>> = Mutex::new(Vec::new());

/// Keeps an enabled detour for good (dropping a `RawDetour` disables it).
fn keep_detour(hook: RawDetour) {
    // a poisoned lock still holds the detours: they must not be dropped
    DETOURS.lock().unwrap_or_else(|poisoned| poisoned.into_inner()).push(hook);
}

/// A detour made but not enabled yet: the target is still unchanged.
unsafe fn new_detour(target: *const (), replacement: *const ()) -> Result<RawDetour, String> {
    unsafe { RawDetour::new(target, replacement) }.map_err(|e| e.to_string())
}

unsafe fn detour(target: *const (), replacement: *const ()) -> Result<(), String> {
    let hook = unsafe { new_detour(target, replacement) }?;
    unsafe { hook.enable() }.map_err(|e| e.to_string())?;
    keep_detour(hook);
    Ok(())
}

/// The first group of the one match of `re`: an unknown game version can
/// match elsewhere too, and a wrong match would be patched before the game's
/// entry point.
fn unique_capture<'h>(re: &Regex, haystack: &'h [u8], what: &str) -> Result<&'h [u8], String> {
    let mut matches = re.captures_iter(haystack);
    let first = matches
        .next()
        .ok_or_else(|| format!("{what} pattern returned no matches"))?;
    if matches.next().is_some() {
        return Err(format!(
            "{what} pattern matched more than once; this game version is not recognized"
        ));
    }
    first
        .get(1)
        .map(|group| group.as_bytes())
        .ok_or_else(|| format!("{what} pattern has no address"))
}

pub(crate) fn game() -> PeView<'static> {
    extern "system" {
        fn GetModuleHandleW(name: *const u16) -> *const u8;
    }
    unsafe { PeView::module(GetModuleHandleW(std::ptr::null())) }
}

fn rip_target(disp32: &[u8]) -> *const u8 {
    let displacement = i32::from_le_bytes(disp32.try_into().unwrap());
    unsafe { disp32.as_ptr_range().end.byte_offset(displacement as isize) }
}

/// Before the game's main: the game asks one function for its system
/// allocator; it answers mimalloc from now on.
pub fn hook_system_allocator() -> Result<(), String> {
    let exe = game();
    let re = Regex::new(
        r"(?s-u)\xe8.{4}\x48\x8b\x74\x24\x30\x48\x8b\x5c\x24\x38\x48\x83\xc4\x20\x5f\xe9(.{4})",
    )
    .unwrap();
    let text = pe::section(exe, ".text").map_err(|_| ".text section is missing")?;
    let text = exe.get_section_bytes(text).map_err(|e| e.to_string())?;
    let get_system_allocator = rip_target(unique_capture(&re, text, "system allocator")?);

    extern "C" fn get_allocator_override() -> NonNull<DlAllocator> {
        NonNull::from_ref(&MIMALLOC_DLALLOC)
    }
    unsafe { detour(get_system_allocator as _, get_allocator_override as _) }
}

/// After the game's main has set up its heaps (me3 runs this once
/// SteamAPI_Init returns): every heap the game made answers mimalloc.
pub fn hook_heap_allocators() -> Result<(), String> {
    let exe = game();
    let class_map = rtti::classes(exe).map_err(|e| e.to_string())?;

    let vtable: &CSMemoryVtable = match class_map
        .get("CS::CSMemoryImp")
        .and_then(|vtables| vtables.first())
    {
        Some(vtable) => unsafe { vtable.as_ref::<CSMemoryVtable>() },
        None => {
            let re = Regex::new(
                r"(?s-u)\xe8.{4}\x90\x48\x8d\x05(.{4})\x48\x89\x03\xc6\x83.\x02\x00\x00\x00\x48\x8b\xc3\x48\x83\xc4.\x5b\xc3",
            )
            .unwrap();
            let address = unique_capture(&re, exe.image(), "CSMemoryImp")?;
            unsafe { &*(rip_target(address) as *const CSMemoryVtable) }
        }
    };

    // found before anything is changed, so a bad match leaves the game as it was
    let table = allocator_table()?;

    // ELDEN RING: CSGraphicsImp's first virtual function does nothing
    let graphics = class_map
        .get("CS::CSGraphicsImp")
        .and_then(|vtables| vtables.first())
        .ok_or("CSGraphicsImp vtable not found")?;
    let graphics_fn = unsafe { *graphics.as_ref::<unsafe extern "C" fn(NonNull<()>)>() };
    extern "C" fn nothing(_: NonNull<()>) {}

    // Every detour is made before any is enabled, and all are enabled before
    // the table changes: a detour that can't be made or enabled then leaves
    // the game as it was, instead of mimalloc in the table with the game's
    // own CSMemoryImp::init still live to rebuild its heaps.
    let graphics_hook = unsafe { new_detour(graphics_fn as _, nothing as _) }?;
    let init_hook = unsafe { new_detour(vtable.init as _, nothing as _) }?;
    // optional, as in me3
    let deinit_hook = unsafe { new_detour(vtable.deinit as _, nothing as _) }.ok();
    let required = [graphics_hook, init_hook];
    for (index, hook) in required.iter().enumerate() {
        if let Err(error) = unsafe { hook.enable() } {
            for enabled in &required[..index] {
                let _ = unsafe { enabled.disable() };
            }
            return Err(format!("a heap detour could not be enabled: {error}"));
        }
    }
    let deinit_hook = deinit_hook.filter(|hook| unsafe { hook.enable() }.is_ok());
    if deinit_hook.is_none() {
        log::warn!("mem_patch: CSMemoryImp::deinit left as it is");
    }

    log::info!("mem_patch: {} heap allocators replaced", table.len());
    table.fill(Some(NonNull::from_ref(&MIMALLOC_DLALLOC)));

    for hook in required.into_iter().chain(deinit_hook) {
        keep_detour(hook);
    }
    Ok(())
}

fn allocator_table() -> Result<&'static mut [Option<NonNull<DlAllocator>>], String> {
    let exe = game();
    let first_re =
        Regex::new(r"(?s-u)\x48\x89\x05(.{4})\x4c\x8b\xc0\xba\x08\x00\x00\x00\x8d\x4a\x08").unwrap();
    let first_ptr = rip_target(unique_capture(&first_re, exe.image(), "first allocator")?)
        as *mut Option<NonNull<DlAllocator>>;

    let last_re = Regex::new(
        r"(?s-u)\x48\x89\x3d(.{4})\xc7\x44\x24\x20\xff\xff\xff\xff\x45\x33\xc9\x4c\x8b\xc7\x48\x8d\x15.{4}",
    )
    .unwrap();
    let mut last_candidates = last_re
        .captures_iter(exe.image())
        .map(|c| rip_target(c.get(1).unwrap().as_bytes()) as *mut Option<NonNull<DlAllocator>>)
        .collect::<Vec<_>>();
    last_candidates.sort();
    let last_ptr = *last_candidates.last().ok_or("last allocator pattern returned no matches")?;
    if first_ptr > last_ptr {
        return Err("malformed allocator range".into());
    }
    // a stray match far away would make the table cover unrelated memory;
    // the game's own table has 21 entries in every version from 1.17 to 1.17.2
    const MAX_ALLOCATORS: usize = 256;
    let count = unsafe { last_ptr.offset_from_unsigned(first_ptr) } + 1;
    if count > MAX_ALLOCATORS {
        return Err(format!("allocator range of {count} entries is not the game's table"));
    }
    unsafe {
        Ok(slice::from_raw_parts_mut(
            first_ptr,
            last_ptr.offset_from_unsigned(first_ptr) + 1,
        ))
    }
}

/// Runs `after` once SteamAPI_Init has returned true, which me3 uses as "after
/// the game's main".
pub fn after_steam_init(after: fn()) -> Result<(), String> {
    use std::sync::OnceLock;
    static AFTER: OnceLock<fn()> = OnceLock::new();
    static TRAMPOLINE: OnceLock<usize> = OnceLock::new();
    extern "system" {
        fn LoadLibraryW(name: *const u16) -> *const u8;
        fn GetProcAddress(module: *const u8, name: *const u8) -> *const ();
    }
    let _ = AFTER.set(after);
    let name: Vec<u16> = "steam_api64.dll".encode_utf16().chain([0]).collect();
    let module = unsafe { LoadLibraryW(name.as_ptr()) };
    if module.is_null() {
        return Err("steam_api64.dll could not be loaded".into());
    }
    let init = unsafe { GetProcAddress(module, c"SteamAPI_Init".as_ptr().cast()) };
    if init.is_null() {
        return Err("SteamAPI_Init not found".into());
    }
    extern "C" fn steam_init_override() -> bool {
        let original: extern "C" fn() -> bool =
            unsafe { std::mem::transmute(*TRAMPOLINE.get().unwrap()) };
        let result = original();
        static RAN: std::sync::Once = std::sync::Once::new();
        if result {
            // a panic must not cross into the game: it would end the process
            RAN.call_once(|| {
                if std::panic::catch_unwind(|| (AFTER.get().unwrap())()).is_err() {
                    log::error!("mem_patch: the heap allocators' hook panicked");
                }
            });
        }
        result
    }
    let hook = unsafe { RawDetour::new(init, steam_init_override as *const ()) }.map_err(|e| e.to_string())?;
    let _ = TRAMPOLINE.set(hook.trampoline() as *const () as usize);
    unsafe { hook.enable() }.map_err(|e| e.to_string())?;
    keep_detour(hook);
    Ok(())
}
