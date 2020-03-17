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

#set -x
scsi_host="/sys/class/scsi_host"
page_size=4095
offset=0;
total_size=0;
CWD=`pwd`;
file_name='trace_buffer'

cd ${scsi_host};
subfolders=`ls -1`;
for i in ${subfolders};  do
	cd ${i};
	if [ `cat proc_name` != "mpt3sas" ]; then
		cd -;
		continue;
	fi;
	rm -f ${CWD}/${i}.${file_name};
	echo release > host_trace_buffer_enable;
	total_size=`cat host_trace_buffer_size`;
	while [ "${offset}" -lt "${total_size}" ] ; do
		echo $offset > host_trace_buffer;
		cat host_trace_buffer >> ${CWD}/${i}.${file_name};
		let offset=${offset}+${page_size};
	done;
	cd -;
done;
