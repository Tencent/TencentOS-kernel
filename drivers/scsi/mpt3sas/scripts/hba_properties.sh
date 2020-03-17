# DISCLAIMER OF LIABILITY
# THIS IS SAMPLE SCRIPT. 
# NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
# HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

#!/bin/bash

clear
scsi_host="/sys/class/scsi_host"
cd ${scsi_host}
subfolders=`ls -1`
for i in ${subfolders};  do
	cd ${i};
	if [ `cat proc_name` == "mpt3sas" ]; then
		board=`cat board_name`
		assembly=`cat board_assembly`
		tracer=`cat board_tracer`
		ioc=`cat unique_id`;
		bios=`cat version_bios`
		fw=`cat version_fw`
		driver=`cat /sys/module/mpt3sas/version`
		product=`cat version_product`
		mpi=`cat version_mpi`
		nvdata_d=`cat version_nvdata_default`
		nvdata_p=`cat version_nvdata_persistent`
		logging_level=`cat logging_level`
		io_delay=`cat io_delay`
		device_delay=`cat device_delay`
		fw_queue_depth=`cat fw_queue_depth`
		sas_address=`cat host_sas_address`
		echo -e \\n${i}: ioc${ioc}: fw=${fw} bios=${bios} driver=${driver} mpi=${mpi}
		echo -e \\t ${product}: board_name=${board} assembly=${assembly} tracer=${tracer}
		echo -e \\t nvdata_persistent=${nvdata_p} nvdata_default=${nvdata_d}
		echo -e \\t io_delay=${io_delay} device_delay=${device_delay}
		echo -e \\t logging_level=${logging_level}
		echo -e \\t fw_queue_depth=${fw_queue_depth}
		echo -e \\t sas_address=${sas_address}
	fi;
	cd ${scsi_host}
done;
echo -e \\n
