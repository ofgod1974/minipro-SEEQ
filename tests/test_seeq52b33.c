/* Protocol-level tests: real SEEQ driver + real T48 bitbang implementation.
 * USB and time are simulated; this is not electrical hardware validation.
 * GPL-3.0-or-later; see LICENSE. */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "database.h"
#include "t48.h"
#include "seeq52b33.h"
#include "usb.h"
#include "bitbang.h"

/* Link-only sentinels for unrelated T48 operations. If a test enters any
 * ordinary chip algorithm, firmware updater or PROM path it must abort. */
int prom_init(minipro_handle_t *h) {(void)h;abort();}
int prom_terminate(minipro_handle_t *h) {(void)h;abort();}
int prom_read(minipro_handle_t *h,uint32_t a,uint8_t *b,size_t n)
{(void)h;(void)a;(void)b;(void)n;abort();}
int minipro_begin_transaction(minipro_handle_t *h) {(void)h;abort();}
int minipro_end_transaction(minipro_handle_t *h) {(void)h;abort();}
int minipro_reset(minipro_handle_t *h) {(void)h;abort();}
minipro_handle_t *minipro_open(uint8_t v) {(void)v;abort();}
void minipro_close(minipro_handle_t *h) {(void)h;abort();}
int minipro_get_devices_count(uint8_t v) {(void)v;abort();}
uint32_t crc_32(uint8_t *p,size_t n,uint32_t c) {(void)p;(void)n;(void)c;abort();}
int write_logic_file(minipro_handle_t *h,uint8_t *a,uint8_t *b)
{(void)h;(void)a;(void)b;abort();}
int minipro_test_ram_generic(minipro_handle_t *h,const uint8_t *p,size_t n)
{(void)h;(void)p;(void)n;abort();}
int read_payload2(void *h,uint8_t *p,size_t n,size_t l)
{(void)h;(void)p;(void)n;(void)l;abort();}
int write_payload2(void *h,uint8_t *p,size_t n,size_t l)
{(void)h;(void)p;(void)n;(void)l;abort();}
void format_int(uint8_t *b,uint64_t v,size_t n,uint8_t endian)
{assert(endian==MP_LITTLE_ENDIAN);for(size_t i=0;i<n;i++)b[i]=(v>>(8*i))&255;}

#define S(p) ((p)<=14 ? (p)-1 : (p)+11)
static uint8_t memory[8192], out[56], output[56], vcc[56], ground[56];
static uint8_t command, pullup;
static uint64_t now_us, falling;
static unsigned address, latched_address, latched_data, pulses, messages;
static int low, powered, stuck, bad_voltage, ovc, fail_once, cancel_sleep;
static minipro_handle_t handle;
static device_t device;
static cmdopts_t options;
static const unsigned ap[]={10,9,8,7,6,5,4,3,25,24,21,23,2};
static const unsigned dp[]={11,12,13,15,16,17,18,19};

static int level(unsigned i)
{
	if (vcc[i] && powered) return 1;
	if (ground[i]) return 0;
	return output[i] ? out[i] : pullup;
}
static unsigned bus(const unsigned *pins, unsigned len)
{
	unsigned val=0;
	for(unsigned i=0;i<len;i++) val|=level(S(pins[i]))<<i;
	return val;
}
static void edges(void)
{
	if(!powered) {low=0;return;}
	assert(level(S(1))); /* /CC never goes low */
	if(!level(S(27)) && !low) {
		assert(!level(S(20)) && level(S(22)));
		for(unsigned i=0;i<8;i++) assert(output[S(dp[i])]);
		latched_address=bus(ap,13); latched_data=bus(dp,8);
		falling=now_us;low=1;
	} else if(low && level(S(27))) {
		assert(now_us-falling>=12000);
		assert(bus(ap,13)==latched_address && bus(dp,8)==latched_data);
		if(!stuck) memory[latched_address]=latched_data;
		pulses++;low=0;
	}
	if(low) {
		assert(!level(S(20)) && level(S(22)));
		assert(bus(ap,13)==latched_address && bus(dp,8)==latched_data);
	}
}

