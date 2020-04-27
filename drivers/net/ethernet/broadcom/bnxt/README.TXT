                               README Notes
                        Broadcom bnxt_en Linux Driver
                              Version 1.10.0
                                08/02/2019

                              Broadcom Inc.
                           15101 Alton Parkway,
                            Irvine, CA 92618

                 Copyright (c) 2015 - 2016 Broadcom Corporation
                   Copyright (c) 2016 - 2018 Broadcom Limited
                      Copyright (c) 2018 Broadcom Inc.
                           All rights reserved


Table of Contents
=================

  Introduction
  Limitations
  Port Speeds
  BNXT_EN Driver Dependencies
  BNXT_EN Driver Settings
  Autoneg
  Energy Efficient Ethernet
  Enabling Receive Side Scaling (RSS)
  Enabling Accelerated Receive Flow Steering (RFS)
  Enabling Busy Poll Sockets
  Enabling SR-IOV
  Virtual Ethernet Bridge (VEB)
  Hardware QoS
  PTP Hardware Clock
  Set IRQ Balance manually
  BNXT_EN Driver Parameters
  BNXT_EN Driver Defaults
  Statistics
  Unloading and Removing Driver
  Updating Firmware for Broadcom NetXtreme-C and NetXtreme-E devices
  Updating Firmware for Broadcom Nitro device
  Devlink


Introduction
============

This file describes the bnxt_en Linux driver for the Broadcom NetXtreme-C/
NetXtreme-E BCM573xx, BCM574xx, BCM575xx, NetXtreme-S BCM5880x
(up to 200 Gbps) Ethernet Network Controllers and Broadcom Nitro
BCM58700 4-port 1/2.5/10 Gbps Ethernet Network Controller.


Limitations
===========

1. The current version of the driver will compile on RHEL7.x, RHEL6.x,
OLE6.x UEK, SLES12, SLES11SP1 and newer, most 3.x/4.x kernels, and some
2.6 kernels starting from 2.6.32.

2. Laser needs to be brought up for Nitro BCM58700 Ethernet controller
using the following command, to bring up the Link.

    i2cset -f -y 1 0x70 0 7 && i2cset -f -y 1 0x24 0xff 0x0

3. Each device supports hundreds of MSIX vectors.  The driver will enable
all MSIX vectors when it loads.  On some systems running on some kernels,
the system may run out of interrupt descriptors, especially when using
multiple devices or NPAR devices.


Port Speeds
===========

On some dual-port devices, the port speed of each port must be compatible
with the port speed of the other port.  10Gbps and 25Gbps are not compatible
speeds.  For example, if one port is set to 10Gbps and link is up, the other
port cannot be set to 25Gbps.  However, the driver will allow incompatible
speeds to be set on the two ports if link is not up yet.  Subsequent link up
on one port will render the incompatible speed on the other port to become
unsupported.  A console message like this may appear when this scenario
happens:

   bnxt_en 0000:04:00.0 eth0: Link speed 25000 no longer supported

If the link is up on one port, the driver will not allow the other port to
be set to an incompatible speed.  An attempt to do that will result in an
error.  For example, eth0 and eth1 are the 2 ports of the dual-port device,
eth0 is set to 10Gbps and link is up.

   ethtool -s eth1 speed 25000
   Cannot set new settings: Invalid argument
     not setting speed

This operation will only be allowed when the link goes down on eth0 or if
eth0 is brought down using ifconfig/ip.

On some NPAR (NIC partioning) devices where one port is shared by multiple
PCI functions, the port speed is pre-configured and cannot be changed by
the driver.

See Autoneg section below for additional information.


BNXT_EN Driver Dependencies
===========================

The driver has no dependencies on user-space firmware packages as all necessary
firmware must be programmed in NVRAM(or QSPI for Nitro BCM58700 devices).
Starting with driver version 1.0.0, the goal is that the driver will be
compatible with all future versions of production firmware. All future versions
of the driver will be backwards compatible with firmware as far back as the
first production firmware.

The first production firmware is version 20.1.11 using Hardware Resource
Manager (HWRM) spec. 1.0.0.

ethtool -i displays the firmware versions.  For example:

   ethtool -i eth0

will show among other things:

   firmware-version: 20.1.11/1.0.0 pkg 20.02.00.03

In this example, the first version number (20.1.11) is the firmware version,
the second version number (1.0.0) is the HWRM spec. version.  The third
version number (20.02.00.03) is the package version of all the different
firmware components in NVRAM.  The package version may not be available on
all devices.

Using kernels older than 4.7, if CONFIG_VLAN_MODULE kernel option is set as a
module option, the vxlan.ko module must be loaded before the bnxt_en.ko module.

Using newer kernels, the hwmon.ko module may need to be loaded first if
CONFIG_HWMON_MODULE kernel option is set as a module option.

Using newer kernels, devlink.ko module may need to be loaded first if
CONFIG_NET_DEVLINK kernel option is set as a module option.

BNXT_EN Driver Settings
=======================

The bnxt_en driver settings can be queried and changed using ethtool. The
latest ethtool can be downloaded from
ftp://ftp.kernel.org/pub/software/network/ethtool if it is not already
installed. The following are some common examples on how to use ethtool. See
the ethtool man page for more information. ethtool settings do not persist
across reboot or module reload. The ethtool commands can be put in a startup
script such as /etc/rc.local to preserve the settings across a reboot. On
Red Hat distributions, "ethtool -s" parameters can be specified in the
ifcfg-ethx scripts using the ETHTOOL_OPTS keyword.

