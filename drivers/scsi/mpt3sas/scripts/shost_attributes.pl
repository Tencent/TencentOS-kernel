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

#main
my($path) = "/sys/class/scsi_host";
my(@line);
my($shost);
my($proc_name);

chdir($path);
@line = `ls -1`;

foreach (@line) {
	chomp;
	$shost = join("/", $path, $_);
	$proc_name = `cat $shost/proc_name`;
	chomp $proc_name;
	&print_shost($shost, $_) if ($proc_name eq 'mptspi');
	&print_shost($shost, $_) if ($proc_name eq 'mptsas');
	&print_shost($shost, $_) if ($proc_name eq 'mpt2sas');
	&print_shost($shost, $_) if ($proc_name eq 'mpt3sas');
}

# subroutine: print_shost
sub print_shost
{
	my($shost_path, $shost_name) = @_;
	my($attribute);
	my(@line);

	chdir($shost_path);
	print $shost_name, "\n";
	@line = `ls -1`;
	foreach $attribute (@line) {
		chomp $attribute;
		&print_attribute($attribute) if (-f $attribute);
	}
}

#subroutine: print_attribute
sub print_attribute
{
	my($attribute_name) = @_;
	my($contents);

	return if ($attribute_name eq 'scan');
	return if ($attribute_name eq 'uevent');

	$contents = `cat $attribute_name`;
	print "\t$attribute_name = $contents";
}

