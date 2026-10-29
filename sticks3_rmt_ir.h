#pragma once

// M5StickS3 native RMT IR backend for Bruce 1.16.1.
// Receives raw demodulated timings on GPIO42 and transmits 38 kHz-modulated
// raw timings on GPIO46, using ESP-IDF 5's RMT drivers.
// This is a source patch, not a tested firmware image.

#if defined(STICKS3_RMT_IR)
#include <Arduino.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp_err.h>
#include <vector>

class StickS3RmtReceiver {
public:
    bool begin(int pin) {
        end();
        pinMode(pin, INPUT_PULLUP);
        symbols_ = static_cast<rmt_symbol_word_t *>(calloc(kSymbols, sizeof(rmt_symbol_word_t)));
        if (!symbols_) return false;
        rmt_rx_channel_config_t cfg = {};
        cfg.gpio_num = static_cast<gpio_num_t>(pin);
        cfg.clk_src = RMT_CLK_SRC_DEFAULT;
        cfg.resolution_hz = 1000000;
        cfg.mem_block_symbols = 128;
        if (rmt_new_rx_channel(&cfg, &channel_) != ESP_OK) {
            end();
            return false;
        }
        rmt_rx_event_callbacks_t callbacks = {};
        callbacks.on_recv_done = onReceiveDone;
        if (rmt_rx_register_event_callbacks(channel_, &callbacks, this) != ESP_OK ||
            rmt_enable(channel_) != ESP_OK) {
            end();
            return false;
        }
        enabled_ = true;
        gpio_pullup_en(static_cast<gpio_num_t>(pin));
        return true;
    }

    bool arm() {
        if (!channel_) return false;
        // Cancel a still-running capture (e.g. user moved to next button).
        if (receiving_ && enabled_) {
            rmt_disable(channel_);
            enabled_ = false;
        }
        if (!enabled_) {
            if (rmt_enable(channel_) != ESP_OK) return false;
            enabled_ = true;
        }
        done_ = false;
        count_ = 0;
        rmt_receive_config_t rx = {};
        rx.signal_range_min_ns = 1000;
        rx.signal_range_max_ns = 20000000;
        esp_err_t rc = rmt_receive(channel_, symbols_, kSymbols * sizeof(rmt_symbol_word_t), &rx);
        receiving_ = rc == ESP_OK;
        return receiving_;
    }

    // Poll in the normal UI task, not from the IRQ callback.
    // Returns a complete Bruce-compatible RAW timings string.
    bool poll(String &out) {
        if (!done_) return false;
        done_ = false;
        receiving_ = false;
        const size_t n = count_;
        out = "";
        if (n < 6 || n > kSymbols) {
            arm();
            return false;
        }
        std::vector<uint32_t> durations;
        durations.reserve(n * 2);
        int lastLevel = -1;
        auto append = [&](uint16_t duration, uint8_t level) {
            if (duration == 0) return;
            // Demodulated IR receiver: active mark is LOW, space is HIGH.
            // Ignore leading idle high samples.
            if (lastLevel == -1 && level != 0) return;
            if (lastLevel == level && !durations.empty()) durations.back() += duration;
            else {
                durations.push_back(duration);
                lastLevel = level;
            }
        };
        for (size_t i = 0; i < n; ++i) {
            append(symbols_[i].duration0, symbols_[i].level0);
            append(symbols_[i].duration1, symbols_[i].level1);
        }
        // Reject startup spikes / incomplete frames. Avoid replaying junk.
        if (durations.size() < 12 || durations[0] < 150 || durations[0] > 20000) {
            arm();
            return false;
        }
        for (size_t i = 0; i < durations.size(); ++i) {
            if (i) out += ' ';
            out += String(durations[i]);
        }
        return true;
    }

    void end() {
        if (channel_) {
            if (enabled_) rmt_disable(channel_);
            rmt_del_channel(channel_);
            channel_ = nullptr;
        }
        if (symbols_) {
            free(symbols_);
            symbols_ = nullptr;
        }
        enabled_ = false;
        receiving_ = false;
        done_ = false;
    }

