#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/thread.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <stdio.h>
#include <openthread/crypto.h>
#include <zephyr/net/coap.h>
#include <sys/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <sys/time.h>
#include <zephyr/net/sntp.h>
#include <arpa/inet.h>

#include "coap_client_utils.h"
#include "tflite/main_functions.h"


LOG_MODULE_REGISTER(coap_client_utils, 4);


/**
 * SNTP server address.
 */
#define SNTP_SERVER_ADDRESS                   "64:ff9b::d8ef:230c"
#define SNTP_SERVER_PORT                      123
#define JWT_TIMEOUT                           10

/**
 * Google Cloud Platform CoAP server parameters.
 */
#define GCP_COAP_IOT_CORE_SERVER_PORT         5683
#define GCP_COAP_IOT_CORE_SERVER_SECURE_PORT  5684

/**
 * Google Cloud Platform project configuration.
 * Must be configured by the user.
 */
#define GCP_COAP_IOT_CORE_SERVER_ADDRESS      "64:ff9b::1234:5678"
#define GCP_COAP_IOT_CORE_PATH                "gcp"
#define GCP_COAP_IOT_CORE_PROJECT_ID          "project_id"
#define GCP_COAP_IOT_CORE_REGION              "us-central1"
#define GCP_COAP_IOT_CORE_PUBLISH             "publishEvent"
#define GCP_COAP_IOT_CORE_CONFIG              "config"

#if DATA_COLLECTION
#define GCP_COAP_IOT_CORE_REGISTRY_ID         "coap-demo"
#else
#define GCP_COAP_IOT_CORE_REGISTRY_ID         "coap-detect"
#endif


/**
 * CoAP transport configuration.
 * Must be configured by the user.
 */
#define GCP_COAP_SECURE_ENABLED               1
#define GCP_COAP_SECURE_PSK_SECRET            "some_secret"
#define GCP_COAP_SECURE_PSK_IDENTITY          "my_identity"

/**
 * Google Cloud Platform device configuration.
 * Must be configured by the user.
 */
#define GCP_COAP_IOT_CORE_DEVICE_ID          "some_device_id"
#define GCP_COAP_IOT_CORE_DEVICE_KEY                                   \
"-----BEGIN EC PRIVATE KEY-----\r\n"                                   \
">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\r\n" \
">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>r\n" \
">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>==\r\n"                             \
"-----END EC PRIVATE KEY-----\r\n"


/**
 * The JSON representation of the header with ES256 algorithm.
 */
#define JWT_HEADER_TYPE_ES256 \
    "{\"alg\":\"ES256\",\"typ\":\"JWT\"}"

/**
 * The maximum size of the JWT signature.
 */
#define JWT_SIGNATURE_SIZE 64

/**
 * The size of key length for ES256.
 */
#define JWT_KEY_LENGTH_ES256 32

/**
 * The JWT delimeter used to separete header, claim and signature.
 *
 */
#define JWT_DELIMETER '.'


#define PSK_TAG 1
#define MAX_COAP_MSG_LEN 4000
#define COAP_VER 1
#define COAP_TOKEN_LEN 8
#define JWT_BUFFER_LENGTH 512
#define SLEEP_TIME_MS   1000

bool is_connected = false;
static int sock;
static sec_tag_t sec_tag_list[] = {PSK_TAG};
static const uint8_t psk[] = "some_secret";
static const uint8_t psk_id[] = "my_identity";

static struct k_work toggle_MTD_SED_work;
static struct k_work on_connect_work;
static struct k_work on_disconnect_work;


mtd_mode_toggle_cb_t on_mtd_mode_toggle;

