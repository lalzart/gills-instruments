// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.

#ifndef KNOBLOOM_DSP_H_
#define KNOBLOOM_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stmlib/dsp/units.h"

namespace knobloom {

static const size_t kNumThreads = 10;
static const size_t kNumScales = 5;

static const float kScaleDegrees[kNumScales][7] = {
  {0.0f, 2.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.0f},
  {0.0f, 3.0f, 5.0f, 7.0f, 10.0f, 0.0f, 0.0f},
  {0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 9.0f, 10.0f},
  {0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 8.0f, 11.0f},
  {0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 0.0f}
};

static const uint8_t kScaleLengths[kNumScales] = {7, 5, 7, 7, 6};

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

class ModalMode {
 public:
  void Init() {
    coefficient_ = 0.0f;
    radius_squared_ = 0.0f;
    excitation_scale_ = 0.0f;
    y1_ = 0.0f;
    y2_ = 0.0f;
  }

  void Configure(float frequency, float radius) {
    frequency = Clamp(frequency, 24.0f, 15000.0f);
    radius = Clamp(radius, 0.9900f, 0.999999f);
    const float omega = 6.28318530718f * frequency * (1.0f / 48000.0f);
    coefficient_ = 2.0f * radius * cosf(omega);
    radius_squared_ = radius * radius;
    // A raw impulse into this resonator grows approximately as 1/sin(omega).
    // Compensating at configuration time keeps low threads from receiving
    // dramatically more energy than high threads for the same pot gesture.
    excitation_scale_ = Clamp(fabsf(sinf(omega)), 0.003f, 1.0f);
  }

  float Process(float excitation) {
    const float output = Clamp(
        excitation * excitation_scale_ +
        coefficient_ * y1_ - radius_squared_ * y2_,
        -2.4f, 2.4f);
    y2_ = y1_;
    y1_ = output;
    return output;
  }

 private:
  float coefficient_;
  float radius_squared_;
  float excitation_scale_;
  float y1_;
  float y2_;
};

class ThreadVoice {
 public:
  void Init() {
    fundamental_.Init();
    overtone_.Init();
  }

  void Configure(float frequency, float radius, size_t index) {
    fundamental_.Configure(frequency, radius);
    const float overtone_ratio = 2.005f + 0.007f *
        static_cast<float>(index % 4);
    overtone_.Configure(frequency * overtone_ratio,
                        0.9965f + 0.00345f *
                        ((radius - 0.9900f) * (1.0f / 0.009999f)));
  }

  float Process(float excitation, float coupling) {
    const float input = excitation + coupling;
    const float fundamental = fundamental_.Process(input);
    const float overtone = overtone_.Process(input * 0.32f);
    return 0.78f * fundamental + 0.30f * overtone;
  }

 private:
  ModalMode fundamental_;
  ModalMode overtone_;
};

class Instrument {
 public:
  enum Parameter {
    PARAM_THREAD_1,
    PARAM_THREAD_2,
    PARAM_THREAD_3,
    PARAM_THREAD_4,
    PARAM_THREAD_5,
    PARAM_THREAD_6,
    PARAM_THREAD_7,
    PARAM_THREAD_8,
    PARAM_THREAD_9,
    PARAM_THREAD_10,
    PARAM_LAST
  };

  enum Topology {
    TOPOLOGY_RING,
    TOPOLOGY_STAR,
    TOPOLOGY_PAIRS,
    TOPOLOGY_CROSS,
    TOPOLOGY_LAST
  };

  void Init() {
    strum_button_.Init();
    reverse_button_.Init();
    sustain_button_.Init();
    tangle_button_.Init();
    scale_button_.Init();
    for (size_t thread = 0; thread < kNumThreads; ++thread) {
      voice_[thread].Init();
      pot_smoothed_[thread] = 0.0f;
      pot_previous_[thread] = 0.0f;
      bow_amount_[thread] = 0.0f;
      pending_impulse_[thread] = 0.0f;
      last_output_[thread] = 0.0f;
      slot_[thread] = 0;
      strum_countdown_[thread] = 0;
      excitation_count_[thread] = 0;
    }
    controls_initialized_ = false;
    tuning_dirty_ = true;
    reverse_ = false;
    sustain_ = false;
    topology_ = TOPOLOGY_RING;
    scale_ = 0;
    decay_step_ = 72;
    activity_blocks_ = 0;
    random_state_ = 0x94d049bbu;
    display_divider_ = 0;
    UpdateTuning();
    UpdateDisplay();
  }

