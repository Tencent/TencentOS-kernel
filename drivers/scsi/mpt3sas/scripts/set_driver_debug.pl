# DISCLAIMER OF LIABILITY
# THIS IS SAMPLE SCRIPT. 
# NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
# HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

#!/usr/bin/perl 
use strict;
use warnings;

# generate the section by running the following (can be shorted more if somebody is bored):
# fgrep "#define MPT_DEBUG" mpt3sas_debug.h | sed 's/#define //;s/\s\s*/ /g;s/\(MPT_DEBUG.*\) \(0x.*\)/\t\t\2 => "\1",/'
# fgrep "#define MPT_DEBUG" mpt3sas_debug.h | sed 's/#define //;s/\s\s*/ /g;s/\(MPT_DEBUG_.*\) \(0x.*\)/\t{level=>"\2",name=>"\1"},/'
my $gen_debug = {
	mptsas => [
{mask=>"0x00000001",name=>"MPT_DEBUG"},
{mask=>"0x00000002",name=>"MPT_DEBUG_MSG_FRAME"},
{mask=>"0x00000004",name=>"MPT_DEBUG_SG"},
{mask=>"0x00000008",name=>"MPT_DEBUG_EVENTS"},
{mask=>"0x00000010",name=>"MPT_DEBUG_VERBOSE_EVENTS"},
{mask=>"0x00000020",name=>"MPT_DEBUG_INIT"},
{mask=>"0x00000040",name=>"MPT_DEBUG_EXIT"},
{mask=>"0x00000080",name=>"MPT_DEBUG_FAIL"},
{mask=>"0x00000100",name=>"MPT_DEBUG_TM"},
{mask=>"0x00000200",name=>"MPT_DEBUG_DV"},
{mask=>"0x00000400",name=>"MPT_DEBUG_REPLY"},
{mask=>"0x00000800",name=>"MPT_DEBUG_HANDSHAKE"},
{mask=>"0x00001000",name=>"MPT_DEBUG_CONFIG"},
{mask=>"0x00002000",name=>"MPT_DEBUG_DL"},
{mask=>"0x00008000",name=>"MPT_DEBUG_RESET"},
{mask=>"0x00010000",name=>"MPT_DEBUG_SCSI"},
{mask=>"0x00020000",name=>"MPT_DEBUG_IOCTL"},
{mask=>"0x00080000",name=>"MPT_DEBUG_FC"},
{mask=>"0x00100000",name=>"MPT_DEBUG_SAS"},
{mask=>"0x00200000",name=>"MPT_DEBUG_SAS_WIDE"},
{mask=>"0x00400000",name=>"MPT_DEBUG_36GB_MEM"},
		],
	mpt2sasbtm => [
{mask=>"0x00000001",name=>"MPT_DEBUG"},
{mask=>"0x00000002",name=>"MPT_DEBUG_MSG_FRAME"},
{mask=>"0x00000004",name=>"MPT_DEBUG_SG"},
{mask=>"0x00000008",name=>"MPT_DEBUG_EVENTS"},
{mask=>"0x00000010",name=>"MPT_DEBUG_EVENT_WORK_TASK"},
{mask=>"0x00000020",name=>"MPT_DEBUG_INIT"},
{mask=>"0x00000040",name=>"MPT_DEBUG_EXIT"},
{mask=>"0x00000080",name=>"MPT_DEBUG_FAIL"},
{mask=>"0x00000100",name=>"MPT_DEBUG_TM"},
{mask=>"0x00000200",name=>"MPT_DEBUG_REPLY"},
{mask=>"0x00000400",name=>"MPT_DEBUG_HANDSHAKE"},
{mask=>"0x00000800",name=>"MPT_DEBUG_CONFIG"},
{mask=>"0x00001000",name=>"MPT_DEBUG_DL"},
{mask=>"0x00002000",name=>"MPT_DEBUG_RESET"},
{mask=>"0x00004000",name=>"MPT_DEBUG_SCSI"},
{mask=>"0x00008000",name=>"MPT_DEBUG_IOCTL"},
{mask=>"0x00020000",name=>"MPT_DEBUG_SAS"},
{mask=>"0x00040000",name=>"MPT_DEBUG_HOST_MAPPING"},
{mask=>"0x00080000",name=>"MPT_DEBUG_TASK_SET_FULL"},
		],
	mpt2sas => [
{mask=>"0x00000001",name=>"MPT_DEBUG"},
{mask=>"0x00000002",name=>"MPT_DEBUG_MSG_FRAME"},
{mask=>"0x00000004",name=>"MPT_DEBUG_SG"},
{mask=>"0x00000008",name=>"MPT_DEBUG_EVENTS"},
{mask=>"0x00000010",name=>"MPT_DEBUG_EVENT_WORK_TASK"},
{mask=>"0x00000020",name=>"MPT_DEBUG_INIT"},
{mask=>"0x00000040",name=>"MPT_DEBUG_EXIT"},
{mask=>"0x00000080",name=>"MPT_DEBUG_FAIL"},
{mask=>"0x00000100",name=>"MPT_DEBUG_TM"},
{mask=>"0x00000200",name=>"MPT_DEBUG_REPLY"},
{mask=>"0x00000400",name=>"MPT_DEBUG_HANDSHAKE"},
{mask=>"0x00000800",name=>"MPT_DEBUG_CONFIG"},
{mask=>"0x00001000",name=>"MPT_DEBUG_DL"},
{mask=>"0x00002000",name=>"MPT_DEBUG_RESET"},
{mask=>"0x00004000",name=>"MPT_DEBUG_SCSI"},
{mask=>"0x00008000",name=>"MPT_DEBUG_IOCTL"},
{mask=>"0x00020000",name=>"MPT_DEBUG_SAS"},
{mask=>"0x00040000",name=>"MPT_DEBUG_TRANSPORT"},
{mask=>"0x00080000",name=>"MPT_DEBUG_TASK_SET_FULL"},
{mask=>"0x00100000",name=>"MPT_DEBUG_TARGET_MODE"},
		],
	mpt3sas => [
{mask=>"0x00000001",name=>"MPT_DEBUG"},
{mask=>"0x00000002",name=>"MPT_DEBUG_MSG_FRAME"},
{mask=>"0x00000004",name=>"MPT_DEBUG_SG"},
{mask=>"0x00000008",name=>"MPT_DEBUG_EVENTS"},
{mask=>"0x00000010",name=>"MPT_DEBUG_EVENT_WORK_TASK"},
{mask=>"0x00000020",name=>"MPT_DEBUG_INIT"},
{mask=>"0x00000040",name=>"MPT_DEBUG_EXIT"},
{mask=>"0x00000080",name=>"MPT_DEBUG_FAIL"},
{mask=>"0x00000100",name=>"MPT_DEBUG_TM"},
{mask=>"0x00000200",name=>"MPT_DEBUG_REPLY"},
{mask=>"0x00000400",name=>"MPT_DEBUG_HANDSHAKE"},
{mask=>"0x00000800",name=>"MPT_DEBUG_CONFIG"},
{mask=>"0x00001000",name=>"MPT_DEBUG_DL"},
{mask=>"0x00002000",name=>"MPT_DEBUG_RESET"},
{mask=>"0x00004000",name=>"MPT_DEBUG_SCSI"},
{mask=>"0x00008000",name=>"MPT_DEBUG_IOCTL"},
{mask=>"0x00020000",name=>"MPT_DEBUG_SAS"},
{mask=>"0x00040000",name=>"MPT_DEBUG_TRANSPORT"},
{mask=>"0x00080000",name=>"MPT_DEBUG_TASK_SET_FULL"},
{mask=>"0x00100000",name=>"MPT_DEBUG_TARGET_MODE"},
		]
	};

