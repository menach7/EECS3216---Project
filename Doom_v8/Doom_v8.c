// -----------------------------------------------------------------------------
// doom_V8.c  – stable OLED + joystick input via ADC for crosshair movement
//   • OLED 128×64   → I²C-0  (GP16 = SDA , GP17 = SCL)  @100 kHz
//   • Joystick VRX  → ADC0  (GP26)
//   • Joystick VRY  → ADC1  (GP27)
//   • Push-button    → GP15 (active-low)               (start / shoot)
//   • Diagonal movement, velocity control, survival timer, win screen
// -----------------------------------------------------------------------------

#undef  PICO_DEFAULT_I2C_SDA_PIN
#undef  PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 16
#define PICO_DEFAULT_I2C_SCL_PIN 17
#define i2c_default           i2c0             // OLED bus

#define BTN_PIN               15               // GP15 (active-low)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "ssd1306_font.h"

// ─────────── Display constants ───────────────────────────────────────────────
#define W        128
#define H         64
#define FB_LEN   (W * H / 8)
static uint8_t fb[FB_LEN];
#define OLED_ADDR 0x3C

// ─────────── ADC center calibration ──────────────────────────────────────────
static uint16_t center_x_raw, center_y_raw;

// ─────────── Joystick parameters ─────────────────────────────────────────────
#define JOY_SPEED   12    // doubled speed

// ─────────── Game constants ──────────────────────────────────────────────────
#define MAX_E       12
#define SPAWN_MS    1200  // twice as fast spawn
#define GROWTH      1.5f  // twice as fast growth
#define START_SZ    1.f
#define COLL_SZ     30
#define SURVIVE_MS  15000 // survive 15 seconds

typedef enum { SQUARE, CIRCLE } shape_t;
typedef struct { shape_t k; int16_t x; float s; uint8_t live; } enemy;

static enemy E[MAX_E];
static int    Ec = 0;

// crosshair position and timer
static int cross_x, cross_y;
static int seconds_left;

// ─────────── I²C helper (retry for 500 µs) ───────────────────────────────────
static bool i2c_write_safe(i2c_inst_t *bus, uint8_t addr,
                           const uint8_t *b, size_t n)
{
    absolute_time_t until = delayed_by_us(get_absolute_time(), 500);
    while (true) {
        if (i2c_write_timeout_us(bus, addr, b, n, false, 50) == (int)n) return true;
        if (absolute_time_diff_us(until, get_absolute_time()) <= 0)      return false;
    }
}

// ─────────── OLED primitives ────────────────────────────────────────────────
static inline void oled_cmd(uint8_t c)
{ uint8_t p[2] = {0x80, c}; i2c_write_blocking(i2c_default, OLED_ADDR, p, 2, false); }
static void oled_cmds(const uint8_t *s, size_t n) { while(n--) oled_cmd(*s++); }
static void oled_data(const uint8_t *d, size_t n)
{
    uint8_t chunk[17] = {0x40};
    while (n) {
        size_t len = n > 16 ? 16 : n;
        memcpy(chunk+1, d, len);
        i2c_write_blocking(i2c_default, OLED_ADDR, chunk, len+1, false);
        d += len; n -= len;
    }
}
static void oled_init(void)
{
    static const uint8_t seq[] = {
        0xAE,0x20,0x00,0x40,0xA1,
        0xA8,(H-1),0xC8,0xD3,0x00,
        0xDA,0x12,0xD5,0x80,0xD9,0xF1,
        0xDB,0x20,0x81,0xFF,0xA4,0xA6,
        0x8D,0x14,0x2E,0xAF
    };
    oled_cmds(seq, sizeof seq);
    sleep_ms(50);
}
static void oled_refresh(void)
{
    const uint8_t a[] = {0x21,0,W-1,0x22,0,(H/8)-1};
    oled_cmds(a, sizeof a);
    oled_data(fb, FB_LEN);
}

// ─────────── Pixel & text helpers ───────────────────────────────────────────
static inline void px(int x,int y,int on)
{
    if ((unsigned)x>=W||(unsigned)y>=H) return;
    uint16_t idx=(y>>3)*W+x; uint8_t m=1u<<(y&7);
    fb[idx] = on ? (fb[idx]|m) : (fb[idx]&~m);
}
extern uint8_t font[];
static int gi(char c){
    if(c>='A'&&c<='Z') return 1+(c-'A');
    if(c>='a'&&c<='z') return 1+(c-'a');
    if(c>='0'&&c<='9') return 27+(c-'0');
    if(c=='!') return 37; if(c=='-') return 38;
    return 0;
}
static void glyph8(int x,int y,int g){
    const uint8_t *s=&font[g*8];
    for(int cx=0;cx<8;cx++) for(int cy=0;cy<8;cy++)
        px(x+cx,y+cy, (s[cx]>>(cy))&1);
}
static void dstr(int x,int y,const char*s){for(;*s;++s,x+=8)glyph8(x,y,gi(*s));}
static void center(int y,const char*s){dstr((W-strlen(s)*8)/2,y,s);}  
static void framed(const char*msg){
    memset(fb,0,FB_LEN);
    dstr(0,H/2-16,"----------------");
    center(H/2-4,msg);
    dstr(0,H/2+8,"----------------");
    oled_refresh();
}

