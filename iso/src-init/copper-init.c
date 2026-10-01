/*
 * copper-init — Copper Linux PID 1.
 * handcrafted by 12hrformat
 *
 * No systemd, no init scripts: this IS the init. It mounts the basics
 * (the initramfs already did most of it), applies the hostname, brings the
 * network up (DHCP or static, wired or WiFi), runs the first-boot wizard
 * once, then parks a copper-sh login shell on tty1 and keeps it alive.
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E   /* stable Linux value, in case musl is shy */
#endif

static void console_stdio(void) {
    int fd = open("/dev/console", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }
}

static void mount_if_needed(const char *what, const char *where,
                            const char *type) {
    struct stat st;
    if (stat(where, &st) != 0 || !S_ISDIR(st.st_mode))
        mkdir(where, 0755);
    if (mount(what, where, type, 0, NULL) != 0 && errno != EBUSY)
        perror(where);
}

static void apply_hostname(void) {
    char host[128] = "copper";
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(host, sizeof host, f))
            host[strcspn(host, "\r\n")] = '\0';
        fclose(f);
    }
    sethostname(host, strlen(host));
}

/* Boot chatter. Goes to the console everyone is already looking at, and to
   the serial port when the machine has one. Both, not either: /dev/console
   only ever points at one of them — whichever console= was last on the kernel
   command line — so writing to stdout alone means a quiet boot leaves a serial
   log completely empty, which is precisely when a log is most wanted. */
static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    FILE *tty;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    printf("copper: %s\n", line);
    fflush(stdout);
    tty = fopen("/dev/ttyS0", "w");
    if (tty) {
        fprintf(tty, "copper: %s\n", line);
        fclose(tty);
    }
}

/* ARPHRD_* link-layer types, from linux/if_arp.h. Spelled out rather than
   included: this builds against musl, which does not ship the kernel UAPI
   headers, and two numbers are not worth a build dependency. */
#define ARPHRD_ETHER             1
#define ARPHRD_IEEE80211_RADIOTAP 801

/* Link-layer type of an interface, or -1 if it cannot be read.
   /sys/class/net/<if>/type is the ARPHRD_* value in decimal. */
static int iface_type(const char *ifname) {
    char path[64];
    char buf[32];
    FILE *f;
    long v;

    snprintf(path, sizeof path, "/sys/class/net/%s/type", ifname);
    f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    v = strtol(buf, NULL, 10);
    if (v < 0 || v > 0xffff)
        return -1;
    return (int)v;
}

/* First interface worth handing to DHCP, or NULL.
   Not simply the first non-loopback name in the directory: with MODULES=n
   every driver is built in, and several of them invent an interface at boot
   before the real NIC has finished probing. The sit module's sit0 is one,
   and handing DHCP a tunnel means broadcasting discover into nowhere. So ask
   sysfs what the interface actually is, and only take something with a real
   link layer. Wired first, wireless second — a box with both should use the
   wire. */
static const char *first_nonloop_iface(void) {
    static const int prefs[] = { ARPHRD_ETHER, ARPHRD_IEEE80211_RADIOTAP };
    static char name[IFNAMSIZ];
    struct dirent *ent;
    DIR *dir;
    size_t pass;

    for (pass = 0; pass < sizeof prefs / sizeof prefs[0]; pass++) {
        dir = opendir("/sys/class/net");
        if (!dir)
            return NULL;
        while ((ent = readdir(dir)) != NULL) {
            size_t len = strlen(ent->d_name);
            /* d_name is far wider than an interface name, so anything that
               long is not one. */
            if (len == 0 || len >= sizeof name || ent->d_name[0] == '.')
                continue;
            if (iface_type(ent->d_name) != prefs[pass])
                continue;
            memcpy(name, ent->d_name, len + 1);
            closedir(dir);
            return name;
        }
        closedir(dir);
    }
    return NULL;
}

/* IFF_UP through ioctl: no subprocess, and no guessing where the build
   happened to install busybox's applet links. */
