
/*-
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001, 2002, 2003, 2004 Networks Associates Technology, Inc.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <security/mac_internal.h>
#include <sys/vnode.h>
#include <miscfs/devfs/devfsdefs.h>
#include <sys/namei.h>
#include <sys/mount.h>

extern struct mac_label_element_list_t mac_label_element_list;

/*
 * Warn about EA transactions only the first time they happen.
 * Weak coherency, no locking.
 */
static int    ea_warn_once = 0;


static struct label *
mac_devfsdirent_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(MAC_WAITOK);
	MAC_PERFORM(init_devfsdirent_label, label);
	MAC_DEBUG_COUNTER_INC(&nmacdevfsdirents);
	return (label);
}

void
mac_init_devfsdirent(struct devnode *de)
{

	de->dn_label = mac_devfsdirent_label_alloc();
}

static struct label *
mac_mount_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(MAC_WAITOK);
	MAC_PERFORM(init_mount_label, label);
	MAC_DEBUG_COUNTER_INC(&nmacmounts);
	return (label);
}

static struct label *
mac_mount_fs_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(MAC_WAITOK);
	MAC_PERFORM(init_mount_fs_label, label);
	MAC_DEBUG_COUNTER_INC(&nmacmounts);
	return (label);
}

void
mac_init_mount(struct mount *mp)
{

	mp->mnt_mntlabel = mac_mount_label_alloc();
	mp->mnt_fslabel = mac_mount_fs_label_alloc();
}

struct label *
mac_vnode_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(MAC_WAITOK);
	MAC_PERFORM(init_vnode_label, label);
	MAC_DEBUG_COUNTER_INC(&nmacvnodes);
	return (label);
}

void
mac_init_vnode(struct vnode *vp)
{

	vp->v_label = mac_vnode_label_alloc();
}

static void
mac_devfsdirent_label_free(struct label *label)
{

	MAC_PERFORM(destroy_devfsdirent_label, label);
	mac_labelzone_free(label);
	MAC_DEBUG_COUNTER_DEC(&nmacdevfsdirents);
}

void
mac_destroy_devfsdirent(struct devnode *de)
{

	mac_devfsdirent_label_free(de->dn_label);
	de->dn_label = NULL;
}

static void
mac_mount_label_free(struct label *label)
{

	MAC_PERFORM(destroy_mount_label, label);
	mac_labelzone_free(label);
	MAC_DEBUG_COUNTER_DEC(&nmacmounts);
}

static void
mac_mount_fs_label_free(struct label *label)
{

	MAC_PERFORM(destroy_mount_fs_label, label);
	mac_labelzone_free(label);
	MAC_DEBUG_COUNTER_DEC(&nmacmounts);
}

void
mac_destroy_mount(struct mount *mp)
{

	if (mp->mnt_fslabel) {
		mac_mount_fs_label_free(mp->mnt_fslabel);
		mp->mnt_fslabel = NULL;
	}
	if (mp->mnt_mntlabel) {
		mac_mount_label_free(mp->mnt_mntlabel);
		mp->mnt_mntlabel = NULL;
	}
}

void
mac_vnode_label_free(struct label *label)
{

	MAC_PERFORM(destroy_vnode_label, label);
	mac_labelzone_free(label);
	MAC_DEBUG_COUNTER_DEC(&nmacvnodes);
}

void
mac_destroy_vnode(struct vnode *vp)
{

	mac_vnode_label_free(vp->v_label);
	vp->v_label = NULL;
	MAC_DEBUG_COUNTER_DEC(&nmacvnodes);
}

void
mac_copy_vnode_label(struct label *src, struct label *dest)
{

	MAC_PERFORM(copy_vnode_label, src, dest);
}


int
mac_get_vnode_audit_labels(struct vnode *vp, struct mac *mac)
{
	int error = 0;
	int count;

	// It is assumed that any necessary vnode locking is done on entry
	MAC_EXTERNALIZE_REGISTERED_LABELS(vnode_audit, vp->v_label,
		mac->m_string, mac->m_buflen, count);

	// error is set by MAC_EXTERNALIZE_REGISTERED_LABELS
	return (error);
}

int
mac_externalize_vnode_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen, int flags)
{
	int error = 0;

	if (elements[0] == '*') {
		int count;
		MAC_EXTERNALIZE_REGISTERED_LABELS(vnode, label, outbuf,
			outbuflen, count);
	} else
		MAC_EXTERNALIZE_LIST(vnode, label, elements, outbuf, outbuflen);

	// error is set by MAC_EXTERNALIZE_*
	return (error);
}

