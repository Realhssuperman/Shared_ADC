#include "UI.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "LCD.h"
#include "colors.h"
#include "ST7789.h"
#include "T6963.h"
#include "font.h"
#include "control.h"
#include "FX.h"
#include "timers.h"

UI_FIELD_T Fields[] = {
	{"Duty Cycle  ", "ct", "", (volatile int *)&g_duty_cycle, NULL, {0,7}, 
	&yellow, &black, 1, 0, 0, 0,Control_DutyCycle_Handler},
	{"Enable Ctlr ", "", "", (volatile int *)&g_enable_control, NULL, {0,8}, 
	&yellow, &black, 1, 0, 0, 0, Control_OnOff_Handler},	
	{"Enable Flash", "", "", (volatile int *)&g_enable_flash, NULL, {0,9}, 
	&yellow, &black, 1, 0, 0, 0, Control_OnOff_Handler},	
	{"T_flash_prd ", "ms", "", (volatile int *)&g_flash_period, NULL, {0,10}, 
	&yellow, &black, 1, 0, 0, 0, Control_IntNonNegative_Handler},		
	{"T_flash_on  ", "ms", "", (volatile int *)&g_flash_duration, NULL, {0,11}, 
	&yellow, &black,  1, 0, 0, 0, Control_IntNonNegative_Handler},
	{"I_set       ", "mA", "", (volatile int *)&g_set_current, NULL, {0,12}, 
	&yellow, &black, 1, 0, 0, 0, Control_IntNonNegative_Handler},
	{"I_set_peak  ", "mA", "", (volatile int *)&g_peak_set_current, NULL, {0,13}, 
	&yellow, &black, 1, 0, 0, 0, Control_IntNonNegative_Handler},
	{"I_measured  ", "mA", "", (volatile int *)&g_measured_current, NULL, {0,14}, 
	&light_gray, &black, 1, 0, 1, 1, NULL},
};

UI_SLIDER_T Slider = {
	0, {0,LCD_HEIGHT-UI_SLIDER_HEIGHT}, {UI_SLIDER_WIDTH-1,LCD_HEIGHT-1}, 
	{119,LCD_HEIGHT-UI_SLIDER_HEIGHT}, {119,LCD_HEIGHT-1}, &white, &dark_gray, &light_gray
};

extern int g_measured_accum[NUMBER_SAMPLES_NEEDED];
extern int g_set_accum[NUMBER_SAMPLES_NEEDED];
extern int state;
extern int samples_taken;

int UI_sel_field = -1;

void UI_Update_Field_Values (UI_FIELD_T * f, int num) {
	int i;
	for (i=0; i < num; i++) {
		snprintf(f[i].Buffer, sizeof(f[i].Buffer), "%s%4d %s", f[i].Label, f[i].Val? *(f[i].Val) : 0, f[i].Units);
		f[i].Updated = 1;
	}	
}

void UI_Update_Volatile_Field_Values(UI_FIELD_T * f) {
	int i;
	for (i=0; i < UI_NUM_FIELDS; i++) {
		if (f[i].Volatile) {
			snprintf(f[i].Buffer, sizeof(f[i].Buffer), "%s%4d %s", f[i].Label, f[i].Val? *(f[i].Val) : 0, f[i].Units);
			f[i].Updated = 1;
		}
	}
}

void UI_Draw_Fields(UI_FIELD_T * f, int num){
	int i;
	COLOR_T * bg_color;
	for (i=0; i < num; i++) {
		if ((f[i].Updated) || (f[i].Volatile)) { // redraw updated or volatile fields
			f[i].Updated = 0;
			if ((f[i].Selected) && (!f[i].ReadOnly)) {
				bg_color = &dark_magenta;
			} else {
				bg_color = f[i].ColorBG;
			}
			LCD_Text_Set_Colors(f[i].ColorFG, bg_color);
			LCD_Text_PrintStr_RC(f[i].RC.Y, f[i].RC.X, f[i].Buffer);
		}
	}
}

void UI_Draw_Slider(UI_SLIDER_T * s) {
	static int initialized=0;
	
	if (!initialized) {
		LCD_Fill_Rectangle(&s->UL, &s->LR, s->ColorBG);
		initialized = 1;
	}
	LCD_Fill_Rectangle(&s->BarUL, &s->BarLR, s->ColorBG); // Erase old bar
	
	s->BarUL.Y = s->UL.Y;
	s->BarLR.Y = s->LR.Y;
	s->BarUL.X = (s->LR.X - s->UL.X)/2 + s->Val;
	s->BarLR.X = s->BarUL.X + UI_SLIDER_BAR_WIDTH/2;
	s->BarUL.X -= UI_SLIDER_BAR_WIDTH/2;
	LCD_Fill_Rectangle(&s->BarUL, &s->BarLR, s->ColorFG); // Draw new bar
}

