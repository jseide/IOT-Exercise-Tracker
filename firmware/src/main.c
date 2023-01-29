#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <ram_pwrdn.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <ram_pwrdn.h>
#include <stdlib.h>
#include <zephyr/timing/timing.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <zephyr/net/sntp.h>

#include "tflite/main_functions.h"
#include "coap_client_utils.h"


LOG_MODULE_REGISTER(DETECT, 4);

#define SLEEP_TIME_MS   1000

#if DATA_COLLECTION
enum {negative, curl, press} exercise = negative;
#define DETECT_LED DK_LED1
#define NEGATIVE_LED DK_LED2
#define CURL_LED DK_LED3
#define PRESS_LED DK_LED4
#else
#define DETECT_LED DK_LED1
#define OT_CONNECTION_LED DK_LED2
#define MTD_SED_LED DK_LED3
uint8_t num_reps;
#endif

static uint8_t addr = 0x28;
static uint8_t quat_val = 0x20;
static uint8_t imu_val = 0x08; 

static void repeating_timer_callback(struct k_timer *timer_id);
static void my_work_handler(struct k_work *work);

//static const struct device *dev_i2c;
const struct device *const dev_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

K_TIMER_DEFINE(my_timer, repeating_timer_callback, NULL);
K_WORK_DEFINE(my_work, my_work_handler);

float coap_buffer[COAP_BUFF_METADATA_SIZE+(NUM_DIMS*COAP_BUFF_TIMESTEPS)];
float detection_buffer[NUM_DIMS*DETECT_BUFF_TIMESTEPS];
uint32_t set_id;
int max_transmissions;
int transmit_timestep;
uint32_t *setid = (uint32_t *)coap_buffer;
uint16_t *metadata = (uint16_t *)coap_buffer;


