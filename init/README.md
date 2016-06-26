#A minimal init#

Copied from http://git.suckless.org/sinit/ version 1.0 (see attic/sinit-1.0.tar.gz).

Local customizations (attic/sinit-1.0.patch):
 1. changed location of startup/shutdown scripts
 2. added singal handlers for SIGKILL and SIGPWR

To build and install:

```
$ make
$ mv ./sinit ${rootfs}/sbin/init
```
 
where ${rootfs} is the target container or vm rootfs directory 
