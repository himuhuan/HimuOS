# Fix Report: Pool Oversize Object Unsigned Underflow

**Date:** 2026-03-31
**Severity:** High (merge blocker)
**Status:** Fixed & verified

## Summary

`KePoolInit()` and `KiPoolPrepareOnePage()` computed `(PAGE_4KB - headerSize) / slotSize` using unsigned arithmetic. When `objectSize` was large enough that the aligned header exceeded `PAGE_4KB` (e.g. `objectSize=8192`), the subtraction underflowed to a huge value, producing `slotsPerPage ≈ 0xFFFFFFFF`. The existing `SlotsPerPage == 0` guard never fired. `KiPoolPrepareOnePage()` would then iterate over that count, writing free-list nodes far beyond the backing page — a critical out-of-bounds write.

The documented contract (`docs/apis/KePool.md`) promises `EC_ILLEGAL_ARGUMENT` for objects that exceed single-page capacity. The bug broke that contract silently.

## Root Cause

Both sites used the pattern:

```c
uint32_t slots = (uint32_t)((PAGE_4KB - headerSize) / slotSize);
```

`PAGE_4KB` is `0x1000ULL` and `headerSize` is `size_t`. When `headerSize >= PAGE_4KB`, the unsigned subtraction wraps around instead of producing a negative value, and the truncation to `uint32_t` preserves a large positive count.

## Fix

### `src/kernel/ke/mm/pool.c` — `KePoolInit()` (validation path)

- Poison `pool->Magic = 0` at function entry so callers always see a deterministic non-alive state on early rejection.
- Guard `if (headerSize >= PAGE_4KB)` before the subtraction; return `EC_ILLEGAL_ARGUMENT`.

### `src/kernel/ke/mm/pool.c` — `KiPoolPrepareOnePage()` (defense-in-depth)

- Guard `if (headerSize >= PAGE_4KB)` before the subtraction; free the just-allocated backing page and return `EC_ILLEGAL_ARGUMENT` instead of panicking, keeping the allocator slow-path failure non-fatal.

### `src/kernel/demo/kthread_pool_race.c` — regression test

Added `KiRunOversizedObjectRegression()`:
- `objectSize = 8192` → verified `EC_ILLEGAL_ARGUMENT`, `Magic != KE_POOL_MAGIC_ALIVE`.
- `objectSize = 4096` → verified `EC_ILLEGAL_ARGUMENT` (exact page boundary; header pushes total beyond page).

## Verification

| Scenario | Result |
|---|---|
| Default kernel boot | Clean; KTHREAD pool initializes normally |
| `test-kthread_pool_race` suite | All 4 regressions pass (oversize, interleaving, ThreadId, create/reap) |
| Oversize rejection log | Both 8192 and 4096 rejected with `[POOL] slot size … exceeds page capacity` |

## Reviewer Notes

Reviewed by `reviewer` agent. Two findings addressed in final iteration:
1. **Medium (fixed):** `pool->Magic` was uninitialized on early-reject path; now poisoned at function entry.
2. **Low (fixed):** `KiPoolPrepareOnePage` defense-in-depth changed from `HO_KASSERT` (panic) to recoverable error return.
