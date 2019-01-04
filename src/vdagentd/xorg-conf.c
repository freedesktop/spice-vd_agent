/*  vdagentd.c vdagentd xorg.conf writing code

    Copyright 2011, 2012 Red Hat, Inc.

    Red Hat Authors:
    Hans de Goede <hdegoede@redhat.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <config.h>

#ifdef HAVE_PCIACCESS
#include <pciaccess.h>
#endif

#include <string.h>
#include <errno.h>
#include <limits.h>
#include <syslog.h>
#include "xorg-conf.h"

#define FPRINTF(...) \
    do { \
        r = fprintf(f, __VA_ARGS__); \
        if (r < 0) { \
            syslog(LOG_ERR, "Error writing to %s: %m", xorg_conf); \
            fclose(f); \
            pci_system_cleanup(); \
            return; \
        } \
    } while(0)

void vdagentd_write_xorg_conf(VDAgentMonitorsConfig *monitor_conf)
{
#ifdef HAVE_PCIACCESS
    int i, r, count, min_x = INT_MAX, min_y = INT_MAX;
    FILE *f;
    struct pci_device_iterator *it;
    struct pci_device *dev;
    const struct pci_id_match qxl_id_match = {
        .vendor_id = 0x1b36,
        .device_id = 0x0100,
        .subvendor_id = PCI_MATCH_ANY,
        .subdevice_id = PCI_MATCH_ANY,
    };
    const char *xorg_conf = "/var/run/spice-vdagentd/xorg.conf.spice";
    const char *xorg_conf_old = "/var/run/spice-vdagentd/xorg.conf.spice.old";

    r = rename(xorg_conf, xorg_conf_old);
    if (r && errno != ENOENT) {
        syslog(LOG_ERR,
               "Error renaming %s to %s: %m, not generating xorg.conf",
               xorg_conf, xorg_conf_old);
        return;
    }

    r = pci_system_init();
    if (r) {
        syslog(LOG_ERR, "Error initializing libpciaccess: %d, not generating xorg.conf", r);
        return;
    }

    it = pci_id_match_iterator_create(&qxl_id_match);
    if (!it) {
        syslog(LOG_ERR, "Error could not create pci id iterator for QXL devices, not generating xorg.conf");
        pci_system_cleanup();
        return;
    }

    dev = pci_device_next(it);
    if (!dev) {
        syslog(LOG_ERR, "No QXL devices found, not generating xorg.conf");
        pci_system_cleanup();
        return;
    }

    f = fopen(xorg_conf, "w");
    if (!f) {
        syslog(LOG_ERR, "Error opening %s for writing: %m", xorg_conf);
        pci_system_cleanup();
        return;
    }

    FPRINTF("# xorg.conf generated by spice-vdagentd\n");
    FPRINTF("# generated from monitor info provided by the client\n\n");

    if (monitor_conf->num_of_monitors == 1) {
        FPRINTF("# Client has only 1 monitor\n");
        FPRINTF("# This works best with no xorg.conf, leaving xorg.conf empty\n");
        fclose(f);
        pci_system_cleanup();
        return;
    }

    FPRINTF("Section \"ServerFlags\"\n");
    FPRINTF("\tOption\t\t\"Xinerama\"\t\"true\"\n");
    FPRINTF("EndSection\n\n");

    i = 0;
    do {
        FPRINTF("Section \"Device\"\n");
        FPRINTF("\tIdentifier\t\"qxl%d\"\n", i++);
        FPRINTF("\tDriver\t\t\"qxl\"\n");
        FPRINTF("\tBusID\t\t\"PCI:%02d:%02d:%d\"\n",
                dev->bus, dev->dev, dev->func);
        FPRINTF("\tOption\t\t\"NumHeads\"\t\"1\"\n");
        FPRINTF("EndSection\n\n");
    } while ((dev = pci_device_next(it)));

    if (i < monitor_conf->num_of_monitors) {
        FPRINTF("# Client has %d monitors, but only %d qxl devices found\n",
                monitor_conf->num_of_monitors, i);
        FPRINTF("# Only generation %d \"Screen\" sections\n\n", i);
        count = i;
    } else {
        count = monitor_conf->num_of_monitors;
    }

    for (i = 0; i < count; i++) {
        FPRINTF("Section \"Screen\"\n");
        FPRINTF("\tIdentifier\t\"Screen%d\"\n", i);
        FPRINTF("\tDevice\t\t\"qxl%d\"\n", i);
        FPRINTF("\tDefaultDepth\t24\n");
        FPRINTF("\tSubSection \"Display\"\n");
        FPRINTF("\t\tViewport\t0 0\n");
        FPRINTF("\t\tDepth\t\t24\n");
        FPRINTF("\t\tModes\t\t\"%dx%d\"\n", monitor_conf->monitors[i].width,
                monitor_conf->monitors[i].height);
        FPRINTF("\tEndSubSection\n");
        FPRINTF("EndSection\n\n");
    }

    /* monitor_conf may contain negative values, convert these to 0 - # */
    for (i = 0; i < count; i++) {
        if (monitor_conf->monitors[i].x < min_x) {
            min_x = monitor_conf->monitors[i].x;
        }
        if (monitor_conf->monitors[i].y < min_y) {
            min_y = monitor_conf->monitors[i].y;
        }
    }

    FPRINTF("Section \"ServerLayout\"\n");
    FPRINTF("\tIdentifier\t\"layout\"\n");
    for (i = 0; i < count; i++) {
        FPRINTF("\tScreen\t\t\"Screen%d\" %d %d\n", i,
                monitor_conf->monitors[i].x - min_x,
                monitor_conf->monitors[i].y - min_y);
    }
    FPRINTF("EndSection\n");

    fclose(f);
    pci_system_cleanup();
#endif
}
