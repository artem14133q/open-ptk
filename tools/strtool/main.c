// strtool: extractor/lister for Petka's "StOR" resource containers.
//
// Format, fully reverse-engineered from Petka.exe (storeInit/MultiOpen):
//   offset 0: "StOR"            magic
//   offset 4: u32 LE trailer_off  absolute file offset where the index starts
//             (this is NOT "payload size" -- it already counts the 8-byte
//             header, i.e. payload itself is bytes [8, trailer_off))
//   [8, trailer_off): raw resource bytes, addressed only by the index below
//                     (not self-delimited -- do not scan this region)
//   at trailer_off:
//     u32 count
//     count * 12-byte records:
//       u32 unused_on_disk   (engine overwrites this with an in-memory name
//                             pointer at load time; on disk it's leftover
//                             build-tool garbage, ignore it)
//       u32 offset           absolute file offset of this resource
//       u32 length           byte length of this resource
//     count NUL-terminated name strings (CP866), concatenated in record
//     order, e.g. "PART_1\BOLOTO.BG\BOLOTO.BMP". Names end exactly at EOF.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// mkdir -p for the directory portion of a '/'-separated relative path.
static void mkdirs_for(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.str> [extract_dir]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    const char *out_dir = argc > 2 ? argv[2] : NULL;
    if (out_dir) mkdir(out_dir, 0755);

    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)filesize);
    if (fread(buf, 1, (size_t)filesize, f) != (size_t)filesize) {
        fprintf(stderr, "short read\n");
        return 1;
    }
    fclose(f);

    if (filesize < 8 || memcmp(buf, "StOR", 4) != 0) {
        fprintf(stderr, "not a StOR container\n");
        return 1;
    }
    uint32_t trailer_off = read_u32le(buf + 4);
    if (trailer_off + 4 > (uint32_t)filesize) {
        fprintf(stderr, "trailer_off out of range\n");
        return 1;
    }
    uint32_t count = read_u32le(buf + trailer_off);
    printf("file: %s\n", path);
    printf("filesize=%ld trailer_off=%u count=%u\n", filesize, trailer_off, count);

    long records_off = (long)trailer_off + 4;
    long names_off = records_off + (long)count * 12;
    if (names_off > filesize) {
        fprintf(stderr, "count looks bogus (records run past EOF)\n");
        return 1;
    }

    long pos = names_off;
    for (uint32_t i = 0; i < count; i++) {
        long rec = records_off + (long)i * 12;
        uint32_t off = read_u32le(buf + rec + 4);
        uint32_t len = read_u32le(buf + rec + 8);

        char name[512];
        long start = pos;
        while (pos < filesize && buf[pos] != 0) pos++;
        long n = pos - start;
        if (n >= (long)sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, buf + start, (size_t)n);
        name[n] = 0;
        pos++; // skip NUL

        for (char *p = name; *p; p++) if (*p == '\\') *p = '/';

        printf("[%u] off=%-10u len=%-10u %s\n", i, off, len, name);

        if (out_dir) {
            if ((long)off + len > filesize) {
                fprintf(stderr, "  skip %s: out of range\n", name);
                continue;
            }
            char outpath[1536];
            snprintf(outpath, sizeof(outpath), "%s/%s", out_dir, name);
            mkdirs_for(outpath);
            FILE *of = fopen(outpath, "wb");
            if (!of) { fprintf(stderr, "  can't write %s: %s\n", outpath, strerror(errno)); continue; }
            fwrite(buf + off, 1, len, of);
            fclose(of);
        }
    }
    printf("names ended at %ld (filesize=%ld)\n", pos, filesize);

    free(buf);
    return 0;
}
