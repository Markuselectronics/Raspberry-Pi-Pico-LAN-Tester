#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

#include "ssd1306.h"

#include "socket.h"
#include "wizchip_conf.h"
#include "DHCP/dhcp.h"

// =============================================================================
// Hardware configuration
// =============================================================================

#define PIN_CHG_STAT       0   // BQ24074 CHG, active low, open drain

#define SPI_PORT           spi0
#define PIN_W5500_SCLK     2
#define PIN_W5500_MOSI     3
#define PIN_W5500_MISO     4
#define PIN_W5500_CS       5   // GPIO6 is reserved for SSD1306 SDA; do not share it.
#define PIN_W5500_RST      21
#define PIN_W5500_INT      22

#define I2C_PORT           i2c1
#define PIN_DISP_SDA       6
#define PIN_DISP_SCL       7
#define SSD1306_ADDR       0x3C

#define PIN_BTN_START      19

#define ADC_CHANNEL_VSYS   3
#define ADC_PIN_VSYS       29

#if PIN_W5500_CS == PIN_DISP_SDA || PIN_W5500_CS == PIN_DISP_SCL
#error "W5500 CS must not share the SSD1306 I2C pins. Move W5500 CS to a free GPIO."
#endif

#define DHCP_SOCKET        0
#define PROBE_SOCKET       1
#define DHCP_BUFFER_SIZE   2048
#define TCP_PROBE_TIMEOUT_MS 2500
#define W5500_SPI_BAUD     (2 * 1000 * 1000)
#define BATTERY_LAN_MIN_V  3.45f

// =============================================================================
// Application state
// =============================================================================

typedef enum {
    LAN_RESULT_IDLE,
    LAN_RESULT_OK,
    LAN_RESULT_NO_LINK,
    LAN_RESULT_NO_CHIP,
    LAN_RESULT_DHCP_FAIL,
    LAN_RESULT_NET_ONLY,
} lan_result_t;

typedef struct {
    wiz_NetInfo net;
    lan_result_t result;
    uint8_t chip_version;
    uint8_t phy_link;
    uint8_t phy_speed_100;
    uint8_t phy_full_duplex;
    uint32_t tcp_ms;
    bool dhcp_ok;
    bool internet_ok;
} lan_test_t;

static volatile bool flag_start_lan_test = false;
static volatile uint32_t counter_1ms = 0;
static volatile uint8_t btn_debounce_counter = 0;
static volatile bool btn_was_pressed = false;

static ssd1306_t disp;
static uint8_t dhcp_buffer[DHCP_BUFFER_SIZE];
static uint16_t next_local_port = 49152;
static uint32_t wiz_irq_state = 0;

static float current_battery_v = 0.0f;
static uint8_t current_battery_pct = 0;
static bool is_charging = false;
static char ui_lines[5][22] = {
    "Ready",
    "Press button",
    "",
    "",
    ""
};

// =============================================================================
// Small utilities
// =============================================================================

static void ui_set_line(uint8_t line, const char *fmt, ...) {
    if (line >= 5) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(ui_lines[line], sizeof(ui_lines[line]), fmt, args);
    va_end(args);
}

static bool ip_is_zero(const uint8_t ip[4]) {
    return (ip[0] | ip[1] | ip[2] | ip[3]) == 0;
}

