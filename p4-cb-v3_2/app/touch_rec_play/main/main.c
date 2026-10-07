/* touch_rec_play - touch UI for 10-second record & play on the
 * ESP32-P4 CB V3.2 (3.5" WKS35HV035-WCT MIPI-DSI touch display,
 * ES8311 codec + NS4150B PA, MEMS mic).
 *
 * State machine (per the requirements):
 *   after reset : only 'Rec' touch-able
 *   touch 'Rec' : both buttons un-touch-able, 10 s recording starts,
 *                 progress bar runs (red)
 *   record done : progress bar changes color (green), 'Play' touch-able
 *   touch 'Play': both un-touch-able, playback runs, progress bar runs (blue)
 *   play done   : back to the initial status (only 'Rec' touch-able)
 *
 * The recording is kept in PSRAM (10 s @ 16 kHz, 16-bit mono = 320 KB);
 * no SD card is needed. Audio paths: mic -> ES8311 -> I2S0 (RX) and
 * I2S0 (TX) -> ES8311 -> NS4150B PA (GPIO 11). Touch: GT9xx on I2C0.
 *
 * Vendor sources: 25_recoding (I2S/ES8311/record), 24_music (playback),
 * 13_mipilcd_touch_screen (LCD + GT9xx touch).
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "lcd.h"
#include "led.h"
#include "touch.h"
#include "myi2s.h"
#include "myes8311.h"

static const char *TAG = "touch_rec_play";

/* ---------------- record buffer: 10 s @ 16 kHz, 16-bit mono -------------- */
#define REC_RATE       16000
#define REC_SECONDS    10
#define REC_TOTAL      (REC_RATE * 2 * REC_SECONDS)     /* 320,000 bytes */
#define CHUNK_SIZE     4800                              /* 1200 samples = 75 ms */

static int16_t *s_rec_buf = NULL;

/* ----------------------------------- UI ----------------------------------- */
/* 3.5" panel, portrait: lcddev.width = 320, lcddev.height = 480 */
#define UI_W      320
#define REC_X0    70
#define REC_Y0    110
#define REC_X1    250
#define REC_Y1    190
#define PLAY_X0   70
#define PLAY_Y0   250
#define PLAY_X1   250
#define PLAY_Y1   330
#define BAR_X0    30
#define BAR_Y0    385
#define BAR_X1    290
#define BAR_Y1    415
#define BAR_W     (BAR_X1 - BAR_X0)

#define COL_BTN_OFF   0x7bef   /* light gray: button disabled */
#define COL_REC       RED
#define COL_PLAY      0x1c9f   /* dim blue */
#define COL_DONE      GREEN

static bool hit(int x, int y, int x0, int y0, int x1, int y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static void button(bool rec_on, bool play_on)
{
    lcd_fill(REC_X0, REC_Y0, REC_X1, REC_Y1, rec_on ? COL_REC : COL_BTN_OFF);
    lcd_show_string(REC_X0 + 80, REC_Y0 + 32, 100, 16, 16,
                    (char *)"Rec", rec_on ? WHITE : BLACK);

    lcd_fill(PLAY_X0, PLAY_Y0, PLAY_X1, PLAY_Y1, play_on ? COL_PLAY : COL_BTN_OFF);
    lcd_show_string(PLAY_X0 + 74, PLAY_Y0 + 32, 100, 16, 16,
                    (char *)"Play", play_on ? WHITE : BLACK);
}

static void progress(float frac, uint16_t color)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fill = (int)(BAR_W * frac);
    /* lcd_fill rejects degenerate areas (sx >= ex): guard both halves so the
     * 0% and 100% cases never draw an empty rectangle */
    if (fill >= 2) {
        lcd_fill(BAR_X0, BAR_Y0, BAR_X0 + fill, BAR_Y1, color);
    }
    if (BAR_X1 - (BAR_X0 + fill) >= 2) {
        lcd_fill(BAR_X0 + fill, BAR_Y0, BAR_X1, BAR_Y1, WHITE);
    }
    lcd_draw_rectangle(BAR_X0, BAR_Y0, BAR_X1, BAR_Y1, BLACK);
}

static void status_text(const char *s, uint16_t color)
{
    lcd_fill(30, 440, UI_W - 30, 456, WHITE);
    lcd_show_string(30, 440, UI_W - 60, 16, 16, (char *)s, color);
}

static void draw_initial(void)
{
    lcd_clear(WHITE);
    lcd_show_string(60, 30, 240, 16, 16, (char *)"ESP32-P4 CB V3.2", RED);
    lcd_show_string(60, 55, 240, 16, 16, (char *)"Touch Rec & Play", RED);
    button(true, false);
    progress(0, WHITE);
    status_text("touch Rec to record 10 s", BLUE);
}

