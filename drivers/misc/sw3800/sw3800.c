/*
 * Copyright LG Electronics (c) 2015
 * All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/switch.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/wakelock.h>
#include <mach/board_lge.h>
#include <mach/gpiomux.h>
#include <linux/io.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/buffer_head.h>

#include <linux/sw3800_auth.h>
#include <linux/sw3800.h>

int gLastCover = 0;
bool gIRQMask = 1;

unsigned long flags;

struct bif_sw3800 {
	struct switch_dev sdev;
	struct switch_dev last_sdev;
	struct delayed_work detect_work;
	struct device *dev;
	struct wake_lock wake_lock;
	const struct BIF_PlatformData *pdata;
	spinlock_t lock;
	int state;
	int detect;
};

static struct workqueue_struct *gBIFWorkQueue;
static struct bif_sw3800 *gSW3800;

int BIF_DATA;
int BIF_DATA_PULLUP;

inline void BIF_DataModeOutput(void) {
	gpio_tlmm_config(GPIO_CFG(BIF_DATA, 0, GPIO_CFG_OUTPUT,
	GPIO_CFG_NO_PULL,GPIO_CFG_10MA), GPIO_CFG_DISABLE);
}

inline void BIF_DataPullModeOutput(void) {
	gpio_tlmm_config(GPIO_CFG(BIF_DATA_PULLUP, 0, GPIO_CFG_OUTPUT,
	GPIO_CFG_NO_PULL,GPIO_CFG_10MA), GPIO_CFG_DISABLE);
}

inline void BIF_DataModeInput(void) {
	gpio_tlmm_config(GPIO_CFG(BIF_DATA, 0, GPIO_CFG_INPUT,
	GPIO_CFG_NO_PULL, GPIO_CFG_10MA), GPIO_CFG_ENABLE);
}

inline void BIF_DataOutputLow(void) {
	gpio_set_value(BIF_DATA, 0);
}

inline void BIF_DataOutputHigh(void) {
	gpio_set_value(BIF_DATA, 1);
}

inline void BIF_DataPullup(void) {
	gpio_set_value(BIF_DATA_PULLUP, 1);
}

inline void BIF_DataPulldown(void) {
	gpio_set_value(BIF_DATA_PULLUP, 0);
}

inline u8 BIF_DataInput(void) {
	return gpio_get_value(BIF_DATA);
}

void BIF_Reset(void)
{
	BIF_DataPullModeOutput();
	BIF_DataPullup();
	BIF_DataModeOutput();
	BIF_DataOutputLow();
	mdelay(20);
	BIF_DataOutputHigh();
	mdelay(10);
}

void BIF_Suspend(void)
{
	BIF_DataModeOutput();
	BIF_DataOutputLow();
	BIF_DataPulldown();
	mdelay(20);
}

void BIF_WriteCommand(u8 transaction, u8 address)
{
	u8 MIPI_bif_Packet[WORD_LENGTH]={0x00,};
	int GPIO_flag = 0, i;

	SW3800_Encode(transaction, address, MIPI_bif_Packet);

	// Transmmit Data Words
	spin_lock_irqsave(&gSW3800->lock, flags);
	BIF_DataModeOutput();
	BIF_DataOutputHigh();
	udelay(STOP_TAU);
	GPIO_flag = 0;
	for(i = 0 ; i < WORD_LENGTH; i++) {
		if(GPIO_flag) BIF_DataOutputHigh();
		else BIF_DataOutputLow();
		if(MIPI_bif_Packet[i]) udelay(THREE_TAU);
		else udelay (ONE_TAU);
		GPIO_flag = !GPIO_flag;
	}
	BIF_DataOutputHigh();	// BCL Idle
	spin_unlock_irqrestore(&gSW3800->lock, flags);
}

int gRawData[360];

int BIF_ReadData(u8 *destData, int destLength, u8 *matchData)
{
	int logicDuration = 0;
	int bifData, lastBifData;
	int readCount = 0;

	spin_lock_irqsave(&gSW3800->lock, flags);
	udelay(ONE_TAU);
	BIF_DataModeInput();
	logicDuration = 0;
	readCount = 0;
	lastBifData = BIF_DataInput();
	while((readCount <= DATA_BIT) && (logicDuration < ERROR_CNT)) {
		logicDuration ++;
		if((bifData = BIF_DataInput()) != lastBifData) {
			gRawData[readCount++] = logicDuration;
			logicDuration = 0;
			lastBifData = bifData;
		}
	}
	BIF_DataModeOutput();
	BIF_DataOutputHigh();
	spin_unlock_irqrestore(&gSW3800->lock, flags);

	SW3800_Decode(gRawData, matchData, destData, destLength);
	return readCount;
}

int BIF_ReadByte(void)
{
	u8 readByte = 0;
	int logicDuration, lastBifData, bifData, i;

	//udelay(STOP_TAU);
	BIF_DataModeInput();

	i = 0;
	logicDuration = 0;
	spin_lock_irqsave(&gSW3800->lock, flags);
	lastBifData = BIF_DataInput();
	while((i < TOTAL_WORD) && (logicDuration < ERROR_CNT)) {
		logicDuration ++;
		if((bifData = BIF_DataInput()) != lastBifData) {
			gRawData[i++] = logicDuration;
			logicDuration = 0;
			lastBifData = bifData;
		}
	}
	BIF_DataModeOutput();
	BIF_DataOutputHigh();
	spin_unlock_irqrestore(&gSW3800->lock, flags);

	SW3800_Decode(gRawData, &readByte, &readByte, 1);

	return readByte;
}

int BIF_GetCoverType(void)
{
	int retCoverType;

	BIF_WriteCommand(TRANSACTION_SDA, 0x01);
	BIF_WriteCommand(TRANSACTION_RRA, 0x04);
	retCoverType = SW3800_GetCoverType(BIF_ReadByte(), gRawData);
	if(retCoverType == SW3800_COVER_TYPE_ERR) BIF_Reset();

	printk(KERN_INFO "[BIF] BIF_GetCoverType = %d\n", retCoverType);
	return retCoverType;
}

static int BIF_SetCoverType(int coverStatus, int coverType)
{
	printk(KERN_INFO "[BIF] BIF_SetCoverType: coverStatus = %d, coverType = %d \n",coverStatus, coverType);
	gLastCover = SW3800_GetLastCoverType();
	if(coverStatus == COVER_CLOSE) {
		if(coverType != SW3800_COVER_TYPE_ERR) {
			if(gLastCover != coverType) {
				SW3800_SetLastCoverType(coverType);
				gLastCover = coverType;
			}
		}else{
			coverType = gLastCover;
			printk(KERN_INFO "[BIF] BIF_SetCoverType: changed coverType = %d \n", coverType);
		}
	}
	wake_lock_timeout(&gSW3800->wake_lock, msecs_to_jiffies(3000));
	switch_set_state(&gSW3800->sdev, coverType);
	switch_set_state(&gSW3800->last_sdev, gLastCover);
	return coverType;
}

int BIF_Authentication(void)
{
	int i = 0;
	u8 result = 0, retResult;
	int coverTypeCheck = 0, coverTypeCheckLast = 0;
	int coverTypeMatchCount = 0;
	u8 readData[RESPONSE_AUTH];
	u8 matchData[RESPONSE_AUTH];
	AuthenticationInfo authenticationInfo;

	SW3800_ChallengeDataInit(&authenticationInfo);

	BIF_Reset();
	retResult = SW3800_COVER_TYPE_ERR;
	coverTypeMatchCount = 0;
	coverTypeCheckLast = BIF_GetCoverType();
	for(i = 0; i < 10; i++) {
		coverTypeCheck = BIF_GetCoverType();
		if(coverTypeCheck == SW3800_COVER_TYPE_ERR) continue;
		if(coverTypeCheck == coverTypeCheckLast) {
			coverTypeMatchCount ++;
		}
		else {
			coverTypeMatchCount = 0;
		}
		if(coverTypeMatchCount >= 2) {
			retResult = coverTypeCheck;
			break;
		}
		coverTypeCheckLast = coverTypeCheck;
	}

	udelay(STOP_TAU);
	BIF_WriteCommand(TRANSACTION_WRA, 0x10);
	for(i = 0; i < 8; i++) BIF_WriteCommand(TRANSACTION_WD, authenticationInfo.challengeData[i]);
	memset(matchData, 0x00, sizeof(matchData));
	for(i = 0; i < 5; i++) {
		mdelay(21);
		BIF_WriteCommand(TRANSACTION_RBE, 0x30 | 0x01);
		BIF_WriteCommand(TRANSACTION_RBL, 0x20 | 0x04);
		BIF_WriteCommand(TRANSACTION_RRA, 0x20);
		BIF_ReadData(readData, RESPONSE_AUTH, matchData);
		SW3800_Authorize(&authenticationInfo);
		result = SW3800_Authentication(readData, matchData, gRawData);
		if(result != SW3800_COVER_AUTH_ERR)break;
		printk(KERN_INFO "[BIF] retryCount = %d\n", i + 1);
	}
	BIF_Suspend();
	if(result == SW3800_COVER_AUTH_ERR) return SW3800_COVER_AUTH_ERR;
	return retResult;
}

static void BIF_DisableIRQ(unsigned int irq)
{
	if(gIRQMask) {
		gIRQMask = 0;
		disable_irq(irq);
		printk(KERN_INFO "[BIF] BIF_DisableIRQ\n");
	}
}

static void BIF_EnableIRQ(unsigned int irq)
{
	if(!gIRQMask) {
		gIRQMask = 1;
		enable_irq(irq);
		printk(KERN_INFO "[BIF] BIF_EnableIRQ\n");
	}
}

static int BIF_DoAuthentication(void)
{
	int result_t, i;

	for(i = 0; i < 5; i++) {
		result_t = BIF_Authentication();
		if(result_t != SW3800_COVER_AUTH_ERR) break;
//		msleep(i * 100 + 200);
		printk(KERN_INFO "[BIF] BIF_DoAuthentication retryCount : %d\n", i + 1);
	}
	return result_t;
}

static void BIF_BootDetectFunc(void)
{
	u32 result_t=0;
	printk(KERN_INFO "[BIF] BIF_BootDetectFunc\n");
	BIF_DisableIRQ(gSW3800->pdata->detectIRQ);
	if(gSW3800->pdata->bifDetectPin) {
		gSW3800->detect = !gpio_get_value(gSW3800->pdata->bifDetectPin);
		switch (gSW3800->detect) {
			case COVER_OPEN:
				BIF_SetCoverType(COVER_OPEN, COVER_OPEN);
				pm8xxx_slide_disable();
				break;
			case COVER_CLOSE:
				result_t = BIF_DoAuthentication();
				if(result_t == SW3800_COVER_AUTH_ERR) pm8xxx_slide_disable();
				else pm8xxx_slide_boot_func();
				BIF_SetCoverType(COVER_CLOSE, result_t);
				break;
		}
	}
	BIF_EnableIRQ(gSW3800->pdata->detectIRQ);
}

static void BIF_DetectWork(struct work_struct *work)
{
	u32 result_t = 0;
	printk(KERN_INFO "[BIF] BIF_DetectWork\n");
	BIF_DisableIRQ(gSW3800->pdata->detectIRQ);
	if(gSW3800->pdata->bifDetectPin) {
		gSW3800->detect = !gpio_get_value(gSW3800->pdata->bifDetectPin);
		switch (gSW3800->detect) {
			case COVER_OPEN:
				BIF_SetCoverType(COVER_OPEN, COVER_OPEN);
				pm8xxx_slide_disable();
				break;
			case COVER_CLOSE:
				result_t = BIF_DoAuthentication();
				if(result_t == SW3800_COVER_AUTH_ERR) pm8xxx_slide_disable();
				else pm8xxx_slide_enable();
				BIF_SetCoverType(COVER_CLOSE, result_t);
				break;
		}
	}
	BIF_EnableIRQ(gSW3800->pdata->detectIRQ);
}

static irqreturn_t BIF_DetectIRQHandler(int irq, void *handle)
{
	struct bif_sw3800 *sw3800_handle = handle;
	printk(KERN_INFO "[BIF] BIF_DetectIRQHandler\n");
	wake_lock_timeout(&gSW3800->wake_lock, msecs_to_jiffies(3000));
	queue_delayed_work(gBIFWorkQueue, &sw3800_handle->detect_work, msecs_to_jiffies(200));
	return IRQ_HANDLED;
}

static ssize_t BIF_DataShow(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len;
	struct bif_sw3800 *gSW3800 = dev_get_drvdata(dev);
	len = snprintf(buf, PAGE_SIZE, "sensing(Auth state) : %d\n", gSW3800->detect);
	return len;
}

static ssize_t BIF_DataStore(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct bif_sw3800 *gSW3800 = dev_get_drvdata(dev);
	sscanf(buf, "%d\n", &gSW3800->detect);
	switch_set_state(&gSW3800->sdev, gSW3800->detect);
	return count;
}
static struct device_attribute bif_data_attr   = __ATTR(detect, S_IRUGO | S_IWUSR, BIF_DataShow, BIF_DataStore);

static void BIF_ParseDT(struct device *dev, struct BIF_PlatformData *pdata)
{
	struct device_node *np = dev->of_node;

	if ((pdata->bifDetectPin = of_get_named_gpio_flags(np, "cover-detect-irq-gpio", 0, NULL)) > 0)
		pdata->detectIRQ = gpio_to_irq(pdata->bifDetectPin);
	printk(KERN_INFO "[BIF] cover-detect-irq-gpio: %d\n", pdata->bifDetectPin);

	if ((pdata->bifDataPin = of_get_named_gpio_flags(np, "cover-validation-gpio", 0, NULL)) > 0)
		BIF_DATA = pdata->bifDataPin;
	printk(KERN_INFO "[BIF] cover-validation-gpio: %d\n", pdata->bifDataPin);

	if ((pdata->bifPullupPin = of_get_named_gpio_flags(np, "cover-pullup-gpio", 0, NULL)) > 0)
		BIF_DATA_PULLUP = pdata->bifPullupPin;
	printk(KERN_INFO "[BIF] cover-pullup-gpio: %d\n", pdata->bifPullupPin);

	pdata->detectIRQFlags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
}

static int __devinit BIF_DeviceProbe(struct platform_device *pdev)
{
	int ret;
	unsigned int backcover_gpio_irq=0;
	struct BIF_PlatformData *pdata;

	if(pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(struct BIF_PlatformData), GFP_KERNEL);
		if (pdata != NULL) {
			pdev->dev.platform_data = pdata;
			BIF_ParseDT(&pdev->dev, pdata);
		}
	}
	else {
		pdata = pdev->dev.platform_data;
	}
	if(pdata == NULL) {
		pr_err("[BIF] probe: no pdata\n");
		return -ENOMEM;
	}

	gSW3800 = kzalloc(sizeof(*gSW3800), GFP_KERNEL);
	if(!gSW3800) return -ENOMEM;
	gSW3800->pdata	= pdata;
	gSW3800->sdev.name = "backcover";
	gSW3800->last_sdev.name = "lastcover";
	gSW3800->state = 0;
	gSW3800->detect= 0;

	spin_lock_init(&gSW3800->lock);
	ret = switch_dev_register(&gSW3800->sdev);
	if (ret < 0) goto err_switch_dev_register;

	ret = switch_dev_register(&gSW3800->last_sdev);
	if (ret < 0) goto err_switch_dev_register;

	wake_lock_init(&gSW3800->wake_lock, WAKE_LOCK_SUSPEND, "bif_wakeups");
	INIT_DELAYED_WORK(&gSW3800->detect_work, BIF_DetectWork);
	printk(KERN_INFO "[BIF] probe : init auth_ic\n");

	/* initialize irq of gpio */
	if(gSW3800->pdata->bifDetectPin > 0) {
		backcover_gpio_irq = gpio_to_irq(gSW3800->pdata->bifDetectPin);
		printk(KERN_INFO"[BIF] probe : backcover_gpio_irq = [%d]\n", backcover_gpio_irq);
		if(backcover_gpio_irq < 0) {
			printk(KERN_INFO "[BIF] probe failed: GPIO TO IRQ \n");
			ret = backcover_gpio_irq;
			goto err_request_irq;
		}
		ret = request_irq(backcover_gpio_irq, BIF_DetectIRQHandler, pdata->detectIRQFlags, BIF_DEV_NAME, gSW3800);
		if(ret > 0) {
			printk(KERN_ERR "[BIF] probe: Can't allocate irq %d, ret %d\n", backcover_gpio_irq, ret);
			goto err_request_irq;
		}
		if(enable_irq_wake(backcover_gpio_irq) == 0)
			printk(KERN_INFO "[BIF] probe : enable_irq_wake enabled\n");
		else
			printk(KERN_INFO "[BIF] probe : enable_irq_wake failed\n");
	}
	printk(KERN_INFO "[BIF] probe : pdata->detectIRQFlags = [%d]\n", (int)pdata->detectIRQFlags);
	printk(KERN_INFO "[BIF] probe : auth_det_func START\n");
	BIF_BootDetectFunc();
	ret = device_create_file(&pdev->dev, &bif_data_attr);
	if (ret) goto err_request_irq;
	platform_set_drvdata(pdev, gSW3800);
	return 0;

