#!/usr/bin/env bash
# Copper Linux — handcrafted by 12hrformat
# build.sh — assemble Copper Linux, a from-source Linux distro, into a
# bootable live ISO. No Debian/Arch packages: every shipped binary is built
# from upstream source in this script (kernel, musl, busybox, coreutils and
# friends) plus Copper's own pieces (copper-sh, copper-init, first-boot
# wizard).
#
# Usage:
#   sudo bash iso/build.sh               # everything, in order
#   sudo bash iso/build.sh <stage>       # one stage only
#
# Stages: kernel | base | tools | copper | rootfs | initramfs | iso
#
# Stages share iso/work/, so CI can run them one step at a time and each
# step skips straight past whatever is already built.

set -euo pipefail

# Every stage below stamps itself with a hash of this script, because that is
# where the kernel version and the config flags live. $0 is whatever was
# typed on the command line — CI runs `sudo bash iso/build.sh kernel`, so it
# is the relative "iso/build.sh" — and the cd on the next line stops it
# resolving. Pin it down first.
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
cd "$(dirname "$0")"
ROOT=$(pwd)
WORK="$ROOT/work"; OUT="$ROOT/out"; DL="$WORK/downloads"
SYS="$WORK/sys"            # our toolchain prefix (musl + musl-gcc)
SYSROOT="$WORK/sysroot"    # staging area for cross-built link-time libs (not shipped in the ISO)
TGT="$WORK/rootfs"         # copper rootfs staging tree
JOBS=${JOBS:-$(nproc)}
export MAKEFLAGS="-j$JOBS"
# coreutils' configure refuses to run as root; CI builds via sudo, so
# pass its documented bypass (cross checks have no runtime step anyway).
export FORCE_UNSAFE_CONFIGURE=1

# Kernel version to build. Pinned LTS on purpose — distro builds should be
# reproducible, not "latest at build time". Bump this when we want a newer
# LTS; override with KREL=6.6.x if you need a different series.
KREL=${KREL:-6.12.10}
KPATH="v${KREL%%.*}.x"

STAGE=${1:-all}

[ "$(id -u)" = 0 ] || { echo "build.sh: run with sudo/root"; exit 1; }
command -v curl >/dev/null || { echo "build.sh: need curl"; exit 1; }

mkdir -p "$DL" "$OUT" "$SYS"/{bin,lib} \
  "$SYSROOT"/usr/{lib,include} \
  "$TGT"/{bin,sbin,usr/bin,usr/sbin,usr/share,etc,dev,proc,sys,run,tmp,home,root,var/log,mnt,boot}

# The scripts we ship are read by busybox ash on a machine with no shell
# anybody can ssh into, so a typo in one is found the hard way. Parse them
# here, where failing costs a second instead of a boot.
lint_scripts() {
  local s
  for s in "$ROOT/live/init" "$ROOT/rootfs-overlay/usr/share/udhcpc/default.script"; do
    sh -n "$s" || { echo "build.sh: syntax error in $s"; exit 1; }
  done
  echo "scripts: live/init and the udhcpc lease script parse clean"
}

lint_scripts

# ---- stage caching -----------------------------------------------------
# iso/work/ is cached between runs, and the CI cache deliberately falls back
# to the most recent older tree so the expensive kernel build survives an
# unrelated change. That makes a stale stage easy: "does the output file
# exist" is just as true for a tree built from sources that have since
# changed. This build shipped exactly that once — an ISO whose initrd was
# packed from the previous version of iso/live/init, so the overlay
# directories were missing and the first boot died on a valid-looking
# artifact. So every stage stamps its output with a hash of the inputs that
# produced it, and only skips when that still matches. A warm cache buys
# speed now; it can't be wrong.
stamped_skip() {   # stamped_skip <stamp> <input>...
  local stamp="$1" want f; shift
  # An input we can't read means we can't know whether the stage is current.
  # Rebuild rather than guess, and say which file is missing.
  for f in "$@"; do
    [ -r "$f" ] || { echo "build.sh: cannot read stamp input $f" >&2; return 1; }
  done
  want=$(cat "$@" | sha256sum | cut -d' ' -f1)
  [ -s "$stamp" ] && [ "$(cat "$stamp" 2>/dev/null)" = "$want" ]
}

