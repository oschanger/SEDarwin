/*
 * Copyright (c) 1995-2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_syscalls.c	8.41 (Berkeley) 6/15/95
 */
/*
 * NOTICE: This file was modified by McAfee Research in 2004 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/extattr.h>
#include <sys/attr.h>
#include <sys/sysctl.h>
#include <sys/ubc.h>
#include <sys/quota.h>

#include <bsm/audit_kernel.h>
#include <bsm/audit_kevents.h>

#include <machine/cons.h>
#include <miscfs/specfs/specdev.h>

#include <architecture/byte_order.h>

struct lock__bsd__	exchangelock;

/*
 * The currently logged-in user, for ownership of files/directories whose on-disk
 * permissions are ignored:
 */
uid_t console_user;

static int change_directory __P((struct nameidata *ndp, struct proc *p));
static void checkdirs __P((struct vnode *olddp));
static void enablequotas __P((struct proc *p, struct mount *mp));
void notify_filemod_watchers(struct vnode *vp, struct proc *p);

/* counts number of mount and unmount operations */
unsigned int vfs_nummntops=0;

/*
 * Virtual File System System Calls
 */

/*
 * Mount a file system.
 */
struct mount_args {
	char	*type;
	char	*path;
	int	flags;
	caddr_t	data;
};
/* ARGSUSED */
int
mount(p, uap, retval)
	struct proc *p;
	register struct mount_args *uap;
	register_t *retval;
{
	struct vnode *vp;
	struct mount *mp;
	struct vfsconf *vfsp;
	int error, flag, err2;
	struct vattr va;
	u_long fstypenum;
	struct nameidata nd;
	char fstypename[MFSNAMELEN];
	size_t dummy=0;

	AUDIT_ARG(fflags, uap->flags);

	/*
	 * Get vnode to be covered
	 */
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	
	if ((vp->v_flag & VROOT) &&
		(vp->v_mount->mnt_flag & MNT_ROOTFS)) 
			uap->flags |= MNT_UPDATE;
	
	if (uap->flags & MNT_UPDATE) {
		if ((vp->v_flag & VROOT) == 0) {
			vput(vp);
			return (EINVAL);
		}
		mp = vp->v_mount;

		if (vfs_busy(mp, LK_NOWAIT, 0, p)) {
			vput(vp);
			return (EBUSY);
		}
		/*
		 * We only allow the filesystem to be reloaded if it
		 * is currently mounted read-only.
		 */
		if ((uap->flags & MNT_RELOAD) &&
		    ((mp->mnt_flag & MNT_RDONLY) == 0)) {
		        vfs_unbusy(mp, p);
			vput(vp);
			return (EOPNOTSUPP);	/* Needs translation */
		}
		/*
		 * Only root, or the user that did the original mount is
		 * permitted to update it.
		 */
		if (mp->mnt_stat.f_owner != p->p_ucred->cr_uid &&
		    (error = suser(p->p_ucred, &p->p_acflag))) {
		        vfs_unbusy(mp, p);
			vput(vp);
			return (error);
		}
		/*
		 * Do not allow NFS export by non-root users. FOr non-root
		 * users, silently enforce MNT_NOSUID and MNT_NODEV, and
		 * MNT_NOEXEC if mount point is already MNT_NOEXEC.
		 */
		if (p->p_ucred->cr_uid != 0) {
			if (uap->flags & MNT_EXPORTED) {
			        vfs_unbusy(mp, p);
				vput(vp);
				return (EPERM);
			}
			uap->flags |= MNT_NOSUID | MNT_NODEV;
			if (mp->mnt_flag & MNT_NOEXEC)
				uap->flags |= MNT_NOEXEC;
		}
		flag = mp->mnt_flag;

		mp->mnt_flag |=
		    uap->flags & (MNT_RELOAD | MNT_FORCE | MNT_UPDATE);

		VOP_UNLOCK(vp, 0, p);

		goto update;
	}
	/*
	 * If the user is not root, ensure that they own the directory
	 * onto which we are attempting to mount.
	 */
	if ((error = VOP_GETATTR(vp, &va, p->p_ucred, p)) ||
	    (va.va_uid != p->p_ucred->cr_uid &&
	     (error = suser(p->p_ucred, &p->p_acflag)))) {
		vput(vp);
		return (error);
	}
	/*
	 * Do not allow NFS export by non-root users. FOr non-root
	 * users, silently enforce MNT_NOSUID and MNT_NODEV, and
	 * MNT_NOEXEC if mount point is already MNT_NOEXEC.
	 */
	if (p->p_ucred->cr_uid != 0) {
		if (uap->flags & MNT_EXPORTED) {
			vput(vp);
			return (EPERM);
		}
		uap->flags |= MNT_NOSUID | MNT_NODEV;
		if (vp->v_mount->mnt_flag & MNT_NOEXEC)
			uap->flags |= MNT_NOEXEC;
	}
	if (error = vinvalbuf(vp, V_SAVE, p->p_ucred, p, 0, 0)) {
		vput(vp);
		return (error);
	}
	if (vp->v_type != VDIR) {
		vput(vp);
		return (ENOTDIR);
	}
#if COMPAT_43
	/*
	 * Historically filesystem types were identified by number. If we
	 * get an integer for the filesystem type instead of a string, we
	 * check to see if it matches one of the historic filesystem types.
	 */
	fstypenum = (u_long)uap->type;
	if (fstypenum < maxvfsconf) {
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == fstypenum)
				break;
		if (vfsp == NULL) {
			vput(vp);
			return (ENODEV);
		}
		strncpy(fstypename, vfsp->vfc_name, MFSNAMELEN);
	} else
#endif /* COMPAT_43 */
	if (error = copyinstr(uap->type, fstypename, MFSNAMELEN, &dummy)) {
		vput(vp);
		return (error);
	}
	/* XXXAUDIT: Should we capture the type on the error path as well? */
	AUDIT_ARG(text, fstypename);
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	if (vfsp == NULL) {
		vput(vp);
		return (ENODEV);
	}
	simple_lock(&vp->v_interlock);
	if (ISSET(vp->v_flag, VMOUNT) && (vp->v_mountedhere != NULL)) {
		simple_unlock(&vp->v_interlock);
		vput(vp);
		return (EBUSY);
	}
	SET(vp->v_flag, VMOUNT);
	simple_unlock(&vp->v_interlock);

	/*
	 * Allocate and initialize the filesystem.
	 */
	MALLOC_ZONE(mp, struct mount *, (u_long)sizeof(struct mount),
		M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));

	/* Initialize the default IO constraints */
	mp->mnt_maxreadcnt = mp->mnt_maxwritecnt = MAXPHYS;
	mp->mnt_segreadcnt = mp->mnt_segwritecnt = 32;

	lockinit(&mp->mnt_lock, PVFS, "vfslock", 0, 0);
	(void)vfs_busy(mp, LK_NOWAIT, 0, p);
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_vfc = vfsp;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_vnodecovered = vp;
	mp->mnt_stat.f_owner = p->p_ucred->cr_uid;
#ifdef MAC
	mac_init_mount(mp);
	mac_create_mount(p->p_ucred, mp);
#endif
	VOP_UNLOCK(vp, 0, p);

update:
	/*
	 * Set the mount level flags.
	 */
	if (uap->flags & MNT_RDONLY)
		mp->mnt_flag |= MNT_RDONLY;
	else if (mp->mnt_flag & MNT_RDONLY)
		mp->mnt_kern_flag |= MNTK_WANTRDWR;
	mp->mnt_flag &= ~(MNT_NOSUID | MNT_NOEXEC | MNT_NODEV |
			  MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC |
			  MNT_UNKNOWNPERMISSIONS | MNT_DONTBROWSE | MNT_AUTOMOUNTED);
	mp->mnt_flag |= uap->flags & (MNT_NOSUID | MNT_NOEXEC |	MNT_NODEV |
				      MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC |
				      MNT_UNKNOWNPERMISSIONS | MNT_DONTBROWSE | MNT_AUTOMOUNTED);
	/*
	 * Mount the filesystem.
	 */
	error = VFS_MOUNT(mp, uap->path, uap->data, &nd, p);

	if (uap->flags & MNT_UPDATE) {
		vrele(vp);
		if (mp->mnt_kern_flag & MNTK_WANTRDWR)
			mp->mnt_flag &= ~MNT_RDONLY;
		mp->mnt_flag &=~
		    (MNT_UPDATE | MNT_RELOAD | MNT_FORCE);
		mp->mnt_kern_flag &=~ MNTK_WANTRDWR;
		if (error)
			mp->mnt_flag = flag;
		vfs_unbusy(mp, p);
		if (!error)
		        enablequotas(p, mp);
		return (error);
	}

	/* get the vnode lock */
	err2 = vn_lock(vp,  LK_EXCLUSIVE|LK_RETRY, p);

	/*
	 * Put the new filesystem on the mount list after root.
	 */
	cache_purge(vp);
	if (!error && !err2) {
		simple_lock(&vp->v_interlock);
		CLR(vp->v_flag, VMOUNT);
		vp->v_mountedhere =mp;
		simple_unlock(&vp->v_interlock);
		simple_lock(&mountlist_slock);
		CIRCLEQ_INSERT_TAIL(&mountlist, mp, mnt_list);
		simple_unlock(&mountlist_slock);
		vfs_event_signal(NULL, VQ_MOUNT, NULL);
		checkdirs(vp);
		VOP_UNLOCK(vp, 0, p);
		vfs_unbusy(mp, p);
		if (error = VFS_START(mp, 0, p))
			vrele(vp);

		/* increment the operations count */
		if (!error) {
			vfs_nummntops++;
			enablequotas(p, mp);
		}
	} else {
		simple_lock(&vp->v_interlock);
		CLR(vp->v_flag, VMOUNT);
		simple_unlock(&vp->v_interlock);
		mp->mnt_vfc->vfc_refcount--;

		if (mp->mnt_kern_flag & MNTK_IO_XINFO)
		        FREE(mp->mnt_xinfo_ptr, M_TEMP);
#ifdef MAC
		mac_destroy_mount(mp);
#endif
		vfs_unbusy(mp, p);
		FREE_ZONE((caddr_t)mp, sizeof (struct mount), M_MOUNT);
		if (err2)
			vrele(vp);
		else
			vput(vp);
	}
	return (error);
}

static void
enablequotas(p, mp)
     struct proc *p;
     struct mount *mp;
{
	struct vnode *vp;  
	struct nameidata qnd;
	int type;
	char qfpath[MAXPATHLEN];
	char *qfname = QUOTAFILENAME;
	char *qfopsname = QUOTAOPSNAME;
	char *qfextension[] = INITQFNAMES;


        if ((strcmp(mp->mnt_stat.f_fstypename, "hfs") != 0 )
                && (strcmp( mp->mnt_stat.f_fstypename, "ufs") != 0))
	  return;

	/* 
	 * Enable filesystem disk quotas if necessary.
	 * We ignore errors as this should not interfere with final mount
	 */
	for (type=0; type < MAXQUOTAS; type++) {
	      sprintf(qfpath, "%s/%s.%s", mp->mnt_stat.f_mntonname, qfopsname, qfextension[type]);
	      NDINIT(&qnd, LOOKUP, FOLLOW, UIO_SYSSPACE, qfpath, p);
	      if (namei(&qnd) != 0)
		    continue; 	    /* option file to trigger quotas is not present */
	      vp = qnd.ni_vp;
	      sprintf(qfpath, "%s/%s.%s", mp->mnt_stat.f_mntonname, qfname, qfextension[type]);
	      if (vp->v_tag == VT_HFS) {
		    vrele(vp);
		    (void)hfs_quotaon(p, mp, type, qfpath, UIO_SYSSPACE);
	      } else if (vp->v_tag == VT_UFS) {
		    vrele(vp);
		    (void)quotaon(p, mp, type, qfpath, UIO_SYSSPACE);
	      } else {
		    vrele(vp);
	      }
	}
	return;
}

/*
 * Scan all active processes to see if any of them have a current
 * or root directory onto which the new filesystem has just been
 * mounted. If so, replace them with the new mount point.
 */
static void
checkdirs(olddp)
	struct vnode *olddp;
{
	struct filedesc *fdp;
	struct vnode *newdp;
	struct proc *p;
	struct vnode *tvp;

	if (olddp->v_usecount == 1)
		return;
	if (VFS_ROOT(olddp->v_mountedhere, &newdp))
		panic("mount: lost mount");
	for (p = allproc.lh_first; p != 0; p = p->p_list.le_next) {
		fdp = p->p_fd;
		if (fdp->fd_cdir == olddp) {
			VREF(newdp);
			tvp = fdp->fd_cdir;
			fdp->fd_cdir = newdp;
			vrele(tvp);
		}
		if (fdp->fd_rdir == olddp) {
			VREF(newdp);
			tvp = fdp->fd_rdir;
			fdp->fd_rdir = newdp;
			vrele(tvp);
		}
	}
	if (rootvnode == olddp) {
		VREF(newdp);
		tvp = rootvnode;
		rootvnode = newdp;
		vrele(tvp);
	}
	vput(newdp);
}

/*
 * Unmount a file system.
 *
 * Note: unmount takes a path to the vnode mounted on as argument,
 * not special file (as before).
 */
struct unmount_args {
	char	*path;
	int	flags;
};
/* ARGSUSED */
int
unmount(p, uap, retval)
	struct proc *p;
	register struct unmount_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct mount *mp;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	mp = vp->v_mount;

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & VROOT) == 0) {
		vput(vp);
		return (EINVAL);
	}
	vput(vp);
	return (safedounmount(mp, uap->flags, p));
}

/*
 * Do the actual file system unmount, prevent some common foot shooting.
 */
int
safedounmount(mp, flags, p)
	struct mount *mp;
	int flags;
	struct proc *p;
{
	int error;

	/*
	 * Only root, or the user that did the original mount is
	 * permitted to unmount this filesystem.
	 */
	if ((mp->mnt_stat.f_owner != p->p_ucred->cr_uid) &&
	    (error = suser(p->p_ucred, &p->p_acflag)))
		return (error);

	/*
	 * Don't allow unmounting the root file system.
	 */
	if (mp->mnt_flag & MNT_ROOTFS)
		return (EBUSY); /* the root is always busy */

	return (dounmount(mp, flags, p));
}

/*
 * Do the actual file system unmount.
 */
int
dounmount(mp, flags, p)
	register struct mount *mp;
	int flags;
	struct proc *p;
{
	struct vnode *coveredvp;
	int error;

	simple_lock(&mountlist_slock);
	/* XXX post jaguar fix LK_DRAIN - then clean this up */
	if ((flags & MNT_FORCE))
		mp->mnt_kern_flag |= MNTK_FRCUNMOUNT;
	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		simple_unlock(&mountlist_slock);
		mp->mnt_kern_flag |= MNTK_MWAIT;
		if ((error = tsleep((void *)mp, PRIBIO, "dounmount", 0)))
			return (error);
		/*
		 * The prior unmount attempt has probably succeeded.
		 * Do not dereference mp here - returning EBUSY is safest.
		 */
		return (EBUSY);
	}
	mp->mnt_kern_flag |= MNTK_UNMOUNT;
	error = lockmgr(&mp->mnt_lock, LK_DRAIN | LK_INTERLOCK,
			&mountlist_slock, p);
	if (error) {
		mp->mnt_kern_flag &= ~MNTK_UNMOUNT;
		goto out;
	}
	mp->mnt_flag &=~ MNT_ASYNC;
	ubc_umount(mp);	/* release cached vnodes */
	cache_purgevfs(mp);	/* remove cache entries for this file sys */
	if (((mp->mnt_flag & MNT_RDONLY) ||
	     (error = VFS_SYNC(mp, MNT_WAIT, p->p_ucred, p)) == 0) ||
	    (flags & MNT_FORCE))
		error = VFS_UNMOUNT(mp, flags, p);
	simple_lock(&mountlist_slock);
	if (error) {
		mp->mnt_kern_flag &= ~MNTK_UNMOUNT;
		lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK | LK_REENABLE,
		    &mountlist_slock, p);
		goto out;
	}

	/* increment the operations count */
	if (!error)
		vfs_nummntops++;
	CIRCLEQ_REMOVE(&mountlist, mp, mnt_list);
	if ((coveredvp = mp->mnt_vnodecovered) != NULLVP) {
		coveredvp->v_mountedhere = (struct mount *)0;
		simple_unlock(&mountlist_slock);
		vrele(coveredvp);
		simple_lock(&mountlist_slock);
	}
	mp->mnt_vfc->vfc_refcount--;
	if (mp->mnt_vnodelist.lh_first != NULL) {
		 panic("unmount: dangling vnode"); 
	}
