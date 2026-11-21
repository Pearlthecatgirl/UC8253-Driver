#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>
#include <assert.h>

/* Basic operation flow
 *
 * assume serial is setup
 * begin()
 * # methods to edit the fb
 *
 * # set refresh method
 * 	- fast full
 * 	- full
 * 	- partial
 *
 * # sendfb -> This method will send fb and refresh all at once
 *
 * end()
 *
 *
 * */

#define SPI_MAX_W_SPEED 20000000
#define SPI_DATA_ORDER	MSBFIRST
#define SPI_DATAMODE	SPI_MODE0

#define WAIT_FAILURE(WAITER, WAITMAX)  \
	do { \
			while (!waitBusy()) { \
				if (rst_counter>WAITMAX) { \
					Serial.printf("%s wait exceeded %d times. Failing", WAITER, WAITMAX); \
					return false; \
				}  \
				Serial.printf("%s wait exceeded, retrying: tried %d times.", WAITER, rst_counter); \
				rst_counter++; \
			} \
			rst_counter=0; \
	}while(0)

typedef enum {FULL, FULLFAST, PARTIAL} UC8253_refreshType ;

class UC8253 {
	public:
		UC8253(uint8_t scl, uint8_t sda, uint8_t res, uint8_t dc, uint8_t cs, uint8_t busy, uint32_t width, uint32_t height):
			scl(scl), sda(sda), res(res), dc(dc), cs(cs), busy(busy), width(width), height(height) {
				fbsiz=height*(width/8); /* width is div by 8 because monochrome screen, consider bits instead */
				fb=(uint8_t *)malloc(sizeof(uint8_t)*fbsiz);
				oldfb=(uint8_t *)malloc(sizeof(uint8_t)*fbsiz);
				memset(oldfb, 0xff, sizeof(uint8_t)*fbsiz);
			}
		bool begin(void) {
			pinMode(cs, OUTPUT);
			pinMode(dc, OUTPUT);
			pinMode(res, OUTPUT);
			pinMode(busy, INPUT);

			SPI.begin(scl, -1, sda, cs); /* No miso line, data should be sent back via SDA? fake spi and all */
			SPI.beginTransaction(SPISettings(SPI_MAX_W_SPEED, SPI_DATA_ORDER, SPI_DATAMODE));

			/*HWRESET*/

			digitalWrite(res, LOW);
			delay(20);
			digitalWrite(res, HIGH);
			delay(20);

			command(0x04); /* POWER ON: PON*/
			WAIT_FAILURE("POWER ON Sequence", 5);

			command(0x00); /* PANEL SETTING: PSR*/
			data(0xdf);
			/* RES1|RES0 : 	RESOLUTION 0b11 ->480x240
			 * REG		 : 	0b0 -> load lut from otp
			 * KW/R		 : 	0b1 -> bw only
			 * UD		 : 	gate scan direction 0b1 -> scan up
			 * SHL		 : 	src shift direction 0b1 -> shift right
			 * SHD_N	 : 	booster switch 0b1 -> booster on
			 * RST_N	 : 	soft reset 0b1 -> no reset
			 * */
			data(0x0d);
			/* DC|DC|DC : 	3 bits don't care
			 * VCMZ		:	VCOM Hi-Z state function 0b0 -> no effect
			 * TS_AUTO	:	Temp sensor activation 0b1
			 * TIEG		:	VGL state function 0b1
			 * NORG		:	VCOM during display 0b0 
			 * VCM_LUTZ	:	default value 0b1
			 * */
			command(0x01); /* POWER setting: PWR */
			data(0x03); /*Default settings */
			data(0x10); /*Default settings */
			data(0x3f); /*Default settings */
			data(0x3f); /*Default settings */
			data(0x0D); /*Default settings */

			command(0x41);
			data(0x80); /*enable internal temp sensor */

			command(0x61); /*RESOLUTION SETTING: TRES*/
			//data((width>>3)<<3); /* 8 gates per pin, remove lower 3 bits */
			data(((width-1)&0xf8)); /*just take the upper 5 bits*/
			data((height-1)>>8);
			data((height-1)&0xff);

			command(0x50); /* VCOM setting */
			data(0xD7);

			WAIT_FAILURE("Initialization VCOM setting", 5);

			return true;
		}
		bool end(void) {
			command(0x02);

			WAIT_FAILURE("sleeping", 5);

			command(0x07);
			data(0xa5);
			return true;
		}
		void setrefreshMode(UC8253_refreshType ref) {
			this->refMode=ref;
		}
		void setfb() {
			memset(fb, 0x55, fbsiz);
		}
		bool sendfb() {
			uint8_t *tmpptr;
			/* Set partial refresh data */
			setRAMStartStop(1, 1, width-1, height-1); /* 1 indexed. */
			command(0x91); /*PTIN*/

			command(0x10);
			uint32_t i;
			for (i=0;i<fbsiz;i++) {
				data(oldfb[i]);
			}
			command(0x13);
			for (i=0;i<fbsiz;i++) {
				data(fb[i]);
			}
			switch (refMode) {
				case FULL:
					break;
				case FULLFAST:
					break;
				case PARTIAL:
					break;
				default:
					break;

			}

			/* Refresh sequence */

			/* Fast refresh data*/
			//command(0xE0);
			//data(0x02);

			/* Fast refresh data*/
			//command(0xE5);
			//data(0x5A);

			command(0x50);
			data(0x97);

			command(0x12);/* refresh */

			WAIT_FAILURE("refresh", 5);

			command(0x92); /*exit partial refresh mode*/

			tmpptr=oldfb;
			oldfb=fb;
			fb=tmpptr;

			return true;
		}
	private:
		uint8_t *fb, *oldfb; /*Only let edit fb, oldfb should not be editable*/
		uint8_t scl, sda, res, dc, cs, busy, rst_counter; 
		uint32_t width, height;
		size_t fbsiz;
		UC8253_refreshType refMode;
		/* 0 index? or 1 index. 1 index!!*/
		void setRAMStartStop(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey) {
			assert(sx > 0 && sy > 0);
			assert(ex > 0 && ey > 0);
			assert(sx < width && sy < height); /* x is 5 bits, y is 9 bits*/
			assert(ex < width && ey < height);
			assert(sx < ex); /* ignore all 0 widths/ 0 heights */
			assert(sy < ey);

			command(0x90);
			data(((sx-1)&0xf8)|0x7); /*just take the upper 5 bits, bottom 3 bits are always 1*/
			data(((ex-1)&0xf8)|0x7); /*just take the upper 5 bits, bottom 3 bits are always 1*/
			data((sy-1)>>8);
			data((sy-1)&0xff);
			data((ey-1)>>8);
			data((ey-1)&0xff);
			data(0x01);
		}
		void command(uint8_t cmd) {
			digitalWrite(dc, LOW);
			digitalWrite(cs, LOW);
			SPI.transfer(cmd);
			digitalWrite(cs, HIGH);
		}
		/* UC8253 datasheet specifies that CS should be
		 * pulled high every 8 bits, so do not include multi byte transfer
		 * */
		void data(uint8_t dat) {
			digitalWrite(dc, HIGH);
			digitalWrite(cs, LOW);
			SPI.transfer(dat);
			digitalWrite(cs, HIGH);
		}
		bool waitBusy(uint32_t wait=10000) {
			uint32_t start=millis();
			/* BUSY is active LOW */
			while (digitalRead(busy)==LOW) {
				if (millis()-start>wait) return false;
				delay(100);
			}
			return true;
		}
		uint8_t flip_byte(uint8_t byte) {
			uint8_t LUT[]={
				0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, 
				0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 
				0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 
				0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, 
				0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 
				0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
				0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, 
				0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
				0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
				0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, 
				0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
				0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
				0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, 
				0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
				0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 
				0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
			};
			return LUT[byte];
		}
};

