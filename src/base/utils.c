#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <syscall.h>
#include "macros.h"
#include "utils.h"


void cbox_utils_daemon(const char *dir)
{
    char max_file[32];
    int max_file_i = 0;
    int pid = 0;
    FILE *fp = NULL;
    int i = 0;

    pid = fork();
    if (pid) exit(EXIT_SUCCESS);
    else if (pid < 0) exit(EXIT_FAILURE);

    setsid();
    pid = fork();
    if (pid) exit(EXIT_SUCCESS);
    else if (pid < 0) exit(EXIT_FAILURE);

    fp = fopen("/proc/sys/fs/file-max", "r");
    if (fp == NULL)
        goto DONE;

    fgets(max_file, sizeof(max_file), fp);
    fclose(fp);
    max_file_i = atoi(max_file);
    for (i = 0; i < max_file_i; ++i)
        close(i);

DONE:
    chdir(dir);
    umask(0);
}

static int lockfile(int fd)
{
    struct flock fl =
    {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };

    return TEMP_FAILURE_RETRY(fcntl(fd, F_SETLK, &fl));
}

int cbox_utils_i_am_running(const char *name)
{
    char buf[16];

    int fd = open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        exit(1);
    }

    if (lockfile(fd) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return 1;
        }

        exit(EXIT_FAILURE);
    }

    ftruncate(fd, 0);
    sprintf(buf, "%ld", (long)getpid());
    TEMP_FAILURE_RETRY(write(fd, buf, strlen(buf) + 1));

    return 0;
}

int cbox_utils_running_in_main()
{
    return getpid() == syscall(SYS_gettid);
}

void cbox_utils_hexstr_to_bin(const char *hexstr, unsigned char *bin)
{
    const char *p_hex = hexstr;
    size_t hex_len = strlen(hexstr);
    size_t i = 0;
    for (i = 0; i < hex_len / 2; i++, p_hex += 2) {
        char s[3];
        strncpy(s, p_hex, 2);
        s[2] = '\0';
        bin[i] = (unsigned char)strtol(s, NULL, 16);
    }
}

void cbox_utils_bin_to_hexstr(unsigned char *bin, unsigned bin_len, char *hexstr, int space, int upper)
{
    size_t i = 0, j = 0;
    for (i = 0; i < bin_len; ++i) {
        sprintf(hexstr + i * 2 + j, upper ? "%02X" : "%02x", bin[i]);

        if (!space)
            continue;

        strcat((char *)hexstr, " ");
        j++;
    }
}

void cbox_utils_system(const char *fmt, ...)
{
    char cmd[1024];
    va_list argp;
    va_start(argp, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, argp);
    if (*cmd) system(cmd);
}

void cbox_utils_perform(const char *cmd, void (*handler)(const char *, const void *), const void *context, int no_lf)
{
    FILE *fp = NULL;
    char line[1024];

RETRY:
    fp = popen(cmd, "r");
    if (fp == NULL) return;

    while (!feof(fp)) {
        char *pline = fgets(line, sizeof(line), fp);
        if (pline == NULL) {
            if (errno == EINTR) {
                pclose(fp);
                goto RETRY;
            }
            else break;
        }

        if (!*line) continue;

        if (no_lf && line[strlen(line) - 1] == '\n') line[strlen(line) - 1] = '\0';

        (*handler)(line, context);
    }

    pclose(fp);
}

int cbox_utils_copy_file(const char *dest, const char *src)
{
    int ret = 0;

    struct stat fstat;
    int sfd = -1, dfd = -1;

    ssize_t max, rlen;
    void *contents = NULL;

    if (access(src, F_OK) != 0 || stat(src, &fstat) < 0 || (sfd = open(src, O_RDONLY)) < 0) goto RET;

    if ((dfd = open(dest, O_RDWR | O_CREAT | O_TRUNC, 0664)) < 0) goto RET;

    max = (size_t)(fstat.st_size > 4096 * 256 ? 4096 * 256 : fstat.st_size);

    if ((contents = calloc(max, 1)) == NULL) goto RET;

    while ((rlen = read(sfd, contents, max)) > 0) TEMP_FAILURE_RETRY(write(dfd, contents, rlen));

    ret = 1;

RET:
    if(sfd > 0) close(sfd);
    if(dfd > 0) close(dfd);

    if(contents) free(contents);

    return ret;
}

