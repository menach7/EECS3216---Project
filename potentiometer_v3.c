// -----------------------------------------------------------------------------
// potentiometer_v3.c  –  Gauge-push game with start, countdown, timer, RGB LED
//   Wording & timer routines from doom_V8.c; LEDs off until play begins.
//   Now requires 5 unique correct placements to win.
// -----------------------------------------------------------------------------

#undef  PICO_DEFAULT_I2C_SDA_PIN
#undef  PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 16    // OLED SDA
#define PICO_DEFAULT_I2C_SCL_PIN 17    // OLED SCL
#define i2c_default           i2c0

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include "ssd1306_font.h"

// ─── Display ────────────────────────────────────────────────
#define W        128
#define H         64
#define FB_LEN   (W * H / 8)
static uint8_t fb[FB_LEN];
#define OLED_ADDR 0x3C

// ─── Pins ────────────────────────────────────────────────────
#define BUTTON_PIN      15    // GP15 active‐low
#define LED_R           0     // GP0 common‐anode RED cathode
#define LED_G           1     // GP1 common‐anode GREEN cathode

// ─── Potentiometer ───────────────────────────────────────────
#define POT_ADC         1     // GP27 → ADC1

// ─── Game timing ─────────────────────────────────────────────
#define TARGET_ZONE_WIDTH 20
#define ENTRY_TIME_MS     3000
#define HOLD_TIME_MS      3000

// ─── Number of rounds ───────────────────────────────────────
#define NUM_ROUNDS       5

#define DEG2RAD(x)     ((x) * M_PI / 180.0f)

// ─── OLED low‐level ──────────────────────────────────────────
static inline void oled_cmd(uint8_t c) {
    uint8_t b[2] = {0x80, c};
    i2c_write_blocking(i2c_default, OLED_ADDR, b, 2, false);
}
static void oled_cmds(const uint8_t *s, size_t n) {
    while (n--) oled_cmd(*s++);
}
static void oled_data(const uint8_t *d, size_t n) {
    uint8_t buf[17] = {0x40};
    while (n) {
        size_t l = n > 16 ? 16 : n;
        memcpy(buf+1, d, l);
        i2c_write_blocking(i2c_default, OLED_ADDR, buf, l+1, false);
        d += l; n -= l;
    }
}
static void oled_init(void) {
    static const uint8_t seq[] = {
        0xAE,0x20,0x00,0x40,0xA1,0xA8,(H-1),0xC8,0xD3,0x00,
        0xDA,0x12,0xD5,0x80,0xD9,0xF1,0xDB,0x20,0x81,0xFF,
        0xA4,0xA6,0x8D,0x14,0x2E,0xAF
    };
    oled_cmds(seq, sizeof seq);
    sleep_ms(50);
}
static void oled_refresh(void) {
    const uint8_t a[] = {0x21,0,W-1,0x22,0,(H/8)-1};
    oled_cmds(a, sizeof a);
    oled_data(fb, FB_LEN);
}
static inline void px(int x,int y,bool on) {
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    uint16_t idx = (y>>3)*W + x;
    uint8_t  m   = 1u << (y&7);
    fb[idx] = on ? (fb[idx] | m) : (fb[idx] & ~m);
}

// ─── Text & framing from doom_V8.c ──────────────────────────
extern uint8_t font[];
static int gi(char c){
    if(c>='A'&&c<='Z') return 1+(c-'A');
    if(c>='a'&&c<='z') return 1+(c-'a');
    if(c>='0'&&c<='9') return 27+(c-'0');
    if(c=='!') return 37;
    if(c=='-') return 38;
    return 0;
}
static void glyph8(int x,int y,int g){
    const uint8_t *s = &font[g*8];
    for(int cx=0;cx<8;cx++) for(int cy=0;cy<8;cy++)
        px(x+cx,y+cy,(s[cx]>>cy)&1);
}
static void dstr(int x,int y,const char *s){
    for(;*s;++s,x+=8) glyph8(x,y,gi(*s));
}
static void centered(int y,const char *s){
    dstr((W-strlen(s)*8)/2,y,s);
}
static void framed(const char *msg){
    memset(fb,0,FB_LEN);
    dstr(0,H/2-16,"----------------");
    centered(H/2-4,msg);
    dstr(0,H/2+8,"----------------");
    oled_refresh();
}

// ─── Pot handling ───────────────────────────────────────────
static void adc_init_pot(void){
    adc_init();
    adc_gpio_init(27);
}
static uint16_t read_pot_raw(void){
    adc_select_input(POT_ADC);
    return adc_read();
}
static float map_pot_to_degrees(uint16_t raw){
    float d = (raw/4095.0f)*180.0f;
    return 180.0f - d;
}

