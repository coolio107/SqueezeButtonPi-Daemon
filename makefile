sbpd: control.c control.h discovery.c discovery.h GPIO.c GPIO.h sbpd.c sbpd.h servercomm.c servercomm.h
	gcc -lwiringPi -lcurl -o sbpd control.c discovery.c GPIO.c sbpd.c servercomm.c