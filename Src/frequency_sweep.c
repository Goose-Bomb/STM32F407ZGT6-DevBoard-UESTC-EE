#include "frequency_sweep.h"
#include "number_input.h"
#include "lcd.h"
#include "zlg7290.h"
#include "ad9959.h"

#include <arm_math.h>

//��ͼ���
static uint8_t str_buffer[32];
static uint16_t display_values[GRID_WIDTH];
static Graph_TypeDef graph;
static uint16_t cursor_XA, cursor_XB;

//�����ֵ����
static uint8_t output_amp;
static uint8_t amp_step;
//static uint8_t pe4302_2x_loss;	// dB

//ɨƵ����
static uint16_t output_freq;		// kHz
static uint16_t sweep_freq[3];		// 0:��ʼƵ��, 1:��ֹƵ��, 2:Ƶ�ʲ���

//�첨��ƽ����
extern ADC_HandleTypeDef hadc1;
static uint16_t adc_values[MAX_SAMPLE_COUNT];
static uint16_t sample_count;

void FreqSweep_Init(void)
{
	//Ӳ�������ʼ��
	PE4302_Init();
	AD9959_Init();
	ADC1_Init();

	AD9959_SetFreq(AD9959_CHANNEL_2, 1000000U);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.Pin = GPIO_PIN_10;
	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStructure.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOF, &GPIO_InitStructure);

	//���ø����ʼ����
	//pe4302_2x_loss = 0;
	output_amp = 50;
	amp_step = 1;

	output_freq = 1000;

	sweep_freq[0] = 4000;
	sweep_freq[1] = 50000;
	sweep_freq[2] = 100;
	sample_count = (sweep_freq[1] - sweep_freq[0]) / sweep_freq[2];

	//GUI ��ʼ��
	graph.X = GRID_X;
	graph.Y = GRID_Y;
	graph.Width = GRID_WIDTH;
	graph.Height = GRID_HEIGHT;
	graph.BorderColor = WHITE;
	graph.BackgroudColor = BLACK;
	graph.RoughGridColor = GRAY;
	graph.FineGridColor = DARKGRAY;

	Graph_Init(&graph);

	/* Interp Test */
	uint16_t size = 64;
	float delta = 0.5f;

	uint16_t sample_data[64];
	uint16_t origin_data[GRID_WIDTH];
	uint16_t interp_data[GRID_WIDTH];

	for (uint16_t i = 0; i < size; i++) {
		sample_data[i] = (2.0f + arm_sin_f32(delta * i)) * 100U;
	}

	for (uint16_t i = 0; i < GRID_WIDTH; i++) {
		//Linear Interpolation
		uint32_t x = (i << 20) / GRID_WIDTH * size;
		interp_data[i] = arm_linear_interp_q15(sample_data, x, size);
		//For Contrast
		origin_data[i] = (2.0f + arm_sin_f32(delta * 64 / GRID_WIDTH * i)) * 100U;
	}

	Graph_DrawCurve(&graph, origin_data, BLUE);
	Graph_DrawCurve(&graph, interp_data, RED);

	LCD_BackBuffer_Update();

	int i;

	//������-����
	LCD_ShowChar_ASCII(GRID_X - 15, GRID_Y + 192, 16, '0', WHITE);
	for (i = 0; i < 4; i++) {
		LCD_ShowNumber(GRID_X - 20, GRID_Y - 8 + i * 50, 16, (4 - i) * 10, WHITE);
	}
	for (i = 0; i < 3; i++) {
		LCD_ShowNumber(GRID_X - 20, GRID_Y + 242 + i * 50, 16, (i + 1) * 10, WHITE);
		LCD_ShowChar_ASCII(GRID_X - 30, GRID_Y + 242 + i * 50, 16, '-', WHITE);
	}
	//��λ��dB
	LCD_ShowString(GRID_X - 20, GRID_Y + GRID_HEIGHT - 16, 16, "dB", WHITE);

	//������-Ƶ��
	UpdateFreqInfoDispaly();
	//��λ��MHz
	LCD_ShowString(GRID_X + GRID_WIDTH - 12, GRID_Y + GRID_HEIGHT + 20, 16, "MHz", WHITE);

	//ɨƵ��Ϣ��    
	LCD_DrawRect(FREQBOX_X, FREQBOX_Y, FREQBOX_WIDTH, FREQBOX_HEIGHT, WHITE);
	LCD_ShowString(FREQBOX_X + 8, FREQBOX_Y + 8, 16, "��ʼƵ��:", WHITE);
	LCD_ShowString(FREQBOX_X + 8, FREQBOX_Y + 32, 16, "��ֹƵ��:", WHITE);
	LCD_ShowString(FREQBOX_X + 8, FREQBOX_Y + 56, 16, "Ƶ�ʲ���:", WHITE);
	LCD_ShowString(FREQBOX_X + 8, FREQBOX_Y + 80, 16, "��������:", WHITE);

	//���������Ϣ��
	LCD_DrawRect(AMPBOX_X, AMPBOX_Y, AMPBOX_WIDTH, AMPBOX_HEIGHT, WHITE);
	LCD_ShowString(AMPBOX_X + 8, AMPBOX_Y + 8, 16, "��ǰ�������:", WHITE);
	LCD_ShowString(AMPBOX_X + 8, AMPBOX_Y + 32, 16, "���ȵ��ڲ���:", WHITE);

	UpdateOutputAmp();

	//��������
	LCD_DrawRect(CURSORBOX_X, CURSORBOX_Y, CURSORBOX_WIDTH, CURSORBOX_HEIGHT, WHITE);
	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 8, 16, "���ѡ��:", WHITE);

	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 40, 16, "A - Ƶ��:", WHITE);
	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 64, 16, "B - Ƶ��:", WHITE);
	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 88, 16, "��- Ƶ��:", WHITE);

	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 120, 16, "A - ����:", WHITE);
	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 144, 16, "B - ����:", WHITE);
	LCD_ShowString(CURSORBOX_X + 8, CURSORBOX_Y + 168, 16, "��- ����:", WHITE);

	SetFreqParameters();
}