stamp_set() {      # stamp_set <stamp> <input>...
  local stamp="$1" f missing=0; shift
  for f in "$@"; do
    [ -r "$f" ] || { echo "build.sh: cannot read stamp input $f" >&2; missing=1; }
  done
  if [ "$missing" -ne 0 ]; then
    # Leave the stamp empty rather than recording a hash of half the inputs.
    # An empty stamp never matches, so this stage rebuilds next time too.
    : > "$stamp"
    return 0
  fi
  cat "$@" | sha256sum | cut -d' ' -f1 > "$stamp"
}

# The ISO is a copy of the entire staged rootfs, so its inputs are the tree
# itself rather than any one file in it. Run from inside the tree so the
# hashed paths are relative — absolute ones carry the checkout directory and
# would make the stamp differ between runners for no reason.
tree_hash() {      # tree_hash <dir>
  ( cd "$1" && find . -type f -exec sha256sum {} + | LC_ALL=C sort ) \
    | sha256sum | cut -d' ' -f1
}

fetch() {                   # fetch url -> prints tarball path (stdout only)
  local url="$1"
  local f="$DL/${1##*/}"
  [ -s "$f" ] && { echo "$f"; return; }
  echo "  < $url" >&2
  curl -fL --retry 3 --retry-delay 2 -o "$f" "$url"
  [ -s "$f" ] || { echo "fetch failed: $url" >&2; exit 1; }
  echo "$f"
}

unpack() {                  # unpack tarball -> prints its dir
  local f="$1"
  local d="${f%.tar.*}"
  [ -d "$d" ] || tar -xf "$f" -C "$DL"
  echo "$d"
}

# ---------------------------------------------------------------
# 1. Linux kernel, from kernel.org, with Copper's .config subset
# ---------------------------------------------------------------

# A kernel that boots to a black screen, or that comes up with no NIC able to
# speak DHCP, is miserable to debug from inside a VM — you never see a build
# error, just a dead machine. So the options Copper actually leans on are
# checked after kconfig has had its say, and a missing one fails the build
# here with a readable list instead.
#
# Only symbols that really exist in $KREL belong in this list. 6.12 dropped
# `ETHERNET` (the driver menu is unconditional once NET is on) and moved
# BLK_DEV_NVME to drivers/nvme/host, so grepping the old paths lies.
require_kernel_config() {
  local cfg="$1" sym missing=""
  for sym in \
      DEVTMPFS DEVTMPFS_MOUNT TMPFS OVERLAY_FS ISO9660_FS BLK_DEV_SR \
      VT VGA_CONSOLE UNIX98_PTYS \
      EXT4_FS BLK_DEV_SD ATA ATA_PIIX BLK_DEV_NVME \
      VIRTIO_PCI VIRTIO_BLK \
      NET NETDEVICES INET PACKET UNIX E1000 E1000E VIRTIO_NET \
      WIRELESS CFG80211 MAC80211 MT7921E ; do
    grep -qx "CONFIG_$sym=y" "$cfg" || missing="$missing $sym"
  done
  if [ -n "$missing" ]; then
    echo "kernel: these options did not survive olddefconfig:$missing" >&2
    exit 1
  fi
  echo "kernel: config looks fit to boot and to reach the network"
}

# CONFIG_FOO=y only says kconfig agreed. This says the code ended up in the
# image: System.map lists every symbol linked into vmlinux, uncompressed, and
# a driver that failed to link is invisible there even though the config says
# otherwise. The bzImage we ship is compressed, so this cannot be checked from
# the ISO afterwards - it has to be checked here, while the tree still exists.
require_kernel_symbols() {
  local map="$1"; shift
  local sym missing=""
  if [ ! -r "$map" ]; then
    echo "kernel: $map is missing, cannot confirm symbols linked" >&2
    exit 1
  fi
  for sym in "$@"; do
    grep -q " $sym\$\| $sym\\." "$map" || missing="$missing $sym"
  done
  if [ -n "$missing" ]; then
    echo "kernel: these symbols are not in System.map, so they did not link:$missing" >&2
    exit 1
  fi
  echo "kernel: linked, as promised: $*"
}