Some ethtool examples:

1. Show current speed, duplex, and link status:

   ethtool eth0

Note that if auto-negotiation is off, ethtool will always show the speed
setting whether link is up or down.  If auto-negotiation is on, ethtool will
show the negotiated speed when link is up, and unknown speed when link is
down.

2. Set speed:

Example: Set speed to 10Gbps with autoneg off:

   ethtool -s eth0 speed 10000 autoneg off

Example: Set speed to 25Gbps with autoneg off:

   ethtool -s eth0 speed 25000 autoneg off

On some NPAR (NIC partitioning) devices, the port speed and flow control
settings cannot be changed by the driver.

See Autoneg section below for additional information on configuring
Autonegotiation.

3. Show offload settings:

   ethtool -k eth0

4. Change offload settings:

Example: Turn off TSO (TCP Segmentation Offload)

   ethtool -K eth0 tso off

Example: Turn off hardware GRO (Generic Receive Offload)

   ethtool -K eth0 rx-gro-hw off

Note that "rx-gro-hw" (hardware GRO) setting is available in newer kernels
such as 4.16.  When "rx-gro-hw" is turned off, there is no effect on software
GRO.  Prior to the introduction of "rx-gro-hw", hardware GRO settings can only
be controlled by controlling "gro", which applies to both GRO and hardware GRO.

   ethtool -K eth0 gro off

Example: Turn off hardware LRO (Large Receive Offload)

   ethtool -K eth0 lro off

Note that hardware GRO and hardware LRO are mutually exclusive.  Hardware
GRO is generally better than LRO because the former is reversible and
is compatible with bridging and routing (including bridging to Virtual
Machines).  LRO must be turned off when bridging or routing is enabled.

Example: Turn on hardware GRO

   ethtool -K eth0 rx-gro-hw on

If "rx-gro-hw" is not available on older kernels, use "gro".

   ethtool -K eth0 gro on

Note that if both "gro" and "lro" are set on older kernels that don't support
"rx-gro-hw", the driver will use hardware GRO.

5. Show ring sizes:

   ethtool -g eth0

6. Change ring sizes:

   ethtool -G eth0 rx N

Note that the RX Jumbo ring size is set automatically when needed and
cannot be changed by the user.

7. Get statistics:

   ethtool -S eth0

8. Show number of channels (rings):

   ethtool -l eth0

9. Set number of channels (rings):

   ethtool -L eth0 rx N tx N combined 0

   ethtool -L eth0 rx 0 tx 0 combined M

Note that the driver can support either all combined or all rx/tx channels,
but not a combination of combined and rx/tx channels.  The default is
combined channels to match the number of CPUs up to 8.  Combined channels
use less system resources but may have lower performance than rx/tx channels
under very high traffic stress.  rx and tx channels can have different numbers
for rx and tx but must both be non-zero.

10. Show interrupt coalescing settings:

    ethtool -c eth0

11. Set interrupt coalescing settings:

    ethtool -C eth0 rx-frames N

    Note that only these parameters are supported:
    rx-usecs, rx-frames, rx-usecs-irq, rx-frames-irq,
    tx-usecs, tx-frames, tx-usecs-irq, tx-frames-irq,
    stats-block-usecs.

12. Show RSS flow hash indirection table and RSS hash key:

    ethtool -x eth0

13. Set 40-byte RSS hash key:

    ethtool -X eth0 hkey 00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f:10:11:12:13:14:15:16:17:18:19:1a:1b:1c:1d:1e:1f:20:21:22:23:24:25:26:27

14. Run self test:

    ethtool -t eth0

    Note that only single function PFs can execute self tests.  If a PF has
    active VFs, only online tests can be executed.

15. Collect Firmware Coredump:

    ethtool -w eth0 data FILENAME

16. Set coredump flags:

    ethtool -W eth0 N

    Note that N is '0' for collecting live dump and '1' for collecting saved
    crash dump.

17. Reset the device:

    ethtool --reset eth0 [flags N] [type]

    Note that driver supports 'ap' and 'all' type of resets.

18. Add receive network flow classification filters.

    ethtool -N eth0 flow-type ether|tcp4|udp4|tcp6|udp6 FLOW_SPEC

    This feature requires n-tuple filters to be enabled (default is enabled):

    ethtool -K eth0 ntuple on

Example: Ethernet filter to rx queue 0

    ethtool -N eth0 flow-type ether dst 00:11:22:33:44:55 action 0

Example: TCP/IPv4 5-tuple filter to rx queue 1

    ethtool -N eth0 flow-type tcp4 dst-ip 192.168.0.1 src-ip 192.168.0.2 \
    dst-port 80 src-port 32768 action 1

Example: UDP/IPv4 4-tuple filter to rx queue 2

    ethtool -N eth0 flow-type udp4 dst-ip 192.168.0.1 src-ip 192.168.0.2 \
    dst-port 2049 action 2

The flow-type is counted as the first tuple and must always be specified.
At least one additional tuple must be specified for TCP/UDP filters.  A
tuple must either be completely specified or not specified at all.  A
partial wildcard tuple with an incomplete mask is not supported.  Note
that the tuple mask is not required and is assumed by ethtool to be the
complete mask when the tuple is specified.

It is possible to create a 5-tuple filter that is inside the domain of
another 4-tuple filter, for example.  In general, the more specific
5-tuple filter will take precedence.

If the filter is created succesfully, the ID of the filter will be returned
by ethtool.  ethtool -n will also display the current list of filters with
their IDs.  A specific filter can be deleted by specifying the ID.

