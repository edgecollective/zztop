/*
 * Acquisition bring-up: prove the input path before asking it to measure anything.
 *
 * The excitation engine is unchanged from the previous application. What is new is
 * the other half of the loop: ADC1 sampling the signal back in.
 *
 * The important design choice is that ADC1 is triggered by the SAME TIM6 TRGO that
 * drives the DAC. Sample k is therefore taken on DAC update k, giving exactly
 * LUT_LEN samples per output period. Phase coherence between excitation and
 * acquisition is structural, a consequence of sharing one timer, rather than
 * something calibrated out afterwards. Nothing drifts because there is only one
 * clock in the story.
 *
 * The ADC DMA stream writes into the same non-cacheable region as the lookup table,
 * making it the third non-CPU accessor to rely on that foundation.
 *
 * ADC kernel clock: asynchronous from per_ck, whose reset default is the 64 MHz
 * internal oscillator, divided by 2 to give 32 MHz. That is inside this part's
 * 50 MHz limit; the synchronous AHB/4 alternative would be 60 MHz and out of spec.
 * Stage A prints the relevant clock registers so that assumption is verified on the
 * bench rather than trusted. At 64.5 + 8.5 cycles, a conversion takes 2.3 us, which
 * fits inside the 3.9 us trigger period with no room to spare. Hardware oversampling
 * would not fit at this rate and would need either a slower trigger or a faster
 * kernel clock.
 *
 * Two stages run in boot order, both observable over the debug transport:
 *   A  software-triggered polled conversions, proving the jumper, the ADC and its
 *      calibration, independent of any timing question
 *   B  timer-triggered DMA bursts of 10 output periods, reporting min, mean, max
 *      and AC RMS so the captured amplitude can be compared against what was
 *      commanded
 *
 * Wiring: the DAC output PA4 (pin 30) is jumpered to PC0, which is ADC1_INP10,
 * exposed as A0 (pin 22).
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <stm32h7xx_hal.h>
#include <math.h>

#define LUT_LEN   256
#define TIM6_PSC  0          /* f_s = 240 MHz / 938 = 255.86 kHz */
#define TIM6_ARR  937
#define N_PERIODS 10
#define N_SAMP    (N_PERIODS * LUT_LEN)   /* 2560 samples ~= 10 ms per burst */

/* DMA-visible buffers in the nocache region (NOLOAD -> runtime fill) */
static uint16_t lut[LUT_LEN] Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sram1)));
static uint16_t adc_buf[N_SAMP] Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sram1)));

static DAC_HandleTypeDef hdac1;
static DMA_HandleTypeDef hdma_dac1;
static DMA_HandleTypeDef hdma_adc1;
static ADC_HandleTypeDef hadc1;
static TIM_HandleTypeDef htim6;

static void lut_fill(void)
{
	for (int i = 0; i < LUT_LEN; i++) {
		float s = sinf(2.0f * 3.14159265f * i / LUT_LEN);
		lut[i] = (uint16_t)(2047.5f + 2047.5f * s);   /* 12-bit, mid-rail centered */
	}
}