int
mac_internalize_vnode_label(struct label *label, char *string)
{
	int error;

	MAC_INTERNALIZE_LIST(vnode, label, string);

	return (error);
}

void
mac_copy_devfs_label(struct label *src, struct label *dest)
{
	MAC_PERFORM(copy_devfs_label, src, dest);
}

void
mac_update_devfsdirent(struct mount *mp, struct devnode *de,
    struct vnode *vp)
{

	MAC_PERFORM(update_devfsdirent, mp, de, de->dn_label, vp,
	    vp->v_label);
}

void
mac_associate_vnode_devfs(struct mount *mp, struct devnode *de,
    struct vnode *vp)
{

	MAC_PERFORM(associate_vnode_devfs, mp, mp ? mp->mnt_fslabel : NULL, de,
	    de->dn_label, vp, vp->v_label);
}

int
mac_associate_vnode_extattr(struct mount *mp, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_associate_vnode_extattr");

	MAC_CHECK(associate_vnode_extattr, mp, mp->mnt_fslabel, vp,
	    vp->v_label);

	return (error);
}

void
mac_associate_vnode_singlelabel(struct mount *mp, struct vnode *vp)
{

	MAC_PERFORM(associate_vnode_singlelabel, mp, mp->mnt_fslabel, vp,
	    vp->v_label);
}

int
mac_create_vnode_extattr(struct ucred *cred, struct mount *mp,
    struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_create_vnode_extattr");
	ASSERT_VOP_LOCKED(vp, "mac_create_vnode_extattr");

	error = VOP_OPENEXTATTR(vp, cred, current_proc());
	if (error == EOPNOTSUPP) {
		/* XXX: Optionally abort if transactions not supported. */
		if (ea_warn_once == 0) {
			printf("Warning: transactions not supported "
			    "in EA write.\n");
			ea_warn_once = 1;
		}
	} else if (error)
		return (error);

	MAC_CHECK(create_vnode_extattr, cred, mp, mp->mnt_fslabel,
	    dvp, dvp->v_label, vp, vp->v_label, cnp);

	if (error) {
		VOP_CLOSEEXTATTR(vp, 0, NOCRED, current_proc());
		return (error);
	}

	error = VOP_CLOSEEXTATTR(vp, 1, NOCRED, current_proc());

	if (error == EOPNOTSUPP)
		error = 0;				/* XXX */

	return (error);
}

static int
mac_setlabel_vnode_extattr(struct ucred *cred, struct vnode *vp,
    struct label *intlabel)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_setlabel_vnode_extattr");

	error = VOP_OPENEXTATTR(vp, cred, current_proc());
	if (error == EOPNOTSUPP) {
		/* XXX: Optionally abort if transactions not supported. */
		if (ea_warn_once == 0) {
			printf("Warning: transactions not supported "
			    "in EA write.\n");
			ea_warn_once = 1;
		}
	} else if (error)
		return (error);

	MAC_CHECK(setlabel_vnode_extattr, cred, vp, vp->v_label, intlabel);

	if (error) {
		VOP_CLOSEEXTATTR(vp, 0, NOCRED, current_proc());
		return (error);
	}

	error = VOP_CLOSEEXTATTR(vp, 1, NOCRED, current_proc());

	if (error == EOPNOTSUPP)
		error = 0;				/* XXX */

	return (error);
}

void
mac_execve_transition(struct ucred *old, struct ucred *new, struct vnode *vp,
    struct label *scriptvnodelabel, struct label *execl)
{

	ASSERT_VOP_LOCKED(vp, "mac_execve_transition");

	if (!mac_enforce_process && !mac_enforce_fs)
		return;

	MAC_PERFORM(execve_transition, old, new, vp, vp->v_label,
	    scriptvnodelabel, execl);
}

int
mac_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *scriptvnodelabel, struct label *execlabel, struct proc *p)
{
	int result;

	ASSERT_VOP_LOCKED(vp, "mac_execve_will_transition");

	if (!mac_enforce_process && !mac_enforce_fs)
		return (0);

	result = 0;
	MAC_BOOLEAN(execve_will_transition, ||, old, vp, vp->v_label,
	    scriptvnodelabel, execlabel, p);

	return (result);
}

int
mac_check_vnode_access(struct ucred *cred, struct vnode *vp, int acc_mode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_access");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_access, cred, vp, vp->v_label, acc_mode);
	return (error);
}

