"""Apply upstream PR #1321's 128:1 structured-HLSL bound to PR1316's refactored translate().

PR #1316 refactored translate() around prepare() and conflicts with #1321's
unrefactored translate() hunk. Cherry-pick PR #1321 for its regression tests,
keep PR #1316's translation function and insert the guard semantics exactly.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: apply_structured_guard.py <xcl source directory>')
path = Path(sys.argv[1]) / 'crates/xcl/src/gpu/recomp/mod.rs'
s = path.read_text('utf8')
if '<<<<<<<' in s or '=======' in s or '>>>>>>>' in s:
    raise RuntimeError('Source has unresolved merge conflicts. Restore PR1316 mod.rs first.')

def one(before: str, after: str):
    global s
    n = s.count(before)
    if n != 1:
        raise RuntimeError(f'Unsafe patch: expected 1 occurrence, got {n}: {before[:80]!r}')
    s = s.replace(before, after, 1)

guard_decl = '''/// When the structured HLSL text grows unreasonably relative to the original
/// console program (nested forward-branch flags), use the bounded pc-blocks form.
/// Ported from upstream PR #1321, whose 148 MB shader caused memory growth.
const STRUCTURED_BYTES_PER_CODE_BYTE: usize = 128;

'''
one(
    'const LOOP_GUARD: &str = "if (xcl_budget == 0u) break; xcl_budget--;";',
    'const LOOP_GUARD: &str = "if (xcl_budget == 0u) break; xcl_budget--;";\n\n' + guard_decl.rstrip()
)
one(
    'pub fn translate(code: &[u8], opt: &Options) -> Result<Translation, String> {\n    let Prepared',
    'pub fn translate(code: &[u8], opt: &Options) -> Result<Translation, String> {\n    let program = code;\n    let Prepared'
)
one(
    'emit_structured(&sctx, &mut em, &mut code, 0, u64::MAX, None, 0, &Conts::default(), &BTreeSet::new(), 1, &mut blocks, &mut flags);',
    '''emit_structured(&sctx, &mut em, &mut code, 0, u64::MAX, None, 0, &Conts::default(), &BTreeSet::new(), 1, &mut blocks, &mut flags);
            // PR #1321: prevent 148 MB HLSL for one nested-jump shader.
            if code.len() > STRUCTURED_BYTES_PER_CODE_BYTE.saturating_mul(program.len()) {
                return translate(program, &Options { structured: false, ..opt.clone() });
            }'''
)
path.write_text(s, encoding='utf8')
print('PR1321 guard applied to refactored PR1316 translate(), retaining both backends.')