static int link_up(const char *ifname) {
    struct ifreq ifr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int rc = 0;

    if (sock < 0)
        return -1;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
        rc = -1;
    else {
        ifr.ifr_flags |= IFF_UP | IFF_BROADCAST;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
            rc = -1;
    }
    close(sock);
    return rc;
}

static void start_dhcp(const char *ifname) {
    char pidfile[64];
    pid_t pid = fork();

    if (pid != 0)
        return;
    /* udhcpc passes its own environment to the lease script and never sets
       PATH itself, so the script needs one to find ip(8). */
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    snprintf(pidfile, sizeof pidfile, "/run/udhcpc.%s.pid", ifname);
    execl("/sbin/udhcpc", "udhcpc", "-i", ifname, "-b", "-p", pidfile,
          (char *)NULL);
    _exit(127);
}

/* -------------------------------------------------------------------- */
/* G3 — Static IP configuration                                          */
/* -------------------------------------------------------------------- */

/* Dotted-quad netmask → prefix length.  255.255.255.0 → 24.
   Returns the prefix [0..32] or -1 if the mask is not a valid contiguous
   run of ones. */
static int mask_to_prefix(const char *mask) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(mask, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255)            return -1;
    unsigned long m = (a << 24) | (b << 16) | (c << 8) | d;
    /* A valid netmask has all 1-bits before all 0-bits — check for gaps. */
    if (m != 0 && (m & (~m >> 1)) != 0) return -1;
    int n = 0;
    while (m & 0x80000000UL) { n++; m <<= 1; }
    return n;
}

struct netcfg {
    char iface[IFNAMSIZ];
    int  is_static;       /* 1 if method=static was set */
    char address[64];
    int  prefix;          /* /N prefix length */
    char gateway[64];
    char dns[256];        /* space-separated nameservers */
};

/*
 * Parse /etc/network/interfaces.  Format:
 *
 *   iface eth0
 *       method static          # or: method dhcp  (default: dhcp)
 *       address 192.168.1.100
 *       netmask 255.255.255.0  # OR: prefix 24
 *       gateway 192.168.1.1
 *       dns 1.1.1.1 9.9.9.9   # space-separated
 *
 * A missing file, or a file with method=dhcp (or no method line), means
 * DHCP and parse_netcfg returns 0.  A valid static stanza returns 1 and
 * fills in *cfg.
 */
static int parse_netcfg(struct netcfg *cfg) {
    FILE *f = fopen("/etc/network/interfaces", "r");
    if (!f) return 0;

    memset(cfg, 0, sizeof *cfg);
    char line[256];
    int in_iface  = 0;
    int is_static = 0;

    while (fgets(line, sizeof line, f)) {
        /* strip leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        /* strip trailing whitespace / newline */
        size_t n = strlen(p);
        while (n > 0 && (p[n-1] == '\n' || p[n-1] == '\r' ||
                         p[n-1] == ' '  || p[n-1] == '\t'))
            p[--n] = '\0';
        if (!*p || *p == '#') continue;

        if (strncmp(p, "iface ", 6) == 0) {
            strncpy(cfg->iface, p + 6, IFNAMSIZ - 1);
            cfg->iface[IFNAMSIZ - 1] = '\0';
            in_iface  = 1;
            is_static = 0;
            continue;
        }
        if (!in_iface) continue;

        if (strcmp(p, "method static") == 0) { is_static = 1; continue; }
        if (strcmp(p, "method dhcp")   == 0) { is_static = 0; continue; }

        if (strncmp(p, "address ", 8) == 0)
            strncpy(cfg->address, p + 8, sizeof cfg->address - 1);
        else if (strncmp(p, "netmask ", 8) == 0) {
            int plen = mask_to_prefix(p + 8);
            if (plen >= 0) cfg->prefix = plen;
        }
        else if (strncmp(p, "prefix ", 7) == 0)
            cfg->prefix = atoi(p + 7);
        else if (strncmp(p, "gateway ", 8) == 0)
            strncpy(cfg->gateway, p + 8, sizeof cfg->gateway - 1);
        else if (strncmp(p, "dns ", 4) == 0)
            strncpy(cfg->dns, p + 4, sizeof cfg->dns - 1);
    }
    fclose(f);

    if (is_static && cfg->address[0] && cfg->prefix > 0) {
        cfg->is_static = 1;
        return 1;
    }
    return 0;
}