/* ------------------------------ record / play ----------------------------- */

static void do_record(void)
{
    status_text("recording...", RED);
    i2s_set_samplerate_bits_sample(REC_RATE, 16);
    i2s_trx_start();

    uint32_t got = 0;
    while (got < REC_TOTAL) {
        uint32_t chunk = REC_TOTAL - got;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;
        size_t n = i2s_rx_read((uint8_t *)s_rec_buf + got, chunk);
        if (n == 0) break;
        got += n;
        progress((float)got / REC_TOTAL, COL_REC);
    }
    i2s_trx_stop();
    ESP_LOGI(TAG, "recorded %lu bytes in 10 s", (unsigned long)got);
}

static void do_play(void)
{
    status_text("playing...", BLUE);
    gpio_set_level(PA_CTRL_GPIO_PIN, 1);   /* PA on */
    i2s_set_samplerate_bits_sample(REC_RATE, 16);
    i2s_trx_start();

    uint32_t sent = 0;
    while (sent < REC_TOTAL) {
        uint32_t chunk = REC_TOTAL - sent;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;
        size_t n = i2s_tx_write((uint8_t *)s_rec_buf + sent, chunk);
        if (n == 0) break;
        sent += n;
        progress((float)sent / REC_TOTAL, COL_PLAY);
    }
    i2s_trx_stop();
    gpio_set_level(PA_CTRL_GPIO_PIN, 0);   /* PA off */
    ESP_LOGI(TAG, "played %lu bytes", (unsigned long)sent);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    led_init();
    lcd_init();

    if (myi2s_init() != ESP_OK) {
        lcd_show_string(30, 110, 240, 16, 16, (char *)"I2S Error", RED);
        return;
    }

    /* codec and touch share the physical I2C0 wires (GPIO 28/29) but use
     * separate master-bus objects: the codec gets its own bus here, the
     * touch attaches via tp_init afterwards (myiic). The codec is only
     * accessed during this init, the touch is not scanned during
     * record/playback, so the two never collide. */
    while (myes8311_init()) {
        lcd_show_string(30, 130, 240, 16, 16, (char *)"ES8311 Error", RED);
        vTaskDelay(pdMS_TO_TICKS(200));
        lcd_fill(30, 130, 280, 146, WHITE);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    gpio_set_level(PA_CTRL_GPIO_PIN, 0);   /* PA off by default */

    /* touch last: attaches the GT911 device to the shared bus */
    ESP_ERROR_CHECK(tp_init());

    s_rec_buf = heap_caps_malloc(REC_TOTAL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rec_buf) {
        lcd_show_string(30, 130, 240, 16, 16, (char *)"PSRAM alloc failed", RED);
        ESP_LOGE(TAG, "record buffer alloc failed");
        return;
    }

    draw_initial();
    ESP_LOGI(TAG, "touch_rec_play ready (buffer %d bytes in PSRAM)", REC_TOTAL);

    bool has_recording = false;
    bool rec_down = false, play_down = false;

    while (1) {
        tp_dev.scan(0);
        bool pressed = (tp_dev.sta & TP_PRES_DOWN) && (tp_dev.sta & 0x3f);
        int x = tp_dev.x[0], y = tp_dev.y[0];

        /* Rec: touch-able unless a record/play process is running */
        if (pressed && !rec_down &&
            hit(x, y, REC_X0, REC_Y0, REC_X1, REC_Y1)) {
            rec_down = true;
            /* both buttons become un-touch-able: the loop below blocks touch
             * scanning for the whole 10 s process */
            button(false, false);
            do_record();
            has_recording = true;
            /* recording complete: progress bar changes color, Play touch-able */
            progress(1.0f, COL_DONE);
            button(true, true);
            status_text("recorded - touch Play", BLUE);
            vTaskDelay(pdMS_TO_TICKS(200));   /* debounce */
            continue;
        }

        /* Play: only touch-able with a recording, never during a process */
        if (pressed && !play_down && has_recording &&
            hit(x, y, PLAY_X0, PLAY_Y0, PLAY_X1, PLAY_Y1)) {
            play_down = true;
            button(false, false);
            do_play();
            /* play done: back to the initial status (only Rec touch-able) */
            button(true, false);
            status_text("touch Rec to record 10 s", BLUE);
            vTaskDelay(pdMS_TO_TICKS(200));   /* debounce */
            continue;
        }

        rec_down = pressed && hit(x, y, REC_X0, REC_Y0, REC_X1, REC_Y1);
        play_down = pressed && hit(x, y, PLAY_X0, PLAY_Y0, PLAY_X1, PLAY_Y1);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
