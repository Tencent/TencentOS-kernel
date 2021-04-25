/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * Copyright (C) 2020  Broadcom Inc.
 *  (mailto:MPT-FusionLinux.pdl@broadcom.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/uio.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "mpt3sas_base.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

struct dentry *mpt3sas_debugfs_root = NULL;

/*
 * _debugfs_iocdump_read :	copy ioc dump from debugfs buffer
 */
static ssize_t
_debugfs_iocdump_read(struct file *filp, char __user *ubuf, size_t cnt,
	loff_t *ppos)

{
	struct mpt3sas_debugfs_buffer *debug = filp->private_data;

	if (!debug || !debug->buf)
		return 0;

	return simple_read_from_buffer(ubuf, cnt, ppos, debug->buf, debug->len);
}

/*
 * _debugfs_iocdump_open :	open the ioc_dump debugfs attribute file
 */
static int
_debugfs_iocdump_open(struct inode *inode, struct file *file)
{
	struct MPT3SAS_ADAPTER *ioc = inode->i_private;
	struct mpt3sas_debugfs_buffer *debug;

	debug = kzalloc(sizeof(struct mpt3sas_debugfs_buffer), GFP_KERNEL);
	if (!debug)
		return -ENOMEM;

	debug->buf = (void *)ioc;
	debug->len = sizeof(struct MPT3SAS_ADAPTER);
	file->private_data = debug;
	
	return 0;
}

/*
 * _debugfs_iocdump_release :	release the ioc_dump debugfs attribute
 */
static int
_debugfs_iocdump_release(struct inode *inode, struct file *file)
{
	struct mpt3sas_debugfs_buffer *debug = file->private_data;

	if (!debug)
		return 0;

	file->private_data = NULL;
	kfree(debug);
	return 0;
}

static const struct file_operations mpt3sas_debugfs_iocdump_fops = {
	.owner		= THIS_MODULE,
	.open           = _debugfs_iocdump_open,
	.read           = _debugfs_iocdump_read,
	.release        = _debugfs_iocdump_release,
};

/*
 * mpt3sas_init_debugfs :	Create debugfs root for mpt3sas driver
 */
void mpt3sas_init_debugfs(void)
{
	mpt3sas_debugfs_root = debugfs_create_dir("mpt3sas", NULL);
	if (!mpt3sas_debugfs_root)
		pr_info("mpt3sas: Cannot create debugfs root\n");
}

/*
 * mpt3sas_exit_debugfs :	Remove debugfs root for mpt3sas driver
 */
void mpt3sas_exit_debugfs(void)
{
	if (mpt3sas_debugfs_root)
		debugfs_remove_recursive(mpt3sas_debugfs_root);
}

/*
 * mpt3sas_setup_debugfs :	Setup debugfs per HBA adapter
 * ioc:				MPT3SAS_ADAPTER object
 */
void
mpt3sas_setup_debugfs(struct MPT3SAS_ADAPTER *ioc)
{
	char name[64];

	snprintf(name, sizeof(name), "scsi_host%d", ioc->shost->host_no);
	if (!ioc->debugfs_root) {
		ioc->debugfs_root =
		    debugfs_create_dir(name, mpt3sas_debugfs_root);
		if (!ioc->debugfs_root) {
			dev_err(&ioc->pdev->dev,
			    "Cannot create per adapter debugfs directory\n");
			return;
		}
	}

	snprintf(name, sizeof(name), "ioc_dump");
	ioc->ioc_dump =	debugfs_create_file(name, S_IRUGO,
	    ioc->debugfs_root, ioc, &mpt3sas_debugfs_iocdump_fops);
	if (!ioc->ioc_dump) {
		dev_err(&ioc->pdev->dev,
		    "Cannot create ioc_dump debugfs file\n");
		debugfs_remove(ioc->debugfs_root);
		return;
	}

	snprintf(name, sizeof(name), "host_recovery");
	debugfs_create_u8(name, S_IRUGO, ioc->debugfs_root, &ioc->shost_recovery);

}

/*
 * mpt3sas_destroy_debugfs :	Destroy debugfs per HBA adapter
 * ioc:				MPT3SAS_ADAPTER object
 */
void mpt3sas_destroy_debugfs(struct MPT3SAS_ADAPTER *ioc)
{
	if (ioc->debugfs_root) 
		debugfs_remove_recursive(ioc->debugfs_root);
}

#else
void mpt3sas_init_debugfs(void)
{
}
void mpt3sas_exit_debugfs(void)
{
}
void mpt3sas_setup_debugfs(struct megasas_instance *instance)
{
}
void mpt3sas_destroy_debugfs(struct megasas_instance *instance)
{
}
#endif /*CONFIG_DEBUG_FS*/