build_kernel() {
  # The kernel's real inputs are the version and the scripts/config flags
  # below, and all of those live in this script, hence $SELF.
  if [ -s "$TGT/boot/vmlinuz" ] && stamped_skip "$WORK/kernel.stamp" "$SELF"; then
    echo "kernel: already built, skipping"; return
  fi
  echo "==> kernel $KREL"
  local KT KD
  KT=$(fetch "https://cdn.kernel.org/pub/linux/kernel/$KPATH/linux-$KREL.tar.xz")
  KD=$(unpack "$KT")
  pushd "$KD" >/dev/null
    make defconfig
    scripts/config --disable MODULES \
      --enable ISO9660_FS --enable OVERLAY_FS --enable TMPFS \
      --enable DEVTMPFS --enable DEVTMPFS_MOUNT \
      --enable UNIX98_PTYS --enable LEGACY_PTYS \
      --enable VIRTIO_PCI --enable VIRTIO_BLK --enable VIRTIO_NET \
      --enable E1000 --enable E1000E \
      --enable VMXNET3 --enable VMWARE_VMXNET3 \
      --enable ATA --enable ATA_PIIX --enable BLK_DEV_SD --enable BLK_DEV_NVME \
      --enable EXT4_FS --enable PACKET --enable UNIX --enable VT \
      --enable VGA_CONSOLE --enable INPUT \
      --enable WIRELESS --enable WIRELESS_EXT --enable CFG80211 \
      --enable MAC80211 --enable RFKILL --enable RFKILL_INPUT \
      --enable MT7921E --enable MT7921U
    # vmxnet3 was renamed at some point around 6.12; asking for both names
    # costs nothing, since kconfig drops whichever one doesn't exist.
    # The wireless set is the same bargain, and the same trap: MT7921E is the
    # PCIe variant (what the test laptop has) and MT7921U the USB one, and both
    # `depends on MAC80211`, so without CFG80211 and MAC80211 above, kconfig
    # silently drops both and the kernel comes up with no wifi and no error.
    make olddefconfig
    require_kernel_config "$KD/.config"
    make -j"$JOBS" bzImage
    # CONFIG_MT7921E=y is necessary but not sufficient: kconfig can be satisfied
    # while the driver is dropped, and the shipped vmlinuz cannot be searched
    # from outside because a bzImage payload is compressed. System.map is the
    # symbol table, uncompressed, and it is the only place that shows whether
    # the driver actually linked into the image we are about to ship.
    # Only non-static names may be insisted on, and that is not a stylistic
    # point: a static function is invisible to System.map whether or not it
    # linked, so naming one here fails the build over something that was never
    # going to appear. The obvious candidates - mt7921_pci_probe,
    # mt7921e_init_reset, mt7921_dma_init - are all `static` in 6.12, and the
    # first version of this gate named them and failed on a kernel that had
    # the driver in it perfectly well.
    require_kernel_symbols "$KD/System.map" \
      mt7921_mcu_parse_response mt7921_rx_check mt7921_queue_rx_skb
    cp arch/x86/boot/bzImage "$TGT/boot/vmlinuz"
  popd >/dev/null
  [ -s "$TGT/boot/vmlinuz" ] || { echo "kernel build failed"; exit 1; }
  stamp_set "$WORK/kernel.stamp" "$SELF"
}

# ---------------------------------------------------------------
# 2. musl libc (our C library, compiled from source)
# ---------------------------------------------------------------
build_musl() {
  if [ ! -x "$SYS/bin/musl-gcc" ]; then
    echo "==> musl"
    local MT MD
    MT=$(fetch "https://musl.libc.org/releases/musl-1.2.5.tar.gz")
    MD=$(unpack "$MT")
    pushd "$MD" >/dev/null
      CC=gcc ./configure --prefix="$SYS" --disable-shared
      make -j"$JOBS"; make install
    popd >/dev/null
    [ -x "$SYS/bin/musl-gcc" ] || { echo "musl build failed"; exit 1; }
  fi
  # everything userland from here on is static musl binaries
  export PATH="$SYS/bin:$PATH"
  export CC=musl-gcc
  export CFLAGS="-static -O2"
  export LDFLAGS="-static"
}