#ifdef MAC
	mac_destroy_mount(mp);
#endif
	lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK, &mountlist_slock, p);
	vfs_event_signal(NULL, VQ_UNMOUNT, NULL);
out:
	if (mp->mnt_kern_flag & MNTK_MWAIT)
		wakeup((caddr_t)mp);
	if (!error) {
		if (mp->mnt_kern_flag & MNTK_IO_XINFO)
		        FREE(mp->mnt_xinfo_ptr, M_TEMP);
		FREE_ZONE((caddr_t)mp, sizeof (struct mount), M_MOUNT);
	}
	return (error);
}

/*
 * Sync each mounted filesystem.
 */
#if DIAGNOSTIC
int syncprt = 0;
struct ctldebug debug0 = { "syncprt", &syncprt };
#endif

struct sync_args {
	int	dummy;
};
int print_vmpage_stat=0;

/* ARGSUSED */
int
sync(p, uap, retval)
	struct proc *p;
	struct sync_args *uap;
	register_t *retval;
{
	register struct mount *mp, *nmp;
	int asyncflag;

	simple_lock(&mountlist_slock);
	for (mp = mountlist.cqh_first; mp != (void *)&mountlist; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, p)) {
			nmp = mp->mnt_list.cqe_next;
			continue;
		}
		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			asyncflag = mp->mnt_flag & MNT_ASYNC;
			mp->mnt_flag &= ~MNT_ASYNC;
			VFS_SYNC(mp, MNT_NOWAIT, p->p_ucred, p);
			if (asyncflag)
				mp->mnt_flag |= MNT_ASYNC;
		}
		simple_lock(&mountlist_slock);
		nmp = mp->mnt_list.cqe_next;
		vfs_unbusy(mp, p);
	}
	simple_unlock(&mountlist_slock);

	{
	extern void vm_countdirtypages(void);
	extern unsigned int vp_pagein, vp_pgodirty, vp_pgoclean;
	extern unsigned int dp_pgins, dp_pgouts;
	if(print_vmpage_stat) {
		vm_countdirtypages();
		printf("VP: %d: %d: %d: %d: %d\n", vp_pgodirty, vp_pgoclean, vp_pagein,
			dp_pgins, dp_pgouts);
	}
	}
#if DIAGNOSTIC
	if (syncprt)
		vfs_bufstats();
#endif /* DIAGNOSTIC */
	return (0);
}

/*
 * Change filesystem quotas.
 */
struct quotactl_args {
	char *path;
	int cmd;
	int uid;
	caddr_t arg;
};
/* ARGSUSED */
int
quotactl(p, uap, retval)
	struct proc *p;
	register struct quotactl_args *uap;
	register_t *retval;
{
	register struct mount *mp;
	int error;
	struct nameidata nd;

	AUDIT_ARG(uid, uap->uid, 0, 0, 0);
	AUDIT_ARG(cmd, uap->cmd);
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	mp = nd.ni_vp->v_mount;
	vrele(nd.ni_vp);
	return (VFS_QUOTACTL(mp, uap->cmd, uap->uid,
	    uap->arg, p));
}

/*
 * Get filesystem statistics.
 */
struct statfs_args {
	char *path;
	struct statfs *buf;
};
/* ARGSUSED */
int
statfs(p, uap, retval)
	struct proc *p;
	register struct statfs_args *uap;
	register_t *retval;
{
	register struct mount *mp;
	register struct statfs *sp;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	mp = nd.ni_vp->v_mount;
	sp = &mp->mnt_stat;
	vrele(nd.ni_vp);
	if (error = VFS_STATFS(mp, sp, p))
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	return (copyout((caddr_t)sp, (caddr_t)uap->buf,
		sizeof(*sp)-sizeof(sp->f_reserved3)-sizeof(sp->f_reserved4)));
}

/*
 * Get filesystem statistics.
 */
struct fstatfs_args {
	int fd;
	struct statfs *buf;
};
/* ARGSUSED */
int
fstatfs(p, uap, retval)
	struct proc *p;
	register struct fstatfs_args *uap;
	register_t *retval;
{
	struct file *fp;
	struct mount *mp;
	register struct statfs *sp;
	int error;

	AUDIT_ARG(fd, uap->fd);

	if (error = getvnode(p, uap->fd, &fp))
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	mp = ((struct vnode *)fp->f_data)->v_mount;
	if (!mp)
		return (EBADF);
	sp = &mp->mnt_stat;
	if (error = VFS_STATFS(mp, sp, p))
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	return (copyout((caddr_t)sp, (caddr_t)uap->buf,
		sizeof(*sp)-sizeof(sp->f_reserved3)-sizeof(sp->f_reserved4)));
}

/*
 * Get statistics on all filesystems.
 */
struct getfsstat_args {
	struct statfs *buf;
	long bufsize;
	int flags;
};
int
getfsstat(p, uap, retval)
	struct proc *p;
	register struct getfsstat_args *uap;
	register_t *retval;
{
	register struct mount *mp, *nmp;
	register struct statfs *sp;
	caddr_t sfsp;
	long count, maxcount, error;

	maxcount = uap->bufsize / sizeof(struct statfs);
	sfsp = (caddr_t)uap->buf;
	count = 0;
	simple_lock(&mountlist_slock);
	for (mp = mountlist.cqh_first; mp != (void *)&mountlist; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, p)) {
			nmp = mp->mnt_list.cqe_next;
			continue;
		}
		if (sfsp && count < maxcount) {
			sp = &mp->mnt_stat;
			/*
			 * If MNT_NOWAIT is specified, do not refresh the
			 * fsstat cache. MNT_WAIT overrides MNT_NOWAIT.
			 */
			if (((uap->flags & MNT_NOWAIT) == 0 ||
			    (uap->flags & MNT_WAIT)) &&
			    (error = VFS_STATFS(mp, sp, p))) {
				simple_lock(&mountlist_slock);
				nmp = mp->mnt_list.cqe_next;
				vfs_unbusy(mp, p);
				continue;
			}
			sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
			if (error = copyout((caddr_t)sp, sfsp, sizeof(*sp))) {
				vfs_unbusy(mp, p);
				return (error);
			}
			sfsp += sizeof(*sp);
		}
		count++;
		simple_lock(&mountlist_slock);
		nmp = mp->mnt_list.cqe_next;
		vfs_unbusy(mp, p);
	}
	simple_unlock(&mountlist_slock);
	if (sfsp && count > maxcount)
		*retval = maxcount;
	else
		*retval = count;
	return (0);
}

#if COMPAT_GETFSSTAT
ogetfsstat(p, uap, retval)
	struct proc *p;
	register struct getfsstat_args *uap;
	register_t *retval;
{
	register struct mount *mp, *nmp;
	register struct statfs *sp;
	caddr_t sfsp;
	long count, maxcount, error;

	maxcount = uap->bufsize / (sizeof(struct statfs) - sizeof(sp->f_reserved4));
	sfsp = (caddr_t)uap->buf;
	count = 0;
	simple_lock(&mountlist_slock);
	for (mp = mountlist.cqh_first; mp != (void *)&mountlist; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, p)) {
			nmp = mp->mnt_list.cqe_next;
			continue;
		}
		if (sfsp && count < maxcount) {
			sp = &mp->mnt_stat;
			/*
			 * If MNT_NOWAIT is specified, do not refresh the
			 * fsstat cache. MNT_WAIT overrides MNT_NOWAIT.
			 */
			if (((uap->flags & MNT_NOWAIT) == 0 ||
			    (uap->flags & MNT_WAIT)) &&
			    (error = VFS_STATFS(mp, sp, p))) {
				simple_lock(&mountlist_slock);
				nmp = mp->mnt_list.cqe_next;
				vfs_unbusy(mp, p);
				continue;
			}
			sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
			error = copyout((caddr_t)sp, sfsp,
					sizeof(*sp) - sizeof(sp->f_reserved3)
						- sizeof(sp->f_reserved4));
			if (error) {
				vfs_unbusy(mp, p);
				return (error);
			}
			sfsp += sizeof(*sp) - sizeof(sp->f_reserved4);
		}
		count++;
		simple_lock(&mountlist_slock);
		nmp = mp->mnt_list.cqe_next;
		vfs_unbusy(mp, p);
	}
	simple_unlock(&mountlist_slock);
	if (sfsp && count > maxcount)
		*retval = maxcount;
	else
		*retval = count;
	return (0);
}
#endif

/*
 * Change current working directory to a given file descriptor.
 */
struct fchdir_args {
	int	fd;
};
/* ARGSUSED */
int
fchdir(p, uap, retval)
	struct proc *p;
	struct fchdir_args *uap;
	register_t *retval;
{
	register struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *tdp, *tvp;
	struct mount *mp;
	struct file *fp;
	int error;

	if (error = getvnode(p, uap->fd, &fp))
		return (error);
	vp = (struct vnode *)fp->f_data;
	VREF(vp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	if (vp->v_type != VDIR)
		error = ENOTDIR;
#ifdef MAC
	else if ((error = mac_check_vnode_chdir(p->p_ucred, vp)) != 0) {
        }
#endif
	else
		error = VOP_ACCESS(vp, VEXEC, p->p_ucred, p);
	while (!error && (mp = vp->v_mountedhere) != NULL) {
		if (vfs_busy(mp, LK_NOWAIT, 0, p)) {
			vput(vp);
			return (EACCES);
		}
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp, p);
		if (error)
			break;
		vput(vp);
		vp = tdp;
	}
	if (error) {
		vput(vp);
		return (error);
	}
	VOP_UNLOCK(vp, 0, p);
	tvp = fdp->fd_cdir;
	fdp->fd_cdir = vp;
	vrele(tvp);
	return (0);
}

/*
 * Change current working directory (``.'').
 */
struct chdir_args {
	char	*path;
};
/* ARGSUSED */
int
chdir(p, uap, retval)
	struct proc *p;
	struct chdir_args *uap;
	register_t *retval;
{
	register struct filedesc *fdp = p->p_fd;
	int error;
	struct nameidata nd;
	struct vnode *tvp;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = change_directory(&nd, p);
	if (error)
		return (error);
	VOP_UNLOCK(nd.ni_vp, 0, p);
	tvp = fdp->fd_cdir;
	fdp->fd_cdir = nd.ni_vp;
	vrele(tvp);
	return (0);
}

/*
 * Change notion of root (``/'') directory.
 */
struct chroot_args {
	char	*path;
};
/* ARGSUSED */
int
chroot(p, uap, retval)
	struct proc *p;
	struct chroot_args *uap;
	register_t *retval;
{
	register struct filedesc *fdp = p->p_fd;
	int error;
	struct nameidata nd;
	boolean_t	shared_regions_active;
	struct vnode *tvp;

	if (error = suser(p->p_ucred, &p->p_acflag))
		return (error);

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = change_directory(&nd, p);
	if (error)
		return (error);
#ifdef MAC
	if (error = mac_check_vnode_chroot(p->p_ucred, nd.ni_vp)) {
		vput(nd.ni_vp);
		return error;
	}
#endif
	VOP_UNLOCK(nd.ni_vp, 0, p);
	if(p->p_flag & P_NOSHLIB) {
		shared_regions_active = FALSE;
	} else {
		shared_regions_active = TRUE;
	}

	if(error = clone_system_shared_regions(shared_regions_active, nd.ni_vp)) {
		vrele(nd.ni_vp);
		return (error);
	}

	tvp = fdp->fd_rdir;
	fdp->fd_rdir = nd.ni_vp;
	if (tvp != NULL)
		vrele(tvp);
	return (0);
}

/*
 * Common routine for chroot and chdir.
 * On a successful return, the vnode is locked.
 * When errors are returned, the vnode is unlocked and de-referenced.
 */
static int
change_directory(ndp, p)
	register struct nameidata *ndp;
	struct proc *p;
{
	struct vnode *vp;
	int error;

	if (error = namei(ndp))
		return (error);
	vp = ndp->ni_vp;
	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
	  {
#ifdef MAC
	    error = mac_check_vnode_chdir (p->p_ucred, ndp->ni_vp);
	    if (0 == error)
#endif
		error = VOP_ACCESS(vp, VEXEC, p->p_ucred, p);
	  }
	if (error)
		vput(vp);
	return (error);
}

/*
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
struct open_args {
	char	*path;
	int	flags;
	int	mode;
};
int
open(p, uap, retval)
	struct proc *p;
	register struct open_args *uap;
	register_t *retval;
{
	register struct filedesc *fdp = p->p_fd;
	register struct file *fp;
	register struct vnode *vp;
	int flags, cmode, oflags;
	struct file *nfp;
	int type, indx, error;
	struct flock lf;
	struct nameidata nd;
	extern struct fileops vnops;

	oflags = uap->flags;
	flags = FFLAGS(uap->flags);

	AUDIT_ARG(fflags, oflags);
	AUDIT_ARG(mode, uap->mode);

	cmode = ((uap->mode &~ fdp->fd_cmask) & ALLPERMS) &~ S_ISTXT;

	if ((oflags & O_ACCMODE) == O_ACCMODE)
		return(EINVAL);
	if (error = falloc(p, &nfp, &indx))
		return (error);
	fp = nfp;
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	p->p_dupfd = -indx - 1;			/* XXX check for fdopen */
	if (error = vn_open_modflags(&nd, &flags, cmode)) {
		ffree(fp);
		if ((error == ENODEV || error == ENXIO) &&
		    p->p_dupfd >= 0 &&			/* XXX from fdopen */
		    (error =
			dupfdopen(fdp, indx, p->p_dupfd, flags, error)) == 0) {
			*retval = indx;
			return (0);
		}
		if (error == ERESTART)
			error = EINTR;
		fdrelse(p, indx);
		return (error);
	}
	p->p_dupfd = 0;
	vp = nd.ni_vp;
	fp->f_flag = flags & FMASK;
	fp->f_type = DTYPE_VNODE;
	fp->f_ops = &vnops;
	fp->f_data = (caddr_t)vp;

	VOP_UNLOCK(vp, 0, p);
	if (flags & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((flags & FNONBLOCK) == 0)
			type |= F_WAIT;
#ifdef MAC_XXX
		error = mac_check_file_change_flags(p->p_ucred, fp,
		    fp->f_flag, fp->f_flag | FHASLOCK);
		if (error)
			goto bad;
#endif
                if (error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type))
                        goto bad;
		fp->f_flag |= FHASLOCK;
	}

	if (flags & O_TRUNC) {
		struct vattr vat;
		struct vattr *vap = &vat;

		VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
		(void)vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		/* try to truncate by setting the size attribute */
		error = VOP_SETATTR(vp, vap, p->p_ucred, p);
		VOP_UNLOCK(vp, 0, p);			/* XXX */
		if (error)
			goto bad;
	}

	*fdflags(p, indx) &= ~UF_RESERVED;
	*retval = indx;
	return (0);
bad:
	vn_close(vp, fp->f_flag, fp->f_cred, p);
	ffree(fp);
	fdrelse(p, indx);
	return (error);
}

#if COMPAT_43
/*
 * Create a file.
 */
struct ocreat_args {
	char	*path;
	int	mode;
};
int
ocreat(p, uap, retval)
	struct proc *p;
	register struct ocreat_args *uap;
	register_t *retval;
{
	struct open_args nuap;

	nuap.path = uap->path;
	nuap.mode = uap->mode;
	nuap.flags = O_WRONLY | O_CREAT | O_TRUNC;
	return (open(p, &nuap, retval));
}
#endif /* COMPAT_43 */

/*
 * Create a special file.
 */
struct mknod_args {
	char	*path;
	int	mode;
	int	dev;
};
/* ARGSUSED */
int
mknod(p, uap, retval)
	struct proc *p;
	register struct mknod_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int cmode, error;
	int whiteout;
	struct nameidata nd;

	AUDIT_ARG(mode, uap->mode);
	AUDIT_ARG(dev, uap->dev);
	cmode = (uap->mode & ALLPERMS) &~ p->p_fd->fd_cmask;
	if (error = suser(p->p_ucred, &p->p_acflag))
		return (error);
	bwillwrite();
	NDINIT(&nd, CREATE, LOCKPARENT | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL)
		error = EEXIST;
	else {
		VATTR_NULL(&vattr);
		vattr.va_mode = cmode;
		vattr.va_rdev = uap->dev;
		whiteout = 0;

		switch (uap->mode & S_IFMT) {
		case S_IFMT:	/* used by badsect to flag bad sectors */
			vattr.va_type = VBAD;
			break;
		case S_IFCHR:
			vattr.va_type = VCHR;
			break;
		case S_IFBLK:
			vattr.va_type = VBLK;
			break;
		case S_IFWHT:
			whiteout = 1;
			break;
		default:
			error = EINVAL;
			break;
		}
	}
