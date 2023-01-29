#ifndef __COAP_UTILS_H__
#define __COAP_UTILS_H__

#include <zephyr/kernel.h>
#include <sys/_stdint.h>

#define TIMEIT 0 //TIME THE INFERENCE/TRANSMISSION TASK
#define DATA_COLLECTION 0 //SWITCH TO DATA_COLLECT SETUP
#define NUM_DIMS 13 //NUMBER OF IMU DATAPOINTS PER READING
#define COAP_BUFF_METADATA_SIZE 4 //EXTRA BUFFER SPACE FOR METADATA 
#define COAP_BUFF_TIMESTEPS 2 //SIZE OF TRANSMISSION BUFFER
#define DETECT_BUFF_TIMESTEPS 16 // MUST BE A MULTIPLE OF COAP_BUFF_TIMESTEPS
#define DETECTION_PADDING DETECT_BUFF_TIMESTEPS/2
#define IMU_FREQ_MS 250 //FREQ IN MS
#define THRESHOLD .5 //ML PROBABILITY THRESHOLD

/** @brief Type indicates function called when OpenThread connection
 *         is established.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ot_connection_cb_t)(struct k_work *item);

/** @brief Type indicates function called when OpenThread connection is ended.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ot_disconnection_cb_t)(struct k_work *item);

/** @brief Type indicates function called when the MTD modes are toggled.
 *
 * @param[in] val 1 if the MTD is in MED mode
 *                0 if the MTD is in SED mode
 */
typedef void (*mtd_mode_toggle_cb_t)(uint32_t val);

extern float coap_buffer[COAP_BUFF_METADATA_SIZE+(NUM_DIMS*COAP_BUFF_TIMESTEPS)];
extern float detection_buffer[NUM_DIMS*DETECT_BUFF_TIMESTEPS];
extern int counter;
extern float output;
extern int session_time;
extern bool is_connected;

/** @brief Initialize CoAP client utilities.
 */
void coap_client_utils_init(ot_connection_cb_t on_connect,
			    ot_disconnection_cb_t on_disconnect,
			    mtd_mode_toggle_cb_t on_toggle);

int coap_open_socket(void);
void coap_close_socket(void);
void coap_publish_new(void);
int set_clock_sntp(void);
void coap_client_toggle_minimal_sleepy_end_device(void);

#endif