# busybox ships no scripts/config (that's a kernel tool), so enable the
# symbols we need via the kernel tree's copy, which edits .config in
# place. Appending CONFIG_* lines instead would duplicate the defaults
# that make defconfig already wrote — conf rejects those as "reassign"
# (first assignment wins, so CONFIG_STATIC=y got silently dropped) and a
# redundant write into a kconfig choice corrupts its state.
set_bb_config() {
  local sym="$1"
  local kcfg="$DL/linux-$KREL/scripts/config"
  if [ -x "$kcfg" ]; then
    "$kcfg" -e "$sym"
  else
    sed -i "s|^# CONFIG_$sym is not set$|CONFIG_$sym=y|" .config
    grep -q "^CONFIG_$sym=y$" .config || echo "CONFIG_$sym=y" >> .config
  fi
}

set_bb_config_off() {
  local sym="$1"
  local kcfg="$DL/linux-$KREL/scripts/config"
  if [ -x "$kcfg" ]; then
    "$kcfg" -d "$sym"
  else
    sed -i "s|^CONFIG_$sym=y$|# CONFIG_$sym is not set|" .config
  fi
}

# Copper runs its init, its shell and its network setup out of this busybox,
# so make sure the applets we lean on really are in there. `make defconfig`
# on busybox means "whatever the Kconfig defaults say", which is easy to
# break by bumping the version.
require_bb_config() {
  local cfg="$1" sym missing=""
  for sym in \
      STATIC ASH \
      UDHCPC FEATURE_UDHCPC_ARPING IP IFCONFIG ROUTE PING \
      WGET FEATURE_WGET_HTTPS NSLOOKUP \
      MOUNT SWITCH_ROOT HOSTNAME \
      ADDUSER ADDGROUP FEATURE_ADDUSER_TO_GROUP \
      CHPASSWD FEATURE_SHADOWPASSWDS ; do
    grep -qx "CONFIG_$sym=y" "$cfg" || missing="$missing $sym"
  done
  if [ -n "$missing" ]; then
    echo "busybox: missing applets we depend on:$missing" >&2
    exit 1
  fi
}

# ---------------------------------------------------------------
# 3. busybox — base utilities, ash, adduser, chpasswd, mount, ...
# ---------------------------------------------------------------
build_busybox() {
  if [ -x "$TGT/bin/busybox" ] && stamped_skip "$WORK/busybox.stamp" "$SELF"; then
    echo "busybox: already built, skipping"; return
  fi
  echo "==> busybox"
  local BT BD
  BT=$(fetch "https://busybox.net/downloads/busybox-1.36.1.tar.bz2")
  BD=$(unpack "$BT")
  pushd "$BD" >/dev/null
    make defconfig
    # static, plus the applets the first-boot wizard and init rely on.
    # Applet links stay at the defconfig default (soft links).
    set_bb_config STATIC
    set_bb_config ADDUSER
    set_bb_config CHPASSWD
    set_bb_config PASSWD
    set_bb_config LOGIN
    set_bb_config SU
    set_bb_config MOUNT
    set_bb_config UMOUNT
    set_bb_config HOSTNAME
    set_bb_config FEATURE_ADDUSER_TO_GROUP
    set_bb_config FEATURE_SHADOWPASSWDS
    set_bb_config FEATURE_INSTALLER
    # the tc applet needs CBQ traffic-class kernel UAPI (TCA_CBQ_* and
    # struct tc_cbq_*) that newer kernel headers removed, so it fails to
    # compile against the runner's headers. Copper ships no traffic
    # control, so drop it.
    set_bb_config_off TC
    # settle remaining symbols to their defaults. busybox's kconfig has
    # no olddefconfig target; oldconfig works because the kconfig choices
    # stay in the consistent state defconfig wrote (scripts/config only
    # edits single symbols), so oldconfig never prompts and NEW symbols
    # take their defaults on a closed stdin
    make oldconfig
    require_bb_config "$BD/.config"
    make -j"$JOBS"
    make CONFIG_PREFIX="$TGT" install
  popd >/dev/null
  [ -x "$TGT/bin/busybox" ] || { echo "busybox build failed"; exit 1; }
  stamp_set "$WORK/busybox.stamp" "$SELF"
}

# ---------------------------------------------------------------
# 4. standard command suite, compiled from source against musl
# ---------------------------------------------------------------
build_gnu() {   # build_gnu NAME URL [configure args...]
  local name="$1" url="$2"; shift 2
  echo "  -> $name"
  local t d
  t=$(fetch "$url"); d=$(unpack "$t")
  pushd "$d" >/dev/null
    ./configure --host=x86_64-linux-musl --prefix=/usr \
      --disable-shared --enable-static --disable-nls "$@"
    make -j"$JOBS"
    make DESTDIR="$TGT" install
  popd >/dev/null
}