static void UpdateFreqInfoDispaly(void)
{
	//ʹ�ô�ڿ����[����ɾ��]
	LCD_FillRect(GRID_X, GRID_Y + GRID_HEIGHT + 3, GRID_WIDTH, 16, BLACK);
	LCD_FillRect(FREQBOX_X + 85, FREQBOX_Y + 8, 80, 88, BLACK);

	//����Ƶ��������ֵ
	for (uint8_t i = 0; i <= 10; i++) {

		sprintf(str_buffer, "%-4.1f", (sweep_freq[0] + i * (sweep_freq[1] - sweep_freq[0]) / 10) * 0.001f);
		LCD_ShowString(GRID_X - 12 + i * 50, GRID_Y + GRID_HEIGHT + 2, 16, str_buffer, WHITE);
	}

	//����ɨƵ��Ϣ����ʾ��ֵ
	for (uint8_t i = 0; i < 3; i++) {
		sprintf(str_buffer, "%-6.3f MHz", sweep_freq[i] * 0.001f);
		LCD_ShowString(FREQBOX_X + 85, FREQBOX_Y + 8 + 24 * i, 16, str_buffer, LIGHTGRAY);
	}

	sprintf(str_buffer, "%u ��", sample_count);
	LCD_ShowString(FREQBOX_X + 85, FREQBOX_Y + 80, 16, str_buffer, LIGHTGRAY);
}

static inline void UpdateOutputAmp(void)
{
	//�������˥��ֵ
	PE4302_SetLoss(amp_table[50 - output_amp][0]);
	AD9959_SetAmp(AD9959_CHANNEL_2, amp_table[50 - output_amp][1]);

	//ʹ�ô�ڿ����[����ɾ��]
	LCD_FillRect(AMPBOX_X + 118, AMPBOX_Y + 8, 48, 40, BLACK);

	//�����������Ϣ����ʾ��ֵ
	sprintf(str_buffer, "%-3u mV", output_amp * 2);
	LCD_ShowString(AMPBOX_X + 118, AMPBOX_Y + 8, 16, str_buffer, LIGHTGRAY);

	sprintf(str_buffer, "%-3u mV", amp_step * 2);
	LCD_ShowString(AMPBOX_X + 118, AMPBOX_Y + 32, 16, str_buffer, LIGHTGRAY);
}

static inline void FreqParameterDisplay(uint8_t i, _Bool isSlected)
{
	uint16_t back_color = (isSlected) ? LIGHTGRAY : BLACK;
	uint16_t font_color = (isSlected) ? BLACK : LIGHTGRAY;

	LCD_FillRect(FREQBOX_X + 85, FREQBOX_Y + 8 + 24 * i, 80, 16, back_color);
	sprintf(str_buffer, "%-6.3f MHz", sweep_freq[i] * 0.001f);
	LCD_ShowString(FREQBOX_X + 85, FREQBOX_Y + 8 + 24 * i, 16, str_buffer, font_color);
}

