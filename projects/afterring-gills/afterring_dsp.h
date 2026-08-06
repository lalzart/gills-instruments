// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Afterring: two independent generative rhythmic voices.

#ifndef AFTERRING_DSP_H_
#define AFTERRING_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "./afterring_voice.h"

namespace afterring {

using tidepit::Clamp01;
using tidepit::ClampInt;
using tidepit::Q27ToFloat;

class Instrument {
 public:
  void Init() {
    waveguide_.Init();
    main_oscillator_.Init();
    sympathetic_.Init();
    body_.Init();
    pulses_.Init();
    reverb_.Init();

    primary_phase_ = 0;
    secondary_phase_ = 0x80000000u;
    stage_ = 0;
    locked_ = false;
    secondary_held_ = false;
    previous_oscillator_ = previous_mutate_ = previous_lock_ = false;
    previous_hold_ = false;
    encoder_switch_stable_ = false;
    encoder_switch_candidate_ = false;
    encoder_switch_debounce_count_ = 0;
    encoder_hold_blocks_ = 0;
    encoder_long_action_ = false;
    scale_ = 0;
    follow_mode_ = 0;
    oscillator_mode_ = 0;
    last_root_ = 60;
    current_note_ = 60;
    previous_note_ = 60;
    pitch_dirty_ = true;
    random_state_ = 0x61667472;  // "aftr"
    for (size_t i = 0; i < 4; ++i) mutation_[i] = 0;
    secondary_rotation_ = 0;
    secondary_transpose_ = 0;
    secondary_tick_ = 0;
    sub_division_ = 2;
    energy_ = 0.0f;
    lpg_state_ = 0.0f;
    duck_envelope_ = 0.0f;
    oscillator_fade_ = 0.0f;
    strike_pending_ = true;
    rhythm_index_ = 0;
    movement_index_ = 0;
    display_divider_ = 0;
    SetMainPitch(60, 0.5f, true);
    UpdateDisplay(60, NULL, 0.5f);
  }

