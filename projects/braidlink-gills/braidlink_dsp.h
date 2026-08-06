// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Uses Mutable Instruments Braids, Streams, and Clouds DSP resources.

#ifndef BRAIDLINK_DSP_H_
#define BRAIDLINK_DSP_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "axoloti_memory.h"
#include "braids/resources.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/reverb.h"
#include "stmlib/dsp/units.h"
#include "streams/vactrol.h"
#include "stmlib/utils/dsp.h"

namespace braidlink {

static inline float Clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline float Clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
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

class MacroOscillator {
 public:
  enum Model {
    MODEL_MORPH,
    MODEL_FOLD,
    MODEL_FM,
    MODEL_HARMONICS,
    MODEL_LAST
  };

  void Init() {
    phase_ = 0;
    sub_phase_ = 0;
    modulator_phase_ = 0;
    phase_increment_ = 1;
    model_ = MODEL_MORPH;
  }

  void Reset() {
    phase_ = 0;
    sub_phase_ = 0;
    modulator_phase_ = 0;
  }

  void set_model(uint8_t model) {
    model_ = model < MODEL_LAST ? model : MODEL_MORPH;
  }

  void set_phase_increment(uint32_t increment) {
    phase_increment_ = increment == 0 ? 1 : increment;
  }

  void Render(float* output, size_t size, float timbre, float color) {
    timbre = Clamp01(timbre);
    color = Clamp01(color);
    switch (model_) {
      case MODEL_FOLD:
        RenderFold(output, size, timbre, color);
        break;
      case MODEL_FM:
        RenderFm(output, size, timbre, color);
        break;
      case MODEL_HARMONICS:
        RenderHarmonics(output, size, timbre, color);
        break;
      default:
        RenderMorph(output, size, timbre, color);
        break;
    }
  }

 private:
  static float Sine(uint32_t phase) {
    return static_cast<float>(stmlib::Interpolate824(braids::wav_sine, phase)) *
        (1.0f / 32768.0f);
  }

  void RenderMorph(float* output, size_t size, float timbre, float color) {
    const float shape = timbre * 2.0f;
    const float first_mix = std::min(1.0f, shape);
    const float second_mix = std::max(0.0f, shape - 1.0f);
    const float sub_mix = color * 0.42f;
    for (size_t i = 0; i < size; ++i) {
      phase_ += phase_increment_;
      sub_phase_ += phase_increment_ >> 1;
      const float sine = Sine(phase_);
      const uint32_t phase16 = phase_ >> 16;
      const float triangle = phase16 < 32768
          ? -1.0f + static_cast<float>(phase16) * (1.0f / 16384.0f)
          : 3.0f - static_cast<float>(phase16) * (1.0f / 16384.0f);
      const float saw = static_cast<float>(static_cast<int32_t>(phase16) -
          32768) * (1.0f / 32768.0f);
      const float sine_triangle = sine + (triangle - sine) * first_mix;
      const float main = sine_triangle + (saw - triangle) * second_mix;
      output[i] = main * (1.0f - sub_mix) + Sine(sub_phase_) * sub_mix;
    }
  }

  void RenderFold(float* output, size_t size, float timbre, float color) {
    const int32_t fold_parameter = static_cast<int32_t>(timbre * 32767.0f);
    const int32_t fold_gain = 2048 + ((fold_parameter * 30720) >> 15);
    const float sub_mix = color * 0.36f;
    for (size_t i = 0; i < size; ++i) {
      phase_ += phase_increment_;
      sub_phase_ += phase_increment_ >> 1;
      const int16_t sine = stmlib::Interpolate824(braids::wav_sine, phase_);
      const int32_t driven = (static_cast<int32_t>(sine) * fold_gain) >> 15;
      const int16_t folded = stmlib::Interpolate88(
          braids::ws_sine_fold, static_cast<uint16_t>(driven + 32768));
      const float main = static_cast<float>(folded) * (1.0f / 32768.0f);
      output[i] = main * (1.0f - sub_mix) + Sine(sub_phase_) * sub_mix;
    }
  }

