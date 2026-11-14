/* GFOpenNews external-unpack hook (design step 8): after a download run
 * ends with every file done (or repaired), the CLI can run a command the
 * user gives, for an unpacker (lha, unrar, 7z...) of their choice. No
 * archive code is in GFOpenNews itself.
 *
 * The template's %d is the output directory and %n the NZB file's name
 * without its directory and ".nzb" (empty for "resume"), each quoted for
 * the shell that runs the line, and %% is a percent sign. Nothing else in
 * the template is changed: it is the user's own command line.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_UNPACK_H
#define GFOPENNEWS_UNPACK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NN_SHELL_POSIX = 0,      /* sh: '...', with ' as '\'' */
       NN_SHELL_AMIGADOS = 1 }; /* AmigaDOS: "...", with " as *" and * as ** */

/* Builds the command line into out. NK_OK; NK_EARG for a % other than
 * %d, %n or %%, or a control character in a value (no newline can split
 * the line into two commands); NK_ETOOLONG when it does not fit. */
int nn_unpack_command(const char *tmpl, const char *dir, const char *name, int shell,
                      char *out, size_t cap);
/* The NZB path's base name without ".nzb" (either case), into out. */
void nn_unpack_name(const char *nzb_path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
