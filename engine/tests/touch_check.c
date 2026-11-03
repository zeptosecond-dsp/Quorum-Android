/* SPDX-License-Identifier: GPL-3.0-only */
/* The touch layer on a computer: taps and drags as a finger would make them (Index, a page's part, a value's slide
 * bar, fine steps, the VDU page), the screen saved after each: build/desktop/touch-*.ppm.
 *   engine/tests/desktop_check.sh builds and runs it too. */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
void quorum_start(const char *dir);
void quorum_pull(float *out, int32_t n);
void quorum_touch(int32_t act, float x, float y);
void quorum_copy_screen(uint32_t *argb);
static float buf[512];
static void run(int ms){ for(int t=0;t<ms;t+=4){ quorum_pull(buf,176); usleep(4000);} }
static void shot(const char *n){ static uint32_t s[57600]; quorum_copy_screen(s); FILE*f=fopen(n,"wb"); fprintf(f,"P6 240 240 255\n"); for(int i=0;i<57600;i++){fputc(s[i]>>16&255,f);fputc(s[i]>>8&255,f);fputc(s[i]&255,f);} fclose(f);}
static void tap(float x,float y){ quorum_touch(0,x,y); run(60); quorum_touch(2,x,y); run(150);}
int main(){
  quorum_start("build/desktop"); run(3500);
  shot("build/desktop/touch-1_index.ppm");
  tap(100, 22+2+3*16+8);            /* 5 SPLIT + FILTERS */
  tap(150, 10);                     /* header title: next part -> PATH A */
  shot("build/desktop/touch-2_patha.ppm");
  tap(90, 22+175);                  /* K2: FREQ */
  quorum_touch(0, 120, 22+120); run(50);
  for(int i=0;i<30;i++){ quorum_touch(1, 120+i*1.5f, 22+120); run(20);} 
  run(200);
  shot("build/desktop/touch-3_bar.ppm");
  for(int i=0;i<20;i++){ quorum_touch(1, 165+i*1.5f, 22+120-30); run(20);}   /* away from the bar: fine */
  run(150); shot("build/desktop/touch-4_fine.ppm");
  quorum_touch(2, 195, 112); run(100);
  tap(10, 10);                      /* PAGE n: index */
  tap(100, 22+2+10*16+8);           /* G GLOBAL */
  tap(150, 10);                     /* -> VDU */
  tap(100, 22+2+2*18+8);            /* SCANLINES: cycles */
  tap(100, 22+2+3*18+8);            /* DARKNESS: bar */
  run(200); shot("build/desktop/touch-5_vdu.ppm");
  return 0;
}
