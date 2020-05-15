#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "driver/ledc.h"

#include "driver/i2c.h"
#include "sdkconfig.h"

#define I2C_ADDR_MAX30102      0x57 //max30102 i2c address
#define i2c_port                  0
#define i2c_frequency       800000
#define i2c_gpio_sda             GPIO_NUM_22
#define i2c_gpio_scl             GPIO_NUM_21

#include "./interfaces/i2c.c"

#define SSD1306_RESET_PIN      GPIO_NUM_19
#define I2C_ADDR_SSD1306       0x3d

#include "./devices/ssd1306.c"

#define EXAMPLE_WIFI_SSID "MountPleasantLAN"
#define EXAMPLE_WIFI_PASS "lastdayinparadise"
#define PORT 80

#include "./interfaces/wifisetup.c"

#include "websocket_server.h"

//globals
//char outStr[1500];
float meastime;
int countedsamples = 0;
int irpower = 0, rpower = 0, lirpower = 0, lrpower = 0;
int startstop = 0, raworbp = 1;
float heartrate = 99.2, pctspo2 = 99.2;
//long avgR, avgIR;
//int adc_read_ptr = 0;
//long adc_read[600];
//int hrstart, hrinterval;
//float redsumsq, redtemp=0, irsumsq, irtemp=0;
//float lastred = 0;

uint8_t data_buf_read_idx = 0;
uint8_t data_buf_write_idx = 0;
float data_buf[400]; // Store red and ir in even and odd indices respectively

static QueueHandle_t client_queue;
const static int client_queue_size = 100;

static ledc_channel_config_t ledc_channel;

static float read_data_buf() {
    float read = data_buf[data_buf_read_idx];
    data_buf_read_idx = (data_buf_read_idx + 1) % sizeof(data_buf);
    return read;
}

static void write_data_buf(float value) {
    data_buf[data_buf_write_idx] = value;
    data_buf_write_idx = (data_buf_write_idx + 1) % sizeof(data_buf);
}

static _Bool starts_with(const char *restrict string, const char *restrict prefix) {
    while (*prefix) {
        if (*prefix++ != *string++) {
            return 0;
        }
    }
    return 1;
}

static void send_file(int sock, uint8_t file_start[], uint8_t file_end[]) {
    int pkt_buf_size = 1500;
    uint pkt_end = pkt_buf_size;
    const int file_len = file_end - file_start;
    for (int pkt_ptr = 0; pkt_ptr < file_len; pkt_ptr = pkt_ptr + pkt_buf_size) {
        if ((file_len - pkt_ptr) < pkt_buf_size) pkt_end = file_len - pkt_ptr;
        send(sock, file_start + pkt_ptr, pkt_end, 0);
    }
}

void max30102_init() {
    uint8_t data;
    data = (0x2 << 5);  //sample averaging 0=1,1=2,2=4,3=8,4=16,5+=32
    i2c_write(I2C_ADDR_MAX30102, 0x08, data);
    data = 0x03;                //mode = red and ir samples
    i2c_write(I2C_ADDR_MAX30102, 0x09, data);
    data = (0x3 << 5) + (0x3 << 2) + 0x3; //first and last 0x3, middle smap rate 0=50,1=100,etc
    i2c_write(I2C_ADDR_MAX30102, 0x0a, data);
    data = 0xd0;                //ir pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0c, data);
    data = 0xa0;                //red pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0d, data);
}

