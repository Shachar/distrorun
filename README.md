# What's all this, then?
distrorun allows you to place the user-space files for another Linux distribution inside a directory, and run specific commands inside that distro. It uses the Linux containers support, and has a very low overhead.

## Didn't you just describe docker?
Yes. And then again, also no.

Docker is focused on isolation. The contained image has a very high isolation from the running system, with almost everything the contained image does not affecting and not being affected from the host system. For many things, that's a very good thing.
There are cases, however, where that's a problem. In particular, when running proprietary programs, in particular, GUI proprietary programs, those are often only certified from certain distributions that may be different than your distribution of choice.

With distrorun, you can give those program a supported environment while specifically *not* isolating them from yours.

## Didn't you just describe snap?
Yes. And then again, also no.

Snap has its uses for cases where someone packages the program for you. If you want to maintain the image yourself (say, because no one else did it, or because no one has the permission to distribute the installed binary program), you're out of luck.

## Didn't you just describe...
Oh, give it a rest.

distrorun is a single executable plus a few directories under `/etc`. If you find it useful, share and enjoy.

# Installation
To compile from source you'll need the `automake` and `autoconfig`. The following sequence brings you from a checked out git image to compiled program:

```
$ autoreconf -i
$ mkdir build ; cd build
$ ../configure --sysconfdir=/etc
$ make
```

The `distrorun` executable itself must be SUID root to function, so installation has to happen as root (or fakeroot, if you only want to package and not run):
```
# make install
```

The `DESTDIR` variable is supported as usual.

# Setting a new distro up
## Base image
To set up the distro, you'll need to create a base image. For Debian based distros you can use [`debootstrap`](https://wiki.debian.org/Debootstrap). Other distros might have other means. Rocky Linux, for example, has a [repo with tar files containing
base images for past versions](https://github.com/rocky-linux/sig-cloud-instance-images/).

## Preperations
Since `distrorun` is a very shallow container, some things need to be set up by hand. At a minimum, you should set up some DNS connectivity and user support. At a minimum, it's probably best to copy `/etc/resolve.conf` from your host to your guest image.
You'll probably also want to copy the user definition lines from `/etc/passwd` to the passwd inside your new distro.

Not strictly necessary, but very useful: if your **host** is Debian derived, it is likely that your default prompt allows indicating whether you're inside a chroot. To activate this feature, place the chroot's name inside the path `/etc/debian_chroot`
inside your container. This way, if you share the same `$HOME` between images, your prompt will tell you whice distro you're currently working with.

## Writing the configuration
Since `distrorun` is a SUID root executable, and to avoid security problems, it will only read the configuration from the path defined at compile time. You can change that path using the `--sysconfdir` option to `configure`. By default, that will be set
to `/usr/local/etc`, which is almost never what you'd want.

The configuration files are under `sysconfdir/distrorun.d/name.conf`. In this case, `name` is replaced by the image name given at the command line. The file might look something like this:
```
dir: "/srv/distros/sample"
mapped_volumes: [
    "/dev",
    "/home",
    "/tmp",
    "/run"
]
```

`dir` is the directory where the distribution is set up.
`mapped_volumes` is an array of paths. Each will be bind mounted into the chroot. In addition to those, `/proc` will also be mounted automatically. This should allow sharing the environment between host and guest in a way the should let most programs
just run. X11 displays are sockets under `/tmp`, so X11 applications should just run.

# Running in the distro
The command line for running is:

> distrorun [*options*] _distro_name_ command

The run environment used by `distrorun` is the exact same environment used outside. Upon running, the mounts are done (visible only to the command), and command is run with the same user, groups and environment as the caller, and with the same directory
as the current directory (relative to the new root). The new command is a direct child of the calling shell, if any, and is connected to the same input, outputs and controlling TTY.