// ─── Graphics primitives ─────────────────────────────────────
void clear_fb(void){ memset(fb,0,FB_LEN); }
void draw_semicircle(int cx,int cy,int r){
    for(int a=0;a<=180;a+=3){
        float rd=DEG2RAD(a);
        px(cx+r*cosf(rd),cy-r*sinf(rd),true);
    }
}
void draw_ticks(int cx,int cy,int r){
    for(int a=0;a<=180;a+=30){
        float rd=DEG2RAD(a);
        px(cx+(r-2)*cosf(rd),cy-(r-2)*sinf(rd),true);
        px(cx+ r   *cosf(rd),cy- r   *sinf(rd),true);
    }
}
void draw_target_zone(int cx,int cy,int r,float s,float e){
    for(float a=s;a<=e;a+=1.0f){
        float rd=DEG2RAD(a);
        px(cx+(r-10)*cosf(rd),cy-(r-10)*sinf(rd),true);
    }
}
void draw_needle(int cx,int cy,float ang,int len){
    float rd=DEG2RAD(ang);
    int x2=cx+len*cosf(rd),y2=cy-len*sinf(rd);
    int dx=abs(x2-cx),sx=cx<x2?1:-1;
    int dy=-abs(y2-cy),sy=cy<y2?1:-1;
    int err=dx+dy,e2,x=cx,y=cy;
    while(1){
        px(x,y,true);
        if(x==x2&&y==y2) break;
        e2=2*err;
        if(e2>=dy){ err+=dy; x+=sx; }
        if(e2<=dx){ err+=dx; y+=sy; }
    }
}

// ─── Render one frame + timer ─────────────────────────────────
void render_frame(float ang,float ts,float te,uint32_t left){
    clear_fb();
    int cx=(W-1)/2,cy=H/2,r=28;
    draw_semicircle(cx,cy,r);
    draw_ticks(cx,cy,r);
    draw_target_zone(cx,cy,r,ts,te);
    draw_needle(cx,cy,ang,r-2);
    // draw countdown seconds
    char buf[4];
    snprintf(buf,sizeof(buf),"%u",left);
    dstr((W-strlen(buf)*8)/2, H-8, buf);
    oled_refresh();
}

// ─── RGB LED control (common‐anode) ───────────────────────────
static inline void set_led(bool r_on,bool g_on){
    gpio_put(LED_R, r_on?0:1);
    gpio_put(LED_G, g_on?0:1);
}

// ─── Debounced button wait ─────────────────────────────────────
static void wait_for_button(void){
    while(gpio_get(BUTTON_PIN)) tight_loop_contents();
    sleep_ms(20);
    while(!gpio_get(BUTTON_PIN)) tight_loop_contents();
    sleep_ms(20);
}

// ─── One play round ────────────────────────────────────────────
void game_round(float ts,float te){
    // ENTRY
    uint32_t start_ms = time_us_32()/1000;
    while(1){
        uint32_t now_ms = time_us_32()/1000;
        if(now_ms - start_ms >= ENTRY_TIME_MS) goto lose;
        uint32_t left = (ENTRY_TIME_MS - (now_ms - start_ms) + 999)/1000;
        float ang = map_pot_to_degrees(read_pot_raw());
        bool in = (ang>=ts && ang<=te);
        set_led(!in, in);
        render_frame(ang, ts, te, left);
        if(in) break;
        sleep_ms(50);
    }
    // HOLD
    start_ms = time_us_32()/1000;
    while(1){
        uint32_t now_ms = time_us_32()/1000;
        if(now_ms - start_ms >= HOLD_TIME_MS) return;
        uint32_t left = (HOLD_TIME_MS - (now_ms - start_ms) + 999)/1000;
        float ang = map_pot_to_degrees(read_pot_raw());
        bool in = (ang>=ts && ang<=te);
        set_led(!in, in);
        render_frame(ang, ts, te, left);
        if(!in) goto lose;
        sleep_ms(50);
    }
lose:
    framed("YOU DIED!");
    set_led(true,false);
    sleep_ms(2000);
    while(1) tight_loop_contents();
}

// ─── Main ─────────────────────────────────────────────────────
int main(void){
    stdio_init_all();
    sleep_ms(50);

    // OLED init exactly like doom_V8.c
    gpio_set_function(16, GPIO_FUNC_I2C);
    gpio_set_function(17, GPIO_FUNC_I2C);
    gpio_pull_up(16);
    gpio_pull_up(17);
    i2c_init(i2c_default,100000);
    oled_init();

    // ADC
    adc_init_pot();

    // Button
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // LED init – both off
    gpio_init(LED_R); gpio_set_dir(LED_R, GPIO_OUT);
    gpio_init(LED_G); gpio_set_dir(LED_G, GPIO_OUT);
    set_led(false,false);

    // PRESS TO START
    framed("PRESS TO START");
    wait_for_button();

    // 3-2-1 COUNTDOWN
    for(int i=3;i>=1;i--){
        char t[2]={(char)('0'+i),0};
        framed(t);
        sleep_ms(1000);
    }
    framed("GO!");

    // now turn on RED LED as play begins
    set_led(true,false);

    // FIVE ROUNDS with unique positions
    srand(time_us_32());
    int used[NUM_ROUNDS] = { -1,-1,-1,-1,-1 };
    for(int r=0;r<NUM_ROUNDS;r++){
        int s_i;
        do {
            s_i = rand() % (181 - TARGET_ZONE_WIDTH);
        } while (memchr(used, s_i, r * sizeof(int)));
        used[r] = s_i;
        float s = (float)s_i;
        float e = s + TARGET_ZONE_WIDTH;
        game_round(s,e);
    }

    // YOU WON
    framed("YOU WON!");
    set_led(false,true);
    while(1) tight_loop_contents();
}