  void Process(const int32_t stage_controls[4],
               int32_t rate_control,
               int32_t memory_control,
               int32_t material_control,
               int32_t rhythm_control,
               int32_t movement_control,
               int32_t secondary_level_control,
               bool oscillator_button,
               bool mutate_button,
               bool lock_button,
               bool hold_button,
               int32_t encoder,
               bool encoder_switch,
               int32_t* left,
               int32_t* right) {
    HandleButtons(oscillator_button, mutate_button, lock_button, hold_button,
                  encoder_switch);

    const int32_t root = ClampInt(encoder, 36, 72);
    float stages[4];
    for (size_t i = 0; i < 4; ++i) {
      stages[i] = Q27ToFloat(stage_controls[i]);
    }
    const float memory = Q27ToFloat(memory_control);
    const float material = Q27ToFloat(material_control);
    const float rhythm = Q27ToFloat(rhythm_control);
    const float movement = Q27ToFloat(movement_control);
    const float secondary_level = Q27ToFloat(secondary_level_control);
    rhythm_index_ = ClampInt(static_cast<int32_t>(rhythm * 7.999f), 0, 7);
    movement_index_ = ClampInt(
        static_cast<int32_t>(movement * 7.999f), 0, 7);

    const float rate_value = Q27ToFloat(rate_control);
    const float rate_hz = 0.08f + 5.92f * rate_value * rate_value;
    const uint32_t phase_increment = static_cast<uint32_t>(
        rate_hz * (4294967296.0f * BUFSIZE / 48000.0f));

    bool stage_changed = false;
    bool cycle_wrapped = false;
    bool secondary_event = false;
    const uint32_t old_primary_phase = primary_phase_;
    primary_phase_ += phase_increment;
    cycle_wrapped = primary_phase_ < old_primary_phase;
    const uint8_t new_stage = primary_phase_ >> 30;
    stage_changed = new_stage != stage_;
    stage_ = new_stage;

    static const float rhythm_ratios[8] = {
      1.00f, 0.75f, 2.00f, 1.50f,
      2.00f, 1.25f, 2.50f, 3.00f
    };
    const float scaled_increment = static_cast<float>(phase_increment) *
        4.0f * rhythm_ratios[rhythm_index_];
    const uint32_t secondary_increment = static_cast<uint32_t>(
        std::min(4294967040.0f, scaled_increment));
    const uint32_t old_secondary_phase = secondary_phase_;
    secondary_phase_ += secondary_increment;
    secondary_event = secondary_phase_ < old_secondary_phase;

    if (cycle_wrapped) {
      strike_pending_ = true;
      if (!locked_ && RandomUnit() > memory * memory) Mutate();
    }

    if (pitch_dirty_ || root != last_root_ || stage_changed || cycle_wrapped) {
      SetMainPitch(root, stages[stage_], true);
    }

    const uint8_t next_stage = (stage_ + 1) & 3;
    const float t = static_cast<float>(primary_phase_ & 0x3fffffff) *
        (1.0f / 1073741824.0f);
    const float smooth_t = t * t * (3.0f - 2.0f * t);
    const float gesture = Clamp01(
        stages[stage_] + (stages[next_stage] - stages[stage_]) * smooth_t);

    if (secondary_event && TriggerSecondary(
        root, stages, material, secondary_level, gesture)) {
      duck_envelope_ = 1.0f;
    }

    const float animated_material = Clamp01(
        material + (gesture - 0.5f) * 0.22f);
    waveguide_.set_parameters(
        static_cast<int16_t>((1.0f - animated_material) * 22000.0f),
        static_cast<int16_t>((0.15f + 0.75f * animated_material) * 32767.0f));
    if (oscillator_mode_ == 2) {
      waveguide_.Render(waveguide_output_, BUFSIZE, strike_pending_);
    } else {
      main_oscillator_.Render(
          waveguide_output_, BUFSIZE, material, oscillator_mode_);
    }
    strike_pending_ = false;

    body_.SetMaterial(animated_material);
    sympathetic_.SetMaterial(animated_material);
    const float energy_target = 0.09f + 0.91f * gesture;
    const float cutoff = 0.014f + 0.095f * animated_material +
        0.105f * gesture;
    const float body_amount = 0.31f + 0.34f * (1.0f - material);
    const float secondary_gain = secondary_level * 2.0f;
    float main_source_gain;
    if (oscillator_mode_ == 0) {
      main_source_gain = 0.13f + 0.03f * material;
    } else if (oscillator_mode_ == 1) {
      main_source_gain = material < 0.5f
          ? 1.04f - 1.608f * material
          : 0.236f - 0.12f * (material - 0.5f);
    } else {
      main_source_gain = 0.82f;
    }
    for (size_t i = 0; i < BUFSIZE; ++i) {
      energy_ += 0.0025f * (energy_target - energy_);
      oscillator_fade_ += 0.0025f * (1.0f - oscillator_fade_);
      const float wave = waveguide_output_[i] * oscillator_fade_;
      const float sympathetic = sympathetic_.Process(wave);
      const float source = wave * main_source_gain + sympathetic *
          (oscillator_mode_ == 2 ? 0.30f : 0.18f);
      lpg_state_ += cutoff * (source - lpg_state_);
      float main_left;
      float main_right;
      body_.Process(lpg_state_ * energy_, body_amount,
                    &main_left, &main_right);

      float secondary_left;
      float secondary_right;
      pulses_.Process(&secondary_left, &secondary_right);
      secondary_frames_[i].l = secondary_left * secondary_gain;
      secondary_frames_[i].r = secondary_right * secondary_gain;
      const float main_gain = 1.20f * (1.0f - duck_envelope_ *
          (0.14f + 0.14f * secondary_level));
      frames_[i].l = main_left * main_gain +
          secondary_frames_[i].l * 0.12f;
      frames_[i].r = main_right * main_gain +
          secondary_frames_[i].r * 0.12f;
      duck_envelope_ *= 0.99935f;
    }

    reverb_.Process(
        frames_, BUFSIZE, 0.16f + 0.14f * material, animated_material);
    for (size_t i = 0; i < BUFSIZE; ++i) {
      const float l = stmlib::SoftClip(
          (frames_[i].l + secondary_frames_[i].l * 0.88f) * 0.68f);
      const float r = stmlib::SoftClip(
          (frames_[i].r + secondary_frames_[i].r * 0.88f) * 0.68f);
      left[i] = static_cast<int32_t>(l * 126000000.0f);
      right[i] = static_cast<int32_t>(r * 126000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay(root, stages, secondary_level);
    }
  }

  uint8_t stage() const { return stage_; }
  uint8_t active_secondary_voices() const {
    return pulses_.active_voices();
  }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  void HandleButtons(bool oscillator, bool mutate, bool lock, bool hold,
                     bool encoder_switch) {
    if (oscillator && !previous_oscillator_) {
      oscillator_mode_ = (oscillator_mode_ + 1) % 3;
      main_oscillator_.Reset();
      oscillator_fade_ = 0.0f;
      strike_pending_ = true;
    }
    if (mutate && !previous_mutate_) Mutate();
    if (lock && !previous_lock_) locked_ = !locked_;
    if (hold && !previous_hold_) secondary_held_ = !secondary_held_;
    previous_oscillator_ = oscillator;
    previous_mutate_ = mutate;
    previous_lock_ = lock;
    previous_hold_ = hold;
    HandleEncoderSwitch(encoder_switch);
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
        follow_mode_ = (follow_mode_ + 1) & 3;
        encoder_long_action_ = true;
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
    if (!secondary_held_) {
      secondary_rotation_ =
          (secondary_rotation_ + ((RandomWord() & 1) ? 1 : 15)) & 15;
      if ((RandomWord() & 3) == 0) {
        secondary_transpose_ += (RandomWord() & 1) ? 1 : -1;
        secondary_transpose_ = ClampInt(secondary_transpose_, -2, 2);
      }
    }
    pitch_dirty_ = true;
  }

  int32_t ScaleDegreeOffset(int32_t degree) const {
    static const int8_t scale_intervals[4][5] = {
      {0, 2, 4, 7, 9},
      {0, 2, 3, 7, 10},
      {0, 2, 3, 7, 9},
      {0, 3, 5, 7, 10}
    };
    int32_t octave = 0;
    while (degree < 0) {
      degree += 5;
      octave -= 12;
    }
    while (degree >= 5) {
      degree -= 5;
      octave += 12;
    }
    return octave + scale_intervals[scale_][degree];
  }

  int32_t QuantizeNote(int32_t root,
                       float height,
                       uint8_t stage,
                       bool apply_mutation) const {
    static const int8_t scales[4][6] = {
      {0, 2, 4, 7, 9, 12},
      {0, 2, 3, 5, 7, 10},
      {0, 2, 3, 5, 7, 9},
      {0, 3, 5, 7, 10, 12}
    };
    int32_t degree = static_cast<int32_t>(height * 5.99f);
    if (apply_mutation) degree += mutation_[stage];
    degree = ClampInt(degree, 0, 5);
    return ClampInt(root + scales[scale_][degree], 24, 96);
  }

  void SetMainPitch(int32_t root, float height, bool apply_mutation) {
    const int32_t note = QuantizeNote(root, height, stage_, apply_mutation);
    if (note != current_note_) {
      previous_note_ = current_note_;
      current_note_ = note;
    }
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

  bool TriggerSecondary(int32_t root,
                        const float stages[4],
                        float material,
                        float secondary_level,
                        float gesture) {
    static const uint16_t pattern_masks[8] = {
      0xffff, 0xeeee, 0xaaaa, 0xf7de,
      0xb6db, 0xef7b, 0xd6ad, 0xfbde
    };
    static const int8_t movement_patterns[7][8] = {
      {0, 0, 0, 0, 0, 0, 0, 0},
      {0, 2, 0, 3, 0, 2, 1, 3},
      {-1, 0, 1, 2, 1, 0, -1, 3},
      {0, 3, 1, 4, 2, 3, 1, 5},
      {-2, 0, 2, 4, 2, 0, -1, 3},
      {0, 1, 3, 2, 4, 1, 5, 2},
      {-3, 0, 4, -1, 2, 5, 1, -2}
    };

    const uint8_t pattern_position =
        (secondary_tick_ + secondary_rotation_) & 15;
    const bool pattern_on =
        (pattern_masks[rhythm_index_] & (1u << pattern_position)) != 0;
    ++secondary_tick_;
    if (!pattern_on || secondary_level < 0.005f) return false;

    int32_t base_note = current_note_;
    if (follow_mode_ == 1) {
      base_note = previous_note_;
    } else if (follow_mode_ == 2) {
      base_note = (secondary_tick_ & 1) ? current_note_ : previous_note_;
    } else if (follow_mode_ == 3) {
      const uint8_t next = (stage_ + 1) & 3;
      base_note = QuantizeNote(root, stages[next], next, true);
    }

    int32_t degree;
    if (movement_index_ < 7) {
      degree = movement_patterns[movement_index_][secondary_tick_ & 7];
    } else {
      degree = static_cast<int32_t>(RandomWord() % 9) - 4;
    }
    degree += secondary_transpose_;
    const int32_t note = ClampInt(
        base_note - 12 + ScaleDegreeOffset(degree), 24, 88);
    const float frequency =
        440.0f * stmlib::SemitonesToRatio(static_cast<float>(note - 69));
    const float velocity = Clamp01(0.62f + 0.34f * gesture);
    pulses_.Trigger(frequency, material, velocity, RandomWord());
    return true;
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
                     float secondary_level) {
    static const char* scale_names[4] = {"MAJ5", "MIN5", "DOR", "HARM"};
    static const char* follow_names[4] = {"CUR", "PRE", "ALT", "NXT"};
    static const char* oscillator_names[3] = {"RND ", "FOLD", "REED"};
    static const char* rhythm_names[8] = {
      "ECHO", "3/4 ", "SYNC", "3/2 ",
      "2X  ", "5/4 ", "5/2 ", "BURT"
    };
    static const char note_names[12][2] = {
      {'C',' '}, {'C','#'}, {'D',' '}, {'D','#'}, {'E',' '}, {'F',' '},
      {'F','#'}, {'G',' '}, {'G','#'}, {'A',' '}, {'A','#'}, {'B',' '}
    };
    ClearLine(line1_); ClearLine(line2_); ClearLine(line3_); ClearLine(line4_);
    Put(line1_, 0, "AFTERRING");
    Put(line1_, 10, oscillator_names[oscillator_mode_]);
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

    Put(line3_, 0, "RHY ");
    Put(line3_, 4, rhythm_names[rhythm_index_]);
    Put(line3_, 9, "MOV");
    line3_[13] = '0' + movement_index_;
    Put(line3_, 15, "LVL");
    line3_[19] = '0' + ClampInt(
        static_cast<int32_t>(secondary_level * 9.99f), 0, 9);

    Put(line4_, 0, scale_names[scale_]);
    Put(line4_, 6, follow_names[follow_mode_]);
    Put(line4_, 10, locked_ ? "LOCK" : "EVOLVE");
    if (secondary_held_) Put(line4_, 17, "HOLD");
  }

  tidepit::HighResolutionWaveguide waveguide_;
  MainOscillator main_oscillator_;
  tidepit::SympatheticString sympathetic_;
  tidepit::StereoBody body_;
  PulsePool pulses_;
  tidepit::DiffusionTail reverb_;

  uint32_t primary_phase_;
  uint32_t secondary_phase_;
  uint8_t stage_;
  bool locked_;
  bool secondary_held_;
  bool previous_oscillator_;
  bool previous_mutate_;
  bool previous_lock_;
  bool previous_hold_;
  bool encoder_switch_stable_;
  bool encoder_switch_candidate_;
  uint8_t encoder_switch_debounce_count_;
  uint16_t encoder_hold_blocks_;
  bool encoder_long_action_;
  uint8_t scale_;
  uint8_t follow_mode_;
  uint8_t oscillator_mode_;
  int32_t last_root_;
  int32_t current_note_;
  int32_t previous_note_;
  bool pitch_dirty_;
  int8_t mutation_[4];
  uint8_t secondary_rotation_;
  int8_t secondary_transpose_;
  uint8_t secondary_tick_;
  uint8_t sub_division_;
  uint32_t random_state_;
  float energy_;
  float lpg_state_;
  float duck_envelope_;
  float oscillator_fade_;
  bool strike_pending_;
  uint8_t rhythm_index_;
  uint8_t movement_index_;
  uint8_t display_divider_;

  float waveguide_output_[BUFSIZE];
  clouds::FloatFrame frames_[BUFSIZE];
  clouds::FloatFrame secondary_frames_[BUFSIZE];
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace afterring

#endif  // AFTERRING_DSP_H_
