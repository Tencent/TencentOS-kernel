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

declare -r CURRENT_PATH=$PWD
declare -r SCSI_HOST="/sys/class/scsi_host"
declare -r SAS_DEVICE="/sys/class/sas_device"

function display()
{
	cd ${SAS_DEVICE}
	for i in end_device-$1:*; do
		echo -e \\n
		echo $i
		cd $i
		for f in *; do
			if [ -f $f -a -r $f ]; then
				echo -n -e \\t "$f:";
				cat $f ;
			fi;
		done;
		cd ..
	done;
	echo -e \\n
}

for HOST in $(ls ${SCSI_HOST});  do
	cd ${SCSI_HOST}/${HOST};
	if [ `cat proc_name` != "mpt3sas" ]; then
		continue;
	fi;
	host_number=${HOST//host/}
	display ${host_number}
	cd ${CURRENT_PATH}
done;
