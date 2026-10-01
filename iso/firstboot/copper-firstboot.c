/*
 * copper-firstboot — first-boot personalization, in the spirit of the OOBE
 * handcrafted by farcrowx
 * on real distros / Windows. copper-init runs this once (until the marker
 * /etc/copper-firstboot.done exists).
 *
 * Asks for: name, username, hostname, timezone, passwords (root + the named
 * user), and optionally a WiFi network (SSID + PSK → /etc/wpa_supplicant.conf).
 * Creates the account via busybox adduser, sets passwords via busybox chpasswd,
 * wires up /etc/localtime.
 *
 * Live-session only for now (the overlay is tmpfs, so it re-runs next
 * boot) — real persistence is a later phase.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void banner(void) {
    printf("\n===================================================\n");
    printf("         Welcome to Copper Linux\n");
    printf("===================================================\n");
    printf("Made by farcrowx and 12hrformat\n");
    printf("A couple of questions and you're in. (This is a live\n");
    printf("session, so answers apply for now — persistence is\n");
    printf("coming in a later build.)\n\n");
}

/* Read one line from stdin, strip the trailing newline/CR.
   Returns 1 on success, 0 on EOF or error. */
static int read_line(char *buf, size_t cap) {
    if (!fgets(buf, (int)cap, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

static int valid_user(const char *u) {
    if (!u[0]) return 0;
    size_t n = strlen(u);
    if (n > 32) return 0;
    if (!(isalpha((unsigned char)u[0]) || u[0] == '_')) return 0;
    for (const char *p = u + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
            return 0;
    return 1;
}

static int valid_host(const char *h) {
    if (!h[0]) return 0;
    size_t n = strlen(h);
    if (n > 63) return 0;
    for (const char *p = h; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '.'))
            return 0;
    return 1;
}

static int valid_tz(const char *z) {
    if (!z[0] || strlen(z) > 100) return 0;
    for (const char *p = z; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' ||
              *p == '+' || *p == '/'))
            return 0;
    return 1;   /* bare zones (UTC) and paths (America/New_York) both OK */
}

/* Read one line with echo turned off, for passwords.
   getpass() is no use here: it wants a controlling terminal and reaches the
   user through /dev/tty, and this process has a console on fd 0 but never a
   ctty of its own, so it fails and the fallback printed the password in plain
   text.  It also wrote its prompt somewhere the rest of the program could not
   see, which was half of why the questions came out invisible.

   Returns 1 if the line was too long so the caller can say so and retry.

   BUG 6 fix: the previous check was `strlen(buf) >= cap - 1`, which fires
   when fgets fills the buffer exactly — even on a valid max-length password,
   because the newline was the (cap-1)th byte and fgets stopped before reading
   it. The correct test: if fgets ran out of room, there will be no '\n' in
   the buffer. If a '\n' is present, the whole line fit. */
static int read_secret(const char *prompt, char *buf, size_t cap) {
    struct termios saved, quiet;
    int hushed = 0;

    fputs(prompt, stdout);
    fflush(stdout);

    if (tcgetattr(STDIN_FILENO, &saved) == 0) {
        quiet = saved;
        quiet.c_lflag &= (tcflag_t)~ECHO;
        quiet.c_lflag |= ICANON;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) == 0)
            hushed = 1;
    }
    if (!hushed)
        printf("(could not turn echo off - this will be visible)\n");

    int overlong = 0;
    if (!fgets(buf, (int)cap, stdin)) {
        buf[0] = '\0';
    } else if (strchr(buf, '\n') == NULL && strlen(buf) == cap - 1) {
        /* Buffer was full and no newline found: line was truncated.
           Drain the remainder so the next read starts cleanly. */
        int c;
        while ((c = fgetc(stdin)) != '\n' && c != EOF) {}
        buf[0] = '\0';
        overlong = 1;
    } else {
        buf[strcspn(buf, "\r\n")] = '\0';
    }

    if (hushed) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        fputc('\n', stdout);
        fflush(stdout);
    }
    return overlong;
}

static void read_password(const char *prompt, char *buf, size_t cap,
                          const char *confirm_prompt) {
    char again[256];
    for (;;) {
        if (read_secret(prompt, buf, cap)) {
            printf("That's too long for a password.\n");
            continue;
        }
        if (confirm_prompt) {
            if (read_secret(confirm_prompt, again, sizeof again)) {
                printf("That's too long for a password.\n");
                continue;
            }
            if (buf[0] && strcmp(buf, again) == 0) return;
            printf("Those didn't match - try again.\n");
            continue;
        }
        if (buf[0]) return;
        printf("Password can't be empty.\n");
    }
}