  void Process(const int32_t controls[PARAM_LAST],
               bool strum_button,
               bool reverse_button,
               bool sustain_button,
               bool tangle_button,
               int32_t decay,
               bool scale_button,
               int32_t* output_left,
               int32_t* output_right) {
    HandleButtons(strum_button, reverse_button, sustain_button,
                  tangle_button, scale_button);
    UpdateControls(controls, decay);
    UpdateStrumSchedule();
    if (tuning_dirty_) UpdateTuning();

    static const float pan_left[kNumThreads] = {
      1.00f, 0.98f, 0.92f, 0.82f, 0.70f,
      0.57f, 0.43f, 0.29f, 0.16f, 0.07f
    };
    static const float pan_right[kNumThreads] = {
      0.07f, 0.16f, 0.29f, 0.43f, 0.57f,
      0.70f, 0.82f, 0.92f, 0.98f, 1.00f
    };
    const float coupling_depth = topology_ == TOPOLOGY_RING ? 0.0022f :
        (topology_ == TOPOLOGY_PAIRS ? 0.0034f : 0.0028f);

    for (size_t sample = 0; sample < BUFSIZE; ++sample) {
      const float noise = RandomBipolar();
      float current_output[kNumThreads];
      float sum_left = 0.0f;
      float sum_right = 0.0f;
      for (size_t thread = 0; thread < kNumThreads; ++thread) {
        float excitation = noise * bow_amount_[thread] *
            ((thread & 1u) ? -0.020f : 0.020f);
        if (sample == 0) {
          excitation += pending_impulse_[thread];
          pending_impulse_[thread] = 0.0f;
        }
        const size_t source = CouplingSource(thread);
        const float polarity = (topology_ == TOPOLOGY_CROSS &&
            ((thread + source) & 1u)) ? -1.0f : 1.0f;
        const float coupled = last_output_[source] * coupling_depth * polarity;
        const float value = voice_[thread].Process(excitation, coupled);
        current_output[thread] = value;
        sum_left += value * pan_left[thread];
        sum_right += value * pan_right[thread];
      }
      for (size_t thread = 0; thread < kNumThreads; ++thread) {
        last_output_[thread] = current_output[thread];
        bow_amount_[thread] *= 0.9975f;
        if (bow_amount_[thread] < 0.00001f) bow_amount_[thread] = 0.0f;
      }
      output_left[sample] = static_cast<int32_t>(
          SoftLimit(sum_left * 0.12f) * 120000000.0f);
      output_right[sample] = static_cast<int32_t>(
          SoftLimit(sum_right * 0.12f) * 120000000.0f);
    }

    if (activity_blocks_ > 0) --activity_blocks_;
    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay();
    }
  }