int set_clock_sntp(void)
{
    struct sntp_ctx ctx;
    struct sockaddr_in6 addr6;
    struct sntp_time sntp_time;
    struct timespec tspec;
    int ret;

    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(SNTP_SERVER_PORT);
    inet_pton(AF_INET6, SNTP_SERVER_ADDRESS, &addr6.sin6_addr);

    ret = sntp_init(&ctx, (struct sockaddr *) &addr6,
            sizeof(struct sockaddr_in6));
    if (ret < 0) {
        LOG_ERR("Failed to init SNTP IPv6 ctx: %d", ret);
        goto end;
    }

    LOG_INF("Sending SNTP IPv6 request...");
    ret = sntp_query(&ctx, 6000, &sntp_time);
    if (ret < 0) {
        LOG_ERR("SNTP IPv6 request: %d", ret);
        goto end;
    }

    LOG_INF("Time : %llu", sntp_time.seconds);

    tspec.tv_sec = sntp_time.seconds;
    tspec.tv_nsec = ((uint64_t)sntp_time.fraction*(1000*1000*1000))>>32;

    ret = clock_settime(CLOCK_REALTIME, &tspec);
    if (ret < 0) {
        LOG_ERR("Setting clock failed: %d", ret);
        goto end;
    }

end:
    sntp_close(&ctx);
    return ret;
}


/***************************************************************************************************
 * @section JWT generation.
 **************************************************************************************************/

static otError base64_url_encode(uint8_t *p_output, uint16_t *p_output_len, const uint8_t *p_buff, uint16_t length)
{
    otError error = OT_ERROR_NONE;
    int     result;
    size_t  encoded_len = 0;

    result = mbedtls_base64_encode(p_output, *p_output_len, &encoded_len, p_buff, length);

    if (result != 0)
    {
        return OT_ERROR_NO_BUFS;
    }

    // JWT uses URI as defined in RFC4648, while mbedtls as is in RFC1421.
    for (uint32_t index = 0; index < encoded_len; index++)
    {
        if (p_output[index] == '+')
        {
            p_output[index] = '-';
        }
        else if (p_output[index] == '/')
        {
            p_output[index] = '_';
        }
        else if (p_output[index] == '=')
        {
            p_output[index] = 0;
            encoded_len  = index;
            break;
        }
    }

    *p_output_len = encoded_len;

    return error;
}

static otError jwt_create(uint8_t       * p_output,
        uint16_t      * p_output_len,
        const uint8_t * p_claims,
        uint16_t        claims_len,
        const uint8_t * p_private_key,
        uint16_t        private_key_len)
{
    otError                error = OT_ERROR_NONE;
    uint8_t                hash[32];
    uint8_t                signature[JWT_SIGNATURE_SIZE];
    uint16_t               signature_size    = JWT_SIGNATURE_SIZE;
    uint16_t               output_max_length = *p_output_len;
    uint16_t               length;

    // Encode JWT Header using Base64 URL.
    length = output_max_length;

    error = base64_url_encode(p_output, &length, (const uint8_t *)JWT_HEADER_TYPE_ES256,
            strlen(JWT_HEADER_TYPE_ES256));
    __ASSERT_NO_MSG(error == OT_ERROR_NONE);

    *p_output_len = length;

    // Append delimiter.
    p_output[*p_output_len] = JWT_DELIMETER;
    *p_output_len += 1;

    // Encode JWT Claim using Base64 URL.
    length = output_max_length - *p_output_len;

    error = base64_url_encode(p_output + *p_output_len, &length, p_claims, claims_len);
    __ASSERT_NO_MSG(error == OT_ERROR_NONE);

    *p_output_len += length;

    // Create SHA256 Hash from encoded JWT Header and JWT Claim.
    error = mbedtls_sha256(p_output, *p_output_len, hash, 0);
    __ASSERT_NO_MSG(error == 0);

    // Append delimiter.
    p_output[*p_output_len] = JWT_DELIMETER;
    *p_output_len += 1;

    // Create ECDSA Sign.
    error = otCryptoEcdsaSign(signature, &signature_size, hash, sizeof(hash), p_private_key, private_key_len);
    __ASSERT_NO_MSG(error == OT_ERROR_NONE);

    // Encode JWT Sign using Base64 URL.
    length = output_max_length - *p_output_len;

    error = base64_url_encode(p_output + *p_output_len, &length, signature, signature_size);
    __ASSERT_NO_MSG(error == OT_ERROR_NONE);

    *p_output_len += length;

    return error;
}