  void RenderFm(float* output, size_t size, float timbre, float color) {
    const float ratio = 0.5f + timbre * 3.5f;
    const uint32_t modulator_increment = static_cast<uint32_t>(
        static_cast<float>(phase_increment_) * ratio);
    const int32_t index = static_cast<int32_t>(color * 32767.0f);
    for (size_t i = 0; i < size; ++i) {
      phase_ += phase_increment_;
      modulator_phase_ += modulator_increment;
      const int32_t modulator = stmlib::Interpolate824(
          braids::wav_sine, modulator_phase_);
      const uint32_t phase_modulation = static_cast<uint32_t>(
          static_cast<int64_t>(modulator) * index * 4);
      output[i] = Sine(phase_ + phase_modulation);
    }
  }

  void RenderHarmonics(float* output, size_t size, float timbre, float color) {
    float amplitude[5];
    float total = 0.0f;
    for (size_t partial = 0; partial < 5; ++partial) {
      const float order = static_cast<float>(partial + 1);
      const float tilt = 1.0f / (1.0f + order * (0.25f + 1.75f * timbre));
      const float parity = (partial & 1)
          ? 0.25f + 1.25f * color
          : 1.35f - 0.60f * color;
      amplitude[partial] = tilt * parity;
      total += amplitude[partial];
    }
    const float normalization = total > 0.0f ? 0.92f / total : 0.0f;
    for (size_t partial = 0; partial < 5; ++partial) {
      amplitude[partial] *= normalization;
    }
    for (size_t i = 0; i < size; ++i) {
      phase_ += phase_increment_;
      float sample = 0.0f;
      for (size_t partial = 0; partial < 5; ++partial) {
        sample += Sine(phase_ * static_cast<uint32_t>(partial + 1)) *
            amplitude[partial];
      }
      output[i] = sample;
    }
  }

  uint32_t phase_;
  uint32_t sub_phase_;
  uint32_t modulator_phase_;
  uint32_t phase_increment_;
  uint8_t model_;
};

class ModalBody {
 public:
  enum Mode {
    MODE_CLEAN,
    MODE_WOOD,
    MODE_METAL,
    MODE_LAST
  };

  void Init() {
    mode_ = MODE_WOOD;
    frequency_ = 261.6256f;
    damping_ = 0.5f;
    damping_step_ = 32;
    for (size_t i = 0; i < kNumModes; ++i) {
      coefficient_[i] = 0.0f;
      radius_squared_[i] = 0.0f;
      drive_[i] = 0.0f;
      y1_[i] = 0.0f;
      y2_[i] = 0.0f;
    }
    Configure();
  }

  void Reset() {
    for (size_t i = 0; i < kNumModes; ++i) {
      y1_[i] = 0.0f;
      y2_[i] = 0.0f;
    }
  }

  void set_mode(uint8_t mode) {
    const uint8_t next = mode < MODE_LAST ? mode : MODE_CLEAN;
    if (next != mode_) {
      mode_ = next;
      Reset();
      Configure();
    }
  }

  void SetFrequency(float frequency) {
    frequency_ = Clamp(frequency, 20.0f, 6000.0f);
    Configure();
  }

  void SetDamping(float damping) {
    const int32_t step = ClampInt(static_cast<int32_t>(Clamp01(damping) *
        63.0f), 0, 63);
    if (step != damping_step_) {
      damping_step_ = step;
      damping_ = static_cast<float>(step) * (1.0f / 63.0f);
      Configure();
    }
  }