err_request_irq:
	if (backcover_gpio_irq) free_irq(backcover_gpio_irq, 0);

err_switch_dev_register:
	switch_dev_unregister(&gSW3800->sdev);
	switch_dev_unregister(&gSW3800->last_sdev);
	kfree(gSW3800);
	return ret;
}

static int __devexit BIF_DeviceRemove(struct platform_device *pdev)
{
	struct bif_sw3800 *gSW3800 = platform_get_drvdata(pdev);
	cancel_delayed_work_sync(&gSW3800->detect_work);
	switch_dev_unregister(&gSW3800->sdev);
	platform_set_drvdata(pdev, NULL);
	kfree(gSW3800);
	return 0;
}

static int BIF_DeviceSuspend(struct device *dev)
{
	printk(KERN_INFO "[BIF] BIF_DeviceSuspend\n");
	return 0;
}

static int BIF_DeviceResume(struct device *dev)
{
	printk(KERN_INFO "[BIF] BIF_DeviceResume\n");
	wake_lock_timeout(&gSW3800->wake_lock, msecs_to_jiffies(3000));
	queue_delayed_work(gBIFWorkQueue, &gSW3800->detect_work, msecs_to_jiffies(200));
	return 0;
}

static const struct dev_pm_ops bif_pm_ops = {
	.suspend = BIF_DeviceSuspend,
	.resume = BIF_DeviceResume,
};

