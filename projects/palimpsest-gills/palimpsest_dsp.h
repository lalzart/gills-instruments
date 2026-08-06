// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Uses the Mutable Instruments Braids sine table by Emilie Gillet.

#ifndef PALIMPSEST_DSP_H_
#define PALIMPSEST_DSP_H_

#include <algorithm>
#include <cmath>

#include "braids/resources.h"
#include "stmlib/utils/dsp.h"

namespace palimpsest {

enum VoiceMode {
  VOICE_TRACE,
  VOICE_KNOCK,
  VOICE_SKIN,
  VOICE_SHARD,
  VOICE_MODE_COUNT
};

enum EffectMode {
  EFFECT_CLEAN,
  EFFECT_FILTER,
  EFFECT_DRIVE,
  EFFECT_MODE_COUNT
};

static inline float Clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline int32_t ClampInt(int32_t value, int32_t low, int32_t high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float Q27ToFloat(int32_t value) {
  return Clamp01(static_cast<float>(value) * (1.0f / 134217728.0f));
}

class TraceVoice {
 public:
  void Init() {
    active_ = false;
    wake_ = false;
    level_ = 0.0f;
    attack_ = 0.0f;
    gain_l_ = 0.707f;
    gain_r_ = 0.707f;
    for (size_t i = 0; i < kNumPartials; ++i) {
      phase_[i] = 0;
      increment_[i] = 0;
      amplitude_[i] = 0.0f;
      damping_[i] = 0.0f;
    }
    attack_step_ = 0.021f;
  }

  void Trigger(float frequency,
               float material,
               float decay_seconds,
               float amplitude,
               float pan,
               bool wake,
               uint8_t mode,
               uint32_t seed) {
    static const float harmonic_ratios[kNumPartials] = {1.0f, 2.0f, 3.01f};
    static const float abstract_ratios[kNumPartials] = {1.0f, 2.43f, 4.07f};
    static const float partial_gains[kNumPartials] = {1.0f, 0.46f, 0.24f};

    material = Clamp01(material);
    decay_seconds = std::max(0.035f, decay_seconds);
    amplitude = std::max(0.0f, amplitude);
    pan = Clamp01(pan);

    const uint8_t voice_mode = mode < VOICE_MODE_COUNT
        ? mode : static_cast<uint8_t>(VOICE_TRACE);
    if (voice_mode == VOICE_TRACE) {
      for (size_t i = 0; i < kNumPartials; ++i) {
        const float ratio = harmonic_ratios[i] + material *
            (abstract_ratios[i] - harmonic_ratios[i]);
        const float partial_frequency = std::min(15000.0f, frequency * ratio);
        increment_[i] = static_cast<uint32_t>(
            partial_frequency * (4294967296.0f / 48000.0f));
        phase_[i] = 0;
        amplitude_[i] = amplitude * partial_gains[i] *
            (i == 0 ? 1.0f : 0.55f + 0.75f * material);
        const float partial_decay = decay_seconds /
            (1.0f + static_cast<float>(i) * (0.30f + 0.55f * material));
        const float decay_step = 1.0f / (partial_decay * 48000.0f);
        damping_[i] = 1.0f / (1.0f + decay_step);
      }
      attack_step_ = 0.021f;
    } else {
      static const float ratio_low[3][kNumPartials] = {
        {1.0f, 2.18f, 3.64f},
        {1.0f, 1.57f, 2.09f},
        {1.0f, 2.63f, 4.82f}
      };
      static const float ratio_high[3][kNumPartials] = {
        {1.0f, 2.43f, 4.10f},
        {1.0f, 1.73f, 2.31f},
        {1.0f, 2.91f, 5.37f}
      };
      static const float gains[3][kNumPartials] = {
        {1.0f, 0.34f, 0.16f},
        {1.0f, 0.42f, 0.20f},
        {1.0f, 0.58f, 0.31f}
      };
      static const float decay_scale[3][kNumPartials] = {
        {0.72f, 0.35f, 0.18f},
        {1.25f, 0.65f, 0.42f},
        {0.75f, 0.46f, 0.25f}
      };
      const uint8_t recipe = voice_mode - 1;
      for (size_t i = 0; i < kNumPartials; ++i) {
        const float ratio = ratio_low[recipe][i] + material *
            (ratio_high[recipe][i] - ratio_low[recipe][i]);
        const float partial_frequency = std::min(15000.0f, frequency * ratio);
        increment_[i] = static_cast<uint32_t>(
            partial_frequency * (4294967296.0f / 48000.0f));
        if (voice_mode == VOICE_SHARD) {
          phase_[i] = seed + static_cast<uint32_t>(i) * 0x9e3779b9u;
        } else if (voice_mode == VOICE_KNOCK && i != 0) {
          phase_[i] = seed ^ (static_cast<uint32_t>(i) * 0x6d2b79f5u);
        } else {
          phase_[i] = 0;
        }
        amplitude_[i] = amplitude * gains[recipe][i] *
            (i == 0 ? 1.0f : 0.72f + 0.48f * material);
        const float partial_decay = std::max(
            0.025f, decay_seconds * decay_scale[recipe][i]);
        const float decay_step = 1.0f / (partial_decay * 48000.0f);
        damping_[i] = 1.0f / (1.0f + decay_step);
      }
      attack_step_ = voice_mode == VOICE_KNOCK
          ? 0.14f : (voice_mode == VOICE_SKIN ? 0.06f : 0.10f);
    }

    gain_l_ = cosf(pan * 1.57079632679f);
    gain_r_ = sinf(pan * 1.57079632679f);
    level_ = amplitude;
    attack_ = 0.0f;
    wake_ = wake;
    active_ = true;
  }

  void Process(float* direct_l,
               float* direct_r,
               float* wake_l,
               float* wake_r) {
    if (!active_) {
      return;
    }

    attack_ = std::min(1.0f, attack_ + attack_step_);
    float sample = 0.0f;
    for (size_t i = 0; i < kNumPartials; ++i) {
      phase_[i] += increment_[i];
      sample += stmlib::Interpolate824(braids::wav_sine, phase_[i]) *
          (1.0f / 32768.0f) * amplitude_[i];
      amplitude_[i] *= damping_[i];
    }
    sample *= attack_;
    level_ = amplitude_[0];

    if (wake_) {
      *wake_l += sample * gain_l_;
      *wake_r += sample * gain_r_;
    } else {
      *direct_l += sample * gain_l_;
      *direct_r += sample * gain_r_;
    }

    if (level_ < 0.000015f) {
      active_ = false;
    }
  }

  bool active() const { return active_; }
  float level() const { return level_; }
  void Clear() { active_ = false; level_ = 0.0f; }

 private:
  static const size_t kNumPartials = 3;
  uint32_t phase_[kNumPartials];
  uint32_t increment_[kNumPartials];
  float amplitude_[kNumPartials];
  float damping_[kNumPartials];
  float gain_l_;
  float gain_r_;
  float level_;
  float attack_;
  float attack_step_;
  bool wake_;
  bool active_;
};

struct WakeEvent {
  bool active;
  int32_t samples_until;
  float frequency;
  float material;
  float decay;
  float amplitude;
  float pan;
  uint8_t voice_mode;
};

class Instrument {
 public:
  void Init() {
    for (size_t i = 0; i < kNumVoices; ++i) {
      voices_[i].Init();
    }
    for (size_t i = 0; i < kNumEvents; ++i) {
      events_[i].active = false;
    }

    phase_ = 0;
    stage_ = 0;
    locked_ = false;
    voice_button_stable_ = false;
    voice_button_candidate_ = false;
    voice_button_debounce_count_ = 0;
    previous_mutate_ = false;
    previous_lock_ = false;
    previous_effect_ = false;
    effect_hold_blocks_ = 0;
    effect_long_action_ = false;
    encoder_switch_stable_ = false;
    encoder_switch_candidate_ = false;
    encoder_switch_debounce_count_ = 0;
    scale_ = 0;
    random_state_ = 0x70616c69;  // "pali"
    for (size_t i = 0; i < 4; ++i) {
      mutation_[i] = 0;
    }
    voice_mode_ = VOICE_TRACE;
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
    effect_parameter_a_[EFFECT_CLEAN] = 0.55f;
    effect_parameter_b_[EFFECT_CLEAN] = 0.50f;
    effect_parameter_a_[EFFECT_FILTER] = 0.65f;
    effect_parameter_b_[EFFECT_FILTER] = 0.25f;
    effect_parameter_a_[EFFECT_DRIVE] = 0.50f;
    effect_parameter_b_[EFFECT_DRIVE] = 0.25f;
    effect_parameter_initialized_[EFFECT_CLEAN] = true;
    effect_parameter_pickup_a_[EFFECT_CLEAN] = true;
    effect_parameter_pickup_b_[EFFECT_CLEAN] = true;
    ResetEffectStates();
    UpdateEffectCoefficients();
    strike_pending_ = true;
    display_divider_ = 0;
    UpdateDisplay(60, NULL, 0.75f, 0.35f, 0.55f);
  }

  void Process(const int32_t stage_controls[4],
               int32_t rate_control,
               int32_t memory_control,
               int32_t material_control,
               int32_t spacing_control,
               int32_t decay_control,
               int32_t wake_control,
               bool voice_button,
               bool mutate_button,
               bool lock_button,
               bool effect_button,
               int32_t encoder,
               bool encoder_switch,
               int32_t* left,
               int32_t* right) {
    HandleControls(voice_button, mutate_button, lock_button, effect_button,
                   encoder_switch);

    const int32_t root = ClampInt(encoder, 36, 72);
    float stages[4];
    for (size_t i = 0; i < 4; ++i) {
      stages[i] = Q27ToFloat(stage_controls[i]);
    }
    const float rate = Q27ToFloat(rate_control);
    const float memory = Q27ToFloat(memory_control);
    const float material = Q27ToFloat(material_control);
    const float spacing = Q27ToFloat(spacing_control);
    const float raw_decay = Q27ToFloat(decay_control);
    const float raw_wake = Q27ToFloat(wake_control);
    float decay;
    float wake;
    UpdateEffectParameters(raw_decay, raw_wake, &decay, &wake);

    const float rate_hz = 0.05f + 2.45f * rate * rate;
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

    if (cycle_wrapped && !locked_ && RandomUnit() > memory * memory) {
      Mutate();
    }
    if (stage_changed || strike_pending_) {
      TriggerStage(root, stages[stage_], material, spacing, decay, wake);
      strike_pending_ = false;
    }

    const uint8_t next_stage = (stage_ + 1) & 3;
    const float t = static_cast<float>(phase_ & 0x3fffffff) *
        (1.0f / 1073741824.0f);
    const float smooth_t = t * t * (3.0f - 2.0f * t);
    const float gesture = Clamp01(
        stages[stage_] + (stages[next_stage] - stages[stage_]) * smooth_t);
    const float motion_gain = 0.74f + 0.26f * gesture;
    const float direct_mix = 0.78f - 0.20f * wake;
    const float wake_mix = wake * (0.56f + 0.30f * wake);

    for (size_t sample_index = 0; sample_index < BUFSIZE; ++sample_index) {
      ProcessEvents();

      float direct_l = 0.0f;
      float direct_r = 0.0f;
      float wake_l = 0.0f;
      float wake_r = 0.0f;
      for (size_t voice_index = 0; voice_index < kNumVoices; ++voice_index) {
        voices_[voice_index].Process(
            &direct_l, &direct_r, &wake_l, &wake_r);
      }

      float l = (direct_l * direct_mix + wake_l * wake_mix) * motion_gain;
      float r = (direct_r * direct_mix + wake_r * wake_mix) * motion_gain;
      ApplyEffects(&l, &r);
      l = l / (1.0f + fabsf(l));
      r = r / (1.0f + fabsf(r));
      left[sample_index] = static_cast<int32_t>(l * 102000000.0f);
      right[sample_index] = static_cast<int32_t>(r * 102000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay(root, stages, memory, spacing, decay);
    }
  }

  uint8_t stage() const { return stage_; }
  bool locked() const { return locked_; }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static const size_t kNumVoices = 16;
  static const size_t kNumEvents = 16;

  void HandleControls(bool voice,
                      bool mutate,
                      bool lock,
                      bool effect,
                      bool encoder_switch) {
    HandleVoiceButton(voice);
    if (mutate && !previous_mutate_) {
      Mutate();
      strike_pending_ = true;
    }
    if (lock && !previous_lock_) {
      locked_ = !locked_;
    }
    if (effect) {
      if (!previous_effect_) {
        effect_hold_blocks_ = 0;
        effect_long_action_ = false;
      } else if (!effect_long_action_) {
        if (effect_hold_blocks_ < 65535) ++effect_hold_blocks_;
        if (effect_hold_blocks_ >= 1500) {
          ClearAll();
          ResetEffectStates();
          effect_long_action_ = true;
        }
      }
    } else if (previous_effect_) {
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
    previous_mutate_ = mutate;
    previous_lock_ = lock;
    previous_effect_ = effect;
    HandleEncoderSwitch(encoder_switch);
  }

  void HandleVoiceButton(bool raw_button) {
    if (raw_button == voice_button_candidate_) {
      if (voice_button_debounce_count_ < 75) {
        ++voice_button_debounce_count_;
      }
    } else {
      voice_button_candidate_ = raw_button;
      voice_button_debounce_count_ = 0;
    }
    if (voice_button_debounce_count_ >= 75 &&
        voice_button_stable_ != voice_button_candidate_) {
      voice_button_stable_ = voice_button_candidate_;
      voice_button_debounce_count_ = 0;
      if (voice_button_stable_) {
        voice_mode_ = (voice_mode_ + 1) % VOICE_MODE_COUNT;
        strike_pending_ = true;
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
                              float* decay,
                              float* wake) {
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

    *decay = effect_parameter_a_[EFFECT_CLEAN];
    *wake = effect_parameter_b_[EFFECT_CLEAN];
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
        scale_ = (scale_ + 1) & 3;
        strike_pending_ = true;
      }
    }
  }

  void TriggerStage(int32_t root,
                    float height,
                    float material,
                    float spacing,
                    float decay,
                    float wake) {
    static const int8_t scales[4][6] = {
      {0, 2, 4, 7, 9, 12},
      {0, 2, 3, 5, 7, 10},
      {0, 2, 3, 5, 7, 9},
      {0, 3, 5, 7, 10, 12}
    };
    static const int8_t wake_intervals[4][3] = {
      {0, 7, 12},
      {0, 7, 10},
      {0, 5, 9},
      {0, 3, 7}
    };
    static const float stage_pan[4] = {0.16f, 0.68f, 0.34f, 0.84f};

    int32_t degree = static_cast<int32_t>(height * 5.99f);
    degree = ClampInt(degree + mutation_[stage_], 0, 5);
    const int32_t note = ClampInt(root + scales[scale_][degree], 24, 96);
    const float frequency = 440.0f * powf(2.0f, (note - 69) / 12.0f);

    const float direct_decay = 0.10f + 0.32f * (1.0f - material);
    TriggerVoice(frequency, material, direct_decay, 0.48f,
                 stage_pan[stage_], false, voice_mode_);

    if (wake <= 0.01f) {
      return;
    }
    const int32_t spacing_samples = static_cast<int32_t>(
        (0.025f + 0.55f * spacing * spacing) * 48000.0f);
    const float wake_decay = 0.30f + 5.20f * decay * decay;
    for (size_t i = 0; i < 3; ++i) {
      const float echo_frequency = frequency * powf(
          2.0f, wake_intervals[scale_][i] * (1.0f / 12.0f));
      float pan = stage_pan[(stage_ + i + 1) & 3];
      pan = Clamp01(pan + (RandomUnit() - 0.5f) * 0.10f);
      ScheduleEvent(spacing_samples * static_cast<int32_t>(i + 1),
                    echo_frequency,
                    material,
                    wake_decay * (1.0f - 0.13f * i),
                    (0.28f - 0.055f * i) * (0.55f + 0.45f * wake),
                    pan,
                    voice_mode_);
    }
  }

  void ScheduleEvent(int32_t delay,
                     float frequency,
                     float material,
                     float decay,
                     float amplitude,
                     float pan,
                     uint8_t voice_mode) {
    size_t selected = 0;
    int32_t longest_delay = -1;
    for (size_t i = 0; i < kNumEvents; ++i) {
      if (!events_[i].active) {
        selected = i;
        longest_delay = 0x7fffffff;
        break;
      }
      if (events_[i].samples_until > longest_delay) {
        longest_delay = events_[i].samples_until;
        selected = i;
      }
    }
    WakeEvent* event = &events_[selected];
    event->active = true;
    event->samples_until = delay;
    event->frequency = frequency;
    event->material = material;
    event->decay = decay;
    event->amplitude = amplitude;
    event->pan = pan;
    event->voice_mode = voice_mode;
  }

  void ProcessEvents() {
    for (size_t i = 0; i < kNumEvents; ++i) {
      WakeEvent* event = &events_[i];
      if (!event->active) {
        continue;
      }
      --event->samples_until;
      if (event->samples_until <= 0) {
        TriggerVoice(event->frequency,
                     event->material,
                     event->decay,
                     event->amplitude,
                     event->pan,
                     true,
                     event->voice_mode);
        event->active = false;
      }
    }
  }

  void TriggerVoice(float frequency,
                    float material,
                    float decay,
                    float amplitude,
                    float pan,
                    bool wake,
                    uint8_t voice_mode) {
    size_t selected = 0;
    float quietest = 1.0e9f;
    for (size_t i = 0; i < kNumVoices; ++i) {
      if (!voices_[i].active()) {
        selected = i;
        quietest = -1.0f;
        break;
      }
      if (voices_[i].level() < quietest) {
        quietest = voices_[i].level();
        selected = i;
      }
    }
    const uint32_t seed = voice_mode == VOICE_TRACE ? 1 : RandomWord();
    voices_[selected].Trigger(
        frequency, material, decay, amplitude, pan, wake, voice_mode, seed);
  }

  void ClearAll() {
    for (size_t i = 0; i < kNumVoices; ++i) {
      voices_[i].Clear();
    }
    for (size_t i = 0; i < kNumEvents; ++i) {
      events_[i].active = false;
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
  }

  static void ClearLine(char* line) {
    for (size_t i = 0; i < 21; ++i) {
      line[i] = ' ';
    }
    line[21] = '\0';
  }

  static void Put(char* line, size_t offset, const char* text) {
    while (*text && offset < 21) {
      line[offset++] = *text++;
    }
  }

  void UpdateDisplay(int32_t root,
                     const float* stages,
                     float memory,
                     float spacing,
                     float decay) {
    static const char* scale_names[4] = {"MAJ5", "MIN5", "DOR", "HARM"};
    static const char* voice_names[VOICE_MODE_COUNT] = {
      "TRACE", "KNOCK", "SKIN ", "SHARD"
    };
    static const char* effect_names[EFFECT_MODE_COUNT] = {
      "CLEAN", "FILT ", "DRIVE"
    };
    static const char note_names[12][2] = {
      {'C',' '}, {'C','#'}, {'D',' '}, {'D','#'}, {'E',' '}, {'F',' '},
      {'F','#'}, {'G',' '}, {'G','#'}, {'A',' '}, {'A','#'}, {'B',' '}
    };

    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);

    Put(line1_, 0, "PALIMPSEST");
    Put(line1_, 11, voice_names[voice_mode_]);
    line1_[17] = note_names[root % 12][0];
    line1_[18] = note_names[root % 12][1];
    line1_[19] = '0' + ClampInt(root / 12 - 1, 0, 9);

    Put(line2_, 0, "FORM 0-0-0-0 STEP 1");
    if (stages != NULL) {
      line2_[5] = '0' + ClampInt(static_cast<int32_t>(stages[0] * 9.99f), 0, 9);
      line2_[7] = '0' + ClampInt(static_cast<int32_t>(stages[1] * 9.99f), 0, 9);
      line2_[9] = '0' + ClampInt(static_cast<int32_t>(stages[2] * 9.99f), 0, 9);
      line2_[11] = '0' + ClampInt(static_cast<int32_t>(stages[3] * 9.99f), 0, 9);
    }
    line2_[18] = '1' + stage_;

    const int32_t memory_percent = ClampInt(
        static_cast<int32_t>(memory * 99.0f), 0, 99);
    const int32_t spacing_percent = ClampInt(
        static_cast<int32_t>(spacing * 99.0f), 0, 99);
    const int32_t decay_percent = ClampInt(
        static_cast<int32_t>(decay * 99.0f), 0, 99);
    if (effect_mode_ == EFFECT_CLEAN) {
      Put(line3_, 0, "M00 S00 D00");
      line3_[1] = '0' + memory_percent / 10;
      line3_[2] = '0' + memory_percent % 10;
      line3_[5] = '0' + spacing_percent / 10;
      line3_[6] = '0' + spacing_percent % 10;
      line3_[9] = '0' + decay_percent / 10;
      line3_[10] = '0' + decay_percent % 10;
    } else {
      Put(line3_, 0, effect_mode_ == EFFECT_FILTER
          ? "CUT 00 RES 00" : "TON 00 DRV 00");
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
    Put(line4_, 6, effect_names[effect_mode_]);
    Put(line4_, 12, locked_ ? "LOCK" : "EVOLVE");
  }

  TraceVoice voices_[kNumVoices];
  WakeEvent events_[kNumEvents];

  uint32_t phase_;
  uint8_t stage_;
  bool locked_;
  bool voice_button_stable_;
  bool voice_button_candidate_;
  uint8_t voice_button_debounce_count_;
  bool previous_mutate_;
  bool previous_lock_;
  bool previous_effect_;
  uint16_t effect_hold_blocks_;
  bool effect_long_action_;
  bool encoder_switch_stable_;
  bool encoder_switch_candidate_;
  uint8_t encoder_switch_debounce_count_;
  uint8_t scale_;
  uint8_t voice_mode_;
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
  uint32_t random_state_;
  int8_t mutation_[4];
  bool strike_pending_;
  uint8_t display_divider_;

  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace palimpsest

#endif  // PALIMPSEST_DSP_H_
