"""Port PR #628's graphics-first work queue to the newer XCL PSO worker implementation.

The original PR #628 targets an older pso_async.rs and calls complete directly.
Newer main uses build(&p, job, false) to preserve demand priority, worker tracking,
and watchdog holding.  Never replace that behavior with the old PR's complete().
This change only prioritizes queued graphics jobs over queued compute jobs.
"""
from pathlib import Path
import sys

assert len(sys.argv) == 2, "Usage: apply_graphics_priority.py <xcl_source_directory>"
p = Path(sys.argv[1]) / 'crates/xcl/src/hle/d3d12x/pso_async.rs'
source = p.read_text(encoding='utf-8')

def replace_once(old: str, new: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f'Expected exactly one occurrence, found {count}: {old[:85]!r}')
    source = source.replace(old, new, 1)

replace_once(
    'use std::collections::HashMap;',
    'use std::collections::{HashMap, VecDeque};',
)
replace_once('use std::sync::{mpsc, Arc, Condvar, Mutex, OnceLock};',
             'use std::sync::{Arc, Condvar, Mutex, OnceLock};\n#[cfg(test)]\nuse std::sync::mpsc;')
start_token = "fn queue() -> &'static Mutex<mpsc::Sender<Arc<Pending>>> {"
end_token = "\n/// Release a pipeline state XCL owns (raw pointer, 0 = none)."
if source.count(start_token) != 1 or source.count(end_token) != 1:
    raise RuntimeError('Cannot safely find the original queue implementation.')
start = source.index(start_token)
end = source.index(end_token, start)
old_queue = source[start:end]
assert 'build(&p, job, false);' in old_queue, 'Missing current async build logic: refusing old PR merge'
new_queue = """// Ported from upstream PR #628. Per-lane FIFO order is preserved, with gfx
// pipelines taken before speculative compute jobs. The existing build() path
// (demand tracking, watchdog hold, and worker priority) is unchanged.
struct GraphicsFirstQueue<T> {
    gfx: VecDeque<T>,
    compute: VecDeque<T>,
}

impl<T> GraphicsFirstQueue<T> {
    fn new() -> Self {
        Self { gfx: VecDeque::new(), compute: VecDeque::new() }
    }
    fn push(&mut self, compute: bool, item: T) {
        if compute { self.compute.push_back(item); }
        else { self.gfx.push_back(item); }
    }
    fn pop_next(&mut self) -> Option<T> {
        self.gfx.pop_front().or_else(|| self.compute.pop_front())
    }
}

struct GraphicsFirstPool {
    q: Mutex<GraphicsFirstQueue<Arc<Pending>>>,
    cv: Condvar,
}

fn graphics_first_pool() -> &'static Arc<GraphicsFirstPool> {
    static Q: OnceLock<Arc<GraphicsFirstPool>> = OnceLock::new();
    Q.get_or_init(|| {
        let p = Arc::new(GraphicsFirstPool {
            q: Mutex::new(GraphicsFirstQueue::new()),
            cv: Condvar::new(),
        });
        let n = crate::gpu::recomp::dxc::compile_threads();
        for k in 0..n {
            let pool = p.clone();
            let _ = std::thread::Builder::new()
                .name(format!("xcl-pso-{k}"))
                .stack_size(64 << 20)
                .spawn(move || loop {
                    let next = {
                        let mut q = pool.q.lock().unwrap();
                        loop {
                            if let Some(item) = q.pop_next() {
                                break item;
                            }
                            q = pool.cv.wait(q).unwrap();
                        }
                    };
                    let job = next.job.lock().unwrap().take();
                    if let Some(job) = job {
                        // CRITICAL: keep the new main's demand/wanted tracking.
                        build(&next, job, false);
                    }
                });
        }
        p
    })
}

fn enqueue_priority(p: Arc<Pending>) {
    let pool = graphics_first_pool();
    let mut q = pool.q.lock().unwrap();
    q.push(p.compute, p);
    pool.cv.notify_one();
}

"""
source = source[:start] + new_queue + source[end:]
needle = 'let _ = queue().lock().unwrap().send(p);'
count = source.count(needle)
if count != 2:
    raise RuntimeError(f'Expected exactly 2 async enqueue sites, got {count}')
source = source.replace(needle, 'enqueue_priority(p);')
# Newer upstream also directly enqueues a cloned pipeline in a unit-test helper
# near the end of the module. Preserve the same queue behavior there.
test_send = 'let _ = queue().lock().unwrap().send(p.clone());'
if source.count(test_send) != 1:
    raise RuntimeError(f'Expected one extra test enqueue, got {source.count(test_send)}')
source = source.replace(test_send, 'enqueue_priority(p.clone());')

# A demand bind can also re-queue a previously-preloaded pipeline using a clone.
replace_once('let _ = queue().lock().unwrap().send(p.clone());', 'enqueue_priority(p.clone());')

test = """
#[cfg(test)]
mod graphics_first_queue_tests {
    use super::GraphicsFirstQueue;

    #[test]
    fn graphics_pipelines_come_first_without_reordering_within_a_lane() {
        let mut q = GraphicsFirstQueue::new();
        q.push(true, "c1");
        q.push(true, "c2");
        q.push(false, "g1");
        q.push(true, "c3");
        q.push(false, "g2");
        assert_eq!(q.pop_next(), Some("g1"));
        assert_eq!(q.pop_next(), Some("g2"));
        assert_eq!(q.pop_next(), Some("c1"));
        assert_eq!(q.pop_next(), Some("c2"));
        assert_eq!(q.pop_next(), Some("c3"));
        assert_eq!(q.pop_next(), None);
    }
}
"""
if "mod graphics_first_queue_tests" in source:
    raise RuntimeError('The graphics queue patch was already applied')
p.write_text(source + test, encoding='utf-8')
print('Applied minimal graphics-first queue to newer PSO implementation; build(), HELD, and demand tracking preserved.')