static void my_work_handler(struct k_work *work){

#if TIMEIT
    timing_t start_time, end_time;
    uint64_t total_cycles;
    uint64_t total_ns;
    timing_init();
    timing_start();
    start_time = timing_counter_get();
#endif

    uint8_t quat[8]; 
    int16_t quatW, quatX, quatY, quatZ; 
    float quat_scale = (1.0 / (16384));

    uint8_t imu[18]; 
    int16_t accelX, accelY, accelZ; 
    float accel_scale = (1.0/100);
    int16_t magX, magY, magZ; 
    float mag_scale = (1.0/16);
    int16_t gyroX, gyroY, gyroZ; 
    float gyro_scale = (1.0/16);

    uint8_t dim_idx = NUM_DIMS*DETECT_BUFF_TIMESTEPS-NUM_DIMS;

    i2c_write_read(dev_i2c, addr, &quat_val, 1, quat, 8);
    quatW = ((quat[1]<<8) | quat[0]);
    quatX = ((quat[3]<<8) | quat[2]);
    quatY = ((quat[5]<<8) | quat[4]);
    quatZ = ((quat[7]<<8) | quat[6]);

    i2c_write_read(dev_i2c, addr, &imu_val, 1, imu, 18);
    accelX = ((imu[1]<<8) | imu[0]);
    accelY = ((imu[3]<<8) | imu[2]);
    accelZ = ((imu[5]<<8) | imu[4]);
    magX = ((imu[7]<<8) | imu[6]);
    magY = ((imu[9]<<8) | imu[8]);
    magZ = ((imu[11]<<8) | imu[10]);
    gyroX = ((imu[13]<<8) | imu[12]);
    gyroY = ((imu[15]<<8) | imu[14]);
    gyroZ = ((imu[17]<<8) | imu[16]);

    memmove(detection_buffer, &detection_buffer[NUM_DIMS], 
            ((DETECT_BUFF_TIMESTEPS-1)*NUM_DIMS)*sizeof(float));

    detection_buffer[dim_idx++] = quatW*quat_scale;
    detection_buffer[dim_idx++] = quatX*quat_scale;
    detection_buffer[dim_idx++] = quatY*quat_scale;
    detection_buffer[dim_idx++] = quatZ*quat_scale;
    detection_buffer[dim_idx++] = accelX*accel_scale;
    detection_buffer[dim_idx++] = accelY*accel_scale;
    detection_buffer[dim_idx++] = accelZ*accel_scale;
    detection_buffer[dim_idx++] = magX*mag_scale;
    detection_buffer[dim_idx++] = magY*mag_scale; 
    detection_buffer[dim_idx++] = magZ*mag_scale;
    detection_buffer[dim_idx++] = gyroX*gyro_scale;
    detection_buffer[dim_idx++] = gyroY*gyro_scale;
    detection_buffer[dim_idx++] = gyroZ*gyro_scale;

    loop(detection_buffer, NUM_DIMS*DETECT_BUFF_TIMESTEPS);

#if DATA_COLLECTION
    if (output>THRESHOLD){
        dk_set_led_on(DETECT_LED);
    }
    else {
        dk_set_led_off(DETECT_LED);
    }

    transmit_timestep++;
    if (!(transmit_timestep%COAP_BUFF_TIMESTEPS)){
        memcpy(&coap_buffer[COAP_BUFF_METADATA_SIZE],detection_buffer,
                NUM_DIMS*COAP_BUFF_TIMESTEPS*sizeof(float)); 
        metadata[4]=transmit_timestep - COAP_BUFF_TIMESTEPS;
        coap_publish_new();
        LOG_INF("Transmitting timestep: %d", transmit_timestep-COAP_BUFF_TIMESTEPS);
    }
#else
    if (output>=THRESHOLD){
        if (!max_transmissions){
            dk_set_led_on(DETECT_LED);
            setid[0] = (unsigned int) rand();
            setid[1] = time(NULL);
            metadata[5]=0;
            metadata[6]=num_reps;
            LOG_INF("NEW ID: %d",setid[0]);
            LOG_INF("Rep Label: %d",num_reps);
            transmit_timestep = 0;
        }
        max_transmissions = DETECT_BUFF_TIMESTEPS + DETECTION_PADDING + transmit_timestep + 
            (COAP_BUFF_TIMESTEPS - transmit_timestep%COAP_BUFF_TIMESTEPS);
    }

    if (transmit_timestep<=(max_transmissions)){
        transmit_timestep++;
        if (!(transmit_timestep%COAP_BUFF_TIMESTEPS)){
            memcpy(&coap_buffer[COAP_BUFF_METADATA_SIZE],detection_buffer,
                    NUM_DIMS*COAP_BUFF_TIMESTEPS*sizeof(float)); 
            metadata[4]=transmit_timestep - COAP_BUFF_TIMESTEPS;

            if (transmit_timestep==(max_transmissions)){
                //LOG_INF("Last One"); 
                dk_set_led_off(DETECT_LED);
                metadata[5]=1;
                max_transmissions=0;
            }
            coap_publish_new();
            //LOG_INF("Transmitting timestep: %d", transmit_timestep-COAP_BUFF_TIMESTEPS);
            //LOG_INF("Transmitting max_transmissions: %d", max_transmissions);
        }
    }
#endif 

#if TIMEIT
    end_time = timing_counter_get();
    total_cycles = timing_cycles_get(&start_time, &end_time);
    total_ns = timing_cycles_to_ns(total_cycles);
    timing_stop();
    LOG_INF("TIME: %llu",total_ns);
#endif
}

static void repeating_timer_callback(struct k_timer *timer_id) {

    k_work_submit(&my_work);
}

static void setup_imu(void){


    if (!device_is_ready(dev_i2c)) {
		printk("I2C: Device is not ready.\n");
		return;
	}

    LOG_INF("Setup IMU");
    k_msleep(SLEEP_TIME_MS);

    int ret;
    uint8_t reg = 0x00;
    uint8_t chipID[1];

    ret = i2c_write(dev_i2c, &reg, 1, addr);
    if(ret != 0){
        LOG_INF("Failed to write to I2C device address during write.");
        return;
    }

    ret = i2c_read(dev_i2c, chipID,1,addr);
    if(ret != 0){
        LOG_INF("Failed to write/read I2C device address during write.");
        return;
    }

    if(chipID[0] != 0xA0){
        LOG_INF("Chip ID Not Correct - %x\n",chipID[0]);
        return;
    }
    else{
        LOG_INF("Success!");
    }

    //Unit Selection
    uint8_t data[2];
    data[0] = 0x3B;
    data[1] = 0b0000000;
    ret = i2c_write(dev_i2c, data, 2, addr);
    if(ret != 0){
        LOG_INF("Failed to write to I2C device address during write.");
        return;
    }

    //Set Opr Mode
    data[0] = 0x3D;
    data[1] = 0x0C;
    ret = i2c_write(dev_i2c, data, 2, addr);
    if(ret != 0){
        LOG_INF("Failed to write to I2C device address during write.");
        return;
    }
}