#ifdef MAC
	if (error == 0 && !whiteout)
		error = mac_check_vnode_create(p->p_ucred, nd.ni_dvp,
		    &nd.ni_cnd, &vattr);
#endif
	if (!error) {
		char *nameptr;
		nameptr = add_name(nd.ni_cnd.cn_nameptr, nd.ni_cnd.cn_namelen, nd.ni_cnd.cn_hash, 0);
		VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
		if (whiteout) {
			error = VOP_WHITEOUT(nd.ni_dvp, &nd.ni_cnd, CREATE);
			if (error)
				VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
			vput(nd.ni_dvp);
		} else {
			error = VOP_MKNOD(nd.ni_dvp, &nd.ni_vp,
						&nd.ni_cnd, &vattr);
		}

		if (error == 0 && nd.ni_vp) {
		    if (VNAME(nd.ni_vp) == NULL) {
			VNAME(nd.ni_vp) = nameptr;
			nameptr = NULL;
		    }
		    if (VPARENT(nd.ni_vp) == NULL) {
			if (vget(nd.ni_dvp, 0, p) == 0) {
			    VPARENT(nd.ni_vp) = nd.ni_dvp;
			}
		    }
		}
		if (nameptr) {
		    remove_name(nameptr);
		    nameptr = NULL;
		}
	} else {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (vp)
			vrele(vp);
	}
	return (error);
}

/*
 * Create a named pipe.
 */
struct mkfifo_args {
	char	*path;
	int	mode;
};
/* ARGSUSED */
int
mkfifo(p, uap, retval)
	struct proc *p;
	register struct mkfifo_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	int error;
	struct nameidata nd;
	char *nameptr=NULL;


#if !FIFO 
	return (EOPNOTSUPP);
#else
	bwillwrite();
	NDINIT(&nd, CREATE, LOCKPARENT | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	if (nd.ni_vp != NULL) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		return (EEXIST);
	}

	nameptr = add_name(nd.ni_cnd.cn_nameptr,
			   nd.ni_cnd.cn_namelen,
			   nd.ni_cnd.cn_hash, 0);
	VATTR_NULL(&vattr);
	vattr.va_type = VFIFO;
	vattr.va_mode = (uap->mode & ALLPERMS) &~ p->p_fd->fd_cmask;
#ifdef MAC
	error = mac_check_vnode_create(p->p_ucred, nd.ni_dvp, &nd.ni_cnd,
	    &vattr);
	if (error)
		return (error);
#endif
	VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
	error = VOP_MKNOD(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);

	if (error == 0 && nd.ni_vp && nd.ni_vp->v_type == VFIFO) {
	    int vpid = nd.ni_vp->v_id;
	    if (vget(nd.ni_vp, 0, p) == 0) {
		if (vpid == nd.ni_vp->v_id && nd.ni_vp->v_type == VFIFO) {
		    VNAME(nd.ni_vp) = nameptr;
		    nameptr = NULL;

		    if (VPARENT(nd.ni_vp) == NULL) {
			if (vget(nd.ni_dvp, 0, p) == 0) {
			    VPARENT(nd.ni_vp) = nd.ni_dvp;
			}
		    }
		}
	    }
	}
	if (nameptr) {
	    remove_name(nameptr);
	}
	return error;
#endif /* FIFO */
}

/*
 * Make a hard file link.
 */
struct link_args {
	char	*path;
	char	*link;
};
/* ARGSUSED */
int
link(p, uap, retval)
	struct proc *p;
	register struct link_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct nameidata nd;
	int error;

	bwillwrite();
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	if (vp->v_type == VDIR)
		error = EPERM;   /* POSIX */
	else {
		nd.ni_cnd.cn_nameiop = CREATE;
		nd.ni_cnd.cn_flags = LOCKPARENT | AUDITVNPATH2;
		nd.ni_dirp = uap->link;
		error = namei(&nd);
		if (error == 0) {
			if (nd.ni_vp != NULL)
				error = EEXIST;
			if (!error) {
				VOP_LEASE(nd.ni_dvp, p, p->p_ucred,
				    LEASE_WRITE);
				VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
#ifdef MAC
				error = mac_check_vnode_link(p->p_ucred, 
				    nd.ni_dvp, vp, &nd.ni_cnd);
			}
			if (error == 0) {
#endif
				error = VOP_LINK(vp, nd.ni_dvp, &nd.ni_cnd);
			} else {
				VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
				if (nd.ni_dvp == nd.ni_vp)
					vrele(nd.ni_dvp);
				else
					vput(nd.ni_dvp);
				if (nd.ni_vp)
					vrele(nd.ni_vp);
			}
		}
	}
	vrele(vp);
	return (error);
}

/*
 * Make a symbolic link.
 */
struct symlink_args {
	char	*path;
	char	*link;
};
/* ARGSUSED */
int
symlink(p, uap, retval)
	struct proc *p;
	register struct symlink_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	char *path, *nameptr;
	int error;
	struct nameidata nd;
	size_t dummy=0;
	u_long vpid;
	
	MALLOC_ZONE(path, char *, MAXPATHLEN, M_NAMEI, M_WAITOK);
	if (error = copyinstr(uap->path, path, MAXPATHLEN, &dummy))
		goto out;
	AUDIT_ARG(text, path);	/* This is the link string */
	bwillwrite();
	NDINIT(&nd, CREATE, LOCKPARENT | AUDITVNPATH1, UIO_USERSPACE, uap->link, p);
	error = namei(&nd);
	if (error)
		goto out;
	if (nd.ni_vp) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		error = EEXIST;
		goto out;
	}
	VATTR_NULL(&vattr);
	vattr.va_mode = ACCESSPERMS &~ p->p_fd->fd_cmask;
#ifdef MAC
	vattr.va_type = VLNK;
	error = mac_check_vnode_create(p->p_ucred, nd.ni_dvp, &nd.ni_cnd,
	    &vattr);
	if (error) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		vput(nd.ni_dvp);
		goto out;
	}
#endif
	VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);

	nameptr = add_name(nd.ni_cnd.cn_nameptr, nd.ni_cnd.cn_namelen, nd.ni_cnd.cn_hash, 0);

	error = VOP_SYMLINK(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr, path);

	// have to do this little dance because nd.ni_vp is not locked
	// on return from the VOP_SYMLINK() call.
	//
	if (error == 0 && nd.ni_vp && nd.ni_vp->v_type == VLNK) {
	    vpid = nd.ni_vp->v_id;
	    if (vget(nd.ni_vp, 0, p) == 0) {
		if (vpid == nd.ni_vp->v_id && nd.ni_vp->v_type == VLNK) {
		    VNAME(nd.ni_vp) = nameptr;
		    nameptr = NULL;

		    if (VPARENT(nd.ni_vp) == NULL && vget(nd.ni_dvp, 0, p) == 0) {
			VPARENT(nd.ni_vp) = nd.ni_dvp;
		    }
		}
		vrele(nd.ni_vp);
	    }
	}
	if (nameptr) {    // only true if we didn't add it to the vnode
	    remove_name(nameptr);
	}
out:
	FREE_ZONE(path, MAXPATHLEN, M_NAMEI);
	return (error);
}

/*
 * Delete a whiteout from the filesystem.
 */
struct undelete_args {
	char	*path;
};
/* ARGSUSED */
int
undelete(p, uap, retval)
	struct proc *p;
	register struct undelete_args *uap;
	register_t *retval;
{
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, DELETE, LOCKPARENT|DOWHITEOUT|AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	if (nd.ni_vp != NULLVP || !(nd.ni_cnd.cn_flags & ISWHITEOUT)) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (nd.ni_vp)
			vrele(nd.ni_vp);
		return (EEXIST);
	}

	VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
	if (error = VOP_WHITEOUT(nd.ni_dvp, &nd.ni_cnd, DELETE))
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
	vput(nd.ni_dvp);
	return (error);
}

/*
 * Delete a name from the filesystem.
 */
struct unlink_args {
	char	*path;
};
/* ARGSUSED */
static int
_unlink(p, uap, retval, nodelbusy)
	struct proc *p;
	struct unlink_args *uap;
	register_t *retval;
	int nodelbusy;
{
	register struct vnode *vp;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, DELETE, LOCKPARENT | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	/* with Carbon semantics, busy files cannot be deleted */
	if (nodelbusy)
		nd.ni_cnd.cn_flags |= NODELETEBUSY;
	error = namei(&nd);
	if (error)
		return (error);

	vp = nd.ni_vp;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	if (vp->v_type == VDIR)
		error = EPERM;	/* POSIX */
	else {
		/*
		 * The root of a mounted filesystem cannot be deleted.
		 *
		 * XXX: can this only be a VDIR case?
		 */
		if (vp->v_flag & VROOT)
			error = EBUSY;
	}

#ifdef MAC
	if (!error)
		error = mac_check_vnode_delete(p->p_ucred, nd.ni_dvp, vp,
		    &nd.ni_cnd);
#endif
	if (!error) {
		VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
		error = VOP_REMOVE(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
	} else {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (vp != NULLVP)
			vput(vp);
	}
	return (error);
}

/*
 * Delete a name from the filesystem using POSIX semantics.
 */
int
unlink(p, uap, retval)
	struct proc *p;
	struct unlink_args *uap;
	register_t *retval;
{
	return _unlink(p, uap, retval, 0);
}

/*
 * Delete a name from the filesystem using Carbon semantics.
 */
int
delete(p, uap, retval)
	struct proc *p;
	struct unlink_args *uap;
	register_t *retval;
{
	return _unlink(p, uap, retval, 1);
}

/*
 * Reposition read/write file offset.
 */
struct lseek_args {
	int	fd;
#ifdef DOUBLE_ALIGN_PARAMS
	int pad;
#endif
	off_t	offset;
	int	whence;
};
int
lseek(p, uap, retval)
	struct proc *p;
	register struct lseek_args *uap;
	register_t *retval;
{
	struct ucred *cred = p->p_ucred;
	struct file *fp;
	struct vnode *vp;
	struct vattr vattr;
	off_t offset = uap->offset;
	int error;

	if (error = fdgetf(p, uap->fd, &fp))
		return (error);
	if (fref(fp) == -1)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE) {
		frele(fp);
		return (ESPIPE);
	}
	vp = (struct vnode *)fp->f_data;
	switch (uap->whence) {
	case L_INCR:
		offset += fp->f_offset;
		break;
	case L_XTND:
		if (error = VOP_GETATTR(vp, &vattr, cred, p))
			break;
		offset += vattr.va_size;
		break;
	case L_SET:
		break;
	default:
		error = EINVAL;
	}
	if (error == 0) {
		if (uap->offset > 0 && offset < 0) {
			/* Incremented/relative move past max size */
			error = EOVERFLOW;
		} else {
			/*
			 * Allow negative offsets on character devices, per
			 * POSIX 1003.1-2001.  Most likely for writing disk
			 * labels.
			 */
			if (offset < 0 && vp->v_type != VCHR) {
				/* Decremented/relative move before start */
				error = EINVAL;
			} else {
				/* Success */
				fp->f_offset = offset;
				*(off_t *)retval = fp->f_offset;
			}
		}
	}
	frele(fp);
	return (error);
}

#if COMPAT_43
/*
 * Reposition read/write file offset.
 */
struct olseek_args {
	int	fd;
	long	offset;
	int	whence;
};
int
olseek(p, uap, retval)
	struct proc *p;
	register struct olseek_args *uap;
	register_t *retval;
{
	struct lseek_args /* {
		syscallarg(int) fd;
#ifdef DOUBLE_ALIGN_PARAMS
        syscallarg(int) pad;
#endif
		syscallarg(off_t) offset;
		syscallarg(int) whence;
	} */ nuap;
	off_t qret;
	int error;

	nuap.fd = uap->fd;
	nuap.offset = uap->offset;
	nuap.whence = uap->whence;
	error = lseek(p, &nuap, &qret);
	*(long *)retval = qret;
	return (error);
}
#endif /* COMPAT_43 */

/*
 * Check access permissions.
 */
struct access_args {
	char	*path;
	int	flags;
};
int
access(p, uap, retval)
	struct proc *p;
	register struct access_args *uap;
	register_t *retval;
{
	register struct ucred *cred = p->p_ucred;
	register struct vnode *vp;
	int error, flags, t_gid, t_uid;
	struct nameidata nd;

	t_uid = cred->cr_uid;
	t_gid = cred->cr_groups[0];
	cred->cr_uid = p->p_cred->p_ruid;
	cred->cr_groups[0] = p->p_cred->p_rgid;
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		goto out1;
	vp = nd.ni_vp;

	/* Flags == 0 means only check for existence. */
	if (uap->flags) {
		flags = 0;
		if (uap->flags & R_OK)
			flags |= VREAD;
		if (uap->flags & W_OK)
			flags |= VWRITE;
		if (uap->flags & X_OK)
			flags |= VEXEC;
#ifdef MAC
		error = mac_check_vnode_access(cred, vp, flags);
		if (error)
			return (error);
#endif
		if ((flags & VWRITE) == 0 || (error = vn_writechk(vp)) == 0)
			error = VOP_ACCESS(vp, flags, cred, p);
	}
	vput(vp);
out1:
	cred->cr_uid = t_uid;
	cred->cr_groups[0] = t_gid;
	return (error);
}

#if COMPAT_43
/*
 * Get file status; this version follows links.
 */
struct ostat_args {
	char	*path;
	struct ostat *ub;
};
/* ARGSUSED */
int
ostat(p, uap, retval)
	struct proc *p;
	register struct ostat_args *uap;
	register_t *retval;
{
	struct stat sb;
	struct ostat osb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	if (error = namei(&nd))
		return (error);
	error = vn_stat(nd.ni_vp, &sb, p);
	vput(nd.ni_vp);
	if (error)
		return (error);
	cvtstat(&sb, &osb);
	error = copyout((caddr_t)&osb, (caddr_t)uap->ub, sizeof (osb));
	return (error);
}

/*
 * Get file status; this version does not follow links.
 */
struct olstat_args {
	char	*path;
	struct ostat *ub;
};
/* ARGSUSED */
int
olstat(p, uap, retval)
	struct proc *p;
	register struct olstat_args *uap;
	register_t *retval;
{
	struct vnode *vp, *dvp;
	struct stat sb, sb1;
	struct ostat osb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, NOFOLLOW | LOCKLEAF | LOCKPARENT | AUDITVNPATH1,
	       UIO_USERSPACE, uap->path, p);
	if (error = namei(&nd))
		return (error);
	/*
	 * For symbolic links, always return the attributes of its
	 * containing directory, except for mode, size, and links.
	 */
	vp = nd.ni_vp;
	dvp = nd.ni_dvp;
	if (vp->v_type != VLNK) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
		error = vn_stat(vp, &sb, p);
		vput(vp);
		if (error)
			return (error);
	} else {
		error = vn_stat(dvp, &sb, p);
		vput(dvp);
		if (error) {
			vput(vp);
			return (error);
		}
		error = vn_stat(vp, &sb1, p);
		vput(vp);
		if (error)
			return (error);
		sb.st_mode &= ~S_IFDIR;
		sb.st_mode |= S_IFLNK;
		sb.st_nlink = sb1.st_nlink;
		sb.st_size = sb1.st_size;
		sb.st_blocks = sb1.st_blocks;
	}
	cvtstat(&sb, &osb);
	error = copyout((caddr_t)&osb, (caddr_t)uap->ub, sizeof (osb));
	return (error);
}

/*
 * Convert from an old to a new stat structure.
 */
void
cvtstat(st, ost)
	struct stat *st;
	struct ostat *ost;
{

	ost->st_dev = st->st_dev;
	ost->st_ino = st->st_ino;
	ost->st_mode = st->st_mode;
	ost->st_nlink = st->st_nlink;
	ost->st_uid = st->st_uid;
	ost->st_gid = st->st_gid;
	ost->st_rdev = st->st_rdev;
	if (st->st_size < (quad_t)1 << 32)
		ost->st_size = st->st_size;
	else
		ost->st_size = -2;
	ost->st_atime = st->st_atime;
	ost->st_mtime = st->st_mtime;
	ost->st_ctime = st->st_ctime;
	ost->st_blksize = st->st_blksize;
	ost->st_blocks = st->st_blocks;
	ost->st_flags = st->st_flags;
	ost->st_gen = st->st_gen;
}
#endif /* COMPAT_43 */

/*
 * The stat buffer spare fields are uninitialized
 * so don't include them in the copyout.
 */
#define STATBUFSIZE	\
        (sizeof(struct stat) - sizeof(int32_t) - 2 * sizeof(int64_t))
