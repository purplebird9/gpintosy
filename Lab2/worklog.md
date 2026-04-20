## Order of Implementation! 
TA session PPT

### 0417
funcs:
- termination message
- parse arg

modified files:
- thread.h
- process_exit()
- syscall.c


### 0417
funcs:
- termination message
- parse arg

modified files:
- thread.h
- process_exit()
- syscall.c


### 0418
**termination message**
modified:
- process.c
- syscall.c
- thread.h(`exit_status`)

**argument parsing**
requirements:
- extend `process_execute()` ok
- multiple spaces = single  检查一下strtok_r怎么实现的
- limit on length: 4KB 

modified: 
- `process_execute()`
- `load()`

design:
- 把`palloc_free_page (file_name_copy)`放进done分支: 防止printf 里 prog_name 所在页面已经被释放


### 0420
- stack setup in argument parsing
    - process.c `setup_stack()`
