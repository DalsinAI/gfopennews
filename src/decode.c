/* yEnc 1.3, UUencode and base64 decoders, and multi-part subjects.
 * Our own code, from the yEnc 1.3 draft and RFC 2045 (both public).
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/binaries.h"

/* ---- yEnc ------------------------------------------------------------------ */
enum { YS_IDLE, YS_WANT_PART, YS_DATA, YS_END };

void nn_ye_init(NNYenc *y, NNWriteFn write, void *user)
{
    memset(y, 0, sizeof(*y));
    y->write = write;
    y->user = user;
    y->status = NN_YE_NOBEGIN;
}

/* finds " key=" in a header line and reads a number after it */
static int kv_num(const char *l, size_t n, const char *key, uint64_t *v, int hex)
{
    size_t kl = strlen(key), i;
    for (i = 0; i + kl + 1 <= n; i++) {
        if ((i == 0 || l[i - 1] == ' ') && memcmp(l + i, key, kl) == 0 && l[i + kl] == '=') {
            size_t j = i + kl + 1;
            uint64_t x = 0;
            int d = 0;
            for (; j < n; j++, d++) {
                int c = (unsigned char)l[j], h;
                if (hex) {
                    h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                    if (h < 0) break;
                    x = x << 4 | (uint64_t)h;
                } else {
                    if (c < '0' || c > '9') break;
                    x = x * 10 + (uint64_t)(c - '0');
                }
                if (d > 20) return 0;
            }
            if (!d)
                return 0;
            *v = x;
            return 1;
        }
    }
    return 0;
}

void nn_bin_safe_name(char *name)
{
    /* never a path: keep only the last component */
    char *sl = strrchr(name, '/'), *bs = strrchr(name, '\\'), *co = strrchr(name, ':');
    char *cut = sl;
    if (bs && (!cut || bs > cut)) cut = bs;
    if (co && (!cut || co > cut)) cut = co;
    if (cut)
        memmove(name, cut + 1, strlen(cut + 1) + 1);
    if (!strcmp(name, "..") || !strcmp(name, "."))
        name[0] = 0;
}

static int starts(const char *l, size_t n, const char *p)
{
    size_t k = strlen(p);
    return n >= k && memcmp(l, p, k) == 0;
}

static void ye_out(NNYenc *y, const uint8_t *d, size_t n)
{
    uint64_t room;
    if (!n || y->status != NN_YE_OK)
        return;
    /* never past the =ypart range (or the size, single-part): a part
     * carrying too much would write over the next part's bytes, which are
     * already held with their CRC */
    room = y->end >= y->begin ? y->end - y->begin + 1 : 0;
    if (y->got >= room || n > room - y->got) {
        y->status = NN_YE_SIZE;
        return;
    }
    if (y->write && y->write(y->user, (y->begin ? y->begin - 1 : 0) + y->got, d, n) != 0) {
        y->status = NN_YE_WRITE;
        return;
    }
    y->crc = nk_crc32_update(y->crc, d, n);
    y->got += n;
}

