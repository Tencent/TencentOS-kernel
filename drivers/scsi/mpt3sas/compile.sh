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
# compile.sh: a script compiling the driver source.
#

# enable scripting debug
# shopt -o -s xtrace

blacklist_file="/etc/modprobe.d/blacklist"
blacklist_enclosure()
{
	if [ -f /etc/SuSE-release ] && [ `grep -c "VERSION = 11" /etc/SuSE-release` -gt 0 ]; then
		if [ -f ${file} ]; then
			if [ `grep -c "blacklist enclosure" ${blacklist_file}` -eq 0 ]; then
				echo "blacklist enclosure" >> ${blacklist_file};
			fi;
			if [ `grep -c "blacklist ses" ${blacklist_file}` -eq 0 ]; then
				echo "blacklist ses" >> ${blacklist_file};
			fi;
		fi;
	fi;
}

fw_mpi2="$PWD/../../fwapi/mpi2"
if [ ! -d mpi ] && [ ! -L mpi ] && [ -d $fw_mpi2 ]; then
        ln -s ../../fwapi/mpi2 mpi
elif [ ! -d mpi ] && [ ! -d $fw_mpi2 ]; then
        echo "MPI header files from $fw_mpi2 are missing"
        exit
fi

# Sparse - a semantic parser, provides a set of annotations designed to convey
# semantic information about types, such as what address space pointers point
# to, or what locks a function acquires or releases. You can obtain the latest
# sparse code by typing the following command:
# git clone git://git.kernel.org/pub/scm/devel/sparse/sparse.git

# Set SPARSE to non-zero value to enable sparse checking
#	kernel="2.6.18-8.el5"
#	kernel="2.6.18-53.el5"
#	kernel="2.6.27.15-2-default"
	kernel=`uname -r`
SPARSE=0

blacklist_enclosure
rm -fr output.log
./clean.sh
ctags -R *

if [ ${SPARSE} == 0 ] ; then
	make -j4 CONFIG_DEBUG_INFO=1 -C /lib/modules/${kernel}/build \
		M=$PWD 2>&1 | tee output.log
else
# This turns on ENDIAN checking:
	make C=2 CF="-D__CHECK_ENDIAN__" CONFIG_DEBUG_INFO=1 -C \
	    /lib/modules/${kernel}/build M=$PWD 2>&1 | tee output.log
fi;

exit 0

