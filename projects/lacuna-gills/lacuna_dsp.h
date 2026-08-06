// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.

#ifndef LACUNA_DSP_H_
#define LACUNA_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stmlib/dsp/units.h"

namespace lacuna {

static const size_t kNumLayers = 3;
static const size_t kPatternSteps = 64;

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

class StatefulButton {
 public:
  enum Event {
    EVENT_NONE = 0,
    EVENT_PRESS = 1,
    EVENT_RELEASE = 2
  };

  void Init() {
    stable_ = false;
    candidate_ = false;
    count_ = 0;
  }

  Event Update(bool raw) {
    if (raw == candidate_) {
      if (count_ < kDebounceBlocks) ++count_;
    } else {
      candidate_ = raw;
      count_ = 0;
    }
    if (count_ >= kDebounceBlocks && stable_ != candidate_) {
      stable_ = candidate_;
      count_ = 0;
      return stable_ ? EVENT_PRESS : EVENT_RELEASE;
    }
    return EVENT_NONE;
  }

  bool stable() const { return stable_; }

 private:
  static const uint8_t kDebounceBlocks = 32;
  bool stable_;
  bool candidate_;
  uint8_t count_;
};

class PhaseOscillator {
 public:
  void Init(uint32_t phase) {
    phase_ = phase;
    increment_ = 0;
  }

  void SetFrequency(float frequency) {
    frequency = Clamp(frequency, 0.001f, 15000.0f);
    increment_ = static_cast<uint32_t>(
        frequency * (4294967296.0f / 48000.0f));
  }

  float Triangle() {
    phase_ += increment_;
    const float bipolar = static_cast<float>(static_cast<int32_t>(phase_)) *
        (1.0f / 2147483648.0f);
    return 2.0f * fabsf(bipolar) - 1.0f;
  }

  float Saw() {
    phase_ += increment_;
    return static_cast<float>(static_cast<int32_t>(phase_)) *
        (1.0f / 2147483648.0f);
  }

 private:
  uint32_t phase_;
  uint32_t increment_;
};

class Instrument {
 public:
  enum Parameter {
    PARAM_PITCH,
    PARAM_SPREAD,
    PARAM_DENSITY,
    PARAM_AIR,
    PARAM_MOTION,
    PARAM_GAP,
    PARAM_EDGE,
    PARAM_DEPTH,
    PARAM_MUTATION,
    PARAM_SCAR,
    PARAM_LAST
  };

  enum GapBit {
    GAP_BODY = 1,
    GAP_FORM = 2,
    GAP_AIR = 4,
    GAP_VOID = 8
  };

  void Init() {
    for (size_t i = 0; i < 4; ++i) gap_button_[i].Init();
    capture_button_.Init();
    body_[0].Init(0x00000000u);
    body_[1].Init(0x531f2a91u);
    form_[0].Init(0x17000000u);
    form_[1].Init(0x69000000u);
    form_[2].Init(0xb3000000u);
    motion_lfo_.Init(0x28000000u);
    for (size_t layer = 0; layer < kNumLayers; ++layer) {
      scar_oscillator_[layer].Init(static_cast<uint32_t>(
          0x34000000u + layer * 0x31000000u));
      layer_gain_[layer] = 1.0f;
      layer_target_[layer] = 1.0f;
      scar_envelope_[layer] = 0.0f;
      gap_active_[layer] = false;
    }
    memset(pattern_, 0, sizeof(pattern_));
    memset(mutation_pattern_, 0, sizeof(mutation_pattern_));
    memset(gap_countdown_, 0, sizeof(gap_countdown_));
    memset(parameters_, 0, sizeof(parameters_));
    parameters_[PARAM_PITCH] = 0.35f;
    parameters_[PARAM_SPREAD] = 0.42f;
    parameters_[PARAM_DENSITY] = 0.58f;
    parameters_[PARAM_AIR] = 0.32f;
    parameters_[PARAM_MOTION] = 0.18f;
    parameters_[PARAM_GAP] = 0.22f;
    parameters_[PARAM_EDGE] = 0.12f;
    parameters_[PARAM_DEPTH] = 1.0f;
    parameters_[PARAM_MUTATION] = 0.0f;
    parameters_[PARAM_SCAR] = 0.18f;
    controls_initialized_ = false;
    oscillator_dirty_ = true;
    pitch_step_ = 0;
    spread_step_ = 0;
    tempo_ = 96;
    recording_ = false;
    playing_ = false;
    loop_phase_ = 0.0f;
    current_step_ = 0;
    loop_count_ = 0;
    capture_hold_blocks_ = 0;
    manual_gap_mask_ = 0;
    playback_gap_mask_ = 0;
    active_gap_mask_ = 0;
    clear_count_ = 0;
    random_state_ = 0xc3a5c85cu;
    body_filter_ = 0.0f;
    air_filter_ = 0.0f;
    air_slow_ = 0.0f;
    display_divider_ = 0;
    UpdateOscillators();
    UpdateDisplay();
  }