build_tools() {
  if [ -x "$TGT/usr/bin/ls" ] && [ -x "$TGT/usr/bin/grep" ] \
     && stamped_skip "$WORK/tools.stamp" "$SELF"; then
    echo "tools: already built, skipping"; return
  fi
  echo "==> standard command suite"
  build_gnu coreutils   "https://ftp.gnu.org/gnu/coreutils/coreutils-9.5.tar.xz"
  build_gnu grep        "https://ftp.gnu.org/gnu/grep/grep-3.11.tar.xz"
  build_gnu sed         "https://ftp.gnu.org/gnu/sed/sed-4.9.tar.xz"
  build_gnu findutils   "https://ftp.gnu.org/gnu/findutils/findutils-4.9.0.tar.xz"
  build_gnu diffutils   "https://ftp.gnu.org/gnu/diffutils/diffutils-3.10.tar.xz"
  build_gnu tar         "https://ftp.gnu.org/gnu/tar/tar-1.35.tar.xz"
  build_gnu gzip        "https://ftp.gnu.org/gnu/gzip/gzip-1.13.tar.xz"
  build_gnu xz          "https://github.com/tukaani-project/xz/releases/download/v5.4.6/xz-5.4.6.tar.xz"
  stamp_set "$WORK/tools.stamp" "$SELF"
}

# ---------------------------------------------------------------
# 4b. libnl — netlink library needed by wpa_supplicant (NL80211 driver)
#     Installed to $SYSROOT only (link-time use); not shipped in the ISO.
# ---------------------------------------------------------------
build_libnl() {
  if [ -f "$SYSROOT/usr/lib/libnl-3.a" ] \
     && stamped_skip "$WORK/libnl.stamp" "$SELF"; then
    echo "libnl: already built, skipping"; return
  fi
  echo "==> libnl"
  local LT LD
  LT=$(fetch "https://github.com/thom311/libnl/releases/download/libnl3_9_0/libnl-3.9.0.tar.gz")
  LD=$(unpack "$LT")
  pushd "$LD" >/dev/null
    ./configure --host=x86_64-linux-musl --prefix=/usr \
      --disable-shared --enable-static --disable-nls
    make -j"$JOBS"
    make DESTDIR="$SYSROOT" install
  popd >/dev/null
  [ -f "$SYSROOT/usr/lib/libnl-3.a" ] || { echo "libnl build failed"; exit 1; }
  stamp_set "$WORK/libnl.stamp" "$SELF"
}

# ---------------------------------------------------------------
# 4c. wpa_supplicant — WiFi association daemon
#     Built with CONFIG_TLS=internal (no OpenSSL dep) and linked
#     statically against musl + libnl.  Binary goes into the rootfs.
# ---------------------------------------------------------------
build_wpa_supplicant() {
  if [ -x "$TGT/usr/sbin/wpa_supplicant" ] \
     && stamped_skip "$WORK/wpa.stamp" "$SELF"; then
    echo "wpa_supplicant: already built, skipping"; return
  fi
  echo "==> wpa_supplicant"
  local WT WD
  WT=$(fetch "https://w1.fi/releases/wpa_supplicant-2.10.tar.gz")
  WD=$(unpack "$WT")
  pushd "$WD/wpa_supplicant" >/dev/null
    # Build config: NL80211 driver (modern mac80211 / cfg80211 kernel path),
    # internal TLS (no OpenSSL), static linkage against musl + the libnl we
    # just built.  Keep EAP and P2P off — Copper only needs WPA2-Personal.
    cat > .config <<EOF
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_NO_RANDOM_POOL=y
CONFIG_IEEE80211W=y
CONFIG_WPA_SUPPLICANT=y
CONFIG_AP=n
CONFIG_IBSS_RSN=n
CONFIG_P2P=n
CONFIG_WIFI_DISPLAY=n
CONFIG_HS20=n
CONFIG_INTERWORKING=n
CONFIG_EAP_PSK=n
CONFIG_EAP_TLS=n
CONFIG_EAP_TTLS=n
CONFIG_EAP_PEAP=n
CONFIG_EAP_MSCHAPv2=n
CONFIG_EAP_GTC=n
CONFIG_EAP_MD5=n
CONFIG_EAP_FAST=n
CONFIG_EAP_SIM=n
CONFIG_EAP_AKA=n
CONFIG_EAP_SAKE=n
CONFIG_EAP_GPSK=n
CONFIG_EAP_PWD=n
CONFIG_EAP_EKE=n
CC=musl-gcc
CFLAGS += -static -O2 -I${SYSROOT}/usr/include/libnl3
LDFLAGS += -static -L${SYSROOT}/usr/lib
LIBS += -lnl-3 -lnl-genl-3
EOF
    make -j"$JOBS"
    install -D -m 0755 wpa_supplicant "$TGT/usr/sbin/wpa_supplicant"
    install -D -m 0755 wpa_cli        "$TGT/usr/sbin/wpa_cli"
  popd >/dev/null
  [ -x "$TGT/usr/sbin/wpa_supplicant" ] || { echo "wpa_supplicant build failed"; exit 1; }
  stamp_set "$WORK/wpa.stamp" "$SELF"
}