/* --- excitation: unchanged from the tone generator --- */
static int dac_dma_start(void)
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_DAC12_CLK_ENABLE();
	__HAL_RCC_TIM6_CLK_ENABLE();
	__HAL_RCC_DMA2_CLK_ENABLE();

	GPIO_InitTypeDef g = {0};
	g.Pin = GPIO_PIN_4;
	g.Mode = GPIO_MODE_ANALOG;
	g.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &g);

	htim6.Instance = TIM6;
	htim6.Init.Prescaler = TIM6_PSC;
	htim6.Init.Period = TIM6_ARR;
	htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&htim6) != HAL_OK) return -1;

	TIM_MasterConfigTypeDef mc = {0};
	mc.MasterOutputTrigger = TIM_TRGO_UPDATE;
	mc.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &mc) != HAL_OK) return -2;

	hdma_dac1.Instance = DMA2_Stream0;
	hdma_dac1.Init.Request = DMA_REQUEST_DAC1_CH1;
	hdma_dac1.Init.Direction = DMA_MEMORY_TO_PERIPH;
	hdma_dac1.Init.PeriphInc = DMA_PINC_DISABLE;
	hdma_dac1.Init.MemInc = DMA_MINC_ENABLE;
	hdma_dac1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
	hdma_dac1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
	hdma_dac1.Init.Mode = DMA_CIRCULAR;
	hdma_dac1.Init.Priority = DMA_PRIORITY_HIGH;
	hdma_dac1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	if (HAL_DMA_Init(&hdma_dac1) != HAL_OK) return -3;

	hdac1.Instance = DAC1;
	if (HAL_DAC_Init(&hdac1) != HAL_OK) return -4;
	__HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1);

	DAC_ChannelConfTypeDef cc = {0};
	cc.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
	cc.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
	cc.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
	cc.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
	cc.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
	if (HAL_DAC_ConfigChannel(&hdac1, &cc, DAC_CHANNEL_1) != HAL_OK) return -5;

	if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)lut, LUT_LEN,
			      DAC_ALIGN_12B_R) != HAL_OK) return -6;
	HAL_TIM_Base_Start(&htim6);
	return 0;
}

/* --- sense: ADC1 INP10 on PC0 (Daisy A0 / pin 22) --- */
static int adc_init(uint32_t trigger, uint32_t data_mgmt)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_ADC12_CLK_ENABLE();
	__HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_CLKP);   /* per_ck (HSI/64MHz default) */

	GPIO_InitTypeDef g = {0};
	g.Pin = GPIO_PIN_0;
	g.Mode = GPIO_MODE_ANALOG;
	g.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOC, &g);

	hadc1.Instance = ADC1;
	hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;      /* 64/2 = 32 MHz, in spec */
	hadc1.Init.Resolution = ADC_RESOLUTION_16B;
	hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
	hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
	hadc1.Init.LowPowerAutoWait = DISABLE;
	hadc1.Init.ContinuousConvMode = DISABLE;
	hadc1.Init.NbrOfConversion = 1;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv = trigger;
	hadc1.Init.ExternalTrigConvEdge = (trigger == ADC_SOFTWARE_START)
		? ADC_EXTERNALTRIGCONVEDGE_NONE : ADC_EXTERNALTRIGCONVEDGE_RISING;
	hadc1.Init.ConversionDataManagement = data_mgmt;
	hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
	hadc1.Init.OversamplingMode = DISABLE;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) return -1;

	ADC_ChannelConfTypeDef ch = {0};
	ch.Channel = ADC_CHANNEL_10;
	ch.Rank = ADC_REGULAR_RANK_1;
	ch.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
	ch.SingleDiff = ADC_SINGLE_ENDED;
	ch.OffsetNumber = ADC_OFFSET_NONE;
	ch.Offset = 0;
	if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) return -2;

	/* offset + linearity calibration, once, before any conversion */
	if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET_LINEARITY,
					ADC_SINGLE_ENDED) != HAL_OK) return -3;
	return 0;
}

static int adc_dma_init(void)
{
	hdma_adc1.Instance = DMA2_Stream1;                 /* Stream0 belongs to the DAC */
	hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
	hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
	hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
	hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
	hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
	hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
	hdma_adc1.Init.Mode = DMA_NORMAL;                  /* one-shot burst per acquisition */
	hdma_adc1.Init.Priority = DMA_PRIORITY_MEDIUM;
	hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) return -1;
	__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
	return 0;
}

