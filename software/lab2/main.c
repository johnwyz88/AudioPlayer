#include <stdio.h>
#include <unistd.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"
#include "altera_avalon_timer.h"
#include "sys/alt_irq.h"
#include <io.h>
#include "SD_Card.h"
#include "fat.h"
#include "LCD.h"
#include "basic_io.h"
#include "Open_I2C.h"
#include "wm8731.h"

data_file df;
int cc[5000];
BYTE buffer[512];
BYTE dlybuf[88200]; //delay buffer is 88200 in length which correspond to 1 second
int length_cc;
int mask;
volatile int stop;
volatile u_int button1;
volatile u_int button2;
volatile u_int button3;
volatile int prevsw, sw; //store previous and current switch base
static void init_button_pio(); //initialize button interrupt
static void handle_button_interrupts(void* context, alt_u32 id);
void nextsong();
void prevsong();
void normal_speed();
void half_speed();
void double_speed();
void reverse();
void delay();

void main() {
	printf("SD_card_init() %s\n", SD_card_init() == 0 ? "successful."
				: "error!");
	printf("init_mbr() %s\n", init_mbr() == 0 ? "successful." : "error!");
	printf("init_bs() %s\n", init_bs() == 0 ? "successful." : "error!");
	init_audio_codec();
	LCD_Init();
	nextsong();
	stop = 0;

	mask = 0xff;
	init_button_pio();
	button1 = 1;
	button2 = 1;
	button3 = 1;
	prevsw = -1;
	sw = -1;
	while (1) {
		if (button1 == 0) {
			//play song
			printf("button1\n");
			//disable all buttons interrupts except button 0 (stop the song)
			mask = 0x01;
			init_button_pio();
			switch(sw){
				case 1:
					double_speed();
			    break;
			    case 2:
			    	half_speed();
			    break;
			    case 3:
			    	delay();
			    break;
			    case 4:
			    	reverse();
			    break;
			    default:
			    	normal_speed();
			    break;
			}
			button1 = 1;
		} else if (button2 == 0) {
			//next song
			printf("button2\n");
			nextsong();
			usleep(500000); //debounce
			button2 = 1;
		} else if (button3 == 0) {
			//prev song
			printf("button3\n");
			prevsong();
			usleep(500000); //debounce
			button3 = 1;
		}
		sw = IORD(SWITCH_PIO_BASE, 0);
		if(prevsw != sw){
			LCD_Display(df.Name,sw);
			prevsw = sw;
		}
	}
}

static void init_button_pio() {
	IOWR(BUTTON_PIO_BASE, 3, 0);
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, mask); // Enable first two buttons interrupts
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x0); // Reset the edge capture register.
	alt_irq_register(BUTTON_PIO_IRQ, (void *) 0, handle_button_interrupts); /* Register the interrupt handler. */
}

static void handle_button_interrupts(void* context, alt_u32 id) {
	int edge = IORD(BUTTON_PIO_BASE, 3);
	IOWR(BUTTON_PIO_BASE, 3, 0);
	switch (edge) {
	case 1:
		stop = 1;
		break;
	case 2:
		button1 = 0;
		break;
	case 4:
		button2 = 0;
		break;
	case 8:
		button3 = 0;
		break;
	}
}

void nextsong() {
	search_for_filetype("WAV", &df, 0, 1);
	LCD_File_Buffering(df.Name);
	length_cc = 1 + ceil(df.FileSize / (BPB_BytsPerSec * BPB_SecPerClus));
	build_cluster_chain(cc, length_cc, &df);
	LCD_Display(df.Name,sw);
}

void prevsong() {
	file_number = file_number - 2; //backward by 2 then next to get back by 1
	search_for_filetype("WAV", &df, 0, 1);
	LCD_File_Buffering(df.Name);
	length_cc = 1 + ceil(df.FileSize / (BPB_BytsPerSec * BPB_SecPerClus));
	build_cluster_chain(cc, length_cc, &df);
	LCD_Display(df.Name,sw);
}

void normal_speed() {
	int i, j;
	// traverse through each cluster
	for (j = 1; j <= (length_cc * BPB_SecPerClus); j++) {
		get_rel_sector(&df, buffer, cc, j);
		i = 1;
		//play each sector
		while (i <= 512) {
			UINT16 tmp;
			while (IORD( AUD_FULL_BASE, 0 )) {
			}
			tmp = (buffer[i] << 8) | (buffer[i + 1]);
			// if stop flag is signal, enable button interrupt and return to main
			if(stop){
				mask = 0xff;
				init_button_pio();
				stop = 0;
				return;
			}
			IOWR( AUDIO_0_BASE, 0, tmp );
			i = i + 2;
		}
	}
	//enable button interrupt
	mask = 0xff;
	init_button_pio();
}

