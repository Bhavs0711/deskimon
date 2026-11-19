#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tal_api.h"

#include "voice_bridge.h"

#if defined(AUDIO_CODEC_NAME)
#include "mix_method.h"
#include "tdl_audio_manage.h"

#define VOICE_BRIDGE_PI                 3.14159265358979323846f
#define VOICE_BRIDGE_BEEP_FREQ_HZ       740U
#define VOICE_BRIDGE_BEEP_MS            280U
#define VOICE_BRIDGE_BEEP_VOL           80U
#define VOICE_BRIDGE_CAPTURE_MIN_MS     800U
#define VOICE_BRIDGE_CAPTURE_MAX_MS     3500U
#define VOICE_BRIDGE_CAPTURE_DEFAULT_MS 2200U
#define VOICE_BRIDGE_VAD_START_LEVEL    520U
#define VOICE_BRIDGE_VAD_PEAK_LEVEL     1450U
#define VOICE_BRIDGE_VAD_HANG_MS        260U
#define VOICE_BRIDGE_VAD_MIN_BYTES      2200U
#define VOICE_BRIDGE_TRANSFER_HZ        8000U
#define VOICE_BRIDGE_READ_CHUNK_BYTES   90U
#define VOICE_BRIDGE_PLAY_MAX_MS        8000U
#define VOICE_BRIDGE_PLAY_MAX_BYTES     ((VOICE_BRIDGE_PLAY_MAX_MS * VOICE_BRIDGE_TRANSFER_HZ) / 1000U)
#define VOICE_BRIDGE_BASE64_LEN(n)      ((((n) + 2U) / 3U) * 4U + 1U)

typedef struct {
    MUTEX_HANDLE       lock;
    TDL_AUDIO_HANDLE_T handle;
    TDL_AUDIO_INFO_T   info;
    bool               opened;
    bool               capture_active;
    bool               capture_ready;
    uint8_t           *capture_buf;
    uint32_t           capture_cap;
    uint32_t           capture_len;
    uint32_t           capture_read_offset;
    uint32_t           capture_silence_ms;
    bool               capture_speech_seen;
    uint8_t           *play_buf;
    uint32_t           play_cap;
    uint32_t           play_len;
} VOICE_BRIDGE_CTX_T;

static VOICE_BRIDGE_CTX_T sg_voice = {0};