void nn_ye_line(NNYenc *y, const char *l, size_t n)
{
    uint64_t v;
    if (y->state == YS_IDLE || y->state == YS_END) {
        if (!starts(l, n, "=ybegin "))
            return;               /* text around the post, or a second post */
        if (y->state == YS_END)
            return;               /* one post per decoder */
        {
            const char *nm = 0;
            size_t i, hl;
            for (i = 0; i + 5 <= n; i++)
                if ((i == 0 || l[i - 1] == ' ') && memcmp(l + i, "name=", 5) == 0) {
                    nm = l + i + 5;
                    break;
                }
            /* the name is last and may hold anything: keys only before it */
            hl = nm ? (size_t)(nm - 5 - l) : n;
            if (!nm || !kv_num(l, hl, "size", &y->size, 0)) {
                y->status = NN_YE_BADHEADER;
                y->state = YS_END;
                return;
            }
            if (kv_num(l, hl, "part", &v, 0)) {
                y->part = (uint32_t)v;
                y->have |= NN_YE_HAVE_PART;
                if (kv_num(l, hl, "total", &v, 0))
                    y->total = (uint32_t)v;
            }
            i = (size_t)(l + n - nm);
            while (i && (nm[i - 1] == ' ' || nm[i - 1] == '\t'))
                i--;
            if (i >= sizeof(y->name))
                i = sizeof(y->name) - 1;
            memcpy(y->name, nm, i);
            y->name[i] = 0;
            nn_bin_safe_name(y->name);
        }
        y->status = NN_YE_OK;
        if (y->have & NN_YE_HAVE_PART) {
            y->state = YS_WANT_PART;
        } else {
            y->begin = 1;
            y->end = y->size;
            y->state = YS_DATA;
        }
        return;
    }
    if (y->state == YS_WANT_PART) {
        if (!starts(l, n, "=ypart ") || !kv_num(l, n, "begin", &y->begin, 0) ||
            !kv_num(l, n, "end", &y->end, 0) || !y->begin || y->end < y->begin ||
            y->end > y->size) {
            y->status = NN_YE_BADHEADER;
            y->state = YS_END;
            return;
        }
        y->state = YS_DATA;
        return;
    }
    /* YS_DATA */
    if (starts(l, n, "=yend") && (n == 5 || l[5] == ' ')) {
        uint64_t sz;
        if (kv_num(l, n, "pcrc32", &v, 1)) {
            y->pcrc32 = (uint32_t)v;
            y->have |= NN_YE_HAVE_PCRC;
        }
        if (kv_num(l, n, "crc32", &v, 1)) {
            y->crc32 = (uint32_t)v;
            y->have |= NN_YE_HAVE_CRC;
        }
        y->state = YS_END;
        if (y->status != NN_YE_OK)
            return;
        if (!kv_num(l, n, "size", &sz, 0))
            y->status = NN_YE_BADHEADER;
        else if (sz != y->got || y->got != y->end - y->begin + 1)
            y->status = NN_YE_SIZE;
        else if ((y->have & NN_YE_HAVE_PCRC) && y->pcrc32 != y->crc)
            y->status = NN_YE_PCRC;
        else if (!(y->have & NN_YE_HAVE_PART) && (y->have & NN_YE_HAVE_CRC) && y->crc32 != y->crc)
            y->status = NN_YE_CRC;
        return;
    }
    {
        uint8_t buf[512];
        size_t i, o = 0;
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)l[i];
            if (y->esc) {
                buf[o++] = (uint8_t)(c - 64 - 42);
                y->esc = 0;
            } else if (c == '=') {
                y->esc = 1;
                continue;
            } else if (c == '\r' || c == '\n') {
                continue;
            } else
                buf[o++] = (uint8_t)(c - 42);
            if (o == sizeof(buf)) {
                ye_out(y, buf, o);
                o = 0;
            }
        }
        ye_out(y, buf, o);
        /* an '=' as the last byte of a line escapes the next line's first
         * byte: some posters do it, so the flag carries over */
    }
}

int nn_ye_finish(NNYenc *y)
{
    if (y->state == YS_DATA || y->state == YS_WANT_PART)
        y->status = y->status == NN_YE_WRITE ? NN_YE_WRITE : NN_YE_NOEND;
    return y->status;
}

const char *nn_ye_strerror(int s)
{
    switch (s) {
    case NN_YE_OK:        return "ok";
    case NN_YE_NOBEGIN:   return "no yEnc data";
    case NN_YE_NOEND:     return "article cut short (no =yend)";
    case NN_YE_BADHEADER: return "unreadable yEnc header";
    case NN_YE_SIZE:      return "wrong size";
    case NN_YE_PCRC:      return "part CRC mismatch";
    case NN_YE_CRC:       return "file CRC mismatch";
    case NN_YE_WRITE:     return "could not write";
    default:              return "unknown";
    }
}

/* ---- UUencode -------------------------------------------------------------- */
void nn_uu_init(NNUu *u, NNWriteFn write, void *user)
{
    memset(u, 0, sizeof(*u));
    u->write = write;
    u->user = user;
}

#define UUDEC(c) (((c) - ' ') & 63)

