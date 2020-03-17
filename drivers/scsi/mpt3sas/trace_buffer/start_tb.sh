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
#
#

#debugging parameters
# shopt -o -s xtrace

# global parmeters
declare -r SCRIPT=${0##*/}
declare -r SCSI_HOST="/sys/class/scsi_host"
declare -ir PAGE_SIZE=4095
declare -r CURRENT_PATH=$PWD
declare -r FILE_NAME="trace_buffer"
declare -ir HOST_NUMBER=$1
declare -i IOC_RESET_COUNT=0

# release_buffers: turn OFF trace buffers, and flush to host memory
function release_buffers()
{
	declare -r CURRENT_STATUS=$(cat host_trace_buffer_enable)

	if [ -z ${CURRENT_STATUS} ] ; then
		return 0;
	fi;
	if [ "${CURRENT_STATUS}" != "release" ] ; then
		printf "\n%s: releasing trace buffers for host%d\n" ${SCRIPT} \
		    ${HOST_NUMBER}
		echo release > host_trace_buffer_enable;
	fi;

	return 0;
}

# post_buffers: turn ON trace buffers
function post_buffers()
{
	declare -r CURRENT_STATUS=$(cat host_trace_buffer_enable)

	if [ -z ${CURRENT_STATUS} ] ; then
		return 0;
	fi;
	if [ "${CURRENT_STATUS}" != "post" ] ; then
		printf "\n%s: posting trace buffers for host%d\n" ${SCRIPT} \
		    ${HOST_NUMBER}
		echo post > host_trace_buffer_enable;
	fi;

	return 0;
}

# dump_trace_buffer: dump trace buffer to file (naming appended with timestamp)
function dump_trace_buffer()
{
	declare DATE=$(date +%Y:%m:%d_%H:%M:%S)
	declare -i TOTAL_SIZE=$(cat host_trace_buffer_size);
	declare -i OFFSET=0

	printf "\n"
	release_buffers
	UNIQUE_FILE_NAME="${FILE_NAME}_host${HOST_NUMBER}_${DATE}"
	printf "%s: reading into filename = %s " ${SCRIPT} ${UNIQUE_FILE_NAME}
	while [ ${OFFSET} -lt ${TOTAL_SIZE} ] ; do
		echo ${OFFSET} > host_trace_buffer;
		cat host_trace_buffer >> ${CURRENT_PATH}/${UNIQUE_FILE_NAME}
		OFFSET=OFFSET+PAGE_SIZE;
		printf "."
	done;
	printf "\ndone\n"

	return 0;
}

# check_for_diag_reset: check whether sysfs attribute "ioc_reset_count" has incremented
function check_for_diag_reset()
{
	declare -i ioc_reset_count=$(cat ioc_reset_count)

	if [ ${ioc_reset_count} -gt ${IOC_RESET_COUNT} ] ; then
		IOC_RESET_COUNT=ioc_reset_count
		return 1
	fi;

	return 0
}

# killing_script: called when program is getting killed
function killing_script()
{
	dump_trace_buffer
	printf "\n%s: script is stopping for host%d!!!\n" \
	     ${SCRIPT} ${HOST_NUMBER}
	exit 0;
}

# polling_loop: this is where we will be most the time
function polling_loop()
{
	trap "killing_script" SIGHUP SIGINT SIGTERM

	# posting buffers if they have not been already
	post_buffers

	printf "%s: starting polling for reset on host%d" ${SCRIPT} \
	    ${HOST_NUMBER}
	while true ; do
		if test ! -d ${SCSI_HOST}/host${HOST_NUMBER} ; then
			printf "\n%s: stopping script for host%d!!!\n" \
			     ${SCRIPT} ${HOST_NUMBER}
			exit 0;
		fi;
		printf "."
		check_for_diag_reset
		if [ $? -ne 0 ] ; then
			printf "\n"
			printf "%s: reset has occurred on host%d!!!\n" \
			    ${SCRIPT} ${HOST_NUMBER}
			dump_trace_buffer
			post_buffers
			printf "%s: resuming polling for host%d" ${SCRIPT} \
			    ${HOST_NUMBER}
		fi;
		sleep 1
	done
	printf "\n"
}

# main routine

# useage banner
clear
if [ $# != 1 ]; then
	printf "useage: %s <host_number>\n" ${SCRIPT}
	exit 1
fi;

# sanity check to make sure folder exists
if test ! -d ${SCSI_HOST}/host${HOST_NUMBER} ; then
	printf "%s: FAILURE: host%d controller doesn't exist\n" ${SCRIPT} \
	    ${HOST_NUMBER}
	exit 1
fi;

cd ${SCSI_HOST}/host${HOST_NUMBER};

# sanity check to make proc_name is for mpt3sas
if [ `cat proc_name` != "mpt3sas" ]; then
	printf "%s: FAILURE: host%d controller is not mpt3sas\n" ${SCRIPT} \
	    ${HOST_NUMBER}
	exit 1
fi;

# delete existing trace buffers
rm -fr ${CURRENT_PATH}/${FILE_NAME}_*

# initialze the diag reset count
IOC_RESET_COUNT=$(cat ioc_reset_count)

# call the routine that will be polling for diag resets
printf "\n"
polling_loop

cd ${CURRENT_PATH}
exit 0
