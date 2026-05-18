# 5.17
**15:00-18:00**, **21:00-22:00**
- Doc reading.
- Overview VA-PA translation procedure.
- Create vm data structures files:
    - vm/spt (per-process)
    - vm/frame (global)
    - vm/swap (global)
- TODO: modify `struct thread` :`struct hash spt`;

# 5.18
**10:00-11:00**
- use frame allocator in load&stack-setup in `process.c`
- frame init&free path in `init.c` and `pagedir.c`

check: all lab2 tests still passed(except the old output disorder?????)

TODO: SPT, and record info whe load&stack-setup in `process.c`.


