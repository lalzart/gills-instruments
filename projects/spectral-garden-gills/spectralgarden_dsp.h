// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.

#ifndef SPECTRALGARDEN_DSP_H_
#define SPECTRALGARDEN_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stmlib/dsp/units.h"

namespace spectralgarden {

static const size_t kNumBands = 6;
static const size_t kNumSlots = 20;
static const size_t kNumScales = 7;

static const float kScaleIntervals[kNumScales][kNumSlots] = {
  {
    0.0f, 2.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.0f,
    12.0f, 14.0f, 16.0f, 17.0f, 19.0f, 21.0f, 23.0f,
    24.0f, 26.0f, 28.0f, 29.0f, 31.0f, 33.0f
  },
  {
    0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 8.0f, 10.0f,
    12.0f, 14.0f, 15.0f, 17.0f, 19.0f, 20.0f, 22.0f,
    24.0f, 26.0f, 27.0f, 29.0f, 31.0f, 32.0f
  },
  {
    0.0f, 2.0f, 5.0f, 7.0f, 9.0f,
    12.0f, 14.0f, 17.0f, 19.0f, 21.0f,
    24.0f, 26.0f, 29.0f, 31.0f, 33.0f,
    36.0f, 38.0f, 41.0f, 43.0f, 45.0f
  },
  {
    0.0f, 12.0f, 19.01955f, 24.0f, 27.86314f,
    31.01955f, 33.68826f, 36.0f, 38.03910f, 39.86314f,
    41.51318f, 43.01955f, 44.40528f, 45.68826f, 46.88269f,
    48.0f, 49.04955f, 50.03910f, 50.97234f, 51.86314f
  },
  {
    0.0f, 2.03910f, 3.86314f, 4.98045f, 7.01955f,
    8.84359f, 10.88269f, 12.0f, 14.03910f, 15.86314f,
    16.98045f, 19.01955f, 20.84359f, 22.88269f, 24.0f,
    26.03910f, 27.86314f, 28.98045f, 31.01955f, 32.84359f
  },
  {
    0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
    7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f,
    14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f
  },
  {
    0.0f, 1.46304f, 2.92608f, 4.38913f, 5.85217f,
    7.31521f, 8.77825f, 10.24129f, 11.70434f, 13.16738f,
    14.63042f, 16.09346f, 17.55651f, 19.01955f, 20.48259f,
    21.94563f, 23.40867f, 24.87172f, 26.33476f, 27.79780f
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

static inline int32_t Wrap20(int32_t value) {
  while (value < 0) value += 20;
  while (value >= 20) value -= 20;
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
    q = Clamp(q, 0.65f, 140.0f);
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

  void Reset() {
    z1_ = 0.0f;
    z2_ = 0.0f;
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

class Instrument {
 public:
  enum SourceMode {
    SOURCE_PULSE,
    SOURCE_AIR,
    SOURCE_INPUT,
    SOURCE_LAST
  };

  enum Parameter {
    PARAM_BAND_1,
    PARAM_BAND_2,
    PARAM_BAND_3,
    PARAM_BAND_4,
    PARAM_BAND_5,
    PARAM_BAND_6,
    PARAM_ROTATE,
    PARAM_SPREAD,
    PARAM_RESONANCE,
    PARAM_MOTION,
    PARAM_LAST
  };

  void Init() {
    source_button_.Init();
    mutate_button_.Init();
    hold_button_.Init();
    scale_button_.Init();
    strike_button_.Init();
    for (size_t i = 0; i < kNumBands; ++i) {
      resonator_[i].Init();
      band_level_[i] = 0.0f;
      band_target_[i] = 0.0f;
      band_reference_[i] = 0.0f;
      band_gain_[i] = 0.0f;
      band_memory_[i] = 0.0f;
      gesture_envelope_[i] = 0.0f;
      active_slot_[i] = static_cast<uint8_t>(i * 3);
    }

    source_mode_ = SOURCE_PULSE;
    scale_ = 0;
    root_note_ = 36;
    manual_rotation_ = 0;
    auto_rotation_ = 0;
    spread_ = 3;
    q_step_ = 72;
    motion_step_ = 32;
    motion_ = 0.0f;
    direction_ = 1;
    accent_band_ = 0;
    interaction_offset_ = 1;
    interaction_polarity_ = 1;
    hold_ = false;
    controls_initialized_ = false;
    targets_initialized_ = false;
    configuration_dirty_ = true;
    motion_countdown_ = 0;
    strike_led_blocks_ = 0;
    strike_envelope_ = 0.0f;
    random_state_ = 0x6d2b79f5u;
    display_divider_ = 0;
    UpdateTargets();
    UpdateDisplay();
  }

  void Process(const int32_t controls[PARAM_LAST],
               bool source_button,
               bool mutate_button,
               bool hold_button,
               bool scale_button,
               int32_t root_note,
               bool strike_button,
               const int32_t* input_left,
               const int32_t* input_right,
               int32_t* output_left,
               int32_t* output_right) {
    HandleButtons(source_button, mutate_button, hold_button,
                  scale_button, strike_button);
    UpdateParameters(controls, root_note);
    UpdateMotionClock();
    if (configuration_dirty_) UpdateTargets();

    const float motion_squared = motion_ * motion_;
    const float coefficient_slew = 0.00008f + 0.012f * motion_squared;
    const float q_normalized = static_cast<float>(q_step_) * (1.0f / 127.0f);
    // Narrow unity-peak bands discard most broadband strike energy. This
    // make-up keeps internal excitation useful across the full Q range.
    const float strike_gain = 6.5f + 23.5f * q_normalized;
    const float gesture_gain = 9.0f + 21.0f * q_normalized;
    const float interaction_depth = 0.025f + 0.11f * motion_ +
        0.04f * q_normalized;
    float gesture_total = 0.0f;
    for (size_t band = 0; band < kNumBands; ++band) {
      gesture_total += gesture_envelope_[band];
    }
    const float gesture_scale = gesture_total > 1.35f
        ? 1.35f / gesture_total
        : 1.0f;
    uint8_t routed_band[kNumBands];
    uint8_t neighbor_band[kNumBands];
    for (size_t band = 0; band < kNumBands; ++band) {
      routed_band[band] = static_cast<uint8_t>(Wrap6(
          static_cast<int32_t>(band) - interaction_offset_));
      neighbor_band[band] = static_cast<uint8_t>(Wrap6(
          static_cast<int32_t>(band) + 1));
    }
    static const float pan_left[kNumBands] = {
      1.00f, 0.78f, 0.42f, 0.22f, 0.58f, 0.92f
    };
    static const float pan_right[kNumBands] = {
      0.22f, 0.58f, 0.92f, 1.00f, 0.78f, 0.42f
    };

    for (size_t sample = 0; sample < BUFSIZE; ++sample) {
      const float noise = RandomBipolar();
      const float burst = noise * strike_envelope_ * strike_gain;
      strike_envelope_ *= 0.9920f;
      if (strike_envelope_ < 0.00001f) strike_envelope_ = 0.0f;

      const float external_left = static_cast<float>(input_left[sample]) *
          (1.0f / 134217728.0f);
      const float external_right = static_cast<float>(input_right[sample]) *
          (1.0f / 134217728.0f);
      float sum_left = 0.0f;
      float sum_right = 0.0f;
      float current_band_output[kNumBands];

      for (size_t band = 0; band < kNumBands; ++band) {
        const int32_t distance = Wrap6(static_cast<int32_t>(band) -
            static_cast<int32_t>(accent_band_));
        const float accent = distance == 0 ? 1.0f :
            (distance == 3 ? 0.58f : 0.22f);
        float drive = 0.0f;
        if (source_mode_ == SOURCE_PULSE) {
          drive = burst * accent;
        } else if (source_mode_ == SOURCE_AIR) {
          drive = noise * 0.35f + burst * (0.18f + 0.62f * accent);
        } else {
          drive = ((band & 1) ? external_right : external_left) * 0.82f +
              burst * accent * 0.72f;
        }

        const size_t routed_index = routed_band[band];
        const size_t neighbor_index = neighbor_band[band];
        const float routed = band_memory_[routed_index] *
            band_gain_[routed_index] * static_cast<float>(interaction_polarity_);
        const float neighbor = band_memory_[neighbor_index] *
            band_gain_[neighbor_index];
        drive += Clamp((routed + 0.45f * neighbor) * interaction_depth,
                       -0.75f, 0.75f);
        drive += noise * gesture_envelope_[band] * gesture_gain *
            gesture_scale;

        const float filtered = resonator_[band].Process(
            drive, coefficient_slew);
        current_band_output[band] = filtered;
        const float contribution = filtered * band_gain_[band];
        sum_left += contribution * pan_left[band];
        sum_right += contribution * pan_right[band];
      }

      for (size_t band = 0; band < kNumBands; ++band) {
        band_memory_[band] = Clamp(current_band_output[band], -2.0f, 2.0f);
        gesture_envelope_[band] *= 0.9960f;
        if (gesture_envelope_[band] < 0.00001f) {
          gesture_envelope_[band] = 0.0f;
        }
      }

      output_left[sample] = static_cast<int32_t>(
          SoftLimit(sum_left) * 120000000.0f);
      output_right[sample] = static_cast<int32_t>(
          SoftLimit(sum_right) * 120000000.0f);
    }

    if (strike_led_blocks_ > 0) --strike_led_blocks_;
    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay();
    }
  }

  bool strike_led() const { return strike_led_blocks_ > 0; }
  bool air_mode() const { return source_mode_ == SOURCE_AIR; }
  bool input_mode() const { return source_mode_ == SOURCE_INPUT; }
  bool hold() const { return hold_; }
  uint8_t source_mode() const { return source_mode_; }
  uint8_t scale() const { return scale_; }
  uint8_t active_slot(size_t band) const {
    return band < kNumBands ? active_slot_[band] : 0;
  }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static int32_t Wrap6(int32_t value) {
    while (value < 0) value += 6;
    while (value >= 6) value -= 6;
    return value;
  }

  static float SoftLimit(float input) {
    // Restore level lost by the six narrow bands, then bound the final mix.
    float value = Clamp(input * 6.0f, -1.0f, 1.0f);
    value = value - value * value * value * (1.0f / 3.0f);
    return value * 1.5f;
  }

  static float ShapeLevel(float level) {
    return level * (1.35f - 0.35f * level);
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

  void TriggerStrike(float amount) {
    strike_envelope_ = std::max(strike_envelope_, Clamp01(amount));
    strike_led_blocks_ = 120;
  }

  void AdvanceAccent() {
    const uint32_t random = NextRandom();
    accent_band_ = static_cast<uint8_t>((accent_band_ + 1 +
        ((random >> 7) & 1)) % kNumBands);
    interaction_offset_ = 1 + static_cast<uint8_t>((random >> 10) % 3u);
    interaction_polarity_ = ((random >> 14) & 1u) ? 1 : -1;
  }

  void TriggerBandGesture(size_t band, float amount) {
    const uint32_t random = NextRandom();
    amount = Clamp(amount, 0.08f, 0.90f);
    const size_t previous = static_cast<size_t>(Wrap6(
        static_cast<int32_t>(band) - 1));
    const size_t next = static_cast<size_t>(Wrap6(
        static_cast<int32_t>(band) + 1));
    const size_t distant = static_cast<size_t>(Wrap6(
        static_cast<int32_t>(band) + 2 + ((random >> 9) & 1u)));
    gesture_envelope_[band] = std::max(gesture_envelope_[band], amount);
    gesture_envelope_[previous] = std::max(
        gesture_envelope_[previous],
        amount * (0.28f + 0.16f * static_cast<float>((random >> 5) & 1u)));
    gesture_envelope_[next] = std::max(
        gesture_envelope_[next],
        amount * (0.28f + 0.16f * static_cast<float>((random >> 6) & 1u)));
    gesture_envelope_[distant] = std::max(
        gesture_envelope_[distant], amount * 0.18f);
    accent_band_ = static_cast<uint8_t>(band);
    interaction_offset_ = 1 + static_cast<uint8_t>((random >> 11) % 3u);
    interaction_polarity_ = ((random >> 15) & 1u) ? 1 : -1;
    strike_led_blocks_ = std::max<uint16_t>(strike_led_blocks_, 60);
    configuration_dirty_ = true;
  }

  void AdvancePattern() {
    const uint32_t random = NextRandom();
    if ((random & 7u) == 0u) direction_ = -direction_;
    const int32_t distance = 1 + static_cast<int32_t>((random >> 4) & 1u);
    auto_rotation_ = Wrap20(auto_rotation_ + direction_ * distance);
    AdvanceAccent();
    configuration_dirty_ = true;
  }

  void HandleButtons(bool source,
                     bool mutate,
                     bool hold,
                     bool scale,
                     bool strike) {
    if (source_button_.Update(source)) {
      source_mode_ = (source_mode_ + 1) % SOURCE_LAST;
      TriggerStrike(0.82f);
    }
    if (mutate_button_.Update(mutate)) {
      AdvancePattern();
      TriggerStrike(1.0f);
    }
    if (hold_button_.Update(hold)) {
      hold_ = !hold_;
    }
    if (scale_button_.Update(scale)) {
      scale_ = (scale_ + 1) % kNumScales;
      configuration_dirty_ = true;
      TriggerStrike(0.88f);
    }
    if (strike_button_.Update(strike)) {
      TriggerStrike(1.0f);
    }
  }

  void UpdateParameters(const int32_t controls[PARAM_LAST],
                        int32_t root_note) {
    for (size_t i = 0; i < kNumBands; ++i) {
      const float target = Q27ToFloat(controls[i]);
      band_target_[i] = target;
      if (!controls_initialized_) {
        band_level_[i] = target;
        band_reference_[i] = target;
      } else {
        band_level_[i] += 0.16f * (target - band_level_[i]);
        const float movement = fabsf(target - band_reference_[i]);
        if (movement >= 0.018f) {
          TriggerBandGesture(i, 0.08f + movement * 2.6f);
          band_reference_[i] = target;
        }
      }
    }
    controls_initialized_ = true;

    float shaped_level[kNumBands];
    for (size_t i = 0; i < kNumBands; ++i) {
      shaped_level[i] = ShapeLevel(band_level_[i]);
    }
    for (size_t i = 0; i < kNumBands; ++i) {
      const size_t previous = static_cast<size_t>(Wrap6(
          static_cast<int32_t>(i) - 1));
      const size_t next = static_cast<size_t>(Wrap6(
          static_cast<int32_t>(i) + 1));
      band_gain_[i] = Clamp(shaped_level[i] +
          0.14f * shaped_level[previous] + 0.09f * shaped_level[next],
          0.0f, 1.15f);
    }

    const int32_t next_rotation = ClampInt(static_cast<int32_t>(
        Q27ToFloat(controls[PARAM_ROTATE]) * 19.999f), 0, 19);
    const int32_t next_spread = 1 + ClampInt(static_cast<int32_t>(
        Q27ToFloat(controls[PARAM_SPREAD]) * 4.999f), 0, 4);
    const int32_t next_q_step = ClampInt(static_cast<int32_t>(
        Q27ToFloat(controls[PARAM_RESONANCE]) * 127.0f + 0.5f), 0, 127);
    const int32_t next_motion_step = ClampInt(static_cast<int32_t>(
        Q27ToFloat(controls[PARAM_MOTION]) * 127.0f + 0.5f), 0, 127);
    const int32_t next_root = ClampInt(root_note, 24, 60);

    if (next_rotation != manual_rotation_ || next_spread != spread_ ||
        next_q_step != q_step_ || next_root != root_note_) {
      manual_rotation_ = next_rotation;
      spread_ = next_spread;
      q_step_ = next_q_step;
      root_note_ = next_root;
      configuration_dirty_ = true;
    }
    motion_step_ = next_motion_step;
    const float motion_target = static_cast<float>(motion_step_) *
        (1.0f / 127.0f);
    motion_ += 0.12f * (motion_target - motion_);
  }

  uint32_t MotionIntervalBlocks() const {
    const float motion_rate = static_cast<float>(motion_step_) *
        (1.0f / 127.0f);
    const float slow = 1.0f - motion_rate;
    const float seconds = 0.12f + 7.88f * slow * slow * slow;
    return static_cast<uint32_t>(seconds * 3000.0f);
  }

  void UpdateMotionClock() {
    if (motion_step_ == 0) {
      motion_countdown_ = 0;
      return;
    }
    const uint32_t interval = MotionIntervalBlocks();
    if (motion_countdown_ == 0) {
      motion_countdown_ = interval;
      return;
    }
    if (motion_countdown_ > interval) motion_countdown_ = interval;
    --motion_countdown_;
    if (motion_countdown_ == 0) {
      if (hold_) {
        AdvanceAccent();
      } else {
        AdvancePattern();
      }
      if (source_mode_ != SOURCE_INPUT) TriggerStrike(0.92f);
      motion_countdown_ = interval;
    }
  }

  void UpdateTargets() {
    const float q_normalized = static_cast<float>(q_step_) * (1.0f / 127.0f);
    const float q = 0.70f + 119.30f * q_normalized * q_normalized *
        q_normalized;
    const int32_t rotation = Wrap20(manual_rotation_ + auto_rotation_);
    const float detune_depth = 0.14f + 0.18f * motion_;
    for (size_t band = 0; band < kNumBands; ++band) {
      const int32_t slot = Wrap20(rotation +
          static_cast<int32_t>(band) * spread_);
      active_slot_[band] = static_cast<uint8_t>(slot);
      const size_t previous = static_cast<size_t>(Wrap6(
          static_cast<int32_t>(band) - 1));
      const size_t next = static_cast<size_t>(Wrap6(
          static_cast<int32_t>(band) + 1));
      const float interaction_detune =
          (band_target_[previous] - band_target_[next]) * detune_depth *
          static_cast<float>(interaction_polarity_);
      const float semitones = static_cast<float>(root_note_ - 69) +
          kScaleIntervals[scale_][slot] + interaction_detune;
      const float frequency = Clamp(440.0f *
          stmlib::SemitonesToRatio(Clamp(semitones, -120.0f, 120.0f)),
          24.0f, 15000.0f);
      resonator_[band].SetTarget(frequency, q);
      if (!targets_initialized_) resonator_[band].SnapToTarget();
    }
    targets_initialized_ = true;
    configuration_dirty_ = false;
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
    if (offset + 1 < 21) {
      line[offset + 1] = static_cast<char>('0' + value % 10);
    }
  }

  void PutRoot(char* line, size_t offset) const {
    static const char* note_name[12] = {
      "C ", "C#", "D ", "D#", "E ", "F ",
      "F#", "G ", "G#", "A ", "A#", "B "
    };
    Put(line, offset, note_name[root_note_ % 12]);
    if (offset + 2 < 21) {
      line[offset + 2] = static_cast<char>('0' + root_note_ / 12 - 1);
    }
  }

  void UpdateDisplay() {
    static const char* scale_name[kNumScales] = {
      "MAJ ", "MIN ", "PENT", "HARM", "JUST", "CHRM", "TRI "
    };
    static const char* source_name[SOURCE_LAST] = {
      "PUL", "AIR", "IN "
    };
    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);

    Put(line1_, 0, "SPECTRAL GARDEN");

    PutRoot(line2_, 0);
    Put(line2_, 4, scale_name[scale_]);
    Put(line2_, 9, source_name[source_mode_]);
    Put(line2_, 13, hold_ ? "HOLD" : "MOVE");

    for (size_t band = 0; band < kNumBands; ++band) {
      Put2(line3_, band * 3, active_slot_[band]);
    }

    Put(line4_, 0, "R");
    Put2(line4_, 1, Wrap20(manual_rotation_ + auto_rotation_));
    Put(line4_, 4, "S");
    if (5 < 21) line4_[5] = static_cast<char>('0' + spread_);
    Put(line4_, 7, "Q");
    Put2(line4_, 8, q_step_ * 99 / 127);
    Put(line4_, 11, "M");
    Put2(line4_, 12, motion_step_ * 99 / 127);
  }

  ResonantBandpass resonator_[kNumBands];
  DebouncedButton source_button_;
  DebouncedButton mutate_button_;
  DebouncedButton hold_button_;
  DebouncedButton scale_button_;
  DebouncedButton strike_button_;

  float band_level_[kNumBands];
  float band_target_[kNumBands];
  float band_reference_[kNumBands];
  float band_gain_[kNumBands];
  float band_memory_[kNumBands];
  float gesture_envelope_[kNumBands];
  uint8_t active_slot_[kNumBands];
  uint8_t source_mode_;
  uint8_t scale_;
  int32_t root_note_;
  int32_t manual_rotation_;
  int32_t auto_rotation_;
  int32_t spread_;
  int32_t q_step_;
  int32_t motion_step_;
  float motion_;
  int32_t direction_;
  uint8_t accent_band_;
  uint8_t interaction_offset_;
  int8_t interaction_polarity_;
  bool hold_;
  bool controls_initialized_;
  bool targets_initialized_;
  bool configuration_dirty_;
  uint32_t motion_countdown_;
  uint16_t strike_led_blocks_;
  float strike_envelope_;
  uint32_t random_state_;
  uint8_t display_divider_;
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace spectralgarden

#endif  // SPECTRALGARDEN_DSP_H_