_Noreturn void max30102_task() {
    int cnt, samp, tcnt = 0, cntr = 0;
    uint8_t rptr, wptr;
    uint8_t data;
    uint8_t regdata[256];
    //int irmeas, redmeas;
    float firxv[5], firyv[5], fredxv[5], fredyv[5];
    float lastmeastime = 0;
    float hrarray[10], spo2array[10];
    int hrarraycnt = 0;
    while (1) {
        if (lirpower != irpower) {
            data = (uint8_t) irpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0c, data);
            lirpower = irpower;
        }
        if (lrpower != rpower) {
            data = (uint8_t) rpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0d, data);
            lrpower = rpower;
        }
        i2c_read(I2C_ADDR_MAX30102, 0x04, &wptr, 1);
        i2c_read(I2C_ADDR_MAX30102, 0x06, &rptr, 1);

        samp = ((32 + wptr) - rptr) % 32;
        i2c_read(I2C_ADDR_MAX30102, 0x07, regdata, 6 * samp);
        //ESP_LOGI("samp","----  %d %d %d %d",  adc_read_ptr,samp,wptr,rptr);

        for (cnt = 0; cnt < samp; cnt++) {
            meastime = 0.01 * tcnt++;
            firxv[0] = firxv[1];
            firxv[1] = firxv[2];
            firxv[2] = firxv[3];
            firxv[3] = firxv[4];
            firxv[4] = (1 / 3.48311) *
                       (256 * 256 * (regdata[6 * cnt + 0] % 4) + 256 * regdata[6 * cnt + 1] + regdata[6 * cnt + 2]);
            firyv[0] = firyv[1];
            firyv[1] = firyv[2];
            firyv[2] = firyv[3];
            firyv[3] = firyv[4];
            firyv[4] = (firxv[0] + firxv[4]) - 2 * firxv[2]
                       + (-0.1718123813 * firyv[0]) + (0.3686645260 * firyv[1])
                       + (-1.1718123813 * firyv[2]) + (1.9738037992 * firyv[3]);

            fredxv[0] = fredxv[1];
            fredxv[1] = fredxv[2];
            fredxv[2] = fredxv[3];
            fredxv[3] = fredxv[4];
            fredxv[4] = (1 / 3.48311) *
                        (256 * 256 * (regdata[6 * cnt + 3] % 4) + 256 * regdata[6 * cnt + 4] + regdata[6 * cnt + 5]);
            fredyv[0] = fredyv[1];
            fredyv[1] = fredyv[2];
            fredyv[2] = fredyv[3];
            fredyv[3] = fredyv[4];
            fredyv[4] = (fredxv[0] + fredxv[4]) - 2 * fredxv[2]
                        + (-0.1718123813 * fredyv[0]) + (0.3686645260 * fredyv[1])
                        + (-1.1718123813 * fredyv[2]) + (1.9738037992 * fredyv[3]);

            //if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[3] < 100){
            //   heartrate = 60 / (meastime - lastmeastime);
            //   pctspo2 = 110 - 25 * ((fredyv[4]/fredxv[4]) / (firyv[4]/firxv[4]));
            //   printf ("%6.2f  %4.2f     hr= %5.1f     spo2= %5.1f\n", meastime, meastime - lastmeastime, heartrate, pctspo2);
            //   lastmeastime = meastime;
            //}

            //printf("%8.1f %8.1f %8.1f %8.3f\n", -1.0 * firyv[0], -1.0 * firyv[2], -1.0 * firyv[4], meastime-lastmeastime); 
            if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[2] > -1 * firyv[0] && -1.0 * firyv[2] > -1 * firyv[4] &&
                meastime - lastmeastime > 0.5) {
                hrarray[hrarraycnt % 5] = 60 / (meastime - lastmeastime);
                spo2array[hrarraycnt % 5] = 110 - 25 * ((fredyv[4] / fredxv[4]) / (firyv[4] / firxv[4]));
                if (spo2array[hrarraycnt % 5] > 100) spo2array[hrarraycnt % 5] = 99.9;
                printf("%6.2f  %4.2f     hr= %5.1f     spo2= %5.1f\n", meastime, meastime - lastmeastime, heartrate, pctspo2);
                lastmeastime = meastime;
                hrarraycnt++;
                heartrate = (hrarray[0] + hrarray[1] + hrarray[2] + hrarray[3] + hrarray[4]) / 5;
                if (heartrate < 40 || heartrate > 150) heartrate = 0;
                pctspo2 = (spo2array[0] + spo2array[1] + spo2array[2] + spo2array[3] + spo2array[4]) / 5;
                if (pctspo2 < 50 || pctspo2 > 101) pctspo2 = 0;
            }

            char tmp[32];
            countedsamples++;
