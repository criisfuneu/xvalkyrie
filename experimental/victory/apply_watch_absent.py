#!/usr/bin/env python3
"""Backport VIctory PR #464 (unmapped view-block caching) to main ffe310e.

The original PR cherry-picks with a conflict in watch.rs because main gained
saturating range arithmetic. Preserve the main safety change while bringing
the PR's absent-block behavior, and fail rather than silently skip any patch.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])
path = root / "crates/victory-gpu/src/watch.rs"
s = path.read_text(encoding="utf-8")

def replace_one(before, after):
    global s
    count = s.count(before)
    if count != 1:
        raise SystemExit(f"watch backport: expected 1 source match, got {count}: {before[:80]!r}")
    s = s.replace(before, after, 1)

replace_one(
    "    clean: BTreeSet<usize>,\n}\n\nstatic GUARDS:",
    """    clean: BTreeSet<usize>,
    /// Unmapped placeholder blocks whose zeros were already copied, until
    /// the runtime next maps them and calls title_memory_remapped.
    absent: BTreeSet<usize>,
}

static GUARDS:""",
)
replace_one(
    "    clean: BTreeSet::new(),\n});\n\nfn guards()",
    "    clean: BTreeSet::new(),\n    absent: BTreeSet::new(),\n});\n\nfn guards()",
)
replace_one(
    "let open = block < last && !g.clean.contains(&block);",
    "let open = block < last && !g.clean.contains(&block) && !g.absent.contains(&block);",
)
replace_one(
    """        if set(block, block + 1) {
            note(g, block, block + 1);
        }
    }
}

/// Gives write access""",
    """        if set(block, block + 1) {
            note(g, block, block + 1);
        } else if unmapped(block * BLOCK) {
            g.absent.insert(block);
        }
    }
}

/// True only for an address reserved/unmapped, not for a read-only mapped
/// page. The runtime's title_memory_remapped notification invalidates it.
fn unmapped(at: usize) -> bool {
    use windows_sys::Win32::System::Memory::{MEM_COMMIT, MEMORY_BASIC_INFORMATION, VirtualQuery};
    // SAFETY: a local, correctly sized query buffer.
    let mut info: MEMORY_BASIC_INFORMATION = unsafe { std::mem::zeroed() };
    let n = unsafe { VirtualQuery(at as *const _, &mut info, size_of_val(&info)) };
    n != 0 && info.State != MEM_COMMIT
}

/// Gives write access""",
)
replace_one(
    """    let blocks: Vec<usize> = g
        .protected
        .range(at / BLOCK..at.saturating_add(len).div_ceil(BLOCK))
        .copied()
        .collect();""",
    """    // Main's saturating range arithmetic is retained (PR #464's older
    // version used at + len and could overflow at the address-space top).
    let range = at / BLOCK..at.saturating_add(len).div_ceil(BLOCK);
    let absent: Vec<usize> = g.absent.range(range.clone()).copied().collect();
    for block in absent {
        g.absent.remove(&block);
    }
    let blocks: Vec<usize> = g.protected.range(range).copied().collect();""",
)
# Keep the PR's behavior testable without touching a real game archive.
needle = """    /// A write or mapping that runs past the end of the address space (a"""
insert = """    /// PR #464: an unmapped placeholder is copied only once. Mapping it
    /// later invalidates that negative cache (no actual game files).
    #[test]
    fn absent_view_blocks_are_rechecked_after_mapping_notification() {
        let base = 0x7fff_0000_0000usize / BLOCK * BLOCK;
        let first = base / BLOCK;
        {
            let mut g = guards();
            g.absent.insert(first);
            g.absent.insert(first + 1);
            assert!(g.absent.contains(&first));
        }
        title_memory_remapped(base, BLOCK);
        {
            let mut g = guards();
            assert!(!g.absent.contains(&first));
            assert!(g.absent.contains(&(first + 1)));
            g.absent.remove(&(first + 1));
        }
    }

"""
replace_one(needle, insert+needle)
path.write_text(s, encoding="utf-8")
print("Applied VIctory PR #464 absent-view cache with main's saturating range safeguards")
