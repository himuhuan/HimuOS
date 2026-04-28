# Ex Object Manager Lite

Object Manager Lite is the Executive Lite ownership model for user-visible
kernel resources. It is intentionally smaller than an NT-style object manager:
there is no global namespace, no named objects, and no security descriptor
model. The teaching goal is to make object lifetime, handles, rights, and
teardown visible in one place.

## Model

- An object is a kernel allocation or embedded kernel resource with an
  `EX_OBJECT_HEADER`.
- A pointer is a kernel-only address used while the object is retained.
- A handle is a process-local `uint32_t` token that user code can pass back to
  Ex syscalls.

The object header stores the type, reference count, flags, and optional destroy
routine. The first Lite object types are process, thread, console/stdout, and
waitable. Future process-table work can add input, event, timer, and program
image objects without changing the handle contract.

## Handles

Each process owns one fixed-capacity handle table. A handle encodes:

- low 8 bits: one-based slot index
- high 24 bits: slot generation
- zero: invalid handle

Opening or seeding a handle retains the target object. Resolving a handle checks
slot bounds, generation, object type, and required rights, then returns a
retained object pointer to the caller. Closing a handle clears the slot,
advances the generation, and releases the handle's object reference. Reusing a
slot therefore rejects stale handles instead of accidentally targeting a newer
object.

## Rights

V1 rights are intentionally small:

- `QUERY`
- `CLOSE`
- `PROCESS_SELF`
- `THREAD_SELF`
- `WRITE`
- `WAIT`

Syscalls must request the minimum right they need. For example, `SYS_WRITE`
requires `WRITE` on a console object, while `SYS_WAIT_ONE` requires `WAIT` on a
waitable object.

## Teardown

Process teardown closes all process-owned handles before releasing the owning
process/thread references. Object release calls the object's destroy routine
when the reference count reaches zero. The current waitable object still wraps a
bootstrap companion-thread artifact; Object Manager Lite only centralizes its
ownership and cleanup until the later process/thread wait-object work replaces
that backing.