int msg_send(void *unused, uint8_t *m, size_t n)
{
	(void)unused; messages++;
	if(fail_once) {fail_once=0;return 1;}
	assert(n>=8); command=m[0];
	switch(m[0]) {
	case 0x2d: /* reset drivers, including power */
		powered=0;low=0;memset(output,0,sizeof output);
		memset(vcc,0,sizeof vcc);memset(ground,0,sizeof ground);break;
	case 0x31: case 0x32:
		assert(!low);memset(output,0,sizeof output);pullup=m[0]==0x31;break;
	case 0x36:
		assert(m[4]<56);out[m[4]]=m[1];output[m[4]]=1;edges();break;
	case 0x2f: /* VPP command: ONLY all-off and IO supply, never enable VPP */
		assert(m[1]!=1);
		if(m[1]==0) for(unsigned i=8;i<n;i++) assert(!m[i]);
		else assert(m[1]==2 && m[8]<=4);
		break;
	case 0x30: /* GND mask: virtual pin 14 = byte9 bit2 */
		assert(m[8]==0 && m[9]==4 && m[10]==0 && m[11]==0 && m[16]==0);
		ground[S(14)]=1;break;
	case 0x2e: /* only pin1 and pin40 may get VCC */
		assert(m[8]==0 || m[8]==1);
		assert(m[9]==0 && m[10]==0 && (m[11]==0 || m[11]==0x80));
		assert(m[16]==0);
		vcc[S(1)]=m[8]&1;vcc[S(28)]=(m[11]&0x80)!=0;
		if(m[22]) assert(m[22]==40); /* target 5 V DAC code */
		powered=vcc[S(28)];edges();break;
	case 0x33: case 0x35: break;
	default: fprintf(stderr,"Unexpected USB command %02X\n",m[0]);abort();
	}
	return 0;
}
int msg_recv(void *unused, uint8_t *m, size_t n)
{
	(void)unused;memset(m,0,n);
	if(command==0x33) { /* raw ADC values, decoded by upstream formulas */
		unsigned raw=bad_voltage?1000:1811;
		m[16]=raw&255;m[17]=raw>>8;return 0;
	}
	assert(command==0x35 && n>=15);
	address=bus(ap,13);
	for(unsigned i=0;i<56;i++) {
		int bit=level(i);
		if(powered && !level(S(20)) && !level(S(22)) && level(S(27))) {
			for(unsigned j=0;j<8;j++) if(i==S(dp[j])) {
				assert(!output[i]);bit=(memory[address]>>j)&1;
			}
		}
		m[8+i/8]|=bit<<(i%8);
	}
	m[1]=ovc;
	return 0;
}
uint64_t load_int(uint8_t *b,size_t len,uint8_t endian)
{
	assert(endian==MP_LITTLE_ENDIAN);uint64_t r=0;
	for(size_t i=0;i<len;i++) r|=(uint64_t)b[i]<<(8*i);
	return r;
}
int seeq_test_nanosleep(const struct timespec *req,struct timespec *remain)
{
	uint64_t us=req->tv_sec*1000000ULL+req->tv_nsec/1000;
	if(cancel_sleep && us==12000) {
		cancel_sleep=0;now_us+=6000;*remain=(struct timespec){0,6000000};
		raise(SIGINT);errno=EINTR;return -1;
	}
	now_us+=us;return 0;
}

static void setup(void)
{
	memset(&handle,0,sizeof handle);memset(&device,0,sizeof device);
	memset(&options,0,sizeof options);memset(memory,0xff,sizeof memory);
	memset(out,0,sizeof out);memset(output,0,sizeof output);
	memset(vcc,0,sizeof vcc);memset(ground,0,sizeof ground);
	handle.version=MP_T48;handle.device=&device;handle.cmdopts=&options;
	device.flags.custom_protocol=1;device.protocol_id=CP_SEEQ52B33;
	device.package_details.pin_count=28;device.code_memory_size=8192;
	device.pulse_delay=12000;options.action=READ;
	low=powered=stuck=bad_voltage=ovc=fail_once=cancel_sleep=0;
	now_us=pulses=messages=0;
}
static void finish(void) {assert(!seeq52b33_terminate(&handle));assert(!powered && !low);}