static float __clampf(float value, float min_v, float max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static void __trim_in_place(char *text)
{
    char *start = text;
    char *end   = NULL;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
}

static uint8_t __pcm16_to_ulaw(int16_t sample)
{
    const uint16_t bias = 0x84;
    uint16_t       magnitude = 0;
    uint8_t        segment = 0;
    uint8_t        ulaw = 0;

    if (sample < 0) {
        sample = (int16_t)(-sample);
        ulaw = 0x7F;
    } else {
        ulaw = 0xFF;
    }

    magnitude = (uint16_t)sample;
    if (magnitude > 32635U) {
        magnitude = 32635U;
    }
    magnitude = (uint16_t)(magnitude + bias);

    if (magnitude >= 256U) {
        uint16_t tmp = (uint16_t)(magnitude >> 7);
        while (tmp > 1U && segment < 7U) {
            segment++;
            tmp >>= 1;
        }
    }

    ulaw ^= (uint8_t)((segment << 4) | ((magnitude >> (segment + 3U)) & 0x0F));
    return ulaw;
}

static int16_t __ulaw_to_pcm16(uint8_t ulaw)
{
    int16_t  sign = 0;
    uint8_t  exponent = 0;
    uint8_t  mantissa = 0;
    int16_t  sample = 0;

    ulaw = (uint8_t)~ulaw;
    sign = (ulaw & 0x80U) ? -1 : 1;
    exponent = (uint8_t)((ulaw >> 4) & 0x07U);
    mantissa = (uint8_t)(ulaw & 0x0FU);

    sample = (int16_t)(((mantissa << 3) + 0x84) << exponent);
    sample = (int16_t)(sample - 0x84);

    return (int16_t)(sign * sample);
}

static void __voice_audio_cb(TDL_AUDIO_FRAME_FORMAT_E type, TDL_AUDIO_STATUS_E status, uint8_t *data, uint32_t len)
{
    uint32_t i = 0;
    int16_t *samples = (int16_t *)data;
    uint32_t sample_count = 0;
    uint32_t stored_samples = 0;
    uint32_t energy_sum = 0;
    uint32_t peak_level = 0;
    bool     speech_active = false;
    uint32_t chunk_ms = 0;

    (void)type;
    (void)status;

    if (NULL == data || 0 == len || NULL == sg_voice.lock) {
        return;
    }

    sample_count = len / sizeof(int16_t);

    tal_mutex_lock(sg_voice.lock);
    if (!sg_voice.capture_active || NULL == sg_voice.capture_buf || 0 == sg_voice.capture_cap) {
        tal_mutex_unlock(sg_voice.lock);
        return;
    }

    for (i = 0; i + 1U < sample_count; i += 2U) {
        int32_t value = samples[i];
        uint32_t level = (uint32_t)((value < 0) ? -value : value);

        energy_sum += level;
        if (level > peak_level) {
            peak_level = level;
        }

        if (sg_voice.capture_len >= sg_voice.capture_cap) {
            sg_voice.capture_active = false;
            sg_voice.capture_ready = true;
            break;
        }

        sg_voice.capture_buf[sg_voice.capture_len++] = __pcm16_to_ulaw(samples[i]);
        stored_samples++;
    }

    if (stored_samples > 0U) {
        uint32_t avg_level = energy_sum / stored_samples;

        speech_active = (avg_level >= VOICE_BRIDGE_VAD_START_LEVEL) || (peak_level >= VOICE_BRIDGE_VAD_PEAK_LEVEL);
        chunk_ms = (stored_samples * 1000U) / VOICE_BRIDGE_TRANSFER_HZ;

        if (speech_active) {
            sg_voice.capture_speech_seen = true;
            sg_voice.capture_silence_ms = 0;
        } else if (sg_voice.capture_speech_seen) {
            sg_voice.capture_silence_ms += chunk_ms;
        }

        if (sg_voice.capture_speech_seen && sg_voice.capture_len >= VOICE_BRIDGE_VAD_MIN_BYTES &&
            sg_voice.capture_silence_ms >= VOICE_BRIDGE_VAD_HANG_MS) {
            sg_voice.capture_active = false;
            sg_voice.capture_ready = true;
        }
    }

    if (sg_voice.capture_len >= sg_voice.capture_cap) {
        sg_voice.capture_active = false;
        sg_voice.capture_ready = true;
    }
    tal_mutex_unlock(sg_voice.lock);
}

static OPERATE_RET __voice_open_if_needed(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_voice.lock) {
        TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_voice.lock));
    }

    tal_mutex_lock(sg_voice.lock);
    if (sg_voice.opened) {
        tal_mutex_unlock(sg_voice.lock);
        return OPRT_OK;
    }

    rt = tdl_audio_find(AUDIO_CODEC_NAME, &sg_voice.handle);
    if (OPRT_OK != rt) {
        tal_mutex_unlock(sg_voice.lock);
        return rt;
    }

    rt = tdl_audio_open(sg_voice.handle, __voice_audio_cb);
    if (OPRT_OK != rt) {
        tal_mutex_unlock(sg_voice.lock);
        return rt;
    }

    rt = tdl_audio_get_info(sg_voice.handle, &sg_voice.info);
    if (OPRT_OK != rt) {
        tal_mutex_unlock(sg_voice.lock);
        return rt;
    }

    rt = tdl_audio_volume_set(sg_voice.handle, VOICE_BRIDGE_BEEP_VOL);
    if (OPRT_OK != rt) {
        tal_mutex_unlock(sg_voice.lock);
        return rt;
    }

    sg_voice.opened = true;
    tal_mutex_unlock(sg_voice.lock);

    PR_NOTICE("voice bridge ready: rate=%u bits=%u ch=%u frame=%u", sg_voice.info.sample_rate, sg_voice.info.sample_bits,
              sg_voice.info.sample_ch_num, sg_voice.info.frame_size);

    return OPRT_OK;
}