  void Process(float input, float amount, float* left, float* right) {
    amount = Clamp01(amount);
    if (mode_ == MODE_CLEAN || amount < 0.001f) {
      *left = input;
      *right = input;
      return;
    }
    static const float pan_l[kNumModes] = {0.90f, 0.66f, 0.34f, 0.12f};
    static const float pan_r[kNumModes] = {0.12f, 0.34f, 0.66f, 0.90f};
    float body_l = 0.0f;
    float body_r = 0.0f;
    for (size_t i = 0; i < kNumModes; ++i) {
      const float value = drive_[i] * input + coefficient_[i] * y1_[i] -
          radius_squared_[i] * y2_[i];
      y2_[i] = y1_[i];
      y1_[i] = value;
      body_l += value * pan_l[i];
      body_r += value * pan_r[i];
    }
    const float dry = 1.0f - 0.48f * amount;
    *left = input * dry + body_l * amount * 0.82f;
    *right = input * dry + body_r * amount * 0.82f;
  }

 private:
  static const size_t kNumModes = 4;

  void Configure() {
    static const float wood_ratio[kNumModes] = {1.0f, 2.61f, 4.18f, 6.46f};
    static const float metal_ratio[kNumModes] = {1.0f, 2.99f, 4.08f, 6.12f};
    const float* ratio = mode_ == MODE_METAL ? metal_ratio : wood_ratio;
    const float base_radius = mode_ == MODE_METAL
        ? 0.9910f + 0.0082f * damping_
        : 0.9820f + 0.0160f * damping_;
    for (size_t i = 0; i < kNumModes; ++i) {
      const float frequency = std::min(18000.0f, frequency_ * ratio[i]);
      const float radius = Clamp(base_radius -
          static_cast<float>(i) * (0.0007f + 0.0018f * (1.0f - damping_)),
          0.94f, 0.9994f);
      coefficient_[i] = 2.0f * radius * cosf(
          6.28318530718f * frequency * (1.0f / 48000.0f));
      radius_squared_[i] = radius * radius;
      drive_[i] = (1.0f - radius) * (i == 0 ? 1.0f : 0.72f);
    }
  }

  uint8_t mode_;
  float frequency_;
  float damping_;
  int32_t damping_step_;
  float coefficient_[kNumModes];
  float radius_squared_[kNumModes];
  float drive_[kNumModes];
  float y1_[kNumModes];
  float y2_[kNumModes];
};

class StereoReverb {
 public:
  void Init() {
    memset(&reverb_, 0, sizeof(reverb_));
    memory_ = static_cast<uint16_t*>(sdram_malloc(
        sizeof(uint16_t) * kReverbWords));
    available_ = memory_ != NULL;
    if (available_) {
      memset(memory_, 0, sizeof(uint16_t) * kReverbWords);
      InitReverb(&reverb_, memory_, 0);
    }
    wet_ = 0.0f;
    drain_blocks_ = 0;
  }

  bool available() const { return available_; }

  void Process(float* left,
               float* right,
               size_t size,
               float amount,
               bool enabled) {
    if (!available_) return;
    const float target = enabled ? Clamp01(amount) : 0.0f;
    wet_ += 0.08f * (target - wet_);
    if (target > 0.001f) {
      drain_blocks_ = 3000;
    } else if (drain_blocks_ > 0) {
      --drain_blocks_;
    }
    if (wet_ < 0.0001f && drain_blocks_ == 0) return;

    for (size_t i = 0; i < size; ++i) {
      frame_[i].l = left[i];
      frame_[i].r = right[i];
    }
    reverb_.set_amount(0.42f * wet_);
    reverb_.set_input_gain(target > 0.001f ? 0.34f : 0.0f);
    reverb_.set_time(0.55f + 0.38f * wet_);
    reverb_.set_diffusion(0.58f + 0.22f * wet_);
    reverb_.set_lp(0.62f + 0.25f * (1.0f - wet_));
    reverb_.Process(frame_, size);
    for (size_t i = 0; i < size; ++i) {
      left[i] = frame_[i].l;
      right[i] = frame_[i].r;
    }
  }

 private:
  static const size_t kReverbWords = 16384;

  template <typename ReverbType>
  static auto InitReverb(ReverbType* reverb, uint16_t* memory, int)
      -> decltype(reverb->Init(memory, 48000.0f), void()) {
    reverb->Init(memory, 48000.0f);
  }