static void ip_to_str(const uint8_t ip[4], char *out, size_t out_len) {
    snprintf(out, out_len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static uint8_t battery_percent_from_vsys(float volts) {
    if (volts >= 4.20f) {
        return 100;
    }
    if (volts <= 3.30f) {
        return 0;
    }
    return (uint8_t)(((volts - 3.30f) * 100.0f) / (4.20f - 3.30f));
}

static float read_battery_voltage(void) {
    adc_select_input(ADC_CHANNEL_VSYS);
    sleep_us(50);
    uint32_t total = 0;
    for (uint8_t i = 0; i < 8; i++) {
        total += adc_read();
    }

    const float conversion_factor = 3.3f / 4095.0f;
    return ((float)total / 8.0f) * conversion_factor * 3.0f;
}

static void update_power_status(void) {
    current_battery_v = read_battery_voltage();
    current_battery_pct = battery_percent_from_vsys(current_battery_v);
    is_charging = gpio_get(PIN_CHG_STAT) == 0;
}

// =============================================================================
// Display
// =============================================================================

static void draw_battery_icon(uint8_t pct, bool charging) {
    const uint8_t x = 104;
    const uint8_t y = 1;
    const uint8_t w = 18;
    const uint8_t h = 8;

    ssd1306_draw_empty_square(&disp, x, y, w, h);
    ssd1306_draw_square(&disp, x + w, y + 2, 2, 4);

    uint8_t fill = (uint8_t)((pct * (w - 4)) / 100);
    if (fill > 0) {
        ssd1306_draw_square(&disp, x + 2, y + 2, fill, h - 4);
    }

    if (charging) {
        ssd1306_draw_line(&disp, x + 8, y + 1, x + 5, y + 5);
        ssd1306_draw_line(&disp, x + 5, y + 5, x + 10, y + 5);
        ssd1306_draw_line(&disp, x + 10, y + 5, x + 7, y + 8);
    }
}

static void update_display_ui(void) {
    char top[22];
    ssd1306_clear(&disp);

    if (is_charging) {
        snprintf(top, sizeof(top), "Charging %.2fV", current_battery_v);
    } else {
        snprintf(top, sizeof(top), "Battery %u%% %.1fV", current_battery_pct, current_battery_v);
    }
    ssd1306_draw_string(&disp, 0, 0, 1, top);
    draw_battery_icon(current_battery_pct, is_charging);
    ssd1306_draw_line(&disp, 0, 11, 127, 11);

    for (uint8_t i = 0; i < 5; i++) {
        ssd1306_draw_string(&disp, 0, 14 + (i * 10), 1, ui_lines[i]);
    }

    ssd1306_show(&disp);
}

// =============================================================================
// W5500 low-level callbacks
// =============================================================================

static void wiz_cris_enter(void) {
    wiz_irq_state = save_and_disable_interrupts();
}

static void wiz_cris_exit(void) {
    restore_interrupts(wiz_irq_state);
}

static void wiz_select(void) {
    gpio_put(PIN_W5500_CS, 0);
}

static void wiz_deselect(void) {
    gpio_put(PIN_W5500_CS, 1);
}

static uint8_t wiz_spi_read_byte(void) {
    uint8_t tx = 0xFF;
    uint8_t rx = 0;
    spi_write_read_blocking(SPI_PORT, &tx, &rx, 1);
    return rx;
}

static void wiz_spi_write_byte(uint8_t tx) {
    spi_write_blocking(SPI_PORT, &tx, 1);
}

static void wiz_spi_read_burst(uint8_t *buf, uint16_t len) {
    memset(buf, 0xFF, len);
    spi_read_blocking(SPI_PORT, 0xFF, buf, len);
}

static void wiz_spi_write_burst(uint8_t *buf, uint16_t len) {
    spi_write_blocking(SPI_PORT, buf, len);
}

static void reset_w5500(void) {
    gpio_put(PIN_W5500_RST, 0);
    sleep_ms(20);
    gpio_put(PIN_W5500_RST, 1);
    sleep_ms(200);
}

static void make_local_mac(uint8_t mac[6]) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    mac[0] = 0x02;  // locally administered, unicast
    mac[1] = 0x08;
    mac[2] = 0xDC;  // WIZnet OUI style prefix, locally administered by mac[0]
    mac[3] = board_id.id[5];
    mac[4] = board_id.id[6];
    mac[5] = board_id.id[7];
}

static bool w5500_init_network_core(void) {
    reg_wizchip_cris_cbfunc(wiz_cris_enter, wiz_cris_exit);
    reg_wizchip_cs_cbfunc(wiz_select, wiz_deselect);
    reg_wizchip_spi_cbfunc(wiz_spi_read_byte, wiz_spi_write_byte);
    reg_wizchip_spiburst_cbfunc(wiz_spi_read_burst, wiz_spi_write_burst);

    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        reset_w5500();

        uint8_t tx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
        uint8_t rx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
        if (wizchip_init(tx_size, rx_size) == 0 && getVERSIONR() == 0x04) {
            wiz_NetTimeout timeout = {
                .retry_cnt = 2,
                .time_100us = 3000,
            };
            ctlnetwork(CN_SET_TIMEOUT, &timeout);
            return true;
        }

        sleep_ms(250);
    }

    return false;
}