int UI_Identify_Field(PT_T * p) {
	int i, t, b, l, r;

	if ((p->X >= LCD_WIDTH) || (p->Y >= LCD_HEIGHT)) {
		return -1;
	}

	if ((p->X >= Slider.UL.X) && (p->X <= Slider.LR.X) 
		&& (p->Y >= Slider.UL.Y) && (p->Y <= Slider.LR.Y)) {
		return UI_SLIDER;
	}
  for (i=0; i<UI_NUM_FIELDS; i++) {
		l = COL_TO_X(Fields[i].RC.X);
		r = l + strlen(Fields[i].Buffer)*CHAR_WIDTH;
		t = ROW_TO_Y(Fields[i].RC.Y);
		b = t + CHAR_HEIGHT-1;
		
		if ((p->X >= l) && (p->X <= r) 
			&& (p->Y >= t) && (p->Y <= b) ) {
			return i;
		}
	}
	return -1;
}

void UI_Update_Field_Selects(int sel) {
	int i;
	for (i=0; i < UI_NUM_FIELDS; i++) {
		Fields[i].Selected = (i == sel)? 1 : 0;
	}
}

void UI_Process_Touch(PT_T * p) {  // Called by Thread_Read_TS
	int i;
	
	i = UI_Identify_Field(p);
	if (i == UI_SLIDER) {
		Slider.Val = p->X - (Slider.LR.X - Slider.UL.X)/2; // Determine slider position (value)
		if (UI_sel_field >= 0) {  // If a field is selected...
			if (Fields[UI_sel_field].Val != NULL) {
				if (Fields[UI_sel_field].Handler != NULL) {
					(*Fields[UI_sel_field].Handler)(&Fields[UI_sel_field], Slider.Val); // Have the field handle the new slider value
				}
				UI_Update_Field_Values(&Fields[UI_sel_field], 1);
			}
		}
	} else if (i>=0) {
		if (!Fields[i].ReadOnly) { // Can't select (and modify) a ReadOnly field
			UI_sel_field = i;
			UI_Update_Field_Selects(UI_sel_field);
			UI_Update_Field_Values(Fields, UI_NUM_FIELDS);
			Slider.Val = 0; // return to slider to zero if a different field is selected
		}
	} 
}
void UI_Draw_Current(void){
	int measured_avg = 0;
	int set_avg = 0;		// Used because we have 4 samples per pixel on screen
	int current_pixel, avg_counter;
	
	static PT_T measured_pt = {0, 0}, set_pt = {0, 0}, prev_measured_pt = {0, 120}, prev_set_pt = {0, 120};
	
	if(samples_taken >= NUMBER_SAMPLES_NEEDED-1){		// Array is full, 960 values accumulated
		for(current_pixel = 0; current_pixel < SCREEN_WIDTH; current_pixel++){
			prev_measured_pt = measured_pt;
			prev_set_pt = set_pt;
			measured_pt.X = current_pixel;
			set_pt.X = current_pixel;
			if(current_pixel > 0){
			for(avg_counter = 0; avg_counter<4; avg_counter++){											// Averages 4 values together
				measured_avg = measured_avg + g_measured_accum[current_pixel*4+avg_counter];
				set_avg = set_avg + g_set_accum[current_pixel*4+avg_counter];
			}
			
				measured_pt.Y = 120 - measured_avg/4;
				set_pt.Y = 120 - set_avg/4;

				measured_avg = 0;
				set_avg = 0;

				LCD_Draw_Line(&prev_measured_pt, &measured_pt, &light_gray);
				LCD_Draw_Line(&prev_set_pt, &set_pt, &red);
		}
	}
		samples_taken = SAMPLE_OFFSET;
	
		PT_T a = {0,20};
		PT_T b = {240,120};
		LCD_Fill_Rectangle(&a,&b, &black);
	}
}
void UI_Draw_Screen(int first_time) { // Called by Thread_Update_Screen
	static uint32_t counter=0;
	char buffer[32];
	
	if (first_time) {
		UI_Update_Field_Values(Fields, UI_NUM_FIELDS);
	}
	
	UI_Draw_Current();
	
	UI_Update_Volatile_Field_Values(Fields);
	UI_Draw_Fields(Fields, UI_NUM_FIELDS);
	UI_Draw_Slider(&Slider);
	
	LCD_Text_Set_Colors(&white, &black);
	if (first_time) {
			LCD_Text_PrintStr_RC(0, 6, "screen redraws");
	}
	sprintf(buffer, "%5d", counter++);
	LCD_Text_PrintStr_RC(0, 0, buffer);
}


