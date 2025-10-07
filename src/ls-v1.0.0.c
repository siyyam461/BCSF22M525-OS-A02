/* src/ls-v1.0.0.c
 * ls v1.3.0: supports -l (long), default column display (down then across),
 * and -x horizontal (across then wrap) display.
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <errno.h>

/* ------------ utilities ---------------- */
static void mode_to_str(mode_t m, char *out) {
    out[0] = S_ISDIR(m) ? 'd' :
             S_ISLNK(m) ? 'l' :
             S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' :
             S_ISFIFO(m)? 'p' :
#ifdef S_ISSOCK
             S_ISSOCK(m)? 's' :
#endif
             '-';

    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    if (m & S_ISUID) out[3] = (m & S_IXUSR) ? 's' : 'S';
    else out[3] = (m & S_IXUSR) ? 'x' : '-';

    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    if (m & S_ISGID) out[6] = (m & S_IXGRP) ? 's' : 'S';
    else out[6] = (m & S_IXGRP) ? 'x' : '-';

    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
#ifdef S_ISVTX
    if (m & S_ISVTX) out[9] = (m & S_IXOTH) ? 't' : 'T';
    else out[9] = (m & S_IXOTH) ? 'x' : '-';
#else
    out[9] = (m & S_IXOTH) ? 'x' : '-';
#endif

    out[10] = '\0';
}

static void build_timestr(time_t t, char *buf, size_t bufsz) {
    char *ct = ctime(&t);
    if (!ct) {
        strncpy(buf, "??? ?? ??:??", bufsz);
        buf[bufsz-1] = '\0';
        return;
    }
    if (strlen(ct) >= 16) {
        strncpy(buf, ct + 4, 12);
        buf[12] = '\0';
    } else {
        strncpy(buf, ct, bufsz);
        buf[bufsz-1] = '\0';
    }
}

/* ------------ long-listing structures & functions -------------- */
typedef struct {
    char *name;
    char *fullpath;
    struct stat st;
    char *owner;
    char *group;
    char timestr[32];
    char *linktarget;
} fileinfo_t;

static int read_dir_collect(const char *path, fileinfo_t **out_array, size_t *out_count) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *entry;
    size_t cap = 64, n = 0;
    fileinfo_t *arr = calloc(cap, sizeof(fileinfo_t));
    if (!arr) { closedir(d); return -1; }

    while ((entry = readdir(d))) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (n == cap) {
            cap *= 2;
            fileinfo_t *tmp = realloc(arr, cap * sizeof(fileinfo_t));
            if (!tmp) break;
            arr = tmp;
        }
        fileinfo_t *fi = &arr[n];
        fi->name = strdup(entry->d_name);
        size_t flen = strlen(path) + 1 + strlen(entry->d_name) + 1;
        fi->fullpath = malloc(flen);
        snprintf(fi->fullpath, flen, "%s/%s", path, entry->d_name);

        if (lstat(fi->fullpath, &fi->st) == -1) {
            fi->owner = strdup("?");
            fi->group = strdup("?");
            fi->linktarget = NULL;
            strncpy(fi->timestr, "??? ?? ??:??", sizeof(fi->timestr));
            n++;
            continue;
        }

        struct passwd *pw = getpwuid(fi->st.st_uid);
        struct group  *gr = getgrgid(fi->st.st_gid);
        fi->owner = strdup(pw ? pw->pw_name : "UNKNOWN");
        fi->group = strdup(gr ? gr->gr_name : "UNKNOWN");
        build_timestr(fi->st.st_mtime, fi->timestr, sizeof(fi->timestr));

        if (S_ISLNK(fi->st.st_mode)) {
            ssize_t r;
            char linkbuf[PATH_MAX + 1];
            r = readlink(fi->fullpath, linkbuf, sizeof(linkbuf)-1);
            if (r > 0) {
                linkbuf[r] = '\0';
                fi->linktarget = strdup(linkbuf);
            } else fi->linktarget = NULL;
        } else {
            fi->linktarget = NULL;
        }
        n++;
    }
    closedir(d);
    *out_array = arr;
    *out_count = n;
    return 0;
}

static void free_fileinfo_array(fileinfo_t *arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        free(arr[i].name);
        free(arr[i].fullpath);
        free(arr[i].owner);
        free(arr[i].group);
        if (arr[i].linktarget) free(arr[i].linktarget);
    }
    free(arr);
}