static void w5500_apply_blank_netinfo(void) {
    wiz_NetInfo net = {0};
    make_local_mac(net.mac);
    net.dhcp = NETINFO_DHCP;
    ctlnetwork(CN_SET_NETINFO, &net);
}

static bool w5500_get_link(lan_test_t *test) {
    uint8_t link = PHY_LINK_OFF;
    if (ctlwizchip(CW_GET_PHYLINK, &link) != 0) {
        test->phy_link = PHY_LINK_OFF;
        return false;
    }

    test->phy_link = link;
    if (link == PHY_LINK_ON) {
        wiz_PhyConf phy = {0};
        ctlwizchip(CW_GET_PHYSTATUS, &phy);
        test->phy_speed_100 = phy.speed == PHY_SPEED_100;
        test->phy_full_duplex = phy.duplex == PHY_DUPLEX_FULL;
    }

    return link == PHY_LINK_ON;
}

static bool run_dhcp(lan_test_t *test) {
    DHCP_stop();
    w5500_apply_blank_netinfo();
    DHCP_init(DHCP_SOCKET, dhcp_buffer);

    absolute_time_t start = get_absolute_time();
    uint32_t last_ui_ms = 0;

    while (absolute_time_diff_us(start, get_absolute_time()) < 22000000) {
        if (!w5500_get_link(test)) {
            DHCP_stop();
            return false;
        }

        uint8_t state = DHCP_run();
        if (state == DHCP_IP_ASSIGN || state == DHCP_IP_CHANGED || state == DHCP_IP_LEASED) {
            getIPfromDHCP(test->net.ip);
            getGWfromDHCP(test->net.gw);
            getSNfromDHCP(test->net.sn);
            getDNSfromDHCP(test->net.dns);
            make_local_mac(test->net.mac);
            test->net.dhcp = NETINFO_DHCP;
            ctlnetwork(CN_SET_NETINFO, &test->net);
            test->dhcp_ok = !ip_is_zero(test->net.ip);
            return test->dhcp_ok;
        }

        if (state == DHCP_FAILED) {
            DHCP_stop();
            return false;
        }

        uint32_t elapsed_ms = absolute_time_diff_us(start, get_absolute_time()) / 1000;
        if (elapsed_ms - last_ui_ms >= 1000) {
            last_ui_ms = elapsed_ms;
            ui_set_line(0, "LAN: Link OK");
            ui_set_line(1, "DHCP... %lus", elapsed_ms / 1000);
            ui_set_line(2, "");
            ui_set_line(3, "");
            ui_set_line(4, "");
            update_display_ui();
        }
        sleep_ms(20);
    }

    DHCP_stop();
    return false;
}

static bool tcp_probe(const uint8_t ip[4], uint16_t port, uint32_t *elapsed_ms) {
    close(PROBE_SOCKET);

    if (socket(PROBE_SOCKET, Sn_MR_TCP, next_local_port++, SF_IO_NONBLOCK) != PROBE_SOCKET) {
        close(PROBE_SOCKET);
        return false;
    }

    absolute_time_t start = get_absolute_time();
    int8_t rc = connect(PROBE_SOCKET, (uint8_t *)ip, port);
    if (rc != SOCK_BUSY && rc != SOCK_OK) {
        close(PROBE_SOCKET);
        return false;
    }

    while (absolute_time_diff_us(start, get_absolute_time()) < TCP_PROBE_TIMEOUT_MS * 1000) {
        uint8_t status = getSn_SR(PROBE_SOCKET);
        if (status == SOCK_ESTABLISHED) {
            *elapsed_ms = absolute_time_diff_us(start, get_absolute_time()) / 1000;
            disconnect(PROBE_SOCKET);
            close(PROBE_SOCKET);
            return true;
        }

        if ((getSn_IR(PROBE_SOCKET) & Sn_IR_TIMEOUT) || status == SOCK_CLOSED) {
            setSn_IR(PROBE_SOCKET, Sn_IR_TIMEOUT);
            close(PROBE_SOCKET);
            return false;
        }
        sleep_ms(10);
    }

    close(PROBE_SOCKET);
    return false;
}

