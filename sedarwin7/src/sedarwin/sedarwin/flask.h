/* This file is automatically generated.  Do not edit. */
#ifndef _SELINUX_FLASK_H_
#define _SELINUX_FLASK_H_

/*
 * Security object class definitions
 */
#define SECCLASS_SECURITY                                1
#define SECCLASS_PROCESS                                 2
#define SECCLASS_SYSTEM                                  3
#define SECCLASS_CAPABILITY                              4
#define SECCLASS_FILESYSTEM                              5
#define SECCLASS_FILE                                    6
#define SECCLASS_DIR                                     7
#define SECCLASS_FD                                      8
#define SECCLASS_LNK_FILE                                9
#define SECCLASS_CHR_FILE                                10
#define SECCLASS_BLK_FILE                                11
#define SECCLASS_SOCK_FILE                               12
#define SECCLASS_FIFO_FILE                               13
#define SECCLASS_SOCKET                                  14
#define SECCLASS_TCP_SOCKET                              15
#define SECCLASS_UDP_SOCKET                              16
#define SECCLASS_RAWIP_SOCKET                            17
#define SECCLASS_NODE                                    18
#define SECCLASS_NETIF                                   19
#define SECCLASS_NETLINK_SOCKET                          20
#define SECCLASS_PACKET_SOCKET                           21
#define SECCLASS_KEY_SOCKET                              22
#define SECCLASS_UNIX_STREAM_SOCKET                      23
#define SECCLASS_UNIX_DGRAM_SOCKET                       24
#define SECCLASS_SEM                                     25
#define SECCLASS_MSG                                     26
#define SECCLASS_MSGQ                                    27
#define SECCLASS_SHM                                     28
#define SECCLASS_IPC                                     29
#define SECCLASS_MACH_PORT                               30
#define SECCLASS_MACH_TASK                               31

/*
 * Security identifier indices for initial entities
 */
#define SECINITSID_KERNEL                               1
#define SECINITSID_SECURITY                             2
#define SECINITSID_UNLABELED                            3
#define SECINITSID_FS                                   4
#define SECINITSID_FILE                                 5
#define SECINITSID_FILE_LABELS                          6
#define SECINITSID_INIT                                 7
#define SECINITSID_ANY_SOCKET                           8
#define SECINITSID_PORT                                 9
#define SECINITSID_NETIF                                10
#define SECINITSID_NETMSG                               11
#define SECINITSID_NODE                                 12
#define SECINITSID_IGMP_PACKET                          13
#define SECINITSID_ICMP_SOCKET                          14
#define SECINITSID_TCP_SOCKET                           15
#define SECINITSID_SYSCTL_MODPROBE                      16
#define SECINITSID_SYSCTL                               17
#define SECINITSID_SYSCTL_FS                            18
#define SECINITSID_SYSCTL_KERNEL                        19
#define SECINITSID_SYSCTL_NET                           20
#define SECINITSID_SYSCTL_NET_UNIX                      21
#define SECINITSID_SYSCTL_VM                            22
#define SECINITSID_SYSCTL_DEV                           23
#define SECINITSID_KMOD                                 24
#define SECINITSID_DEVFS                                25
#define SECINITSID_DEVPTS                               26
#define SECINITSID_NFS                                  27
#define SECINITSID_POLICY                               28
#define SECINITSID_SCMP_PACKET                          29
#define SECINITSID_DEVNULL                              30
#define SECINITSID_TMPFS                                31

#define SECINITSID_NUM                                  31

#endif