    ~StickS3RmtReceiver() { end(); }

private:
    static constexpr size_t kSymbols = 512;
    rmt_channel_handle_t channel_ = nullptr;
    rmt_symbol_word_t *symbols_ = nullptr;
    volatile bool done_ = false;
    volatile size_t count_ = 0;
    bool receiving_ = false;
    bool enabled_ = false;

    static bool onReceiveDone(rmt_channel_handle_t,
                              const rmt_rx_done_event_data_t *data,
                              void *user) {
        auto *self = static_cast<StickS3RmtReceiver *>(user);
        self->count_ = data->num_symbols;
        self->done_ = true;
        return false;
    }
};

inline bool sticks3RmtSendRaw(int pin, const uint16_t *durations, size_t count,
                             uint32_t frequencyHz, unsigned extraRepeats) {
    if (!durations || !count || frequencyHz < 1000 || frequencyHz > 100000) return false;

    std::vector<rmt_symbol_word_t> symbols;
    symbols.reserve(count);
    std::vector<uint16_t> intervals;
    std::vector<uint8_t> levels;
    intervals.reserve(count * 2);
    levels.reserve(count * 2);

    for (size_t i = 0; i < count; ++i) {
        uint32_t remaining = durations[i];
        if (!remaining) continue;
        // Hardware durations are 15-bit; split very long intervals as needed.
        while (remaining) {
            uint16_t part = remaining > 30000 ? 30000 : remaining;
            intervals.push_back(part);
            levels.push_back((i & 1) == 0 ? 1 : 0); // LED active HIGH
            remaining -= part;
        }
    }
    if (intervals.empty()) return false;
    for (size_t i = 0; i < intervals.size(); i += 2) {
        rmt_symbol_word_t symbol = {};
        symbol.duration0 = intervals[i];
        symbol.level0 = levels[i];
        symbol.duration1 = i + 1 < intervals.size() ? intervals[i + 1] : 1;
        symbol.level1 = i + 1 < intervals.size() ? levels[i + 1] : 0;
        symbols.push_back(symbol);
    }

    rmt_channel_handle_t chan = nullptr;
    rmt_encoder_handle_t encoder = nullptr;
    rmt_tx_channel_config_t cfg = {};
    cfg.gpio_num = static_cast<gpio_num_t>(pin);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = 1000000;
    cfg.mem_block_symbols = 128;
    cfg.trans_queue_depth = 4;
    bool ok = rmt_new_tx_channel(&cfg, &chan) == ESP_OK;
    if (ok) {
        rmt_carrier_config_t carrier = {};
        carrier.frequency_hz = frequencyHz;
        carrier.duty_cycle = 0.33f;
        ok = rmt_apply_carrier(chan, &carrier) == ESP_OK;
    }
    if (ok) {
        rmt_copy_encoder_config_t enc = {};
        ok = rmt_new_copy_encoder(&enc, &encoder) == ESP_OK;
    }
    bool enabled = false;
    if (ok) {
        enabled = rmt_enable(chan) == ESP_OK;
        ok = enabled;
    }
    if (ok) {
        rmt_transmit_config_t tx = {};
        tx.loop_count = 0;
        for (unsigned repeat = 0; repeat <= extraRepeats; ++repeat) {
            if (repeat) delay(40);
            if (rmt_transmit(chan, encoder, symbols.data(),
                             symbols.size() * sizeof(rmt_symbol_word_t), &tx) != ESP_OK ||
                rmt_tx_wait_all_done(chan, 2000) != ESP_OK) {
                ok = false;
                break;
            }
        }
    }
    if (enabled) rmt_disable(chan);
    if (encoder) rmt_del_encoder(encoder);
    if (chan) rmt_del_channel(chan);
    return ok;
}
#endif // STICKS3_RMT_IR
