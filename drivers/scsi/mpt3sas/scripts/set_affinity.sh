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

if [ -f /etc/SuSE-release ] && [ -f /etc/init.d/irq_balancer ] ; then
	/etc/init.d/irq_balancer stop
elif [ -f /etc/redhat-release ] && [ -f /etc/init.d/irqbalance ]; then
	/etc/init.d/irqbalance stop
else
	exit 1;
fi;

# this is count of the number cpu's we have set the affinity for
cpu_affinity_count=0

# This is total number of cores
cpu_count=0
for i in /sys/devices/system/cpu/cpu[0-9]* ; do
	let cpu_count=${cpu_count}+1
done

# line below for is for debuging
#cpu_count=24

scsi_host="/sys/class/scsi_host"
cd ${scsi_host}
subfolders=`ls -1`
for i in ${subfolders};  do
	cd ${scsi_host}/${i};
	if [ `cat proc_name` != "mpt3sas" ]; then
		continue;
	fi;
	if [ ! -f reply_queue_count ]; then
		echo "reply_queue_count sysfs attribute doesn't exist"
		continue;
	fi;
	if [ `cat reply_queue_count` -lt 2 ]; then
		continue;
	fi;
	ioc=`cat unique_id`
	index=0
	affinity=1
	cpu_affinity_count=0
	msix_count=`cat /proc/interrupts | grep -c mpt3sas${ioc}-msix`
	if [ ${cpu_count} -gt ${msix_count} ]; then
		let grouping=${cpu_count}/${msix_count}
		let grouping_mod=${cpu_count}%${msix_count}
# line below for is for debuging
#		echo grouping = ${grouping}, grouping_mod = ${grouping_mod}
		if [ ${grouping} -lt 2 ]; then
			cpu_grouping=2;
		elif [ ${grouping} -eq 2 ] && [ ${grouping_mod} -eq 0 ]; then
			cpu_grouping=2;
		elif [ ${grouping} -lt 4 ]; then
			cpu_grouping=4;
		elif [ ${grouping} -eq 4 ] && [ ${grouping_mod} -eq 0 ]; then
			cpu_grouping=4;
		elif [ ${grouping} -lt 8 ]; then
			cpu_grouping=8;
		elif [ ${grouping} -eq 8 ] && [ ${grouping_mod} -eq 0 ]; then
			cpu_grouping=8;
		else
			cpu_grouping=16;
		fi;
	else
		cpu_grouping=0;
	fi;
	echo -e "\n"
	echo ioc number = ${ioc}
	echo number of core processors = ${cpu_count}
	echo msix vector count = ${msix_count}
	if [ ${cpu_grouping} -eq 0 ] ; then
		echo number of cores per msix vector = 1
	else
		echo number of cores per msix vector = ${cpu_grouping}
	fi;
	echo -e "\n"
	while [ ${msix_count} -gt ${index} ] && \
		( [ ${cpu_affinity_count} -lt ${cpu_count} ] || \
		  [ ${cpu_affinity_count} -eq ${cpu_count} ] ); do
		b=0
		a=`cat /proc/interrupts | grep -w mpt3sas${ioc}-msix${index} | cut -d : -f 1`
		irq_number=`basename ${a}`
		cd /proc/irq/${irq_number}
		if [ ${cpu_grouping} -ne 0 ]; then
			loop=1
			calculate_affinity=${affinity}
			let cpu_affinity_count=${cpu_affinity_count}+1
			while [ ${loop} -lt ${cpu_grouping} ]; do
				let affinity=${affinity}*2
				let calculate_affinity=${calculate_affinity}+${affinity}
				let loop=${loop}+1
				let cpu_affinity_count=${cpu_affinity_count}+1
			done;
			b=`printf "%x" ${calculate_affinity}`
		else
			let cpu_affinity_count=${cpu_affinity_count}+1
			b=`printf "%x" ${affinity}`
		fi;
# line below for is for debuging
#		echo cpu_affinity_count = ${cpu_affinity_count}
		if [ ${cpu_affinity_count} -lt ${cpu_count} ] || \
		   [ ${cpu_affinity_count} -eq ${cpu_count} ]; then
			let length=${#b}
			let number_of_words=${length}/8
			let extra_word=${length}%8
			let i=0
			if [ ${extra_word} -gt 0 ]; then
				echo -n "${b:0:${extra_word}}" > /tmp/smp_affinity-${index}
				if [ ${number_of_words} -gt 0 ]; then
					echo -n "," >> /tmp/smp_affinity-${index}
				fi
			fi
			while [ $i -lt ${number_of_words} ]; do
				echo -n "${b:${i}*8+${extra_word}:8}" >> /tmp/smp_affinity-${index}
				let i=${i}+1
				if [ $i -lt ${number_of_words} ]; then
					 echo -n "," >> /tmp/smp_affinity-${index}
				fi
			done
			cat /tmp/smp_affinity-${index} > smp_affinity
			rm -f /tmp/smp_affinity-${index}
# line below for is for debuging	
#			echo -e "\tmsix index = ${index}, irq number =  ${irq_number}, cpu affinity mask = ${b}"
			echo -e "\tmsix index = ${index}, irq number =  ${irq_number}, cpu affinity mask = `cat smp_affinity`"
			let index=${index}+1
			let affinity=${affinity}*2
		fi;
	done
	echo -e "\nWe have set affinity for ${index} msix vectors and ${cpu_count} core processors\n"
done
