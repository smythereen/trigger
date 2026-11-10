# trigger — file & directory trigger unit

Copyright (c) 2026 John R. Smythe Jr.

The trigger unit watches files and directory trees with Linux inotify. When something changes, it either
fork & execs a program with the path that changed (the `trigger` server), or calls a subroutine
(`Trig_Fork_Watch()` / `Trig_Poll()` for far and the other units).

It builds with the [base](https://github.com/smythereen/base) unit make system, which handles C, C++, and Rust, including large mixed-language projects.

## Files

| File | What it is |
|---|---|
| `trigger.h` | Public header (copied to `~/cmn`): `.trigmem` layout, flags, control words, trace bits, API |
| `triglocal.h` | Private watcher structures |
| `trigwatch.c` | `Trig_Open/Fd/Add/Poll/Watches/Close`, the inotify engine (recursive, debounce, rescan, retry) |
| `trigfork.c` | `Trig_Fork_Watch()` and `Trig_Stop_Watch()`, a forked watcher that calls your subroutine |
| `trigevt.c` | `Trig_Event_Mask()` and `Trig_Event_Names()`, event names to and from masks |
| `trigmemf.c` | `Trig_Mem_Create/Map/Grow/Unmap`, `Trig_Time`, `Trig_Mono` |
| `trigger.c` | Server program: `trigger <.trigmem> [trace_file]` |
| `trigmem.c` | Tool that creates, edits, lists, traces and controls a `.trigmem` file |
| `trig_dir.c` | Tool: `trig_dir <directory> <cmd>` runs `cmd <file>` in a login bash for each file that appears in the directory (inotify); built to `~/bin/trig_dir` (moved here from far) |
| `tests/trigtest.sh` | Unit tests (`make test`) |
| `tests/trigcb.c` | Library callback test program (built by the test script) |

`make` builds `~/lib/libtrigger.a`, `~/cmn/trigger.h`, and `~/bin/trigger` and `~/bin/trigmem`. The library
links with `-lsys` only. It uses its own `mmap()`, not `Map()`, so programs don't also need map, sem and msg.

## The .trigmem file

The file is a header followed by `max` entry slots. Everything uses `common.h` sizes, and the layout is
the same on every 64-bit machine. The header records `hdr_size` and `ent_size`, and `Trig_Mem_Map()`
rejects a file whose sizes, magic or version don't match.

### Header, `trig_hdr_t` (128 bytes)

| Field | Type | Meaning |
|---|---|---|
| `magic` | u32 | `0x47495254`, which reads "TRIG" in the file |
| `version` | u32 | `TRIG_VERSION` (1) |
| `hdr_size`, `ent_size` | u32 | 128 and 1088 |
| **`TraceLevel`** | u32 | The server's live trace level. The server sets `TracePointer = &hdr->TraceLevel` |
| `control` | u32 | A `TRIG_CTL_*` command. The server swaps it back to 0 and acts on it |
| `state` | u32 | `TRIG_ST_STOPPED` (0), `RUNNING` (1) or `PAUSED` (2) |
| `generation` | u32 | Goes up by one on every (re)load |
| `count`, `max` | u32 | Highest used slot + 1, and the number of slots in the file |
| `debounce_ms` | u32 | Default quiet time, used by entries whose own value is 0 |
| `overflows` | u32 | How many times `IN_Q_OVERFLOW` was seen |
| `pid` | s32 | The server's pid, or 0 when it's stopped (the server won't start twice) |
| `started`, `heartbeat` | u64 | usec since the epoch. `heartbeat` is updated on each loop pass (about 250 ms) |
| `events`, `spawns` | u64 | Totals |

### Entry, `trig_ent_t` (1088 bytes, one per file or directory)

| Field | Type | Who writes it | Meaning |
|---|---|---|---|
| `flags` | u32 | writer and server | `TRIG_F_USED 0x1`, `RECURSIVE 0x2`, `SHELL 0x4`, `DISABLED 0x8`. The server also sets `GONE 0x10000` and `RUNNING 0x20000` |
| `events` | u32 | writer | The inotify `IN_*` mask to trigger on |
| `debounce_ms` | u32 | writer | Quiet time per changed path (0 means use the header default) |
| `nwatch` | u32 | server | How many inotify watches the entry holds |
| `last_pid`, `last_status` | s32 | server | The last child, and its raw `wait()` status (-1 while it's running) |
| `last_mask`, `errors` | u32 | server | The last event mask, and the count of fork, exec and watch errors |
| `fired`, `last_fired`, `spawns` | u64 | server | Trigger count, the last trigger time (usec) and the number of programs started |
| `path[256]` | char | writer | The file or directory to watch |
| `run[512]` | char | writer | The program and its arguments |
| `last_path[256]` | char | server | The last path that changed |

## Server: `trigger <.trigmem> [trace_file]`

* **Startup:** sets `LocalName = av[0]`, calls `Initialize_Print("trigger.trc")` (or the trace_file you give
  it, using `$FILE_PATH` for relative names), sets `TracePointer = &hdr->TraceLevel`, then writes the pid
  and state into the header.
* **Main loop:** calls `New_Err_File()`, reaps children, checks the control word and updates the heartbeat,
  then calls `Trig_Poll(250 ms)`.
* **Running the program:** forks, then execs.
  * With the `s` flag, it runs `$SHELL -c "<run>" trigger <path>`, so the path is `$1`. Write
    `script "$1"` to hand it to a script.
  * Without `s`, it splits `run` on blanks and execs it directly with the changed path as the last argument.
  * The child also gets `TRIG_PATH`, `TRIG_EVENTS` (names), `TRIG_MASK`, `TRIG_ENTRY` and `TRIG_WATCH` in its
    environment.
* **Reaping:** `SIGCHLD` sets a flag, and `waitpid(WNOHANG)` stores each child's status in the entry whose
  `last_pid` matches. Exit 127 (exec failed) counts as an error.
* **Control word:** `RELOAD 1` remaps the file (it may have grown) and rebuilds every watch. `PAUSE 2` keeps
  watching and counting but starts no programs. `RESUME 3` starts them again. `STOP 4` exits cleanly with
  state STOPPED and pid 0. `SIGHUP` reloads; `SIGTERM` and `SIGINT` stop.
* **Other rules:** an event fires an entry only when `mask & entry.events` is non-zero. `IN_Q_OVERFLOW`
  always fires each entry, with its root path, so the program can rescan.

## Library API (`#include <trigger.h>`, `-ltrigger -lsys`)

```c
typedef void (*trig_cb_t)( s32 tag, char * path, u32 mask, void * arg );

trig_w_t * Trig_Open( void );
int        Trig_Fd( trig_w_t * w );                       /* inotify fd, for your own poll()      */
int        Trig_Add( trig_w_t * w, char * path, u32 events, u32 flags, u32 debounce_ms, s32 tag );
int        Trig_Poll( trig_w_t * w, int timeout_ms, trig_cb_t cb, void * arg );   /* callbacks run */
int        Trig_Watches( trig_w_t * w, s32 tag );
void       Trig_Close( trig_w_t * w );

pid_t      Trig_Fork_Watch( char * path, u32 events, u32 flags, u32 debounce_ms, trig_cb_t cb, void * arg );
int        Trig_Stop_Watch( pid_t pid );                  /* SIGTERM + waitpid, returns status    */

u32        Trig_Event_Mask( char * names );               /* "create,write,0x200" -> mask, 0 = bad */
char *     Trig_Event_Names( u32 mask, char * buf, int size );
int        Trig_Mem_Create( char * file, u32 max, u32 debounce_ms );
trig_hdr_t * Trig_Mem_Map( char * file, u64 * size );
int        Trig_Mem_Grow( char * file, trig_hdr_t ** hp, u64 * size, u32 max );
void       Trig_Mem_Unmap( trig_hdr_t * h, u64 size );
u64        Trig_Time( void );    /* usec since epoch */
u64        Trig_Mono( void );    /* monotonic msec   */
```

`Trig_Fork_Watch()` forks a child that watches the tree and calls `cb` **in the child** for each debounced
trigger. The child uses `PR_SET_PDEATHSIG`, so it exits when the parent dies, and it also exits on
`SIGTERM` (`Trig_Stop_Watch()`). To share results with the parent, use a pipe, a mapped file, or a
message from the callback.

What the engine does:

* **Recursive roots:** a root with `TRIG_F_RECURSIVE` gets a watch on every subdirectory. A new or moved-in
  directory is watched as soon as it appears, and its existing contents are reported as `IN_CREATE`, so
  files made before the watch landed aren't lost.
* **Moved-out or deleted subdirectories:** they lose their watches.
* **Debounce:** events are queued per (root, path), and their masks are OR'd together. The callback runs
  only after `debounce_ms` with no new events. The queue holds 64 paths, and when it's full the oldest one
  fires early.
* **Deleted, moved or unmounted root:** `cb(tag, root, IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT)` is called,
  and the server marks the entry GONE. The path is retried every second and watched again when it returns.
* **`IN_Q_OVERFLOW`:** every watch is rebuilt, and `cb(tag, root, IN_Q_OVERFLOW)` is called for each root.

## Trace bits

| Bit | Name | Used by |
|---|---|---|
| `0x20000000` | `TRIG_TRC_LVL` | Shared library functions (upper 16 bits, the unit's single global bit) |
| `0x00000001` | `TRIG_T_WATCH` | trigger server: watch setup |
| `0x00000002` | `TRIG_T_EVENT` | trigger server: events |
| `0x00000004` | `TRIG_T_SPAWN` | trigger server: fork & exec |
| `0x00000008` | `TRIG_T_CHILD` | trigger server: child exits |
| `0x00000010` | `TRIG_T_CTL` | trigger server: control word and reloads |
| `0x00000020` | `TRIG_T_LOOP` | trigger server: every loop pass (noisy) |
| `0x00000100` | `TRIG_T_TOOL` | trigmem |

`Trace(0, ...)` always prints. It's used for startup, stop, errors and lost or returned roots. You can
change tracing live with `trigmem trace file 0x2000001F`, or with any program that writes `TraceLevel` in
the file.

## trigmem tool

```
trigmem create file [ max_entries [ debounce_ms ] ]        default 64 slots, 100 ms
trigmem add    file path events flags debounce_ms program [ args ]
trigmem load   file text_file      lines: path events flags [debounce_ms] program [args], # comments
trigmem del    file entry_number
trigmem print  file
trigmem trace  file level
trigmem ctl    file reload|pause|resume|stop
trigmem wait   file running|stopped [ seconds ]
```

* **Events:** `create write modify delete move moved_from moved_to attrib self changes all`, or hex, joined
  with `,`. `changes` means create, write, delete and move.
* **Flags:** `r` (recursive), `s` (shell), `d` (disabled), or `-` for none.
* **Automatic reload:** `add`, `load` and `del` set the control word to RELOAD when a server is running.
* **Growth:** `add` doubles the file when it's full, and the server picks up the new size on reload.

Example:

```
trigmem create ~/run/src.trigmem
trigmem add ~/run/src.trigmem ~/far write,moved_to r 200 s 'cd ~/far && make "$1"'
trigger ~/run/src.trigmem &
```

## Types

Only `common.h` types are used (u8–u64, s32, s64, u_t, and so on), plus `int` for `main`, loop counters and
file descriptors. Neither `long` nor `long long` appears. 64-bit values are printed with `PRIu64`.

## Tests (`make test` or `tests/trigtest.sh`)

There are 46 checks:

* `.trigmem` create, add, load, del and grow
* server start and the pid check
* create, write, move (from and to) and delete events, with the changed path passed to the program
* the child's exit status (0 and 3) recorded in the entry
* recursive trees:
  * a new subdirectory, and files in new sub-subdirectories
  * the race where a file is created together with its directories
  * a moved-out subtree being dropped and a moved-in subtree being watched
  * the attrib event
* debounce (10 quick writes run the program once)
* live trace through the file: program bits off and on, and the library bit `0x20000000` off and on
* pause, resume, and reload after `add` (the generation goes up)
* direct exec with arguments, multiple entries on one directory, and a watched directory being deleted
  (reported, GONE) and recreated (watched again)
* `Trig_Fork_Watch()` callbacks (including a new subdirectory) and a clean `Trig_Stop_Watch()`
* `Trig_Poll()` with a tag
* stop through the control word (exit 0, state stopped, pid 0)
* rejecting a bad file

You can override the binaries with `TRIGGER=` and `TRIGMEM=`, for example to test builds made by c-saw.

## c-saw

Every source file, `trigwatch.c` included, compiles with `cayecc`. `trigger` and `trigmem` built from
c-saw objects pass all 46 tests.

The shared control and flag words are written with the atomic unit (`Atomic_Store` and `Atomic_Exchange`
in `atomic_bump.h`, a lock free `xchgl`), so gcc and c-saw build the same code and no `__atomic_*`
builtins are used.

## Open questions

* Should a watched root that's deleted or moved also run the program? Right now it only does when `self` is
  in the entry's events.
* Should there be a "no overlap" flag that skips a spawn while the entry's previous child is still running?
* The overflow rescan doesn't replay what changed. Programs get the root path and should rescan it.
* `last_path` is 256 bytes, so longer tree paths are cut short in the record, but the program gets the full
  path.