  template <typename ReverbType>
  static void InitReverb(ReverbType* reverb, uint16_t* memory, long) {
    reverb->Init(memory);
  }

  clouds::Reverb reverb_;
  uint16_t* memory_;
  bool available_;
  float wet_;
  uint16_t drain_blocks_;
  clouds::FloatFrame frame_[BUFSIZE];
};

class Instrument {
 public:
  enum MidiParameter {
    PARAM_TIMBRE,
    PARAM_COLOR,
    PARAM_ATTACK,
    PARAM_DECAY,
    PARAM_BRIGHTNESS,
    PARAM_BODY,
    PARAM_DAMPING,
    PARAM_DRIVE,
    PARAM_SPACE,
    PARAM_LEVEL,
    PARAM_LAST
  };

  void Init() {
    oscillator_.Init();
    memset(&vactrol_, 0, sizeof(vactrol_));
    vactrol_.Init();
    body_.Init();
    reverb_.Init();
    model_button_.Init();
    character_button_.Init();
    midi_mode_button_.Init();
    reverb_button_.Init();
    articulation_button_.Init();

    midi_channel_ = 1;
    midi_note_ = 60;
    midi_velocity_ = 100;
    midi_gate_ = false;
    midi_trigger_counter_ = 0;
    midi_pitch_bend_ = 8192;
    midi_cc_seen_ = 0;
    for (size_t i = 0; i < PARAM_LAST; ++i) {
      midi_cc_[i] = 64;
      parameter_[i] = 0.5f;
    }

    last_trigger_counter_ = 0;
    last_note_ = 60;
    last_pitch_bend_ = 8192;
    gate_ = false;
    velocity_ = 100.0f / 127.0f;
    phase_increment_ = 1;
    note_frequency_ = 261.6256f;
    oscillator_model_ = MacroOscillator::MODEL_MORPH;
    body_mode_ = ModalBody::MODE_WOOD;
    midi_offset_mode_ = true;
    pluck_mode_ = true;
    reverb_enabled_ = false;
    filter_state_ = 0.0f;
    envelope_ = 0.0f;
    model_fade_ = 1.0f;
    display_divider_ = 0;
    last_vactrol_shape_ = -1;
    last_vactrol_tone_ = -1;
    last_vactrol_pluck_ = false;
    UpdatePitch();
    UpdateDisplay();
  }

  void HandleMidi(uint8_t status, uint8_t data1, uint8_t data2) {
    const uint8_t message = status & 0xf0;
    if (message < 0x80 || message >= 0xf0) return;
    if (static_cast<uint8_t>((status & 0x0f) + 1) != midi_channel_) return;

    if (message == 0x90 && data2 != 0) {
      midi_note_ = data1;
      midi_velocity_ = data2;
      midi_gate_ = true;
      ++midi_trigger_counter_;
    } else if (message == 0x80 || (message == 0x90 && data2 == 0)) {
      if (data1 == midi_note_) midi_gate_ = false;
    } else if (message == 0xb0) {
      if (data1 >= 20 && data1 < 20 + PARAM_LAST) {
        const uint8_t index = data1 - 20;
        midi_cc_[index] = data2;
        midi_cc_seen_ |= static_cast<uint16_t>(1u << index);
      } else if (data1 == 120 || data1 == 123) {
        midi_gate_ = false;
      }
    } else if (message == 0xe0) {
      midi_pitch_bend_ = static_cast<uint16_t>(data1 |
          (static_cast<uint16_t>(data2) << 7));
    }
  }

