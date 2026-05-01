# Ex User Sysinfo ABI

`src/include/kernel/ex/user_sysinfo_abi.h` is the stable kernel-to-user
sysinfo contract. Callers should prefer structured classes and treat text
classes as presentation helpers.

## Stable Data Classes

| Class | Structure | Source of truth |
| --- | --- | --- |
| `EX_SYSINFO_CLASS_OVERVIEW` | `EX_SYSINFO_OVERVIEW` | Ex aggregates bounded Ke mechanism snapshots. |
| `EX_SYSINFO_CLASS_PROCESS_LIST` | `EX_SYSINFO_PROCESS_LIST` | Ex runtime process table. |
| `EX_SYSINFO_CLASS_THREAD_LIST` | `EX_SYSINFO_THREAD_LIST` | Ex runtime thread table, plus the scheduler idle thread when available. |

`EX_SYSINFO_PROCESS_ENTRY.State` values are numeric
`EX_SYSINFO_PROCESS_STATE_*` enum values. User presentation code may render
names, but the enum value is the ABI.

## Presentation Helpers

The text classes remain convenience views:

| Class | Role |
| --- | --- |
| `EX_SYSINFO_CLASS_OVERVIEW_TEXT` | Human-readable rendering of `EX_SYSINFO_OVERVIEW`. |
| `EX_SYSINFO_CLASS_PROCESS_LIST_TEXT` | Human-readable rendering of `EX_SYSINFO_PROCESS_LIST`. |
| `EX_SYSINFO_CLASS_THREAD_LIST_TEXT` | Human-readable rendering of `EX_SYSINFO_THREAD_LIST`. |
| `EX_SYSINFO_CLASS_MEMMAP_TEXT` | Human-readable memory-map view over Ke VMM/KVA snapshots and current user layout. |

`hsh ps` consumes `EX_SYSINFO_CLASS_PROCESS_LIST` directly and formats the
view in user space. `hsh sysinfo` and `hsh memmap` still use text helpers to
fit the one-page shell image budget; the stable data contract is still the
structured overview/process/thread classes above.