#ifdef CONFIG_OF
static struct of_device_id bif_match_table[] = {
	{ .compatible = "sw,sw3800", },
	{ },
};
#endif

static struct platform_driver bif_device_driver = {
	.probe		= BIF_DeviceProbe,
	.remove		= __devexit_p(BIF_DeviceRemove),
	.driver		= {
					.name		= BIF_DEV_NAME,
					.owner		= THIS_MODULE,
					#ifdef CONFIG_OF
					.of_match_table = bif_match_table,
					#endif
					#ifdef CONFIG_PM
					.pm	= &bif_pm_ops,
					#endif
	},
};

static int __init BIF_DeviceInit(void)
{
	gBIFWorkQueue = create_singlethread_workqueue("gBIFWorkQueue");
	printk(KERN_ERR "[BIF] ic init \n");
	if(!gBIFWorkQueue) return -ENOMEM;
	return platform_driver_register(&bif_device_driver);
}
module_init(BIF_DeviceInit);

static void __exit BIF_DeviceExit(void)
{
	platform_driver_unregister(&bif_device_driver);
	if(gBIFWorkQueue) {
		flush_workqueue(gBIFWorkQueue);
		destroy_workqueue(gBIFWorkQueue);
	}
}
module_exit(BIF_DeviceExit);

MODULE_ALIAS("platform:" BIF_DEV_NAME);
MODULE_AUTHOR("LG Electronics Inc.");
MODULE_DESCRIPTION("sw3800 auth-ic driver");
MODULE_LICENSE("GPL");