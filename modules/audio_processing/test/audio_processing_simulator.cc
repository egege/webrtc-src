/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/test/audio_processing_simulator.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_processing.h"
#include "api/scoped_refptr.h"
#include "common_audio/channel_buffer.h"
#include "common_audio/include/audio_util.h"
#include "common_audio/wav_file.h"
#include "modules/audio_processing/aec_dump/aec_dump_factory.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "modules/audio_processing/test/api_call_statistics.h"
#include "modules/audio_processing/test/fake_recording_device.h"
#include "modules/audio_processing/test/test_utils.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/time_utils.h"

namespace webrtc {
namespace test {
namespace {

std::string GetIndexedOutputWavFilename(absl::string_view wav_name,
                                        int counter) {
  StringBuilder ss;
  ss << wav_name.substr(0, wav_name.size() - 4) << "_" << counter
     << wav_name.substr(wav_name.size() - 4);
  return ss.Release();
}

void WriteEchoLikelihoodGraphFileHeader(std::ofstream* output_file) {
  (*output_file) << "import numpy as np" << std::endl
                 << "import matplotlib.pyplot as plt" << std::endl
                 << "y = np.array([";
}

void WriteEchoLikelihoodGraphFileFooter(std::ofstream* output_file) {
  (*output_file) << "])" << std::endl
                 << "if __name__ == '__main__':" << std::endl
                 << "  x = np.arange(len(y))*.01" << std::endl
                 << "  plt.plot(x, y)" << std::endl
                 << "  plt.ylabel('Echo likelihood')" << std::endl
                 << "  plt.xlabel('Time (s)')" << std::endl
                 << "  plt.show()" << std::endl;
}

// RAII class for execution time measurement. Updates the provided
// ApiCallStatistics based on the time between ScopedTimer creation and
// leaving the enclosing scope.
class ScopedTimer {
 public:
  ScopedTimer(ApiCallStatistics* api_call_statistics,
              ApiCallStatistics::CallType call_type)
      : start_time_(TimeNanos()),
        call_type_(call_type),
        api_call_statistics_(api_call_statistics) {}

  ~ScopedTimer() {
    api_call_statistics_->Add(TimeNanos() - start_time_, call_type_);
  }

