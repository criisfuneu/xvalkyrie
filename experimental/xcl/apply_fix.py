#!/usr/bin/env python3
"""Experimental fix for XCL loader. Never alters game binaries."""
from pathlib import Path
import argparse
import sys

CHANGES = {
    "crates/xcl/src/hle/mod.rs": [
        ('        "kernelx.dll",\n        "kernel32.dll",',
         '        "kernelx.dll",\n        "xmem.dll", // XMem exports provided by kernelx HLE.\n        "kernel32.dll",'),
        ('        assert_eq!(name("kernelx.dll"), Some(KERNELX));\n        assert_eq!(name("kernel32.dll"), Some(KERNELX));',
         '        assert_eq!(name("kernelx.dll"), Some(KERNELX));\n        assert_eq!(name("xmem.dll"), Some(KERNELX));\n        assert_eq!(name("kernel32.dll"), Some(KERNELX));'),
    ],
    "crates/xcl/src/loader.rs": [
        ('static READY: AtomicBool = AtomicBool::new(false);',
         'static READY: AtomicBool = AtomicBool::new(false);\nstatic FIXED_FFX_RESERVATION: Mutex<Option<usize>> = Mutex::new(None);'),
        ('fn section_protection(ch: u32) -> u32 {',
         """/// Reserve non-relocatable FidelityFX loader ImageBase before title CRT imports.
/// Never relocate a PE without its own relocation table.
pub fn reserve_fixed_ffx_loader() -> Result<(), String> {
    let Some(path) = resolver::find_game_dll("amd_fidelityfx_loader_xbox.dll") else {
        return Ok(());
    };
    let bytes = std::fs::read(&path).map_err(|e| format!("reading {}: {e}", path.display()))?;
    let pe = Pe::parse(&bytes, Layout::File)
        .map_err(|e| format!("{}: {e}", path.display()))?;
    if pe.has_relocs() {
        return Ok(());
    }
    let mut reserved = FIXED_FFX_RESERVATION.lock().unwrap_or_else(|e| e.into_inner());
    if reserved.is_some() {
        return Ok(());
    }
    let base = unsafe {
        VirtualAlloc(pe.image_base as *const c_void, pe.size_of_image as usize, MEM_RESERVE, PAGE_NOACCESS)
    } as usize;
    if base == 0 {
        return Err(format!("{}: fixed ImageBase {:#x} unavailable", path.display(), pe.image_base));
    }
    *reserved = Some(base);
    crate::xlog_info!("reserved fixed FidelityFX base {base:#x} before title imports");
    Ok(())
}

fn section_protection(ch: u32) -> u32 {"""),
        ('    let size = pe.size_of_image as usize;\n\n    // 1. reserve + commit',
         """    let size = pe.size_of_image as usize;

    // Replace temporary reservation with the actual DLL under the loader lock.
    if name == "amd_fidelityfx_loader_xbox.dll" {
        let reserved = FIXED_FFX_RESERVATION.lock().unwrap_or_else(|e| e.into_inner()).take();
        if let Some(addr) = reserved {
            if unsafe { VirtualFree(addr as *mut c_void, 0, MEM_RELEASE) } == 0 {
                return Err(format!("{name}: could not release reserved ImageBase {addr:#x}"));
            }
        }
    }

    // 1. reserve + commit"""),
    ],
    "crates/xcl/src/host.rs": [
        ('    HOST_MODE.store(true, Ordering::Release);\n\n    let main = match crate::loader::load_main(&boot.exe_path) {',
         """    HOST_MODE.store(true, Ordering::Release);

    // Reserve non-relocatable console DLL base before loading CRTs.
    if let Err(e) = crate::loader::reserve_fixed_ffx_loader() {
        crate::xlog_warn!("FidelityFX fixed-base reservation: {e}");
    }

    let main = match crate::loader::load_main(&boot.exe_path) {"""),
    ],
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    root = args.checkout.resolve()
    changes = {}
    try:
        for relative, substitutions in CHANGES.items():
            path = root / relative
            before = path.read_text(encoding="utf-8")
            after = before
            for old, new in substitutions:
                if new in after:
                    continue
                if after.count(old) != 1:
                    raise ValueError(f"unexpected source layout in {relative}: {old[:70]!r}")
                after = after.replace(old, new, 1)
            changes[path] = after
    except (OSError, ValueError) as e:
        print(f"Patch refused without changes: {e}", file=sys.stderr)
        return 1
    for path, contents in changes.items():
        path.write_text(contents, encoding="utf-8")
        print("Patched:", path.relative_to(root))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