Example: Delete filter ID 3

    ethtool -N eth0 delete 3

Note that if accelerated RFS is enabled and it has added some 5-tuple
filters, any duplicate 5-tuple filters added by ethtool will be rejected.
It is generally not recommended to enable accelerated RFS and create
static 5-tuple filters on the same function.

19. See ethtool man page for more options.


Autoneg
=======

The bnxt_en driver supports Autonegotiation of speed and flow control on
most devices.  Some dual-port 25G devices do not support Autoneg.  Autoneg
must be enabled for 10GBase-T devices.

Note that parallel detection is not supported when autonegotiating
100GBase-CR4, 50GBase-CR2, 40GBase-CR4, 25GBase-CR, 10GbE SFP+.
If one side is autonegoatiating and the other side is not,
link will not come up.

25G, 50G and 100G advertisements are newer standards first defined in the 4.7
kernel's ethtool interface.  To fully support these new advertisement speeds
for autonegotiation, 4.7 (or newer) kernel and a newer ethtool utility are
required.

Below are some examples to illustrate the limitations when using 4.6 and
older kernels:

1. Enable Autoneg with all supported speeds advertised when the device
currently has Autoneg disabled:

   ethtool -s eth0 autoneg on advertise 0x0

Note that to advertise all supported speeds (including 25G, 50G and 100G),
the device must initially have Autoneg disabled.  advertise is a hexadecimal
value specifying one or more advertised speed.  0x0 is special value that
means all supported speeds.  See ethtool man page.  These advertise values
are supported by the driver:

0x020           1000baseT Full
0x1000          10000baseT Full
0x1000000       40000baseCR4 Full

2. Enable Autoneg with only 10G advertised:

   ethtool -s eth0 autoneg on advertise 0x1000

or:

   ethtool -s eth0 autoneg on speed 10000 duplex full


3. Enable Autoneg with only 40G advertised:

   ethtool -s eth0 autoneg on advertise 0x01000000

4. Enable Autoneg with 40G and 10G advertised:

   ethtool -s eth0 autoneg on advertise 0x01001000

Note that the "Supported link modes" and "Advertised link modes" will not
show 25G, 50G and 100G even though they may be supported or advertised.  For
example, on a device that is supporting and advertising 10G, 25G, 40G, 50G and
100G, and linking up at 50G, ethtool will show the following:

   ethtool eth0
   Settings for eth0:
           Supported ports: [ FIBRE ]
           Supported link modes:   10000baseT/Full
                                   40000baseCR4/Full
           Supported pause frame use: Symmetric Receive-only
           Supports auto-negotiation: Yes
           Advertised link modes:  10000baseT/Full
                                   40000baseCR4/Full
           Advertised pause frame use: Symmetric
           Advertised auto-negotiation: Yes
           Speed: 50000Mb/s
           Duplex: Full
           Port: FIBRE
           PHYAD: 1
           Transceiver: internal
           Auto-negotiation: on
           Current message level: 0x00000000 (0)

           Link detected: yes

Using kernels 4.7 or newer and ethtool version 4.8 or newer, 25G, 50G and 100G
advertisement speeds can be properly configured and displayed, without any
of the limitations described above.  ethtool version 4.8 has a bug that
ignores the advertise parameter, so it is recommended to use ethtool 4.10.
Example ethtool 4.10 output showing 10G/25G/40G/50G/100G advertisement settings:

   ethtool eth0
   Settings for eth0:
           Supported ports: [ FIBRE ]
           Supported link modes:   10000baseT/Full
                                   40000baseCR4/Full
                                   25000baseCR/Full
                                   50000baseCR2/Full
                                   100000baseCR4/Full
           Supported pause frame use: Symmetric Receive-only
           Supports auto-negotiation: Yes
           Advertised link modes:  10000baseT/Full
                                   40000baseCR4/Full
                                   25000baseCR/Full
                                   50000baseCR2/Full
                                   100000baseCR4/Full
           Advertised pause frame use: No
           Advertised auto-negotiation: Yes
           Speed: 50000Mb/s
           Duplex: Full
           Port: Direct Attach Copper
           PHYAD: 1
           Transceiver: internal
           Auto-negotiation: on
           Supports Wake-on: d
           Wake-on: d
           Current message level: 0x00000000 (0)

           Link detected: yes

These are the complete advertise values supported by the driver using 4.7
kernel or newer and a compatible version of ethtool supporting the new
values:

0x020           1000baseT Full
0x1000          10000baseT Full
0x1000000       40000baseCR4 Full
0x80000000      25000baseCR Full
0x400000000     50000baseCR2 Full
0x4000000000    100000baseCR4 Full

Note that the driver does not make a distinction on the exact physical
layer encoding and media type for a link speed.  For example, at 50G, the
device may support 50000baseCR2 and 50000baseSR2 for copper and multimode
fiber cables respectively.  Regardless of what cabling is used for 50G,
the driver currently uses only the ethtool value defined for 50000baseCR2
to cover all variants of the 50G media types.  The same applies to all
other advertise value for other link speeds listed above.


Energy Efficient Ethernet
=========================

The driver supports Energy Efficient Ethernet (EEE) settings on 10GBase-T
devices.  If enabled, and connected to a link partner that advertises EEE,
EEE will become active.  EEE saves power by entering Low Power Idle (LPI)
state when the transmitter is idle.  The downside is increased latency as
it takes a few microseconds to exit LPI to start transmitting again.