static OPERATE_RET __play_pcm16_samples(const int16_t *samples, uint32_t sample_count)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t    *frame_buf = NULL;
    uint32_t    frame_samples = 0;
    uint32_t    offset = 0;

    if (NULL == samples || 0 == sample_count || 0 == sg_voice.info.frame_size) {
        return OPRT_INVALID_PARM;
    }

    frame_buf = (uint8_t *)tal_malloc(sg_voice.info.frame_size);
    TUYA_CHECK_NULL_RETURN(frame_buf, OPRT_MALLOC_FAILED);

    frame_samples = sg_voice.info.frame_size / sizeof(int16_t);
    tdl_audio_play_stop(sg_voice.handle);

    while (offset < sample_count) {
        int16_t *pcm = (int16_t *)frame_buf;
        uint32_t i = 0;

        memset(frame_buf, 0, sg_voice.info.frame_size);
        for (i = 0; i < frame_samples && offset < sample_count; i++, offset++) {
            pcm[i] = samples[offset];
        }

        rt = tdl_audio_play(sg_voice.handle, frame_buf, sg_voice.info.frame_size);
        if (OPRT_OK != rt) {
            break;
        }
    }

    tal_free(frame_buf);
    return rt;
}

static OPERATE_RET __play_ulaw8k(const uint8_t *data, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;
    int16_t    *pcm = NULL;
    uint32_t    i = 0;

    if (NULL == data || 0 == len) {
        return OPRT_INVALID_PARM;
    }

    pcm = (int16_t *)tal_malloc(len * 2U * sizeof(int16_t));
    TUYA_CHECK_NULL_RETURN(pcm, OPRT_MALLOC_FAILED);

    for (i = 0; i < len; i++) {
        int16_t sample = __ulaw_to_pcm16(data[i]);
        pcm[i * 2U] = sample;
        pcm[i * 2U + 1U] = sample;
    }

    rt = __play_pcm16_samples(pcm, len * 2U);
    tal_free(pcm);
    return rt;
}

static OPERATE_RET __start_capture(uint32_t duration_ms)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t    target_bytes = 0;
    uint8_t    *new_buf = NULL;

    TUYA_CALL_ERR_RETURN(__voice_open_if_needed());

    duration_ms = (uint32_t)__clampf((float)duration_ms, (float)VOICE_BRIDGE_CAPTURE_MIN_MS,
                                     (float)VOICE_BRIDGE_CAPTURE_MAX_MS);
    target_bytes = (duration_ms * VOICE_BRIDGE_TRANSFER_HZ) / 1000U;

    new_buf = (uint8_t *)tal_malloc(target_bytes);
    TUYA_CHECK_NULL_RETURN(new_buf, OPRT_MALLOC_FAILED);
    memset(new_buf, 0, target_bytes);

    tal_mutex_lock(sg_voice.lock);
    if (NULL != sg_voice.capture_buf) {
        tal_free(sg_voice.capture_buf);
    }
    sg_voice.capture_buf = new_buf;
    sg_voice.capture_cap = target_bytes;
    sg_voice.capture_len = 0;
    sg_voice.capture_read_offset = 0;
    sg_voice.capture_active = true;
    sg_voice.capture_ready = false;
    sg_voice.capture_silence_ms = 0;
    sg_voice.capture_speech_seen = false;
    tal_mutex_unlock(sg_voice.lock);

    PR_NOTICE("voice capture started: %lu ms -> %lu ulaw bytes", (unsigned long)duration_ms, (unsigned long)target_bytes);

    return rt;
}