int
mac_check_vnode_chdir(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_chdir");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_chdir, cred, dvp, dvp->v_label);
	return (error);
}

int
mac_check_vnode_chroot(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_chroot");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_chroot, cred, dvp, dvp->v_label);
	return (error);
}

int
mac_check_vnode_create(struct ucred *cred, struct vnode *dvp,
    struct componentname *cnp, struct vattr *vap)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_create");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_create, cred, dvp, dvp->v_label, cnp, vap);
	return (error);
}

int
mac_check_vnode_delete(struct ucred *cred, struct vnode *dvp, struct vnode *vp,
    struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_delete");
	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_delete");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_delete, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	return (error);
}

#if 0
int
mac_check_vnode_deleteacl(struct ucred *cred, struct vnode *vp,
    acl_type_t type)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_deleteacl");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_deleteacl, cred, vp, vp->v_label, type);
	return (error);
}
#endif

int
mac_check_vnode_deleteextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_deleteextattr");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_deleteextattr, cred, vp, attrnamespace, name);
	return (error);
}

int
mac_check_vnode_exchangedata(struct ucred *cred,
    struct vnode *v1, struct vnode *v2)
{
	int error;

	ASSERT_VOP_LOCKED(v1, "mac_check_vnode_exchangedata 1");
	ASSERT_VOP_LOCKED(v2, "mac_check_vnode_exchangedata 2");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_exchangedata, cred, v1, v1->v_label, 
	    v2, v2->v_label);

	return (error);
}

int
mac_check_vnode_exec(struct ucred *cred, struct vnode *vp, struct label *execl)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_exec");

	if (!mac_enforce_process && !mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_exec, cred, vp, vp->v_label, execl);

	return (error);
}

#if 0
int
mac_check_vnode_getacl(struct ucred *cred, struct vnode *vp, acl_type_t type)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_getacl");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_getacl, cred, vp, vp->v_label, type);
	return (error);
}
#endif

int
mac_check_vnode_getattrlist(struct ucred *cred, struct vnode *vp,
    struct attrlist *alist, struct uio *ab)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_getattrlist");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_getattrlist, cred, vp, vp->v_label, alist, ab);

	/* Falsify results instead of returning error? */
	return (error);
}

int
mac_check_vnode_getextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name, struct uio *uio)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_getextattr");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_getextattr, cred, vp, vp->v_label,
	    attrnamespace, name, uio);
	return (error);
}

int
mac_check_vnode_link(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_link");
	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_link");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_link, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	return (error);
}

int
mac_check_vnode_listextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_listextattr");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_listextattr, cred, vp, attrnamespace);
	return (error);
}

int
mac_check_vnode_lookup(struct ucred *cred, struct vnode *dvp,
    struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_lookup");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_lookup, cred, dvp, dvp->v_label, cnp);
	return (error);
}

/*
 * On some platforms, VM_PROT_READ implies VM_PROT_EXECUTE. If that is true,
 * both prot and maxprot will have VM_PROT_EXECUTE set after check_vnode_mmap
 * if VM_PROT_READ is set.
 *
 * The type of maxprot in check_vnode_mmap must be equivalent to vm_prot_t *
 * (defined in <mach/vm_prot.h>). mac_policy.h does not include any header files,
 * so cannot use the typedef itself.
 */

int
mac_check_vnode_mmap(struct ucred *cred, struct vnode *vp, int prot, int flags,
    int *maxprot)
{
	int error;
	int maxp;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_mmap");

	if (!mac_enforce_fs || !mac_enforce_vm)
		return (0);

	maxp = *maxprot;
	MAC_CHECK(check_vnode_mmap, cred, vp, vp->v_label, prot, flags, &maxp);
	if ((maxp | *maxprot) != *maxprot)
		panic("check_vnode_mmap increased max protections");
	*maxprot = maxp;
	return (error);
}

void
mac_check_vnode_mmap_downgrade(struct ucred *cred, struct vnode *vp, int *prot)
{
	int result = *prot;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_mmap_downgrade");

	if (!mac_enforce_fs || !mac_enforce_vm)
		return;

	MAC_PERFORM(check_vnode_mmap_downgrade, cred, vp, vp->v_label,
	    &result);

	*prot = result;
}