  void Process(const int32_t controls[PARAM_LAST],
               bool model_button,
               bool character_button,
               bool midi_mode_button,
               bool reverb_button,
               int32_t channel,
               bool articulation_button,
               int32_t* left,
               int32_t* right) {
    HandleControls(model_button, character_button, midi_mode_button,
                   reverb_button, articulation_button);

    const uint8_t next_channel = static_cast<uint8_t>(
        ClampInt(channel, 1, 16));
    if (next_channel != midi_channel_) {
      midi_channel_ = next_channel;
      midi_gate_ = false;
      gate_ = false;
    }

    const uint16_t seen = midi_cc_seen_;
    for (size_t i = 0; i < PARAM_LAST; ++i) {
      const float base = Q27ToFloat(controls[i]);
      float target = base;
      if (seen & (1u << i)) {
        const float cc = static_cast<float>(midi_cc_[i]);
        target = midi_offset_mode_
            ? base + (cc - 64.0f) * (0.5f / 63.0f)
            : cc * (1.0f / 127.0f);
      }
      target = Clamp01(target);
      const float smoothing = i == PARAM_TIMBRE || i == PARAM_COLOR ||
          i == PARAM_BRIGHTNESS || i == PARAM_BODY || i == PARAM_DRIVE ||
          i == PARAM_SPACE ? 0.28f : 0.50f;
      parameter_[i] += smoothing * (target - parameter_[i]);
    }

    const uint8_t trigger_counter = midi_trigger_counter_;
    const bool triggered = trigger_counter != last_trigger_counter_;
    if (triggered) {
      last_trigger_counter_ = trigger_counter;
      velocity_ = 0.08f + 0.92f * static_cast<float>(midi_velocity_) *
          (1.0f / 127.0f);
      if (pluck_mode_) {
        envelope_ = velocity_;
      } else {
        envelope_ *= 0.12f;
      }
    }
    gate_ = midi_gate_;

    const uint16_t pitch_bend = midi_pitch_bend_;
    if (triggered || last_note_ != midi_note_ ||
        pitch_bend != last_pitch_bend_) {
      last_note_ = midi_note_;
      last_pitch_bend_ = pitch_bend;
      UpdatePitch();
    }

    ConfigureVactrol();
    body_.SetDamping(parameter_[PARAM_DAMPING]);
    oscillator_.Render(oscillator_output_, BUFSIZE,
                       parameter_[PARAM_TIMBRE], parameter_[PARAM_COLOR]);

    const float attack = parameter_[PARAM_ATTACK];
    const float decay = parameter_[PARAM_DECAY];
    const float attack_time = 0.0005f + attack * attack * 0.95f;
    const float decay_time = 0.012f + decay * decay * decay * 3.988f;
    const float attack_coefficient = std::min(
        1.0f, 1.0f / (attack_time * 48000.0f));
    const float decay_coefficient = std::min(
        1.0f, 1.0f / (decay_time * 48000.0f));
    const float drive_gain = 1.0f + parameter_[PARAM_DRIVE] * 4.5f;

    for (size_t i = 0; i < BUFSIZE; ++i) {
      if (pluck_mode_) {
        envelope_ += (0.0f - envelope_) * decay_coefficient;
      } else {
        const float target = gate_ ? velocity_ : 0.0f;
        const float coefficient = target > envelope_
            ? attack_coefficient : decay_coefficient;
        envelope_ += (target - envelope_) * coefficient;
      }

      const int16_t oscillator_sample = static_cast<int16_t>(Clamp(
          oscillator_output_[i] * 30000.0f, -32767.0f, 32767.0f));
      const int16_t excite = pluck_mode_
          ? ((triggered && i == 0) ? 32767 : 0)
          : (gate_ ? 32767 : 0);
      uint16_t vactrol_gain = 0;
      uint16_t vactrol_frequency = 0;
      vactrol_.Process(oscillator_sample, excite,
                       &vactrol_gain, &vactrol_frequency);
      const float cutoff = 0.0015f + 0.46f *
          static_cast<float>(vactrol_frequency) *
          static_cast<float>(vactrol_frequency) *
          (1.0f / 4294836225.0f);
      const float source = static_cast<float>(oscillator_sample) *
          (1.0f / 32768.0f);
      filter_state_ += cutoff * (source - filter_state_);
      const float gain = static_cast<float>(vactrol_gain) *
          (1.0f / 32768.0f);
      model_fade_ += 0.0045f * (1.0f - model_fade_);
      const float articulated = filter_state_ * gain * envelope_ * model_fade_;

      float sample_l;
      float sample_r;
      body_.Process(articulated, parameter_[PARAM_BODY],
                    &sample_l, &sample_r);
      processed_left_[i] = SoftDrive(sample_l, drive_gain);
      processed_right_[i] = SoftDrive(sample_r, drive_gain);
    }

    reverb_.Process(processed_left_, processed_right_, BUFSIZE,
                    parameter_[PARAM_SPACE], reverb_enabled_);
    const float output_level = 0.08f + 0.92f * parameter_[PARAM_LEVEL];
    for (size_t i = 0; i < BUFSIZE; ++i) {
      const float sample_l = Clamp(processed_left_[i] * output_level,
                                   -1.0f, 1.0f);
      const float sample_r = Clamp(processed_right_[i] * output_level,
                                   -1.0f, 1.0f);
      left[i] = static_cast<int32_t>(sample_l * 120000000.0f);
      right[i] = static_cast<int32_t>(sample_r * 120000000.0f);
    }

    if (++display_divider_ >= 64) {
      display_divider_ = 0;
      UpdateDisplay();
    }
  }