On a 10GBase-T device that supports EEE, the link up console message will
include the current state of EEE.  For example:

   bnxt_en 0000:05:00.0 eth0: NIC Link is Up, 10000 Mbps full duplex, Flow control: none
   bnxt_en 0000:05:00.0 eth0: EEE is active

The active state means that EEE is negotiated to be active during
autonegotiation.  Additional EEE parameters can be obtained using ethtool:

   ethtool --show-eee eth0

   EEE Settings for eth0:
           EEE status: enabled - active
           Tx LPI: 8 (us)
           Supported EEE link modes:  10000baseT/Full
           Advertised EEE link modes:  10000baseT/Full
           Link partner advertised EEE link modes:  10000baseT/Full

The tx LPI timer of 8 microseconds is currently fixed and cannot be adjusted.
EEE is only supported on 10GBase-T.  1GBase-T does not currently support EEE.

To disable EEE:

   ethtool --set-eee eth0 eee off

To enable EEE, but disable LPI:

   ethtool --set-eee eth0 eee on tx-lpi off

This setting will negotiate EEE with the link partner but the transmitter on
eth0 will not enter LPI during idle.  The link partner may independently
choose to enter LPI when its transmitter is idle.


Enabling Receive Side Scaling (RSS)
===================================

By default, the driver enables RSS by allocating receive rings to match the
the number of CPUs (up to 8).  Incoming packets are run through a 4-tuple
or 2-tuple hash function for TCP/IP packets and IP packets respectively.
Non fragmented UDP packets are run through a 4-tuple hash function on newer
devices (2-tuple on older devices).  See below for more information about
4-tuple and 2-tuple and how to configure it.

The computed hash value will determine the receive ring number for the
packet.  This way, RSS distributes packets to multiple receive rings while
guaranteeing that all packets from the same flow will be steered to the same
receive ring.  The processing of each receive ring can be done in parallel
by different CPUs to achieve higher performance.  For example, irqbalance
will distribute the MSIX vector of each RSS receive ring across CPUs.
However, RSS does not guarantee even distribution or optimal distribution of
packets.

To disable RSS, set the number of receive channels (or combined channels) to 1:

   ethtool -L eth0 rx 1 combined 0

or

   ethtool -L eth0 combined 1 rx 0 tx 0

To re-enable RSS, set the number of receive channels or (combined channels) to
a value higher than 1.

The RSS hash can be configured for 4-tuple or 2-tuple for various flow types.
4-tuple means that the source, destination IP addresses and layer 4 port
numbers are included in the hash function.  2-tuple means that only the source
and destination IP addresses are included.  4-tuple generally gives better
results.  Below are some examples on how to set and display the hash function.

To display the current hash for TCP over IPv4:

   ethtool -u eth0 rx-flow-hash tcp4

To disable 4-tuple (enable 2-tuple) for UDP over IPv4:

   ethtool -U eth0 rx-flow-hash udp4 sd

To enable 4-tuple for UDP over IPv4:

   ethtool -U eth0 rx-flow-hash udp4 sdfn


Enabling Accelerated Receive Flow Steering (RFS)
================================================

RSS distributes packets based on n-tuple hash to multiple receive rings.
The destination receive ring of a packet flow is solely determined by the
hash value.  This receive ring may or may not be processed in the kernel by
the CPU where the sockets application consuming the packet flow is running.

Accelerated RFS will steer incoming packet flows to the ring whose MSI-X
vector will interrupt the CPU running the sockets application consuming
the packets.  The benefit is higher cache locality of the packet data from
the moment it is processed by the kernel until it is consumed by the
application.

Accelerated RFS requires n-tuple filters to be supported.  On older
devices, only Physical Functions (PFs, see SR-IOV below) support n-tuple
filters.  On the latest devices, n-tuple filters are supported and enabled
by default on all functions.  Use ethtool to disable n-tuple filters:

   ethtool -K eth0 ntuple off

To re-enable n-tuple filters:

   ethtool -K eth0 ntuple on

After n-tuple filters are enabled, Accelerated RFS will be automatically
enabled when RFS is enabled.  These are example steps to enable RFS on
a device with 8 rx rings:

echo 32768 > /proc/sys/net/core/rps_sock_flow_entries
echo 2048 > /sys/class/net/eth0/queues/rx-0/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-1/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-2/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-3/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-4/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-5/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-6/rps_flow_cnt
echo 2048 > /sys/class/net/eth0/queues/rx-7/rps_flow_cnt

These steps will set the global flow table to have 32K entries and each
receive ring to have 2K entries.  These values can be adjusted based on
usage.

Note that for Accelerated RFS to be effective, the number of receive channels
(or combined channels) should generally match the number of CPUs.  Use
ethtool -L to fine-tune the number of receive channels (or combined channels)
if necessary.  Accelerated RFS has precedence over RSS.  If a packet matches an
n-tuple filter rule, it will be steered to the RFS specified receive ring.
If the packet does not match any n-tuple filter rule, it will be steered
according to RSS hash.

To display the active n-tuple filters setup for Accelerated RFS:

   ethtool -n eth0

Note that if there are a large number of filters and they are constantly
changing, ethtool may report some retrieval failures.  These errors are
normal.

IPv6, GRE and IP-inIP n-tuple filters are supported on 4.5 and newer kernels.
Note that RFS will only steer non-fragmented UDP packets to a connected UDP
socket.  Fragmented UDP packets or UDP packets to a connectionless socket
will fall back to RSS hashing.

n-tuple filters can also be added statically using ethtool (See
BNXT_EN Driver Settings section above).


Enabling Busy Poll Sockets
==========================