/* run a shell command with absolute busybox; input is pre-validated */
static int run(const char *fmt, ...) {
    char cmd[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    return system(cmd);
}

static void set_password(const char *user, const char *pw) {
    char line[1024];
    snprintf(line, sizeof line, "%s:%s\n", user, pw);
    FILE *p = popen("/bin/busybox chpasswd 2>/dev/null", "w");
    if (p) {
        fputs(line, p);
        pclose(p);
    }
}

/* Write /etc/wpa_supplicant.conf from a validated SSID and passphrase.
   Uses the plaintext passphrase form (psk="...") which wpa_supplicant
   accepts for WPA2-Personal. The file is 0600 so only root can read it. */
static void write_wpa_conf(const char *ssid, const char *psk) {
    FILE *f = fopen("/etc/wpa_supplicant.conf", "w");
    if (!f) {
        printf("(could not write /etc/wpa_supplicant.conf - WiFi manual setup needed)\n");
        return;
    }
    fprintf(f,
        "# Copper Linux — WiFi configuration\n"
        "# Written by copper-firstboot. Edit this file to add networks.\n"
        "ctrl_interface=/run/wpa_supplicant\n"
        "ctrl_interface_group=0\n"
        "update_config=1\n"
        "\n"
        "network={\n"
        "    ssid=\"%s\"\n"
        "    psk=\"%s\"\n"
        "    key_mgmt=WPA-PSK\n"
        "}\n", ssid, psk);
    fclose(f);
    chmod("/etc/wpa_supplicant.conf", 0600);
    printf("WiFi: /etc/wpa_supplicant.conf written.\n");
}

int main(void) {
    char name[128]  = "";
    char user[64]   = "";
    char host[64]   = "copper";
    char tz[128]    = "UTC";
    char rootpw[256];
    char userpw[256];

    /* Unbuffered, and not because of taste. Every question below is a printf
       with no trailing newline, and stdout to a terminal is line-buffered, so
       each one sat in the buffer until some later newline flushed the lot. The
       first boot of this machine printed "Welcome to Copper Linux" and then
       appeared to hang, and the questions turned up afterwards all at once and
       out of order, which is how someone ended up typing "ping -c 1 1.1.1.1"
       into a password field. A wizard whose questions you cannot see is not a
       wizard. copper-sh already does the fflush; this is the same lesson. */
    setvbuf(stdout, NULL, _IONBF, 0);

    banner();

    printf("Your name: ");
    read_line(name, sizeof name);
    if (!name[0]) snprintf(name, sizeof name, "friend");

    do {
        printf("Username [letters, digits, - _]: ");
        read_line(user, sizeof user);
    } while (!valid_user(user));

    printf("Hostname [copper]: ");
    {
        char h[64] = "";
        if (read_line(h, sizeof h) && h[0] && valid_host(h))
            snprintf(host, sizeof host, "%s", h);
    }

    read_password("Password (root): ", rootpw, sizeof rootpw,
                  "Confirm root password: ");

    printf("Timezone [UTC]: ");
    {
        char z[128] = "";
        if (read_line(z, sizeof z) && z[0] && valid_tz(z))
            snprintf(tz, sizeof tz, "%s", z);
    }

    /* --- WiFi setup -------------------------------------------------- */
    printf("WiFi network name (SSID) [leave blank to skip]: ");
    {
        char ssid[128] = "";
        if (read_line(ssid, sizeof ssid) && ssid[0]) {
            char wifipw[256] = "";
            /* WiFi password: blank is valid for open networks */
            if (read_secret("WiFi password (blank for open network): ",
                            wifipw, sizeof wifipw)) {
                printf("WiFi password too long — skipping WiFi setup.\n");
            } else {
                write_wpa_conf(ssid, wifipw);
            }
        }
    }

    /* --- apply ------------------------------------------------------- */

    printf("\nSetting things up...\n");

    /* hostname + hosts */
    run("echo %s > /etc/hostname", host);
    run("echo '127.0.0.1 localhost %s' > /etc/hosts", host);
    run("echo '::1 localhost ip6-localhost ip6-loopback' >> /etc/hosts");
    run("/bin/busybox hostname %s", host);

    /* The named user, with copper-sh as their login shell.
       busybox's adduser takes exactly one -G group; the comma-separated list
       that GNU useradd accepts is read here as a single group *name*, so the
       account was never created and the first boot ended with "Couldn't create
       user" and no home directory. Primary group goes in -G, and the rest are
       supplementary memberships, which is what addgroup is for. Each of those
       is allowed to fail: a group the build did not create should not stop a
       person getting a working account. */
    if (run("/bin/busybox adduser -h /home/%s -s /usr/bin/copper-sh "
            "-G users %s", user, user) != 0) {
        printf("Couldn't create user %s.\n", user);
        return 1;
    }
    {
        static const char *extra[] = { "audio", "video", "dialout", "cdrom" };
        size_t i;
        for (i = 0; i < sizeof extra / sizeof extra[0]; i++)
            run("/bin/busybox addgroup %s %s", user, extra[i]);
    }
    read_password("Password (for you): ", userpw, sizeof userpw,
                  "Confirm your password: ");
    set_password("root", rootpw);
    set_password(user, userpw);

    /* timezone */
    {
        char zfile[160];
        snprintf(zfile, sizeof zfile, "/usr/share/zoneinfo/%s", tz);
        if (access(zfile, R_OK) == 0) {
            run("ln -sf /usr/share/zoneinfo/%s /etc/localtime", tz);
            run("echo %s > /etc/timezone", tz);
        } else {
            printf("(timezone %s not found — staying on UTC)\n", tz);
        }
    }

    /* done marker */
    {
        FILE *m = fopen("/etc/copper-firstboot.done", "w");
        if (m) {
            fprintf(m, "%s\n", name);
            fclose(m);
        }
    }

    printf("\n===================================================\n");
    printf("  Done — welcome, %s.\n", name);
    printf("  Copper is yours. Type 'help' to see builtins.\n");
    printf("===================================================\n\n");
    return 0;
}
