// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Tuned pulse/body voice for Afterring's independent rhythmic layer.

#ifndef AFTERRING_VOICE_H_
#define AFTERRING_VOICE_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "braids/resources.h"
#include "stmlib/utils/dsp.h"

// Afterring shares Tide Pit's feedback body and diffusion tail without
// modifying the Tide Pit project.
#include "../tide-pit-gills/tidepit_voice.h"

namespace afterring {

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
    timbre = tidepit::Clamp01(timbre);
    const int32_t fold_parameter = static_cast<int32_t>(timbre * 32767.0f);
    const int32_t fold_gain =
        2048 + ((fold_parameter * 30720) >> 15);
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

class PulseVoice {
 public:
  void Init() {
    phase_ = 0;
    phase_increment_ = 0.0f;
    target_increment_ = 0.0f;
    amplitude_ = 0.0f;
    amplitude_decay_ = 0.9997f;
    impact_envelope_ = 0.0f;
    brightness_envelope_ = 0.0f;
    noise_state_ = 1;
    noise_lowpass_ = 0.0f;
    body_lowpass_ = 0.0f;
    cutoff_ = 0.15f;
    harmonic_ = 0.15f;
    pan_left_ = pan_right_ = 0.707f;
    active_ = false;
  }

  void Trigger(float frequency,
               float tone,
               float velocity,
               float pan,
               uint32_t seed) {
    tone = tidepit::Clamp01(tone);
    velocity = tidepit::Clamp01(velocity);
    frequency = std::max(28.0f, std::min(2200.0f, frequency));

    target_increment_ = frequency * 89478.485333f;
    phase_increment_ = target_increment_ * (1.10f + 0.16f * (1.0f - tone));
    phase_ = 0x40000000u;
    amplitude_ = 0.42f + 0.58f * velocity;
    amplitude_decay_ = 0.99955f + 0.00030f * tone;
    impact_envelope_ = 0.38f + 0.42f * velocity;
    brightness_envelope_ = 1.0f;
    noise_state_ = seed ? seed : 1;
    noise_lowpass_ = 0.0f;
    body_lowpass_ = 0.0f;
    cutoff_ = 0.065f + 0.235f * tone;
    harmonic_ = 0.26f + 0.30f * tone;

    pan = std::max(-1.0f, std::min(1.0f, pan));
    pan_left_ = 0.72f - 0.28f * pan;
    pan_right_ = 0.72f + 0.28f * pan;
    active_ = true;
  }

  void Process(float* left, float* right) {
    if (!active_) {
      *left = 0.0f;
      *right = 0.0f;
      return;
    }

    phase_increment_ += 0.0015f * (target_increment_ - phase_increment_);
    phase_ += static_cast<uint32_t>(phase_increment_);
    const float fundamental = Sine(phase_);
    const float second = Sine(phase_ << 1);
    const float oscillator = fundamental + second *
        (0.04f + harmonic_ * brightness_envelope_);
    body_lowpass_ += cutoff_ * (oscillator - body_lowpass_);

    noise_state_ ^= noise_state_ << 13;
    noise_state_ ^= noise_state_ >> 17;
    noise_state_ ^= noise_state_ << 5;
    const float noise = static_cast<float>(
        static_cast<int16_t>(noise_state_ >> 16)) * (1.0f / 32768.0f);
    noise_lowpass_ += 0.10f * (noise - noise_lowpass_);

    const float body = body_lowpass_ * amplitude_;
    const float impact = noise_lowpass_ * impact_envelope_ * 0.34f;
    const float output = stmlib::SoftLimit((body + impact) * 1.12f);
    *left = output * pan_left_;
    *right = output * pan_right_;

    amplitude_ *= amplitude_decay_;
    impact_envelope_ *= 0.972f;
    brightness_envelope_ *= 0.9983f;
    if (amplitude_ < 0.00005f && impact_envelope_ < 0.00005f) {
      active_ = false;
    }
  }

  bool active() const { return active_; }

 private:
  static float Sine(uint32_t phase) {
    const uint32_t index = phase >> 24;
    const float fraction = static_cast<float>((phase >> 8) & 0xffff) *
        (1.0f / 65536.0f);
    const float a = static_cast<float>(braids::wav_sine[index]);
    const float b = static_cast<float>(braids::wav_sine[index + 1]);
    return (a + (b - a) * fraction) * (1.0f / 32768.0f);
  }

  uint32_t phase_;
  float phase_increment_;
  float target_increment_;
  float amplitude_;
  float amplitude_decay_;
  float impact_envelope_;
  float brightness_envelope_;
  uint32_t noise_state_;
  float noise_lowpass_;
  float body_lowpass_;
  float cutoff_;
  float harmonic_;
  float pan_left_;
  float pan_right_;
  bool active_;
};

class PulsePool {
 public:
  void Init() {
    for (size_t i = 0; i < kNumVoices; ++i) voices_[i].Init();
    next_voice_ = 0;
  }

  void Trigger(float frequency,
               float tone,
               float velocity,
               uint32_t seed) {
    const float pan = static_cast<float>(next_voice_) *
        (2.0f / static_cast<float>(kNumVoices - 1)) - 1.0f;
    voices_[next_voice_].Trigger(frequency, tone, velocity, pan, seed);
    next_voice_ = (next_voice_ + 1) % kNumVoices;
  }

  void Process(float* left, float* right) {
    float sum_left = 0.0f;
    float sum_right = 0.0f;
    for (size_t i = 0; i < kNumVoices; ++i) {
      float voice_left;
      float voice_right;
      voices_[i].Process(&voice_left, &voice_right);
      sum_left += voice_left;
      sum_right += voice_right;
    }
    *left = sum_left * 0.52f;
    *right = sum_right * 0.52f;
  }

  uint8_t active_voices() const {
    uint8_t active = 0;
    for (size_t i = 0; i < kNumVoices; ++i) {
      if (voices_[i].active()) ++active;
    }
    return active;
  }

 private:
  static const size_t kNumVoices = 4;
  PulseVoice voices_[kNumVoices];
  uint8_t next_voice_;
};

}  // namespace afterring

#endif  // AFTERRING_VOICE_H_