Using 3.11 and newer kernels (also backported to some major distributions),
Busy Poll Sockets are supported by the bnxt_en driver if
CONFIG_NET_RX_BUSY_POLL is enabled.  Individual sockets can set the
SO_BUSY_POLL option, or it can be enabled globally using sysctl:

    sysctl -w net.core.busy_read=50

This sets the time to busy read the device's receive ring to 50 usecs.
For socket applications waiting for data to arrive, using this method
can decrease latency by 2 or 3 usecs typically at the expense of
higher CPU utilization.  The value to use depends on the expected
time the socket will wait for data to arrive.  Use 50 usecs as a
starting recommended value.

In addition, the following sysctl parameter should also be set:

    sysctl -w net.core.busy_poll=50

This sets the time to busy poll for socket poll and select to 50 usecs.
50 usecs is a recommended value for a small number of polling sockets.


Enabling SR-IOV
===============

The Broadcom NetXtreme-C and NetXtreme-E devices support Single Root I/O
Virtualization (SR-IOV) with Physical Functions (PFs) and Virtual Functions
(VFs) sharing the Ethernet port.  The same bnxt_en driver is used for both
PFs and VFs under Linux.

Only the PFs are automatically enabled.  If a PF supports SR-IOV, lspci
will show that it has the SR-IOV capability and the total number of VFs
supported.  To enable one or more VFs, write the desired number of VFs
to the following sysfs file:

    /sys/bus/pci/devices/<domain>:<bus>:<device>:<function>/sriov_numvfs

For example, to enable 4 VFs on bus 82 device 0 function 0:

    echo 4 > /sys/bus/pci/devices/0000:82:00.0/sriov_numvfs

To disable the VFs, write 0 to the same sysfs file.  Note that to change
the number of VFs, 0 must first be written before writing the new number
of VFs.

On older 2.6 kernels that do not support the sysfs method to enable SR-IOV,
the driver uses the module parameter "num_vfs" to enable the desired number
of VFs.  Note that this is a global parameter that applies to all PF
devices in the system.  For example, to enable 4 VFs on all supported PFs:

    modprobe bnxt_en num_vfs=4

The 4 VFs of each supported PF will be enabled when the PF is brought up.

The VF and the PF operate almost identically under the same Linux driver
but not all operations supported on the PF are supported on the VF.

The resources needed by each VF are assigned by the PF based on how many
VFs are requested to be enabled and the resources currently used by the PF.
It is important to fully configure the PF first with all the desired features,
such as number of RSS/TSS channels, jumbo MTU, etc, before enabling SR-IOV.
After enabling SR-IOV, there may not be enough resources left to reconfigure
the PF.

The resources are evenly divided among the VFs.  Enabling a large number of
VFs will result in less resources (such as RSS/TSS channels) for each VF.

Refer to other documentation on how to map a VF to a VM or a Linux Container.

Some attributes of a VF can be set using iproute2 through the PF.  SR-IOV
must be enabled by setting the number of desired VFs before any attributes
can be set.  Some examples:

1. Set VF MAC address:

   ip link set <pf> vf <vf_index> mac <vf_mac>

Example:

   ip link set eth0 vf 0 mac 00:12:34:56:78:9a

Note that if the VF MAC addres is not set as shown, a random MAC address will
be used for the VF.  If the VF MAC address is changed while the VF driver has
already brought up the VF, it is necessary to bring down and up the VF before
the new MAC address will take effect.

2. Set VF link state:

   ip link set <pf> vf <vf_index> state auto|enable|disable

The default is "auto" which reflects the true link state.  Setting the VF
link to "enable" allows loopback traffic regardless of the true link state.

Example:

   ip link set eth0 vf 0 state enable

3. Set VF default VLAN:

   ip link set <pf> vf <vf_index> vlan <vlan id>

Example:

   ip link set eth0 vf 0 vlan 100

4. Set VF MAC address spoof check:

   ip link set <pf> vf <vf_index> spoofchk on|off

Example:

   ip link set eth0 vf 0 spoofchk on

Note that spoofchk is only effective if a VF MAC address has been set as
shown in #1 above.

5. Set VF trust:

   ip link set <pf> vf <vf_index> trust on|off

Example:

   ip link set eth0 vf 0 trust on

A VF with trust enabled can change its MAC address even if a MAC address has
been set by the PF as shown in #1 above.  This will be useful in some
bonding configurations where MAC address changes may be required.

Note VF trust attribute is supported on kernel 4.4 or newer and iproute utility
4.5 or newer.

6. Set VF queues:

   ip link set <pf> vf <vf_index> min_tx_queues <count> max_tx_queues <count> \
                                  min_rx_queues <count> max_rx_queues <count>

Note that this is an experimental way to configure VF queue resources
from the PF and requires the experimental kernel patch and iproute2
patch.  The official method to configure this in the official mainline
kernel will be likely very different when it becomes available.  This
experimental method will be deprecated at that time.

The PF initially divides queue resources equally among the VFs.  This
command reconfigures the VF queue resources for an individual VF by
increasing or decreasing TX and RX queue parameters.  The minimum
parameter represents the guaranteed resource for the VF and the maximum
parameter represents the maximum but not necessarily guaranteed resource
for the VF.  These are raw queue resources used to create the channels
reported by "ethtool -l" on the VF.  For example, an ethtool channel
may require 2 RX queues if hardware GRO/LRO or jumbo MTU is in use.

After queues are successfully reconfigured, it may require the VF to be
brought down and up before it will take effect.  ethtool -l on the VF
should show different channel parameters after the new queue parameters
take effect on the VF.

