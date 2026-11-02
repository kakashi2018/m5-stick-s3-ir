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
        Serial.printf("STICKS3_IR_RX: RMT completed, symbols=%u\n", (unsigned)n);
        out = "";
        // NEC repeat frames are much shorter than full NEC commands.
        if (n < 2 || n > kSymbols) {
            Serial.println("STICKS3_IR_RX: rejected short/oversize capture");
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
        if (durations.size() < 4 || durations[0] < 150 || durations[0] > 20000) {
            Serial.printf("STICKS3_IR_RX: rejected pulse count=%u first=%u us\n",
                          (unsigned)durations.size(),
                          (unsigned)(durations.empty() ? 0 : durations[0]));
            arm();
            return false;
        }
        Serial.printf("STICKS3_IR_RX: accepted %u pulse widths, first=%u us\n",
                      (unsigned)durations.size(), (unsigned)durations[0]);
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
    if (!durations || !count || frequencyHz < 1000 || frequencyHz > 100000) {
        Serial.printf("STICKS3_IR_TX: invalid input count=%u frequency=%lu\n",
                      (unsigned)count, (unsigned long)frequencyHz);
        return false;
    }
    Serial.printf("STICKS3_IR_TX: start pin=%d widths=%u frequency=%luHz repeats=%u\n",
                  pin, (unsigned)count, (unsigned long)frequencyHz, extraRepeats);

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
    // Match the working StickS3 standalone test / official M5 RMT example.
    cfg.mem_block_symbols = 64;
    cfg.trans_queue_depth = 4;
    cfg.flags.invert_out = false; // The built-in IR LED is active HIGH.
    cfg.flags.with_dma = false;
    esp_err_t rc = rmt_new_tx_channel(&cfg, &chan);
    bool ok = (rc == ESP_OK);
    if (!ok) Serial.printf("STICKS3_IR_TX: new channel failed: %s\n", esp_err_to_name(rc));
    if (ok) {
        rmt_carrier_config_t carrier = {};
        carrier.frequency_hz = frequencyHz;
        carrier.duty_cycle = 0.33f;
        carrier.flags.polarity_active_low = false;
        rc = rmt_apply_carrier(chan, &carrier);
        ok = rc == ESP_OK;
        if (!ok) Serial.printf("STICKS3_IR_TX: apply carrier failed: %s\n", esp_err_to_name(rc));
    }
    if (ok) {
        rmt_copy_encoder_config_t enc = {};
        rc = rmt_new_copy_encoder(&enc, &encoder);
        ok = rc == ESP_OK;
        if (!ok) Serial.printf("STICKS3_IR_TX: create encoder failed: %s\n", esp_err_to_name(rc));
    }
    bool enabled = false;
    if (ok) {
        rc = rmt_enable(chan);
        enabled = rc == ESP_OK;
        ok = enabled;
        if (!ok) Serial.printf("STICKS3_IR_TX: enable failed: %s\n", esp_err_to_name(rc));
    }
    if (ok) {
        rmt_transmit_config_t tx = {};
        tx.loop_count = 0;
        tx.flags.eot_level = 0; // Leave the IR LED off after each command.
        for (unsigned repeat = 0; repeat <= extraRepeats; ++repeat) {
            if (repeat) delay(40);
            rc = rmt_transmit(chan, encoder, symbols.data(),
                              symbols.size() * sizeof(rmt_symbol_word_t), &tx);
            if (rc == ESP_OK) rc = rmt_tx_wait_all_done(chan, 2000);
            if (rc != ESP_OK) {
                Serial.printf("STICKS3_IR_TX: transmit failed: %s\n", esp_err_to_name(rc));
                ok = false;
                break;
            }
        }
    }
    if (enabled) rmt_disable(chan);
    if (encoder) rmt_del_encoder(encoder);
    if (chan) rmt_del_channel(chan);
    Serial.printf("STICKS3_IR_TX: %s\n", ok ? "COMPLETE" : "FAILED");
    return ok;
}

// Sends an actual NEC protocol waveform using the same direct-RMT TX approach
// as the working standalone test, not the IRremoteESP8266 LEDC driver.
// Standard 8-bit NEC: address and inverse; extended NEC: full 16-bit address.
inline bool sticks3RmtSendNec(int pin, uint16_t address, uint8_t command,
                            bool extended, unsigned extraRepeats = 0) {
    const uint16_t a = extended ? address
        : static_cast<uint16_t>((uint16_t(uint8_t(~address)) << 8) | uint8_t(address));
    const uint32_t frame = uint32_t(a) | (uint32_t(command) << 16) |
                           (uint32_t(uint8_t(~command)) << 24);
    uint16_t widths[68] = {};
    size_t n = 0;
    widths[n++] = 9000;
    widths[n++] = 4500;
    for (int b = 0; b < 32; ++b) {
        widths[n++] = 560;
        widths[n++] = (frame & (1UL << b)) ? 1690 : 560;
    }
    widths[n++] = 560; // NEC stop mark
    widths[n++] = 0;   // no trailing space required
    Serial.printf("STICKS3_IR_NEC: pin=%d address=%04X cmd=%02X frame=%08lX extended=%d\n",
                  pin, address, command, (unsigned long)frame, (int)extended);
    return sticks3RmtSendRaw(pin, widths, n, 38000, extraRepeats);
}

inline bool sticks3RmtSendNecTest(int pin) {
    // Identical NEC parameters to M5Stack's documented test example.
    return sticks3RmtSendNec(pin, 0x0000, 0x55, false, 1);
}
#endif // STICKS3_RMT_IR
