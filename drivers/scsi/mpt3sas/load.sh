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
# load.sh : a helper script for loading the drivers
#

# toggling bits for enabling scsi-mid layer logging

# enable sense data
#sysctl -w dev.scsi.logging_level=0x1000

# enable scanning debuging
sysctl -w dev.scsi.logging_level=0x1C0

# enable sense data and scanning
#sysctl -w dev.scsi.logging_level=0x11C0

# loading scsi mid layer and transports
modprobe sg
modprobe sd_mod
modprobe scsi_transport_sas
modprobe raid_class

# loading the driver
insmod mpt3sas.ko
#insmod mpt3sas.ko logging_level=0x310
#insmod mpt3sas.ko max_queue_depth=128 max_sgl_entries=32 logging_level=0x620
#insmod mpt3sas.ko max_queue_depth=500 max_sgl_entries=32 logging_level=0x620

# fw events + tm + reset + reply
#insmod mpt3sas.ko logging_level=0x2308

# work task + reply
#insmod mpt3sas.ko logging_level=0x210
#insmod mpt3sas.ko logging_level=0x200

# fw events + reply
#insmod mpt3sas.ko logging_level=0x208

# fw events + work task + reply
#insmod mpt3sas.ko logging_level=0x218

# handshake + init
#insmod mpt3sas.ko logging_level=0x420

# everything
#insmod mpt3sas.ko logging_level=0xFFFFFFF

# reset
#insmod mpt3sas.ko logging_level=0x2000

#ioctls
#insmod mpt3sas.ko logging_level=0x8000

#init
#insmod mpt3sas.ko logging_level=0x20

#config
#insmod mpt3sas.ko logging_level=0x800

exit 0
