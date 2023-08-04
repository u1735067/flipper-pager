#include "pocsag_pager_app_i.h"

#define TAG "POCSAGPager"
#include <flipper_format/flipper_format_i.h>


// Hacky hack !
#include <m-list.h>

typedef struct {
    FuriString* custom_preset_name;
    uint8_t* custom_preset_data;
    size_t custom_preset_data_size;
} SubGhzSettingCustomPresetItem;

ARRAY_DEF(SubGhzSettingCustomPresetItemArray, SubGhzSettingCustomPresetItem, M_POD_OPLIST)

#define M_OPL_SubGhzSettingCustomPresetItemArray_t() \
    ARRAY_OPLIST(SubGhzSettingCustomPresetItemArray, M_POD_OPLIST)

LIST_DEF(FrequencyList, uint32_t)

#define M_OPL_FrequencyList_t() LIST_OPLIST(FrequencyList)

typedef struct {
    SubGhzSettingCustomPresetItemArray_t data;
} SubGhzSettingCustomPresetStruct;

struct SubGhzSetting {
    FrequencyList_t frequencies;
    FrequencyList_t hopper_frequencies;
    SubGhzSettingCustomPresetStruct* preset;
};
// /Hacky hack !


void pcsg_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    furi_string_set(app->txrx->preset->name, preset_name);
    app->txrx->preset->frequency = frequency;
    app->txrx->preset->data = preset_data;
    app->txrx->preset->data_size = preset_data_size;
}

void pcsg_get_frequency_modulation(
    POCSAGPagerApp* app,
    FuriString* frequency,
    FuriString* modulation) {
    furi_assert(app);
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            app->txrx->preset->frequency / 1000000 % 1000,
            app->txrx->preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        furi_string_printf(modulation, "%.2s", furi_string_get_cstr(app->txrx->preset->name));
    }
}

void pcsg_begin(POCSAGPagerApp* app, uint8_t* preset_data) {
    furi_assert(app);

    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);

    // furi_hal_gpio_init(furi_hal_subghz.cc1101_g0_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}

bool pcsg_is_frequency_valid(const SubGhzDevice* device, uint32_t value) {
    UNUSED(device);

    if(!(value >= 281000000 && value <= 361000000) &&
       !(value >= 378000000 && value <= 481000000) &&
       !(value >= 749000000 && value <= 962000000)) {
        return false;
    }

    return true;
}

// subghz_setting_load
void pgsg_setting_load(SubGhzSetting* instance, const char* file_path) {
    subghz_setting_load(instance, file_path);

    furi_assert(instance);

    // Add more freqs, bypassing the subghz_setting_load checks
    FrequencyList_push_back(instance->frequencies, 466000000);
    FrequencyList_push_back(instance->frequencies, 466025000);
    FrequencyList_push_back(instance->frequencies, 466050000);
    FrequencyList_push_back(instance->frequencies, 466075000);
    FrequencyList_push_back(instance->frequencies, 466175000);
    FrequencyList_push_back(instance->frequencies, 466206250);
    FrequencyList_push_back(instance->frequencies, 466231250);
    FrequencyList_push_back(instance->frequencies, 466475000);
    FrequencyList_push_back(instance->frequencies, 466525000);
    FrequencyList_push_back(instance->frequencies, 466575000);

    // Replace hopping freqs with std freqs
    FrequencyList_reset(instance->hopper_frequencies);
    FrequencyList_push_back(instance->hopper_frequencies, 466025000);
    FrequencyList_push_back(instance->hopper_frequencies, 466050000);
    FrequencyList_push_back(instance->hopper_frequencies, 466075000);
    FrequencyList_push_back(instance->hopper_frequencies, 466175000);
    FrequencyList_push_back(instance->hopper_frequencies, 466206250);
    FrequencyList_push_back(instance->hopper_frequencies, 466231250);
}