/***************************************************************************************************
 * @section CoAP messages.
 **************************************************************************************************/
static void coap_header_proxy_uri_append(struct coap_packet * p_message, const char * p_action)
{

    otError error = OT_ERROR_NONE;
    char    jwt[JWT_BUFFER_LENGTH];
    char    claims[JWT_BUFFER_LENGTH];

    memset(jwt, 0, sizeof(jwt));
    memset(claims, 0, sizeof(claims));

    unsigned int m_unix_time = (unsigned int) time(NULL);

    uint16_t offset = snprintf(jwt, sizeof(jwt), "%s/%s/%s/%s/%s?jwt=",
            GCP_COAP_IOT_CORE_PROJECT_ID, GCP_COAP_IOT_CORE_REGION,
            GCP_COAP_IOT_CORE_REGISTRY_ID, GCP_COAP_IOT_CORE_DEVICE_ID,
            p_action);

    uint16_t output_len = sizeof(jwt) - offset;


    uint64_t timeout = m_unix_time + JWT_TIMEOUT;

    snprintf(claims, sizeof(claims), "{\"iat\":%u,\"exp\":%u,\"aud\":\"%s\"}",
            (uint32_t)(m_unix_time), (uint32_t)(timeout), GCP_COAP_IOT_CORE_PROJECT_ID);

    __ASSERT_NO_MSG(length > 0);

    error = jwt_create((uint8_t *)&jwt[offset], &output_len, (const uint8_t *)claims, strlen(claims),
            (const uint8_t *)GCP_COAP_IOT_CORE_DEVICE_KEY, sizeof(GCP_COAP_IOT_CORE_DEVICE_KEY));

    __ASSERT_NO_MSG(error == OT_ERROR_NONE);

    coap_packet_append_option(p_message, COAP_OPTION_PROXY_URI,
            jwt, strlen(jwt));
}


void coap_publish_new(void)
{
    int ret;
    struct coap_packet request;
    uint8_t data[MAX_COAP_MSG_LEN];


    ret = coap_packet_init(&request, data, sizeof(data),
            1, OT_COAP_TYPE_NON_CONFIRMABLE, 2, coap_next_token(),
            COAP_METHOD_POST, coap_next_id());
    if (ret < 0) {
        LOG_ERR("COAP packet init failed: %d", ret);
    }

    ret = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
            GCP_COAP_IOT_CORE_PATH, strlen(GCP_COAP_IOT_CORE_PATH));
    if (ret < 0) {
        LOG_ERR("COAP append option failed: %d", ret);
    }

    coap_header_proxy_uri_append(&request, GCP_COAP_IOT_CORE_PUBLISH);

    ret = coap_packet_append_payload_marker(&request);
    if (ret < 0) {
        LOG_ERR("COAP append append payload marker failed: %d", ret);
    }

    ret = coap_packet_append_payload(&request, (uint8_t *)coap_buffer, 
            (COAP_BUFF_METADATA_SIZE+(NUM_DIMS*COAP_BUFF_TIMESTEPS))*sizeof(float));
    if (ret < 0) {
        LOG_ERR("COAP append payload failed: %d", ret);
    }

    ret = send(sock, request.data, request.offset, 0);
    if (ret < 0) {
        LOG_ERR("Socket send failed: %d", ret); 
    }
}