/*
 * Get file status; this version follows links.
 */
struct stat_args {
	char	*path;
	struct stat *ub;
};
/* ARGSUSED */
int
stat(p, uap, retval)
	struct proc *p;
	register struct stat_args *uap;
	register_t *retval;
{
	struct stat sb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | SHAREDLEAF | AUDITVNPATH1, 
		UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	error = vn_stat(nd.ni_vp, &sb, p);
	vput(nd.ni_vp);
	if (error)
		return (error);
	error = copyout((caddr_t)&sb, (caddr_t)uap->ub, STATBUFSIZE);
	return (error);
}

/*
 * Get file status; this version does not follow links.
 */
struct lstat_args {
	char	*path;
	struct stat *ub;
};
/* ARGSUSED */
int
lstat(p, uap, retval)
	struct proc *p;
	register struct lstat_args *uap;
	register_t *retval;
{
	int error;
	struct vnode *vp;
	struct stat sb;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, NOFOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE, 
		uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	error = vn_stat(vp, &sb, p);
	vput(vp);
	if (error)
		return (error);
	error = copyout((caddr_t)&sb, (caddr_t)uap->ub, STATBUFSIZE);
	return (error);
}

/*
 * Get configurable pathname variables.
 */
struct pathconf_args {
	char	*path;
	int	name;
};
/* ARGSUSED */
int
pathconf(p, uap, retval)
	struct proc *p;
	register struct pathconf_args *uap;
	register_t *retval;
{
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	error = VOP_PATHCONF(nd.ni_vp, uap->name, retval);
	vput(nd.ni_vp);
	return (error);
}

/*
 * Return target name of a symbolic link.
 */
struct readlink_args {
	char	*path;
	char	*buf;
	int	count;
};
/* ARGSUSED */
int
readlink(p, uap, retval)
	struct proc *p;
	register struct readlink_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct iovec aiov;
	struct uio auio;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, NOFOLLOW | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
#ifdef MAC
	error = mac_check_vnode_readlink(p->p_ucred, vp);
	if (error) {
		vput(vp);
		return (error);
	}
#endif
	if (vp->v_type != VLNK)
		error = EINVAL;
	else {
		aiov.iov_base = uap->buf;
		aiov.iov_len = uap->count;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_procp = p;
		auio.uio_resid = uap->count;
		error = VOP_READLINK(vp, &auio, p->p_ucred);
	}
	vput(vp);
	*retval = uap->count - auio.uio_resid;
	return (error);
}

/*
 * Change flags of a file given a path name.
 */
struct chflags_args {
	char	*path;
	int	flags;
};
/* ARGSUSED */
int
chflags(p, uap, retval)
	struct proc *p;
	register struct chflags_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	AUDIT_ARG(fflags, uap->flags);
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	VATTR_NULL(&vattr);
	vattr.va_flags = uap->flags;
#ifdef MAC
	error = mac_check_vnode_setflags(p->p_ucred, vp, vattr.va_flags);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	vput(vp);
	return (error);
}

/*
 * Change flags of a file given a file descriptor.
 */
struct fchflags_args {
	int	fd;
	int	flags;
};
/* ARGSUSED */
int
fchflags(p, uap, retval)
	struct proc *p;
	register struct fchflags_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	AUDIT_ARG(fd, uap->fd);
	AUDIT_ARG(fflags, uap->flags);
	if (error = getvnode(p, uap->fd, &fp))
		return (error);

	vp = (struct vnode *)fp->f_data;

	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	VATTR_NULL(&vattr);
	vattr.va_flags = uap->flags;
#ifdef MAC
	error = mac_check_vnode_setflags(p->p_ucred, vp, vattr.va_flags);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	VOP_UNLOCK(vp, 0, p);
	return (error);
}

/*
 * Change mode of a file given path name.
 */
struct chmod_args {
	char	*path;
	int	mode;
};
/* ARGSUSED */
int
chmod(p, uap, retval)
	struct proc *p;
	register struct chmod_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	AUDIT_ARG(mode, (mode_t)uap->mode);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	VATTR_NULL(&vattr);
	vattr.va_mode = uap->mode & ALLPERMS;
#ifdef MAC
	error = mac_check_vnode_setmode(p->p_ucred, vp, vattr.va_mode);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);

	vput(vp);
	return (error);
}

/*
 * Change mode of a file given a file descriptor.
 */
struct fchmod_args {
	int	fd;
	int	mode;
};
/* ARGSUSED */
int
fchmod(p, uap, retval)
	struct proc *p;
	register struct fchmod_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	AUDIT_ARG(fd, uap->fd);
	AUDIT_ARG(mode, (mode_t)uap->mode);
	if (error = getvnode(p, uap->fd, &fp))
		return (error);

	vp = (struct vnode *)fp->f_data;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	VATTR_NULL(&vattr);
	vattr.va_mode = uap->mode & ALLPERMS;
	AUDIT_ARG(mode, (mode_t)vattr.va_mode);
#ifdef MAC
	error = mac_check_vnode_setmode(p->p_ucred, vp, vattr.va_mode);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);

	VOP_UNLOCK(vp, 0, p);

	return (error);
}

/*
 * Set ownership given a path name.
 */
struct chown_args {
	char	*path;
	int	uid;
	int	gid;
};
/* ARGSUSED */
int
chown(p, uap, retval)
	struct proc *p;
	register struct chown_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	AUDIT_ARG(owner, uap->uid, uap->gid);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;

	/*
	 * XXX A TEMPORARY HACK FOR NOW: Try to track console_user
	 * by looking for chown() calls on /dev/console from a console process.
	 */
	if ((vp) && (vp->v_type == VBLK || vp->v_type == VCHR) && (vp->v_specinfo) &&
		(major(vp->v_specinfo->si_rdev) == CONSMAJOR) &&
		(minor(vp->v_specinfo->si_rdev) == 0)) {
		console_user = uap->uid;
	};
	
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	VATTR_NULL(&vattr);
	vattr.va_uid = uap->uid;
	vattr.va_gid = uap->gid;
#ifdef MAC
	error = mac_check_vnode_setowner(p->p_ucred, vp, vattr.va_uid,
	    vattr.va_gid);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);

	vput(vp);
	return (error);
}

/*
 * Set ownership given a file descriptor.
 */
struct fchown_args {
	int	fd;
	int	uid;
	int	gid;
};
/* ARGSUSED */
int
fchown(p, uap, retval)
	struct proc *p;
	register struct fchown_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	AUDIT_ARG(owner, uap->uid, uap->gid);
	AUDIT_ARG(fd, uap->fd);

	if (error = getvnode(p, uap->fd, &fp))
		return (error);

	vp = (struct vnode *)fp->f_data;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	VATTR_NULL(&vattr);
	vattr.va_uid = uap->uid;
	vattr.va_gid = uap->gid;
#ifdef MAC
	error = mac_check_vnode_setowner(p->p_ucred, vp, vattr.va_uid,
	    vattr.va_gid);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);

	VOP_UNLOCK(vp, 0, p);
	return (error);
}

static int
getutimes(usrtvp, tsp)
	const struct timeval *usrtvp;
	struct timespec *tsp;
{
	struct timeval tv[2];
	int error;

	if (usrtvp == NULL) {
		microtime(&tv[0]);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		tsp[1] = tsp[0];
	} else {
		if ((error = copyin((void *)usrtvp, (void *)tv, sizeof (tv))) != 0)
			return (error);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		TIMEVAL_TO_TIMESPEC(&tv[1], &tsp[1]);
	}
	return 0;
}

static int
setutimes(p, vp, ts, nullflag)
	struct proc *p;
	struct vnode *vp;
	const struct timespec *ts;
	int nullflag;
{
	int error;
	struct vattr vattr;

	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	if (error)
		goto out;

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	VATTR_NULL(&vattr);
	vattr.va_atime = ts[0];
	vattr.va_mtime = ts[1];
	if (nullflag)
		vattr.va_vaflags |= VA_UTIMES_NULL;
#ifdef MAC
	error = mac_check_vnode_setutimes(p->p_ucred, vp, vattr.va_atime,
					  vattr.va_mtime);
	if (error == 0)
#endif
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);

	VOP_UNLOCK(vp, 0, p);
out:
	return error;
}

/*
 * Set the access and modification times of a file.
 */
struct utimes_args {
	char	*path;
	struct	timeval *tptr;
};
/* ARGSUSED */
int
utimes(p, uap, retval)
	struct proc *p;
	register struct utimes_args *uap;
	register_t *retval;
{
	struct timespec ts[2];
	struct timeval *usrtvp;
	int error;
	struct nameidata nd;

	/* AUDIT: Needed to change the order of operations to do the 
	 * name lookup first because auditing wants the path.
	 */
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	usrtvp = uap->tptr;
	if ((error = getutimes(usrtvp, ts)) != 0) {
		vrele(nd.ni_vp);
		return (error);
	}
	error = setutimes(p, nd.ni_vp, ts, usrtvp == NULL);
	vrele(nd.ni_vp);
	return (error);
}

/*
 * Set the access and modification times of a file.
 */
struct futimes_args {
	int	fd;
	struct	timeval *tptr;
};
/* ARGSUSED */
int
futimes(p, uap, retval)
	struct proc *p;
	register struct futimes_args *uap;
	register_t *retval;
{
	struct timespec ts[2];
	struct file *fp;
	struct timeval *usrtvp;
	int error;

	AUDIT_ARG(fd, uap->fd);
	usrtvp = uap->tptr;
	if ((error = getutimes(usrtvp, ts)) != 0)
		return (error);
	if ((error = getvnode(p, uap->fd, &fp)) != 0)
		return (error);

	return setutimes(p, (struct vnode *)fp->f_data, ts, usrtvp == NULL);
}

/*
 * Truncate a file given its path name.
 */
struct truncate_args {
	char	*path;
#ifdef DOUBLE_ALIGN_PARAMS
	int	pad;
#endif
	off_t	length;
};
/* ARGSUSED */
int
truncate(p, uap, retval)
	struct proc *p;
	register struct truncate_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	if (uap->length < 0)
		return(EINVAL);
	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	if (error = namei(&nd))
		return (error);
	vp = nd.ni_vp;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	if (vp->v_type == VDIR)
		error = EISDIR;
#ifdef MAC
	else if ((error = mac_check_vnode_write(p->p_ucred, NOCRED, vp))) {
	}
#endif
	else if ((error = vn_writechk(vp)) == 0 &&
	    (error = VOP_ACCESS(vp, VWRITE, p->p_ucred, p)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = uap->length;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
	vput(vp);
	return (error);
}

/*
 * Truncate a file given a file descriptor.
 */
struct ftruncate_args {
	int	fd;
#ifdef DOUBLE_ALIGN_PARAMS
	int	pad;
#endif
	off_t	length;
};
/* ARGSUSED */
int
ftruncate(p, uap, retval)
	struct proc *p;
	register struct ftruncate_args *uap;
	register_t *retval;
{
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	AUDIT_ARG(fd, uap->fd);
	if (uap->length < 0)
		return(EINVAL);
        
	if (error = fdgetf(p, uap->fd, &fp))
		return (error);

	if (fp->f_type == DTYPE_PSXSHM) {
		return(pshm_truncate(p, fp, uap->fd, uap->length, retval));
	}
	if (fp->f_type != DTYPE_VNODE) 
		return (EINVAL);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	if ((fp->f_flag & FWRITE) == 0)
		return (EINVAL);
	vp = (struct vnode *)fp->f_data;
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	if (vp->v_type == VDIR)
		error = EISDIR;
#ifdef MAC
	else if ((error = mac_check_vnode_write(p->p_ucred, fp->f_cred, vp))) {
	}
#endif
	else if ((error = vn_writechk(vp)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = uap->length;
		error = VOP_SETATTR(vp, &vattr, fp->f_cred, p);
	}
	VOP_UNLOCK(vp, 0, p);
	return (error);
}

#if COMPAT_43
/*
 * Truncate a file given its path name.
 */
struct otruncate_args {
	char	*path;
	long	length;
};
/* ARGSUSED */
int
otruncate(p, uap, retval)
	struct proc *p;
	register struct otruncate_args *uap;
	register_t *retval;
{
	struct truncate_args /* {
		syscallarg(char *) path;
#ifdef DOUBLE_ALIGN_PARAMS
		syscallarg(int) pad;
#endif
		syscallarg(off_t) length;
	} */ nuap;

	nuap.path = uap->path;
	nuap.length = uap->length;
	return (truncate(p, &nuap, retval));
}

/*
 * Truncate a file given a file descriptor.
 */
struct oftruncate_args {
	int	fd;
	long	length;
};
/* ARGSUSED */
int
oftruncate(p, uap, retval)
	struct proc *p;
	register struct oftruncate_args *uap;
	register_t *retval;
{
	struct ftruncate_args /* {
		syscallarg(int) fd;
#ifdef DOUBLE_ALIGN_PARAMS
		syscallarg(int) pad;
#endif
		syscallarg(off_t) length;
	} */ nuap;

	nuap.fd = uap->fd;
	nuap.length = uap->length;
	return (ftruncate(p, &nuap, retval));
}
#endif /* COMPAT_43 */

/*
 * Sync an open file.
 */
struct fsync_args {
	int	fd;
};
/* ARGSUSED */
int
fsync(p, uap, retval)
	struct proc *p;
	struct fsync_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct file *fp;
	int error;

	if (error = getvnode(p, uap->fd, &fp))
		return (error);
	if (fref(fp) == -1)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	error = VOP_FSYNC(vp, fp->f_cred, MNT_WAIT, p);
	VOP_UNLOCK(vp, 0, p);
	frele(fp);
	return (error);
}

/*
 * Duplicate files.  Source must be a file, target must be a file or 
 * must not exist.
 */

struct copyfile_args {
	char	*from;
	char	*to;
        int	mode;
        int 	flags;
};
/* ARGSUSED */
int
copyfile(p, uap, retval)
	struct proc *p;
	register struct copyfile_args *uap;
	register_t *retval;
{
	register struct vnode *tvp, *fvp, *tdvp;
	register struct ucred *cred = p->p_ucred;
	struct nameidata fromnd, tond;
	int error;

	/* Check that the flags are valid. */

	if (uap->flags & ~CPF_MASK) {
		return(EINVAL);
	}

	NDINIT(&fromnd, LOOKUP, SAVESTART | AUDITVNPATH1,
	       UIO_USERSPACE, uap->from, p);
	if (error = namei(&fromnd))
		return (error);
	fvp = fromnd.ni_vp;

	NDINIT(&tond, CREATE, LOCKPARENT | LOCKLEAF | NOCACHE | SAVESTART | AUDITVNPATH2,
	       UIO_USERSPACE, uap->to, p);
	if (error = namei(&tond)) {
		vrele(fvp);
		goto out1;
	}
	tdvp = tond.ni_dvp;
	tvp = tond.ni_vp;
	if (tvp != NULL) {
		if (!(uap->flags & CPF_OVERWRITE)) {
			error = EEXIST;
			goto out;
		}
	}

	if (fvp->v_type == VDIR || (tvp && tvp->v_type == VDIR)) {
		error = EISDIR;
		goto out;
	}

	if (error = VOP_ACCESS(tdvp, VWRITE, cred, p)) 	
		goto out;

	if (fvp == tdvp)
		error = EINVAL;
	/*
	 * If source is the same as the destination (that is the
	 * same inode number) then there is nothing to do.
	 * (fixed to have POSIX semantics - CSM 3/2/98)
	 */
	if (fvp == tvp)
		error = -1;
out:
	if (!error) {
		error = VOP_COPYFILE(fvp,tdvp,tvp,&tond.ni_cnd,uap->mode,uap->flags);
	} else {
		VOP_ABORTOP(tdvp, &tond.ni_cnd);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fvp);
	}
	vrele(tond.ni_startdir);
	FREE_ZONE(tond.ni_cnd.cn_pnbuf, tond.ni_cnd.cn_pnlen, M_NAMEI);
out1:
	if (fromnd.ni_startdir)
		vrele(fromnd.ni_startdir);
	FREE_ZONE(fromnd.ni_cnd.cn_pnbuf, fromnd.ni_cnd.cn_pnlen, M_NAMEI);
	if (error == -1)
		return (0);
	return (error);
}