  bool activity() const { return activity_blocks_ > 0; }
  bool reverse() const { return reverse_; }
  bool sustain() const { return sustain_; }
  uint8_t topology() const { return topology_; }
  uint8_t scale() const { return scale_; }
  int32_t decay_step() const { return decay_step_; }
  uint8_t slot(size_t thread) const {
    return thread < kNumThreads ? slot_[thread] : 0;
  }
  uint32_t excitation_count(size_t thread) const {
    return thread < kNumThreads ? excitation_count_[thread] : 0;
  }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static float SoftLimit(float input) {
    float value = Clamp(input * 1.5f, -1.0f, 1.0f);
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

  float ScaleInterval(uint8_t slot) const {
    const uint8_t length = kScaleLengths[scale_];
    const uint8_t octave = static_cast<uint8_t>(slot / length);
    const uint8_t degree = static_cast<uint8_t>(slot % length);
    return 12.0f * static_cast<float>(octave) +
        kScaleDegrees[scale_][degree];
  }

  void Excite(size_t thread, float amount) {
    if (thread >= kNumThreads) return;
    pending_impulse_[thread] = Clamp(
        pending_impulse_[thread] + amount, -0.18f, 0.18f);
    activity_blocks_ = 120;
    ++excitation_count_[thread];
  }

  void StartStrum(float amount) {
    for (size_t order = 0; order < kNumThreads; ++order) {
      const size_t thread = reverse_ ? (kNumThreads - 1 - order) : order;
      strum_countdown_[thread] = static_cast<uint16_t>(1 + order * 20);
      strum_amount_[thread] = amount *
          (0.82f + 0.18f * static_cast<float>((thread * 7u) % 5u) * 0.25f);
    }
    activity_blocks_ = 240;
  }

  void UpdateStrumSchedule() {
    for (size_t thread = 0; thread < kNumThreads; ++thread) {
      if (strum_countdown_[thread] == 0) continue;
      --strum_countdown_[thread];
      if (strum_countdown_[thread] == 0) {
        const float polarity = (thread & 1u) ? -1.0f : 1.0f;
        Excite(thread, polarity * 0.10f * strum_amount_[thread]);
      }
    }
  }

  void HandleButtons(bool strum,
                     bool reverse,
                     bool sustain,
                     bool tangle,
                     bool scale) {
    if (strum_button_.Update(strum)) StartStrum(1.0f);
    if (reverse_button_.Update(reverse)) {
      reverse_ = !reverse_;
      StartStrum(0.60f);
    }
    if (sustain_button_.Update(sustain)) {
      sustain_ = !sustain_;
      tuning_dirty_ = true;
    }
    if (tangle_button_.Update(tangle)) {
      topology_ = static_cast<uint8_t>((topology_ + 1) % TOPOLOGY_LAST);
      StartStrum(0.72f);
    }
    if (scale_button_.Update(scale)) {
      scale_ = static_cast<uint8_t>((scale_ + 1) % kNumScales);
      tuning_dirty_ = true;
      StartStrum(0.86f);
    }
  }

  void UpdateControls(const int32_t controls[PARAM_LAST], int32_t decay) {
    bool slots_changed = false;
    for (size_t thread = 0; thread < kNumThreads; ++thread) {
      const float target = Q27ToFloat(controls[thread]);
      if (!controls_initialized_) {
        pot_smoothed_[thread] = target;
        pot_previous_[thread] = target;
      } else {
        pot_smoothed_[thread] += 0.12f * (target - pot_smoothed_[thread]);
        const float delta = pot_smoothed_[thread] - pot_previous_[thread];
        pot_previous_[thread] = pot_smoothed_[thread];
        const float speed = fabsf(delta);
        const float bow = Clamp((speed - 0.00004f) * 450.0f, 0.0f, 1.0f);
        if (bow > 0.012f) {
          bow_amount_[thread] = std::max(bow_amount_[thread], bow);
          const float impulse = Clamp(delta * 10.0f, -0.055f, 0.055f);
          Excite(thread, impulse);
        }
      }
      const uint8_t next_slot = static_cast<uint8_t>(ClampInt(
          static_cast<int32_t>(pot_smoothed_[thread] * 20.999f), 0, 20));
      if (next_slot != slot_[thread]) {
        slot_[thread] = next_slot;
        slots_changed = true;
      }
    }
    controls_initialized_ = true;
    const int32_t next_decay = ClampInt(decay, 0, 127);
    if (next_decay != decay_step_) {
      decay_step_ = next_decay;
      slots_changed = true;
    }
    if (slots_changed) tuning_dirty_ = true;
  }

  size_t CouplingSource(size_t thread) const {
    if (topology_ == TOPOLOGY_RING) {
      return reverse_ ? (thread + 1) % kNumThreads :
          (thread + kNumThreads - 1) % kNumThreads;
    }
    if (topology_ == TOPOLOGY_STAR) {
      if (thread == 4) return reverse_ ? 9 : 0;
      if (thread == 5) return reverse_ ? 0 : 9;
      return thread < 5 ? 4 : 5;
    }
    if (topology_ == TOPOLOGY_PAIRS) {
      return thread ^ 1u;
    }
    return reverse_ ? (thread + 5) % kNumThreads :
        (thread + kNumThreads - 5) % kNumThreads;
  }

  void UpdateTuning() {
    const float decay = static_cast<float>(decay_step_) * (1.0f / 127.0f);
    float radius = 0.99920f + 0.000795f * decay * decay;
    if (sustain_) radius = std::max(radius, 0.999998f);
    for (size_t thread = 0; thread < kNumThreads; ++thread) {
      const float semitones = -33.0f + ScaleInterval(slot_[thread]);
      const float frequency = Clamp(440.0f * stmlib::SemitonesToRatio(
          Clamp(semitones, -120.0f, 120.0f)), 24.0f, 7200.0f);
      voice_[thread].Configure(frequency, radius, thread);
    }
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
    if (offset + 1 < 21) line[offset + 1] =
        static_cast<char>('0' + value % 10);
  }

  void UpdateDisplay() {
    static const char* scale_name[kNumScales] = {
      "MAJ", "PEN", "DOR", "HAR", "WHO"
    };
    static const char* topology_name[TOPOLOGY_LAST] = {
      "RING", "STAR", "PAIR", "XCRS"
    };
    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);
    Put(line1_, 0, "KNOB LOOM");
    Put(line2_, 0, scale_name[scale_]);
    Put(line2_, 4, "DEC");
    Put2(line2_, 8, decay_step_ * 99 / 127);
    Put(line2_, 12, sustain_ ? "SUST" : "FREE");
    for (size_t thread = 0; thread < kNumThreads; ++thread) {
      Put2(line3_, thread * 2, slot_[thread]);
    }
    Put(line4_, 0, reverse_ ? "REV" : "FWD");
    Put(line4_, 4, topology_name[topology_]);
    Put(line4_, 9, activity() ? "MOVING" : "TOUCH A KNOB");
  }

  ThreadVoice voice_[kNumThreads];
  DebouncedButton strum_button_;
  DebouncedButton reverse_button_;
  DebouncedButton sustain_button_;
  DebouncedButton tangle_button_;
  DebouncedButton scale_button_;

  float pot_smoothed_[kNumThreads];
  float pot_previous_[kNumThreads];
  float bow_amount_[kNumThreads];
  float pending_impulse_[kNumThreads];
  float last_output_[kNumThreads];
  float strum_amount_[kNumThreads];
  uint8_t slot_[kNumThreads];
  uint16_t strum_countdown_[kNumThreads];
  uint32_t excitation_count_[kNumThreads];
  bool controls_initialized_;
  bool tuning_dirty_;
  bool reverse_;
  bool sustain_;
  uint8_t topology_;
  uint8_t scale_;
  int32_t decay_step_;
  uint16_t activity_blocks_;
  uint32_t random_state_;
  uint8_t display_divider_;
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace knobloom

#endif  // KNOBLOOM_DSP_H_
