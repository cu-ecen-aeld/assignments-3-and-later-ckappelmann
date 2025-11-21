# Faulty OOPs Analysis

## Issue
Writing to the /dev/faulty device results in a kernel crash.

```bash
root@qemuarm64:~# echo "hello" > /dev/faulty 
[   55.768338] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[   55.770117] Mem abort info:
[   55.770182]   ESR = 0x0000000096000045
[   55.770258]   EC = 0x25: DABT (current EL), IL = 32 bits
[   55.770339]   SET = 0, FnV = 0
[   55.771090]   EA = 0, S1PTW = 0
[   55.771161]   FSC = 0x05: level 1 translation fault
[   55.771238] Data abort info:
[   55.772623]   ISV = 0, ISS = 0x00000045
[   55.772915]   CM = 0, WnR = 1
[   55.773214] user pgtable: 4k pages, 39-bit VAs, pgdp=0000000043de3000
[   55.773874] [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
[   55.775549] Internal error: Oops: 0000000096000045 [#1] PREEMPT SMP
[   55.776316] Modules linked in: scull(O) faulty(O) hello(O)
[   55.776868] CPU: 0 PID: 314 Comm: sh Tainted: G           O      5.15.194-yocto-standard #1
[   55.778291] Hardware name: linux,dummy-virt (DT)
[   55.779978] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   55.780301] pc : faulty_write+0x18/0x20 [faulty]
[   55.782147] lr : vfs_write+0xf8/0x2a0
[   55.782363] sp : ffffffc0097fbd80
[   55.782510] x29: ffffffc0097fbd80 x28: ffffff800373d280 x27: 0000000000000000
[   55.783154] x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
[   55.783893] x23: 0000000000000000 x22: ffffffc0097fbdc0 x21: 000000555bd76cb0
[   55.785920] x20: ffffff80036e4e00 x19: 0000000000000006 x18: 0000000000000000
[   55.786340] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
[   55.787848] x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
[   55.788349] x11: 0000000000000000 x10: 0000000000000000 x9 : ffffffc008272108
[   55.789216] x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
[   55.789882] x5 : 0000000000000001 x4 : ffffffc000ba5000 x3 : ffffffc0097fbdc0
[   55.790583] x2 : 0000000000000006 x1 : 0000000000000000 x0 : 0000000000000000
[   55.791884] Call trace:
[   55.792629]  faulty_write+0x18/0x20 [faulty]
[   55.793083]  ksys_write+0x74/0x110
[   55.793390]  __arm64_sys_write+0x24/0x30
[   55.793655]  invoke_syscall+0x5c/0x130
[   55.793944]  el0_svc_common.constprop.0+0x4c/0x100
[   55.794231]  do_el0_svc+0x4c/0xc0
[   55.794750]  el0_svc+0x28/0x80
[   55.794895]  el0t_64_sync_handler+0xa4/0x130
[   55.795067]  el0t_64_sync+0x1a0/0x1a4
[   55.795455] Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
[   55.798211] ---[ end trace c0a081824e47277e ]---
Segmentation fault
```

We can see by this message, that a segmentation fault occurs because of a Null pointer dereference. The call trace further tells us that the dereference occurs at faulty_write+0x18/0x20 in the faulty device.

## strace Analysis
Running with strace produces the following.