void nn_uu_line(NNUu *u, const char *l, size_t n)
{
    while (n && (l[n - 1] == '\r' || l[n - 1] == '\n'))
        n--;
    if (u->state == 0) {
        size_t i = 6;
        unsigned mode = 0;
        if (n < 10 || memcmp(l, "begin ", 6) != 0)
            return;
        while (i < n && l[i] >= '0' && l[i] <= '7')
            mode = mode * 8 + (unsigned)(l[i++] - '0');
        if (i == 6 || i >= n || l[i] != ' ')
            return;
        i++;
        u->mode = mode;
        if (n - i >= sizeof(u->name))
            n = i + sizeof(u->name) - 1;
        memcpy(u->name, l + i, n - i);
        u->name[n - i] = 0;
        nn_bin_safe_name(u->name);
        u->state = 1;
        return;
    }
    if (u->state != 1)
        return;
    if (n == 3 && memcmp(l, "end", 3) == 0) {
        u->state = 2;
        return;
    }
    if (!n)
        return;
    {
        uint8_t out[64];
        size_t want = (size_t)UUDEC((unsigned char)l[0]), o = 0, i = 1;
        if (want == 0)
            return;                 /* the "`" line before end */
        /* trailing spaces (zeros) are often cut by mailers and servers, so
         * missing characters count as zero; a line longer than its count
         * says is not uuencode */
        if (n < 2 || n > 1 + (want + 2) / 3 * 4 + 2) {
            u->bad_lines++;
            return;
        }
        while (o < want) {
            unsigned c0 = i < n ? UUDEC((unsigned char)l[i]) : 0;
            unsigned c1 = i + 1 < n ? UUDEC((unsigned char)l[i + 1]) : 0;
            unsigned c2 = i + 2 < n ? UUDEC((unsigned char)l[i + 2]) : 0;
            unsigned c3 = i + 3 < n ? UUDEC((unsigned char)l[i + 3]) : 0;
            out[o++] = (uint8_t)(c0 << 2 | c1 >> 4);
            if (o < want) out[o++] = (uint8_t)(c1 << 4 | c2 >> 2);
            if (o < want) out[o++] = (uint8_t)(c2 << 6 | c3);
            i += 4;
        }
        if (u->write && u->write(u->user, u->got, out, o) != 0) {
            u->bad_lines++;
            return;
        }
        u->got += o;
    }
}

int nn_uu_done(const NNUu *u)
{
    return u->state == 2;
}

/* ---- base64 ---------------------------------------------------------------- */
void nn_b64_init(NNB64 *b, NNWriteFn write, void *user)
{
    memset(b, 0, sizeof(*b));
    b->write = write;
    b->user = user;
}

static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int nn_b64_line(NNB64 *b, const char *l, size_t n)
{
    uint8_t out[256];
    size_t i, o = 0;
    for (i = 0; i < n && !b->ended; i++) {
        int v;
        if (l[i] == '=') {
            b->ended = 1;           /* padding: the rest is done */
            break;
        }
        v = b64v((unsigned char)l[i]);
        if (v < 0)
            continue;               /* RFC 2045: ignore what isn't base64 */
        b->acc = b->acc << 6 | (uint32_t)v;
        b->bits += 6;
        if (b->bits >= 8) {
            b->bits -= 8;
            out[o++] = (uint8_t)(b->acc >> b->bits);
            if (o == sizeof(out)) {
                if (b->write && b->write(b->user, b->got, out, o) != 0)
                    return NK_EIO;
                b->got += o;
                o = 0;
            }
        }
    }
    if (o) {
        if (b->write && b->write(b->user, b->got, out, o) != 0)
            return NK_EIO;
        b->got += o;
    }
    return NK_OK;
}

/* ---- subjects -------------------------------------------------------------- */
int nn_bin_subject(const char *s, char *name, size_t cap, uint32_t *part, uint32_t *total)
{
    size_t n = strlen(s), i;
    const char *q1 = strchr(s, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : 0;
    int found = 0;
    if (cap)
        name[0] = 0;
    *part = *total = 0;
    /* the last (n/m) or [n/m] */
    for (i = n; i-- > 0;) {
        if (s[i] == '(' || s[i] == '[') {
            char close = s[i] == '(' ? ')' : ']';
            const char *p = s + i + 1;
            unsigned long a = 0, b = 0;
            int da = 0, db = 0;
            while (*p >= '0' && *p <= '9' && da < 10) a = a * 10 + (unsigned long)(*p++ - '0'), da++;
            if (!da || *p != '/') continue;
            p++;
            while (*p >= '0' && *p <= '9' && db < 10) b = b * 10 + (unsigned long)(*p++ - '0'), db++;
            if (!db || *p != close || !b || a > b) continue;
            *part = (uint32_t)a;
            *total = (uint32_t)b;
            found = 1;
            break;
        }
    }
    if (!cap)
        return found;
    if (q1 && q2 && q2 > q1 + 1) {
        size_t l = (size_t)(q2 - q1 - 1);
        if (l >= cap) l = cap - 1;
        memcpy(name, q1 + 1, l);
        name[l] = 0;
    } else {
        /* "name.ext yEnc (1/5)": the word before " yEnc" */
        const char *y = strstr(s, " yEnc");
        if (y) {
            const char *st = y;
            size_t l;
            while (st > s && st[-1] != ' ')
                st--;
            l = (size_t)(y - st);
            if (l >= cap) l = cap - 1;
            memcpy(name, st, l);
            name[l] = 0;
        }
    }
    nn_bin_safe_name(name);
    return found;
}
