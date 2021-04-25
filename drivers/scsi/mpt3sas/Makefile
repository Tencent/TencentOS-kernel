# mpt3sas makefile
# uncomment when enabling firmware UART 'iop reset -1' option
#EXTRA_CFLAGS += -DDISABLE_RESET_SUPPORT
EXTRA_CFLAGS := -DCPQ_CIM

# comment out this line so driver can compile for kernels not having mpt3sas
obj-$(CONFIG_SCSI_MPT3SAS) += mpt3sas.o
mpt3sas-y +=  mpt3sas_base.o     \
		mpt3sas_config.o \
		mpt3sas_warpdrive.o \
		mpt3sas_scsih.o      \
		mpt3sas_transport.o     \
		mpt3sas_ctl.o	\
		mpt3sas_trigger_diag.o \
		mpt3sas_debugfs.o \