//            int outStrLen = strlen(outStr);
//            if (outStrLen < (sizeof outStr) - 20) {
//            if (countedsamples < 100) {
//                if (raworbp == 0) {
//                    snprintf(tmp, sizeof tmp, "%5.1f,%5.1f,", -fredyv[4], -firyv[4]);
//                    strcat(outStr, tmp);
//                } else {
//                    snprintf(tmp, sizeof tmp, "%5.1f,%5.1f,", fredxv[4], firxv[4]);
//                    strcat(outStr, tmp);
//                }
//            }

            if (raworbp) {
                write_data_buf(-fredyv[4]);
                write_data_buf(-firyv[4]);
            } else {
                write_data_buf(fredxv[4]);
                write_data_buf(firxv[4]);
            }

            //if((tcnt%10)==0) printf("%8.3f %2d samp=%3d red= %8.3f %8.2f  ir= %8.3f %8.2f rate= %4.1f o2= %4.1f\n",
            //   meastime, cntr, samp, fredxv[4], -1.0*fredyv[4], firxv[4], -1.0*firyv[4], heartrate, pctspo2);

            //if(adc_read_ptr>200)adc_read_ptr=0;
        }

        //print to ssd1306
        cntr++;
        char textstr[64];
        if ((cntr % 10) == 0) {
            sprintf(textstr, "4Heart Rate|||4 %4.1f bpm|| %3.1f SpO~", heartrate, pctspo2);
            ssd1306_text(textstr);
            vTaskDelay(2);
        }
    }
}

// sets up the led for pwm
static void led_duty(uint16_t duty) {
    static uint16_t val;
    static uint16_t max = (1L << 10) - 1;
    if (duty > 100) return;
    val = (duty * max) / 100;
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, val);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

// handles websocket events
void websocket_callback(uint8_t num, WEBSOCKET_TYPE_t type, char *msg, uint64_t len) {
    const static char *TAG = "websocket_callback";
    int value;

    ESP_LOGI(TAG, "Got ws callback type %i", type);

    switch (type) {
        case WEBSOCKET_CONNECT:
            ESP_LOGI(TAG, "client %i connected!", num);
            break;
        case WEBSOCKET_DISCONNECT_EXTERNAL:
            ESP_LOGI(TAG, "client %i sent a disconnect message", num);
            led_duty(0);
            break;
        case WEBSOCKET_DISCONNECT_INTERNAL:
            ESP_LOGI(TAG, "client %i was disconnected", num);
            break;
        case WEBSOCKET_DISCONNECT_ERROR:
            ESP_LOGI(TAG, "client %i was disconnected due to an error", num);
            led_duty(0);
            break;
        case WEBSOCKET_TEXT:
            if (len) { // if the message length was greater than zero
                switch (msg[0]) {
                    case 'L':
                        if (sscanf(msg, "L%i", &value)) {
                            ESP_LOGI(TAG, "LED value: %i", value);
                            led_duty(value);
                            ws_server_send_text_all_from_callback(msg, len); // broadcast it!
                        }
                        break;
                    case 'M':
                        ESP_LOGI(TAG, "got message length %i: %s", (int) len - 1, &(msg[1]));
                        break;
                    default:
                        ESP_LOGI(TAG, "got an unknown message with length %i", (int) len);
                        break;
                }
            }
            break;
        case WEBSOCKET_BIN:
            ESP_LOGI(TAG, "client %i sent binary message of size %i:\n%s", num, (uint32_t) len, msg);
            break;
        case WEBSOCKET_PING:
            ESP_LOGI(TAG, "client %i pinged us with message of size %i:\n%s", num, (uint32_t) len, msg);
            break;
        case WEBSOCKET_PONG:
            ESP_LOGI(TAG, "client %i responded to the ping", num);
            break;
    }
}


