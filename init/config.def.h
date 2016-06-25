/* See LICENSE file for copyright and license details. */

static char *const rcinitcmd[]     = { "/etc/rc.startup", NULL };
static char *const rchaltcmd[]     = { "/etc/rc.shutdown", "halt", NULL };
static char *const rcpoweroffcmd[] = { "/etc/rc.shutdown", "poweroff", NULL };
static char *const rcrebootcmd[]   = { "/etc/rc.shutdown", "reboot", NULL };