while(do_card_menu() >= 0) {
}

sub do_card_menu
{
	my @hosts = glob("/sys/class/scsi_host/*");
	my @lsi_hosts;
	my $next;
	my $host;
	my $index;
	my $option = -1;
	my $tmpstr;
	my $selected_card = -1;

	printf("\nCard Selection\n");
	foreach $host (@hosts) {
		open( FILE, "< ${host}/proc_name") or die "can't open $host/proc_name : $!";
		$tmpstr = <FILE>;
		if ($tmpstr =~ /mpi*/){
			push(@lsi_hosts, substr($host, 25));
		}
		close(FILE);
	}

	while ($option < 0) {
		$index = 1;
		foreach $host (@lsi_hosts) {
			open(FILE, "< /sys/class/scsi_host/host${host}/board_name") or die "can't open $host/board_name : $!";
			$tmpstr = <FILE>;
			chomp($tmpstr);
			close(FILE);
			printf("%2d.  Adapter %2d - %s\n", $index, $host, $tmpstr);
			$index++;
		}
		if (-e "/sys/module/mptbase/parameters/mpt_debug_level") {
			printf("%2d.  Global mptlinux\n", 97);
		}
		if (-e "/sys/module/mpt2sas/parameters/logging_level") {
			printf("%2d.  Global mpt3sas\n", 98);
		}
		if (-e "/sys/module/mpt2sasbtm/parameters/logging_level") {
			printf("%2d.  Global mpt3sas\n", 99);
		}
		if (-e "/sys/module/mpt3sas/parameters/logging_level") {
			printf("%2d.  Global mpt3sas\n", 100);
		}
		printf("\nSelect a card or global driver [0 to exit] > ", $index);
		$option = <STDIN>;
		if (!($option =~ /^\n/) && $option == 0) {
			$selected_card = -1;
		} elsif ($option =~ /^\n/ || $option < 0 || ($option >= $index && $option < 97) || $option > 100) {
			$option = -1;
		} elsif ($option == 97) {
			do_global_driver("/sys/module/mptbase/parameters/mpt_debug_level");
			$selected_card = $option;
		} elsif ($option == 98) {
			do_global_driver("/sys/module/mpt2sas/parameters/logging_level");
			$selected_card = $option;
		} elsif ($option == 99) {
			do_global_driver("/sys/module/mpt2sasbtm/parameters/logging_level");
			$selected_card = $option;
		} elsif ($option == 100) {
			do_global_driver("/sys/module/mpt3sas/parameters/logging_level");
			$selected_card = $option;
		} else {
			$selected_card = $lsi_hosts[$option - 1];
			do_debug_menu($selected_card);
		}
	}
	return $selected_card;
}


