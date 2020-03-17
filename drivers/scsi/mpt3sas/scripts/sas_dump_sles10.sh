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
current_path=`pwd`;
count=0;
none_count=0;
smp_count=0;
dev_count=0;
phy_count=0;
stp_count=0;
ssp_count=0;
sata_count=0;

clear;
echo
echo --------------------------------- Phy info ------------------------------------
echo -e \\t\\tindex\\ttype\\tphy\\tport\\tSAS_ADDR\\t\\trate
cd /sys/class/sas_phy
for i in $( ls -1);  do
	cd ${i};
	sas_address=`cat sas_address`
	phy=`cat phy_identifier`
	port=`cat port_identifier`
	rate=`cat negotiated_linkrate`
	sas_type=`cat target_port_protocols`
	echo -e ${i}\\t${phy_count}\\t${sas_type}\\t${phy}\\t${port}\\t${sas_address}\\t${rate}
	cd ..
	let phy_count=${phy_count}+1
done;



#######################################################
# none
echo
echo ---------------------------- Initiator info ------------------------------------
echo -e \\t\\tindex\\ttype\\tphy\\tSAS_ADDR
count=0;
#cd /sys/class/sas_rphy;
cd /sys/class/sas_device;
for i in $( ls -1);  do
	cd ${i};
	if [ `cat target_port_protocols` == "none" ]; then
		let none_count=${none_count}+1
		sas_address=`cat sas_address`
		phy=`cat phy_identifier`
		bay_id=`cat bay_identifier`;
		enclosure_id=`cat enclosure_identifier`;
		echo ${i}: index=${count} phy_id=${phy} SAS=${sas_address}
		echo -e \\t enclosure slot=${bay_id} WWN=${enclosure_id}
	fi;
	cd ..
	let count=${count}+1
done;

# smp
echo
echo ---------------------------- Expander info ------------------------------------
echo -e \\t\\tindex\\ttype\\tphy\\tSAS_ADDR
count=0;
#cd /sys/class/sas_rphy;
cd /sys/class/sas_device;
for i in $( ls -1);  do
	cd ${i};
	if [ `cat target_port_protocols` == "smp" ]; then
		let smp_count=${smp_count}+1
		sas_address=`cat sas_address`
		sas_type=`cat target_port_protocols`
		phy=`cat phy_identifier`
		echo -e ${i}\\t${count}\\t${sas_type}\\t${phy}\\t${sas_address}
	fi;
	cd ..
	let count=${count}+1
done;

stp_count=0;
ssp_count=0;
sata_count=0;

# devices
echo
echo ---------------------------- Target info ------------------------------------
echo -e \\t\\t\\tindex\\ttype\\tphy\\tslot\\tSAS_ADDR\\t\\tEnclosure ID
count=0;
#cd /sys/class/sas_rphy;
cd /sys/class/sas_device;
for i in $( ls -1);  do
	cd ${i};
	if [ `cat target_port_protocols` == "none" ]; then
		cd ..
		let count=${count}+1
		continue;
	fi;
	if [ `cat target_port_protocols` == "smp" ]; then
		cd ..
		let count=${count}+1
		continue;
	fi;
	if [ `cat target_port_protocols` == "stp" ]; then
		let stp_count=${stp_count}+1
	fi;
	if [ `cat target_port_protocols` == "ssp" ]; then
		let ssp_count=${ssp_count}+1
	fi;
	if [ `cat target_port_protocols` == "sata" ]; then
		let sata_count=${sata_count}+1
	fi;
	let dev_count=${dev_count}+1
	bay_id=`cat bay_identifier`;
	enclosure_id=`cat enclosure_identifier`;
	sas_address=`cat sas_address`
	sas_type=`cat target_port_protocols`
	phy=`cat phy_identifier`
	echo -e ${i}\\t${count}\\t${sas_type}\\t${phy}\\t${bay_id}\\t${sas_address}\\t${enclosure_id}
	cd ..
	let count=${count}+1
done;


echo
echo -------- Statistics ------------------
echo phy count = ${phy_count}
echo rphy count = ${count}
echo none = ${none_count}
echo smp = ${smp_count}
echo ssp = ${ssp_count}
echo stp = ${stp_count}
echo sata = ${sata_count}
echo total devices = ${dev_count}
cd ${current_path}


