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

trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

clear
CWD=`pwd`;
scsi_host="/sys/class/scsi_host"
cd ${scsi_host}
subfolders=`ls -1`
for i in ${subfolders};  do
	cd ${i};
	if [ `cat proc_name` == "mpt3sas" ]; then
		reset_count=`cat ioc_reset_count`
		{
			while true; do
				if [[ `cat /sys/kernel/debug/mpt3sas/scsi_${i}/host_recovery` == 1 && `cat ioc_reset_count` == $reset_count ]]; then
					date_str=$(date +'%y-%m-%d-%H-%M-%S')
					file_name=${i}-iocdump-${date_str};
					echo "==== host reset on ${i} is observed at ${date_str}, hence copied the ioc dump to file ${file_name} ===="
					cat /sys/kernel/debug/mpt3sas/scsi_${i}/ioc_dump > ${CWD}/$file_name;
					reset_count=`expr $reset_count + 1`;
				fi;
				sleep 1;
			done;
		} &
	fi;
	cd ${scsi_host}
done;

while true; do sleep 300; done

echo -e \\n
