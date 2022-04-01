.globl realmode_blob_start, realmode_blob_size

# Workaround Meson bug. This is the "dirty, filthy, terrible hack" suggested at
# https://github.com/mesonbuild/meson/issues/10241#issuecomment-1087964333
#include "realmode_bin_kludge.h"

# realmode blob is writable at runtime so that we can update its header in-place
.section .data
realmode_blob_start:
    .incbin REALMODE_BIN_PATH
realmode_blob_end:

.section .rodata
realmode_blob_size:
    .quad realmode_blob_end - realmode_blob_start