Example:

   ip link set eth0 vf 0 min_tx_queues 8 max_tx_queues 16 \
                         min_rx_queues 16 max_rx_queues 32


Virtual Ethernet Bridge (VEB)
=============================

The NetXtreme-C/E devices contain an internal hardware Virtual Ethernet
Bridge (VEB) to bridge traffic between virtual ports enabled by SR-IOV.
VEB is normally turned on by default.  VEB can be switched to VEPA
(Virtual Ethernet Port Aggregator) mode if an external VEPA switch is used
to provide bridging between the virtual ports.

Use the bridge command to switch between VEB/VEPA mode.  Note that only
the PF driver will accept the command for all virtual ports belonging to the
same physical port.  The bridge mode cannot be changed if there are multiple
PFs sharing the same physical port (e.g. NPAR or Multi-Host).

To set the bridge mode:

   bridge link set dev <pf> hwmode {veb/vepa}

To show the bridge mode:

   bridge link show dev <pf>

Example:

   bridge link set dev eth0 hwmode vepa

Note that older firmware does not support VEPA mode.  This operation is
also not supported on older kernels.


Hardware QoS
============

The NetXtreme-C/E devices support hardware QoS.  The hardware has multiple
internal queues, each can be configured to support different QoS attributes,
such as latency, bandwidth, lossy or lossless data delivery.  These QoS
attributes are specified in the IEEE Data Center Bridging (DCB) standard
extensions to Ethernet.  DCB parameters include Enhanced Transmission
Selection (ETS) and Priority-based Flow Control (PFC).  In a DCB network,
all traffic will be classified into multiple Traffic Classes (TCs), each
of which is assigned different DCB parameters.

Typically, all traffic is VLAN tagged with a 3-bit priority in the VLAN
tag.  The VLAN priority is mapped to a TC.  For example, a network with
3 TCs may have the following priority to TC mapping:

0:0,1:0,2:0,3:2,4:1,5:0,6:0,7:0

This means that priorities 0,1,2,5,6,7 are mapped to TC0, priority 3 to TC2,
and priority 4 to TC1.  ETS allows bandwidth assigment for the TCs.  For
example, the ETS bandwidth assignment may be 40%, 50%, and 10% to TC0, TC1,
and TC2 respectively.  PFC provides link level flow control for each VLAN
priority independently.  For example, if PFC is enabled on VLAN priority 4,
then only TC1 will be subject to flow control without affecting the other
two TCs.

Typically, DCB parameters are automatically configured using the DCB
Capabilities Exchange protocol (DCBX).  The bnxt_en driver currently
supports the Linux lldpad DCBX agent.  lldpad supports all versions of
DCBX but the bnxt_en driver currently only supports the IEEE DCBX version.
Typically, the DCBX enabled switch will convey the DCB parameters to lldpad
which will then send the hardware QoS parameters to bnxt_en to configure
the device.  Refer to the lldpad(8) and lldptool(8) man pages for further
information on how to setup the lldpad DCBX agent.

Note that the embedded firmware DCBX/LLDP agent must be disabled in order
to run the lldpad agent in host software.  Refer to other Broadcom
documentation on how to disable the firmware agent in NVRAM.

To support hardware TCs, the proper Linux qdisc must be used to classify
outgoing traffic into their proper hardware TCs.  For example, the mqprio
qdisc may be used.  A simple example using mqprio qdisc is illustrated below.
Refer to the tc-mqprio(8) man page for more information.

   tc qdisc add dev eth0 root mqprio num_tc 3 map 0 0 0 2 1 0 0 0 hw 1

The above command creates the mqprio qdisc with 3 hardware TCs.  The priority
to TC mapping is the same as the example at the beginning of the section.
The bnxt_en driver will create 3 groups of tx rings, with each group mapping
to an internal hardware TC.

Once this is created, SKBs with different priorities will be mapped to the
3 TCs according to the specified map above.  Note that this SKB priority
is only used to direct packets within the kernel stack to the proper hardware
ring.  If the outgoing packets are VLAN tagged, the SKB priority does not
automatically map to the VLAN priority of the packet.  The VLAN egress map
has to be set up to have the proper VLAN priority for each packet.

In the current example, if VLAN 100 is used for all traffic, the VLAN egress
map can be set up like this:

   ip link add link eth0 name eth0.100 type vlan id 100 \
      egress 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7

This creates a one-to-one mapping of SKB priority to VLAN egress priority.
In other words, SKB priority 0 maps VLAN priority 0, SKB priority 1 maps to
VLAN priority 1, etc.  This one-to-one mapping should generally be used.

Instead of using VLAN priority to map to TCs in the network, it is also
possible to use DSCP (Differentiated Services Code Point) in the IP header
to do the mapping.  Obviously, only IP traffic in the network can be mapped
this way, whereas VLAN priority will work universally for all traffic types.
The DSCP to priority mapping is specified using a new Application Priority TLV
recently added to the IEEE DCBX spec.  This is supported by the driver's
interface to lldpad.  Note that the application is responsible to set
the proper DSCP value in the IP header for outgoing traffic on a Linux host.
For example, iptables may be used to set the proper DSCP values for outgoing
traffic.  This will replace the VLAN egress mapping mentioned earlier if DSCP
is used instead of VLAN.  The rest of the steps are the same between VLAN and
DSCP.

If each TC has more than one ring, TSS will be performed to select a tx ring
within the TC.

To display the current qdisc configuration:

   tc qdisc show

Example output:

    qdisc mqprio 8010: dev eth0 root  tc 3 map 0 0 0 2 1 0 0 0 0 0 0 0 0 0 0 0
                 queues:(0:3) (4:7) (8:11)