 private:
  const int64_t start_time_;
  const ApiCallStatistics::CallType call_type_;
  ApiCallStatistics* const api_call_statistics_;
};

}  // namespace

SimulationSettings::SimulationSettings() = default;
SimulationSettings::SimulationSettings(const SimulationSettings&) = default;
SimulationSettings::~SimulationSettings() = default;

AudioProcessingSimulator::AudioProcessingSimulator(
    const SimulationSettings& settings,
    absl_nonnull scoped_refptr<AudioProcessing> audio_processing)
    : settings_(settings),
      ap_(std::move(audio_processing)),
      applied_input_volume_(settings.initial_mic_level),
      fake_recording_device_(
          settings.initial_mic_level,
          settings_.simulate_mic_gain ? *settings.simulated_mic_kind : 0),
      worker_queue_("file_writer_task_queue") {
  RTC_CHECK(!settings_.dump_internal_data || WEBRTC_APM_DEBUG_DUMP == 1);
  if (settings_.dump_start_frame || settings_.dump_end_frame) {
    ApmDataDumper::SetActivated(!settings_.dump_start_frame);
  } else {
    ApmDataDumper::SetActivated(settings_.dump_internal_data);
  }

  if (settings_.dump_set_to_use) {
    ApmDataDumper::SetDumpSetToUse(*settings_.dump_set_to_use);
  }

  if (settings_.dump_internal_data_output_dir.has_value()) {
    ApmDataDumper::SetOutputDirectory(
        settings_.dump_internal_data_output_dir.value());
  }

  if (settings_.ed_graph_output_filename &&
      !settings_.ed_graph_output_filename->empty()) {
    residual_echo_likelihood_graph_writer_.open(
        *settings_.ed_graph_output_filename);
    RTC_CHECK(residual_echo_likelihood_graph_writer_.is_open());
    WriteEchoLikelihoodGraphFileHeader(&residual_echo_likelihood_graph_writer_);
  }

  if (settings_.simulate_mic_gain)
    RTC_LOG(LS_VERBOSE) << "Simulating analog mic gain";
}

AudioProcessingSimulator::~AudioProcessingSimulator() {
  if (residual_echo_likelihood_graph_writer_.is_open()) {
    WriteEchoLikelihoodGraphFileFooter(&residual_echo_likelihood_graph_writer_);
    residual_echo_likelihood_graph_writer_.close();
  }
}

void AudioProcessingSimulator::ProcessStream(bool fixed_interface) {
  // Optionally simulate the input volume.
  if (settings_.simulate_mic_gain) {
    RTC_DCHECK(!settings_.use_analog_mic_gain_emulation);
    // Set the input volume to simulate.
    fake_recording_device_.SetMicLevel(applied_input_volume_);

    if (settings_.aec_dump_input_filename &&
        aec_dump_applied_input_level_.has_value()) {
      // For AEC dumps, use the applied input level, if recorded, to "virtually
      // restore" the capture signal level before the input volume was applied.
      fake_recording_device_.SetUndoMicLevel(*aec_dump_applied_input_level_);
    }

    // Apply the input volume.
    if (fixed_interface) {
      fake_recording_device_.SimulateAnalogGain(fwd_frame_.data);
    } else {
      fake_recording_device_.SimulateAnalogGain(in_buf_.get());
    }
  }

  // Let APM know which input volume was applied.
  // Keep track of whether `set_stream_analog_level()` is called.
  bool applied_input_volume_set = false;
  if (settings_.simulate_mic_gain) {
    // When the input volume is simulated, use the volume applied for
    // simulation.
    ap_->set_stream_analog_level(fake_recording_device_.MicLevel());
    applied_input_volume_set = true;
  } else if (!settings_.use_analog_mic_gain_emulation) {
    // Ignore the recommended input volume stored in `applied_input_volume_` and
    // instead notify APM with the recorded input volume (if available).
    if (settings_.aec_dump_input_filename &&
        aec_dump_applied_input_level_.has_value()) {
      // The actually applied input volume is available in the AEC dump.
      ap_->set_stream_analog_level(*aec_dump_applied_input_level_);
      applied_input_volume_set = true;
    } else if (!settings_.aec_dump_input_filename) {
      // Wav files do not include any information about the actually applied
      // input volume. Hence, use the recommended input volume stored in
      // `applied_input_volume_`.
      ap_->set_stream_analog_level(applied_input_volume_);
      applied_input_volume_set = true;
    }
  }

  // Post any scheduled runtime settings.
  if (settings_.frame_for_sending_capture_output_used_false &&
      *settings_.frame_for_sending_capture_output_used_false ==
          static_cast<int>(num_process_stream_calls_)) {
    ap_->PostRuntimeSetting(
        AudioProcessing::RuntimeSetting::CreateCaptureOutputUsedSetting(false));
  }
  if (settings_.frame_for_sending_capture_output_used_true &&
      *settings_.frame_for_sending_capture_output_used_true ==
          static_cast<int>(num_process_stream_calls_)) {
    ap_->PostRuntimeSetting(
        AudioProcessing::RuntimeSetting::CreateCaptureOutputUsedSetting(true));
  }

  // Process the current audio frame.
  if (fixed_interface) {
    {
      const auto st = ScopedTimer(&api_call_statistics_,
                                  ApiCallStatistics::CallType::kCapture);
      RTC_CHECK_EQ(
          AudioProcessing::kNoError,
          ap_->ProcessStream(fwd_frame_.data.data(), fwd_frame_.config,
                             fwd_frame_.config, fwd_frame_.data.data()));
    }
    fwd_frame_.CopyTo(out_buf_.get());
  } else {
    const auto st = ScopedTimer(&api_call_statistics_,
                                ApiCallStatistics::CallType::kCapture);
    RTC_CHECK_EQ(AudioProcessing::kNoError,
                 ap_->ProcessStream(in_buf_->channels(), in_config_,
                                    out_config_, out_buf_->channels()));
  }

  // Retrieve the recommended input volume only if `set_stream_analog_level()`
  // has been called to stick to the APM API contract.
  if (applied_input_volume_set) {
    applied_input_volume_ = ap_->recommended_stream_analog_level();
  }

  if (buffer_memory_writer_) {
    RTC_CHECK(!buffer_file_writer_);
    buffer_memory_writer_->Write(*out_buf_);
  } else if (buffer_file_writer_) {
    RTC_CHECK(!buffer_memory_writer_);
    buffer_file_writer_->Write(*out_buf_);
  }

  if (linear_aec_output_file_writer_) {
    bool output_available = ap_->GetLinearAecOutput(linear_aec_output_buf_);
    RTC_CHECK(output_available);
    RTC_CHECK_GT(linear_aec_output_buf_.size(), 0);
    RTC_CHECK_EQ(linear_aec_output_buf_[0].size(), 160);
    for (size_t k = 0; k < linear_aec_output_buf_[0].size(); ++k) {
      for (size_t ch = 0; ch < linear_aec_output_buf_.size(); ++ch) {
        RTC_CHECK_EQ(linear_aec_output_buf_[ch].size(), 160);
        float sample = FloatToFloatS16(linear_aec_output_buf_[ch][k]);
        linear_aec_output_file_writer_->WriteSamples(&sample, 1);
      }
    }
  }

  if (residual_echo_likelihood_graph_writer_.is_open()) {
    auto stats = ap_->GetStatistics();
    residual_echo_likelihood_graph_writer_
        << stats.residual_echo_likelihood.value_or(-1.f) << ", ";
  }

  ++num_process_stream_calls_;
}

void AudioProcessingSimulator::ProcessReverseStream(bool fixed_interface) {
  if (fixed_interface) {
    {
      const auto st = ScopedTimer(&api_call_statistics_,
                                  ApiCallStatistics::CallType::kRender);
      RTC_CHECK_EQ(
          AudioProcessing::kNoError,
          ap_->ProcessReverseStream(rev_frame_.data.data(), rev_frame_.config,
                                    rev_frame_.config, rev_frame_.data.data()));
    }
    rev_frame_.CopyTo(reverse_out_buf_.get());
  } else {
    const auto st = ScopedTimer(&api_call_statistics_,
                                ApiCallStatistics::CallType::kRender);
    RTC_CHECK_EQ(AudioProcessing::kNoError,
                 ap_->ProcessReverseStream(
                     reverse_in_buf_->channels(), reverse_in_config_,
                     reverse_out_config_, reverse_out_buf_->channels()));
  }

  if (reverse_buffer_file_writer_) {
    reverse_buffer_file_writer_->Write(*reverse_out_buf_);
  }

  ++num_reverse_process_stream_calls_;
}

void AudioProcessingSimulator::SetupBuffersConfigsOutputs(
    int input_sample_rate_hz,
    int output_sample_rate_hz,
    int reverse_input_sample_rate_hz,
    int reverse_output_sample_rate_hz,
    int input_num_channels,
    int output_num_channels,
    int reverse_input_num_channels,
    int reverse_output_num_channels) {
  in_config_ = StreamConfig(input_sample_rate_hz, input_num_channels);
  in_buf_.reset(new ChannelBuffer<float>(
      CheckedDivExact(input_sample_rate_hz, kChunksPerSecond),
      input_num_channels));

  reverse_in_config_ =
      StreamConfig(reverse_input_sample_rate_hz, reverse_input_num_channels);
  reverse_in_buf_.reset(new ChannelBuffer<float>(
      CheckedDivExact(reverse_input_sample_rate_hz, kChunksPerSecond),
      reverse_input_num_channels));

  out_config_ = StreamConfig(output_sample_rate_hz, output_num_channels);
  out_buf_.reset(new ChannelBuffer<float>(
      CheckedDivExact(output_sample_rate_hz, kChunksPerSecond),
      output_num_channels));

  reverse_out_config_ =
      StreamConfig(reverse_output_sample_rate_hz, reverse_output_num_channels);
  reverse_out_buf_.reset(new ChannelBuffer<float>(
      CheckedDivExact(reverse_output_sample_rate_hz, kChunksPerSecond),
      reverse_output_num_channels));

  fwd_frame_.SetFormat(input_sample_rate_hz, input_num_channels);
  rev_frame_.SetFormat(reverse_input_sample_rate_hz,
                       reverse_input_num_channels);

  if (settings_.use_verbose_logging) {
    LogMessage::LogToDebug(LS_VERBOSE);

    std::cout << "Sample rates:" << std::endl;
    std::cout << " Forward input: " << input_sample_rate_hz << std::endl;
    std::cout << " Forward output: " << output_sample_rate_hz << std::endl;
    std::cout << " Reverse input: " << reverse_input_sample_rate_hz
              << std::endl;
    std::cout << " Reverse output: " << reverse_output_sample_rate_hz
              << std::endl;
    std::cout << "Number of channels: " << std::endl;
    std::cout << " Forward input: " << input_num_channels << std::endl;
    std::cout << " Forward output: " << output_num_channels << std::endl;
    std::cout << " Reverse input: " << reverse_input_num_channels << std::endl;
    std::cout << " Reverse output: " << reverse_output_num_channels
              << std::endl;
  }

  SetupOutput();
}

void AudioProcessingSimulator::SelectivelyToggleDataDumping(
    int init_index,
    int capture_frames_since_init) const {
  if (!(settings_.dump_start_frame || settings_.dump_end_frame)) {
    return;
  }

  if (settings_.init_to_process && *settings_.init_to_process != init_index) {
    return;
  }

  if (settings_.dump_start_frame &&
      *settings_.dump_start_frame == capture_frames_since_init) {
    ApmDataDumper::SetActivated(true);
  }

  if (settings_.dump_end_frame &&
      *settings_.dump_end_frame == capture_frames_since_init) {
    ApmDataDumper::SetActivated(false);
  }
}

void AudioProcessingSimulator::SetupOutput() {
  if (settings_.output_filename) {
    std::string filename;
    if (settings_.store_intermediate_output) {
      filename = GetIndexedOutputWavFilename(*settings_.output_filename,
                                             output_reset_counter_);
    } else {
      filename = *settings_.output_filename;
    }

    std::unique_ptr<WavWriter> out_file(
        new WavWriter(filename, out_config_.sample_rate_hz(),
                      static_cast<size_t>(out_config_.num_channels()),
                      settings_.wav_output_format));
    buffer_file_writer_.reset(new ChannelBufferWavWriter(std::move(out_file)));
  } else if (settings_.aec_dump_input_string.has_value()) {
    buffer_memory_writer_ = std::make_unique<ChannelBufferVectorWriter>(
        settings_.processed_capture_samples);
  }

  if (settings_.linear_aec_output_filename) {
    std::string filename;
    if (settings_.store_intermediate_output) {
      filename = GetIndexedOutputWavFilename(
          *settings_.linear_aec_output_filename, output_reset_counter_);
    } else {
      filename = *settings_.linear_aec_output_filename;
    }

    linear_aec_output_file_writer_.reset(
        new WavWriter(filename, 16000, out_config_.num_channels(),
                      settings_.wav_output_format));

    linear_aec_output_buf_.resize(out_config_.num_channels());
  }

  if (settings_.reverse_output_filename) {
    std::string filename;
    if (settings_.store_intermediate_output) {
      filename = GetIndexedOutputWavFilename(*settings_.reverse_output_filename,
                                             output_reset_counter_);
    } else {
      filename = *settings_.reverse_output_filename;
    }

    std::unique_ptr<WavWriter> reverse_out_file(
        new WavWriter(filename, reverse_out_config_.sample_rate_hz(),
                      static_cast<size_t>(reverse_out_config_.num_channels()),
                      settings_.wav_output_format));
    reverse_buffer_file_writer_.reset(
        new ChannelBufferWavWriter(std::move(reverse_out_file)));
  }

  ++output_reset_counter_;
}

void AudioProcessingSimulator::DetachAecDump() {
  if (settings_.aec_dump_output_filename) {
    ap_->DetachAecDump();
  }
}

void AudioProcessingSimulator::ConfigureAudioProcessor() {
  AudioProcessing::Config apm_config;
  if (settings_.use_ts) {
    apm_config.transient_suppression.enabled = *settings_.use_ts != 0;
  }
  if (settings_.multi_channel_render) {
    apm_config.pipeline.multi_channel_render = *settings_.multi_channel_render;
  }

  if (settings_.multi_channel_capture) {
    apm_config.pipeline.multi_channel_capture =
        *settings_.multi_channel_capture;
  }

  if (settings_.use_agc2) {
    apm_config.gain_controller2.enabled = *settings_.use_agc2;
    if (settings_.agc2_fixed_gain_db) {
      apm_config.gain_controller2.fixed_digital.gain_db =
          *settings_.agc2_fixed_gain_db;
    }
    if (settings_.agc2_use_adaptive_gain) {
      apm_config.gain_controller2.adaptive_digital.enabled =
          *settings_.agc2_use_adaptive_gain;
    }
    if (settings_.agc2_use_input_volume_controller) {
      apm_config.gain_controller2.input_volume_controller.enabled =
          *settings_.agc2_use_input_volume_controller;
    }
  }
  if (settings_.use_pre_amplifier) {
    apm_config.pre_amplifier.enabled = *settings_.use_pre_amplifier;
    if (settings_.pre_amplifier_gain_factor) {
      apm_config.pre_amplifier.fixed_gain_factor =
          *settings_.pre_amplifier_gain_factor;
    }
  }

  if (settings_.use_analog_mic_gain_emulation) {
    if (*settings_.use_analog_mic_gain_emulation) {
      apm_config.capture_level_adjustment.enabled = true;
      apm_config.capture_level_adjustment.analog_mic_gain_emulation.enabled =
          true;
    } else {
      apm_config.capture_level_adjustment.analog_mic_gain_emulation.enabled =
          false;
    }
  }
  if (settings_.analog_mic_gain_emulation_initial_level) {
    apm_config.capture_level_adjustment.analog_mic_gain_emulation
        .initial_level = *settings_.analog_mic_gain_emulation_initial_level;
  }

  if (settings_.use_capture_level_adjustment) {
    apm_config.capture_level_adjustment.enabled =
        *settings_.use_capture_level_adjustment;
  }
  if (settings_.pre_gain_factor) {
    apm_config.capture_level_adjustment.pre_gain_factor =
        *settings_.pre_gain_factor;
  }
  if (settings_.post_gain_factor) {
    apm_config.capture_level_adjustment.post_gain_factor =
        *settings_.post_gain_factor;
  }

  const bool use_aec = settings_.use_aec && *settings_.use_aec;
  const bool use_aecm = settings_.use_aecm && *settings_.use_aecm;
  if (use_aec || use_aecm) {
    apm_config.echo_canceller.enabled = true;
    apm_config.echo_canceller.mobile_mode = use_aecm;
  }
  apm_config.echo_canceller.export_linear_aec_output =
      !!settings_.linear_aec_output_filename;

  if (settings_.use_hpf) {
    apm_config.high_pass_filter.enabled = *settings_.use_hpf;
  }

  if (settings_.use_agc) {
    apm_config.gain_controller1.enabled = *settings_.use_agc;
  }
  if (settings_.agc_mode) {
    apm_config.gain_controller1.mode =
        static_cast<AudioProcessing::Config::GainController1::Mode>(
            *settings_.agc_mode);
  }
  if (settings_.use_agc_limiter) {
    apm_config.gain_controller1.enable_limiter = *settings_.use_agc_limiter;
  }
  if (settings_.agc_target_level) {
    apm_config.gain_controller1.target_level_dbfs = *settings_.agc_target_level;
  }
  if (settings_.agc_compression_gain) {
    apm_config.gain_controller1.compression_gain_db =
        *settings_.agc_compression_gain;
  }
  if (settings_.use_analog_agc) {
    apm_config.gain_controller1.analog_gain_controller.enabled =
        *settings_.use_analog_agc;
  }
  if (settings_.analog_agc_use_digital_adaptive_controller) {
    apm_config.gain_controller1.analog_gain_controller.enable_digital_adaptive =
        *settings_.analog_agc_use_digital_adaptive_controller;
  }

  if (settings_.maximum_internal_processing_rate) {
    apm_config.pipeline.maximum_internal_processing_rate =
        *settings_.maximum_internal_processing_rate;
  }

  if (settings_.use_ns) {
    apm_config.noise_suppression.enabled = *settings_.use_ns;
  }
  if (settings_.ns_level) {
    const int level = *settings_.ns_level;
    RTC_CHECK_GE(level, 0);
    RTC_CHECK_LE(level, 3);
    apm_config.noise_suppression.level =
        static_cast<AudioProcessing::Config::NoiseSuppression::Level>(level);
  }
  if (settings_.ns_analysis_on_linear_aec_output) {
    apm_config.noise_suppression.analyze_linear_aec_output_when_available =
        *settings_.ns_analysis_on_linear_aec_output;
  }

  ap_->ApplyConfig(apm_config);

  if (settings_.use_ts) {
    // Default to key pressed if activating the transient suppressor with
    // continuous key events.
    ap_->set_stream_key_pressed(*settings_.use_ts == 2);
  }

  if (settings_.aec_dump_output_filename) {
    ap_->AttachAecDump(AecDumpFactory::Create(
        *settings_.aec_dump_output_filename, -1, worker_queue_.Get()));
  }
}

}  // namespace test
}  // namespace webrtc