static OPERATE_RET __capture_status(char *reply, uint32_t reply_len)
{
    const char *state = "IDLE";
    uint32_t    bytes = 0;
    uint32_t    offset = 0;

    tal_mutex_lock(sg_voice.lock);
    if (sg_voice.capture_active) {
        state = "RECORDING";
    } else if (sg_voice.capture_ready) {
        state = "READY";
    }
    bytes = sg_voice.capture_len;
    offset = sg_voice.capture_read_offset;
    tal_mutex_unlock(sg_voice.lock);

    snprintf(reply, reply_len, "OK VOICE STATE=%s BYTES=%lu READ=%lu", state, (unsigned long)bytes,
             (unsigned long)offset);
    return OPRT_OK;
}

static OPERATE_RET __capture_read(char *reply, uint32_t reply_len)
{
    uint8_t      raw[VOICE_BRIDGE_READ_CHUNK_BYTES] = {0};
    char         b64[VOICE_BRIDGE_BASE64_LEN(VOICE_BRIDGE_READ_CHUNK_BYTES)] = {0};
    uint32_t     chunk_len = 0;
    bool         more = false;
    uint8_t     *free_buf = NULL;

    tal_mutex_lock(sg_voice.lock);
    if (sg_voice.capture_active) {
        tal_mutex_unlock(sg_voice.lock);
        snprintf(reply, reply_len, "ERR VOICE BUSY");
        return OPRT_COM_ERROR;
    }

    if (!sg_voice.capture_ready || NULL == sg_voice.capture_buf || sg_voice.capture_read_offset >= sg_voice.capture_len) {
        if (NULL != sg_voice.capture_buf && sg_voice.capture_read_offset >= sg_voice.capture_len) {
            free_buf = sg_voice.capture_buf;
            sg_voice.capture_buf = NULL;
            sg_voice.capture_cap = 0;
            sg_voice.capture_len = 0;
            sg_voice.capture_read_offset = 0;
            sg_voice.capture_ready = false;
        }
        tal_mutex_unlock(sg_voice.lock);

        if (NULL != free_buf) {
            tal_free(free_buf);
        }
        snprintf(reply, reply_len, "OK VOICE EMPTY");
        return OPRT_OK;
    }

    chunk_len = sg_voice.capture_len - sg_voice.capture_read_offset;
    if (chunk_len > VOICE_BRIDGE_READ_CHUNK_BYTES) {
        chunk_len = VOICE_BRIDGE_READ_CHUNK_BYTES;
    }

    memcpy(raw, &sg_voice.capture_buf[sg_voice.capture_read_offset], chunk_len);
    sg_voice.capture_read_offset += chunk_len;
    more = (sg_voice.capture_read_offset < sg_voice.capture_len);
    if (!more) {
        free_buf = sg_voice.capture_buf;
        sg_voice.capture_buf = NULL;
        sg_voice.capture_cap = 0;
        sg_voice.capture_len = 0;
        sg_voice.capture_read_offset = 0;
        sg_voice.capture_ready = false;
    }
    tal_mutex_unlock(sg_voice.lock);

    tuya_base64_encode(raw, b64, (int)chunk_len);

    if (NULL != free_buf) {
        tal_free(free_buf);
    }

    snprintf(reply, reply_len, "OK VOICE DATA=%s MORE=%d", b64, more ? 1 : 0);
    return OPRT_OK;
}

static OPERATE_RET __play_begin(uint32_t total_bytes)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t *new_buf = NULL;

    TUYA_CALL_ERR_RETURN(__voice_open_if_needed());

    if (0 == total_bytes || total_bytes > VOICE_BRIDGE_PLAY_MAX_BYTES) {
        return OPRT_INVALID_PARM;
    }

    new_buf = (uint8_t *)tal_malloc(total_bytes);
    TUYA_CHECK_NULL_RETURN(new_buf, OPRT_MALLOC_FAILED);
    memset(new_buf, 0, total_bytes);

    tal_mutex_lock(sg_voice.lock);
    if (NULL != sg_voice.play_buf) {
        tal_free(sg_voice.play_buf);
    }
    sg_voice.play_buf = new_buf;
    sg_voice.play_cap = total_bytes;
    sg_voice.play_len = 0;
    tal_mutex_unlock(sg_voice.lock);

    return OPRT_OK;
}