sub do_debug_menu
{
	my $card = $_[0];
	my $option = -1;
	my $curlevel = get_current_debug_level($card);
	my $gen = get_gen($card);
	my $index;

	while ($option != 0) {
		printf("\nDebug Options for Adapter $card \n");
		$index = 1;
		foreach my $hash (@{$gen_debug->{$gen}}) {
			printf("%2d.  %-28s %s: %s\n", $index, $hash->{name}, $hash->{mask}, ($curlevel & hex($hash->{mask})) ? "ON" : "OFF");
			$index++;
		}
		printf("\n99.  Set Hex Debug Value          0x%08X\n", $curlevel);
		printf("\nToggle debug option [1-23, 100 or 0 to exit] > ");

		$option = <STDIN>;
		if ($option =~ /^\n/ || $option < 0 || ($option > 23 && $option < 100) || $option > 100) {
			$option = -1;
		} elsif ($option == 100) {
			my $tmp_level;
			printf("\nEnter desired hex debug value (ie 0x811) > ");
			$tmp_level = <STDIN>;
			chomp($tmp_level);
			if ($tmp_level =~ /0x/) {
				$curlevel = hex($tmp_level);
			} else {
				$curlevel = $tmp_level;
			}
        		set_debug_level($card, $curlevel);
		} else {
			$curlevel = $curlevel ^ (1 << ($option - 1));
			set_debug_level($card, $curlevel);
		}
	}
}

sub get_gen
{
	my $card = $_[0];
	my $file = "/sys/class/scsi_host/host${card}/proc_name";
	my $gen;

	open(FILE, "< $file") or die "can't open $file : $!";
	$gen = <FILE>;
	chomp($gen);
	return $gen;
}

sub set_debug_level
{
	my $card = $_[0];
	my $level = $_[1];
	my $file = "/sys/class/scsi_host/host${card}";
	my $input;

	if (-e "$file/debug_level") {
		$file .= "/debug_level";
	} elsif(-e "$file/logging_level") {
		$file .= "/logging_level";
	} else {
		print("Unable to find logging or debug file\n");
		exit(1);
	}

	open(FILE, "> $file") or die "can't open $file : $!";

	printf(FILE "%X\n", $level);

	close(FILE);
}

sub get_current_debug_level
{
	my $card = $_[0];
	my $file = "/sys/class/scsi_host/host${card}";
	my $current_level = 0;
	my $input;

	if (-e "$file/debug_level") {
		$file .= "/debug_level";
	} elsif(-e "$file/logging_level") {
		$file .= "/logging_level";
	} else {
		print("Unable to find logging or debug file\n");
		exit(1);
	}

	open(FILE, "< $file") or die "can't open $file : $!";

	$input = <FILE>;
	$input = substr($input, 0, 8);
	$current_level = hex($input);

	close(FILE);

	return $current_level;
}


sub do_global_driver
{
        my $file = shift;
	my $input;
	my $current_level;
	my $tmp_level;

	open(FILE, "< $file") or die "can't open $file : $!";

        $input = <FILE>;
	chomp($input);
	$current_level = $input;

	close(FILE);

	printf("\nCurrent Debug Level         0x%08X\n", $current_level);
	printf("\nEnter desired hex debug value (ie 0x811) > ");
	$tmp_level = <STDIN>;
	chomp($tmp_level);
	if ($tmp_level =~ /0x/) {
		$current_level = hex($tmp_level);
	} else {
		$current_level = $tmp_level;
	}

	open(FILE, "> $file") or die "can't open $file : $!";
	printf(FILE "%s\n", $tmp_level);
	close(FILE);

	return 0
}