static void on_mtd_mode_toggle(uint32_t med)
{
#if IS_ENABLED(CONFIG_PM_DEVICE) //still need the logs for debugging 
const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

if (med) {
pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
} else {
pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
}
#endif
    LOG_INF("MTD CB");
#if !(DATA_COLLECTION)
    dk_set_led(MTD_SED_LED, med);
#endif
}

static void on_connect(struct k_work *item)
{

    k_msleep(SLEEP_TIME_MS);
    LOG_INF("Sending SNTP");
    int err = set_clock_sntp();
    if (err<0){
        LOG_ERR("SNTP_FAILED");
    }

    LOG_INF("Setting rand seed");
    srand(time(NULL));

    LOG_INF("Opening socket");
    coap_open_socket();

    coap_publish_new(); //sending empty buffer because first send is slow

#if DATA_COLLECTION
    setid[0] = (unsigned int) rand();
    setid[1] = time(NULL);
    LOG_INF("INITIAL ID: %d",setid[0]);
    dk_set_led_on(NEGATIVE_LED);
#else
    dk_set_led_on(OT_CONNECTION_LED);
#endif

    k_msleep(SLEEP_TIME_MS);
    k_msleep(SLEEP_TIME_MS);
    k_msleep(SLEEP_TIME_MS);

    LOG_INF("Starting IMU timer");
    k_timer_start(&my_timer, K_MSEC(IMU_FREQ_MS), K_MSEC(IMU_FREQ_MS));
}

static void on_disconnect(struct k_work *item)
{
    LOG_INF("Disconnected: pausing IMU and closing socket");
    coap_close_socket();
    k_timer_stop(&my_timer);
#if !(DATA_COLLECTION)
    dk_set_led_off(OT_CONNECTION_LED);
#endif
}

static void on_button_changed(uint32_t button_state, uint32_t has_changed)
{
    uint32_t buttons = button_state & has_changed;
    if (buttons & DK_BTN1_MSK) {
#if DATA_COLLECTION
        dk_set_led_off(NEGATIVE_LED);
        dk_set_led_off(CURL_LED);
        dk_set_led_off(PRESS_LED);
        switch (exercise){
            case negative:
                exercise = curl;
                dk_set_led_on(CURL_LED);
                break;
            case curl:
                exercise = press;
                dk_set_led_on(PRESS_LED);
                break;
            case press:
                exercise = negative;
                dk_set_led_on(NEGATIVE_LED);
                break;
        }
        setid[0] = rand();
        transmit_timestep=0;
        LOG_INF("\nSession ID: %d", setid[0]);     
        LOG_INF("Exercise ID: %d",exercise);
        metadata[5]=exercise;
#else
        LOG_INF("Toggle MTD Button");
        coap_client_toggle_minimal_sleepy_end_device();
#endif
}

#if (!DATA_COLLECTION)
    if (buttons & DK_BTN2_MSK) {
        num_reps+=1;
        if (num_reps>15){
            num_reps=0;
        }
        LOG_INF("Rep Label: %d",num_reps);
    }
#endif
}

void main(void)
{
    int ret;

    LOG_INF("Start...");
#if DATA_COLLECTION
    LOG_INF("Data Collection Enabled");
#else
    LOG_INF("Detect Enabled");
#endif

    if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
        power_down_unused_ram();
    }

    ret = dk_leds_init();
    if (ret) {
        LOG_ERR("Cannot init leds, (error: %d)", ret);
        return;
    }

    ret = dk_buttons_init(on_button_changed);
    if (ret) {
        LOG_ERR("Cannot init buttons (error: %d)", ret);
        return;
    }

    setup_imu();
    setup_tflite();

    k_msleep(SLEEP_TIME_MS);

    LOG_INF("COAP STARTING!");
    coap_client_utils_init(on_connect,on_disconnect,on_mtd_mode_toggle);
}