The example above shows that bnxt_en has allocated 4 tx rings for each of the
3 TCs.  SKBs with priorities 0,1,2,5,6,7 will be transmitted using tx rings
0 to 3 (TC0).  SKBs with priority 4 will be transmitted using rings 4 to 7
(TC1).  SKBs with priority 3 will be transmitted using rings 8 to 11 (TC2).

Next, SKB priorities have to be set for different applications so that the
packets from the different applications will be mapped to the proper TCs.
By default, the SKB priority is set to 0.  There are multiple methods to set
SKB priorities.  net_prio cgroup is a convenient way to do this.  Refer to the
link below for more information:

https://www.kernel.org/doc/Documentation/cgroup-v1/net_prio.txt

As mentioned previously, the DCB attributes of each TC are normally configured
by the DCBX agent in lldpad.  It is also possible to set the DCB attributes
manually in a simple network or for test purposes.  The following example
will manually set up eth0 with the example DCB local parameters mentioned at
the beginning of the section.

   lldpad -d
   lldptool -T -i eth0 -V ETS-CFG tsa=0:ets,1:ets,2:ets \
            up2tc=0:0,1:0,2:0,3:2,4:1,5:0,6:0,7:0 \
            tcbw=40,50,10
   lldptool -T -i eth0 -V PFC enabled=4

Note that the ETS bandwidth distribution will only be evident when all
traffic classes are transmitting and reaching the link capacity.

RoCE APP TLV can also be set.  For example, to map RoCE v2 traffic
to priority 4:

lldptool -T -i eth0 -V APP app=4,3,4791

The bnxt_re driver will automatically obtain the proper priority and TC
mapping for offloaded RoCE traffic (RoCE v2 traffic mapped to priority 4 and
TC1 with PFC enabled in this example).

See lldptool-ets(8), lldptool-pfc(8), lldptool-app(8) man pages for more
information.

On an NPAR device with multiple partitions sharing the same network port,
DCBX cannot be run on more than one partition.  In other words, the lldpad
adminStatus can be set to rxtx on no more than one partition.  The same is
true for SRIOV virtual functions.  DCBX cannot be run on the VFs.

On these multi-function devices, the hardware TCs are generally shared
between all the functions.  The DCB parameters negotiated and setup on
the main function (NPAR or PF function) will be the same on the other
functions sharing the same port.  Note that the standard lldptool will
not be able to show the DCB parameters on the other functions which have
adminStatus disabled.


PTP Hardware Clock
==================

The NetXtreme-C/E devices support PTP Hardware Clock which provides hardware
timestamps for PTP v2 packets.  The Linux PTP project contains more
information about this feature.  A newer 4.x kernel and newer firmware
(2.6.134 or newer) are required to use this feature.  Only the first PF
of the network port has access to the hardware PTP feature.  Use ethtool -T
to check if PTP Hardware Clock is supported.


Set IRQ Balance Manually
========================

On newer 4.x kernels, the driver does IRQ affinity for higher performance.
But in older kernels, if driver is reloaded IRQ affinity will not be set
properly. In the case where IRQ affinity is not properly set, the interrupts
will be manually associated with a CPU using SMP affinity.

To manually balance interrupts, the `irqbalance` service needs to be stopped.

   service irqbalance stop

View the CPU cores where NetXtreme-C/E device's interrupt is allowed to be
received.

   grep "ethX" /proc/interrupts
   cat /proc/irq/<irq #>/smp_affinity_list

Associate each interrupt with a CPU core.

   echo <CPU #> > /proc/irq/<irq #>/smp_affinity_list


BNXT_EN Module Parameters
=========================

On newer 3.x/4.x kernels, the driver does not support any driver parameters.
Please use standard tools (sysfs, ethtool, iproute2, etc) to configure the
driver.

The only exception is the "num_vfs" module parameter supported on older 2.6
kernels to enable SR-IOV.  Please see the SR-IOV section above.


BNXT_EN Driver Defaults
=======================

Speed :                    1G/2.5G/10G/25G/40G/50G/100G depending on the board.

Flow control :             None

MTU :                      1500 (range 60 - 9500)

Rx Ring Size :             511 (range 0 - 2047)

Rx Jumbo Ring Size :       2044 (range 0 - 8191) automatically adjusted by the
                           driver.

Tx Ring Size :             511 (range (MAX_SKB_FRAGS+1) - 2047)

                           MAX_SKB_FRAGS varies on different kernels and
                           different architectures. On a 2.6/3.x kernel for
                           x86, MAX_SKB_FRAGS is 18.

Number of RSS/TSS channels:Up to 8 combined channels to match the number of
                           CPUs

TSO :                      Enabled

GRO (hardware) :           Enabled

LRO :                      Disabled

Coalesce rx usecs :        12 usec

Coalesce rx usecs irq :    1 usec

Coalesce rx frames :       15 frames

Coalesce rx frames irq :   1 frame

Coalesce tx usecs :        25 usec

Coalesce tx usecs irq :    2 usec

Coalesce tx frames :       30 frames

Coalesce tx frames irq :   2 frame

Coalesce stats usecs :     1000000 usec (range 250000 - 1000000, 0 to disable)


Statistics
==========

The driver reports all major standard network counters to the stack.  These
counters are reported in /proc/net/dev or by other standard tools such as
netstat -i.

Note that the counters are updated every second by the firmware by
default.  To increase the frequency of these updates, ethtool -C can
be used to increase the frequency to 0.25 seconds if necessary.