/*
 * Rename files.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
struct rename_args {
	char	*from;
	char	*to;
};
/* ARGSUSED */
int
rename(p, uap, retval)
	struct proc *p;
	register struct rename_args *uap;
	register_t *retval;
{
	register struct vnode *tvp, *fvp, *tdvp, *fdvp;
	struct nameidata fromnd, tond;
	int error;
	int mntrename;
	int casesense,casepres;
	char *nameptr=NULL, *oname;
	struct vnode *oparent;
	
	mntrename = FALSE;

	bwillwrite();
#ifdef MAC
	NDINIT(&fromnd, DELETE, LOCKPARENT | LOCKLEAF | SAVESTART | AUDITVNPATH1,
		UIO_USERSPACE, uap->from, p);
#else
	NDINIT(&fromnd, DELETE, WANTPARENT | SAVESTART | AUDITVNPATH1, 
		UIO_USERSPACE, uap->from, p);
#endif
	error = namei(&fromnd);
	if (error)
		return (error);
	fvp = fromnd.ni_vp;
	fdvp = fromnd.ni_dvp;
#ifdef MAC
	error = mac_check_vnode_rename_from(p->p_ucred, fdvp, fvp,
		&fromnd.ni_cnd);
	if (error) {
		VOP_ABORTOP(fdvp, &fromnd.ni_cnd);
		vput(fdvp);
		vput(fvp);
		goto out2;
	}
	VOP_UNLOCK(fdvp, 0, p);
	VOP_UNLOCK(fvp, 0, p);
#endif

	NDINIT(&tond, RENAME, 
	    LOCKPARENT | LOCKLEAF | NOCACHE | SAVESTART | AUDITVNPATH2,
	    UIO_USERSPACE, uap->to, p);
	if (fromnd.ni_vp->v_type == VDIR)
		tond.ni_cnd.cn_flags |= WILLBEDIR;
	if (error = namei(&tond)) {
		/* Translate error code for rename("dir1", "dir2/."). */
		if (error == EISDIR && fvp->v_type == VDIR)
			error = EINVAL;
		VOP_ABORTOP(fromnd.ni_dvp, &fromnd.ni_cnd);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
		goto out2;
	}
	tdvp = tond.ni_dvp;
	tvp = tond.ni_vp;

#ifdef MAC
	error = mac_check_vnode_rename_to(p->p_ucred, tdvp, tvp, fdvp == tdvp,
		&tond.ni_cnd);
	if (error) {
		goto out;
	}
#endif
	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
	}
	if (fvp == tdvp)
		error = EINVAL;
	/*
	 * If source is the same as the destination (that is the
	 * same inode number) then there is nothing to do...  EXCEPT if the
	 * underlying file system supports case insensitivity and is case
	 * preserving. Then a special case is made, i.e. foo -> Foo.
	 *
	 * Only file systems that support pathconf selectors _PC_CASE_SENSITIVE
	 * and _PC_CASE_PRESERVING can have this exception, and they need to
	 * handle the special case of getting the same vnode as target and
	 * source.  NOTE: Then the target is unlocked going into VOP_RENAME,
	 * so not to cause locking problems. There is a single reference on tvp.
	 *
	 * NOTE - that fvp == tvp also occurs if they are hard linked - NOTE
	 * that correct behaviour then is just to remove the source (link)
	 */
	if (fvp == tvp && fromnd.ni_dvp == tdvp) {
		if (fromnd.ni_cnd.cn_namelen == tond.ni_cnd.cn_namelen &&
	       	    !bcmp(fromnd.ni_cnd.cn_nameptr, tond.ni_cnd.cn_nameptr,
			  fromnd.ni_cnd.cn_namelen)) {
			error = -1;	/* Default "unix" behavior */
		} else {	/* probe for file system specifics */
			if (VOP_PATHCONF(tdvp, _PC_CASE_SENSITIVE, &casesense))
				casesense = 1;
			if (VOP_PATHCONF(tdvp, _PC_CASE_PRESERVING, &casepres))
				casepres = 1;
			if (!casesense && casepres)
				vput(tvp);	/* Unlock target and drop ref */
		}
	}

	/*
	 * Allow the renaming of mount points.
	 * - target must not exist
	 * - target must reside in the same directory as source
	 * - union mounts cannot be renamed
	 * - "/" cannot be renamed
	 */
	if (!error &&
	    (fvp->v_flag & VROOT)  &&
	    (fvp->v_type == VDIR) &&
	    (tvp == NULL)  &&
	    (fvp->v_mountedhere == NULL)  &&
	    (fromnd.ni_dvp == tond.ni_dvp)  &&
	    ((fvp->v_mount->mnt_flag & (MNT_UNION | MNT_ROOTFS)) == 0)  &&
	    (fvp->v_mount->mnt_vnodecovered != NULLVP)) {
	
		/* switch fvp to the covered vnode */
		fromnd.ni_vp = fvp->v_mount->mnt_vnodecovered;
		vrele(fvp);
		fvp = fromnd.ni_vp;
		VREF(fvp);
		mntrename = TRUE;
	}
out:
	if (!error) {
		VOP_LEASE(tdvp, p, p->p_ucred, LEASE_WRITE);
		if (fromnd.ni_dvp != tdvp)
			VOP_LEASE(fromnd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
		if (tvp)
			VOP_LEASE(tvp, p, p->p_ucred, LEASE_WRITE);

		// XXXdbg - so that the fs won't block when it vrele()'s 
		//          these nodes before returning
		if (fromnd.ni_dvp != tdvp) {
		    vget(tdvp, 0, p);
		}
		
		// save these off so we can later verify that fvp is the same
		oname   = VNAME(fvp);
		oparent = VPARENT(fvp);

		nameptr = add_name(tond.ni_cnd.cn_nameptr,
				   tond.ni_cnd.cn_namelen,
				   tond.ni_cnd.cn_hash, 0);


		error = VOP_RENAME(fromnd.ni_dvp, fvp, &fromnd.ni_cnd,
				   tond.ni_dvp, tvp, &tond.ni_cnd);
		if (error) {
		    remove_name(nameptr);
		    nameptr = NULL;
		    if (fromnd.ni_dvp != tdvp) {
			vrele(tdvp);
		    }

		    goto out1;
		}
		
		/*
		 * update filesystem's mount point data
		 */
		if (mntrename) {
			char *cp, *pathend, *mpname;
			char * tobuf;
			struct mount *mp;
			int maxlen;
			size_t len = 0;

			VREF(fvp);
			vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY, p);
			mp = fvp->v_mountedhere;

			if (vfs_busy(mp, LK_NOWAIT, 0, p)) {
				vput(fvp);
				error = EBUSY;
				goto out1;
			}
			VOP_UNLOCK(fvp, 0, p);
			
			MALLOC_ZONE(tobuf, char *, MAXPATHLEN, M_NAMEI, M_WAITOK);
			error = copyinstr(uap->to, tobuf, MAXPATHLEN, &len);
			if (!error) {
				/* find current mount point prefix */
				pathend = &mp->mnt_stat.f_mntonname[0];
				for (cp = pathend; *cp != '\0'; ++cp) {
					if (*cp == '/')
						pathend = cp + 1;
				}
				/* find last component of target name */
				for (mpname = cp = tobuf; *cp != '\0'; ++cp) {
					if (*cp == '/')
						mpname = cp + 1;
				}
				/* append name to prefix */
				maxlen = MNAMELEN - (pathend - mp->mnt_stat.f_mntonname);
				bzero(pathend, maxlen);
				strncpy(pathend, mpname, maxlen - 1);
			}
			FREE_ZONE(tobuf, MAXPATHLEN, M_NAMEI);

			vrele(fvp);
			vfs_unbusy(mp, p);
		}


		// fix up name & parent pointers.  note that we first
		// check that fvp has the same name/parent pointers it
		// had before the rename call and then we lock fvp so 
		// that it won't go away on us when we hit blocking
		// points like remove_name() or vrele() where fvp could
		// get recycled.
		if (oname == VNAME(fvp) && oparent == VPARENT(fvp) && vget(fvp, LK_EXCLUSIVE | LK_INTERLOCK, p) == 0) {
		    if (VNAME(fvp)) {
			char *tmp = VNAME(fvp);
			VNAME(fvp) = NULL;
			remove_name(tmp);
		    }

		    VNAME(fvp) = nameptr;
		    nameptr = NULL;
		
		    if (fromnd.ni_dvp != tdvp) {
			struct vnode *tmpvp;
			
			tmpvp = VPARENT(fvp);
			VPARENT(fvp) = NULL;
			vrele(tmpvp); 

			VPARENT(fvp) = tdvp;

			// note: we don't vrele() tdvp because we want to keep
			//       the reference until fvp gets recycled
		    }
		    
		    vput(fvp);
		    
		} else {
		    // if fvp isn't kosher anymore and we locked tdvp, 
		    // release tdvp
		    if (fromnd.ni_dvp != tdvp) {
			vrele(tdvp);
		    }
		    remove_name(nameptr);
		    nameptr = NULL;
		}

	} else {
		VOP_ABORTOP(tond.ni_dvp, &tond.ni_cnd);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		VOP_ABORTOP(fromnd.ni_dvp, &fromnd.ni_cnd);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
	}
out1:
	vrele(tond.ni_startdir);
	FREE_ZONE(tond.ni_cnd.cn_pnbuf, tond.ni_cnd.cn_pnlen, M_NAMEI);
out2:
	if (fromnd.ni_startdir)
		vrele(fromnd.ni_startdir);
	FREE_ZONE(fromnd.ni_cnd.cn_pnbuf, fromnd.ni_cnd.cn_pnlen, M_NAMEI);
	if (error == -1)
		return (0);
	return (error);
}

/*
 * Make a directory file.
 */
struct mkdir_args {
	char	*path;
	int	mode;
};
/* ARGSUSED */
int
mkdir(p, uap, retval)
	struct proc *p;
	register struct mkdir_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;
	char *nameptr;

	AUDIT_ARG(mode, (mode_t)uap->mode);
	bwillwrite();
	NDINIT(&nd, CREATE, LOCKPARENT | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	nd.ni_cnd.cn_flags |= WILLBEDIR;
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(vp);
		return (EEXIST);
	}
	VATTR_NULL(&vattr);
	vattr.va_type = VDIR;
	vattr.va_mode = (uap->mode & ACCESSPERMS) &~ p->p_fd->fd_cmask;
#ifdef MAC
	error = mac_check_vnode_create(p->p_ucred, nd.ni_dvp, &nd.ni_cnd,
	    &vattr);
	if (error) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		vput(nd.ni_dvp);
		return (error);
	}
#endif
	VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);

	nameptr = add_name(nd.ni_cnd.cn_nameptr, nd.ni_cnd.cn_namelen, nd.ni_cnd.cn_hash, 0);

	error = VOP_MKDIR(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	if (!error) {
	    VNAME(nd.ni_vp) = nameptr;
	    if (VPARENT(nd.ni_vp) == NULL && vget(nd.ni_dvp, 0, p) == 0) {
		VPARENT(nd.ni_vp) = nd.ni_dvp;
	    }

	    vput(nd.ni_vp);
	}
	return (error);
}

/*
 * Remove a directory file.
 */
struct rmdir_args {
	char	*path;
};
/* ARGSUSED */
int
rmdir(p, uap, retval)
	struct proc *p;
	struct rmdir_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, DELETE, LOCKPARENT | LOCKLEAF | AUDITVNPATH1, UIO_USERSPACE,
	    uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}
	/*
	 * No rmdir "." please.
	 */
	if (nd.ni_dvp == vp) {
		error = EINVAL;
		goto out;
	}
	/*
	 * The root of a mounted filesystem cannot be deleted.
	 */
	if (vp->v_flag & VROOT)
		error = EBUSY;
out:
#ifdef MAC
	if (!error)
		error = mac_check_vnode_delete(p->p_ucred, nd.ni_dvp, vp,
		    &nd.ni_cnd);
#endif
	if (!error) {
		VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
		VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
		error = VOP_RMDIR(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
	} else {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vput(vp);
	}
	return (error);
}

#if COMPAT_43
/*
 * Read a block of directory entries in a file system independent format.
 */
struct ogetdirentries_args {
	int	fd;
	char	*buf;
	u_int	count;
	long	*basep;
};
int
ogetdirentries(p, uap, retval)
	struct proc *p;
	register struct ogetdirentries_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct file *fp;
	struct uio auio, kuio;
	struct iovec aiov, kiov;
	struct dirent *dp, *edp;
	caddr_t dirbuf;
	int error, eofflag, readcnt;
	long loff;

	AUDIT_ARG(fd, uap->fd);
	if (error = getvnode(p, uap->fd, &fp))
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	auio.uio_resid = uap->count;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	loff = auio.uio_offset = fp->f_offset;
#ifdef MAC
	error = mac_check_vnode_readdir(p->p_ucred, vp);
	if (error) {
		VOP_UNLOCK(vp, 0, p);
		return (error);
	}
#endif
#	if (BYTE_ORDER != LITTLE_ENDIAN)
		if (vp->v_mount->mnt_maxsymlinklen <= 0) {
			error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag,
			    (int *)0, (u_long **)0);
			fp->f_offset = auio.uio_offset;
		} else
#	endif
	{
		kuio = auio;
		kuio.uio_iov = &kiov;
		kuio.uio_segflg = UIO_SYSSPACE;
		kiov.iov_len = uap->count;
		MALLOC(dirbuf, caddr_t, uap->count, M_TEMP, M_WAITOK);
		kiov.iov_base = dirbuf;
		error = VOP_READDIR(vp, &kuio, fp->f_cred, &eofflag,
			    (int *)0, (u_long **)0);
		fp->f_offset = kuio.uio_offset;
		if (error == 0) {
			readcnt = uap->count - kuio.uio_resid;
			edp = (struct dirent *)&dirbuf[readcnt];
			for (dp = (struct dirent *)dirbuf; dp < edp; ) {
#				if (BYTE_ORDER == LITTLE_ENDIAN)
					/*
					 * The expected low byte of
					 * dp->d_namlen is our dp->d_type.
					 * The high MBZ byte of dp->d_namlen
					 * is our dp->d_namlen.
					 */
					dp->d_type = dp->d_namlen;
					dp->d_namlen = 0;
#				else
					/*
					 * The dp->d_type is the high byte
					 * of the expected dp->d_namlen,
					 * so must be zero'ed.
					 */
					dp->d_type = 0;
#				endif
				if (dp->d_reclen > 0) {
					dp = (struct dirent *)
					    ((char *)dp + dp->d_reclen);
				} else {
					error = EIO;
					break;
				}
			}
			if (dp >= edp)
				error = uiomove(dirbuf, readcnt, &auio);
		}
		FREE(dirbuf, M_TEMP);
	}
	VOP_UNLOCK(vp, 0, p);
	if (error)
		return (error);

#if UNION
{
	extern int (**union_vnodeop_p)(void *);
	extern struct vnode *union_dircache __P((struct vnode*, struct proc*));

	if ((uap->count == auio.uio_resid) &&
	    (vp->v_op == union_vnodeop_p)) {
		struct vnode *lvp;

		lvp = union_dircache(vp, p);
		if (lvp != NULLVP) {
			struct vattr va;

			/*
			 * If the directory is opaque,
			 * then don't show lower entries
			 */
			error = VOP_GETATTR(vp, &va, fp->f_cred, p);
			if (va.va_flags & OPAQUE) {
				vput(lvp);
				lvp = NULL;
			}
		}
		
		if (lvp != NULLVP) {
#ifdef MAC
			error = mac_check_vnode_readdir(p->p_ucred, lvp);
			if (!error)
#endif
			error = VOP_OPEN(lvp, FREAD, fp->f_cred, p);
			if (error) {
				vput(lvp);
				return (error);
			}
			VOP_UNLOCK(lvp, 0, p);
			fp->f_data = (caddr_t) lvp;
			fp->f_offset = 0;
			error = VOP_CLOSE(vp, FREAD, fp->f_cred, p);
			vrele(vp);
			if (error)
				return (error);
			vp = lvp;
			goto unionread;
		}
	}
}
#endif /* UNION */

	if ((uap->count == auio.uio_resid) &&
	    (vp->v_flag & VROOT) &&
	    (vp->v_mount->mnt_flag & MNT_UNION)) {
		struct vnode *tvp = vp;
		vp = vp->v_mount->mnt_vnodecovered;
		VREF(vp);
		fp->f_data = (caddr_t) vp;
		fp->f_offset = 0;
		vrele(tvp);
		goto unionread;
	}
	error = copyout((caddr_t)&loff, (caddr_t)uap->basep,
	    sizeof(long));
	*retval = uap->count - auio.uio_resid;
	return (error);
}
#endif /* COMPAT_43 */

/*
 * Read a block of directory entries in a file system independent format.
 */
struct getdirentries_args {
	int	fd;
	char	*buf;
	u_int	count;
	long	*basep;
};
int
getdirentries(p, uap, retval)
	struct proc *p;
	register struct getdirentries_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	long loff;
	int error, eofflag;

	AUDIT_ARG(fd, uap->fd);
	error = getvnode(p, uap->fd, &fp);
	if (error)
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	auio.uio_resid = uap->count;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	loff = auio.uio_offset = fp->f_offset;
#ifdef MAC
	error = mac_check_vnode_readdir(p->p_ucred, vp);
	if (error == 0)