/* Fork+exec a command with a NULL-terminated argument list. Waits for it. */
static void run_cmd(const char *prog, ...) {
    const char *args[32];
    va_list ap;
    va_start(ap, prog);
    int n = 0;
    args[n++] = prog;
    const char *arg;
    while ((arg = va_arg(ap, const char *)) != NULL && n < 31)
        args[n++] = arg;
    args[n] = NULL;
    va_end(ap);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], (char *const *)args);
        _exit(127);
    }
    if (pid > 0) { int st; waitpid(pid, &st, 0); }
}

static void apply_static(const struct netcfg *cfg) {
    char cidr[80];
    snprintf(cidr, sizeof cidr, "%s/%d", cfg->address, cfg->prefix);

    say("static: %s on %s", cidr, cfg->iface);
    run_cmd("ip", "addr", "add", cidr, "dev", cfg->iface, NULL);

    if (cfg->gateway[0]) {
        say("static: default route via %s", cfg->gateway);
        run_cmd("ip", "route", "add", "default", "via", cfg->gateway,
                "dev", cfg->iface, NULL);
    }

    /* Write resolv.conf from the dns field (space-separated servers). */
    if (cfg->dns[0]) {
        FILE *rc = fopen("/etc/resolv.conf", "w");
        if (rc) {
            char copy[256];
            strncpy(copy, cfg->dns, sizeof copy - 1);
            copy[sizeof copy - 1] = '\0';
            char *save = NULL;
            char *tok = strtok_r(copy, " \t", &save);
            while (tok) {
                say("static: nameserver %s", tok);
                fprintf(rc, "nameserver %s\n", tok);
                tok = strtok_r(NULL, " \t", &save);
            }
            fclose(rc);
        }
    }
}

/* -------------------------------------------------------------------- */
/* G4 — WiFi: wpa_supplicant + wait for association                     */
/* -------------------------------------------------------------------- */

/* Start wpa_supplicant in the background on a wifi interface.
   Only runs if /etc/wpa_supplicant.conf exists — written by the firstboot
   wizard.  If the file is absent the interface is left alone, so DHCP will
   still run and will either pick up an open network or time out gracefully. */
static void start_wpa(const char *ifname) {
    if (access("/etc/wpa_supplicant.conf", R_OK) != 0) {
        say("wifi: no /etc/wpa_supplicant.conf — skipping association");
        return;
    }

    say("wifi: starting wpa_supplicant on %s", ifname);
    /* ctrl_interface directory: wpa_supplicant creates the socket here. */
    mkdir("/run/wpa_supplicant", 0700);

    pid_t pid = fork();
    if (pid != 0) return;   /* parent continues */
    execl("/usr/sbin/wpa_supplicant", "wpa_supplicant",
          "-B",                          /* background after startup */
          "-i", ifname,
          "-c", "/etc/wpa_supplicant.conf",
          (char *)NULL);
    _exit(127);
}

/* Poll /sys/class/net/<if>/operstate until it reads "up" or timeout_sec
   elapses.  Returns 1 on association, 0 on timeout. */
static int wait_assoc(const char *ifname, int timeout_sec) {
    char path[64];
    char buf[32];
    snprintf(path, sizeof path, "/sys/class/net/%s/operstate", ifname);
    for (int i = 0; i < timeout_sec * 10; i++) {
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(buf, sizeof buf, f) && strncmp(buf, "up", 2) == 0) {
                fclose(f);
                return 1;
            }
            fclose(f);
        }
        usleep(100 * 1000);  /* 100 ms */
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Main network bringup                                                   */
/* -------------------------------------------------------------------- */

/* Nobody has logged in yet, but the box should already be online: raise the
   interface and let DHCP sort out the address, the default route and the
   resolver. Runs in the background, so a slow or absent DHCP server never
   holds up the first-boot wizard.
   G3: if /etc/network/interfaces has a static stanza for this interface,
       apply it and skip DHCP entirely.
   G4: if the interface is WiFi, run wpa_supplicant first and wait up to
       15 s for association before handing off to DHCP. */