More detailed statistics are reported by ethtool -S.  Some of the counters
reported by ethtool -S are for diagnostics purposes only.  For example,
the "rx_drops" counter reported by ethtool -S includes dropped packets
that don't match the unicast and multicast filters in the hardware.  A
non-zero count is normal and does not generally reflect any error conditions.
This counter should not be confused with the "RX-DRP" counter reported by
netstat -i.  The latter reflects dropped packets due to buffer overflow
conditions.

Another example is the "tpa_aborts" counter reported by ethtool -S.  It
counts the LRO (Large Receive Offload) aggregation aborts due to normal
TCP conditions.  A high tpa_aborts count is generally not an indication
of any errors.

The "rx_ovrsz_frames" counter reported by ethtool -S may count all
packets bigger than 1518 bytes when using earlier versions of the firmware.
Newer version of the firmware has reprogrammed the counter to count
packets bigger than 9600 bytes.


Unloading and Removing Driver
=============================

rmmod bnxt_en

Note that if SR-IOV is enabled and there are active VFs running in VMs, the
PF driver should never be unloaded.  It can cause catastrophic failures such
as kernel panics or reboots.  The only time the PF driver can be unloaded
with active VFs is when all the VFs and the PF are running in the same host
kernel environment with one driver instance controlling the PF and all the
VFs.  Using Linux Containers is one such example where the PF driver can be
unloaded to gracefully shutdown the PF and all the VFs.


Updating Firmware for Broadcom NetXtreme-C and NetXtreme-E devices
==================================================================

Controller firmware may be updated using the Linux request_firmware interface
in conjunction with the ethtool "flash device" interface.

Using the ethtool utility, the controller's boot processor firmware may be
updated by copying the 2 "boot code" firmware files to the local /lib/firmware/
directory:

    cp bc1_cm_a.bin bc2_cm_a.bin /lib/firmware

and then issuing the following 2 ethtool commands (both are required):

    ethtool -f <device> bc1_cm_a.bin 4

    ethtool -f <device> bc2_cm_a.bin 18

NVM packages (*.pkg files) containing controller firmware, microcode, 
pre-boot software and configuration data may be installed into a controller's
NVRAM using the ethtool utility by first copying the .pkg file to the local
/lib/firmware/ directory and then executing a single command:

    ethtool -f <device> <filename.pkg>

Note: do not specify the full path to the file on the ethtool -f command-line.

Note: root privileges are required to successfully execute these commands.

After "flashing" new firmware into the controller's NVRAM, a cold restart of 
the system is required for the new firmware to take effect. This requirement
will be removed in future firmware and driver versions.

Updating Firmware for Broadcom Nitro device
===========================================

Nitro controller firmware should be updated from Uboot prompt by following the
below steps

    sf probe
    sf erase 0x50000 0x30000
    tftpboot 0x85000000 <location>/chimp_xxx.bin
    sf write 0x85000000 0x50000 <size in hex>

Devlink
=======

In newer kernels, some operations on bnxt_en driver can be done using devlink.
Devlink tool is part of iproute2 routing commands and utilities. Latest devlink
can be downloaded from http://www.kernel.org/pub/linux/utils/net/iproute2/,
if it is not already installed. Following are some examples on how to use
devlink. See the devlink man page for more information.

devlink examples:

1. Command to display board information and firmware versions:

   devlink dev info [DEV]

Example:
   devlink dev info pci/0000:3b:00.0

will show:

pci/0000:3b:00.0:
  driver bnxt_en
  serial_number B0-26-28-FF-FE-C8-85-20
  versions:
      fixed:
        asic.id 1750
        asic.rev 1
      running:
        fw 216.0.167.0
        fw.psid 0.0.6
        fw.app 216.0.167.0
        fw.mgmt 864.0.32.0
        fw.roce 216.0.157.0

2. Updating firmware:

   devlink dev flash DEV file PATH

Example:
   devlink dev flash pci/0000:3b:00.0 file BCM957454A4540C.pkg

Note: File path is relative to /lib/firmware directory.

3. Dump device level driver parameter information:

   devlink dev param show

4. Display the device level driver parameter information:

   devlink dev param show [ DEV name PARAMETER ]

Example:
   devlink dev param show pci/0000:3b:00.0 name enable_sriov

will show:

pci/0000:3b:00.0:
  name enable_sriov type generic
    values:
      cmode permanent value true

5. Set the device level driver parameter information:

   devlink dev param set DEV name PARAMETER value VALUE cmode \
                         { runtime | driverinit | permanent }

Example:
   devlink dev param set pci/0000:3b:00.0 name enable_sriov \
               value false cmode permanent

6. Dump health reporter information:

   devlink health show [ DEV reporter REPORTER ]

Example:
   devlink health show pci/0000:3b:00.0 reporter fw

will show:

pci/0000:3b:00.0:
  name fw
    state healthy error 0 recover 0

Example2:
    devlink health show pci/0000:af:00.0 reporter fw_reset

will show:

pci/0000:af:00.0:
  reporter fw_reset
    state healthy error 0 recover 0 grace_period 0 auto_recover true

Note that health reporters are created only when corresponding features
are enabled in NVM configuration.

7. Run diagnostics via health reporter:

   devlink health diagnose DEV reporter REPORTER

Example:
   devlink health diag pci/0008:01:00.1 reporter fw

will show:

Description: Encountered fatal error and cannot recover Error code: 4 \
             Reset count: 19

Note that only "fw" health reporter supports diagnose callback.

8. See devlink man pages for more options.