int main(void)
{
	uint8_t b[8192], target;
	setup();handle.version=MP_T56;
	assert(seeq52b33_init(&handle));assert(!messages);
	handle.version=MP_T48;options.blank_skip=1;
	assert(seeq52b33_init(&handle));assert(!messages);
	options.blank_skip=0;device.pulse_delay=100;
	assert(seeq52b33_init(&handle));assert(!messages);
	puts("PASS reject unsupported programmer/options/timing before USB");

	setup();for(unsigned i=0;i<8192;i++) memory[i]=(i^(i>>5)^(i>>8))&255;
	assert(!seeq52b33_init(&handle));assert(!seeq52b33_read(&handle,0,b,8192));
	assert(!memcmp(b,memory,8192));assert(!pulses);finish();
	puts("PASS read all 8192 addresses without write pulses");

	setup();memory[0x1fff]=0;assert(!seeq52b33_init(&handle));target=0x55;
	assert(!seeq52b33_write(&handle,0x1fff,&target,1));assert(pulses==2 && memory[8191]==target);
	assert(!seeq52b33_write(&handle,0x1fff,&target,1));assert(pulses==2);
	target=0xff;assert(!seeq52b33_write(&handle,0x1fff,&target,1));assert(pulses==3);
	finish();puts("PASS erase-before-write, byte verification, unchanged-byte skip");

	setup();assert(!seeq52b33_init(&handle));unsigned before=messages;
	assert(seeq52b33_write(&handle,8191,b,2));assert(seeq52b33_read(&handle,8192,b,1));
	assert(seeq52b33_write(&handle,0,b,0));assert(messages==before);finish();
	puts("PASS reject out-of-range operations before I/O");

	setup();memory[0]=0;assert(!seeq52b33_init(&handle));stuck=1;target=0xaa;
	assert(seeq52b33_write(&handle,0,&target,1));assert(pulses==1 && !powered);finish();
	puts("PASS stuck byte stops before program pulse and cuts power");

	setup();assert(!seeq52b33_init(&handle));fail_once=1;
	assert(seeq52b33_read(&handle,0,b,1));assert(!powered && !pulses);finish();
	puts("PASS USB failure attempts power-off cleanup");

	setup();assert(!seeq52b33_init(&handle));cancel_sleep=1;target=0x55;
	assert(seeq52b33_write(&handle,0,&target,1));assert(pulses==1 && !powered);finish();
	puts("PASS interrupted pulse completes >=12 ms before cancellation");

	setup();bad_voltage=1;assert(seeq52b33_init(&handle));assert(!powered);finish();
	puts("PASS out-of-range VCC refuses operation and cuts power");

	setup();assert(!seeq52b33_init(&handle));ovc=1;
	assert(seeq52b33_read(&handle,0,b,1));assert(!powered);finish();
	puts("PASS overcurrent response aborts read");

	setup();memory[0]=0;memory[8191]=0x55;assert(!seeq52b33_init(&handle));
	assert(!seeq52b33_erase(&handle));for(unsigned i=0;i<8192;i++) assert(memory[i]==0xff);
	assert(pulses==2);finish();puts("PASS full erase touches only non-FF bytes");

	setup();options.action=WRITE;
	for(unsigned i=0;i<8192;i++) b[i]=(i^(i>>5)^(i>>8))&255;
	assert(!bb_begin_transaction(&handle));
	for(unsigned a=0;a<8192;a+=32) assert(!bb_write_block(&handle,MP_CODE,a,b+a,32));
	assert(!memcmp(b,memory,8192));
	memset(b,0,8192);assert(!bb_read_block(&handle,MP_CODE,0,b,8192));
	assert(!memcmp(b,memory,8192));
	assert(!bb_end_transaction(&handle));assert(!powered);
	puts("PASS full image write/read through custom bitbang dispatch");
	puts("11 protocol tests passed (simulated hardware).");return 0;
}