static void bring_up_network(void) {
    char ifname[IFNAMSIZ] = "";
    int tries;

    /* The kernel is done probing before it runs us, but a freshly attached
       VMware NIC can land a moment later. Give the bus a couple of seconds
       before deciding this machine has no network. */
    for (tries = 0; tries < 20 && !ifname[0]; tries++) {
        const char *found = first_nonloop_iface();
        if (found)
            snprintf(ifname, sizeof ifname, "%s", found);
        else
            usleep(100 * 1000);
    }
    if (!ifname[0]) {
        say("no network interface, skipping");
        return;
    }
    if (link_up(ifname) != 0) {
        say("could not bring up %s, skipping", ifname);
        return;
    }

    int is_wifi = (iface_type(ifname) == ARPHRD_IEEE80211_RADIOTAP);

    /* G3: check for a static config that names this interface. */
    struct netcfg cfg;
    if (parse_netcfg(&cfg) && strcmp(cfg.iface, ifname) == 0) {
        apply_static(&cfg);
        return;   /* static address configured — DHCP not needed */
    }

    /* G4: WiFi needs wpa_supplicant before DHCP can get a lease. */
    if (is_wifi) {
        start_wpa(ifname);
        say("wifi: waiting for association (up to 15 s)");
        if (!wait_assoc(ifname, 15))
            say("wifi: no association yet — DHCP will retry in background");
    }

    say("%s is up, asking DHCP for an address", ifname);
    start_dhcp(ifname);
}

/* Start a login shell on a tty. Returns its pid, or -1 if the fork failed. */
static pid_t spawn_tty(int tty) {
    pid_t pid = fork();
    if (pid != 0) return pid;

    setsid();
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/tty%d", tty);
    int fd = open(dev, O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        ioctl(fd, TIOCSCTTY, 0);
        if (fd > 2) close(fd);
    }
    execl("/usr/bin/copper-sh", "copper-sh", (char *)NULL);
    execl("/bin/sh", "sh", (char *)NULL);
    _exit(1);
}

int main(void) {
    console_stdio();

    /* Anything spawned from here inherits this, including the login shell, so
       it has to be right. busybox's applets live in /bin and /sbin; the real
       GNU tools live in /usr/bin and /usr/sbin, and PATH order decides which
       one runs. /bin first means busybox wins and the userland is invisible. */
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    mount_if_needed("proc", "/proc", "proc");
    mount_if_needed("sysfs", "/sys", "sysfs");
    mount_if_needed("devtmpfs", "/dev", "devtmpfs");
    mount_if_needed("tmpfs", "/run", "tmpfs");

    apply_hostname();

    /* Started before the wizard on purpose: DHCP / wpa_supplicant get to
       negotiate while the user is still typing their name. */
    bring_up_network();

    struct stat st_done;
    if (stat("/etc/copper-firstboot.done", &st_done) != 0) {
        pid_t wiz = fork();
        if (wiz == 0) {
            execl("/usr/bin/copper-firstboot", "copper-firstboot",
                  (char *)NULL);
            _exit(1);
        }
        int wst;
        waitpid(wiz, &wst, 0);
    }

    /* One shell on tty1, and when *it* exits, another. Waiting on any child
       with waitpid(-1, …) instead meant udhcpc counted too: it is a child of
       PID 1, it forks the lease script and, with -b, exits a process of its
       own accord. Every one of those woke the loop and started a second
       copper-sh, so the first boot printed the banner twice and left two
       prompts fighting over one tty. Wait on the shell's own pid. */
    pid_t shell = spawn_tty(1);
    for (;;) {
        int st;
        if (shell <= 0) {
            /* fork failed; do not spin */
            sleep(1);
            shell = spawn_tty(1);
            continue;
        }
        while (waitpid(shell, &st, 0) < 0 && errno == EINTR)
            ;
        say("shell on tty1 exited, starting another");
        shell = spawn_tty(1);
    }
    return 0;                        /* never reached */
}