  void Process(const int32_t controls[PARAM_LAST],
               bool body_button,
               bool form_button,
               bool air_button,
               bool void_button,
               int32_t tempo,
               bool capture_button,
               int32_t* output_left,
               int32_t* output_right) {
    UpdateParameters(controls, tempo);
    HandleButtons(body_button, form_button, air_button, void_button,
                  capture_button);
    UpdateManualGaps();
    UpdateLoop();
    active_gap_mask_ = static_cast<uint8_t>(
        manual_gap_mask_ | playback_gap_mask_);
    UpdateGapTargets();
    if (oscillator_dirty_) UpdateOscillators();

    const float density = parameters_[PARAM_DENSITY];
    const float air = parameters_[PARAM_AIR];
    const float motion = parameters_[PARAM_MOTION];
    const float edge = parameters_[PARAM_EDGE];
    const float scar = parameters_[PARAM_SCAR];
    const float edge_inverse = 1.0f - edge;
    const float edge_coefficient = 0.00045f +
        0.18f * edge_inverse * edge_inverse * edge_inverse;
    const float scar_decay = 0.9960f + 0.0037f * scar;

    for (size_t sample = 0; sample < BUFSIZE; ++sample) {
      const float lfo = motion_lfo_.Triangle();
      const float body_raw = 0.62f * body_[0].Triangle() +
          0.38f * body_[1].Triangle();
      body_filter_ += (0.012f + 0.035f * density) *
          (body_raw - body_filter_);
      const float body_signal = body_filter_ *
          (0.72f + 0.10f * lfo * motion);

      const float form_a = form_[0].Saw();
      const float form_b = form_[1].Triangle();
      const float form_c = form_[2].Saw();
      float form_signal = (0.46f * form_a + 0.34f * form_b +
          0.24f * form_c) * (0.22f + 0.56f * density);
      form_signal = form_signal - 0.24f * density *
          form_signal * form_signal * form_signal;

      const float noise = RandomBipolar();
      const float air_coefficient = 0.003f + 0.30f * air * air;
      air_filter_ += air_coefficient * (noise - air_filter_);
      air_slow_ += 0.0012f * (air_filter_ - air_slow_);
      const float air_signal = ((1.0f - air) * air_slow_ +
          air * (noise - 0.70f * air_filter_)) * (0.10f + 0.25f * air);

      float layer_signal[kNumLayers] = {
        body_signal, form_signal, air_signal
      };
      for (size_t layer = 0; layer < kNumLayers; ++layer) {
        layer_gain_[layer] += edge_coefficient *
            (layer_target_[layer] - layer_gain_[layer]);
        if (layer_target_[layer] == 0.0f && layer_gain_[layer] < 0.00001f) {
          layer_gain_[layer] = 0.0f;
        }
        if (layer_target_[layer] == 1.0f && layer_gain_[layer] > 0.99999f) {
          layer_gain_[layer] = 1.0f;
        }
        const float scar_wave = scar_oscillator_[layer].Triangle();
        layer_signal[layer] += scar_wave * scar_envelope_[layer] * 0.20f;
        scar_envelope_[layer] *= scar_decay;
        if (scar_envelope_[layer] < 0.00001f) scar_envelope_[layer] = 0.0f;
        layer_signal[layer] *= layer_gain_[layer];
      }

      const float motion_pan = 0.16f * lfo * motion;
      const float left = 0.72f * layer_signal[0] +
          (0.72f - motion_pan) * layer_signal[1] +
          (0.42f + motion_pan) * layer_signal[2];
      const float right = 0.72f * layer_signal[0] +
          (0.42f + motion_pan) * layer_signal[1] +
          (0.72f - motion_pan) * layer_signal[2];
      output_left[sample] = static_cast<int32_t>(
          SoftLimit(left) * 120000000.0f);
      output_right[sample] = static_cast<int32_t>(
          SoftLimit(right) * 120000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay();
    }
  }

  bool recording() const { return recording_; }
  bool playing() const { return playing_; }
  int32_t tempo() const { return tempo_; }
  uint8_t current_step() const { return current_step_; }
  uint32_t loop_count() const { return loop_count_; }
  uint32_t clear_count() const { return clear_count_; }
  uint8_t gap_mask() const { return active_gap_mask_; }
  uint8_t pattern_step(size_t step) const {
    return step < kPatternSteps ? pattern_[step] : 0;
  }
  uint8_t mutation_step(size_t step) const {
    return step < kPatternSteps ? mutation_pattern_[step] : 0;
  }
  float layer_gain(size_t layer) const {
    return layer < kNumLayers ? layer_gain_[layer] : 0.0f;
  }
  bool layer_present(size_t layer) const {
    if (layer >= kNumLayers) return false;
    return (active_gap_mask_ & (1u << layer)) == 0 &&
        (active_gap_mask_ & GAP_VOID) == 0;
  }
  bool field_present() const {
    return (active_gap_mask_ & GAP_VOID) == 0;
  }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static const uint32_t kCaptureClearBlocks = 1800;

  static float SoftLimit(float input) {
    float value = Clamp(input * 1.15f, -1.0f, 1.0f);
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

  uint32_t GapDurationBlocks() const {
    const float gap = parameters_[PARAM_GAP];
    return static_cast<uint32_t>(60.0f + 2200.0f * gap * gap);
  }

  void HandleButtons(bool body,
                     bool form,
                     bool air,
                     bool void_gap,
                     bool capture) {
    const bool raw[4] = {body, form, air, void_gap};
    for (size_t lane = 0; lane < 4; ++lane) {
      const StatefulButton::Event event = gap_button_[lane].Update(raw[lane]);
      if (event == StatefulButton::EVENT_PRESS) {
        gap_countdown_[lane] = GapDurationBlocks();
      }
    }

    const StatefulButton::Event capture_event = capture_button_.Update(capture);
    if (capture_button_.stable()) {
      if (capture_hold_blocks_ < kCaptureClearBlocks + 1) {
        ++capture_hold_blocks_;
      }
    }
    if (capture_event == StatefulButton::EVENT_RELEASE) {
      if (capture_hold_blocks_ >= kCaptureClearBlocks) {
        ClearLoop();
      } else {
        StartRecording();
      }
      capture_hold_blocks_ = 0;
    }
  }

  void UpdateManualGaps() {
    manual_gap_mask_ = 0;
    for (size_t lane = 0; lane < 4; ++lane) {
      if (gap_countdown_[lane] > 0) --gap_countdown_[lane];
      if (gap_button_[lane].stable() || gap_countdown_[lane] > 0) {
        manual_gap_mask_ |= static_cast<uint8_t>(1u << lane);
      }
    }
  }

  void StartRecording() {
    memset(pattern_, 0, sizeof(pattern_));
    memset(mutation_pattern_, 0, sizeof(mutation_pattern_));
    recording_ = true;
    playing_ = false;
    loop_phase_ = 0.0f;
    current_step_ = 0;
  }

  void ClearLoop() {
    memset(pattern_, 0, sizeof(pattern_));
    memset(mutation_pattern_, 0, sizeof(mutation_pattern_));
    recording_ = false;
    playing_ = false;
    loop_phase_ = 0.0f;
    current_step_ = 0;
    playback_gap_mask_ = 0;
    ++clear_count_;
  }

  void UpdateLoop() {
    current_step_ = static_cast<uint8_t>(ClampInt(
        static_cast<int32_t>(loop_phase_ * static_cast<float>(kPatternSteps)),
        0, static_cast<int32_t>(kPatternSteps - 1)));
    if (recording_) pattern_[current_step_] |= manual_gap_mask_;
    playback_gap_mask_ = playing_ ? static_cast<uint8_t>(
        pattern_[current_step_] | mutation_pattern_[current_step_]) : 0;

    const float increment = static_cast<float>(tempo_) *
        (1.0f / (60.0f * 4.0f * 3000.0f));
    loop_phase_ += increment;
    if (loop_phase_ < 1.0f) return;
    loop_phase_ -= 1.0f;
    ++loop_count_;
    if (recording_) {
      recording_ = false;
      playing_ = true;
    }
    BuildMutationPattern();
  }

  void BuildMutationPattern() {
    memset(mutation_pattern_, 0, sizeof(mutation_pattern_));
    const float amount = parameters_[PARAM_MUTATION];
    if (!playing_ || amount < 0.02f) return;
    const size_t additions = static_cast<size_t>(amount * 4.999f);
    for (size_t addition = 0; addition < additions; ++addition) {
      size_t source = static_cast<size_t>(NextRandom() % kPatternSteps);
      for (size_t search = 0; search < kPatternSteps && pattern_[source] == 0;
           ++search) {
        source = (source + 1) % kPatternSteps;
      }
      if (pattern_[source] == 0) return;
      const int32_t shift = static_cast<int32_t>(NextRandom() % 5u) - 2;
      const size_t destination = static_cast<size_t>(
          (static_cast<int32_t>(source) + shift +
           static_cast<int32_t>(kPatternSteps)) %
          static_cast<int32_t>(kPatternSteps));
      mutation_pattern_[destination] |= pattern_[source];
      if (amount > 0.55f && (NextRandom() & 1u)) {
        mutation_pattern_[(destination + 1) % kPatternSteps] |=
            pattern_[source];
      }
      if (amount > 0.88f && (NextRandom() & 7u) == 0u) {
        mutation_pattern_[destination] |= GAP_VOID;
      }
    }
  }

  void UpdateGapTargets() {
    const bool void_active = (active_gap_mask_ & GAP_VOID) != 0;
    const float depth = parameters_[PARAM_DEPTH];
    const float scar = parameters_[PARAM_SCAR];
    for (size_t layer = 0; layer < kNumLayers; ++layer) {
      const bool absent = void_active ||
          ((active_gap_mask_ & (1u << layer)) != 0);
      if (gap_active_[layer] && !absent) {
        scar_envelope_[layer] = std::max(scar_envelope_[layer], scar);
      }
      gap_active_[layer] = absent;
      layer_target_[layer] = absent ? (void_active ? 0.0f : 1.0f - depth) :
          1.0f;
    }
  }

  void UpdateParameters(const int32_t controls[PARAM_LAST], int32_t tempo) {
    for (size_t parameter = 0; parameter < PARAM_LAST; ++parameter) {
      const float target = Q27ToFloat(controls[parameter]);
      if (!controls_initialized_) {
        parameters_[parameter] = target;
      } else {
        const float slew = parameter >= PARAM_GAP ? 0.10f : 0.06f;
        parameters_[parameter] += slew *
            (target - parameters_[parameter]);
      }
    }
    controls_initialized_ = true;
    const int32_t next_pitch = ClampInt(static_cast<int32_t>(
        parameters_[PARAM_PITCH] * 36.999f), 0, 36);
    const int32_t next_spread = ClampInt(static_cast<int32_t>(
        parameters_[PARAM_SPREAD] * 12.999f), 0, 12);
    if (next_pitch != pitch_step_ || next_spread != spread_step_) {
      pitch_step_ = next_pitch;
      spread_step_ = next_spread;
      oscillator_dirty_ = true;
    }
    tempo_ = ClampInt(tempo, 40, 180);
    motion_lfo_.SetFrequency(0.015f + 2.1f *
        parameters_[PARAM_MOTION] * parameters_[PARAM_MOTION]);
  }

  void UpdateOscillators() {
    const float root_semitones = static_cast<float>(30 + pitch_step_ - 69);
    const float root_frequency = Clamp(440.0f * stmlib::SemitonesToRatio(
        root_semitones), 24.0f, 1800.0f);
    const float spread = static_cast<float>(spread_step_);
    body_[0].SetFrequency(root_frequency);
    body_[1].SetFrequency(root_frequency * 0.5015f);
    const float interval_a = 3.0f + 0.50f * spread;
    const float interval_b = 7.0f + spread;
    const float interval_c = 12.0f + 1.50f * spread;
    form_[0].SetFrequency(root_frequency * stmlib::SemitonesToRatio(interval_a));
    form_[1].SetFrequency(root_frequency * stmlib::SemitonesToRatio(interval_b));
    form_[2].SetFrequency(root_frequency * stmlib::SemitonesToRatio(interval_c));
    scar_oscillator_[0].SetFrequency(root_frequency * 1.498f);
    scar_oscillator_[1].SetFrequency(root_frequency *
        stmlib::SemitonesToRatio(interval_b + 0.37f));
    scar_oscillator_[2].SetFrequency(1200.0f +
        4200.0f * parameters_[PARAM_AIR]);
    oscillator_dirty_ = false;
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

  static void Put3(char* line, size_t offset, int32_t value) {
    value = ClampInt(value, 0, 999);
    if (offset < 21) line[offset] = static_cast<char>('0' + value / 100);
    if (offset + 1 < 21) line[offset + 1] =
        static_cast<char>('0' + (value / 10) % 10);
    if (offset + 2 < 21) line[offset + 2] =
        static_cast<char>('0' + value % 10);
  }

  char PatternCharacter(size_t quarter) const {
    uint8_t mask = 0;
    const size_t start = quarter * 4;
    for (size_t step = start; step < start + 4; ++step) {
      mask |= pattern_[step];
    }
    if (mask & GAP_VOID) return 'V';
    const uint8_t layers = static_cast<uint8_t>(mask & 7u);
    if (layers == 0) return '.';
    if (layers == GAP_BODY) return 'b';
    if (layers == GAP_FORM) return 'f';
    if (layers == GAP_AIR) return 'a';
    return '*';
  }

  void UpdateDisplay() {
    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);
    Put(line1_, 0, "LACUNA");
    Put(line1_, 8, recording_ ? "REC" : (playing_ ? "PLAY" : "LIVE"));
    Put(line2_, 0, "T");
    Put3(line2_, 1, tempo_);
    Put(line2_, 5, "STEP");
    Put2(line2_, 10, current_step_);
    Put(line2_, 13, field_present() ? "FIELD" : "VOID");
    for (size_t quarter = 0; quarter < 16; ++quarter) {
      line3_[quarter] = PatternCharacter(quarter);
    }
    Put(line4_, 0, "G");
    Put2(line4_, 1, static_cast<int32_t>(parameters_[PARAM_GAP] * 99.0f));
    Put(line4_, 4, "E");
    Put2(line4_, 5, static_cast<int32_t>(parameters_[PARAM_EDGE] * 99.0f));
    Put(line4_, 8, "D");
    Put2(line4_, 9, static_cast<int32_t>(parameters_[PARAM_DEPTH] * 99.0f));
    Put(line4_, 12, "M");
    Put2(line4_, 13, static_cast<int32_t>(parameters_[PARAM_MUTATION] * 99.0f));
    Put(line4_, 16, "S");
    Put2(line4_, 17, static_cast<int32_t>(parameters_[PARAM_SCAR] * 99.0f));
  }

  StatefulButton gap_button_[4];
  StatefulButton capture_button_;
  PhaseOscillator body_[2];
  PhaseOscillator form_[3];
  PhaseOscillator motion_lfo_;
  PhaseOscillator scar_oscillator_[kNumLayers];

  uint8_t pattern_[kPatternSteps];
  uint8_t mutation_pattern_[kPatternSteps];
  uint32_t gap_countdown_[4];
  float parameters_[PARAM_LAST];
  float layer_gain_[kNumLayers];
  float layer_target_[kNumLayers];
  float scar_envelope_[kNumLayers];
  bool gap_active_[kNumLayers];
  bool controls_initialized_;
  bool oscillator_dirty_;
  int32_t pitch_step_;
  int32_t spread_step_;
  int32_t tempo_;
  bool recording_;
  bool playing_;
  float loop_phase_;
  uint8_t current_step_;
  uint32_t loop_count_;
  uint32_t capture_hold_blocks_;
  uint8_t manual_gap_mask_;
  uint8_t playback_gap_mask_;
  uint8_t active_gap_mask_;
  uint32_t clear_count_;
  uint32_t random_state_;
  float body_filter_;
  float air_filter_;
  float air_slow_;
  uint8_t display_divider_;
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace lacuna

#endif  // LACUNA_DSP_H_
