#ifndef __SW3800_H__
#define __SW3800_H__

#define BIF_DEV_NAME "BIF"

struct BIF_PlatformData {
	int bifDetectPin;
	int bifDataPin;
	int bifPullupPin;
	unsigned int detectIRQ;
	unsigned long detectIRQFlags;
};

#define ONE_TAU				20
#define THREE_TAU			(60 + 10)
#define STOP_TAU			(100 + 20)
#define DATA_BIT			360
#define ERROR_CNT			10000

#define COVER_OPEN			0
#define COVER_CLOSE			1

extern void pm8xxx_slide_enable(void);
extern void pm8xxx_slide_disable(void);
extern void pm8xxx_slide_boot_func(void);
extern bool slide_boot_mode(void);

#endif /* __SW3800_H__ */