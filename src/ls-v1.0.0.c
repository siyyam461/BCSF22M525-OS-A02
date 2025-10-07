/* src/lsv1.0.0.c
 * Simple ls with optional -l long listing (version 1.1.0 feature)
 * Uses: lstat(), getpwuid(), getgrgid(), ctime()
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

typedef struct {
    char *name;
    char *fullpath;
    struct stat st;
    char *owner;
    char *group;
    char timestr[32];
    char *linktarget; /* for symlink -> target */
} fileinfo_t;

static void mode_to_str(mode_t m, char *out) {
    /* out must hold at least 11 bytes */
    out[0] = S_ISDIR(m) ? 'd' :
             S_ISLNK(m) ? 'l' :
             S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' :
             S_ISFIFO(m)? 'p' :
             S_ISSOCK(m)? 's' : '-';

    /* user */
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    if (m & S_ISUID) out[3] = (m & S_IXUSR) ? 's' : 'S';
    else out[3] = (m & S_IXUSR) ? 'x' : '-';

    /* group */
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    if (m & S_ISGID) out[6] = (m & S_IXGRP) ? 's' : 'S';
    else out[6] = (m & S_IXGRP) ? 'x' : '-';

    /* others */
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    if (m & S_ISVTX) out[9] = (m & S_IXOTH) ? 't' : 'T';
    else out[9] = (m & S_IXOTH) ? 'x' : '-';

    out[10] = '\0';
}

/* Build a timestring similar to "ls -l" using ctime() result.
 * ctime gives "Wed Jan 13 14:22:01 2021\n" -> we take substring "Jan 13 14:22"
 */
static void build_timestr(time_t t, char *buf, size_t bufsz) {
    char *ct = ctime(&t); /* e.g. "Wed Jan 13 14:22:01 2021\n" */
    if (!ct) {
        strncpy(buf, "??? ?? ??:??", bufsz);
        buf[bufsz-1] = '\0';
        return;
    }
    /* copy month day hh:mm (characters 4..15) */
    if (strlen(ct) >= 16) {
        strncpy(buf, ct + 4, 12);
        buf[12] = '\0';
    } else {
        strncpy(buf, ct, bufsz);
        buf[bufsz-1] = '\0';
    }
}

static int read_dir_collect(const char *path, fileinfo_t **out_array, size_t *out_count) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *entry;
    size_t cap = 64, n = 0;
    fileinfo_t *arr = calloc(cap, sizeof(fileinfo_t));
    while ((entry = readdir(d))) {
        /* keep '.' and '..' if you like; ls shows them only with -a; default skip '.' */
        if (strcmp(entry->d_name, ".") == 0) continue;

        if (n == cap) {
            cap *= 2;
            arr = realloc(arr, cap * sizeof(fileinfo_t));
        }
        fileinfo_t *fi = &arr[n];
        fi->name = strdup(entry->d_name);
        size_t flen = strlen(path) + 1 + strlen(entry->d_name) + 1;
        fi->fullpath = malloc(flen);
        snprintf(fi->fullpath, flen, "%s/%s", path, entry->d_name);

        if (lstat(fi->fullpath, &fi->st) == -1) {
            /* On lstat failure, fill minimal and continue */
            fi->owner = strdup("?");
            fi->group = strdup("?");
            fi->linktarget = NULL;
            strcpy(fi->timestr, "??? ?? ??:??");
            n++;
            continue;
        }

        struct passwd *pw = getpwuid(fi->st.st_uid);
        struct group  *gr = getgrgid(fi->st.st_gid);
        fi->owner = strdup(pw ? pw->pw_name : "UNKNOWN");
        fi->group = strdup(gr ? gr->gr_name : "UNKNOWN");
        build_timestr(fi->st.st_mtime, fi->timestr, sizeof(fi->timestr));

        /* if symlink, read link target */
        if (S_ISLNK(fi->st.st_mode)) {
            ssize_t r;
            char linkbuf[PATH_MAX+1];
            r = readlink(fi->fullpath, linkbuf, sizeof(linkbuf)-1);
            if (r > 0) {
                linkbuf[r] = '\0';
                fi->linktarget = strdup(linkbuf);
            } else {
                fi->linktarget = NULL;
            }
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

    /* compute column widths */
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

    /* print entries */
    for (size_t i = 0; i < n; ++i) {
        char perm[11];
        mode_to_str(arr[i].st.st_mode, perm);

        /* links */
        unsigned long links = (unsigned long)arr[i].st.st_nlink;
        /* size */
        long long sz = (long long)arr[i].st.st_size;

        /* name (+ symlink target) */
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

/* fallback simple listing (names only) */
static void simple_list(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno));
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0) continue;
        printf("%s\n", e->d_name);
    }
    closedir(d);
}

int main(int argc, char **argv) {
    int opt;
    int longflag = 0;
    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
            case 'l': longflag = 1; break;
            default:
                fprintf(stderr, "Usage: %s [-l] [path]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    const char *path = ".";
    if (optind < argc) path = argv[optind];

    if (longflag) long_list(path);
    else simple_list(path);

    return 0;
}