void half_speed() {
	int i, j;

	// traverse through each cluster
	for (j = 1; j <= (length_cc * BPB_SecPerClus); j++) {
		get_rel_sector(&df, buffer, cc, j);
		i = 1;
		//play each sector twice
		while (i <= 512) {
			UINT16 tmp, replayed;
			replayed = 0;
			while (replayed < 2) {
				while (IORD( AUD_FULL_BASE, 0 )) {
				}
				tmp = (buffer[i] << 8) | (buffer[i + 1]);
				// if stop flag is signal, enable button interrupt and return to main
				if(stop){
					mask = 0xff;
					init_button_pio();
					stop = 0;
					return;
				}
				IOWR( AUDIO_0_BASE, 0, tmp );
				replayed++;
			}
			i = i + 2;
		}
	}
	//enable button interrupt
	mask = 0xff;
	init_button_pio();
}

void double_speed() {
	int i, j;

	// traverse through each cluster
	for (j = 1; j <= (length_cc * BPB_SecPerClus); j++) {
		get_rel_sector(&df, buffer, cc, j);
		i = 1;
		//play each sector with double the step
		while (i <= 512) {
			UINT16 tmp;
			while (IORD( AUD_FULL_BASE, 0 )) {
			}
			tmp = (buffer[i] << 8) | (buffer[i + 1]);
			// if stop flag is signal, enable button interrupt and return to main
			if(stop){
				mask = 0xff;
				init_button_pio();
				stop = 0;
				return;
			}
			IOWR( AUDIO_0_BASE, 0, tmp );
			i = i + 4;
		}
	}
	//enable button interrupt
	mask = 0xff;
	init_button_pio();
}

void reverse() {
	int i, j;

	// traverse through each cluster
	for (j = (length_cc * BPB_SecPerClus); j >= 0; j--) {
		get_rel_sector(&df, buffer, cc, j);
		i = 511;
		//play each sector backwards
		while (i >= 1) {
			UINT16 tmp;
			while (IORD( AUD_FULL_BASE, 0 )) {
			}
			tmp = (buffer[i] << 8) | (buffer[i + 1]);
			// if stop flag is signal, enable button interrupt and return to main
			if(stop){
				mask = 0xff;
				init_button_pio();
				stop = 0;
				return;
			}
			IOWR( AUDIO_0_BASE, 0, tmp );
			i = i - 2;
		}
	}
	//enable button interrupt
	mask = 0xff;
	init_button_pio();
}

void delay() {
	int i, j;
	int count = 1;
	// init dlybuf to zero
	while(count <= 88200){
		dlybuf[count] = 0;
		count++;
	}
	// Delay 1 second
	count = 1;
	for (j = 1; j <= (length_cc * BPB_SecPerClus); j++) {
		get_rel_sector(&df, buffer, cc, j);
		i = 1;
		while (i <= 512) {
			UINT16 tmp, dlytmp;
			tmp = (buffer[i] << 8) | (buffer[i + 1]); // left channel play from buffer
			while (IORD( AUD_FULL_BASE, 0 )) {
			}
			if(stop){
				mask = 0xff;
				init_button_pio();
				stop = 0;
				return;
			}
			IOWR( AUDIO_0_BASE, 0, tmp );

			dlytmp = (dlybuf[count + 2] << 8) | (dlybuf[count + 3]); // right channel play from dlybuf
			while (IORD( AUD_FULL_BASE, 0 )) {
			}
			if(stop){
				mask = 0xff;
				init_button_pio();
				stop = 0;
				return;
			}
			IOWR( AUDIO_0_BASE, 0, dlytmp );

			dlybuf[count + 2] = buffer[i + 2]; // assign right channel from buffer to dlybuf
			dlybuf[count + 3] = buffer[i + 3];
			i = i + 4;
			count = count + 4;
			if(count > 88200)
				count = 1;
		}
	}
	// play the remaining 1 second to the right channel
	int remain = 1;
	while(remain <= 88200){
		UINT16 dlytmp;
		while (IORD( AUD_FULL_BASE, 0 )) {
		}
		if(stop){
			mask = 0xff;
			init_button_pio();
			stop = 0;
			return;
		}
		IOWR( AUDIO_0_BASE, 0, 0 );
		dlytmp = (dlybuf[count + 2] << 8) | (dlybuf[count + 3]); // right channel play from dlybuf
		while (IORD( AUD_FULL_BASE, 0 )) {
		}
		if(stop){
			mask = 0xff;
			init_button_pio();
			stop = 0;
			return;
		}
		IOWR( AUDIO_0_BASE, 0, dlytmp );
		count = count + 4;
		if(count > 88200)
			count = 1;
		remain = remain + 4;
	}
	mask = 0xff;
	init_button_pio();
}


