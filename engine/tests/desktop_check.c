/* SPDX-License-Identifier: GPL-3.0-only */
/* A quick check of the Android platform layer on a computer, without a phone: the engine on its own thread exactly as
 * the app runs it, pulled for audio in real time as Oboe would, a few keys pressed. Out come a WAV and a screen.
 *
 *   engine/tests/desktop_check.sh      (-> build/desktop/quorum.wav, build/desktop/screen.ppm) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void quorum_start(const char *dir);
void quorum_pull(float *out, int32_t n);
void quorum_keys(uint32_t mask);
void quorum_button(int32_t id, int32_t down);
uint32_t quorum_underruns(void);
void quorum_copy_screen(uint32_t *argb);
void quorum_crt(uint8_t *intensity, uint8_t *rgba, int32_t *gain);

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/desktop";
    enum { FR = 192, SECS = 8 };
    static float buf[FR * 2];
    static uint32_t scr[240 * 240];
    uint32_t total = 44100u * SECS, done = 0, i, bytes, v[] = {16, 0x00020001u, 44100u, 44100u * 4u, 0x00100004u};
    int16_t *wav = malloc((total + FR) * 4u);
    double peak = 0;
    char name[512];
    FILE *f;
    quorum_start(out);
    usleep(300000);
    while (done < total) {
        uint32_t t = done / 441u;               /* (centiseconds) */
        quorum_keys(t >= 300 && t < 500 ? (1u << 7 | 1u << 11) : 0u);
        if (t == 550) quorum_button(12, 1);      /* PLAY */
        if (t == 552) quorum_button(12, 0);
        quorum_pull(buf, FR);
        for (i = 0; i < FR * 2u; i++) {
            float s = buf[i];
            if (s > peak) peak = s; else if (-s > peak) peak = -s;
            wav[done * 2u + i] = (int16_t)(s * 32767.0f);
        }
        done += FR;
        usleep(FR * 1000000u / 44100u);         /* real time, as the device clock pulls */
    }
    snprintf(name, sizeof name, "%s/quorum.wav", out);
    f = fopen(name, "wb");
    bytes = total * 4u;
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, total, f); fclose(f);
    quorum_copy_screen(scr);
    snprintf(name, sizeof name, "%s/screen.ppm", out);
    f = fopen(name, "wb");
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) { fputc(scr[i] >> 16 & 255, f); fputc(scr[i] >> 8 & 255, f); fputc(scr[i] & 255, f); }
    fclose(f);
    {                                           /* what the phone's tube gets: intensity, ramp, gain */
        static uint8_t in[240 * 240], ramp[1024];
        int32_t gain[4];
        quorum_crt(in, ramp, gain);
        snprintf(name, sizeof name, "%s/crt.bin", out);
        f = fopen(name, "wb");
        fwrite(in, 1, sizeof in, f); fwrite(ramp, 1, sizeof ramp, f); fwrite(gain, 4, 2, f);
        fclose(f);
    }
    printf("peak %.3f, underruns %u -> %s/quorum.wav, screen.ppm\n", peak, quorum_underruns(), out);
    return peak > 0.0005 ? 0 : 1;
}