// subghz_device_cc1101_int_interconnect_set_frequency
// > furi_hal_subghz_set_frequency_and_path
uint32_t pcsg_set_frequency(const SubGhzDevice* device, uint32_t value) {
    UNUSED(device);

    // Set these values to the extended frequency range
    value = furi_hal_subghz_set_frequency(value);
    if(value >= 281000000 && value <= 361000000) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath315);
    } else if(value >= 378000000 && value <= 481000000) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath433);
    } else if(value >= 749000000 && value <= 962000000) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath868);
    } else {
        furi_crash("SubGhz: Incorrect frequency during set.");
    }
    return value;
}

uint32_t pcsg_rx(POCSAGPagerApp* app, uint32_t frequency) {
    furi_assert(app);
    if(!pcsg_is_frequency_valid(app->txrx->radio_device, frequency)) {
        furi_crash("POCSAGPager: Incorrect RX frequency.");
    }
    furi_assert(
        app->txrx->txrx_state != PCSGTxRxStateRx && app->txrx->txrx_state != PCSGTxRxStateSleep);

    subghz_devices_idle(app->txrx->radio_device);
    uint32_t value = pcsg_set_frequency(app->txrx->radio_device, frequency);

    // Not need. init in subghz_devices_start_async_tx
    // furi_hal_gpio_init(furi_hal_subghz.cc1101_g0_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);

    subghz_devices_flush_rx(app->txrx->radio_device);
    subghz_devices_set_rx(app->txrx->radio_device);

    subghz_devices_start_async_rx(
        app->txrx->radio_device, subghz_worker_rx_callback, app->txrx->worker);
    subghz_worker_start(app->txrx->worker);
    app->txrx->txrx_state = PCSGTxRxStateRx;
    return value;
}

void pcsg_idle(POCSAGPagerApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state != PCSGTxRxStateSleep);
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}

void pcsg_rx_end(POCSAGPagerApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state == PCSGTxRxStateRx);
    if(subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
        subghz_devices_stop_async_rx(app->txrx->radio_device);
    }
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}

void pcsg_sleep(POCSAGPagerApp* app) {
    furi_assert(app);
    subghz_devices_sleep(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateSleep;
}

void pcsg_hopper_update(POCSAGPagerApp* app) {
    furi_assert(app);

    switch(app->txrx->hopper_state) {
    case PCSGHopperStateOFF:
        return;
        break;
    case PCSGHopperStatePause:
        return;
        break;
    case PCSGHopperStateRSSITimeOut:
        if(app->txrx->hopper_timeout != 0) {
            app->txrx->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }
    float rssi = -127.0f;
    if(app->txrx->hopper_state != PCSGHopperStateRSSITimeOut) {
        // See RSSI Calculation timings in CC1101 17.3 RSSI
        rssi = subghz_devices_get_rssi(app->txrx->radio_device);

        // Stay if RSSI is high enough
        if(rssi > -90.0f) {
            app->txrx->hopper_timeout = 10;
            app->txrx->hopper_state = PCSGHopperStateRSSITimeOut;
            return;
        }
    } else {
        app->txrx->hopper_state = PCSGHopperStateRunnig;
    }
    // Select next frequency
    if(app->txrx->hopper_idx_frequency <
       subghz_setting_get_hopper_frequency_count(app->setting) - 1) {
        app->txrx->hopper_idx_frequency++;
    } else {
        app->txrx->hopper_idx_frequency = 0;
    }

    if(app->txrx->txrx_state == PCSGTxRxStateRx) {
        pcsg_rx_end(app);
    };
    if(app->txrx->txrx_state == PCSGTxRxStateIDLE) {
        subghz_receiver_reset(app->txrx->receiver);
        app->txrx->preset->frequency =
            subghz_setting_get_hopper_frequency(app->setting, app->txrx->hopper_idx_frequency);
        pcsg_rx(app, app->txrx->preset->frequency);
    }
}