```bash
root@qemuarm64:~# strace echo "hello" > /dev/faulty 
execve("/bin/echo", ["echo", "hello"], 0x7fd4248368 /* 12 vars */) = 0
brk(NULL)                               = 0x5563b95000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f97417000
faccessat(AT_FDCWD, "/etc/ld.so.preload", R_OK) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
newfstatat(3, "", {st_mode=S_IFREG|0644, st_size=3463, ...}, AT_EMPTY_PATH) = 0
mmap(NULL, 3463, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f97416000
close(3)                                = 0
openat(AT_FDCWD, "/lib/libm.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0\267\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832
newfstatat(3, "", {st_mode=S_IFREG|0755, st_size=555048, ...}, AT_EMPTY_PATH) = 0
mmap(NULL, 684056, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f9733a000
mmap(0x7f97340000, 618520, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0) = 0x7f97340000
munmap(0x7f9733a000, 24576)             = 0
munmap(0x7f973d8000, 36888)             = 0
mprotect(0x7f973c7000, 61440, PROT_NONE) = 0
mmap(0x7f973d6000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x86000) = 0x7f973d6000
close(3)                                = 0
openat(AT_FDCWD, "/lib/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0\267\0\1\0\0\0\360\263\2\0\0\0\0\0"..., 832) = 832
pread64(3, "\4\0\0\0\24\0\0\0\3\0\0\0GNU\0\27u\nG\234U\254\2\205\10\245\211c\177A\274"..., 68, 768) = 68
newfstatat(3, "", {st_mode=S_IFREG|0755, st_size=1634112, ...}, AT_EMPTY_PATH) = 0
mmap(NULL, 1809352, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f97186000
mmap(0x7f97190000, 1743816, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0) = 0x7f97190000
munmap(0x7f97186000, 40960)             = 0
munmap(0x7f9733a000, 23496)             = 0
mprotect(0x7f97318000, 65536, PROT_NONE) = 0
mmap(0x7f97328000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x188000) = 0x7f97328000
mmap(0x7f9732e000, 48072, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) = 0x7f9732e000
close(3)                                = 0
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f97414000
set_tid_address(0x7f974140f0)           = 415
set_robust_list(0x7f97414100, 24)       = 0
rseq(0x7f974147c0, 0x20, 0, 0xd428bc00) = 0
mprotect(0x7f97328000, 12288, PROT_READ) = 0
mprotect(0x7f973d6000, 4096, PROT_READ) = 0
mprotect(0x5563b91000, 12288, PROT_READ) = 0
mprotect(0x7f9741c000, 8192, PROT_READ) = 0
prlimit64(0, RLIMIT_STACK, NULL, {rlim_cur=8192*1024, rlim_max=RLIM64_INFINITY}) = 0
munmap(0x7f97416000, 3463)              = 0
getrandom("\x87\x3f\xaf\x92\xa1\x07\x94\xf3", 8, GRND_NONBLOCK) = 8
getuid()                                = 0
brk(NULL)                               = 0x5563b95000
brk(0x5563bb6000)                       = 0x5563bb6000
write(1, "hello\n", 6[   58.875095] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[   58.878944] Mem abort info:
[   58.879015]   ESR = 0x0000000096000045
[   58.879327]   EC = 0x25: DABT (current EL), IL = 32 bits
[   58.880192]   SET = 0, FnV = 0
[   58.882229]   EA = 0, S1PTW = 0
[   58.882501]   FSC = 0x05: level 1 translation fault
[   58.883255] Data abort info:
[   58.884250]   ISV = 0, ISS = 0x00000045
[   58.888381]   CM = 0, WnR = 1
[   58.889355] user pgtable: 4k pages, 39-bit VAs, pgdp=00000000423d1000
[   58.892137] [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
[   58.895583] Internal error: Oops: 0000000096000045 [#1] PREEMPT SMP
[   58.900891] Modules linked in: scull(O) faulty(O) hello(O)
[   58.902139] CPU: 1 PID: 415 Comm: echo Tainted: G           O      5.15.194-yocto-standard #1
[   58.902603] Hardware name: linux,dummy-virt (DT)
[   58.903080] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   58.903390] pc : faulty_write+0x18/0x20 [faulty]
[   58.904265] lr : vfs_write+0xf8/0x2a0
[   58.905147] sp : ffffffc00973bd80
[   58.905517] x29: ffffffc00973bd80 x28: ffffff8003e09b80 x27: 0000000000000000
[   58.906100] x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
[   58.906473] x23: 0000000000000000 x22: ffffffc00973bdc0 x21: 0000005563b952a0
[   58.909460] x20: ffffff8003c37b00 x19: 0000000000000006 x18: 0000000000000000
[   58.909952] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
[   58.910069] x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
[   58.910183] x11: 00000000000000c0 x10: 0000000000000970 x9 : ffffffc008272108
[   58.910336] x8 : ffffff8003e0a550 x7 : 0000000000000001 x6 : 0000000db524a800
[   58.910444] x5 : 0000000000000000 x4 : ffffffc000ba5000 x3 : ffffffc00973bdc0
[   58.910739] x2 : 0000000000000006 x1 : 0000000000000000 x0 : 0000000000000000
[   58.910984] Call trace:
[   58.911057]  faulty_write+0x18/0x20 [faulty]
[   58.911162]  ksys_write+0x74/0x110
[   58.911234]  __arm64_sys_write+0x24/0x30
[   58.911309]  invoke_syscall+0x5c/0x130
[   58.911373]  el0_svc_common.constprop.0+0xdc/0x100
[   58.911451]  do_el0_svc+0x4c/0xc0
[   58.911505]  el0_svc+0x28/0x80
[   58.911559]  el0t_64_sync_handler+0xa4/0x130
[   58.912146]  el0t_64_sync+0x1a0/0x1a4
[   58.919902] Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
[   58.920647] ---[ end trace c4a36d4f567ab3ac ]---
)                  = ?
+++ killed by SIGSEGV +++
Segmentation fault
```

This doesn't add too much information that we don't already have. We can still localize the fault to the faulty_write call.

## Fault line discovery with addr2line
We can find the line of the fault by using the addr2line program. The following command on the host provides the line of the source given the binary driver and the address extracted from the koops.

```bash
aarch64-none-linux-gnu-addr2line -e build/tmp/work/qemuarm64-poky-linux/misc-modules/1.0+gitAUTOINC+4344472640-r0/image/lib/modules/5.15.194-yocto-standard/extra/faulty.ko faulty_write+0x18/0x20 
/usr/src/debug/misc-modules/1.0+gitAUTOINC+4344472640-r0/git/misc-modules/faulty.c:53
```

Looking at the provided source reference, we see the following:
```c
*(int *)0 = 0;
```

This is indeed our null dereference.