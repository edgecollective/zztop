/*
 * A continuously generated sine wave: timer-triggered DMA into the DAC.
 *
 * This is the first real signal the instrument produces. The CPU fills a lookup
 * table once at startup and then does nothing; the waveform comes out of a
 * hardware chain that free-runs:
 *
 *   TIM6 update event -> TRGO -> DAC1 conversion -> DMA2 fetches the next sample
 *
 * The DMA stream is circular, so it wraps the lookup table forever with no
 * interrupts and no CPU involvement. That matters more than it might appear:
 * a measurement instrument whose output waveform depends on interrupt latency
 * has jitter designed into it from the start.
 *
 * Zephyr's st,stm32-dac binding is single-value, with no notion of a hardware
 * trigger or a DMA stream, so this drives the vendored STM32 HAL directly. The
 * arrangement follows Electrosmith's libDaisy (src/per/dac.cpp), which uses the
 * same trigger, stream and circular mode on this same part.
 *
 * The lookup table lives in the board's non-cacheable sram1 region, established
 * by the previous application. The data cache stays enabled and the DMA engine
 * still reads fresh samples, because writes to that region bypass the cache.
 * libDaisy places all of its DMA buffers in the same memory for the same reason.
 *
 * Output appears on DAC1_OUT1, which is PA4, exposed as pin 30 on the Daisy Seed.
 * See ../../BUILDING.md for the build invocation.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <stm32h7xx_hal.h>
#include <math.h>

#define LUT_LEN 256

/*
 * The frequency plan.
 *
 * TIM6 is clocked from the APB1 timer clock. On this clock tree that is
 * SYSCLK 480 MHz -> HCLK 240 MHz (hpre /2) -> APB1 120 MHz (d2ppre1 /2), and
 * the APB1 *timer* clock runs at twice APB1, so 240 MHz.
 *
 *   f_sample = 240 MHz / ((TIM6_PSC + 1) * (TIM6_ARR + 1))
 *   f_out    = f_sample / LUT_LEN
 *
 * With PSC = 0 and ARR = 937 that gives f_sample = 255.9 kHz and, across a
 * 256-point table, f_out = 999.5 Hz. The 0.5 Hz shortfall from a round kilohertz
 * is not error: it is the closest the integer divider chain can land, and the
 * measurement side knows the exact value rather than assuming 1000 Hz.
 *
 * Two constraints shape the choice. DAC settling time (roughly 1.5 us to full
 * 12-bit accuracy) puts a practical ceiling on f_sample near 600 kHz. And for a
 * given f_sample, more samples per period buys a cleaner spectrum at the cost of
 * a lower maximum output frequency.
 */
#define TIM6_PSC   0
#define TIM6_ARR   937

/* sine lookup table, placed in the non-cacheable region so the DMA engine
 * reads it without cache interference. NOLOAD section, filled at runtime. */
static uint16_t lut[LUT_LEN] Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sram1)));

static DAC_HandleTypeDef hdac1;
static DMA_HandleTypeDef hdma_dac1;
static TIM_HandleTypeDef htim6;

static void lut_fill(void)
{
	for (int i = 0; i < LUT_LEN; i++) {
		float s = sinf(2.0f * 3.14159265f * i / LUT_LEN);
		lut[i] = (uint16_t)(2047.5f + 2047.5f * s);   /* 12-bit, mid-rail centered */
	}
}

static int dac_dma_start(void)
{
	/* clocks */
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_DAC12_CLK_ENABLE();
	__HAL_RCC_TIM6_CLK_ENABLE();
	__HAL_RCC_DMA2_CLK_ENABLE();

	/* PA4 analog (DAC1_OUT1) */
	GPIO_InitTypeDef g = {0};
	g.Pin = GPIO_PIN_4;
	g.Mode = GPIO_MODE_ANALOG;
	g.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &g);

	/* TIM6: update event at f_sample, TRGO on update */
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

	/* DMA2_Stream0 <- DMAMUX1 request DAC1_CH1 (67), circular mem->periph halfword */
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

	/* DAC1 channel 1 (PA4), hardware-triggered by TIM6 TRGO */
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

int main(void)
{
	const float f_tim = 240e6f;
	float f_s = f_tim / ((TIM6_PSC + 1.0f) * (TIM6_ARR + 1.0f));
	float f_out = f_s / LUT_LEN;

	printk("*** dac_dma_tone: TIM6 -> DAC1 -> DMA2 circular ***\n");
	printk("LUT@%p (sram1 nocache), %d pts\n", (void *)lut, LUT_LEN);

	lut_fill();
	int rc = dac_dma_start();
	if (rc) {
		printk("dac_dma_start FAILED: %d\n", rc);
		return rc;
	}
	printk("running: f_sample=%d Hz  f_out=%d Hz  (PSC=%d ARR=%d)\n",
	       (int)f_s, (int)f_out, TIM6_PSC, TIM6_ARR);

	while (1) {
		k_sleep(K_SECONDS(5));
		printk("alive: DAC free-running, expect %d Hz sine on PA4/pin30\n", (int)f_out);
	}
	return 0;
}