// ─────────── Spawn/update ───────────────────────────────────────────────────
static void spawn(void){
    if(Ec<MAX_E){
        int margin=(int)START_SZ;
        int xpos=rand()%(W-2*margin)+margin;
        E[Ec++] = (enemy){ .k=(rand()&1)?SQUARE:CIRCLE,
                           .x=xpos,
                           .s=START_SZ,
                           .live=1};
    }
}
static int update(void){
    int col=0;
    for(int i=0;i<Ec;i++){
        if(E[i].live && (E[i].s+=GROWTH)>=COLL_SZ) col=1;
    }
    return col;
}

// ─────────── Crosshair via joystick (velocity mode) ─────────────────────────
static void update_crosshair(void){
    adc_select_input(0); uint x=adc_read();
    adc_select_input(1); uint y=adc_read();
    float jx=((float)x-center_x_raw)/2048.0f;
    float jy=((float)y-center_y_raw)/2048.0f;
    jx=fmaxf(-1,fminf(1,jx)); jy=fmaxf(-1,fminf(1,jy));
    cross_x += (int)(jx * JOY_SPEED);
    cross_y += (int)(jy * JOY_SPEED);
    cross_x = fmax(4, fmin(W-5, cross_x));
    cross_y = fmax(4, fmin(H-5, cross_y));
}

// ─────────── Render & shoot ─────────────────────────────────────────────────
static void render_world(void){
    memset(fb,0,FB_LEN);
    // move & draw crosshair
    update_crosshair();
    for(int i=-2;i<=2;i++){ px(cross_x+i, cross_y,1); px(cross_x, cross_y+i,1); }
    // draw enemies
    for(int i=0;i<Ec;i++) if(E[i].live){
        int ex=E[i].x, r=(int)E[i].s;
        if(E[i].k==SQUARE){
            for(int yy=H/2-r;yy<=H/2+r;yy++)
                for(int xx=ex-r;xx<=ex+r;xx++) px(xx,yy,1);
        } else {
            for(int yy=-r;yy<=r;yy++) for(int xx=-r;xx<=r;xx++)
                if(xx*xx+yy*yy<=r*r) px(ex+xx,H/2+yy,1);
        }
    }
    // draw timer at bottom
    char tbuf[6];
    snprintf(tbuf, sizeof tbuf, "%2d", seconds_left);
    dstr((W - strlen(tbuf)*8)/2, H-8, tbuf);
    oled_refresh();
}
static void shoot(void){
    for(int i=0;i<Ec;i++){
        if(!E[i].live) continue;
        int ex=E[i].x, r=(int)E[i].s;
        bool hit=false;
        if(E[i].k==SQUARE){
            if(abs(ex-cross_x)<=r && abs(H/2-cross_y)<=r) hit=true;
        } else {
            int dx=cross_x-ex, dy=cross_y-(H/2);
            if(dx*dx+dy*dy<=r*r) hit=true;
        }
        if(hit){E[i].live=0;break;}
    }
}

// ─────────── Button helpers ──────────────────────────────────────────────────
static void wait_for_press(void){
    while(gpio_get(BTN_PIN)) sleep_ms(2);
    sleep_ms(20);
    while(!gpio_get(BTN_PIN)) sleep_ms(2);
    sleep_ms(20);
}

// ─────────── Hardware setup ──────────────────────────────────────────────────
static void hw_once(void){
    stdio_init_all(); sleep_ms(50);
    gpio_set_function(16,GPIO_FUNC_I2C);
    gpio_set_function(17,GPIO_FUNC_I2C);
    gpio_pull_up(16); gpio_pull_up(17);
    i2c_init(i2c_default,100000);
    adc_init(); adc_gpio_init(26); adc_gpio_init(27);
    gpio_init(BTN_PIN); gpio_set_dir(BTN_PIN,GPIO_IN); gpio_pull_up(BTN_PIN);
}

// ─────────── Main loop ───────────────────────────────────────────────────────
int main(void){
    hw_once(); oled_init();
    while(true){
        framed("PRESS TO START"); wait_for_press();
        for(int i=3;i>0;i--){ char d[2]={(char)('0'+i),'\0'}; framed(d); sleep_ms(500); }
        framed("GO!"); sleep_ms(400);
        adc_select_input(0); center_x_raw=adc_read();
        adc_select_input(1); center_y_raw=adc_read();
        cross_x = W/2; cross_y = H/2;
        Ec=0; memset(E,0,sizeof E); srand(time_us_32());
        uint32_t start_ms = time_us_32()/1000;
        uint32_t last_spawn = start_ms; int prev=1;
        while(true){
            uint32_t now_ms = time_us_32()/1000;
            // spawn
            if(now_ms - last_spawn >= SPAWN_MS){ spawn(); last_spawn = now_ms; }
            // timer & win check
            uint32_t elapsed = now_ms - start_ms;
            if(elapsed >= SURVIVE_MS){ framed("YOU WON!"); sleep_ms(2000); break; }
            seconds_left = (SURVIVE_MS - elapsed + 999) / 1000;
            // input & shooting
            int b = gpio_get(BTN_PIN);
            if(!b && prev) shoot();
            prev = b;
            // collision check
            if(update()){ framed("YOU DIED!"); sleep_ms(2000); break; }
            // render
            render_world(); sleep_ms(5);
        }
        sleep_ms(250);
    }
}
