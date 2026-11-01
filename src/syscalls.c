/*
 * Newlib syscall stubs.
 *
 * Linking with --specs=nosys.specs pulls in libnosys, whose _close, _lseek,
 * _read and _write carry a .gnu.warning section. The linker then prints
 *
 *     warning: _write is not implemented and will always fail
 *
 * for each one. The build succeeds, but MCUXpresso IDE's error parser reads
 * the "ld.exe: ...: in function `_write_r':" line that precedes each warning
 * as an error and reports "Build Failed. 4 errors".
 *
 * Defining the symbols here means libnosys' copies are never pulled in, so
 * the warnings disappear at the source rather than being filtered out of the
 * log. There is no file system and no console on this target, so each call
 * fails with EBADF - honest behaviour, not a silent no-op that would make a
 * lost printf look like it worked.
 *
 * _write is the hook the UART driver takes over in Milestone 4: routing it to
 * the serial port is what makes printf work.
 */
#include <errno.h>

int _close(int file)
{
    (void)file;
    errno = EBADF;
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file; (void)ptr; (void)dir;
    errno = EBADF;
    return -1;
}

int _read(int file, char *ptr, int len)
{
    (void)file; (void)ptr; (void)len;
    errno = EBADF;
    return -1;
}

int _write(int file, char *ptr, int len)
{
    (void)file; (void)ptr; (void)len;
    errno = EBADF;
    return -1;
}