#endif
	error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag,
			    (int *)0, (u_long **)0);
	fp->f_offset = auio.uio_offset;
	VOP_UNLOCK(vp, 0, p);
	if (error)
		return (error);

#if UNION
{
	extern int (**union_vnodeop_p)(void *);
	extern struct vnode *union_dircache __P((struct vnode*, struct proc*));

	if ((uap->count == auio.uio_resid) &&
	    (vp->v_op == union_vnodeop_p)) {
		struct vnode *lvp;

		lvp = union_dircache(vp, p);
		if (lvp != NULLVP) {
			struct vattr va;

			/*
			 * If the directory is opaque,
			 * then don't show lower entries
			 */
			error = VOP_GETATTR(vp, &va, fp->f_cred, p);
			if (va.va_flags & OPAQUE) {
				vput(lvp);
				lvp = NULL;
			}
		}

		if (lvp != NULLVP) {
			error = VOP_OPEN(lvp, FREAD, fp->f_cred, p);
			if (error) {
				vput(lvp);
				return (error);
			}
			VOP_UNLOCK(lvp, 0, p);
			fp->f_data = (caddr_t) lvp;
			fp->f_offset = 0;
			error = VOP_CLOSE(vp, FREAD, fp->f_cred, p);
			vrele(vp);
			if (error)
				return (error);
			vp = lvp;
			goto unionread;
		}
	}
}
#endif /* UNION */

	if ((uap->count == auio.uio_resid) &&
	    (vp->v_flag & VROOT) &&
	    (vp->v_mount->mnt_flag & MNT_UNION)) {
		struct vnode *tvp = vp;
		vp = vp->v_mount->mnt_vnodecovered;
		VREF(vp);
		fp->f_data = (caddr_t) vp;
		fp->f_offset = 0;
		vrele(tvp);
		goto unionread;
	}
	error = copyout((caddr_t)&loff, (caddr_t)uap->basep,
	    sizeof(long));
	*retval = uap->count - auio.uio_resid;
	return (error);
}

/*
 * Set the mode mask for creation of filesystem nodes.
 */
struct umask_args {
	int	newmask;
};
int
umask(p, uap, retval)
	struct proc *p;
	struct umask_args *uap;
	register_t *retval;
{
	register struct filedesc *fdp;

	AUDIT_ARG(mask, uap->newmask);
	fdp = p->p_fd;
	*retval = fdp->fd_cmask;
	fdp->fd_cmask = uap->newmask & ALLPERMS;
	return (0);
}

/*
 * Void all references to file by ripping underlying filesystem
 * away from vnode.
 */
struct revoke_args {
	char	*path;
};
/* ARGSUSED */
int
revoke(p, uap, retval)
	struct proc *p;
	register struct revoke_args *uap;
	register_t *retval;
{
	register struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
#ifdef MAC
	error = mac_check_vnode_revoke(p->p_ucred, vp);
	if (error)
		goto out;
#endif
	if (error = VOP_GETATTR(vp, &vattr, p->p_ucred, p))
		goto out;
	if (p->p_ucred->cr_uid != vattr.va_uid &&
	    (error = suser(p->p_ucred, &p->p_acflag)))
		goto out;
	if (vp->v_usecount > 1 || (vp->v_flag & VALIASED))
		VOP_REVOKE(vp, REVOKEALL);
out:
	vrele(vp);
	return (error);
}

/*
 * Convert a user file descriptor to a kernel file entry.
 */
int
getvnode(p, fd, fpp)
	struct proc *p;
	int fd;
	struct file **fpp;
{
	struct file *fp;
	int error;

	if (error = fdgetf(p, fd, &fp))
		return (error);
	if (fp->f_type != DTYPE_VNODE)
		return (EINVAL);
	*fpp = fp;
	return (0);
}

/*
 *  HFS/HFS PlUS SPECIFIC SYSTEM CALLS
 *  The following system calls are designed to support features
 *  which are specific to the HFS & HFS Plus volume formats
 */

#ifdef __APPLE_API_OBSOLETE

/************************************************/
/* *** Following calls will be deleted soon *** */
/************************************************/

/*
 * Make a complex file.  A complex file is one with multiple forks (data streams)
 */
struct mkcomplex_args {
        const char *path;	/* pathname of the file to be created */
	mode_t mode;		/* access mode for the newly created file */
        u_long type;		/* format of the complex file */
};
/* ARGSUSED */
int
mkcomplex(p,uap,retval)
	struct proc *p;
        register struct mkcomplex_args *uap;
        register_t *retval;
{
	struct vnode *vp;
        struct vattr vattr;
        int error;
        struct nameidata nd;

	/* mkcomplex wants the directory vnode locked so do that here */

        NDINIT(&nd, CREATE, FOLLOW | LOCKPARENT, UIO_USERSPACE, (char *)uap->path, p);
        if (error = namei(&nd))
                return (error);

	/*  Set the attributes as specified by the user */

        VATTR_NULL(&vattr);
        vattr.va_mode = (uap->mode & ACCESSPERMS);
        error = VOP_MKCOMPLEX(nd.ni_dvp, &vp, &nd.ni_cnd, &vattr, uap->type);

	/*  The mkcomplex call promises to release the parent vnode pointer
	 *  even an an error case so don't do it here unless the operation
	 *  is not supported.  In that case, there isn't anyone to unlock the parent
	 *  The vnode pointer to the file will also be released.
	 */

        if (error)
		{
       		if (error == EOPNOTSUPP)
		   	vput(nd.ni_dvp);
                return (error);
	        }

        return (0);

} /* end of mkcomplex system call */

/*
 * Extended stat call which returns volumeid and vnodeid as well as other info
 */
struct statv_args {
        const char *path;	/* pathname of the target file       */
        struct vstat *vsb;	/* vstat structure for returned info */
};
/* ARGSUSED */
int
statv(p,uap,retval)
        struct proc *p;
        register struct statv_args *uap;
        register_t *retval;

{
	return (EOPNOTSUPP);	/*  We'll just return an error for now */

} /* end of statv system call */

/*
* Extended lstat call which returns volumeid and vnodeid as well as other info
*/
struct lstatv_args {
       const char *path;	/* pathname of the target file       */
       struct vstat *vsb;	/* vstat structure for returned info */
};
/* ARGSUSED */
int
lstatv(p,uap,retval)
       struct proc *p;
       register struct lstatv_args *uap;
       register_t *retval;

{
       return (EOPNOTSUPP);	/*  We'll just return an error for now */
} /* end of lstatv system call */

/*
* Extended fstat call which returns volumeid and vnodeid as well as other info
*/
struct fstatv_args {
       int fd;			/* file descriptor of the target file */
       struct vstat *vsb;	/* vstat structure for returned info  */
};
/* ARGSUSED */
int
fstatv(p,uap,retval)
       struct proc *p;
       register struct fstatv_args *uap;
       register_t *retval;

{
       return (EOPNOTSUPP);	/*  We'll just return an error for now */
} /* end of fstatv system call */


/************************************************/
/* *** Preceding calls will be deleted soon *** */
/************************************************/

#endif /* __APPLE_API_OBSOLETE */


/*
* Obtain attribute information about a file system object
*/

struct getattrlist_args {
       const char *path;	/* pathname of the target object */
       struct attrlist * alist; /* Attributes desired by the user */
       void * attributeBuffer; 	/* buffer to hold returned attributes */
       size_t bufferSize;	/* size of the return buffer */
       unsigned long options;   /* options (follow/don't follow) */
};
/* ARGSUSED */
int
getattrlist (p,uap,retval)
       struct proc *p;
       register struct getattrlist_args *uap;
       register_t *retval;

{
        int error;
        struct nameidata nd;	
	struct iovec aiov;
	struct uio auio;
        struct attrlist attributelist;
	u_long nameiflags;

	/* Get the attributes desire and do our parameter checking */

	if (error = copyin((caddr_t)uap->alist, (caddr_t) &attributelist,
                 sizeof (attributelist)))
		{
		return(error);
		}

	if (attributelist.bitmapcount != ATTR_BIT_MAP_COUNT
#if 0
	    ||	attributelist.commonattr & ~ATTR_CMN_VALIDMASK ||
		attributelist.volattr & ~ATTR_VOL_VALIDMASK ||
		attributelist.dirattr & ~ATTR_DIR_VALIDMASK ||
		attributelist.fileattr & ~ATTR_FILE_VALIDMASK ||
		attributelist.forkattr & ~ATTR_FORK_VALIDMASK
#endif
	)
		{
		return (EINVAL);
		}

	/* Get the vnode for the file we are getting info on.  */
	nameiflags = LOCKLEAF | SHAREDLEAF;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;
        NDINIT(&nd, LOOKUP, nameiflags | AUDITVNPATH1, UIO_USERSPACE, 
		(char *)uap->path, p);

        error = namei(&nd);
        if (error)
                return (error);

	/* Set up the UIO structure for use by the vfs routine */
	
	aiov.iov_base = uap->attributeBuffer;
        aiov.iov_len = uap->bufferSize;  
  	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
        auio.uio_offset = 0;
        auio.uio_rw = UIO_READ;
        auio.uio_segflg = UIO_USERSPACE;
        auio.uio_procp = p;
        auio.uio_resid = uap->bufferSize;

#ifdef MAC
	error = mac_check_vnode_getattrlist(p->p_ucred, nd.ni_vp,
	    &attributelist, &auio);
	if (error)
	{
		vput (nd.ni_vp);
		return error;
	}
#endif

        error = VOP_GETATTRLIST(nd.ni_vp, &attributelist, &auio, p->p_ucred, p);

	/* Unlock and release the vnode which will have been locked by namei */

        vput(nd.ni_vp);

        /* return the effort if we got one, otherwise return success */

        if (error)
            {
            return (error);
            }

        return(0);

} /* end of getattrlist system call */



/*
 * Set attribute information about a file system object
 */

struct setattrlist_args {
      const char *path;		/* pathname of the target object 	  */
      struct attrlist * alist;  /* Attributes being set  by the user 	  */
      void * attributeBuffer; 	/* buffer with attribute values to be set */
      size_t bufferSize;	/* size of the return buffer 		  */
      unsigned long options;    /* options (follow/don't follow) */
};
/* ARGSUSED */
int
setattrlist (p,uap,retval)
      struct proc *p;
      register struct setattrlist_args *uap;
      register_t *retval;

{
	int error;
	struct nameidata nd;	
	struct iovec aiov;
	struct uio auio;
	struct attrlist alist;
	u_long nameiflags;

       /* Get the attributes desired and do our parameter checking */

	if ((error = copyin((caddr_t)uap->alist, (caddr_t) &alist,
	    sizeof (alist)))) {
		return (error);
	}

	if (alist.bitmapcount != ATTR_BIT_MAP_COUNT)
		return (EINVAL);

	/* Get the vnode for the file whose attributes are being set. */
	nameiflags = LOCKLEAF;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;
	NDINIT(&nd, LOOKUP, nameiflags | AUDITVNPATH1, UIO_USERSPACE, 
		(char *)uap->path, p);
        error = namei(&nd);
        if (error)
                return (error);

#ifdef MAC
	error = mac_check_vnode_setattrlist(p->p_ucred, nd.ni_vp, &alist, &auio);
	if (error)
	{
		vput (nd.ni_vp);
		return error;
	}
#endif

	/* Set up the UIO structure for use by the vfs routine */
	aiov.iov_base = uap->attributeBuffer;
	aiov.iov_len = uap->bufferSize;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	auio.uio_resid = uap->bufferSize;

	error = VOP_SETATTRLIST(nd.ni_vp, &alist, &auio, p->p_ucred, p);

	vput(nd.ni_vp);

	return (error);

} /* end of setattrlist system call */


/*
* Obtain attribute information on objects in a directory while enumerating
* the directory.  This call does not yet support union mounted directories.
* TO DO
*  1.union mounted directories.
*/

struct getdirentriesattr_args {
      int fd;			/* file descriptor */
      struct attrlist *alist;   /* bit map of requested attributes */
      void *buffer;		/* buffer to hold returned attribute info */
      size_t buffersize; 	/* size of the return buffer */
      u_long *count;		/* the count of entries requested/returned */
      u_long *basep;		/* the offset of where we are leaving off in buffer */
      u_long *newstate;		/* a flag to inform of changes in directory */
      u_long options;		/* maybe unused for now */
};
/* ARGSUSED */
int
getdirentriesattr (p,uap,retval)
      struct proc *p;
      register struct getdirentriesattr_args *uap;
      register_t *retval;

{
        register struct vnode *vp;
        struct file *fp;
        struct uio auio;
        struct iovec aiov;
        u_long actualcount;
        u_long newstate;
        int error, eofflag;
        long loff;
        struct attrlist attributelist; 

	AUDIT_ARG(fd, uap->fd);

        /* Get the attributes into kernel space */
        if (error = copyin((caddr_t)uap->alist, (caddr_t) &attributelist, sizeof (attributelist)))
           return(error);
        if (error = copyin((caddr_t)uap->count, (caddr_t) &actualcount, sizeof (u_long)))
           return(error);

        if (error = getvnode(p, uap->fd, &fp))
                return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

        if ((fp->f_flag & FREAD) == 0)
                return(EBADF);
        vp = (struct vnode *)fp->f_data;

        if (vp->v_type != VDIR)
            return(EINVAL);

	/* set up the uio structure which will contain the users return buffer */
        aiov.iov_base = uap->buffer;
        aiov.iov_len = uap->buffersize;
        auio.uio_iov = &aiov;
        auio.uio_iovcnt = 1;
        auio.uio_rw = UIO_READ;
        auio.uio_segflg = UIO_USERSPACE;
        auio.uio_procp = p;
        auio.uio_resid = uap->buffersize;
        
        loff = auio.uio_offset = fp->f_offset;
        vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
#ifdef MAC
	error = mac_check_vnode_readdir(p->p_ucred, vp);
	if (error) {
		VOP_UNLOCK(vp, 0, p);
		return (error);
	}
#endif
        error = VOP_READDIRATTR (vp, &attributelist, &auio,
                   actualcount, uap->options, &newstate, &eofflag,
                   &actualcount, ((u_long **)0), p->p_ucred);

        VOP_UNLOCK(vp, 0, p);
        if (error) return (error);
        fp->f_offset = auio.uio_offset; /* should be multiple of dirent, not variable */

        if (error = copyout((caddr_t) &actualcount, (caddr_t) uap->count, sizeof(u_long)))
            return (error);
        if (error = copyout((caddr_t) &newstate, (caddr_t) uap->newstate, sizeof(u_long)))
            return (error);
        if (error = copyout((caddr_t)&loff, (caddr_t)uap->basep, sizeof(long)))
            return (error);

	*retval = eofflag;  /* similar to getdirentries */
        return (0); /* return error earlier, an retval of 0 or 1 now */

} /* end of getdirentryattr system call */

/*
* Exchange data between two files
*/

struct exchangedata_args {
      const char *path1;	/* pathname of the first swapee  */
      const char *path2;	/* pathname of the second swapee */
      unsigned long options;    /* options */
};
/* ARGSUSED */
int
exchangedata (p,uap,retval)
      struct proc *p;
      register struct exchangedata_args *uap;
      register_t *retval;

{

	struct nameidata fnd, snd;
	struct vnode *fvp, *svp;
        int error;
	u_long nameiflags;

	nameiflags = 0;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;

		/* Global lock, to prevent race condition, only one exchange at a time */
        lockmgr(&exchangelock, LK_EXCLUSIVE , (struct slock *)0, p);

        NDINIT(&fnd, LOOKUP, nameiflags | AUDITVNPATH1, UIO_USERSPACE, 
		(char *) uap->path1, p);

        error = namei(&fnd);
        if (error)
                goto out2;

        fvp = fnd.ni_vp;

        NDINIT(&snd, LOOKUP, nameiflags | AUDITVNPATH2, UIO_USERSPACE, 
		(char *)uap->path2, p);

        error = namei(&snd);
        if (error) {
		vrele(fvp);
		goto out2;
        }

	svp = snd.ni_vp;

	/* if the files are the same, return an inval error */
	if (svp == fvp) {
		vrele(fvp);
		vrele(svp);
        error = EINVAL;
		goto out2;
	 } 

    vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY, p);
    vn_lock(svp, LK_EXCLUSIVE | LK_RETRY, p);

#ifdef MAC
	error = mac_check_vnode_exchangedata(p->p_ucred, fvp, svp);
	if (error) goto out;