int
mac_check_vnode_mprotect(struct ucred *cred, struct vnode *vp, int prot)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_mprotect");

	if (!mac_enforce_fs || !mac_enforce_vm)
		return (0);

	MAC_CHECK(check_vnode_mprotect, cred, vp, vp->v_label, prot);
	return (error);
}

int
mac_check_vnode_open(struct ucred *cred, struct vnode *vp, int acc_mode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_open");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_open, cred, vp, vp->v_label, acc_mode);
	return (error);
}

int
mac_check_vnode_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_poll");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_poll, active_cred, file_cred, vp,
	    vp->v_label);

	return (error);
}

int
mac_check_vnode_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_read");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_read, active_cred, file_cred, vp,
	    vp->v_label);

	return (error);
}

int
mac_check_vnode_readdir(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_readdir");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_readdir, cred, dvp, dvp->v_label);
	return (error);
}

int
mac_check_vnode_readlink(struct ucred *cred, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_readlink");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_readlink, cred, vp, vp->v_label);
	return (error);
}

int
mac_check_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *newlabel)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_relabel");

	MAC_CHECK(check_vnode_relabel, cred, vp, vp->v_label, newlabel);

	return (error);
}

int
mac_check_vnode_rename_from(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_rename_from");
	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_rename_from");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_rename_from, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	return (error);
}

int
mac_check_vnode_rename_to(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, int samedir, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_check_vnode_rename_to");
	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_rename_to");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_rename_to, cred, dvp, dvp->v_label, vp,
	    vp != NULL ? vp->v_label : NULL, samedir, cnp);
	return (error);
}

int
mac_check_vnode_revoke(struct ucred *cred, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_revoke");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_revoke, cred, vp, vp->v_label);
	return (error);
}

int
mac_check_vnode_select(struct ucred *cred, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_select");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_select, cred, vp, vp->v_label);
	return (error);
}

#if 0
int
mac_check_vnode_setacl(struct ucred *cred, struct vnode *vp, acl_type_t type,
    struct acl *acl)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setacl");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setacl, cred, vp, vp->v_label, type, acl);
	return (error);
}
#endif

int
mac_check_vnode_setattrlist(struct ucred *cred, struct vnode *vp,
    struct attrlist *alist, struct uio *ab)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setattrlist");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setattrlist, cred, vp, vp->v_label, alist, ab);
	return (error);
}

int
mac_check_vnode_setextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name, struct uio *uio)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setextattr");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setextattr, cred, vp, vp->v_label,
	    attrnamespace, name, uio);
	return (error);
}

int
mac_check_vnode_setflags(struct ucred *cred, struct vnode *vp, u_long flags)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setflags");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setflags, cred, vp, vp->v_label, flags);
	return (error);
}

int
mac_check_vnode_setmode(struct ucred *cred, struct vnode *vp, mode_t mode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setmode");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setmode, cred, vp, vp->v_label, mode);
	return (error);
}

int
mac_check_vnode_setowner(struct ucred *cred, struct vnode *vp, uid_t uid,
    gid_t gid)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setowner");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setowner, cred, vp, vp->v_label, uid, gid);
	return (error);
}

int
mac_check_vnode_setutimes(struct ucred *cred, struct vnode *vp,
    struct timespec atime, struct timespec mtime)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_setutimes");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_setutimes, cred, vp, vp->v_label, atime,
	    mtime);
	return (error);
}

int
mac_check_vnode_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_stat");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_stat, active_cred, file_cred, vp,
	    vp->v_label);
	return (error);
}

int
mac_check_vnode_write(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_check_vnode_write");

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_vnode_write, active_cred, file_cred, vp,
	    vp->v_label);

	return (error);
}

void
mac_relabel_vnode(struct ucred *cred, struct vnode *vp, struct label *newlabel)
{

	MAC_PERFORM(relabel_vnode, cred, vp, vp->v_label, newlabel);
}

void
mac_create_mount(struct ucred *cred, struct mount *mp)
{

	MAC_PERFORM(create_mount, cred, mp, mp->mnt_mntlabel,
	    mp->mnt_fslabel);
}

int
mac_check_mount_stat(struct ucred *cred, struct mount *mount)
{
	int error;

	if (!mac_enforce_fs)
		return (0);

	MAC_CHECK(check_mount_stat, cred, mount, mount->mnt_mntlabel);

	return (error);
}

void
mac_create_devfs_device(struct ucred *cr, struct mount *mp, dev_t dev, 
    struct devnode *de, const char *fullpath)
{

