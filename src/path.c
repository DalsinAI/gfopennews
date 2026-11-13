/* Host and AmigaOS file names. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/path.h"

int nn_path_dir(const char *path, char *dir, size_t cap, size_t *name_at)
{
    const char *sl = strrchr(path, '/'), *co = strrchr(path, ':'), *end;
    size_t n;
    if (!cap)
        return NK_EARG;
    if (co && (!sl || co > sl)) {
        end = co + 1;               /* "DH0:x" -> "DH0:" */
        n = (size_t)(end - path);
    } else if (sl) {
        end = sl + 1;               /* "a/b" -> "a", "DH0:a/b" -> "DH0:a" */
        n = (size_t)(sl - path);
        if (!n)
            n = 1;                  /* "/x": the root (host), the parent (Amiga) */
    } else {
        end = path;
        n = 0;
    }
    if (n >= cap)
        return NK_EARG;
    memcpy(dir, path, n);
    dir[n] = 0;
    if (name_at)
        *name_at = (size_t)(end - path);
    return NK_OK;
}

int nn_path_join(const char *dir, const char *name, char *out, size_t cap)
{
    size_t dl = strlen(dir);
    int sep = dl && dir[dl - 1] != '/' && dir[dl - 1] != ':';
    int n = snprintf(out, cap, "%s%s%s", dir, sep ? "/" : "", name);
    return n < 0 || (size_t)n >= cap ? NK_EARG : NK_OK;
}
