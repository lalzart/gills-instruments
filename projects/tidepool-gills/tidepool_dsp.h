// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Uses DSP derived from Mutable Instruments Braids and Clouds by Emilie Gillet.

#ifndef TIDEPOOL_DSP_H_
#define TIDEPOOL_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "braids/resources.h"
#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/grain.h"
#include "clouds/dsp/parameters.h"
#include "clouds/resources.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/rsqrt.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"

namespace tidepool {

static inline float Clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline int32_t ClampInt(int32_t value, int32_t low, int32_t high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float Q27ToFloat(int32_t value) {
  return Clamp01(static_cast<float>(value) * (1.0f / 134217728.0f));
}

class MainOscillator {
 public:
  void Init() {
    phase_ = 0;
    phase_increment_ = 1;
  }

  void SetFrequency(float frequency) {
    frequency = std::max(20.0f, std::min(6000.0f, frequency));
    phase_increment_ = static_cast<uint32_t>(frequency * 89478.485333f);
  }

  void Reset() { phase_ = 0; }

  void Render(float* output, size_t size, float timbre, uint8_t mode) {
    timbre = Clamp01(timbre);
    const int32_t fold_parameter = static_cast<int32_t>(timbre * 32767.0f);
    const int32_t fold_gain = 2048 + ((fold_parameter * 30720) >> 15);
    while (size--) {
      phase_ += phase_increment_;
      const int16_t sine_sample =
          stmlib::Interpolate824(braids::wav_sine, phase_);
      if (mode == 0) {
        const uint32_t phase_16 = phase_ >> 16;
        const float triangle = phase_16 < 32768
            ? -1.0f + static_cast<float>(phase_16) * (1.0f / 16384.0f)
            : 3.0f - static_cast<float>(phase_16) * (1.0f / 16384.0f);
        const float sine = static_cast<float>(sine_sample) *
            (1.0f / 32768.0f);
        const float triangle_mix = 0.12f + 0.48f * timbre;
        *output++ = sine + (triangle - sine) * triangle_mix;
      } else {
        const int32_t driven =
            (static_cast<int32_t>(sine_sample) * fold_gain) >> 15;
        const int16_t folded = stmlib::Interpolate88(
            braids::ws_sine_fold,
            static_cast<uint16_t>(driven + 32768));
        *output++ = static_cast<float>(folded) * (1.0f / 32768.0f);
      }
    }
  }

 private:
  uint32_t phase_;
  uint32_t phase_increment_;
};

class FluteVoice {
 public:
  void Init() {
    bore_ = static_cast<int8_t*>(sdram_malloc(kBoreLength));
    jet_ = static_cast<int8_t*>(sdram_malloc(kJetLength));
    available_ = bore_ != NULL && jet_ != NULL;
    if (available_) {
      memset(bore_, 0, kBoreLength);
      memset(jet_, 0, kJetLength);
    }
    pitch_ = 60 * 128;
    parameter_[0] = 12000;
    parameter_[1] = 14000;
    delay_ptr_ = 0;
    excitation_ptr_ = 0;
    lp_state_ = 0;
    dc_x_ = 0;
    dc_y_ = 0;
  }

  void set_pitch(int16_t pitch) { pitch_ = pitch; }

  void set_parameters(int16_t timbre, int16_t color) {
    parameter_[0] = timbre;
    parameter_[1] = color;
  }

  bool available() const { return available_; }

  void Render(int32_t* output, size_t size, bool strike) {
    if (!available_) {
      std::fill(output, output + size, 0);
      return;
    }

    uint16_t delay_ptr = delay_ptr_;
    uint16_t excitation_ptr = excitation_ptr_;
    int32_t lp_state = lp_state_;
    int32_t dc_x = dc_x_;
    int32_t dc_y = dc_y_;

    if (strike) {
      excitation_ptr = 0;
      memset(bore_, 0, kBoreLength);
      memset(jet_, 0, kJetLength);
      lp_state = 0;
      dc_x = 0;
      dc_y = 0;
    }

    uint32_t bore_delay = (ComputeDelay(pitch_) << 1) - (2 << 16);
    uint32_t jet_delay = (bore_delay >> 8) * (48 + (parameter_[1] >> 10));
    bore_delay -= jet_delay;
    while (bore_delay > ((kBoreLength - 1) << 16) ||
           jet_delay > ((kJetLength - 1) << 16)) {
      bore_delay >>= 1;
      jet_delay >>= 1;
    }

    const uint16_t bore_integral = bore_delay >> 16;
    const uint16_t bore_fractional = bore_delay & 0xffff;
    const uint16_t jet_integral = jet_delay >> 16;
    const uint16_t jet_fractional = jet_delay & 0xffff;
    const uint16_t breath_intensity = 2100 - (parameter_[0] >> 4);
    const uint16_t filter_coefficient = braids::lut_flute_body_filter[pitch_ >> 7];

    for (size_t i = 0; i < size; ++i) {
      const uint16_t bore_read = delay_ptr + 2 * kBoreLength - bore_integral;
      const uint16_t jet_read = delay_ptr + 2 * kJetLength - jet_integral;
      const int16_t bore_a = bore_[bore_read % kBoreLength];
      const int16_t bore_b = bore_[(bore_read - 1) % kBoreLength];
      const int16_t jet_a = jet_[jet_read % kJetLength];
      const int16_t jet_b = jet_[(jet_read - 1) % kJetLength];
      const int32_t bore_value =
          stmlib::Mix(bore_a, bore_b, bore_fractional) << 9;
      const int32_t jet_value =
          stmlib::Mix(jet_a, jet_b, jet_fractional) << 9;

      int32_t breath = braids::lut_blowing_envelope[excitation_ptr] << 1;
      int32_t random_pressure =
          stmlib::Random::GetSample() * breath_intensity >> 12;
      random_pressure = random_pressure * breath >> 15;
      // The original Fluted model uses a conspicuous breath-noise component.
      // Tidepool is intended to sound clean and organic rather than breathy,
      // so retain only a quiet trace of that excitation noise.
      random_pressure >>= 2;
      breath += random_pressure;

      lp_state = (-filter_coefficient * bore_value +
                  (4096 - filter_coefficient) * lp_state) >> 12;
      int32_t reflection = lp_state;
      dc_y = (kDcBlockingPole * dc_y >> 12) + reflection - dc_x;
      dc_x = reflection;
      reflection = dc_y;

      int32_t pressure_delta = breath - (reflection >> 1);
      jet_[delay_ptr % kJetLength] = pressure_delta >> 9;

      int32_t jet_index = ClampInt(jet_value, 0, 65535);
      pressure_delta = static_cast<int16_t>(
          braids::lut_blowing_jet[jet_index >> 8]) + (reflection >> 1);
      bore_[delay_ptr % kBoreLength] = pressure_delta >> 9;
      ++delay_ptr;

      int32_t sample = bore_value >> 1;
      sample = ClampInt(sample, -32768, 32767);
      output[i] = sample << 11;

      // Retains the excitation timing of the established Ksoloti derivative.
      if ((size - i - 1) & 3) {
        ++excitation_ptr;
      }
    }

    if (excitation_ptr >= braids::LUT_BLOWING_ENVELOPE_SIZE - 32) {
      excitation_ptr = braids::LUT_BLOWING_ENVELOPE_SIZE - 32;
    }
    delay_ptr_ = delay_ptr;
    excitation_ptr_ = excitation_ptr;
    lp_state_ = lp_state;
    dc_x_ = dc_x;
    dc_y_ = dc_y;
  }

 private:
  static const size_t kBoreLength = 4096;
  static const size_t kJetLength = 1024;
  static const uint16_t kDcBlockingPole = 4055;  // 0.99 in Q12.
  static const uint16_t kHighestNote = 140 * 128;
  static const uint16_t kPitchTableStart = 128 * 128;
  static const uint16_t kOctave = 12 * 128;

  uint32_t ComputeDelay(int16_t midi_pitch) const {
    if (midi_pitch >= kHighestNote - kOctave) {
      midi_pitch = kHighestNote - kOctave;
    }
    int32_t reference = midi_pitch - kPitchTableStart;
    size_t shifts = 0;
    while (reference < 0) {
      reference += kOctave;
      ++shifts;
    }
    const uint32_t a = braids::lut_oscillator_delays[reference >> 4];
    const uint32_t b = braids::lut_oscillator_delays[(reference >> 4) + 1];
    uint32_t delay = a +
        (static_cast<int32_t>(b - a) * (reference & 0xf) >> 4);
    delay >>= 12 - shifts;
    return delay;
  }

  bool available_;
  int8_t* jet_;
  int8_t* bore_;
  int16_t pitch_;
  int16_t parameter_[2];
  uint16_t delay_ptr_;
  uint16_t excitation_ptr_;
  int32_t lp_state_;
  int32_t dc_x_;
  int32_t dc_y_;
};

// Grain lengths are scaled from Clouds' 32 kHz rate to Ksoloti's 48 kHz rate.
class GranularPlayer48k {
 public:
  static const int32_t kNumGrains = 12;

  void Init() {
    for (int32_t i = 0; i < kNumGrains; ++i) {
      grains_[i].Init();
    }
    num_grains_ = 0.0f;
    gain_normalization_ = 1.0f;
    grain_size_hint_ = 1536.0f;
    grain_rate_phasor_ = 0.0f;
  }

  void Play(const clouds::AudioBuffer<clouds::RESOLUTION_16_BIT>* buffer,
            const clouds::Parameters& parameters,
            float* output,
            size_t size) {
    float overlap = Clamp01(parameters.granular.overlap);
    overlap = overlap * overlap * overlap;
    const float target = kNumGrains * overlap;
    const float probability = target > 0.0f
        ? target / std::max(32.0f, grain_size_hint_) : 0.0f;
    const float spacing = target > 0.0f
        ? grain_size_hint_ / target : 1.0e9f;

    int32_t available = FillAvailable();
    bool trigger = parameters.trigger;
    for (size_t t = 0; t < size; ++t) {
      grain_rate_phasor_ += 1.0f;
      const bool random_seed = !parameters.granular.use_deterministic_seed &&
          stmlib::Random::GetFloat() < probability &&
          target > num_grains_;
      const bool clocked_seed = parameters.granular.use_deterministic_seed &&
          grain_rate_phasor_ >= spacing;
      if (available && (random_seed || clocked_seed || trigger)) {
        --available;
        const int32_t index = available_grains_[available];
        const clouds::GrainQuality quality = available < 3
            ? clouds::GRAIN_QUALITY_HIGH : clouds::GRAIN_QUALITY_MEDIUM;
        Schedule(&grains_[index], parameters, t, buffer->size(),
                 buffer->head() - size + t, quality);
        grain_rate_phasor_ = 0.0f;
        trigger = false;
      }
    }

    std::fill(output, output + size * 2, 0.0f);
    for (int32_t i = 0; i < kNumGrains; ++i) {
      clouds::Grain* grain = &grains_[i];
      if (grain->recommended_quality() == clouds::GRAIN_QUALITY_HIGH) {
        grain->OverlapAdd<1, clouds::GRAIN_QUALITY_HIGH>(
            buffer, output, envelope_, size);
      } else if (grain->recommended_quality() == clouds::GRAIN_QUALITY_MEDIUM) {
        grain->OverlapAdd<1, clouds::GRAIN_QUALITY_MEDIUM>(
            buffer, output, envelope_, size);
      } else {
        grain->OverlapAdd<1, clouds::GRAIN_QUALITY_LOW>(
            buffer, output, envelope_, size);
      }
    }

    const int32_t active = kNumGrains - available;
    const float grain_slope = active > num_grains_ ? 0.9f : 0.2f;
    num_grains_ += grain_slope * (active - num_grains_);
    float normalization = num_grains_ > 2.0f
        ? stmlib::fast_rsqrt_carmack(num_grains_ - 1.0f) : 1.0f;
    const float window_gain = std::min(
        2.0f, 1.0f + 2.0f * parameters.granular.window_shape);
    normalization *= stmlib::Crossfade(
        1.0f, window_gain, parameters.granular.overlap);
    gain_normalization_ += 0.01f * (normalization - gain_normalization_);
    for (size_t i = 0; i < size * 2; ++i) {
      output[i] *= gain_normalization_;
    }
  }

 private:
  int32_t FillAvailable() {
    int32_t count = 0;
    for (int32_t i = 0; i < kNumGrains; ++i) {
      if (!grains_[i].active()) {
        available_grains_[count++] = i;
      }
    }
    return count;
  }

  void Schedule(clouds::Grain* grain,
                const clouds::Parameters& parameters,
                int32_t pre_delay,
                int32_t buffer_size,
                int32_t buffer_head,
                clouds::GrainQuality quality) {
    const float pitch_ratio = stmlib::SemitonesToRatio(parameters.pitch);
    const float inverse_pitch = stmlib::SemitonesToRatio(-parameters.pitch);
    float grain_size = stmlib::Interpolate(
        clouds::lut_grain_size, Clamp01(parameters.size), 256.0f) * 1.5f;
    if (pitch_ratio > 1.0f) {
      grain_size = std::min(
          grain_size, buffer_size * 0.25f * inverse_pitch);
    }

    const float eaten_playing = grain_size * pitch_ratio;
    const float available = std::max(
        0.0f, buffer_size - eaten_playing - grain_size);
    const int32_t width = std::max(
        static_cast<int32_t>(2),
        static_cast<int32_t>(grain_size) & static_cast<int32_t>(~1));
    const int32_t start = buffer_head - static_cast<int32_t>(
        Clamp01(parameters.position) * available + eaten_playing);
    const float pan = 0.5f + Clamp01(parameters.stereo_spread) *
        (stmlib::Random::GetFloat() - 0.5f);
    const float gain_l = stmlib::Interpolate(clouds::lut_sin, pan, 256.0f);
    const float gain_r = stmlib::Interpolate(clouds::lut_sin + 256, pan, 256.0f);
    grain->Start(pre_delay, buffer_size, start, width,
                 static_cast<uint32_t>(pitch_ratio * 65536.0f),
                 Clamp01(parameters.granular.window_shape), gain_l, gain_r,
                 quality);
    grain_size_hint_ += 0.1f * (grain_size - grain_size_hint_);
  }

  clouds::Grain grains_[kNumGrains];
  int32_t available_grains_[kNumGrains];
  float envelope_[BUFSIZE];
  float num_grains_;
  float gain_normalization_;
  float grain_size_hint_;
  float grain_rate_phasor_;
};

class WoodResonator {
 public:
  void Init() {
    for (size_t i = 0; i < kNumModes; ++i) {
      y1_[i] = 0.0f;
      y2_[i] = 0.0f;
      coefficient_[i] = 0.0f;
      radius_squared_[i] = 0.0f;
      drive_[i] = 0.0f;
    }
    SetFrequency(261.6256f);
  }

  void SetFrequency(float fundamental) {
    static const float ratios[kNumModes] = {1.0f, 2.61f, 4.18f};
    static const float radii[kNumModes] = {0.9960f, 0.9915f, 0.9850f};
    for (size_t i = 0; i < kNumModes; ++i) {
      const float frequency = std::min(16000.0f, fundamental * ratios[i]);
      const float radius = radii[i];
      coefficient_[i] = 2.0f * radius *
          cosf(6.28318530718f * frequency * (1.0f / 48000.0f));
      radius_squared_[i] = radius * radius;
      drive_[i] = 1.0f - radius;
    }
  }

  float Process(float input, float amount) {
    static const float gains[kNumModes] = {0.62f, 0.27f, 0.13f};
    float body = 0.0f;
    for (size_t i = 0; i < kNumModes; ++i) {
      const float value = drive_[i] * input + coefficient_[i] * y1_[i] -
          radius_squared_[i] * y2_[i];
      y2_[i] = y1_[i];
      y1_[i] = value;
      body += value * gains[i];
    }
    return input * (1.0f - 0.28f * amount) + body * amount;
  }

 private:
  static const size_t kNumModes = 3;
  float coefficient_[kNumModes];
  float radius_squared_[kNumModes];
  float drive_[kNumModes];
  float y1_[kNumModes];
  float y2_[kNumModes];
};

class Instrument {
 public:
  void Init() {
    flute_.Init();
    main_oscillator_.Init();
    player_.Init();
    resonator_.Init();

    record_memory_ = static_cast<int16_t*>(
        sdram_malloc(sizeof(int16_t) * kRecordStorageSamples));
    granular_available_ = record_memory_ != NULL;
    if (granular_available_) {
      record_buffer_.Init(record_memory_, kRecordStorageSamples, record_tail_);
    }

    phase_ = 0;
    stage_ = 0;
    running_ = true;
    locked_ = false;
    captured_ = false;
    previous_oscillator_ = previous_mutate_ = previous_lock_ = false;
    previous_capture_ = false;
    encoder_switch_stable_ = false;
    encoder_switch_candidate_ = false;
    encoder_switch_debounce_count_ = 0;
    encoder_hold_blocks_ = 0;
    encoder_long_action_ = false;
    scale_ = 0;
    wave_destination_ = 0;
    oscillator_mode_ = 0;
    last_root_ = 60;
    pitch_dirty_ = true;
    random_state_ = 0x74696465;  // "tide"
    for (size_t i = 0; i < 4; ++i) {
      mutation_[i] = 0;
    }
    sub_division_ = 2;
    sub_phase_ = 0;
    sub_increment_ = 0;
    energy_ = 0.0f;
    lpg_state_ = 0.0f;
    oscillator_fade_ = 1.0f;
    strike_pending_ = true;
    display_divider_ = 0;
    SetPitch(60, 0.5f, true);
    UpdateDisplay(60, NULL, 0.75f, 0.5f, false);
  }

  void Process(const int32_t stage_controls[4],
               int32_t rate_control,
               int32_t memory_control,
               int32_t timbre_control,
               int32_t position_control,
               int32_t size_control,
               int32_t texture_control,
               bool oscillator_button,
               bool mutate_button,
               bool lock_button,
               bool freeze_button,
               int32_t encoder,
               bool encoder_switch,
               int32_t* left,
               int32_t* right) {
    HandleButtons(oscillator_button, mutate_button, lock_button, freeze_button,
                  encoder_switch);

    const int32_t root = ClampInt(encoder, 36, 72);
    float stages[4];
    for (size_t i = 0; i < 4; ++i) {
      stages[i] = Q27ToFloat(stage_controls[i]);
    }
    const float memory = Q27ToFloat(memory_control);
    const float rate_value = Q27ToFloat(rate_control);
    const float rate_hz = 0.08f + 5.92f * rate_value * rate_value;
    const uint32_t phase_increment = static_cast<uint32_t>(
        rate_hz * (4294967296.0f * BUFSIZE / 48000.0f));

    bool stage_changed = false;
    bool cycle_wrapped = false;
    const uint32_t old_phase = phase_;
    phase_ += phase_increment;
    cycle_wrapped = phase_ < old_phase;
    const uint8_t new_stage = phase_ >> 30;
    stage_changed = new_stage != stage_;
    stage_ = new_stage;

    if (cycle_wrapped) {
      strike_pending_ = true;
      if (!locked_ && RandomUnit() > memory * memory) {
        Mutate();
      }
    }
    const bool wave_pitch = wave_destination_ == 0 || wave_destination_ == 3;
    const bool wave_breath = wave_destination_ == 1 || wave_destination_ == 3;
    const bool wave_grain = wave_destination_ == 2 || wave_destination_ == 3;
    if (pitch_dirty_ || root != last_root_ ||
        (wave_pitch && (stage_changed || cycle_wrapped))) {
      SetPitch(root, wave_pitch ? stages[stage_] : 0.45f, wave_pitch);
    }

    const uint8_t next_stage = (stage_ + 1) & 3;
    const float t = static_cast<float>(phase_ & 0x3fffffff) *
        (1.0f / 1073741824.0f);
    const float smooth_t = t * t * (3.0f - 2.0f * t);
    const float gesture = Clamp01(
        stages[stage_] + (stages[next_stage] - stages[stage_]) * smooth_t);

    const float timbre = Q27ToFloat(timbre_control);
    const float animated_timbre = Clamp01(
        timbre + (wave_breath ? (gesture - 0.5f) * 0.28f : 0.0f));
    flute_.set_parameters(
        static_cast<int16_t>((1.0f - animated_timbre) * 22000.0f),
        static_cast<int16_t>((0.15f + 0.75f * animated_timbre) * 32767.0f));
    if (oscillator_mode_ == 0) {
      flute_.Render(source_q27_, BUFSIZE, strike_pending_);
    } else {
      main_oscillator_.Render(
          oscillator_output_, BUFSIZE, timbre, oscillator_mode_ - 1);
    }
    strike_pending_ = false;

    float dry[BUFSIZE];
    const float energy_target = wave_breath
        ? 0.05f + 0.95f * gesture : 0.62f;
    const float cutoff = 0.012f + 0.075f * animated_timbre +
        (wave_breath ? 0.105f * gesture : 0.045f);
    const float body_amount = 0.16f + 0.30f * (1.0f - timbre);
    float main_source_gain;
    if (oscillator_mode_ == 0) {
      main_source_gain = 0.78f;
    } else if (oscillator_mode_ == 1) {
      main_source_gain = 0.12f + 0.03f * timbre;
    } else {
      main_source_gain = timbre < 0.5f
          ? 0.96f - 1.488f * timbre
          : 0.216f - 0.10f * (timbre - 0.5f);
    }
    for (size_t i = 0; i < BUFSIZE; ++i) {
      energy_ += 0.0025f * (energy_target - energy_);
      oscillator_fade_ += 0.0025f * (1.0f - oscillator_fade_);
      const float main_source = oscillator_mode_ == 0
          ? source_q27_[i] * (1.0f / 134217728.0f)
          : oscillator_output_[i];
      sub_phase_ += sub_increment_;
      const float sub = stmlib::Interpolate824(braids::wav_sine, sub_phase_) *
          (1.0f / 32768.0f);
      const float source = main_source * main_source_gain * oscillator_fade_ +
          sub * 0.16f;
      lpg_state_ += cutoff * (source - lpg_state_);
      dry[i] = resonator_.Process(lpg_state_ * energy_, body_amount);
    }

    const float grain_wave_depth = wave_destination_ == 2
        ? 0.78f : (wave_destination_ == 3 ? 0.38f : 0.0f);
    const float position = Clamp01(
        Q27ToFloat(position_control) + (gesture - 0.5f) * grain_wave_depth);
    const float grain_size = Q27ToFloat(size_control);
    const float texture = Q27ToFloat(texture_control);
    const float granular_texture = wave_grain
        ? texture * (wave_destination_ == 2
            ? 0.28f + 0.72f * gesture
            : 0.55f + 0.45f * gesture)
        : texture;

    if (granular_available_) {
      record_buffer_.WriteFade(dry, BUFSIZE, 1, !captured_);
      clouds::Parameters parameters;
      memset(&parameters, 0, sizeof(parameters));
      parameters.position = position;
      parameters.size = grain_size;
      parameters.pitch = 0.0f;
      parameters.stereo_spread = 0.25f + 0.75f * granular_texture;
      parameters.trigger = stage_changed && granular_texture > 0.15f;
      parameters.granular.overlap = 0.08f + 0.78f * granular_texture;
      parameters.granular.window_shape = 0.20f + 0.75f * granular_texture;
      parameters.granular.use_deterministic_seed = false;
      if (granular_texture > 0.01f) {
        player_.Play(&record_buffer_, parameters, grain_output_, BUFSIZE);
      } else {
        std::fill(grain_output_, grain_output_ + BUFSIZE * 2, 0.0f);
      }
    } else {
      std::fill(grain_output_, grain_output_ + BUFSIZE * 2, 0.0f);
    }

    const float wet = granular_available_
        ? granular_texture * (0.58f + 0.22f * granular_texture) : 0.0f;
    for (size_t i = 0; i < BUFSIZE; ++i) {
      float l = dry[i] * (1.0f - 0.55f * wet) + grain_output_[2 * i] * wet;
      float r = dry[i] * (1.0f - 0.55f * wet) + grain_output_[2 * i + 1] * wet;
      l = l / (1.0f + fabsf(l));
      r = r / (1.0f + fabsf(r));
      left[i] = static_cast<int32_t>(l * 108000000.0f);
      right[i] = static_cast<int32_t>(r * 108000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay(root, stages, memory, texture, captured_);
    }
  }

  uint8_t stage() const { return stage_; }
  bool running() const { return running_; }
  bool locked() const { return locked_; }
  bool granular_available() const { return granular_available_; }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static const int32_t kRecordSamples = 96000;
  static const int32_t kRecordStorageSamples = kRecordSamples + 8;

  void HandleButtons(bool oscillator,
                     bool mutate,
                     bool lock,
                     bool capture,
                     bool encoder_switch) {
    if (oscillator && !previous_oscillator_) {
      oscillator_mode_ = (oscillator_mode_ + 1) % 3;
      main_oscillator_.Reset();
      oscillator_fade_ = 0.0f;
      strike_pending_ = true;
    }
    if (mutate && !previous_mutate_) {
      Mutate();
    }
    if (lock && !previous_lock_) {
      locked_ = !locked_;
    }
    if (capture && !previous_capture_) {
      captured_ = !captured_;
    }
    previous_oscillator_ = oscillator;
    previous_mutate_ = mutate;
    previous_lock_ = lock;
    previous_capture_ = capture;
    HandleEncoderSwitch(encoder_switch);
  }

  void HandleEncoderSwitch(bool raw_switch) {
    if (raw_switch == encoder_switch_candidate_) {
      if (encoder_switch_debounce_count_ < 8) {
        ++encoder_switch_debounce_count_;
      }
    } else {
      encoder_switch_candidate_ = raw_switch;
      encoder_switch_debounce_count_ = 0;
    }

    if (encoder_switch_debounce_count_ >= 8 &&
        encoder_switch_stable_ != encoder_switch_candidate_) {
      encoder_switch_stable_ = encoder_switch_candidate_;
      encoder_switch_debounce_count_ = 0;
      if (encoder_switch_stable_) {
        encoder_hold_blocks_ = 0;
        encoder_long_action_ = false;
      } else {
        if (!encoder_long_action_) {
          scale_ = (scale_ + 1) & 3;
          pitch_dirty_ = true;
          strike_pending_ = true;
        }
        encoder_hold_blocks_ = 0;
        encoder_long_action_ = false;
      }
    }

    if (encoder_switch_stable_ && !encoder_long_action_) {
      if (encoder_hold_blocks_ < 65535) {
        ++encoder_hold_blocks_;
      }
      if (encoder_hold_blocks_ >= 1500) {
        wave_destination_ = (wave_destination_ + 1) & 3;
        encoder_long_action_ = true;
        pitch_dirty_ = true;
        strike_pending_ = true;
      }
    }
  }

  uint32_t RandomWord() {
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return random_state_;
  }

  float RandomUnit() {
    return static_cast<float>(RandomWord() & 0x00ffffff) *
        (1.0f / 16777216.0f);
  }

  void Mutate() {
    const uint8_t slot = RandomWord() & 3;
    mutation_[slot] += (RandomWord() & 1) ? 1 : -1;
    mutation_[slot] = ClampInt(mutation_[slot], -2, 2);
    if ((RandomWord() & 7) == 0) {
      sub_division_ = 2 + (RandomWord() % 4);
    }
    pitch_dirty_ = true;
  }

  void SetPitch(int32_t root, float height, bool apply_mutation) {
    static const int8_t scales[4][6] = {
      {0, 2, 4, 7, 9, 12},
      {0, 2, 3, 5, 7, 10},
      {0, 2, 3, 5, 7, 9},
      {0, 3, 5, 7, 10, 12}
    };
    int32_t degree = static_cast<int32_t>(height * 5.99f);
    if (apply_mutation) {
      degree += mutation_[stage_];
    }
    degree = ClampInt(degree, 0, 5);
    const int32_t note = ClampInt(root + scales[scale_][degree], 24, 96);
    flute_.set_pitch(note * 128);
    const float note_frequency = 440.0f *
        powf(2.0f, (note - 69) / 12.0f);
    resonator_.SetFrequency(note_frequency);
    main_oscillator_.SetFrequency(note_frequency);
    const float frequency = note_frequency / static_cast<float>(sub_division_);
    sub_increment_ = static_cast<uint32_t>(
        frequency * (4294967296.0f / 48000.0f));
    last_root_ = root;
    pitch_dirty_ = false;
  }

  static void ClearLine(char* line) {
    for (size_t i = 0; i < 21; ++i) line[i] = ' ';
    line[21] = '\0';
  }

  static void Put(char* line, size_t offset, const char* text) {
    while (*text && offset < 21) line[offset++] = *text++;
  }

  void UpdateDisplay(int32_t root,
                     const float* stages,
                     float memory,
                     float texture,
                     bool captured) {
    static const char* scale_names[4] = {"MAJ5", "MIN5", "DOR", "HARM"};
    static const char* destination_names[4] = {"PIT", "BRTH", "GRN", "ALL"};
    static const char* oscillator_names[3] = {"FLUT", "RND ", "FOLD"};
    static const char note_names[12][2] = {
      {'C',' '}, {'C','#'}, {'D',' '}, {'D','#'}, {'E',' '}, {'F',' '},
      {'F','#'}, {'G',' '}, {'G','#'}, {'A',' '}, {'A','#'}, {'B',' '}
    };
    ClearLine(line1_); ClearLine(line2_); ClearLine(line3_); ClearLine(line4_);
    Put(line1_, 0, "TIDE");
    Put(line1_, 5, destination_names[wave_destination_]);
    Put(line1_, 10, "NOTE");
    line1_[15] = note_names[root % 12][0];
    line1_[16] = note_names[root % 12][1];
    line1_[17] = '0' + ClampInt(root / 12 - 1, 0, 9);

    Put(line2_, 0, "WAVE 0-0-0-0 STEP 1");
    if (stages != NULL) {
      line2_[5] = '0' + ClampInt(static_cast<int32_t>(stages[0] * 9.99f), 0, 9);
      line2_[7] = '0' + ClampInt(static_cast<int32_t>(stages[1] * 9.99f), 0, 9);
      line2_[9] = '0' + ClampInt(static_cast<int32_t>(stages[2] * 9.99f), 0, 9);
      line2_[11] = '0' + ClampInt(static_cast<int32_t>(stages[3] * 9.99f), 0, 9);
    }
    line2_[18] = '1' + stage_;

    Put(line3_, 0, "MEM 00%  GRAIN 00%");
    const int32_t mem_percent = ClampInt(static_cast<int32_t>(memory * 99.0f), 0, 99);
    const int32_t grain_percent = ClampInt(static_cast<int32_t>(texture * 99.0f), 0, 99);
    line3_[4] = '0' + mem_percent / 10;
    line3_[5] = '0' + mem_percent % 10;
    line3_[15] = '0' + grain_percent / 10;
    line3_[16] = '0' + grain_percent % 10;

    Put(line4_, 0, scale_names[scale_]);
    Put(line4_, 6, oscillator_names[oscillator_mode_]);
    Put(line4_, 11, locked_ ? "LOCK" : "EVOLVE");
    if (captured) Put(line4_, 17, "CAP");
    if (!granular_available_) Put(line4_, 11, "NO GRAIN");
  }

  FluteVoice flute_;
  MainOscillator main_oscillator_;
  GranularPlayer48k player_;
  WoodResonator resonator_;
  clouds::AudioBuffer<clouds::RESOLUTION_16_BIT> record_buffer_;
  int16_t* record_memory_;
  int16_t record_tail_[256];
  bool granular_available_;

  uint32_t phase_;
  uint8_t stage_;
  bool running_;
  bool locked_;
  bool captured_;
  bool previous_oscillator_;
  bool previous_mutate_;
  bool previous_lock_;
  bool previous_capture_;
  bool encoder_switch_stable_;
  bool encoder_switch_candidate_;
  uint8_t encoder_switch_debounce_count_;
  uint16_t encoder_hold_blocks_;
  bool encoder_long_action_;
  uint8_t scale_;
  uint8_t wave_destination_;
  uint8_t oscillator_mode_;
  int32_t last_root_;
  bool pitch_dirty_;
  int8_t mutation_[4];
  uint8_t sub_division_;
  uint32_t random_state_;
  uint32_t sub_phase_;
  uint32_t sub_increment_;
  float energy_;
  float lpg_state_;
  float oscillator_fade_;
  bool strike_pending_;
  uint8_t display_divider_;

  int32_t source_q27_[BUFSIZE];
  float oscillator_output_[BUFSIZE];
  float grain_output_[BUFSIZE * 2];
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace tidepool

#endif  // TIDEPOOL_DSP_H_
