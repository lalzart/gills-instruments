// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.

#ifndef SPECTRUMTRIBE_DSP_H_
#define SPECTRUMTRIBE_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stmlib/dsp/units.h"

namespace spectrumtribe {

static const size_t kNumVoices = 3;
static const size_t kNumBands = 6;
static const size_t kNumTunings = 4;
static const size_t kNumTuneSteps = 12;

static const float kTuningIntervals[kNumTunings][kNumTuneSteps] = {
  {
    0.0f, 2.0f, 4.0f, 5.0f, 7.0f, 9.0f,
    11.0f, 12.0f, 14.0f, 16.0f, 19.0f, 24.0f
  },
  {
    0.0f, 3.0f, 5.0f, 7.0f, 10.0f, 12.0f,
    15.0f, 17.0f, 19.0f, 22.0f, 24.0f, 27.0f
  },
  {
    0.0f, 2.03910f, 3.86314f, 4.98045f, 7.01955f, 8.84359f,
    10.88269f, 12.0f, 14.03910f, 15.86314f, 19.01955f, 24.0f
  },
  {
    0.0f, 1.46304f, 2.92608f, 4.38913f, 5.85217f, 7.31521f,
    8.77825f, 10.24129f, 11.70434f, 13.16738f, 14.63042f, 16.09346f
  }
};

static inline float Clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float Clamp01(float value) {
  return Clamp(value, 0.0f, 1.0f);
}

static inline int32_t ClampInt(int32_t value, int32_t low, int32_t high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float Q27ToFloat(int32_t value) {
  return Clamp01(static_cast<float>(value) * (1.0f / 134217728.0f));
}

static inline float Wrap01(float value) {
  while (value < 0.0f) value += 1.0f;
  while (value >= 1.0f) value -= 1.0f;
  return value;
}

static inline int32_t Wrap12(int32_t value) {
  while (value < 0) value += 12;
  while (value >= 12) value -= 12;
  return value;
}

class DebouncedButton {
 public:
  void Init() {
    stable_ = false;
    candidate_ = false;
    count_ = 0;
  }

  bool Update(bool raw) {
    if (raw == candidate_) {
      if (count_ < kDebounceBlocks) ++count_;
    } else {
      candidate_ = raw;
      count_ = 0;
    }
    if (count_ >= kDebounceBlocks && stable_ != candidate_) {
      stable_ = candidate_;
      count_ = 0;
      return stable_;
    }
    return false;
  }

 private:
  static const uint8_t kDebounceBlocks = 32;
  bool stable_;
  bool candidate_;
  uint8_t count_;
};

class ResonantBandpass {
 public:
  void Init() {
    target_b0_ = 0.0f;
    target_b2_ = 0.0f;
    target_a1_ = 0.0f;
    target_a2_ = 0.0f;
    b0_ = 0.0f;
    b2_ = 0.0f;
    a1_ = 0.0f;
    a2_ = 0.0f;
    z1_ = 0.0f;
    z2_ = 0.0f;
  }

  void SetTarget(float frequency, float q) {
    frequency = Clamp(frequency, 24.0f, 15000.0f);
    q = Clamp(q, 0.65f, 120.0f);
    const float omega = 6.28318530718f * frequency * (1.0f / 48000.0f);
    const float sine = sinf(omega);
    const float cosine = cosf(omega);
    const float alpha = sine / (2.0f * q);
    const float inverse_a0 = 1.0f / (1.0f + alpha);
    target_b0_ = alpha * inverse_a0;
    target_b2_ = -target_b0_;
    target_a1_ = -2.0f * cosine * inverse_a0;
    target_a2_ = (1.0f - alpha) * inverse_a0;
  }

  void SnapToTarget() {
    b0_ = target_b0_;
    b2_ = target_b2_;
    a1_ = target_a1_;
    a2_ = target_a2_;
  }

  float Process(float input, float slew) {
    b0_ += slew * (target_b0_ - b0_);
    b2_ += slew * (target_b2_ - b2_);
    a1_ += slew * (target_a1_ - a1_);
    a2_ += slew * (target_a2_ - a2_);
    const float output = b0_ * input + z1_;
    z1_ = -a1_ * output + z2_;
    z2_ = b2_ * input - a2_ * output;
    return output;
  }

 private:
  float target_b0_;
  float target_b2_;
  float target_a1_;
  float target_a2_;
  float b0_;
  float b2_;
  float a1_;
  float a2_;
  float z1_;
  float z2_;
};

// Four short mutually coupled delay lines make a modest stereo bloom. The
// matrix is energy-normalized and the feedback stays below unity.
class SpectralBloom {
 public:
  void Init() {
    memset(delay0_, 0, sizeof(delay0_));
    memset(delay1_, 0, sizeof(delay1_));
    memset(delay2_, 0, sizeof(delay2_));
    memset(delay3_, 0, sizeof(delay3_));
    index0_ = index1_ = index2_ = index3_ = 0;
    damp0_ = damp1_ = damp2_ = damp3_ = 0.0f;
  }

  void Process(float input_left,
               float input_right,
               float send,
               float feedback,
               float damping,
               float wet,
               float* output_left,
               float* output_right) {
    const float tap0 = delay0_[index0_];
    const float tap1 = delay1_[index1_];
    const float tap2 = delay2_[index2_];
    const float tap3 = delay3_[index3_];

    damp0_ += damping * (tap0 - damp0_);
    damp1_ += damping * (tap1 - damp1_);
    damp2_ += damping * (tap2 - damp2_);
    damp3_ += damping * (tap3 - damp3_);

    const float h0 = 0.5f * (damp0_ + damp1_ + damp2_ + damp3_);
    const float h1 = 0.5f * (damp0_ - damp1_ + damp2_ - damp3_);
    const float h2 = 0.5f * (damp0_ + damp1_ - damp2_ - damp3_);
    const float h3 = 0.5f * (damp0_ - damp1_ - damp2_ + damp3_);
    const float mono = 0.5f * (input_left + input_right) * send;
    const float side = 0.25f * (input_left - input_right) * send;

    delay0_[index0_] = Clamp(mono + side + feedback * h0, -2.0f, 2.0f);
    delay1_[index1_] = Clamp(mono - side + feedback * h1, -2.0f, 2.0f);
    delay2_[index2_] = Clamp(mono + feedback * h2, -2.0f, 2.0f);
    delay3_[index3_] = Clamp(-mono + feedback * h3, -2.0f, 2.0f);

    if (++index0_ >= kDelay0) index0_ = 0;
    if (++index1_ >= kDelay1) index1_ = 0;
    if (++index2_ >= kDelay2) index2_ = 0;
    if (++index3_ >= kDelay3) index3_ = 0;

    *output_left = input_left + wet * (0.70f * tap0 + 0.45f * tap2);
    *output_right = input_right + wet * (0.70f * tap1 + 0.45f * tap3);
  }

 private:
  static const size_t kDelay0 = 1193;
  static const size_t kDelay1 = 1427;
  static const size_t kDelay2 = 1699;
  static const size_t kDelay3 = 1999;
  float delay0_[kDelay0];
  float delay1_[kDelay1];
  float delay2_[kDelay2];
  float delay3_[kDelay3];
  size_t index0_;
  size_t index1_;
  size_t index2_;
  size_t index3_;
  float damp0_;
  float damp1_;
  float damp2_;
  float damp3_;
};

class Instrument {
 public:
  enum Parameter {
    PARAM_LOW_TUNE,
    PARAM_MID_TUNE,
    PARAM_HIGH_TUNE,
    PARAM_DECAY,
    PARAM_MATERIAL,
    PARAM_LOW_RHYTHM,
    PARAM_MID_RHYTHM,
    PARAM_HIGH_RHYTHM,
    PARAM_PHASE,
    PARAM_SPACE,
    PARAM_LAST
  };

  enum Groove {
    GROOVE_EVEN,
    GROOVE_SWAY,
    GROOVE_CLAVE,
    GROOVE_DRIFT,
    GROOVE_LAST
  };

  void Init() {
    groove_button_.Init();
    mutate_button_.Init();
    hold_button_.Init();
    tuning_button_.Init();
    fill_button_.Init();
    bloom_.Init();
    for (size_t band = 0; band < kNumBands; ++band) {
      resonator_[band].Init();
      band_memory_[band] = 0.0f;
    }
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      tune_step_[voice] = 0;
      hits_per_cycle_[voice] = static_cast<uint8_t>(3 + voice);
      event_bucket_[voice] = -1;
      event_index_[voice] = 0;
      rhythm_hits_[voice] = 0;
      voice_events_[voice] = 0;
      voice_envelope_[voice] = 0.0f;
      noise_state_[voice] = 0.0f;
      phase_mutation_[voice] = 0.0f;
      overtone_rotation_[voice] = 0;
      flam_countdown_[voice] = 0;
      led_countdown_[voice] = 0;
    }
    groove_ = GROOVE_EVEN;
    tuning_ = 0;
    hold_ = false;
    controls_initialized_ = false;
    targets_initialized_ = false;
    tuning_dirty_ = true;
    tempo_ = 96;
    decay_ = 0.62f;
    material_ = 0.36f;
    decay_step_ = 79;
    material_step_ = 46;
    phase_ = 0.0f;
    space_ = 0.22f;
    cycle_phase_ = 0.0f;
    cycle_count_ = 0;
    collision_count_ = 0;
    collision_envelope_ = 0.0f;
    random_state_ = 0x8c032fc1u;
    display_divider_ = 0;
    UpdateTuning();
    UpdateDisplay();
  }

  void Process(const int32_t controls[PARAM_LAST],
               bool groove_button,
               bool mutate_button,
               bool hold_button,
               bool tuning_button,
               int32_t tempo,
               bool fill_button,
               int32_t* output_left,
               int32_t* output_right) {
    UpdateParameters(controls, tempo);
    HandleButtons(groove_button, mutate_button, hold_button,
                  tuning_button, fill_button);
    AdvanceClock();
    if (tuning_dirty_) UpdateTuning();

    const float decay_squared = decay_ * decay_;
    const float material_squared = material_ * material_;
    const float drive_gain = 11.0f + 21.0f * decay_squared;
    const float overtone_level = 0.20f + 0.68f * material_;
    const float coupling = 0.015f + 0.105f * material_squared;
    const float bloom_send = 0.30f * space_;
    const float bloom_feedback = 0.24f + 0.55f * space_;
    const float bloom_damping = 0.16f + 0.36f * (1.0f - space_);
    const float bloom_wet = 0.48f * space_;
    const float envelope_decay[kNumVoices] = {
      0.9915f + 0.0068f * decay_squared,
      0.9870f + 0.0100f * decay_squared,
      0.9760f + 0.0180f * decay_squared
    };
    const float noise_color[kNumVoices] = {
      0.055f + 0.48f * material_,
      0.120f + 0.58f * material_,
      0.240f + 0.68f * material_
    };
    static const float pan_left[kNumVoices] = {0.78f, 1.00f, 0.38f};
    static const float pan_right[kNumVoices] = {0.78f, 0.38f, 1.00f};

    for (size_t sample = 0; sample < BUFSIZE; ++sample) {
      float dry_left = 0.0f;
      float dry_right = 0.0f;
      float current_band_output[kNumBands];
      const float voice_noise[kNumVoices] = {
        RandomBipolar(), RandomBipolar(), RandomBipolar()
      };

      for (size_t voice = 0; voice < kNumVoices; ++voice) {
        const size_t body_band = voice * 2;
        const size_t overtone_band = body_band + 1;
        noise_state_[voice] += noise_color[voice] *
            (voice_noise[voice] - noise_state_[voice]);
        const float bright = voice_noise[voice] - noise_state_[voice];
        const float exciter = (noise_state_[voice] * (1.0f - material_) +
            voice_noise[voice] * material_) * voice_envelope_[voice];
        const size_t next_body = ((voice + 1) % kNumVoices) * 2;
        const size_t previous_body = ((voice + 2) % kNumVoices) * 2;
        const float sympathetic = Clamp(
            (band_memory_[next_body] - 0.62f * band_memory_[previous_body]) *
            coupling, -0.42f, 0.42f);
        const float bloom_excitation = bright * collision_envelope_ *
            (0.22f + 0.24f * static_cast<float>(voice));
        const float body_drive = exciter * drive_gain + sympathetic;
        const float overtone_drive =
            (exciter * (0.24f + 0.64f * material_) + bloom_excitation) *
            drive_gain + 0.55f * sympathetic;

        const float body = resonator_[body_band].Process(body_drive, 0.0045f);
        const float overtone = resonator_[overtone_band].Process(
            overtone_drive, 0.0045f);
        current_band_output[body_band] = body;
        current_band_output[overtone_band] = overtone;
        const float voice_output = body + overtone_level * overtone;
        dry_left += voice_output * pan_left[voice];
        dry_right += voice_output * pan_right[voice];

        voice_envelope_[voice] *= envelope_decay[voice];
        if (voice_envelope_[voice] < 0.00001f) {
          voice_envelope_[voice] = 0.0f;
        }
      }

      for (size_t band = 0; band < kNumBands; ++band) {
        band_memory_[band] = Clamp(current_band_output[band], -2.0f, 2.0f);
      }
      collision_envelope_ *= 0.9950f;
      if (collision_envelope_ < 0.00001f) collision_envelope_ = 0.0f;

      float wet_left;
      float wet_right;
      bloom_.Process(dry_left, dry_right, bloom_send, bloom_feedback,
                     bloom_damping, bloom_wet, &wet_left, &wet_right);
      output_left[sample] = static_cast<int32_t>(
          SoftLimit(wet_left) * 120000000.0f);
      output_right[sample] = static_cast<int32_t>(
          SoftLimit(wet_right) * 120000000.0f);
    }

    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      if (led_countdown_[voice] > 0) --led_countdown_[voice];
    }
    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay();
    }
  }

  bool voice_led(size_t voice) const {
    return voice < kNumVoices && led_countdown_[voice] > 0;
  }
  bool hold() const { return hold_; }
  uint8_t groove() const { return groove_; }
  uint8_t tuning() const { return tuning_; }
  uint8_t hits_per_cycle(size_t voice) const {
    return voice < kNumVoices ? hits_per_cycle_[voice] : 0;
  }
  uint32_t rhythm_hits(size_t voice) const {
    return voice < kNumVoices ? rhythm_hits_[voice] : 0;
  }
  uint32_t voice_events(size_t voice) const {
    return voice < kNumVoices ? voice_events_[voice] : 0;
  }
  uint32_t cycle_count() const { return cycle_count_; }
  uint32_t collision_count() const { return collision_count_; }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static float SoftLimit(float input) {
    float value = Clamp(input * 4.6f, -1.0f, 1.0f);
    value = value - value * value * value * (1.0f / 3.0f);
    return value * 1.5f;
  }

  uint32_t NextRandom() {
    uint32_t value = random_state_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state_ = value;
    return value;
  }

  float RandomBipolar() {
    return static_cast<float>(static_cast<int32_t>(NextRandom())) *
        (1.0f / 2147483648.0f);
  }

  void UpdateParameters(const int32_t controls[PARAM_LAST], int32_t tempo) {
    bool next_tuning_dirty = false;
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      const int32_t next_step = ClampInt(static_cast<int32_t>(
          Q27ToFloat(controls[voice]) * 11.999f), 0, 11);
      if (next_step != tune_step_[voice]) {
        tune_step_[voice] = next_step;
        next_tuning_dirty = true;
      }
    }

    const float decay_target = Q27ToFloat(controls[PARAM_DECAY]);
    const float material_target = Q27ToFloat(controls[PARAM_MATERIAL]);
    const float phase_target = Q27ToFloat(controls[PARAM_PHASE]);
    const float space_target = Q27ToFloat(controls[PARAM_SPACE]);
    if (!controls_initialized_) {
      decay_ = decay_target;
      material_ = material_target;
      phase_ = phase_target;
      space_ = space_target;
    } else {
      decay_ += 0.10f * (decay_target - decay_);
      material_ += 0.10f * (material_target - material_);
      phase_ += 0.10f * (phase_target - phase_);
      space_ += 0.06f * (space_target - space_);
    }
    controls_initialized_ = true;

    const int32_t material_quantized = ClampInt(static_cast<int32_t>(
        material_target * 127.0f + 0.5f), 0, 127);
    const int32_t decay_quantized = ClampInt(static_cast<int32_t>(
        decay_target * 127.0f + 0.5f), 0, 127);
    if (material_quantized != material_step_ ||
        decay_quantized != decay_step_) {
      material_step_ = material_quantized;
      decay_step_ = decay_quantized;
      next_tuning_dirty = true;
    }

    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      const size_t parameter = PARAM_LOW_RHYTHM + voice;
      const uint8_t next_hits = static_cast<uint8_t>(1 + ClampInt(
          static_cast<int32_t>(Q27ToFloat(controls[parameter]) * 8.999f),
          0, 8));
      if (next_hits != hits_per_cycle_[voice]) {
        hits_per_cycle_[voice] = next_hits;
        event_bucket_[voice] = -1;
      }
    }
    tempo_ = ClampInt(tempo, 40, 180);
    if (next_tuning_dirty) tuning_dirty_ = true;
  }

  void HandleButtons(bool groove,
                     bool mutate,
                     bool hold,
                     bool tuning,
                     bool fill) {
    if (groove_button_.Update(groove)) {
      groove_ = static_cast<uint8_t>((groove_ + 1) % GROOVE_LAST);
      ResetEventBuckets();
    }
    if (mutate_button_.Update(mutate)) {
      MutatePattern();
      TriggerAll(0.76f);
    }
    if (hold_button_.Update(hold)) {
      hold_ = !hold_;
    }
    if (tuning_button_.Update(tuning)) {
      tuning_ = static_cast<uint8_t>((tuning_ + 1) % kNumTunings);
      tuning_dirty_ = true;
      TriggerAll(0.86f);
    }
    if (fill_button_.Update(fill)) {
      TriggerAll(1.0f);
    }
  }

  void ResetEventBuckets() {
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      event_bucket_[voice] = -1;
    }
  }

  void MutatePattern() {
    if (hold_) return;
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      const int32_t phase_code = static_cast<int32_t>(NextRandom() % 17u) - 8;
      phase_mutation_[voice] = static_cast<float>(phase_code) * 0.0075f;
      overtone_rotation_[voice] = static_cast<int8_t>(
          static_cast<int32_t>(NextRandom() % 5u) - 2);
    }
    tuning_dirty_ = true;
    ResetEventBuckets();
  }

  float VoicePhase(size_t voice) const {
    float offset = phase_mutation_[voice];
    if (voice == 1) offset += 0.25f * phase_;
    if (voice == 2) offset += 0.50f * phase_;
    return Wrap01(cycle_phase_ + offset);
  }

  float WarpForGroove(float phase) const {
    if (groove_ != GROOVE_SWAY) return phase;
    static const float split = 0.60f;
    if (phase < split) return phase * (0.5f / split);
    return 0.5f + (phase - split) * (0.5f / (1.0f - split));
  }

  float AccentForEvent(size_t voice) {
    const uint32_t index = event_index_[voice]++;
    if (groove_ == GROOVE_SWAY) {
      return (index & 1u) ? 0.68f : 1.0f;
    }
    if (groove_ == GROOVE_CLAVE) {
      const uint32_t slot = (index * 5u + static_cast<uint32_t>(voice) * 2u) & 7u;
      return (slot == 0u || slot == 3u || slot == 5u) ? 1.0f : 0.62f;
    }
    if (groove_ == GROOVE_DRIFT) {
      return 0.72f + 0.28f * static_cast<float>(NextRandom() & 255u) *
          (1.0f / 255.0f);
    }
    return index == 0u ? 1.0f : 0.86f;
  }

  void AdvanceClock() {
    uint8_t trigger_mask = 0;
    float trigger_amount[kNumVoices] = {0.0f, 0.0f, 0.0f};

    const float increment = static_cast<float>(tempo_) *
        (1.0f / (60.0f * 4.0f * 3000.0f));
    cycle_phase_ += increment;
    if (cycle_phase_ >= 1.0f) {
      cycle_phase_ -= 1.0f;
      ++cycle_count_;
      if (groove_ == GROOVE_DRIFT && !hold_) {
        for (size_t voice = 1; voice < kNumVoices; ++voice) {
          const int32_t drift = static_cast<int32_t>(NextRandom() % 5u) - 2;
          phase_mutation_[voice] = Clamp(
              phase_mutation_[voice] + static_cast<float>(drift) * 0.0035f,
              -0.09f, 0.09f);
        }
      }
    }

    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      if (flam_countdown_[voice] > 0) {
        --flam_countdown_[voice];
        if (flam_countdown_[voice] == 0) {
          TriggerVoice(voice, 0.46f, false);
        }
      }
      const float voice_phase = WarpForGroove(VoicePhase(voice));
      int32_t bucket = static_cast<int32_t>(
          voice_phase * static_cast<float>(hits_per_cycle_[voice]));
      if (bucket >= hits_per_cycle_[voice]) bucket = hits_per_cycle_[voice] - 1;
      if (event_bucket_[voice] < 0 || bucket != event_bucket_[voice]) {
        event_bucket_[voice] = bucket;
        trigger_mask |= static_cast<uint8_t>(1u << voice);
        trigger_amount[voice] = AccentForEvent(voice);
      }
    }

    if (trigger_mask != 0) ApplyRhythmTriggers(trigger_mask, trigger_amount);
  }

  void ApplyRhythmTriggers(uint8_t mask, const float amount[kNumVoices]) {
    uint8_t count = 0;
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      if (mask & (1u << voice)) {
        ++count;
        ++rhythm_hits_[voice];
        TriggerVoice(voice, amount[voice], true);
      }
    }
    if (count < 2) return;

    ++collision_count_;
    collision_envelope_ = std::max(collision_envelope_, 0.36f);
    if (hold_) return;

    const size_t chosen = static_cast<size_t>(NextRandom() % kNumVoices);
    const int32_t turn = (NextRandom() & 1u) ? 1 : -1;
    overtone_rotation_[chosen] = static_cast<int8_t>(ClampInt(
        overtone_rotation_[chosen] + turn, -2, 2));
    tuning_dirty_ = true;
    const size_t flam_voice = (chosen + 1) % kNumVoices;
    if (flam_countdown_[flam_voice] == 0) {
      flam_countdown_[flam_voice] = static_cast<uint16_t>(
          24u + (NextRandom() % 73u));
    }
  }

  void TriggerVoice(size_t voice, float amount, bool primary) {
    if (voice >= kNumVoices) return;
    voice_envelope_[voice] = std::max(
        voice_envelope_[voice], Clamp(amount, 0.05f, 1.0f));
    led_countdown_[voice] = 96;
    ++voice_events_[voice];
    if (!primary) collision_envelope_ = std::max(collision_envelope_, 0.14f);
  }

  void TriggerAll(float amount) {
    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      TriggerVoice(voice, amount, false);
    }
    collision_envelope_ = std::max(collision_envelope_, 0.40f);
  }

  void UpdateTuning() {
    static const int32_t base_midi[kNumVoices] = {31, 43, 55};
    const float tuning_decay = static_cast<float>(decay_step_) *
        (1.0f / 127.0f);
    const float tuning_material = static_cast<float>(material_step_) *
        (1.0f / 127.0f);
    const float decay_squared = tuning_decay * tuning_decay;
    const float body_q = 2.2f + 92.0f * decay_squared;
    const float overtone_q = 1.8f + body_q *
        (0.48f + 0.44f * tuning_material);
    const float material_interval = 7.0f + 16.0f *
        tuning_material * tuning_material;

    for (size_t voice = 0; voice < kNumVoices; ++voice) {
      const size_t body_band = voice * 2;
      const size_t overtone_band = body_band + 1;
      const int32_t body_slot = tune_step_[voice];
      const int32_t color_slot = Wrap12(
          body_slot + overtone_rotation_[voice]);
      const float body_semitones = static_cast<float>(base_midi[voice] - 69) +
          kTuningIntervals[tuning_][body_slot];
      const float color_offset = 0.18f *
          (kTuningIntervals[tuning_][color_slot] -
           kTuningIntervals[tuning_][body_slot]);
      const float overtone_semitones = body_semitones + material_interval +
          color_offset;
      const float body_frequency = Clamp(440.0f * stmlib::SemitonesToRatio(
          Clamp(body_semitones, -120.0f, 120.0f)), 24.0f, 12000.0f);
      const float overtone_frequency = Clamp(440.0f * stmlib::SemitonesToRatio(
          Clamp(overtone_semitones, -120.0f, 120.0f)), 24.0f, 15000.0f);
      resonator_[body_band].SetTarget(body_frequency, body_q);
      resonator_[overtone_band].SetTarget(overtone_frequency, overtone_q);
      if (!targets_initialized_) {
        resonator_[body_band].SnapToTarget();
        resonator_[overtone_band].SnapToTarget();
      }
    }
    targets_initialized_ = true;
    tuning_dirty_ = false;
  }

  static void ClearLine(char* line) {
    for (size_t i = 0; i < 21; ++i) line[i] = ' ';
    line[21] = '\0';
  }

  static void Put(char* line, size_t offset, const char* text) {
    while (*text && offset < 21) line[offset++] = *text++;
  }

  static void Put2(char* line, size_t offset, int32_t value) {
    value = ClampInt(value, 0, 99);
    if (offset < 21) line[offset] = static_cast<char>('0' + value / 10);
    if (offset + 1 < 21) line[offset + 1] = static_cast<char>('0' + value % 10);
  }

  static void Put3(char* line, size_t offset, int32_t value) {
    value = ClampInt(value, 0, 999);
    if (offset < 21) line[offset] = static_cast<char>('0' + value / 100);
    if (offset + 1 < 21) {
      line[offset + 1] = static_cast<char>('0' + (value / 10) % 10);
    }
    if (offset + 2 < 21) line[offset + 2] = static_cast<char>('0' + value % 10);
  }

  void UpdateDisplay() {
    static const char* groove_name[GROOVE_LAST] = {
      "EVN", "SWY", "CLV", "DRF"
    };
    static const char* tuning_name[kNumTunings] = {
      "HARM", "PENT", "JUST", "TRI "
    };
    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);

    Put(line1_, 0, "SPECTRUM TRIBE");
    Put(line2_, 0, "T");
    Put3(line2_, 1, tempo_);
    Put(line2_, 5, groove_name[groove_]);
    Put(line2_, 9, tuning_name[tuning_]);
    Put(line2_, 14, hold_ ? "HOLD" : "MOVE");

    Put(line3_, 0, "L");
    Put2(line3_, 1, hits_per_cycle_[0]);
    Put(line3_, 4, "M");
    Put2(line3_, 5, hits_per_cycle_[1]);
    Put(line3_, 8, "H");
    Put2(line3_, 9, hits_per_cycle_[2]);
    Put(line3_, 12, "P");
    Put2(line3_, 13, static_cast<int32_t>(phase_ * 99.0f + 0.5f));

    Put(line4_, 0, "D");
    Put2(line4_, 1, decay_step_ * 99 / 127);
    Put(line4_, 4, "M");
    Put2(line4_, 5, material_step_ * 99 / 127);
    Put(line4_, 8, "S");
    Put2(line4_, 9, static_cast<int32_t>(space_ * 99.0f + 0.5f));
    Put(line4_, 12, "C");
    Put2(line4_, 13, static_cast<int32_t>(collision_count_ % 100u));
  }

  ResonantBandpass resonator_[kNumBands];
  SpectralBloom bloom_;
  DebouncedButton groove_button_;
  DebouncedButton mutate_button_;
  DebouncedButton hold_button_;
  DebouncedButton tuning_button_;
  DebouncedButton fill_button_;

  float band_memory_[kNumBands];
  int32_t tune_step_[kNumVoices];
  uint8_t hits_per_cycle_[kNumVoices];
  int32_t event_bucket_[kNumVoices];
  uint32_t event_index_[kNumVoices];
  uint32_t rhythm_hits_[kNumVoices];
  uint32_t voice_events_[kNumVoices];
  float voice_envelope_[kNumVoices];
  float noise_state_[kNumVoices];
  float phase_mutation_[kNumVoices];
  int8_t overtone_rotation_[kNumVoices];
  uint16_t flam_countdown_[kNumVoices];
  uint16_t led_countdown_[kNumVoices];
  uint8_t groove_;
  uint8_t tuning_;
  bool hold_;
  bool controls_initialized_;
  bool targets_initialized_;
  bool tuning_dirty_;
  int32_t tempo_;
  int32_t decay_step_;
  int32_t material_step_;
  float decay_;
  float material_;
  float phase_;
  float space_;
  float cycle_phase_;
  uint32_t cycle_count_;
  uint32_t collision_count_;
  float collision_envelope_;
  uint32_t random_state_;
  uint8_t display_divider_;
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace spectrumtribe

#endif  // SPECTRUMTRIBE_DSP_H_