static bool test_internet(lan_test_t *test) {
    static const uint8_t cloudflare_http[4] = {1, 1, 1, 1};
    static const uint8_t google_dns[4] = {8, 8, 8, 8};

    if (tcp_probe(cloudflare_http, 80, &test->tcp_ms)) {
        return true;
    }
    return tcp_probe(google_dns, 53, &test->tcp_ms);
}

static lan_test_t run_lan_test(void) {
    lan_test_t test = {0};
    test.result = LAN_RESULT_IDLE;
    test.chip_version = getVERSIONR();

    ui_set_line(0, "Preparing test");
    ui_set_line(1, "");
    ui_set_line(2, "");
    ui_set_line(3, "");
    ui_set_line(4, "");
    update_display_ui();

    if (test.chip_version != 0x04) {
        test.result = LAN_RESULT_NO_CHIP;
        return test;
    }

    ui_set_line(0, "LAN cable test");
    ui_set_line(1, "Checking link...");
    update_display_ui();

    absolute_time_t start = get_absolute_time();
    while (!w5500_get_link(&test)) {
        if (absolute_time_diff_us(start, get_absolute_time()) > 3000000) {
            test.result = LAN_RESULT_NO_LINK;
            return test;
        }
        sleep_ms(100);
    }

    if (!run_dhcp(&test)) {
        test.result = LAN_RESULT_DHCP_FAIL;
        return test;
    }

    char ip[16];
    char gw[16];
    ip_to_str(test.net.ip, ip, sizeof(ip));
    ip_to_str(test.net.gw, gw, sizeof(gw));
    ui_set_line(0, "IP %s", ip);
    ui_set_line(1, "GW %s", gw);
    ui_set_line(2, "Internet check");
    ui_set_line(3, "");
    ui_set_line(4, "");
    update_display_ui();

    test.internet_ok = test_internet(&test);
    test.result = test.internet_ok ? LAN_RESULT_OK : LAN_RESULT_NET_ONLY;
    return test;
}

static void show_lan_result(const lan_test_t *test) {
    char ip[16];
    char gw[16];
    char dns[16];

    switch (test->result) {
    case LAN_RESULT_NO_CHIP:
        ui_set_line(0, "Hardware fault");
        ui_set_line(1, "Ethernet chip");
        ui_set_line(2, "not responding");
        ui_set_line(3, "Check SPI/CS/RST");
        ui_set_line(4, "Ver 0x%02X", test->chip_version);
        break;

    case LAN_RESULT_NO_LINK:
        ui_set_line(0, "No LAN link");
        ui_set_line(1, "Connect cable");
        ui_set_line(2, "or check switch");
        ui_set_line(3, "Internet: no");
        ui_set_line(4, "");
        break;

    case LAN_RESULT_DHCP_FAIL:
        ui_set_line(0, "Network found");
        ui_set_line(1, "%s %s", test->phy_speed_100 ? "100M" : "10M",
                    test->phy_full_duplex ? "Full" : "Half");
        ui_set_line(2, "DHCP failed");
        ui_set_line(3, "No IP address");
        ui_set_line(4, "");
        break;

    case LAN_RESULT_OK:
    case LAN_RESULT_NET_ONLY:
        ip_to_str(test->net.ip, ip, sizeof(ip));
        ip_to_str(test->net.gw, gw, sizeof(gw));
        ip_to_str(test->net.dns, dns, sizeof(dns));
        ui_set_line(0, test->internet_ok ? "Internet online" : "Network only");
        ui_set_line(1, "IP %s", ip);
        ui_set_line(2, "GW %s", gw);
        ui_set_line(3, "DNS %s", dns);
        if (test->internet_ok) {
            ui_set_line(4, "%s %s %lums", test->phy_speed_100 ? "100M" : "10M",
                        test->phy_full_duplex ? "Full" : "Half", test->tcp_ms);
        } else {
            ui_set_line(4, "No internet route");
        }
        break;

    default:
        ui_set_line(0, "Ready to test");
        ui_set_line(1, "Connect LAN cable");
        ui_set_line(2, "Press button");
        ui_set_line(3, "");
        ui_set_line(4, "");
        break;
    }

    update_display_ui();
}

