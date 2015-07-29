#ifndef __SW3800_AUTH_H__
#define __SW3800_AUTH_H__

#define SW3800_COVER_TYPE_ERR	0
#define SW3800_COVER_AUTH_ERR	0
#define SW3800_COVER_SHA_OK		1
#define SW3800_FAIL				0
#define SW3800_SUCCESS			1

#define RESPONSE_AUTH		20
#define TOTAL_WORD			18
#define WORD_LENGTH			17

typedef enum
{
	TRANSACTION_SDA = 0x01,
	TRANSACTION_WRA = 0x02,
	TRANSACTION_WD  = 0x03,
	TRANSACTION_RBE = 0x04,
	TRANSACTION_RBL = 0x04,
	TRANSACTION_RRA = 0x05,
	TRANSACTION_LAST,
} TRANSACTION_ENUM;

typedef struct {
	u8 challengeData[8];
} AuthenticationInfo;

extern void SW3800_Authorize(AuthenticationInfo *ci);
extern int SW3800_Authentication(u8 *readData, u8 *matchData, int *data);
extern int SW3800_GetCoverType(u8 readByte, int *data);
extern void SW3800_ChallengeDataInit(AuthenticationInfo *ci);
extern int SW3800_Encode(u8 transaction, u8 addressData, u8 *destData);
extern int SW3800_Decode(int *srcData, u8 *matchData, u8 *destData, int destLength);
extern int SW3800_GetLastCoverType(void);
extern int SW3800_SetLastCoverType(unsigned long int in);

#endif /* __SW3800_AUTH_H__ */
