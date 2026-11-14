/* File names that work on the host and on AmigaOS: a directory is "" (the
 * current one), "DH0:", "DH0:dir" or "dir/sub", and '/' or ':' ends it.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_PATH_H
#define GFOPENNEWS_PATH_H
#include <stddef.h>

/* The directory part of `path` (with the ':' of a volume kept, without a
 * trailing '/'), "" when there is none. The file part is path + the answer.
 * NK_EARG when it does not fit in `cap`. */
int nn_path_dir(const char *path, char *dir, size_t cap, size_t *name_at);

/* dir + name with a '/' between them only when dir needs one.
 * NK_EARG when it does not fit in `cap`. */
int nn_path_join(const char *dir, const char *name, char *out, size_t cap);

#endif