static OPERATE_RET __play_append_b64(const char *b64)
{
    uint8_t raw[VOICE_BRIDGE_READ_CHUNK_BYTES + 8U] = {0};
    int     decoded_len = 0;
    OPERATE_RET rt = OPRT_OK;

    if (NULL == b64 || '\0' == b64[0]) {
        return OPRT_INVALID_PARM;
    }

    decoded_len = tuya_base64_decode(b64, raw);
    if (decoded_len <= 0) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(sg_voice.lock);
    if (NULL == sg_voice.play_buf || (sg_voice.play_len + (uint32_t)decoded_len) > sg_voice.play_cap) {
        tal_mutex_unlock(sg_voice.lock);
        return OPRT_COM_ERROR;
    }

    memcpy(&sg_voice.play_buf[sg_voice.play_len], raw, (uint32_t)decoded_len);
    sg_voice.play_len += (uint32_t)decoded_len;
    tal_mutex_unlock(sg_voice.lock);

    return rt;
}

static OPERATE_RET __play_end(void)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t    *buf = NULL;
    uint32_t    len = 0;

    tal_mutex_lock(sg_voice.lock);
    buf = sg_voice.play_buf;
    len = sg_voice.play_len;
    sg_voice.play_buf = NULL;
    sg_voice.play_cap = 0;
    sg_voice.play_len = 0;
    tal_mutex_unlock(sg_voice.lock);

    if (NULL == buf || 0 == len) {
        if (NULL != buf) {
            tal_free(buf);
        }
        return OPRT_INVALID_PARM;
    }

    rt = __play_ulaw8k(buf, len);
    tal_free(buf);
    return rt;
}

OPERATE_RET voice_bridge_init(void)
{
    return __voice_open_if_needed();
}

OPERATE_RET voice_bridge_play_beep(void)
{
    OPERATE_RET rt = OPRT_OK;
    int16_t    *pcm = NULL;
    uint32_t    sample_count = 0;
    uint32_t    i = 0;
    float       phase = 0.0f;
    float       phase_step = 0.0f;

    TUYA_CALL_ERR_RETURN(__voice_open_if_needed());

    sample_count = (sg_voice.info.sample_rate * VOICE_BRIDGE_BEEP_MS) / 1000U;
    pcm = (int16_t *)tal_malloc(sample_count * sizeof(int16_t));
    TUYA_CHECK_NULL_RETURN(pcm, OPRT_MALLOC_FAILED);

    phase_step = (2.0f * VOICE_BRIDGE_PI * (float)VOICE_BRIDGE_BEEP_FREQ_HZ) / (float)sg_voice.info.sample_rate;
    for (i = 0; i < sample_count; i++) {
        float attack = (i < (sg_voice.info.sample_rate / 50U)) ? ((float)i / (float)(sg_voice.info.sample_rate / 50U)) : 1.0f;
        float release = ((sample_count - i) < (sg_voice.info.sample_rate / 40U))
                            ? ((float)(sample_count - i) / (float)(sg_voice.info.sample_rate / 40U))
                            : 1.0f;
        float env = (attack < release) ? attack : release;
        pcm[i] = (int16_t)(sinf(phase) * env * 18000.0f);
        phase += phase_step;
        if (phase >= (2.0f * VOICE_BRIDGE_PI)) {
            phase -= (2.0f * VOICE_BRIDGE_PI);
        }
    }

    rt = __play_pcm16_samples(pcm, sample_count);
    tal_free(pcm);
    return rt;
}

