// Copyright 2026 Lance Ship. MIT licensed; see LICENSE.md.
// Uses DSP tables and effects derived from Mutable Instruments Braids and
// Clouds by Emilie Gillet.

#ifndef TIDEPIT_VOICE_H_
#define TIDEPIT_VOICE_H_

#include <algorithm>
#include <cmath>
#include <cstring>

#include "braids/resources.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/reverb.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace tidepit {

static inline float Clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline float ClampBipolar(float value) {
  return value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
}

static inline int32_t ClampInt(int32_t value, int32_t low, int32_t high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float Q27ToFloat(int32_t value) {
  return Clamp01(static_cast<float>(value) * (1.0f / 134217728.0f));
}

// The Ksoloti Braids derivative stores the Fluted model's two feedback delay
// lines in signed 8-bit samples. Tide Pit keeps the same waveguide topology and
// scaling but retains eight additional bits inside the loop. This removes much
// of the stepped, papery decay without changing the model's basic personality.
class HighResolutionWaveguide {
 public:
  void Init() {
    bore_ = static_cast<int16_t*>(
        sdram_malloc(sizeof(int16_t) * kBoreLength));
    jet_ = static_cast<int16_t*>(
        sdram_malloc(sizeof(int16_t) * kJetLength));
    available_ = bore_ != NULL && jet_ != NULL;
    if (available_) {
      memset(bore_, 0, sizeof(int16_t) * kBoreLength);
      memset(jet_, 0, sizeof(int16_t) * kJetLength);
    }
    pitch_ = 60 * 128;
    parameter_[0] = 12000;
    parameter_[1] = 14000;
    delay_ptr_ = 0;
    excitation_ptr_ = 0;
    lp_state_ = 0;
    dc_x_ = 0;
    dc_y_ = 0;
    noise_state_ = 0.0f;
  }

  void set_pitch(int16_t pitch) { pitch_ = pitch; }

  void set_parameters(int16_t timbre, int16_t color) {
    parameter_[0] = timbre;
    parameter_[1] = color;
  }

  bool available() const { return available_; }

  void Render(float* output, size_t size, bool strike) {
    if (!available_) {
      std::fill(output, output + size, 0.0f);
      return;
    }

    uint16_t delay_ptr = delay_ptr_;
    uint16_t excitation_ptr = excitation_ptr_;
    int32_t lp_state = lp_state_;
    int32_t dc_x = dc_x_;
    int32_t dc_y = dc_y_;
    float noise_state = noise_state_;

    if (strike) {
      excitation_ptr = 0;
      memset(bore_, 0, sizeof(int16_t) * kBoreLength);
      memset(jet_, 0, sizeof(int16_t) * kJetLength);
      lp_state = 0;
      dc_x = 0;
      dc_y = 0;
      noise_state = 0.0f;
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
    const uint16_t breath_intensity = 1800 - (parameter_[0] >> 5);
    const uint16_t filter_coefficient =
        braids::lut_flute_body_filter[pitch_ >> 7];

    for (size_t i = 0; i < size; ++i) {
      const uint16_t bore_read = delay_ptr + 2 * kBoreLength - bore_integral;
      const uint16_t jet_read = delay_ptr + 2 * kJetLength - jet_integral;
      const int16_t bore_a = bore_[bore_read % kBoreLength];
      const int16_t bore_b = bore_[(bore_read - 1) % kBoreLength];
      const int16_t jet_a = jet_[jet_read % kJetLength];
      const int16_t jet_b = jet_[(jet_read - 1) % kJetLength];
      const int32_t bore_value =
          static_cast<int32_t>(stmlib::Mix(
              bore_a, bore_b, bore_fractional)) << 1;
      const int32_t jet_value =
          static_cast<int32_t>(stmlib::Mix(
              jet_a, jet_b, jet_fractional)) << 1;

      int32_t breath = braids::lut_blowing_envelope[excitation_ptr] << 1;
      const float raw_noise = static_cast<float>(stmlib::Random::GetSample()) *
          (1.0f / 32768.0f);
      noise_state += 0.12f * (raw_noise - noise_state);
      int32_t random_pressure = static_cast<int32_t>(
          noise_state * static_cast<float>(breath_intensity));
      random_pressure = random_pressure * breath >> 15;
      breath += random_pressure;

      lp_state = (-filter_coefficient * bore_value +
                  (4096 - filter_coefficient) * lp_state) >> 12;
      int32_t reflection = lp_state;
      dc_y = (kDcBlockingPole * dc_y >> 12) + reflection - dc_x;
      dc_x = reflection;
      reflection = dc_y;

      int32_t pressure_delta = breath - (reflection >> 1);
      jet_[delay_ptr % kJetLength] = stmlib::Clip16(pressure_delta >> 1);

      const int32_t jet_index = ClampInt(jet_value, 0, 65535);
      pressure_delta = static_cast<int16_t>(
          braids::lut_blowing_jet[jet_index >> 8]) + (reflection >> 1);
      bore_[delay_ptr % kBoreLength] = stmlib::Clip16(pressure_delta >> 1);
      ++delay_ptr;

      output[i] = static_cast<float>(bore_value) * (1.0f / 131072.0f);
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
    noise_state_ = noise_state;
  }

 private:
  static const size_t kBoreLength = 4096;
  static const size_t kJetLength = 1024;
  static const uint16_t kDcBlockingPole = 4055;
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
  int16_t* jet_;
  int16_t* bore_;
  int16_t pitch_;
  int16_t parameter_[2];
  uint16_t delay_ptr_;
  uint16_t excitation_ptr_;
  int32_t lp_state_;
  int32_t dc_x_;
  int32_t dc_y_;
  float noise_state_;
};

// A driven, damped delay line replaces Tidepool's static sine sub. It does not
// retrigger as a separate oscillator; it resonates with the main waveguide and
// carries small spectral changes from one gesture into the next.
class SympatheticString {
 public:
  void Init() {
    delay_ = static_cast<float*>(sdram_malloc(sizeof(float) * kDelayLength));
    available_ = delay_ != NULL;
    if (available_) {
      memset(delay_, 0, sizeof(float) * kDelayLength);
    }
    write_ptr_ = 0;
    delay_samples_ = 720.0f;
    damping_state_ = 0.0f;
    feedback_ = 0.9970f;
    damping_ = 0.14f;
  }

  void SetFrequency(float frequency) {
    frequency = std::max(12.0f, std::min(1200.0f, frequency));
    delay_samples_ = std::min(
        static_cast<float>(kDelayLength - 2), 48000.0f / frequency);
  }

  void SetMaterial(float material) {
    material = Clamp01(material);
    damping_ = 0.075f + 0.24f * material;
    feedback_ = 0.9983f - 0.0012f * material;
  }

  float Process(float excitation) {
    if (!available_) return 0.0f;
    float read_position = static_cast<float>(write_ptr_) - delay_samples_;
    while (read_position < 0.0f) read_position += kDelayLength;
    const uint32_t index_a = static_cast<uint32_t>(read_position);
    const uint32_t index_b = (index_a + 1) & (kDelayLength - 1);
    const float fraction = read_position - static_cast<float>(index_a);
    const float delayed = delay_[index_a] +
        (delay_[index_b] - delay_[index_a]) * fraction;
    damping_state_ += damping_ * (delayed - damping_state_);
    float next = damping_state_ * feedback_ + excitation * 0.018f;
    next = std::max(-1.25f, std::min(1.25f, next));
    delay_[write_ptr_] = next;
    write_ptr_ = (write_ptr_ + 1) & (kDelayLength - 1);
    return delayed;
  }

  bool available() const { return available_; }

 private:
  static const uint32_t kDelayLength = 4096;
  float* delay_;
  bool available_;
  uint32_t write_ptr_;
  float delay_samples_;
  float damping_state_;
  float feedback_;
  float damping_;
};

// Eight gently irregular modes per channel give the body enough partials to
// feel like an object. Small left/right offsets provide depth before any grain
// or reverb processing, rather than relying on random panning for stereo.
class StereoBody {
 public:
  void Init() {
    memset(y1_, 0, sizeof(y1_));
    memset(y2_, 0, sizeof(y2_));
    memset(coefficient_, 0, sizeof(coefficient_));
    memset(radius_squared_, 0, sizeof(radius_squared_));
    memset(drive_, 0, sizeof(drive_));
    material_ = 0.5f;
    SetFrequency(261.6256f);
  }

  void SetMaterial(float material) { material_ = Clamp01(material); }

  void SetFrequency(float fundamental) {
    static const float ratios[kNumModes] = {
      1.000f, 1.503f, 2.011f, 2.947f,
      4.083f, 5.431f, 6.817f, 8.263f
    };
    static const float radii[kNumModes] = {
      0.99915f, 0.99885f, 0.99855f, 0.99795f,
      0.99710f, 0.99610f, 0.99480f, 0.99320f
    };
    static const float stereo_offsets[kNumModes] = {
      0.0000f, 0.0017f, -0.0021f, 0.0028f,
      -0.0034f, 0.0041f, -0.0047f, 0.0053f
    };
    for (size_t channel = 0; channel < 2; ++channel) {
      const float side = channel == 0 ? -1.0f : 1.0f;
      for (size_t i = 0; i < kNumModes; ++i) {
        const float frequency = std::min(
            17500.0f,
            fundamental * ratios[i] * (1.0f + side * stereo_offsets[i]));
        const float radius = radii[i];
        coefficient_[channel][i] = 2.0f * radius *
            cosf(6.28318530718f * frequency * (1.0f / 48000.0f));
        radius_squared_[channel][i] = radius * radius;
        drive_[channel][i] = 1.0f - radius;
      }
    }
  }

  void Process(float input, float amount, float* left, float* right) {
    static const float gains[kNumModes] = {
      0.33f, 0.20f, 0.15f, 0.105f,
      0.075f, 0.055f, 0.040f, 0.027f
    };
    float body[2] = {0.0f, 0.0f};
    for (size_t channel = 0; channel < 2; ++channel) {
      for (size_t i = 0; i < kNumModes; ++i) {
        const float high_damping = 1.0f -
            material_ * 0.62f * static_cast<float>(i) /
                static_cast<float>(kNumModes - 1);
        const float value = drive_[channel][i] * input +
            coefficient_[channel][i] * y1_[channel][i] -
            radius_squared_[channel][i] * y2_[channel][i];
        y2_[channel][i] = y1_[channel][i];
        y1_[channel][i] = value;
        body[channel] += value * gains[i] * high_damping;
      }
    }
    const float direct = input * (1.0f - 0.22f * amount);
    *left = direct + body[0] * amount;
    *right = direct + body[1] * amount;
  }

 private:
  static const size_t kNumModes = 8;
  float material_;
  float coefficient_[2][kNumModes];
  float radius_squared_[2][kNumModes];
  float drive_[2][kNumModes];
  float y1_[2][kNumModes];
  float y2_[2][kNumModes];
};

class DiffusionTail {
 public:
  void Init() {
    memory_ = static_cast<uint16_t*>(
        sdram_malloc(sizeof(uint16_t) * kReverbWords));
    available_ = memory_ != NULL;
    if (available_) {
      memset(memory_, 0, sizeof(uint16_t) * kReverbWords);
      InitReverb(&reverb_, memory_, 0);
    }
  }

  void Process(clouds::FloatFrame* frames, size_t size, float depth,
               float material) {
    if (!available_) return;
    depth = Clamp01(depth);
    material = Clamp01(material);
    reverb_.set_amount(0.055f + 0.27f * depth);
    reverb_.set_input_gain(0.18f + 0.07f * depth);
    reverb_.set_time(0.78f + 0.16f * depth);
    reverb_.set_diffusion(0.72f);
    reverb_.set_lp(0.68f - 0.16f * material);
    reverb_.Process(frames, size);
  }

  bool available() const { return available_; }

 private:
  template<typename T>
  static auto InitReverb(T* reverb, uint16_t* memory, int)
      -> decltype(reverb->Init(memory, 48000.0f), void()) {
    reverb->Init(memory, 48000.0f);
  }

  template<typename T>
  static void InitReverb(T* reverb, uint16_t* memory, long) {
    reverb->Init(memory);
  }

  static const size_t kReverbWords = 16384;
  clouds::Reverb reverb_;
  uint16_t* memory_;
  bool available_;
};

}  // namespace tidepit

#endif  // TIDEPIT_VOICE_H_