static void SetFreqParameters(void)
{
	uint8_t i = 0;
	float input_val;

	uint16_t backup_sweep_freq[3];
	backup_sweep_freq[0] = sweep_freq[0];
	backup_sweep_freq[1] = sweep_freq[1];
	backup_sweep_freq[2] = sweep_freq[2];

	FreqParameterDisplay(i, 1);

	for (;;)
	{
		switch (ZLG7290_ReadKey())
		{
		case 5:

			FreqParameterDisplay(i, 0);

			if (i == 0) {
				i = 2;
			}
			else {
				--i;
			}

			FreqParameterDisplay(i, 1);
			break;

		case 13:

			FreqParameterDisplay(i, 0);

			if (i == 2) {
				i = 0;
			}
			else {
				++i;
			}

			FreqParameterDisplay(i, 1);
			break;

		case 21:

			if (GetInputFloat(&input_val)) {
				sweep_freq[i] = (uint16_t)(input_val * 1000U);
				FreqParameterDisplay(i, 1);
			}

			break;

		case 29:
			//�����ɨƵ��Χ��Ч ��ԭԭ���ķ�Χ
			if (sweep_freq[0] > sweep_freq[1] || sweep_freq[1] < 100U || sweep_freq[1] > 200000U) {
				sweep_freq[0] = backup_sweep_freq[0];
				sweep_freq[1] = backup_sweep_freq[1];
			}

			sample_count = (sweep_freq[1] - sweep_freq[0]) / sweep_freq[2];

			//������̫���̫�� ��ԭԭ����Ƶ�ʲ���
			if (sample_count < MIN_SAMPLE_COUNT || sample_count > MAX_SAMPLE_COUNT) {
				sweep_freq[2] = backup_sweep_freq[2];
				sample_count = (sweep_freq[1] - sweep_freq[0]) / sweep_freq[2];
			}

			UpdateFreqInfoDispaly();
			return;
		}

		Delay_ms(33);
	}
}


void FreqSweep_Function()
{
	for (;;)
	{
		switch (ZLG7290_ReadKey())
		{
		case 1:
			FreqPoint_Output();
			break;

		case 9:
			FreqSweep_Start();
			break;

		case 17:
			Draw_Curve(LIGHTBLUE);
			Curve_Trace();
			Recover_Grid();
			break;
		}

		STATUS_LED(1);
		Delay_ms(75);
		STATUS_LED(0);
		Delay_ms(75);
	}
}

/*
void FreqPoint_Output(void)
{

	Update_StatusDisplay(FREQ_POINT_OUTPUT);
	LCD_ShowNumber(640, 260, 32, output_freq, GREEN);
	LCD_ShowNumber(640, 156, 32, pe4302_2x_loss, RED);

	AD9959_SetAmp(AD9959_CHANNEL_3, 1023);
	AD9959_SetFreq(AD9959_CHANNEL_3, output_freq * 1000000U);
	PE4302_SetLoss(0, pe4302_2x_loss);


	_Bool flag = 1;
	while (flag)
	{
		switch (ZLG7290_ReadKey())
		{
		case 34:
			if (output_freq > 1)
			{
				output_freq--;
				AD9959_SetFreq(AD9959_CHANNEL_3, output_freq * 1000000U);
				LCD_FillRect(640, 260, 32, 32, BLACK);
				LCD_ShowNumber(640, 260, 32, output_freq, GREEN);
			}
			break;

		case 36:
			if (output_freq < 40)
			{
				output_freq++;
				AD9959_SetFreq(AD9959_CHANNEL_3, output_freq * 1000000U);
				LCD_FillRect(640, 260, 32, 32, BLACK);
				LCD_ShowNumber(640, 260, 32, output_freq, GREEN);
			}
			break;

		case 37:
			if (pe4302_2x_loss < 63)
			{
				PE4302_SetLoss(0, ++pe4302_2x_loss);
				LCD_FillRect(640, 156, 48, 32, BLACK);
				LCD_ShowNumber(640, 156, 32, pe4302_2x_loss, RED);
			}
			break;

		case 29:
			if (pe4302_2x_loss > 0)
			{
				PE4302_SetLoss(0, --pe4302_2x_loss);
				LCD_FillRect(640, 156, 48, 32, BLACK);
				LCD_ShowNumber(640, 156, 32, pe4302_2x_loss, RED);
			}
			break;

		case 5:
			flag = 0;
			Update_StatusDisplay(CHI_GUA);
			break;
		}

		Delay_ms(10);
	}
}
*/