#endif

	error = VOP_ACCESS(fvp, VWRITE, p->p_ucred, p);
	if (error) goto out;

	error = VOP_ACCESS(svp, VWRITE, p->p_ucred, p);
	if (error) goto out;

	/* Ok, make the call */
	error = VOP_EXCHANGE (fvp, svp, p->p_ucred, p);

	if (error == 0 && VPARENT(fvp) != VPARENT(svp)) {
	    struct vnode *tmp;

	    tmp = VPARENT(fvp);
	    VPARENT(fvp) = VPARENT(svp);
	    VPARENT(svp) = tmp;
	}

out:
    vput (svp);
	vput (fvp);

out2:
    lockmgr(&exchangelock, LK_RELEASE, (struct slock *)0, p);

	if (error) {
        return (error);
		}
	
	return (0);

} /* end of exchangedata system call */

#ifdef __APPLE_API_OBSOLETE

/************************************************/
/* *** Following calls will be deleted soon *** */
/************************************************/

/*
* Check users access to a file 
*/

struct checkuseraccess_args {
     const char *path;		/* pathname of the target file   */
     uid_t userid;		/* user for whom we are checking access */
     gid_t *groups;		/* Group that we are checking for */
     int ngroups;	        /* Number of groups being checked */
     int accessrequired; 	/* needed access to the file */
     unsigned long options;     /* options */
};

/* ARGSUSED */
int
checkuseraccess (p,uap,retval)
     struct proc *p;
     register struct checkuseraccess_args *uap;
     register_t *retval;

{
	register struct vnode *vp;
	int error;
	struct nameidata nd;
	struct ucred cred;
	int flags;		/*what will actually get passed to access*/
	u_long nameiflags;

	/* Make sure that the number of groups is correct before we do anything */

	if ((uap->ngroups <=  0) || (uap->ngroups > NGROUPS))
		return (EINVAL);

	/* Verify that the caller is root */
	
	if (error = suser(p->p_ucred, &p->p_acflag))
		return(error);

	/* Fill in the credential structure */

	cred.cr_ref = 0;
	cred.cr_uid = uap->userid;
	cred.cr_ngroups = uap->ngroups;
	if (error = copyin((caddr_t) uap->groups, (caddr_t) &(cred.cr_groups), (sizeof(gid_t))*uap->ngroups))
		return (error);

	/* Get our hands on the file */

	nameiflags = LOCKLEAF;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;
	NDINIT(&nd, LOOKUP, nameiflags | AUDITVNPATH1, UIO_USERSPACE, (char *)uap->path, p);

	if (error = namei(&nd))
       		 return (error);
	vp = nd.ni_vp;
	
	/* Flags == 0 means only check for existence. */

	flags = 0;

	if (uap->accessrequired) {
		if (uap->accessrequired & R_OK)
			flags |= VREAD;
		if (uap->accessrequired & W_OK)
			flags |= VWRITE;
		if (uap->accessrequired & X_OK)
			flags |= VEXEC;
		}
	error = VOP_ACCESS(vp, flags, &cred, p);
		    	
	vput(vp);

	if (error) 
 		return (error);
	
	return (0); 

} /* end of checkuseraccess system call */

/************************************************/
/* *** Preceding calls will be deleted soon *** */
/************************************************/

#endif /* __APPLE_API_OBSOLETE */



struct searchfs_args {
	const char *path;
	struct fssearchblock *searchblock; 
	u_long *nummatches;
	u_long scriptcode;
	u_long options;
 	struct searchstate *state; 
	};
/* ARGSUSED */

int
searchfs (p,uap,retval)
  	struct proc *p;
   	register struct searchfs_args *uap;
   	register_t *retval;

{
	register struct vnode *vp;
	int error=0;
	int fserror = 0;
	struct nameidata nd;
	struct fssearchblock searchblock;
	struct searchstate *state;
	struct attrlist *returnattrs;
	void *searchparams1,*searchparams2;
	struct iovec aiov;
	struct uio auio;
	u_long nummatches;
	int mallocsize;
	u_long nameiflags;
	

	/* Start by copying in fsearchblock paramater list */

	if (error = copyin((caddr_t) uap->searchblock, (caddr_t) &searchblock,sizeof(struct fssearchblock)))
		return(error);

	/* Now malloc a big bunch of space to hold the search parameters, the attrlists and the search state. */
	/* It all has to do into local memory and it's not that big so we might as well  put it all together. */
	/* Searchparams1 shall be first so we might as well use that to hold the base address of the allocated*/
	/* block.  											      */
	
	mallocsize = searchblock.sizeofsearchparams1+searchblock.sizeofsearchparams2 +
		      sizeof(struct attrlist) + sizeof(struct searchstate);

	MALLOC(searchparams1, void *, mallocsize, M_TEMP, M_WAITOK);

	/* Now set up the various pointers to the correct place in our newly allocated memory */

	searchparams2 = (void *) (((caddr_t) searchparams1) + searchblock.sizeofsearchparams1);
	returnattrs = (struct attrlist *) (((caddr_t) searchparams2) + searchblock.sizeofsearchparams2);
	state = (struct searchstate *) (((caddr_t) returnattrs) + sizeof (struct attrlist));

	/* Now copy in the stuff given our local variables. */

	if (error = copyin((caddr_t) searchblock.searchparams1, searchparams1,searchblock.sizeofsearchparams1))
		goto freeandexit;

	if (error = copyin((caddr_t) searchblock.searchparams2, searchparams2,searchblock.sizeofsearchparams2))
		goto freeandexit;

	if (error = copyin((caddr_t) searchblock.returnattrs, (caddr_t) returnattrs, sizeof(struct attrlist)))
		goto freeandexit;
		
	if (error = copyin((caddr_t) uap->state, (caddr_t) state, sizeof(struct searchstate)))
		goto freeandexit;
	
	/* set up the uio structure which will contain the users return buffer */

	aiov.iov_base = searchblock.returnbuffer;
	aiov.iov_len = searchblock.returnbuffersize;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	auio.uio_resid = searchblock.returnbuffersize;

	nameiflags = LOCKLEAF;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;
	NDINIT(&nd, LOOKUP, nameiflags | AUDITVNPATH1, UIO_USERSPACE, 
		(char *)uap->path, p);

	error = namei(&nd);
	if (error)
		goto freeandexit;

	vp = nd.ni_vp; 

	 
	/*
	 * If searchblock.maxmatches == 0, then skip the search. This has happened 
	 * before and sometimes the underlyning code doesnt deal with it well.
	 */
	 if (searchblock.maxmatches == 0) {
		nummatches = 0;
		goto saveandexit;
	 }

	/*
	   Allright, we have everything we need, so lets make that call.
	   
	   We keep special track of the return value from the file system:
	   EAGAIN is an acceptable error condition that shouldn't keep us
	   from copying out any results...
	 */

	fserror = VOP_SEARCHFS(vp,
							searchparams1,
							searchparams2,
							&searchblock.searchattrs,
							searchblock.maxmatches,
							&searchblock.timelimit,
							returnattrs,
							&nummatches,
							uap->scriptcode,
							uap->options,
							&auio,
							state);
		
saveandexit:

	vput(vp);

	/* Now copy out the stuff that needs copying out. That means the number of matches, the
	   search state.  Everything was already put into he return buffer by the vop call. */

	if (error = copyout((caddr_t) state, (caddr_t) uap->state, sizeof(struct searchstate)))
		goto freeandexit;

	if (error = copyout((caddr_t) &nummatches, (caddr_t) uap->nummatches, sizeof(u_long)))
		goto freeandexit;
	
	error = fserror;

freeandexit:

	FREE(searchparams1,M_TEMP);

	return(error);


} /* end of searchfs system call */


/*
 * Make a filesystem-specific control call:
 */
struct fsctl_args {
       const char *path;	/* pathname of the target object */
       u_long cmd;   		/* cmd (also encodes size/direction of arguments a la ioctl) */
       caddr_t data; 		/* pointer to argument buffer */
       u_long options;		/* options for fsctl processing */
};
/* ARGSUSED */
int
fsctl (p,uap,retval)
	struct proc *p;
	struct fsctl_args *uap;
	register_t *retval;

{
	int error;
	struct nameidata nd;	
	u_long nameiflags;
	u_long cmd = uap->cmd;
	register u_int size;
#define STK_PARAMS 128
	char stkbuf[STK_PARAMS];
	caddr_t data, memp;

	size = IOCPARM_LEN(cmd);
	if (size > IOCPARM_MAX) return (EINVAL);

	memp = NULL;
	if (size > sizeof (stkbuf)) {
		if ((memp = (caddr_t)kalloc(size)) == 0) return ENOMEM;
		data = memp;
	} else {
		data = stkbuf;
	};
	
	if (cmd & IOC_IN) {
		if (size) {
			error = copyin(uap->data, data, (u_int)size);
			if (error) goto FSCtl_Exit;
		} else {
			*(caddr_t *)data = uap->data;
		};
	} else if ((cmd & IOC_OUT) && size) {
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		bzero(data, size);
	} else if (cmd & IOC_VOID)
		*(caddr_t *)data = uap->data;

	/* Get the vnode for the file we are getting info on:  */
	nameiflags = LOCKLEAF;
	if ((uap->options & FSOPT_NOFOLLOW) == 0) nameiflags |= FOLLOW;
	NDINIT(&nd, LOOKUP, nameiflags, UIO_USERSPACE, (char *)uap->path, p);
	if (error = namei(&nd)) goto FSCtl_Exit;
	
	/* Invoke the filesystem-specific code */
	error = VOP_IOCTL(nd.ni_vp, IOCBASECMD(cmd), data, uap->options, p->p_ucred, p);
	
	vput(nd.ni_vp);
	
	/*
	 * Copy any data to user, size was
	 * already set and checked above.
	 */
	if (error == 0 && (cmd & IOC_OUT) && size) error = copyout(data, uap->data, (u_int)size);
	
FSCtl_Exit:
	if (memp) kfree(memp, size);
	
	return error;
}
/* end of fsctl system call */

/*
 * An in-kernel sync for power management to call.
 */
__private_extern__ int
sync_internal(void)
{
	boolean_t   funnel_state;
	int error;

	struct sync_args data;

	int retval[2];

	funnel_state = thread_funnel_set(kernel_flock, TRUE);

	error = sync(current_proc(), &data, &retval);

	thread_funnel_set(kernel_flock, funnel_state);

	return (error);
} /* end of sync_internal call */

// XXXdbg fmod watching calls
#define NUM_CHANGE_NODES 256
static int                    changed_init=0;
static volatile int           fmod_watch_enabled = 0;
static pid_t                  fmod_watch_owner;
static simple_lock_data_t     changed_nodes_lock;    // guard access
static volatile struct vnode *changed_nodes[NUM_CHANGE_NODES];
static volatile pid_t         changed_nodes_pid[NUM_CHANGE_NODES];
static volatile int           changed_rd_index=0, changed_wr_index=0;
static volatile int           notifier_sleeping=0;


void
notify_filemod_watchers(struct vnode *vp, struct proc *p)
{
    int ret;
    
    // only want notification on regular files.
    if (fmod_watch_enabled == 0 || (vp->v_type != VREG && vp->v_type != VDIR)) {
	return;
    }

    // grab a reference so it doesn't go away
    if (vget(vp, 0, p) != 0) {
	return;
    }

  retry:
    simple_lock(&changed_nodes_lock);

    // If the table is full, block until it clears up
    if (((changed_wr_index+1) % NUM_CHANGE_NODES) == changed_rd_index) {
	simple_unlock(&changed_nodes_lock);

	notifier_sleeping++;
	// wait up to 10 seconds for the queue to drain
	ret = tsleep((caddr_t)&changed_wr_index, PINOD, "changed_nodes_full", 10*hz);
	if (ret != 0 || fmod_watch_enabled == 0) {
	    notifier_sleeping--;
	    printf("notify_filemod: err %d from tsleep/enabled %d.  bailing out (vp 0x%x).\n",
		   ret, fmod_watch_enabled, vp);
	    vrele(vp);
	    return;
	}

	notifier_sleeping--;
	goto retry;
    }

    // insert our new guy
    if (changed_nodes[changed_wr_index] != NULL) {
	panic("notify_fmod_watchers: index %d is 0x%x, not null!\n",
	      changed_wr_index, changed_nodes[changed_wr_index]);
    }
    changed_nodes[changed_wr_index] = vp;
    changed_nodes_pid[changed_wr_index] = current_proc()->p_pid;
    changed_wr_index = (changed_wr_index + 1) % NUM_CHANGE_NODES;

    simple_unlock(&changed_nodes_lock);

    wakeup((caddr_t)&changed_rd_index);
}


struct fmod_watch_args {
    int  *new_fd;
    char *pathbuf;
    int   len;
    pid_t pid;
};

int
fmod_watch(struct proc *p, struct fmod_watch_args *uap, register_t *retval)
{
    int fd, didhold = 0;
    struct filedesc *fdp;
    struct file *fp;
    struct vnode *vp;
    int flags;
    int type, indx, error, need_wakeup=0;
    struct flock lf;
    struct nameidata nd;
    extern struct fileops vnops;
    pid_t pid;

    if (fmod_watch_enabled == 0) {
	*retval = -1;
	return EINVAL;
    }

    p = current_proc();

    if (changed_init == 0) {
	changed_init = 1;
	simple_lock_init(&changed_nodes_lock);
    }

    if (changed_rd_index == changed_wr_index) {
	// there's nothing to do, go to sleep
	error = tsleep((caddr_t)&changed_rd_index, PUSER|PCATCH, "changed_nodes_empty", 0);
	if (error != 0) {
	    // XXXdbg - what if after we unblock the changed_nodes
	    //          table is full?  We should wakeup() the writer.
	    *retval = -1;
	    return error;
	}
    }

    simple_lock(&changed_nodes_lock);

    vp = (struct vnode *)changed_nodes[changed_rd_index];
    pid = changed_nodes_pid[changed_rd_index];
    
    changed_nodes[changed_rd_index] = NULL;
    changed_rd_index = (changed_rd_index + 1) % NUM_CHANGE_NODES;

    if (vp == NULL) {
	printf("watch_file_changes: Someone put a null vnode in my table! (%d %d)\n",
	       changed_rd_index, changed_wr_index);
	error = EINVAL;
	goto err0;
    }

    simple_unlock(&changed_nodes_lock);
    
    // if the writers are blocked, wake them up as we just freed up
    // some space for them.
    if (notifier_sleeping > 0) {
	wakeup((caddr_t)&changed_wr_index);
    }

    if (vp->v_type != VREG && vp->v_type != VDIR) {
	error = EBADF;
	goto err1;
    }

    if ((error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p)) != 0) {
	printf("fmod_watch: vn_lock returned %d\n", error);
	goto err1;
    }

    // first copy out the name
    if (uap->pathbuf) {
	char *buff;
	int len=MAXPATHLEN;
	
	MALLOC(buff, char *, len, M_TEMP, M_WAITOK);
	error = vn_getpath(vp, buff, &len);
	if (error == 0) {
	    if (len < uap->len) 
		error = copyout(buff, (void *)uap->pathbuf, len);
	    else
		error = ENOSPC;
	}
	FREE(buff, M_TEMP);
	if (error) {
	    goto err1;
	}
    }

    // now copy out the pid of the person that changed the file
    if (uap->pid) {
	if ((error = copyout((caddr_t)&pid, (void *)uap->pid, sizeof(pid_t))) != 0) {
	    printf("fmod_watch: failed to copy out the pid (%d)\n", pid);
	    goto err1;
	}
    }
    
    // now create a file descriptor for this vnode
    fdp = p->p_fd;
    flags = FREAD;
    if (error = falloc(p, &fp, &indx)) {
	printf("fmod_watch: failed to allocate an fd...\n");
	goto err2;
    }
    
    if ((error = copyout((caddr_t)&indx, (void *)uap->new_fd, sizeof(int))) != 0) {
	printf("fmod_watch: failed to copy out the new fd (%d)\n", indx);
	goto err3;
    }
    
    fp->f_flag = flags & FMASK;
    fp->f_type = DTYPE_VNODE;
    fp->f_ops = &vnops;
    fp->f_data = (caddr_t)vp;

    if (UBCINFOEXISTS(vp) && ((didhold = ubc_hold(vp)) == 0)) {
	goto err3;
    }

    error = VOP_OPEN(vp, flags, p->p_ucred, p);
    if (error) {
	goto err4;
    }

    VOP_UNLOCK(vp, 0, p);
    
    *fdflags(p, indx) &= ~UF_RESERVED;

    // note: we explicitly don't vrele() here because it
    //       happens when the fd is closed.

    return error;

  err4:
    if (didhold) {
	ubc_rele(vp);
    }
  err3:
    ffree(fp);
    fdrelse(p, indx);
  err2:
    VOP_UNLOCK(vp, 0, p);
  err1:
    vrele(vp);    // undoes the vref() in notify_filemod_watchers()

  err0:
    *retval = -1;
    return error;
}

