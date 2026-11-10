#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>

#define SPI_MAX_W_SPEED 20000000
#define SPI_DATA_ORDER	MSBFIRST
#define SPI_DATAMODE	SPI_MODE0

#define WAIT_FAILURE(WAITER, WAITMAX)  \
	do { \
			while (!waitBusy()) { \
				if (rst_counter>5) { \
					Serial.printf("%s wait exceeded %d times. Failing", WAITER, WAITMAX); \
					return false; \
				}  \
				Serial.printf("%s wait exceeded, retrying: tried %d times.", WAITER, rst_counter); \
				rst_counter++; \
			} \
			rst_counter=0; \
	}while(0)



class UC8253 {
	public:
		UC8253(uint8_t scl, uint8_t sda, uint8_t res, uint8_t dc, uint8_t cs, uint8_t busy, uint32_t width, uint32_t height):
			scl(scl), sda(sda), res(res), dc(dc), cs(cs), busy(busy), width(width), height(height) {
				fbsiz=height*(width/8); /* width is div by 8 because monochrome screen, consider bits instead */
				fb=(uint8_t *)malloc(sizeof(uint8_t)*fbsiz);
			}
		bool begin(void) {
			delay(10); /* Make sure there actually is power at everything */
			rst_counter=0;
			pinMode(cs, OUTPUT);
			pinMode(dc, OUTPUT);
			pinMode(res, OUTPUT);
			pinMode(busy, INPUT);

			Serial.begin(115200);

			SPI.begin(scl, -1, sda, cs); /* No miso line, data should be sent back via SDA? fake spi and all */
			SPI.beginTransaction(SPISettings(SPI_MAX_W_SPEED, SPI_DATA_ORDER, SPI_DATAMODE));
			delay(10);

			digitalWrite(cs, LOW);

			return true;
		}
		void sendbuf(void) {
			int i;
			command(0x13);
			for (i=0;i<bufsiz;i++) data(fb[i]);
		}
		void clearbuf(uint8_t colour) {
			memset(fb, colour, fbsiz);
		}
		void refresh(void) {
		
		}
		void lutload(void) {
		
		}
	private:
		uint8_t *fb;
		uint8_t scl, sda, res, dc, cs, busy, rst_counter; 
		uint32_t width, height;
		size_t fbsiz;

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
};