// =============================================================================
// Timer and setup
// =============================================================================

static bool cyclic_1ms_task(struct repeating_timer *t) {
    (void)t;
    counter_1ms++;

    static uint16_t dhcp_ms = 0;
    if (++dhcp_ms >= 1000) {
        dhcp_ms = 0;
        DHCP_time_handler();
    }

    if (gpio_get(PIN_BTN_START) == 0) {
        if (btn_debounce_counter < 50) {
            btn_debounce_counter++;
        } else if (!btn_was_pressed) {
            btn_was_pressed = true;
            flag_start_lan_test = true;
        }
    } else {
        btn_debounce_counter = 0;
        btn_was_pressed = false;
    }

    return true;
}

static void init_gpio(void) {
    gpio_init(PIN_CHG_STAT);
    gpio_set_dir(PIN_CHG_STAT, GPIO_IN);
    gpio_pull_up(PIN_CHG_STAT);

    gpio_init(PIN_BTN_START);
    gpio_set_dir(PIN_BTN_START, GPIO_IN);
    gpio_pull_up(PIN_BTN_START);

    gpio_init(PIN_W5500_CS);
    gpio_set_dir(PIN_W5500_CS, GPIO_OUT);
    gpio_put(PIN_W5500_CS, 1);

    gpio_init(PIN_W5500_RST);
    gpio_set_dir(PIN_W5500_RST, GPIO_OUT);
    gpio_put(PIN_W5500_RST, 1);

    gpio_init(PIN_W5500_INT);
    gpio_set_dir(PIN_W5500_INT, GPIO_IN);
    gpio_pull_up(PIN_W5500_INT);
}

static void init_display(void) {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PIN_DISP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_DISP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_DISP_SDA);
    gpio_pull_up(PIN_DISP_SCL);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, SSD1306_ADDR, I2C_PORT);
    ssd1306_contrast(&disp, 0x5F);
}

static void init_w5500_spi(void) {
    spi_init(SPI_PORT, W5500_SPI_BAUD);
    gpio_set_function(PIN_W5500_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_W5500_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_W5500_MISO, GPIO_FUNC_SPI);
}

static void init_adc(void) {
    adc_init();
    adc_gpio_init(ADC_PIN_VSYS);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1000);

    init_gpio();
    init_adc();
    update_power_status();
    init_display();

    ui_set_line(0, "LAN Tester");
    ui_set_line(1, "Starting...");
    ui_set_line(2, "");
    ui_set_line(3, "");
    ui_set_line(4, "");
    update_display_ui();

    init_w5500_spi();
    bool w5500_ok = w5500_init_network_core();

    struct repeating_timer timer;
    add_repeating_timer_ms(-1, cyclic_1ms_task, NULL, &timer);

    if (w5500_ok) {
        ui_set_line(0, "Ready to test");
        ui_set_line(1, "Connect LAN cable");
        ui_set_line(2, "Press button");
        ui_set_line(3, "");
        ui_set_line(4, "");
    } else {
        ui_set_line(0, "Hardware fault");
        ui_set_line(1, "Ethernet chip");
        ui_set_line(2, "not responding");
        ui_set_line(3, "Press to retry");
        ui_set_line(4, "");
    }
    update_display_ui();

    while (true) {
        if (flag_start_lan_test) {
            flag_start_lan_test = false;
            update_power_status();

            if (!is_charging && current_battery_v < BATTERY_LAN_MIN_V) {
                ui_set_line(0, "Battery too low");
                ui_set_line(1, "%.2fV", current_battery_v);
                ui_set_line(2, "Charge before test");
                ui_set_line(3, "LAN test skipped");
                ui_set_line(4, "");
                update_display_ui();
                continue;
            }

            if (!w5500_ok) {
                w5500_ok = w5500_init_network_core();
            }

            if (w5500_ok) {
                lan_test_t test = run_lan_test();
                show_lan_result(&test);
            } else {
                lan_test_t test = {
                    .result = LAN_RESULT_NO_CHIP,
                    .chip_version = getVERSIONR(),
                };
                show_lan_result(&test);
            }
        }

        if (counter_1ms >= 1000) {
            counter_1ms = 0;
            update_power_status();
            update_display_ui();
        }

        tight_loop_contents();
    }
}