static int
enable_fmod_watching(register_t *retval)
{
    *retval = -1;

    if (!is_suser()) {
	return EPERM;
    }
    
    // XXXdbg for now we only allow one watcher at a time.
    if (fmod_watch_enabled) {
	return EBUSY;
    }
    
    fmod_watch_enabled++;
    fmod_watch_owner = current_proc()->p_pid;

    *retval = 0;
    return 0;
}

static int
disable_fmod_watching(register_t *retval)
{
    if (!is_suser()) {
	return EPERM;
    }
    
    if (fmod_watch_enabled < 1) {
	printf("fmod_watching: too many disables! (%d)\n", fmod_watch_enabled);
	return EINVAL;
    }

    fmod_watch_enabled--;
    
    // if we're the last guy, clear out any remaining vnodes
    // in the table so they don't remain referenced.
    //
    if (fmod_watch_enabled == 0) {
	int i;
	for(i=changed_rd_index; i != changed_wr_index; ) {
	    if (changed_nodes[i] == NULL) {
		panic("disable_fmod_watch: index %d is NULL!\n", i);
	    }
	    vrele((struct vnode *)changed_nodes[i]);
	    changed_nodes[i] = NULL;
	    i = (i + 1) % NUM_CHANGE_NODES;
	}
	changed_wr_index = changed_rd_index = 0;

	fmod_watch_owner = 0;
    }

    // wake up anyone that may be waiting for the
    // queue to clear out.
    //
    while(notifier_sleeping) {
	wakeup((caddr_t)&changed_wr_index);

	// yield the cpu so the notifiers can run
	tsleep((caddr_t)&fmod_watch_enabled, PINOD, "disable_fmod_watch", 1);
    }

    *retval = 0;
    return 0;
}


struct fmod_watch_enable_args {
    int on_or_off;
};

int
fmod_watch_enable(struct proc *p, struct fmod_watch_enable_args *uap, register_t *retval)
{
    int ret;
    
    if (uap->on_or_off != 0) {
	ret = enable_fmod_watching(retval);
    } else {
	ret = disable_fmod_watching(retval);
    }

    return ret;
}

void
clean_up_fmod_watch(struct proc *p)
{
    if (fmod_watch_enabled && fmod_watch_owner == p->p_pid) {
	register_t *retval;
	
	disable_fmod_watching(&retval);
    }
}

/*
 * Syscall to push extended attribute configuration information into the
 * VFS.  Accepts a path, which it converts to a mountpoint, as well as
 * a command (int cmd), and attribute name and misc data.  For now, the
 * attribute name is left in userspace for consumption by the VFS_op.
 * It will probably be changed to be copied into sysspace by the
 * syscall in the future, once issues with various consumers of the
 * attribute code have raised their hands.
 */

struct extattrctl_args {
	char * path;
	int cmd;
	char * filename;
	int attrnamespace;
	char * attrname;
};

int
extattrctl(p, uap, retval)
	struct proc *p;
	register struct extattrctl_args *uap;
	register_t *retval;
{
	return (ENOSYS);
#if 0
	struct vnode *filename_vp;
	struct nameidata nd;
	struct mount *mp, *mp_writable;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	/*
	 * uap->attrname is not always defined.  We check again later when we
	 * invoke the VFS call so as to pass in NULL there if needed.
	 */
	if (uap->attrname != NULL) {
		error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN,
		    &dummy);
		if (error)
			return (error);
	}

	/*
	 * uap->filename is not always defined.  If it is, grab a vnode lock,
	 * which VFS_EXTATTRCTL() will later release.
	 */
	filename_vp = NULL;
	if (uap->filename != NULL) {
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
		    uap->filename, p);
		error = namei(&nd);
		if (error)
			return (error);
		filename_vp = nd.ni_vp;
		NDFREE(&nd, NDF_NO_VP_RELE | NDF_NO_VP_UNLOCK);
	}

	/* uap->path is always defined. */
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error) {
		if (filename_vp != NULL)
			vput(filename_vp);
		return (error);
	}
	mp = nd.ni_vp->v_mount;
	error = vn_start_write(nd.ni_vp, &mp_writable, V_WAIT | PCATCH);
	NDFREE(&nd, 0);
	if (error) {
		if (filename_vp != NULL)
			vput(filename_vp);
		return (error);
	}

	error = VFS_EXTATTRCTL(mp, uap->cmd, filename_vp, uap->attrnamespace,
	    uap->attrname != NULL ? attrname : NULL, p);

	vn_finished_write(mp_writable);
	/*
	 * VFS_EXTATTRCTL will have unlocked, but not de-ref'd,
	 * filename_vp, so vrele it if it is defined.
	 */
	if (filename_vp != NULL)
		vrele(filename_vp);
	return (error);
#endif
}

/*-
 * Set a named extended attribute on a file or directory
 * 
 * Arguments: unlocked vnode "vp", attribute namespace "attrnamespace",
 *            kernelspace string pointer "attrname", userspace buffer
 *            pointer "data", buffer length "nbytes", thread "td".
 * Returns: 0 on success, an error number otherwise
 * Locks: none
 * References: vp must be a valid reference for the duration of the call
 */
static int
extattr_set_vp(struct vnode *vp, int attrnamespace, const char *attrname,
    void *data, size_t nbytes, struct proc *p, register_t *retval)
{
	struct mount *mp;
	struct uio auio;
	struct iovec aiov;
	ssize_t cnt;
	int error;

	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	aiov.iov_base = data;
	aiov.iov_len = nbytes;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	if (nbytes > INT_MAX) {
		error = EINVAL;
		goto done;
	}
	auio.uio_resid = nbytes;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	cnt = nbytes;

#ifdef MAC
	error = mac_check_vnode_setextattr(p->p_ucred, vp, attrnamespace,
	    attrname, &auio);
	if (error)
		goto done;
#endif

	error = VOP_SETEXTATTR(vp, attrnamespace, attrname, &auio,
	    p->p_ucred, p);
	cnt -= auio.uio_resid;
	*retval = cnt;

done:
	VOP_UNLOCK(vp, 0, p);
	return (error);
}

struct extattr_set_fd_args {
	int fd;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_set_fd(p, uap, retval)
	struct proc *p;
	register struct extattr_set_fd_args *uap;
	register_t *retval;
{
	struct file *fp;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	AUDIT_ARG(fd, uap->fd);

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	error = getvnode(p, uap->fd, &fp);
	if (error)
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	error = extattr_set_vp((struct vnode *)fp->f_data, uap->attrnamespace,
	    attrname, uap->data, uap->nbytes, p, retval);

	return (error);
}

struct extattr_set_file_args {
	char *path;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_set_file(p, uap, retval)
	struct proc *p;
	register struct extattr_set_file_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_set_vp(nd.ni_vp, uap->attrnamespace, attrname,
	    uap->data, uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}

struct extattr_set_link_args {
	char *path;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_set_link(p, uap, retval)
	struct proc *p;
	register struct extattr_set_link_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	NDINIT(&nd, LOOKUP, NOFOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_set_vp(nd.ni_vp, uap->attrnamespace, attrname,
	    uap->data, uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}

/*-
 * Get a named extended attribute on a file or directory
 * 
 * Arguments: unlocked vnode "vp", attribute namespace "attrnamespace",
 *            kernelspace string pointer "attrname", userspace buffer
 *            pointer "data", buffer length "nbytes", thread "td".
 * Returns: 0 on success, an error number otherwise
 * Locks: none
 * References: vp must be a valid reference for the duration of the call
 */
static int
extattr_get_vp(struct vnode *vp, int attrnamespace, const char *attrname,
    void *data, size_t nbytes, struct proc *p, register_t *retval)
{
	struct uio auio, *auiop;
	struct iovec aiov;
	ssize_t cnt;
	size_t size, *sizep;
	int error;

#if 0
	/*
	 * XXX: Temporary API compatibility for applications that know
	 * about this hack ("" means list), but haven't been updated
	 * for the extattr_list_*() system calls yet.  This will go
	 * away for FreeBSD 5.3.
	 */
	if (strlen(attrname) == 0)
		return (extattr_list_vp(vp, attrnamespace, data, nbytes, p, retval));
#endif

	VOP_LEASE(vp, p, p->p_ucred, LEASE_READ);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	/*
	 * Slightly unusual semantics: if the user provides a NULL data
	 * pointer, they don't want to receive the data, just the
	 * maximum read length.
	 */
	auiop = NULL;
	sizep = NULL;
	cnt = 0;
	if (data != NULL) {
		aiov.iov_base = data;
		aiov.iov_len = nbytes;
		auio.uio_iov = &aiov;
		auio.uio_offset = 0;
		if (nbytes > INT_MAX) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid = nbytes;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_procp = p;
		auiop = &auio;
		cnt = nbytes;
	} else
		sizep = &size;

#ifdef MAC
	error = mac_check_vnode_getextattr(p->p_ucred, vp, attrnamespace,
	    attrname, &auio);
	if (error)
		goto done;
#endif

	error = VOP_GETEXTATTR(vp, attrnamespace, attrname, auiop, sizep,
	    p->p_ucred, p);

	if (auiop != NULL) {
		cnt -= auio.uio_resid;
		*retval = cnt;
	} else
		*retval = size;

done:
	VOP_UNLOCK(vp, 0, p);
	return (error);
}

struct extattr_get_fd_args {
	int fd;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_get_fd(p, uap, retval)
	struct proc *p;
	register struct extattr_get_fd_args *uap;
	register_t *retval;
{
	struct file *fp;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	AUDIT_ARG(fd, uap->fd);

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	error = getvnode(p, uap->fd, &fp);
	if (error)
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	error = extattr_get_vp((struct vnode *)fp->f_data, uap->attrnamespace,
	    attrname, uap->data, uap->nbytes, p, retval);

	return (error);
}

struct extattr_get_file_args {
	char *path;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_get_file(p, uap, retval)
	struct proc *p;
	register struct extattr_get_file_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_get_vp(nd.ni_vp, uap->attrnamespace, attrname,
	    uap->data, uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}

struct extattr_get_link_args {
	char *path;
	int attrnamespace;
	char *attrname;
	void *data;
	size_t nbytes;
};

int
extattr_get_link(p, uap, retval)
	struct proc *p;
	register struct extattr_get_link_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	NDINIT(&nd, LOOKUP, NOFOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_get_vp(nd.ni_vp, uap->attrnamespace, attrname,
	    uap->data, uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}

/*
 * extattr_delete_vp(): Delete a named extended attribute on a file or
 *                      directory
 * 
 * Arguments: unlocked vnode "vp", attribute namespace "attrnamespace",
 *            kernelspace string pointer "attrname", proc "p"
 * Returns: 0 on success, an error number otherwise
 * Locks: none
 * References: vp must be a valid reference for the duration of the call
 */
static int
extattr_delete_vp(struct vnode *vp, int attrnamespace, const char *attrname,
    struct proc *p, register_t *retval)
{
	struct mount *mp;
	int error;

	if (error)
		return (error);
	VOP_LEASE(vp, p, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

#ifdef MAC
	error = mac_check_vnode_deleteextattr(p->p_ucred, vp, attrnamespace,
	    attrname);
	if (error)
		goto done;
#endif

	error = VOP_DELETEEXTATTR(vp, attrnamespace, attrname, p->p_ucred,
	    p);
	if (error == EOPNOTSUPP)
		error = VOP_SETEXTATTR(vp, attrnamespace, attrname, NULL,
		    p->p_ucred, p);
#ifdef MAC
done:
#endif
	VOP_UNLOCK(vp, 0, p);
	return (error);
}

struct extattr_delete_fd_args {
	int fd;
	int attrnamespace;
	char *attrname;
};

int
extattr_delete_fd(p, uap, retval)
	struct proc *p;
	register struct extattr_delete_fd_args *uap;
	register_t *retval;
{
	struct file *fp;
	struct vnode *vp;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	AUDIT_ARG(fd, uap->fd);

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return (error);

	error = getvnode(p, uap->fd, &fp);
	if (error)
		return (error);
	vp = (struct vnode *)fp->f_data;

	AUDIT_ARG(vnpath, vp, ARG_VNODE1);

	error = extattr_delete_vp(vp, uap->attrnamespace, attrname, p, retval);
	return (error);
}

struct extattr_delete_file_args {
	char *path;
	int attrnamespace;
	char *attrname;
};

int
extattr_delete_file(p, uap, retval)
	struct proc *p;
	register struct extattr_delete_file_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return(error);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return(error);

	error = extattr_delete_vp(nd.ni_vp, uap->attrnamespace, attrname, p,
	    retval);
	vrele(nd.ni_vp);
	return(error);
}

struct extattr_delete_link_args {
	char *path;
	int attrnamespace;
	char *attrname;
};

int
extattr_delete_link(p, uap, retval)
	struct proc *p;
	register struct extattr_delete_link_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	size_t dummy;
	int error;

	error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN, &dummy);
	if (error)
		return(error);

	NDINIT(&nd, LOOKUP, NOFOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return(error);

	error = extattr_delete_vp(nd.ni_vp, uap->attrnamespace, attrname, p,
	    retval);
	vrele(nd.ni_vp);
	return(error);
}

/*-
 * Retrieve a list of extended attributes on a file or directory.
 *
 * Arguments: unlocked vnode "vp", attribute namespace 'attrnamespace",
 *            userspace buffer pointer "data", buffer length "nbytes",
 *            thread "td".
 * Returns: 0 on success, an error number otherwise
 * Locks: none
 * References: vp must be a valid reference for the duration of the call
 */
static int
extattr_list_vp(struct vnode *vp, int attrnamespace, void *data,
    size_t nbytes, struct proc *p, register_t *retval)
{
	struct uio auio, *auiop;
	size_t size, *sizep;
	struct iovec aiov;
	ssize_t cnt;
	int error;

	VOP_LEASE(vp, p, p->p_ucred, LEASE_READ);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);

	auiop = NULL;
	sizep = NULL;
	cnt = 0;
	if (data != NULL) {
		aiov.iov_base = data;
		aiov.iov_len = nbytes;
		auio.uio_iov = &aiov;
		auio.uio_offset = 0;
		if (nbytes > INT_MAX) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid = nbytes;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_procp = p;
		auiop = &auio;
		cnt = nbytes;
	} else
		sizep = &size;

#ifdef MAC
	error = mac_check_vnode_listextattr(p->p_ucred, vp, attrnamespace);
	if (error)
		goto done;
#endif

	error = VOP_LISTEXTATTR(vp, attrnamespace, auiop, sizep,
	    p->p_ucred, p);

	if (auiop != NULL) {
		cnt -= auio.uio_resid;
		*retval = cnt;
	} else
		*retval = size;

done:
	VOP_UNLOCK(vp, 0, p);
	return (error);
}


struct extattr_list_fd_args {
	int fd;
	int attrnamespace;
	void *data;
	size_t nbytes;
};

int
extattr_list_fd(p, uap, retval)
	struct proc *p;
	register struct extattr_list_fd_args *uap;
	register_t *retval;
{
	struct file *fp;
	int error;

	AUDIT_ARG(fd, uap->fd);

	error = getvnode(p, uap->fd, &fp);
	if (error)
		return (error);

	AUDIT_ARG(vnpath, (struct vnode *)fp->f_data, ARG_VNODE1);

	error = extattr_list_vp((struct vnode *)fp->f_data, 
	    uap->attrnamespace, uap->data,
	    uap->nbytes, p, retval);

	return (error);
}

struct extattr_list_file_args {
	char *path;
	int attrnamespace;
	void *data;
	size_t nbytes;
};
int
extattr_list_file(p, uap, retval)
	struct proc *p;
	register struct extattr_list_file_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_list_vp(nd.ni_vp, uap->attrnamespace, uap->data,
	    uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}

struct extattr_list_link_args {
	char *path;
	int attrnamespace;
	void *data;
	size_t nbytes;
};

int
extattr_list_link(p, uap, retval)
	struct proc *p;
	register struct extattr_list_link_args *uap;
	register_t *retval;
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, NOFOLLOW | AUDITVNPATH1, UIO_USERSPACE, uap->path, p);
	error = namei(&nd);
	if (error)
		return (error);

	error = extattr_list_vp(nd.ni_vp, uap->attrnamespace, uap->data,
	    uap->nbytes, p, retval);

	vrele(nd.ni_vp);
	return (error);
}