/* Stage A: prove the signal path with software-triggered polled reads */
static void stage_a_polled(void)
{
	printk("--- stage A: polled ADC, software trigger ---\n");
	printk("RCC CR=0x%08x (HSION=%d HSIRDY=%d) D3CCIPR=0x%08x\n",
	       (unsigned)RCC->CR, (int)!!(RCC->CR & RCC_CR_HSION),
	       (int)!!(RCC->CR & RCC_CR_HSIRDY), (unsigned)RCC->D3CCIPR);

	uint32_t vmin = 0xFFFF, vmax = 0;
	uint64_t acc = 0;
	const int n = 4096;
	for (int i = 0; i < n; i++) {
		HAL_ADC_Start(&hadc1);
		if (HAL_ADC_PollForConversion(&hadc1, 2) != HAL_OK) {
			printk("stage A: poll timeout at i=%d\n", i);
			return;
		}
		uint32_t v = HAL_ADC_GetValue(&hadc1);
		if (v < vmin) vmin = v;
		if (v > vmax) vmax = v;
		acc += v;
	}
	HAL_ADC_Stop(&hadc1);
	uint32_t mean = (uint32_t)(acc / n);
	/* 16-bit code -> mV at 3.3 V full scale */
	printk("stage A: n=%d min=%u mean=%u max=%u  (%u..%u..%u mV)\n",
	       n, vmin, mean, vmax,
	       (unsigned)((uint64_t)vmin * 3300 / 65535),
	       (unsigned)((uint64_t)mean * 3300 / 65535),
	       (unsigned)((uint64_t)vmax * 3300 / 65535));
}

/* Stage B: one TIM6-coherent DMA burst into adc_buf; returns 0 on success */
static int burst_capture(void)
{
	if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, N_SAMP) != HAL_OK)
		return -1;
	int64_t t0 = k_uptime_get();
	while (__HAL_DMA_GET_COUNTER(&hdma_adc1) > 0) {
		if (k_uptime_get() - t0 > 100) {           /* burst should take ~10 ms */
			HAL_ADC_Stop_DMA(&hadc1);
			return -2;
		}
	}
	HAL_ADC_Stop_DMA(&hadc1);
	return 0;
}

static void burst_stats(void)
{
	uint32_t vmin = 0xFFFF, vmax = 0;
	uint64_t acc = 0;
	for (int i = 0; i < N_SAMP; i++) {
		uint32_t v = adc_buf[i];
		if (v < vmin) vmin = v;
		if (v > vmax) vmax = v;
		acc += v;
	}
	float mean = (float)acc / N_SAMP;
	float ss = 0.0f;
	for (int i = 0; i < N_SAMP; i++) {
		float d = (float)adc_buf[i] - mean;
		ss += d * d;
	}
	float rms = sqrtf(ss / N_SAMP);
	/* full-scale sine (12-bit DAC -> 16-bit ADC codes): amplitude ~32767, RMS ~23170 */
	printk("stage B: min=%u mean=%d max=%u  AC-RMS=%d  (sine amp est=%d codes, %d mV)\n",
	       vmin, (int)mean, vmax, (int)rms,
	       (int)(rms * 1.41421356f),
	       (int)(rms * 1.41421356f * 3300.0f / 65535.0f));
}

int main(void)
{
	printk("*** lock-in: TIM6 -> {DAC1 out, ADC1 in} coherent pair ***\n");
	printk("LUT@%p adc_buf@%p (sram1 nocache)\n", (void *)lut, (void *)adc_buf);

	lut_fill();
	int rc = dac_dma_start();
	if (rc) { printk("dac_dma_start FAILED: %d\n", rc); return rc; }
	printk("excitation: f_s=255863 Hz  f_out=999.5 Hz on PA4/pin30\n");

	/* stage A: software-trigger config, polled sanity */
	rc = adc_init(ADC_SOFTWARE_START, ADC_CONVERSIONDATA_DR);
	if (rc) { printk("adc_init(sw) FAILED: %d\n", rc); return rc; }
	stage_a_polled();

	/* stage B: re-init for TIM6-triggered DMA bursts */
	HAL_ADC_DeInit(&hadc1);
	rc = adc_init(ADC_EXTERNALTRIG_T6_TRGO, ADC_CONVERSIONDATA_DMA_ONESHOT);
	if (rc) { printk("adc_init(t6) FAILED: %d\n", rc); return rc; }
	rc = adc_dma_init();
	if (rc) { printk("adc_dma_init FAILED: %d\n", rc); return rc; }

	printk("--- stage B: TIM6-coherent DMA bursts, %d samples (%d periods) ---\n",
	       N_SAMP, N_PERIODS);
	while (1) {
		rc = burst_capture();
		if (rc) {
			printk("burst FAILED: %d\n", rc);
		} else {
			burst_stats();
		}
		k_sleep(K_SECONDS(2));
	}
	return 0;
}