static void long_list(const char *path) {
    fileinfo_t *arr = NULL;
    size_t n = 0;
    if (read_dir_collect(path, &arr, &n) == -1) {
        fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno));
        return;
    }
    size_t w_links = 1, w_owner = 1, w_group = 1, w_size = 1;
    for (size_t i = 0; i < n; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)arr[i].st.st_nlink);
        if (strlen(buf) > w_links) w_links = strlen(buf);
        if (strlen(arr[i].owner) > w_owner) w_owner = strlen(arr[i].owner);
        if (strlen(arr[i].group) > w_group) w_group = strlen(arr[i].group);
        snprintf(buf, sizeof(buf), "%lld", (long long)arr[i].st.st_size);
        if (strlen(buf) > w_size) w_size = strlen(buf);
    }
    for (size_t i = 0; i < n; ++i) {
        char perm[12];
        mode_to_str(arr[i].st.st_mode, perm);
        unsigned long links = (unsigned long)arr[i].st.st_nlink;
        long long sz = (long long)arr[i].st.st_size;
        if (arr[i].linktarget) {
            printf("%s %*lu %-*s %-*s %*lld %s %s -> %s\n",
                   perm, (int)w_links, links,
                   (int)w_owner, arr[i].owner,
                   (int)w_group, arr[i].group,
                   (int)w_size, sz,
                   arr[i].timestr,
                   arr[i].name,
                   arr[i].linktarget);
        } else {
            printf("%s %*lu %-*s %-*s %*lld %s %s\n",
                   perm, (int)w_links, links,
                   (int)w_owner, arr[i].owner,
                   (int)w_group, arr[i].group,
                   (int)w_size, sz,
                   arr[i].timestr,
                   arr[i].name);
        }
    }
    free_fileinfo_array(arr, n);
}

/* ------------ name-only list helpers (for default & -x) ------------ */
static int cmp_strptr(const void *a, const void *b) {
    const char *const *pa = a;
    const char *const *pb = b;
    return strcmp(*pa, *pb);
}

static int get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
        return 80;
    }
    return (int)w.ws_col;
}

static int read_names(const char *path, char ***out_names, size_t *out_count, size_t *out_maxlen) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    size_t cap = 128, n = 0;
    char **arr = malloc(cap * sizeof(char*));
    size_t maxlen = 0;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (e->d_name[0] == '.') continue;
        if (n == cap) {
            cap *= 2;
            arr = realloc(arr, cap * sizeof(char*));
        }
        arr[n] = strdup(e->d_name);
        size_t len = strlen(e->d_name);
        if (len > maxlen) maxlen = len;
        n++;
    }
    closedir(d);
    if (n > 0) qsort(arr, n, sizeof(char*), cmp_strptr);
    *out_names = arr;
    *out_count = n;
    *out_maxlen = maxlen;
    return 0;
}

static void free_names(char **arr, size_t n) {
    for (size_t i = 0; i < n; ++i) free(arr[i]);
    free(arr);
}

/* ------------ default (down then across) column display ------------ */
static void column_list(const char *path) {
    char **names = NULL;
    size_t n = 0;
    size_t maxlen = 0;
    if (read_names(path, &names, &n, &maxlen) == -1) {
        fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno));
        return;
    }
    if (n == 0) { free_names(names, n); return; }

    int termw = get_terminal_width();
    int spacing = 2;
    int colwidth = (int)maxlen + spacing;
    if (colwidth <= 0) colwidth = 1;
    int cols = termw / colwidth;
    if (cols < 1) cols = 1;
    size_t rows = (n + cols - 1) / cols;

    for (size_t r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            size_t idx = c * rows + r;
            if (idx < n) {
                if (c == cols - 1) printf("%s", names[idx]);
                else printf("%-*s", colwidth, names[idx]);
            }
        }
        printf("\n");
    }
    free_names(names, n);
}

/* ------------ new: horizontal (row-major) display for -x ------------ */
static void horizontal_list(const char *path) {
    char **names = NULL;
    size_t n = 0;
    size_t maxlen = 0;
    if (read_names(path, &names, &n, &maxlen) == -1) {
        fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno));
        return;
    }
    if (n == 0) { free_names(names, n); return; }

    int termw = get_terminal_width();
    int spacing = 2;
    int colwidth = (int)maxlen + spacing;
    if (colwidth <= 0) colwidth = 1;

    int curw = 0;
    for (size_t i = 0; i < n; ++i) {
        int namelen = (int)strlen(names[i]);
        /* if first on line, print without leading space; else ensure space/pad fits */
        if (curw == 0) {
            printf("%s", names[i]);
            curw += namelen;
        } else {
            /* if printing this item padded would exceed term width, wrap */
            if (curw + colwidth > termw) {
                printf("\n");
                printf("%s", names[i]);
                curw = namelen;
            } else {
                printf("%-*s", colwidth, names[i]);
                curw += colwidth;
            }
        }
    }
    printf("\n");
    free_names(names, n);
}

/* ------------ fallback simple list (not used) ------------ */
static void simple_list(const char *path) {
    DIR *d = opendir(path);
    if (!d) { fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno)); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0) continue;
        if (e->d_name[0] == '.') continue;
        printf("%s\n", e->d_name);
    }
    closedir(d);
}

/* --------------- main & arg parsing -------------- */
enum display_mode { MODE_DEFAULT = 0, MODE_LONG = 1, MODE_HORIZONTAL = 2 };

int main(int argc, char **argv) {
    int opt;
    enum display_mode mode = MODE_DEFAULT;
    while ((opt = getopt(argc, argv, "lx")) != -1) {
        switch (opt) {
            case 'l': mode = MODE_LONG; break;
            case 'x': mode = MODE_HORIZONTAL; break;
            default:
                fprintf(stderr, "Usage: %s [-l] [-x] [path]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    const char *path = ".";
    if (optind < argc) path = argv[optind];

    if (mode == MODE_LONG) long_list(path);
    else if (mode == MODE_HORIZONTAL) horizontal_list(path);
    else column_list(path);

    return 0;
}
