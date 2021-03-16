#define	COMLEN	16

struct taskcount {
	unsigned long long	tcpsndpacks;
	unsigned long long	tcpsndbytes;
	unsigned long long	tcprcvpacks;
	unsigned long long	tcprcvbytes;

	unsigned long long	udpsndpacks;
	unsigned long long	udpsndbytes;
	unsigned long long	udprcvpacks;
	unsigned long long	udprcvbytes;

	/* space for future extensions */
};

struct netpertask {
	pid_t			id;	// tgid or tid (depending on command)
	unsigned long		btime;
	char			command[COMLEN];

	struct taskcount	tc;
};


/*
** getsocktop commands
*/
#define NETATOP_BASE_CTL   	15661

// just probe if the netatop module is active
#define NETATOP_PROBE		(NETATOP_BASE_CTL)

// force garbage collection to make finished processes available
#define NETATOP_FORCE_GC	(NETATOP_BASE_CTL+1)

// wait until all finished processes are read (blocks until done)
#define NETATOP_EMPTY_EXIT	(NETATOP_BASE_CTL+2)

// get info for finished process (blocks until available)
#define NETATOP_GETCNT_EXIT	(NETATOP_BASE_CTL+3)

// get counters for thread group (i.e. process):  input is 'id' (pid)
#define NETATOP_GETCNT_TGID	(NETATOP_BASE_CTL+4)

// get counters for thread:  input is 'id' (tid)
#define NETATOP_GETCNT_PID 	(NETATOP_BASE_CTL+5)