static void toggle_minimal_sleepy_end_device(struct k_work *item)
{
	otError error;
	otLinkModeConfig mode;
	struct openthread_context *context = openthread_get_default_context();

	__ASSERT_NO_MSG(context != NULL);

	openthread_api_mutex_lock(context);
	mode = otThreadGetLinkMode(context->instance);
	mode.mRxOnWhenIdle = !mode.mRxOnWhenIdle;
	error = otThreadSetLinkMode(context->instance, mode);
	openthread_api_mutex_unlock(context);

	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to set MLE link mode configuration");
	} else {
		LOG_INF("Success set MLE link mode configuration");
		on_mtd_mode_toggle(mode.mRxOnWhenIdle);
	}
}

void coap_client_toggle_minimal_sleepy_end_device(void)
{
	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED)) {
		k_work_submit(&toggle_MTD_SED_work);
	}
}

void coap_close_socket(void)
{
    (void)close(sock);
}

int coap_open_socket(void)
{
    int ret = 0;
    struct sockaddr_in6 addr6;

    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(GCP_COAP_IOT_CORE_SERVER_SECURE_PORT);

    inet_pton(AF_INET6, GCP_COAP_IOT_CORE_SERVER_ADDRESS, &addr6.sin6_addr);

    sock = socket(addr6.sin6_family, SOCK_DGRAM, IPPROTO_DTLS_1_2);
    if (sock < 0) {
        LOG_ERR("Failed to create UDP socket %d", errno);
        return -errno;
    }


    ret = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
            sec_tag_list, sizeof(sec_tag_list));
    if (ret < 0) {
        LOG_ERR("Failed to set sockopt: %d", ret);
        return ret;
    }

    //Call connect so we can use send and recv
    ret = connect(sock, (const struct sockaddr *)&addr6, sizeof(addr6));
    if (ret < 0) {
        LOG_ERR("Error coneccting: %d", ret);
        return ret;
    }

    LOG_INF("Connection Successful!");
    return 0;
}

static void update_device_state(void)
{
	struct otInstance *instance = openthread_get_default_instance();
	otLinkModeConfig mode = otThreadGetLinkMode(instance);
	on_mtd_mode_toggle(mode.mRxOnWhenIdle);
}

static void on_thread_state_changed(uint32_t flags, void *context)
{
	struct openthread_context *ot_context = context;

	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
            LOG_INF("Child");
		case OT_DEVICE_ROLE_ROUTER:
            LOG_INF("Router");
		case OT_DEVICE_ROLE_LEADER:
            LOG_INF("Leader");
            if (!is_connected){
            is_connected = true;
            k_work_submit(&on_connect_work);
            }
			break;

		case OT_DEVICE_ROLE_DISABLED:
            LOG_INF("Disabled");
		case OT_DEVICE_ROLE_DETACHED:
            LOG_INF("Detached");
		default:
            LOG_INF("Default");
            if (is_connected){
            k_work_submit(&on_disconnect_work);
			is_connected = false;
            }
			break;
		}
	}
}

static void dtls_init(void){

    int ret;
    ret = tls_credential_add(PSK_TAG,
            TLS_CREDENTIAL_PSK,
            psk,
            sizeof(psk) - 1);
    if (ret < 0) {
        LOG_ERR("Failed to add tls credential PSK: %d", ret);
    }

    ret = tls_credential_add(PSK_TAG,
            TLS_CREDENTIAL_PSK_ID,
            psk_id,
            sizeof(psk_id) - 1);
    if (ret < 0) {
        LOG_ERR("Failed to add tls credential PSKID: %d", ret);
    }
    return;
}

void coap_client_utils_init(ot_connection_cb_t on_connect,
			    ot_disconnection_cb_t on_disconnect,
			    mtd_mode_toggle_cb_t on_toggle){

    dtls_init();
	on_mtd_mode_toggle = on_toggle;
    k_work_init(&on_connect_work, on_connect);
    k_work_init(&on_disconnect_work, on_disconnect);
    k_work_init(&toggle_MTD_SED_work,
            toggle_minimal_sleepy_end_device);

	openthread_set_state_changed_cb(on_thread_state_changed);
	openthread_start(openthread_get_default_context());
    update_device_state();
}