  bool gate() const { return gate_; }
  uint8_t oscillator_model() const { return oscillator_model_; }
  uint8_t body_mode() const { return body_mode_; }
  bool midi_offset_mode() const { return midi_offset_mode_; }
  bool pluck_mode() const { return pluck_mode_; }
  bool reverb_enabled() const { return reverb_enabled_; }
  bool reverb_available() const { return reverb_.available(); }
  char* line1() { return line1_; }
  char* line2() { return line2_; }
  char* line3() { return line3_; }
  char* line4() { return line4_; }

 private:
  static float SoftDrive(float input, float gain) {
    float value = Clamp(input * gain, -1.0f, 1.0f);
    value = value - value * value * value * (1.0f / 3.0f);
    return value * 1.5f;
  }

  void HandleControls(bool model,
                      bool character,
                      bool midi_mode,
                      bool reverb,
                      bool articulation) {
    if (model_button_.Update(model)) {
      oscillator_model_ = (oscillator_model_ + 1) %
          MacroOscillator::MODEL_LAST;
      oscillator_.set_model(oscillator_model_);
      oscillator_.Reset();
      model_fade_ = 0.0f;
    }
    if (character_button_.Update(character)) {
      body_mode_ = (body_mode_ + 1) % ModalBody::MODE_LAST;
      body_.set_mode(body_mode_);
    }
    if (midi_mode_button_.Update(midi_mode)) {
      midi_offset_mode_ = !midi_offset_mode_;
    }
    if (reverb_button_.Update(reverb)) {
      reverb_enabled_ = !reverb_enabled_;
    }
    if (articulation_button_.Update(articulation)) {
      pluck_mode_ = !pluck_mode_;
      last_vactrol_shape_ = -1;
      if (!pluck_mode_) envelope_ = 0.0f;
    }
  }

  void ConfigureVactrol() {
    const int32_t shape = ClampInt(static_cast<int32_t>(
        (0.30f * parameter_[PARAM_ATTACK] +
         0.70f * parameter_[PARAM_DECAY]) * 65535.0f), 0, 65535);
    const int32_t tone = ClampInt(static_cast<int32_t>(
        parameter_[PARAM_BRIGHTNESS] * 65535.0f), 0, 65535);
    if (shape == last_vactrol_shape_ && tone == last_vactrol_tone_ &&
        pluck_mode_ == last_vactrol_pluck_) {
      return;
    }
    int32_t parameters[2] = {shape, tone};
    vactrol_.Configure(pluck_mode_, parameters, NULL);
    last_vactrol_shape_ = shape;
    last_vactrol_tone_ = tone;
    last_vactrol_pluck_ = pluck_mode_;
  }