OPERATE_RET voice_bridge_process_command(const char *cmd, char *reply, uint32_t reply_len)
{
    char     work[224] = {0};
    char    *token = NULL;
    char    *saveptr = NULL;
    uint32_t duration_ms = VOICE_BRIDGE_CAPTURE_DEFAULT_MS;
    uint32_t total_bytes = 0;
    OPERATE_RET rt = OPRT_OK;

    if (NULL == cmd || NULL == reply || 0 == reply_len) {
        return OPRT_INVALID_PARM;
    }

    snprintf(work, sizeof(work), "%s", cmd);
    __trim_in_place(work);
    token = strtok_r(work, " ", &saveptr);

    if (NULL == token) {
        snprintf(reply, reply_len, "ERR VOICE");
        return OPRT_INVALID_PARM;
    }

    if (0 == strcmp(token, "REC") || 0 == strcmp(token, "CAPTURE")) {
        char *ms_token = strtok_r(NULL, " ", &saveptr);
        if (NULL != ms_token) {
            duration_ms = (uint32_t)strtoul(ms_token, NULL, 10);
        }

        rt = __start_capture(duration_ms);
        if (OPRT_OK != rt) {
            snprintf(reply, reply_len, "ERR VOICE %d", rt);
            return rt;
        }

        snprintf(reply, reply_len, "OK VOICE REC=%lu", (unsigned long)duration_ms);
        return OPRT_OK;
    }

    if (0 == strcmp(token, "STATUS")) {
        return __capture_status(reply, reply_len);
    }

    if (0 == strcmp(token, "READ")) {
        return __capture_read(reply, reply_len);
    }

    if (0 == strcmp(token, "PLAY")) {
        char *sub = strtok_r(NULL, " ", &saveptr);

        if (NULL == sub) {
            snprintf(reply, reply_len, "ERR VOICE PLAY");
            return OPRT_INVALID_PARM;
        }

        if (0 == strcmp(sub, "BEGIN")) {
            char *len_token = strtok_r(NULL, " ", &saveptr);
            if (NULL == len_token) {
                snprintf(reply, reply_len, "ERR VOICE PLAY");
                return OPRT_INVALID_PARM;
            }
            total_bytes = (uint32_t)strtoul(len_token, NULL, 10);
            rt = __play_begin(total_bytes);
            if (OPRT_OK != rt) {
                snprintf(reply, reply_len, "ERR VOICE %d", rt);
                return rt;
            }
            snprintf(reply, reply_len, "OK VOICE PLAY BEGIN");
            return OPRT_OK;
        }

        if (0 == strcmp(sub, "DATA")) {
            char *data = strtok_r(NULL, " ", &saveptr);
            if (NULL == data) {
                snprintf(reply, reply_len, "ERR VOICE PLAY");
                return OPRT_INVALID_PARM;
            }
            rt = __play_append_b64(data);
            if (OPRT_OK != rt) {
                snprintf(reply, reply_len, "ERR VOICE %d", rt);
                return rt;
            }
            snprintf(reply, reply_len, "OK VOICE PLAY DATA");
            return OPRT_OK;
        }

        if (0 == strcmp(sub, "END")) {
            rt = __play_end();
            if (OPRT_OK != rt) {
                snprintf(reply, reply_len, "ERR VOICE %d", rt);
                return rt;
            }
            snprintf(reply, reply_len, "OK VOICE PLAY END");
            return OPRT_OK;
        }

        snprintf(reply, reply_len, "ERR VOICE PLAY");
        return OPRT_INVALID_PARM;
    }

    if (0 == strcmp(token, "BEEP")) {
        rt = voice_bridge_play_beep();
        if (OPRT_OK != rt) {
            snprintf(reply, reply_len, "ERR VOICE %d", rt);
            return rt;
        }
        snprintf(reply, reply_len, "OK VOICE BEEP");
        return OPRT_OK;
    }

    snprintf(reply, reply_len, "ERR VOICE");
    return OPRT_INVALID_PARM;
}

#else

OPERATE_RET voice_bridge_init(void)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET voice_bridge_play_beep(void)
{
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET voice_bridge_process_command(const char *cmd, char *reply, uint32_t reply_len)
{
    (void)cmd;
    if (NULL != reply && reply_len > 0) {
        snprintf(reply, reply_len, "ERR AUDIO %d", OPRT_NOT_SUPPORTED);
    }
    return OPRT_NOT_SUPPORTED;
}

#endif
