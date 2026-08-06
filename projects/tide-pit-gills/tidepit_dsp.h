// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Uses DSP derived from Mutable Instruments Braids and Clouds by Emilie Gillet.

#ifndef TIDEPIT_DSP_H_
#define TIDEPIT_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/grain.h"
#include "clouds/dsp/parameters.h"
#include "clouds/resources.h"
#include "stmlib/dsp/rsqrt.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "./tidepit_voice.h"

namespace tidepit {

enum EffectMode {
  EFFECT_CLEAN,
  EFFECT_FILTER,
  EFFECT_DRIVE,
  EFFECT_MODE_COUNT
};

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

// Tidepool can schedule twelve mixed-quality grains. Tide Pit spends the same
// idea on six voices, always using Clouds' high-quality interpolation. Slight
// per-grain pitch variation keeps the spatial layer from being six copies of
// the same mono spectrum.
class DetailedGranularPlayer {
 public:
  static const int32_t kNumGrains = 6;

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
        Schedule(&grains_[index], parameters, t, buffer->size(),
                 buffer->head() - size + t);
        grain_rate_phasor_ = 0.0f;
        trigger = false;
      }
    }

    std::fill(output, output + size * 2, 0.0f);
    for (int32_t i = 0; i < kNumGrains; ++i) {
      clouds::Grain* grain = &grains_[i];
      grain->OverlapAdd<1, clouds::GRAIN_QUALITY_HIGH>(
          buffer, output, envelope_, size);
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
                int32_t buffer_head) {
    const float micro_pitch =
        (stmlib::Random::GetFloat() - 0.5f) * 0.10f;
    const float pitch_ratio =
        stmlib::SemitonesToRatio(parameters.pitch + micro_pitch);
    const float inverse_pitch =
        stmlib::SemitonesToRatio(-parameters.pitch - micro_pitch);
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
    const float gain_r =
        stmlib::Interpolate(clouds::lut_sin + 256, pan, 256.0f);
    grain->Start(pre_delay, buffer_size, start, width,
                 static_cast<uint32_t>(pitch_ratio * 65536.0f),
                 Clamp01(parameters.granular.window_shape), gain_l, gain_r,
                 clouds::GRAIN_QUALITY_HIGH);
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

class Instrument {
 public:
  void Init() {
    waveguide_.Init();
    main_oscillator_.Init();
    sympathetic_.Init();
    player_.Init();
    body_.Init();
    reverb_.Init();

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
    effect_button_stable_ = false;
    effect_button_candidate_ = false;
    effect_button_debounce_count_ = 0;
    effect_hold_blocks_ = 0;
    effect_long_action_ = false;
    encoder_switch_stable_ = false;
    encoder_switch_candidate_ = false;
    encoder_switch_debounce_count_ = 0;
    encoder_hold_blocks_ = 0;
    encoder_long_action_ = false;
    scale_ = 0;
    wave_destination_ = 0;
    oscillator_mode_ = 0;
    effect_mode_ = EFFECT_CLEAN;
    previous_effect_mode_ = EFFECT_CLEAN;
    effect_crossfade_ = 1.0f;
    effect_mode_changed_ = false;
    for (size_t i = 0; i < EFFECT_MODE_COUNT; ++i) {
      effect_parameter_a_[i] = 0.5f;
      effect_parameter_b_[i] = 0.25f;
      effect_parameter_initialized_[i] = false;
      effect_parameter_pickup_a_[i] = false;
      effect_parameter_pickup_b_[i] = false;
      previous_effect_control_a_[i] = 0.0f;
      previous_effect_control_b_[i] = 0.0f;
    }
    effect_parameter_a_[EFFECT_CLEAN] = 0.5f;
    effect_parameter_b_[EFFECT_CLEAN] = 0.5f;
    effect_parameter_a_[EFFECT_FILTER] = 0.65f;
    effect_parameter_b_[EFFECT_FILTER] = 0.25f;
    effect_parameter_a_[EFFECT_DRIVE] = 0.50f;
    effect_parameter_b_[EFFECT_DRIVE] = 0.25f;
    effect_parameter_initialized_[EFFECT_CLEAN] = true;
    effect_parameter_pickup_a_[EFFECT_CLEAN] = true;
    effect_parameter_pickup_b_[EFFECT_CLEAN] = true;
    ResetEffectStates();
    UpdateEffectCoefficients();
    last_root_ = 60;
    pitch_dirty_ = true;
    random_state_ = 0x70697421;  // "pit!"
    for (size_t i = 0; i < 4; ++i) mutation_[i] = 0;
    sub_division_ = 2;
    energy_ = 0.0f;
    lpg_state_ = 0.0f;
    oscillator_fade_ = 1.0f;
    strike_pending_ = true;
    display_divider_ = 0;
    SetPitch(60, 0.5f, true);
    UpdateDisplay(60, NULL, 0.75f, 0.5f, 0.5f, false);
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
      if (!locked_ && RandomUnit() > memory * memory) Mutate();
    }

    const bool wave_pitch = wave_destination_ == 0 || wave_destination_ == 3;
    const bool wave_body = wave_destination_ == 1 || wave_destination_ == 3;
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
        timbre + (wave_body ? (gesture - 0.5f) * 0.28f : 0.0f));
    waveguide_.set_parameters(
        static_cast<int16_t>((1.0f - animated_timbre) * 22000.0f),
        static_cast<int16_t>((0.15f + 0.75f * animated_timbre) * 32767.0f));
    if (oscillator_mode_ == 0) {
      waveguide_.Render(waveguide_output_, BUFSIZE, strike_pending_);
    } else {
      main_oscillator_.Render(
          waveguide_output_, BUFSIZE, timbre, oscillator_mode_ - 1);
    }
    strike_pending_ = false;

    body_.SetMaterial(animated_timbre);
    sympathetic_.SetMaterial(animated_timbre);
    const float energy_target = wave_body
        ? 0.05f + 0.95f * gesture : 0.66f;
    const float cutoff = 0.014f + 0.090f * animated_timbre +
        (wave_body ? 0.115f * gesture : 0.050f);
    const float body_amount = 0.30f + 0.34f * (1.0f - timbre);
    float main_source_gain;
    if (oscillator_mode_ == 0) {
      main_source_gain = 0.82f;
    } else if (oscillator_mode_ == 1) {
      main_source_gain = 1.43f * (0.13f + 0.03f * timbre);
    } else {
      main_source_gain = 1.46f * (timbre < 0.5f
          ? 1.04f - 1.608f * timbre
          : 0.236f - 0.12f * (timbre - 0.5f));
    }
    for (size_t i = 0; i < BUFSIZE; ++i) {
      energy_ += 0.0025f * (energy_target - energy_);
      oscillator_fade_ += 0.0025f * (1.0f - oscillator_fade_);
      const float wave = waveguide_output_[i] * oscillator_fade_;
      const float sympathetic = sympathetic_.Process(wave);
      const float source = wave * main_source_gain + sympathetic *
          (oscillator_mode_ == 0 ? 0.30f : 0.18f);
      lpg_state_ += cutoff * (source - lpg_state_);
      body_.Process(lpg_state_ * energy_, body_amount,
                    &dry_left_[i], &dry_right_[i]);
      record_source_[i] = 0.5f * (dry_left_[i] + dry_right_[i]);
    }

    const float grain_wave_depth = wave_destination_ == 2
        ? 0.76f : (wave_destination_ == 3 ? 0.36f : 0.0f);
    const float position = Clamp01(
        Q27ToFloat(position_control) + (gesture - 0.5f) * grain_wave_depth);
    const float raw_size = Q27ToFloat(size_control);
    const float raw_texture = Q27ToFloat(texture_control);
    float grain_size;
    float texture;
    UpdateEffectParameters(raw_size, raw_texture, &grain_size, &texture);
    const float granular_texture = wave_grain
        ? texture * (wave_destination_ == 2
            ? 0.24f + 0.76f * gesture
            : 0.52f + 0.48f * gesture)
        : texture;

    if (granular_available_) {
      record_buffer_.WriteFade(record_source_, BUFSIZE, 1, !captured_);
      clouds::Parameters parameters;
      memset(&parameters, 0, sizeof(parameters));
      parameters.position = position;
      parameters.size = grain_size;
      parameters.pitch = 0.0f;
      parameters.stereo_spread = 0.34f + 0.66f * granular_texture;
      parameters.trigger = stage_changed && granular_texture > 0.14f;
      parameters.granular.overlap = 0.10f + 0.74f * granular_texture;
      parameters.granular.window_shape = 0.26f + 0.68f * granular_texture;
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
        ? granular_texture * (0.42f + 0.20f * granular_texture) : 0.0f;
    for (size_t i = 0; i < BUFSIZE; ++i) {
      frames_[i].l = dry_left_[i] * (1.0f - 0.36f * wet) +
          grain_output_[2 * i] * wet;
      frames_[i].r = dry_right_[i] * (1.0f - 0.36f * wet) +
          grain_output_[2 * i + 1] * wet;
    }
    reverb_.Process(frames_, BUFSIZE, texture, animated_timbre);

    for (size_t i = 0; i < BUFSIZE; ++i) {
      float l = frames_[i].l;
      float r = frames_[i].r;
      ApplyEffects(&l, &r);
      l = stmlib::SoftClip(l * 0.72f);
      r = stmlib::SoftClip(r * 0.72f);
      left[i] = static_cast<int32_t>(l * 126000000.0f);
      right[i] = static_cast<int32_t>(r * 126000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay(root, stages, memory, grain_size, texture, captured_);
    }
  }

  uint8_t stage() const { return stage_; }
  bool granular_available() const { return granular_available_; }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static const int32_t kRecordSamples = 96000;
  static const int32_t kRecordStorageSamples = kRecordSamples + 8;

  void HandleButtons(bool oscillator, bool mutate, bool lock, bool capture,
                     bool encoder_switch) {
    if (oscillator && !previous_oscillator_) {
      oscillator_mode_ = (oscillator_mode_ + 1) % 3;
      main_oscillator_.Reset();
      oscillator_fade_ = 0.0f;
      strike_pending_ = true;
    }
    if (mutate && !previous_mutate_) Mutate();
    if (lock && !previous_lock_) locked_ = !locked_;
    previous_oscillator_ = oscillator;
    previous_mutate_ = mutate;
    previous_lock_ = lock;
    HandleEffectButton(capture);
    HandleEncoderSwitch(encoder_switch);
  }

  void HandleEffectButton(bool raw_button) {
    if (raw_button == effect_button_candidate_) {
      if (effect_button_debounce_count_ < 75) {
        ++effect_button_debounce_count_;
      }
    } else {
      effect_button_candidate_ = raw_button;
      effect_button_debounce_count_ = 0;
    }

    if (effect_button_debounce_count_ >= 75 &&
        effect_button_stable_ != effect_button_candidate_) {
      effect_button_stable_ = effect_button_candidate_;
      effect_button_debounce_count_ = 0;
      if (effect_button_stable_) {
        effect_hold_blocks_ = 0;
        effect_long_action_ = false;
      } else {
        if (!effect_long_action_) {
          previous_effect_mode_ = effect_mode_;
          effect_mode_ = (effect_mode_ + 1) % EFFECT_MODE_COUNT;
          effect_crossfade_ = 0.0f;
          effect_mode_changed_ = true;
          ResetEffectMode(effect_mode_);
        }
        effect_hold_blocks_ = 0;
        effect_long_action_ = false;
      }
    }

    if (effect_button_stable_ && !effect_long_action_) {
      if (effect_hold_blocks_ < 65535) ++effect_hold_blocks_;
      if (effect_hold_blocks_ >= 1500) {
        captured_ = !captured_;
        effect_long_action_ = true;
      }
    }
  }

  static bool PickupCrossed(float previous, float current, float target) {
    const float previous_difference = previous - target;
    const float current_difference = current - target;
    return fabsf(current_difference) < 0.015f ||
        previous_difference * current_difference <= 0.0f;
  }

  void UpdateEffectParameters(float raw_a,
                              float raw_b,
                              float* grain_size,
                              float* texture) {
    const uint8_t mode = effect_mode_;
    if (effect_mode_changed_) {
      if (!effect_parameter_initialized_[mode]) {
        effect_parameter_a_[mode] = raw_a;
        effect_parameter_b_[mode] = raw_b;
        effect_parameter_initialized_[mode] = true;
        effect_parameter_pickup_a_[mode] = true;
        effect_parameter_pickup_b_[mode] = true;
      } else {
        effect_parameter_pickup_a_[mode] = false;
        effect_parameter_pickup_b_[mode] = false;
      }
      previous_effect_control_a_[mode] = raw_a;
      previous_effect_control_b_[mode] = raw_b;
      effect_mode_changed_ = false;
    }

    if (effect_parameter_pickup_a_[mode]) {
      effect_parameter_a_[mode] = raw_a;
    } else if (PickupCrossed(
          previous_effect_control_a_[mode], raw_a,
          effect_parameter_a_[mode])) {
      effect_parameter_pickup_a_[mode] = true;
      effect_parameter_a_[mode] = raw_a;
    }
    if (effect_parameter_pickup_b_[mode]) {
      effect_parameter_b_[mode] = raw_b;
    } else if (PickupCrossed(
          previous_effect_control_b_[mode], raw_b,
          effect_parameter_b_[mode])) {
      effect_parameter_pickup_b_[mode] = true;
      effect_parameter_b_[mode] = raw_b;
    }
    previous_effect_control_a_[mode] = raw_a;
    previous_effect_control_b_[mode] = raw_b;

    *grain_size = effect_parameter_a_[EFFECT_CLEAN];
    *texture = effect_parameter_b_[EFFECT_CLEAN];
    UpdateEffectCoefficients();
  }

  void UpdateEffectCoefficients() {
    const float cutoff = effect_parameter_a_[EFFECT_FILTER];
    const float resonance = effect_parameter_b_[EFFECT_FILTER];
    filter_g_ = 0.004f + 0.996f * cutoff * cutoff;
    filter_k_ = 2.0f - 1.88f * resonance;
    filter_a1_ = 1.0f /
        (1.0f + filter_g_ * (filter_g_ + filter_k_));

    const float tone = effect_parameter_a_[EFFECT_DRIVE];
    const float drive = effect_parameter_b_[EFFECT_DRIVE];
    drive_tone_coefficient_ = 0.008f + 0.35f * tone * tone;
    drive_color_ = 0.18f + 0.82f * tone;
    drive_gain_ = 1.0f + 18.0f * drive * drive;
    drive_mix_ = drive;
  }

  void ResetEffectMode(uint8_t mode) {
    if (mode == EFFECT_FILTER) {
      filter_band_[0] = filter_band_[1] = 0.0f;
      filter_low_[0] = filter_low_[1] = 0.0f;
    } else if (mode == EFFECT_DRIVE) {
      drive_low_[0] = drive_low_[1] = 0.0f;
    }
  }

  void ResetEffectStates() {
    filter_band_[0] = filter_band_[1] = 0.0f;
    filter_low_[0] = filter_low_[1] = 0.0f;
    drive_low_[0] = drive_low_[1] = 0.0f;
  }

  float ProcessFilter(float input, uint8_t channel) {
    const float v1 = filter_a1_ * (filter_band_[channel] +
        filter_g_ * (input - filter_low_[channel]));
    const float v2 = filter_low_[channel] + filter_g_ * v1;
    filter_band_[channel] = 2.0f * v1 - filter_band_[channel];
    filter_low_[channel] = 2.0f * v2 - filter_low_[channel];
    return v2 * (1.0f + 0.20f * effect_parameter_b_[EFFECT_FILTER]);
  }

  float ProcessDrive(float input, uint8_t channel) {
    drive_low_[channel] += drive_tone_coefficient_ *
        (input - drive_low_[channel]);
    const float colored = drive_low_[channel] +
        (input - drive_low_[channel]) * drive_color_;
    float driven = colored * drive_gain_;
    if (driven >= 0.0f) {
      driven *= 1.0f + 0.18f * drive_mix_;
    } else {
      driven *= 1.0f - 0.08f * drive_mix_;
    }
    const float shaped = driven / (1.0f + fabsf(driven));
    return input + drive_mix_ * (shaped * 1.30f - input);
  }

  float ProcessEffect(uint8_t mode, float input, uint8_t channel) {
    if (mode == EFFECT_FILTER) return ProcessFilter(input, channel);
    if (mode == EFFECT_DRIVE) return ProcessDrive(input, channel);
    return input;
  }

  void ApplyEffects(float* left, float* right) {
    if (previous_effect_mode_ == effect_mode_) {
      *left = ProcessEffect(effect_mode_, *left, 0);
      *right = ProcessEffect(effect_mode_, *right, 1);
      return;
    }

    const float previous_l = ProcessEffect(previous_effect_mode_, *left, 0);
    const float previous_r = ProcessEffect(previous_effect_mode_, *right, 1);
    const float current_l = ProcessEffect(effect_mode_, *left, 0);
    const float current_r = ProcessEffect(effect_mode_, *right, 1);
    *left = previous_l + (current_l - previous_l) * effect_crossfade_;
    *right = previous_r + (current_r - previous_r) * effect_crossfade_;
    effect_crossfade_ += 1.0f / 1024.0f;
    if (effect_crossfade_ >= 1.0f) {
      effect_crossfade_ = 1.0f;
      previous_effect_mode_ = effect_mode_;
    }
  }

  void HandleEncoderSwitch(bool raw_switch) {
    if (raw_switch == encoder_switch_candidate_) {
      if (encoder_switch_debounce_count_ < 8) ++encoder_switch_debounce_count_;
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
      if (encoder_hold_blocks_ < 65535) ++encoder_hold_blocks_;
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
    if (apply_mutation) degree += mutation_[stage_];
    degree = ClampInt(degree, 0, 5);
    const int32_t note = ClampInt(root + scales[scale_][degree], 24, 96);
    waveguide_.set_pitch(note * 128);
    const float note_frequency =
        440.0f * stmlib::SemitonesToRatio(static_cast<float>(note - 69));
    body_.SetFrequency(note_frequency);
    main_oscillator_.SetFrequency(note_frequency);
    sympathetic_.SetFrequency(
        note_frequency / static_cast<float>(sub_division_));
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

  void UpdateDisplay(int32_t root, const float* stages, float memory,
                     float grain_size, float texture, bool captured) {
    static const char* scale_names[4] = {"MAJ5", "MIN5", "DOR", "HARM"};
    static const char* destination_names[4] = {
      "PIT", "BODY", "GRAIN", "ALL"
    };
    static const char* oscillator_names[3] = {"REED", "RND ", "FOLD"};
    static const char note_names[12][2] = {
      {'C',' '}, {'C','#'}, {'D',' '}, {'D','#'}, {'E',' '}, {'F',' '},
      {'F','#'}, {'G',' '}, {'G','#'}, {'A',' '}, {'A','#'}, {'B',' '}
    };
    ClearLine(line1_); ClearLine(line2_); ClearLine(line3_); ClearLine(line4_);
    Put(line1_, 0, "TIDE PIT");
    Put(line1_, 9, destination_names[wave_destination_]);
    line1_[16] = note_names[root % 12][0];
    line1_[17] = note_names[root % 12][1];
    line1_[18] = '0' + ClampInt(root / 12 - 1, 0, 9);

    Put(line2_, 0, "WAVE 0-0-0-0 STEP 1");
    if (stages != NULL) {
      line2_[5] = '0' + ClampInt(static_cast<int32_t>(stages[0] * 9.99f), 0, 9);
      line2_[7] = '0' + ClampInt(static_cast<int32_t>(stages[1] * 9.99f), 0, 9);
      line2_[9] = '0' + ClampInt(static_cast<int32_t>(stages[2] * 9.99f), 0, 9);
      line2_[11] = '0' + ClampInt(static_cast<int32_t>(stages[3] * 9.99f), 0, 9);
    }
    line2_[18] = '1' + stage_;

    const int32_t mem_percent = ClampInt(
        static_cast<int32_t>(memory * 99.0f), 0, 99);
    if (effect_mode_ == EFFECT_CLEAN) {
      Put(line3_, 0, "M00 S00 D00 CLEAN");
      const int32_t size_percent = ClampInt(
          static_cast<int32_t>(grain_size * 99.0f), 0, 99);
      const int32_t depth_percent = ClampInt(
          static_cast<int32_t>(texture * 99.0f), 0, 99);
      line3_[1] = '0' + mem_percent / 10;
      line3_[2] = '0' + mem_percent % 10;
      line3_[5] = '0' + size_percent / 10;
      line3_[6] = '0' + size_percent % 10;
      line3_[9] = '0' + depth_percent / 10;
      line3_[10] = '0' + depth_percent % 10;
    } else {
      Put(line3_, 0, effect_mode_ == EFFECT_FILTER
          ? "CUT 00 RES 00 FILT" : "TON 00 DRV 00 DRIVE");
      const int32_t parameter_a = ClampInt(static_cast<int32_t>(
          effect_parameter_a_[effect_mode_] * 99.0f), 0, 99);
      const int32_t parameter_b = ClampInt(static_cast<int32_t>(
          effect_parameter_b_[effect_mode_] * 99.0f), 0, 99);
      line3_[4] = '0' + parameter_a / 10;
      line3_[5] = '0' + parameter_a % 10;
      line3_[11] = '0' + parameter_b / 10;
      line3_[12] = '0' + parameter_b % 10;
    }

    Put(line4_, 0, scale_names[scale_]);
    Put(line4_, 6, oscillator_names[oscillator_mode_]);
    Put(line4_, 11, locked_ ? "LOCK" : "EVOLVE");
    if (captured) Put(line4_, 17, "CAP");
    if (!granular_available_) Put(line4_, 11, "NO GRAIN");
  }

  HighResolutionWaveguide waveguide_;
  MainOscillator main_oscillator_;
  SympatheticString sympathetic_;
  DetailedGranularPlayer player_;
  StereoBody body_;
  DiffusionTail reverb_;
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
  bool effect_button_stable_;
  bool effect_button_candidate_;
  uint8_t effect_button_debounce_count_;
  uint16_t effect_hold_blocks_;
  bool effect_long_action_;
  bool encoder_switch_stable_;
  bool encoder_switch_candidate_;
  uint8_t encoder_switch_debounce_count_;
  uint16_t encoder_hold_blocks_;
  bool encoder_long_action_;
  uint8_t scale_;
  uint8_t wave_destination_;
  uint8_t oscillator_mode_;
  uint8_t effect_mode_;
  uint8_t previous_effect_mode_;
  float effect_crossfade_;
  bool effect_mode_changed_;
  float effect_parameter_a_[EFFECT_MODE_COUNT];
  float effect_parameter_b_[EFFECT_MODE_COUNT];
  bool effect_parameter_initialized_[EFFECT_MODE_COUNT];
  bool effect_parameter_pickup_a_[EFFECT_MODE_COUNT];
  bool effect_parameter_pickup_b_[EFFECT_MODE_COUNT];
  float previous_effect_control_a_[EFFECT_MODE_COUNT];
  float previous_effect_control_b_[EFFECT_MODE_COUNT];
  float filter_band_[2];
  float filter_low_[2];
  float filter_g_;
  float filter_k_;
  float filter_a1_;
  float drive_low_[2];
  float drive_tone_coefficient_;
  float drive_color_;
  float drive_gain_;
  float drive_mix_;
  int32_t last_root_;
  bool pitch_dirty_;
  int8_t mutation_[4];
  uint8_t sub_division_;
  uint32_t random_state_;
  float energy_;
  float lpg_state_;
  float oscillator_fade_;
  bool strike_pending_;
  uint8_t display_divider_;

  float waveguide_output_[BUFSIZE];
  float dry_left_[BUFSIZE];
  float dry_right_[BUFSIZE];
  float record_source_[BUFSIZE];
  float grain_output_[BUFSIZE * 2];
  clouds::FloatFrame frames_[BUFSIZE];
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace tidepit

#endif  // TIDEPIT_DSP_H_