# ---------------------------------------------------------------
# 5. Copper's own pieces: shell, init (PID 1), first-boot wizard
# ---------------------------------------------------------------
build_copper() {
  local SRC="$ROOT/../src"
  if [ -x "$TGT/usr/bin/copper-sh" ] && [ -x "$TGT/usr/bin/copper-init" ] \
     && [ -x "$TGT/usr/bin/copper-firstboot" ] \
     && stamped_skip "$WORK/copper.stamp" "$SELF" "$SRC"/*.c "$SRC"/*.h \
        "$ROOT/src-init/copper-init.c" "$ROOT/firstboot/copper-firstboot.c"
  then
    echo "copper: already built, skipping"; return
  fi
  echo "==> copper built-ins"
  $CC $CFLAGS -std=c11 -o "$TGT/usr/bin/copper-sh" \
     "$SRC/main.c" "$SRC/builtins.c" -I "$SRC"
  $CC $CFLAGS -std=c11 -o "$TGT/usr/bin/copper-init" \
     "$ROOT/src-init/copper-init.c"
  $CC $CFLAGS -std=c11 -o "$TGT/usr/bin/copper-firstboot" \
     "$ROOT/firstboot/copper-firstboot.c"
  ln -sf /usr/bin/copper-init "$TGT/sbin/init"   # our PID 1

  # switch_root is going to need all of these, and a staged tree missing one
  # of them is a black screen on a machine with no shell to debug it from.
  # Say it here, where it costs a second. (The udhcpc lease script is checked
  # in build_rootfs instead — this stage runs before the overlay is copied.)
  local f
  for f in usr/bin/copper-init usr/bin/copper-sh usr/bin/copper-firstboot \
            usr/sbin/wpa_supplicant usr/sbin/wpa_cli; do
    if [ ! -x "$TGT/$f" ]; then
      echo "copper: $f is missing from the staged rootfs" >&2
      exit 1
    fi
  done
  # sbin/init is a symlink, so -e is the wrong test for it: -e follows the
  # link, the target is absolute, and it therefore gets looked up on the build
  # host, where there is no /usr/bin/copper-init — the link reads as dangling
  # and a perfectly good staged tree gets reported as broken. That is the same
  # trap that stopped /init handing over, one file over. readlink does not
  # follow, which is exactly what is wanted here.
  if [ "$(readlink "$TGT/sbin/init")" != "/usr/bin/copper-init" ]; then
    echo "copper: sbin/init should be a symlink to /usr/bin/copper-init" >&2
    exit 1
  fi

  stamp_set "$WORK/copper.stamp" "$SELF" "$SRC"/*.c "$SRC"/*.h \
    "$ROOT/src-init/copper-init.c" "$ROOT/firstboot/copper-firstboot.c"
}

# ---------------------------------------------------------------
# 6. rootfs config + timezone data
# ---------------------------------------------------------------
build_rootfs() {
  echo "==> rootfs config"
  cp -a "$ROOT/rootfs-overlay/." "$TGT/"
  mkdir -p "$TGT/usr/share/zoneinfo" "$TGT/etc/skel" "$TGT/etc/network"
  cp -a /usr/share/zoneinfo/. "$TGT/usr/share/zoneinfo/" 2>/dev/null \
    || echo "  (no host zoneinfo to copy — timezone data will be missing)"

  # WiFi config: root-only read, it contains a plaintext PSK.
  if [ -f "$TGT/etc/wpa_supplicant.conf" ]; then
    chmod 0600 "$TGT/etc/wpa_supplicant.conf"
  fi

  # /var/run -> /run: wpa_supplicant's ctrl_interface and other daemons
  # use /var/run; /run is the tmpfs we mount at boot. Make them the same.
  mkdir -p "$TGT/var"
  ln -sfn /run "$TGT/var/run" 2>/dev/null || true
  # udhcpc execs this the moment a lease lands, and git does not reliably
  # carry the exec bit across platforms, so set it here.
  chmod 0755 "$TGT/usr/share/udhcpc/default.script"
  [ -x "$TGT/usr/share/udhcpc/default.script" ] || {
    echo "rootfs: udhcpc lease script is not executable"; exit 1; }

  # A CR anywhere in this file makes busybox ash fail every line of it, and
  # the machine has no shell to fix that with. Cheap to prove here.
  if LC_ALL=C grep -q $'\r' "$TGT/usr/share/udhcpc/default.script"; then
    echo "rootfs: udhcpc lease script has CRLF line endings" >&2
    exit 1
  fi

  # /usr/bin before /bin, in the profile. busybox's applets are symlinks in
  # /bin and the real GNU tools are in /usr/bin, and PATH order decides which
  # one answers to a name. Get it backwards and every grep, tar and sed in a
  # Copper shell is busybox while the entire userland sits there unused — which
  # is exactly what happened, and it is invisible from the build: the tools
  # were installed, the ISO was green, and `tar --version` said "busybox".
  #
  # Counting the position of each directory the obvious way does not work:
  # ${p%%:/bin:*} leaves the string alone when /bin is the last element,
  # because there is no trailing colon to match, so /usr/bin:/bin compares
  # equal with itself and a correct profile gets rejected. Walk it instead.
  path_pos() {   # path_pos <path> <dir> -> 1-based position, 0 if absent
    local rest="$1" want="$2" i=1 part
    local IFS=:
    for part in $rest; do
      if [ "$part" = "$want" ]; then echo "$i"; return 0; fi
      i=$((i + 1))
    done
    echo 0
    return 1
  }
  local ppath pu pb
  ppath=$(sed -n 's/^[[:space:]]*\(export \)\?PATH=//p' "$TGT/etc/profile" | head -1)
  if [ -z "$ppath" ]; then
    echo "rootfs: /etc/profile does not set PATH" >&2
    exit 1
  fi
  pu=$(path_pos "$ppath" /usr/bin)
  if [ "$pu" = 0 ]; then
    echo "rootfs: /etc/profile PATH has no /usr/bin: $ppath" >&2
    exit 1
  fi
  pb=$(path_pos "$ppath" /bin)
  if [ "$pb" != 0 ] && [ "$pu" -gt "$pb" ]; then
    echo "rootfs: /etc/profile puts /bin (position $pb) before /usr/bin" >&2
    echo "        (position $pu), so busybox shadows every GNU tool this" >&2
    echo "        build installed. PATH is: $ppath" >&2
    exit 1
  fi
  echo "  PATH order ok (/usr/bin at $pu, /bin at $pb): $ppath"

  # The overlay is copied onto a $TGT that may have come straight out of the
  # build cache, still holding files from an earlier revision. mkdir -p and cp
  # only ever add, so a file deleted from iso/rootfs-overlay/ would survive
  # forever and be baked into the next ISO. Record what the overlay contained
  # last time and drop whatever it no longer claims.
  #
  # The list has to come from iso/rootfs-overlay/, not from $TGT: $TGT is the
  # cached directory that still has the stale file in it, so diffing $TGT
  # against itself would never notice.
  local old="$WORK/overlay.manifest" new="$WORK/overlay.manifest.new" rel
  ( cd "$ROOT/rootfs-overlay" && find . \( -type f -o -type l \) | sed 's|^\./||' ) \
    | LC_ALL=C sort > "$new"
  if [ -s "$old" ]; then
    while IFS= read -r rel; do
      [ -n "$rel" ] || continue
      # Paths other stages own. Deleting one of these would break the build
      # in a much more confusing way than the bug this fixes.
      case "$rel" in usr/bin/*|usr/sbin/*|bin/*|sbin/*|usr/lib/*|lib/*|boot/*) continue ;; esac
      rm -f "$TGT/$rel"
      rmdir -p "$TGT/$(dirname "$rel")" 2>/dev/null || true
    done < <(comm -23 "$old" "$new")
  fi
  mv -f "$new" "$old"
}

# ---------------------------------------------------------------
# 7. live initramfs (busybox + our /init, drivers built into kernel)
# ---------------------------------------------------------------
build_initramfs() {
  local INITRD="$WORK/initramfs"
  if [ -s "$TGT/boot/initrd.img" ] \
     && stamped_skip "$WORK/initramfs.stamp" "$SELF" "$ROOT/live/init"; then
    echo "initramfs: already built, skipping"; return
  fi
  echo "==> initramfs"
  # Start from nothing every time. $INITRD lives in the cached work tree, and
  # mkdir -p only ever adds: a staging directory left over from an earlier
  # version of the init script gets packed into the cpio again, which is how a
  # removed mnt/upper/upper kept turning up in the image.
  rm -rf "$INITRD"
  # Only the mount points themselves. upper/ and work/ are deliberately NOT
  # created here: /init has to make them after it mounts the tmpfs on
  # /mnt/upper, because a tmpfs mounted over a directory hides what was
  # already in it, and the overlay then fails on a missing upperdir.
  mkdir -p "$INITRD"/{bin,sbin,proc,sys,dev,mnt/root,mnt/upper,mnt/merged,run}
  cp "$TGT/bin/busybox" "$INITRD/bin/busybox"
  install -m 0755 "$ROOT/live/init" "$INITRD/init"
  ( cd "$INITRD" && find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 ) \
    > "$TGT/boot/initrd.img"
  [ -s "$TGT/boot/initrd.img" ] || { echo "initramfs build failed"; exit 1; }
  stamp_set "$WORK/initramfs.stamp" "$SELF" "$ROOT/live/init"
}

# ---------------------------------------------------------------
# 8. boot media (grub makes a BIOS+UEFI bootable ISO)
# ---------------------------------------------------------------
build_iso() {
  # Stamped on the whole staged tree, not on grub.cfg alone: the ISO is a
  # copy of all of it, so a change to the overlay, the initrd or the kernel
  # all have to be able to invalidate it. The config goes in first so it is
  # part of the tree that gets hashed, which is the tree that gets packaged.
  mkdir -p "$TGT/boot/grub"
  cp "$ROOT/boot/grub.cfg" "$TGT/boot/grub/grub.cfg"
  local want; want=$(tree_hash "$TGT")
  if [ -s "$OUT/copper.iso" ] && [ -s "$WORK/iso.stamp" ] \
     && [ "$(cat "$WORK/iso.stamp")" = "$want" ]; then
    echo "iso: already built, skipping"; return
  fi
  echo "==> grub-mkrescue"
  grub-mkrescue -o "$OUT/copper.iso" "$TGT"
  [ -s "$OUT/copper.iso" ] || { echo "grub-mkrescue failed"; exit 1; }
  echo "$want" > "$WORK/iso.stamp"
  ls -lh "$OUT/copper.iso"
  sha256sum "$OUT/copper.iso"
}

# ---------------------------------------------------------------
# stage dispatch
# ---------------------------------------------------------------
full() {
  build_kernel
  build_musl
  build_busybox
  build_tools
  build_libnl
  build_wpa_supplicant
  build_copper
  build_rootfs
  build_initramfs
  build_iso
}

case "$STAGE" in
  kernel)     build_kernel ;;
  base)       build_musl; build_busybox ;;
  tools)      build_musl; build_tools ;;
  wifi)       build_musl; build_libnl; build_wpa_supplicant ;;
  copper)     build_musl; build_copper ;;
  rootfs)     build_rootfs ;;
  initramfs)  build_musl; build_busybox; build_rootfs; build_initramfs ;;
  iso)        build_musl; build_busybox; build_rootfs; build_initramfs; build_iso ;;
  all|"")     full ;;
  *)          echo "unknown stage: $STAGE (kernel|base|tools|wifi|copper|rootfs|initramfs|iso|all)"; exit 2 ;;
esac

echo "==> done"