/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
/* BerryOS init process (pid 1) + M3 interactive shell.
 *
 * M2 one-time demo forks/execs/waits the worker program; afterwards init
 * becomes an interactive shell that reads commands from the keyboard via
 * the blocking sys_read() (canonical mode, echoed live by the keyboard
 * driver).  Commands: help, echo, worker, clear, about, exit. */
#include "syscall.h"
#include "string.h"

static const char crlf[] = "\r\n";

static int streq(const char* a, const char* b){
    while (*a && *a == *b){ a++; b++; }
    return (*a == 0 && *b == 0);
}

/* Split line into argv on spaces/tabs (modifies line in place). */
static int tokenize(char* line, char* argv[], int max){
    int argc = 0;
    char* p = line;
    while (*p && argc < max){
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p){ *p = 0; p++; }
    }
    return argc;
}

static void run_worker(void){
    long pid = sys_fork();
    if (pid == 0){
        sys_exec(1);           /* replace image with the worker program */
        sys_exit(127);         /* reached only if exec failed */
    }
    sys_wait(pid);             /* parent waits (blocks) */
}

static void cmd_echo(char* argv[], int argc){
    if (argc <= 1){ sys_write(1, crlf, 2); return; }
    for (int i = 1; i < argc; i++){
        sys_write(1, argv[i], strlen(argv[i]));
        if (i + 1 < argc) sys_write(1, " ", 1);
    }
    sys_write(1, crlf, 2);
}

static void cmd_ls(void){
    static char buf[2048];
    long n = sys_ls(buf, sizeof(buf));
    if (n < 0){ sys_write(1, "ls: failed\r\n", 13); return; }
    if (n == 0){ sys_write(1, "(empty)\r\n", 9); return; }
    sys_write(1, buf, (unsigned long)n);
}

static void cmd_mkfs(void){
    sys_mkfs();
    sys_write(1, "filesystem formatted\r\n", 21);
}

/* Rebuild a single space-joined string from argv[first..argc-1] into out. */
static void join_args(char* out, int first, char* argv[], int argc, int max){
    int i, p = 0;
    for (i = first; i < argc && p < max - 1; i++){
        const char* s = argv[i];
        while (*s && p < max - 1) out[p++] = *s++;
        if (i + 1 < argc && p < max - 1) out[p++] = ' ';
    }
    out[p] = 0;
}

static void cmd_write(char* argv[], int argc){
    static char text[256];
    long fd;
    if (argc < 3){ sys_write(1, "usage: write <file> <text>\r\n", 28); return; }
    join_args(text, 2, argv, argc, (int)sizeof(text));
    fd = sys_open(argv[1], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0){ sys_write(1, "write: open failed\r\n", 20); return; }
    sys_fwrite(fd, text, strlen(text));
    sys_close(fd);
    sys_write(1, "wrote ", 6);
    sys_write(1, argv[1], strlen(argv[1]));
    sys_write(1, crlf, 2);
}

static void cmd_cat(char* argv[], int argc){
    static char buf[512];
    long fd, n;
    if (argc < 2){ sys_write(1, "usage: cat <file>\r\n", 19); return; }
    fd = sys_open(argv[1], O_RDONLY);
    if (fd < 0){ sys_write(1, "cat: no such file\r\n", 20); return; }
    while ((n = sys_fread(fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (unsigned long)n);
    sys_close(fd);
}

static void cmd_rm(char* argv[], int argc){
    long r;
    if (argc < 2){ sys_write(1, "usage: rm <file>\r\n", 18); return; }
    r = sys_unlink(argv[1]);
    if (r < 0){ sys_write(1, "rm: no such file\r\n", 19); return; }
    sys_write(1, "removed ", 8);
    sys_write(1, argv[1], strlen(argv[1]));
    sys_write(1, crlf, 2);
}

int main(void){
    static const char banner[]   = "BerryOS init (pid 1)\r\n";
    static const char shellmsg[] =
        "BerryOS shell ready. Type 'help' for commands.\r\n";
    static const char prompt[]   = "$ ";

    sys_write(1, banner, sizeof(banner) - 1);

    /* M2 one-time demo: fork + exec + wait the worker program */
    {
        static const char m[] = "[init] M2 demo: fork+exec+wait worker\r\n";
        sys_write(1, m, sizeof(m) - 1);
        run_worker();
    }

    sys_write(1, shellmsg, sizeof(shellmsg) - 1);

    for (;;){
        static char line[256];
        char* argv[16];
        long n;

        sys_write(1, prompt, sizeof(prompt) - 1);
        n = sys_read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = 0;
        if (n >= 1 && line[n - 1] == '\n') line[n - 1] = 0;

        int argc = tokenize(line, argv, 16);
        if (argc == 0) continue;

        if (streq(argv[0], "help")){
            static const char h[] =
                "commands:\r\n"
                "  help          show this help\r\n"
                "  echo <text>   print text\r\n"
                "  worker        run the worker program (fork+exec+wait)\r\n"
                "  clear         clear the screen\r\n"
                "  about         OS information\r\n"
                "  ls            list files on BerryFS\r\n"
                "  write <f> <t> write text to a file (creates/truncates)\r\n"
                "  cat <f>       print a file\r\n"
                "  rm <f>        delete a file\r\n"
                "  mkfs          format BerryFS (wipes all files)\r\n"
                "  exit          halt the OS\r\n";
            sys_write(1, h, sizeof(h) - 1);
        } else if (streq(argv[0], "echo")){
            cmd_echo(argv, argc);
        } else if (streq(argv[0], "worker")){
            run_worker();
        } else if (streq(argv[0], "clear")){
            sys_clear();
        } else if (streq(argv[0], "about")){
            static const char a[] =
                "BerryOS 0.0.1 - hybrid x86_64 kernel\r\n"
                "M1 boot/mm/paging/sched  M2 multiprocess  M3 keyboard+shell\r\n"
                "M4 device framework + BerryFS persistent filesystem\r\n";
            sys_write(1, a, sizeof(a) - 1);
        } else if (streq(argv[0], "ls")){
            cmd_ls();
        } else if (streq(argv[0], "mkfs")){
            cmd_mkfs();
        } else if (streq(argv[0], "write")){
            cmd_write(argv, argc);
        } else if (streq(argv[0], "cat")){
            cmd_cat(argv, argc);
        } else if (streq(argv[0], "rm")){
            cmd_rm(argv, argc);
        } else if (streq(argv[0], "exit") || streq(argv[0], "quit")){
            static const char bye[] = "BerryOS halted.\r\n";
            sys_write(1, bye, sizeof(bye) - 1);
            sys_exit(0);
        } else {
            static const char err[] = "unknown command: ";
            sys_write(1, err, sizeof(err) - 1);
            sys_write(1, argv[0], strlen(argv[0]));
            sys_write(1, crlf, 2);
        }
    }
}