  MAC_PERFORM(create_devfs_device, cr, mp, dev, de, de->dn_label,
	      fullpath);
}

#if 0
void
mac_create_devfs_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct devfs_dirent *de, const char *fullpath)
{

	MAC_PERFORM(create_devfs_symlink, cred, mp, dd, &dd->de_label, de,
	    de->de_label, fullpath);
}
#endif /* 0 */

void
mac_create_devfs_directory(struct mount *mp, char *dirname, int dirnamelen,
    struct devnode *de, const char *fullpath)
{

	MAC_PERFORM(create_devfs_directory, mp, dirname, dirnamelen, de,
	    de->dn_label, fullpath);
}

/*
 * Implementation of VOP_SETLABEL() that relies on extended attributes
 * to store label data.  Can be referenced by filesystems supporting
 * extended attributes.
 */
int
vop_stdsetlabel_ea(struct vop_setlabel_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct label *intlabel = ap->a_vl;
	int error;

	ASSERT_VOP_LOCKED(vp, "vop_stdsetlabel_ea");

	if ((vp->v_mount->mnt_flag & MNT_MULTILABEL) == 0)
		return (EOPNOTSUPP);

	error = mac_setlabel_vnode_extattr(ap->a_cred, vp, intlabel);
	if (error)
		return (error);

	mac_relabel_vnode(ap->a_cred, vp, intlabel);

	return (0);
}

int
vn_setlabel(struct vnode *vp, struct label *intlabel, struct ucred *cred,
	    struct proc *p)
{
	int error;


#if 0
	/* TBD/XXX - We don' have multilabel vs. singlelabel semantics yet */
	if (vp->v_mount == NULL) {
		/* printf("vn_setlabel: null v_mount\n"); */
		if (vp->v_type != VNON)
			printf("vn_setlabel: null v_mount with non-VNON\n");
		return (EBADF);
	}

	if ((vp->v_mount->mnt_flag & MNT_MULTILABEL) == 0)
		return (EOPNOTSUPP);
#endif
	/*
	 * Multi-phase commit.  First check the policies to confirm the
	 * change is OK.  Then commit via the filesystem.  Finally,
	 * update the actual vnode label.  Question: maybe the filesystem
	 * should update the vnode at the end as part of VOP_SETLABEL()?
	 */
	error = mac_check_vnode_relabel(cred, vp, intlabel);
	if (error)
		return (error);

	/*
	 * VADMIN provides the opportunity for the filesystem to make
	 * decisions about who is and is not able to modify labels
	 * and protections on files.  This might not be right.  We can't
	 * assume VOP_SETLABEL() will do it, because we might implement
	 * that as part of vop_stdsetlabel_ea().
	 */
	error = VOP_ACCESS(vp, VADMIN, cred, p);
	if (error) {
		printf("vn_setlabel: vop access failed %d\n", error);
		return (error);
	}

	error = VOP_SETLABEL(vp, intlabel, cred, p);
	if (error) {
		printf("vn_setlabel: vop setlabel failed %d\n", error);
		return (error);
	}

	return (0);
}
 
#ifdef MAC_NETWORK

int
mac_check_fcntl(struct ucred *cred, struct file *fd, int cmd, int arg) 
{
	int error;
	
	MAC_CHECK(check_fcntl, cred, fd, cmd, arg);
	return (error);
}

int
mac_check_ioctl(struct ucred *cred, struct file *fd, int com, void *data)
{
	int error;
	
	MAC_CHECK(check_ioctl, cred, fd, com, data);
	return (error);
}

int
mac_check_get_fd(struct ucred *cred, struct file *fd, char *elements, int len)
{
	int error;
	
	MAC_CHECK(check_get_fd, cred, fd, elements, len);
	return (error);
}

int
mac_check_set_fd(struct ucred *cred, struct file *fd, char *buf, int buflen)
{
	int error;
	
	MAC_CHECK(check_set_fd, cred, fd, buf, buflen);
	return (error);
}

#else

int
mac_check_fcntl(struct ucred *cred, struct file *fdp, int cmd, int arg)
{
	return (0);
}

int
mac_check_ioctl(struct ucred *cred, struct file *fdp, int com, void *data)
{
	return (0);
}

int
mac_check_get_fd(struct ucred *cred, struct file *fd, char *elements, int len)
{
	return (0);
}

int
mac_check_set_fd(struct ucred *cred, struct file *fd, char *buf, int buflen)
{
	return (0);
}

#endif  /* MAC_NETWORK */