  void UpdatePitch() {
    const int32_t bend = static_cast<int32_t>(midi_pitch_bend_) - 8192;
    const float semitones = static_cast<float>(midi_note_) - 69.0f +
        static_cast<float>(bend) * (2.0f / 8192.0f);
    note_frequency_ = 440.0f * stmlib::SemitonesToRatio(semitones);
    phase_increment_ = static_cast<uint32_t>(note_frequency_ *
        (4294967296.0f / 48000.0f));
    oscillator_.set_phase_increment(phase_increment_);
    body_.SetFrequency(note_frequency_);
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
    if (offset < 21) line[offset] = '0' + value / 10;
    if (offset + 1 < 21) line[offset + 1] = '0' + value % 10;
  }

  void UpdateDisplay() {
    static const char* model_name[MacroOscillator::MODEL_LAST] = {
      "MORP", "FOLD", "FM  ", "HARM"
    };
    static const char* body_name[ModalBody::MODE_LAST] = {
      "CLN ", "WOOD", "METL"
    };
    ClearLine(line1_);
    ClearLine(line2_);
    ClearLine(line3_);
    ClearLine(line4_);

    Put(line1_, 0, "BRAIDLINK CH");
    Put2(line1_, 12, midi_channel_);
    Put(line1_, 15, "N");
    Put2(line1_, 16, midi_note_);
    Put(line1_, 19, gate_ ? "ON" : "--");

    Put(line2_, 0, model_name[oscillator_model_]);
    Put(line2_, 5, body_name[body_mode_]);
    Put(line2_, 10, pluck_mode_ ? "PLK" : "GAT");
    Put(line2_, 14, midi_offset_mode_ ? "OFFS" : "ABS ");

    Put(line3_, 0, "T");
    Put2(line3_, 1, static_cast<int32_t>(parameter_[PARAM_TIMBRE] * 99.0f));
    Put(line3_, 4, "C");
    Put2(line3_, 5, static_cast<int32_t>(parameter_[PARAM_COLOR] * 99.0f));
    Put(line3_, 8, "D");
    Put2(line3_, 9, static_cast<int32_t>(parameter_[PARAM_DECAY] * 99.0f));
    Put(line3_, 12, "B");
    Put2(line3_, 13, static_cast<int32_t>(parameter_[PARAM_BODY] * 99.0f));

    Put(line4_, 0, "CC20-29");
    Put(line4_, 9, reverb_enabled_ ? "RVB ON" : "RVB --");
    if (!reverb_.available()) Put(line4_, 9, "NO RVB");
  }

  MacroOscillator oscillator_;
  streams::Vactrol vactrol_;
  ModalBody body_;
  StereoReverb reverb_;
  DebouncedButton model_button_;
  DebouncedButton character_button_;
  DebouncedButton midi_mode_button_;
  DebouncedButton reverb_button_;
  DebouncedButton articulation_button_;

  volatile uint8_t midi_channel_;
  volatile uint8_t midi_note_;
  volatile uint8_t midi_velocity_;
  volatile bool midi_gate_;
  volatile uint8_t midi_trigger_counter_;
  volatile uint16_t midi_pitch_bend_;
  volatile uint8_t midi_cc_[PARAM_LAST];
  volatile uint16_t midi_cc_seen_;

  uint8_t last_trigger_counter_;
  uint8_t last_note_;
  uint16_t last_pitch_bend_;
  bool gate_;
  float velocity_;
  uint32_t phase_increment_;
  float note_frequency_;
  uint8_t oscillator_model_;
  uint8_t body_mode_;
  bool midi_offset_mode_;
  bool pluck_mode_;
  bool reverb_enabled_;
  float filter_state_;
  float envelope_;
  float model_fade_;
  float parameter_[PARAM_LAST];
  int32_t last_vactrol_shape_;
  int32_t last_vactrol_tone_;
  bool last_vactrol_pluck_;
  uint8_t display_divider_;

  float oscillator_output_[BUFSIZE];
  float processed_left_[BUFSIZE];
  float processed_right_[BUFSIZE];
  char line1_[22];
  char line2_[22];
  char line3_[22];
  char line4_[22];
};

}  // namespace braidlink

#endif  // BRAIDLINK_DSP_H_