// serves any clients
static void http_serve(struct netconn *conn) {
    const static char *TAG = "http_server";
    const static char HTML_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
    const static char ERROR_HEADER[] = "HTTP/1.1 404 Not Found\nContent-type: text/html\n\n";
    const static char JS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/javascript\n\n";
    const static char CSS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/css\n\n";
    const static char ICO_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/x-icon\n\n";
    //const static char EVENT_HEADER[] = "HTTP/1.1 200 OK\nContent-Type: text/event-stream\nCache-Control: no-cache\nretry: 3000\n\n";

    const static char OPTIONS_HEADER[] = "HTTP/1.1 204 No Content\n"
                                         "Access-Control-Allow-Origin: * \n"
                                         "Access-Control-Allow-Headers: X-PINGOTHER, Content-Type \n"
                                         "Access-Control-Allow-Methods: GET, OPTIONS \n"
                                         "Access-Control-Max-Age: 1728000 \n";

    struct netbuf *inbuf;
    static char *buf;
    static uint16_t buflen;
    static err_t err;

    // To store parameter from getData query string
    char *temp;
//    char outstr[2000];

    // sensor default page
    extern uint8_t index_html_start[] asm("_binary_index_html_start");
    extern uint8_t index_html_end[] asm("_binary_index_html_end");
    const uint32_t index_html_len = index_html_end - index_html_start;

    // websocket demo
    extern const uint8_t root_html_start[] asm("_binary_root_html_start");
    extern const uint8_t root_html_end[] asm("_binary_root_html_end");
    const uint32_t root_html_len = root_html_end - root_html_start;

    // test.js
    extern const uint8_t test_js_start[] asm("_binary_test_js_start");
    extern const uint8_t test_js_end[] asm("_binary_test_js_end");
    const uint32_t test_js_len = test_js_end - test_js_start;

    // test.css
    extern const uint8_t test_css_start[] asm("_binary_test_css_start");
    extern const uint8_t test_css_end[] asm("_binary_test_css_end");
    const uint32_t test_css_len = test_css_end - test_css_start;

    // favicon.ico
    extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
    const uint32_t favicon_ico_len = favicon_ico_end - favicon_ico_start;

    // error page
    extern const uint8_t error_html_start[] asm("_binary_error_html_start");
    extern const uint8_t error_html_end[] asm("_binary_error_html_end");
    const uint32_t error_html_len = error_html_end - error_html_start;

    netconn_set_recvtimeout(conn, 1000); // allow a connection timeout of 1 second
    ESP_LOGI(TAG, "reading from client...");
    err = netconn_recv(conn, &inbuf);
    ESP_LOGI(TAG, "read from client");
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void **) &buf, &buflen);
        if (buf) {


            if (starts_with(buf, "OPTIONS")) {
                ESP_LOGI(TAG, "Sending OPTIONS");
                netconn_write(conn, OPTIONS_HEADER, sizeof(OPTIONS_HEADER) - 1, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            }

                // default page
            else if ((strstr(buf, "GET / ") || strstr(buf, "GET /index.html ")) && !strstr(buf, "Upgrade: websocket")) {
                ESP_LOGI(TAG, "Sending /");
                netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, index_html_start, index_html_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            }

                // default page
            else if (strstr(buf, "GET /root.html ") && !strstr(buf, "Upgrade: websocket")) {
                ESP_LOGI(TAG, "Sending /root.html");
                netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, root_html_start, root_html_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            }

                // default page websocket
            else if (strstr(buf, "GET / ") && strstr(buf, "Upgrade: websocket")) {
                ESP_LOGI(TAG, "Requesting websocket on /");
                ws_server_add_client(conn, buf, buflen, "/", websocket_callback);
                netbuf_delete(inbuf);
            } else if (strstr(buf, "GET /test.js ")) {
                ESP_LOGI(TAG, "Sending /test.js");
                netconn_write(conn, JS_HEADER, sizeof(JS_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, test_js_start, test_js_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            } else if (strstr(buf, "GET /test.css ")) {
                ESP_LOGI(TAG, "Sending /test.css");
                netconn_write(conn, CSS_HEADER, sizeof(CSS_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, test_css_start, test_css_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            } else if (strstr(buf, "GET /favicon.ico ")) {
                ESP_LOGI(TAG, "Sending favicon.ico");
                netconn_write(conn, ICO_HEADER, sizeof(ICO_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, favicon_ico_start, favicon_ico_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            } else if (starts_with(buf, "GET /getData")) {
                //parse datagram, rcv data from webpage and return collected data
                temp = strstr(buf, "irpower=");
                if (temp)sscanf(temp, "irpower=%d", &irpower);
                temp = strstr(buf, "xrpower=");
                if (temp)sscanf(temp, "xrpower=%d", &rpower);
                temp = strstr(buf, "raworbp=");
                if (temp)sscanf(temp, "raworbp=%d", &raworbp);
                temp = strstr(buf, "startstop=");
                if (temp)sscanf(temp, "startstop=%d", &startstop);
                //printf ("ir=%d r=%d ss=%d braw=%d\n", irpower,rpower,startstop,raworbp);

                //int adc_read_ptr_shadow = adc_read_ptr;
                //adc_read_ptr = 0;

                /*
                          snprintf( outstr, sizeof outstr, "%d,",adc_read_ptr_shadow / 3);
                          snprintf(tmp, sizeof tmp, "%ld,", adc_read[0]); strcat (outstr, tmp);
                          snprintf(tmp, sizeof tmp, "%ld,", avgIR); strcat (outstr, tmp);
                          snprintf(tmp, sizeof tmp, "%ld,", avgR); strcat (outstr, tmp);
                          avgR = 0; avgIR = 0;
                          //filter is 2nd order butterworth 0.5/25 Hz
                          for( x = 0; x < adc_read_ptr_shadow; x++){
                             if(x%3 == 1){
                                 avgR = avgR + adc_read[x];
                                 rxv[0] = rxv[1]; rxv[1] = rxv[2]; rxv[2] = rxv[3]; rxv[3] = rxv[4];
                                 rxv[4] = ((float) adc_read[x]) / 3.48311;
                                 ryv[0] = ryv[1]; ryv[1] = ryv[2]; ryv[2] = ryv[3]; ryv[3] = ryv[4];
                                 ryv[4] = (rxv[0] + rxv[4]) - 2 * rxv[2]
                                        + ( -0.1718123813 * ryv[0]) + (  0.3686645260 * ryv[1])
                                        + ( -1.1718123813 * ryv[2]) + (  1.9738037992 * ryv[3]);
                                 if(raworbp==1)snprintf(tmp, sizeof tmp, "%5.1f,", rxv[4]);
                                    else snprintf(tmp, sizeof tmp, "%5.1f,", -1.0 * ryv[4]);
                                 strcat (outstr, tmp);

                   }
                             if(x%3 == 2){
                                 avgIR = avgIR + adc_read[x];
                                 irxv[0] = irxv[1]; irxv[1] = irxv[2]; irxv[2] = irxv[3]; irxv[3] = irxv[4];
                                 irxv[4] = ((float) adc_read[x]) / 3.48311;
                                 iryv[0] = iryv[1]; iryv[1] = iryv[2]; iryv[2] = iryv[3]; iryv[3] = iryv[4];
                                 iryv[4] = (irxv[0] + irxv[4]) - 2 * irxv[2]
                                         + ( -0.1718123813 * iryv[0]) + (  0.3686645260 * iryv[1])
                                         + ( -1.1718123813 * iryv[2]) + (  1.9738037992 * iryv[3]);
                                 if(raworbp==1)snprintf(tmp, sizeof tmp, "%5.4f,", irxv[4]);
                                    else   snprintf(tmp, sizeof tmp, "%5.4f,", -1.0 * iryv[4]);
                                 strcat (outstr, tmp); }
                          }
                          strcat (outstr, "0\0");
                          //ESP_LOGI("","sizeof=%d  %s",strlen(outstr), outstr);
                          //printf("%s\n",outstr);
                          send(sock, outstr, sizeof outstr, 0);
                          */

//                snprintf(outstr, sizeof outstr, "%2d,%2d,%4.1f,%4.1f,", countedsamples, (int) (100 * meastime),
//                         heartrate, pctspo2);
//                strcat(outstr, outStr);  //header and data for outstr
//                int contentLength = strlen(outstr);
//                ESP_LOGI("", "Printing %d bytes", contentLength);
//
//                char header[100];
//                sprintf(header, "HTTP/1.1 200 OK\n"
//                                "Access-Control-Allow-Origin: *\n"
//                                "Content-Type: text/plain\n"
//                                "Content-Length: %d\n\n", contentLength);
//
//                netconn_write(conn, header, strlen(header), NETCONN_NOCOPY);
//                netconn_write(conn, outstr, contentLength, NETCONN_NOCOPY);

//                memset(outStr, 0, sizeof outStr);
                countedsamples = 0;

                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            } else if (strstr(buf, "GET /")) {
                ESP_LOGI(TAG, "Unknown request, sending error page: %s", buf);
                netconn_write(conn, ERROR_HEADER, sizeof(ERROR_HEADER) - 1, NETCONN_NOCOPY);
                netconn_write(conn, error_html_start, error_html_len, NETCONN_NOCOPY);
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            } else {
                ESP_LOGI(TAG, "Unknown request");
                netconn_close(conn);
                netconn_delete(conn);
                netbuf_delete(inbuf);
            }
        } else {
            ESP_LOGI(TAG, "Unknown request (empty?...)");
            netconn_close(conn);
            netconn_delete(conn);
            netbuf_delete(inbuf);
        }
    } else { // if err==ERR_OK
        ESP_LOGI(TAG, "error on read, closing connection");
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
    }
}


// handles clients when they first connect. passes to a queue
static void server_task(void *pvParameters) {
    const static char *TAG = "server_task";
    struct netconn *conn, *newconn;
    static err_t err;
    client_queue = xQueueCreate(client_queue_size, sizeof(struct netconn *));

    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, 80);
    netconn_listen(conn);
    ESP_LOGI(TAG, "server listening");
    do {
        err = netconn_accept(conn, &newconn);
        ESP_LOGI(TAG, "new client");
        if (err == ERR_OK) {
            xQueueSendToBack(client_queue, &newconn, portMAX_DELAY);
        }
    } while (err == ERR_OK);
    netconn_close(conn);
    netconn_delete(conn);
    ESP_LOGE(TAG, "task ending, rebooting board");
    esp_restart();
}

_Noreturn // receives clients from queue, handles them
static void server_handle_task(void *pvParameters) {
    const static char *TAG = "server_handle_task";
    struct netconn *conn;
    ESP_LOGI(TAG, "task starting");
    for (;;) {
        xQueueReceive(client_queue, &conn, portMAX_DELAY);
        if (!conn) continue;
        http_serve(conn);
    }
    vTaskDelete(NULL);
}

_Noreturn static void count_task(void *pvParameters) {
    const static char *TAG = "count_task";
    char out[20];
    int len;
    int clients;
    const static char *word = "%5.1f,%5.1f";

    int n = 0;
    const int DELAY = 1; // / portTICK_PERIOD_MS; // 1 second

    ESP_LOGI(TAG, "starting task");
    for (;;) {
        float red = read_data_buf();
        float ir = read_data_buf();
        len = sprintf(out, word, red, ir);
        clients = ws_server_send_text_all(out,len);
        if (clients > 0) {
            ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
        }
        n++;
        vTaskDelay(DELAY);
    }
}

void app_main() {
    //configure esp32 memory, wifi and i2c 
    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi();
    wait_for_ip();

    i2c_init();
    i2cdetect();

    ssd1306_reset(SSD1306_RESET_PIN);
    ssd1306_init();
    ssd1306_blank(0xcc);

    //configure max30102 with i2c instructions
    max30102_init();

    ws_server_start();
    //start tcp server and data collection tasks
    xTaskCreate(&server_task, "server_task", 8192, NULL, 9, NULL);
    xTaskCreate(&server_handle_task, "server_handle_task", 8192, NULL, 6, NULL);
    xTaskCreate(max30102_task, "max30102_task", 4096, NULL, 5, NULL);
    xTaskCreate(&count_task, "count_task", 6000, NULL, 2, NULL);
}

