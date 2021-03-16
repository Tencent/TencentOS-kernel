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

while true ; do
	cd /sys/class/scsi_device
	for i in `ls -1 .`; do
		iodone_cnt=`cat ${i}/device/iodone_cnt`
		iorequest_cnt=`cat ${i}/device/iorequest_cnt`
		state=`cat ${i}/device/state`
		let pending_count=${iorequest_cnt}-${iodone_cnt}
		echo ${i}: pending = ${pending_count}, state = ${state}
	done
	echo -e \\n
	cd /sys/class/scsi_host
	for i in `ls -1 .`; do
		host_busy=`cat ${i}/host_busy`
		echo ${i}: host_busy = ${host_busy}
	done
	sleep 1
	clear
done
