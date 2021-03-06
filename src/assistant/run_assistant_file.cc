/*
Copyright 2017 Google Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <grpc++/grpc++.h>

#include <getopt.h>

#include <chrono>  // NOLINT
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>  // NOLINT

#include "google/assistant/embedded/v1alpha2/embedded_assistant.grpc.pb.h"
#include "google/assistant/embedded/v1alpha2/embedded_assistant.pb.h"

#include "assistant/assistant_config.h"
#include "assistant/audio_input.h"
#include "assistant/audio_input_file.h"
#include "assistant/base64_encode.h"
#include "assistant/json_util.h"

namespace assistant = google::assistant::embedded::v1alpha2;

using assistant::AssistRequest;
using assistant::AssistResponse;
using assistant::AssistResponse_EventType_END_OF_UTTERANCE;
using assistant::AudioInConfig;
using assistant::AudioOutConfig;
using assistant::EmbeddedAssistant;

using grpc::CallCredentials;
using grpc::Channel;
using grpc::ClientReaderWriter;

static const char kCredentialsTypeUserAccount[] = "USER_ACCOUNT";
static const char kLanguageCode[] = "en-US";
static const char kDeviceModelId[] = "default";
static const char kDeviceInstanceId[] = "default";

bool verbose = false;

// Creates a channel to be connected to Google.
std::shared_ptr<Channel> CreateChannel(const std::string& host) {
  std::ifstream file("robots.pem");
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string roots_pem = buffer.str();

  if (verbose) {
    std::clog << "assistant_sdk robots_pem: " << roots_pem << std::endl;
  }
  ::grpc::SslCredentialsOptions ssl_opts = {roots_pem, "", ""};
  auto creds = ::grpc::SslCredentials(ssl_opts);
  std::string server = host + ":443";
  if (verbose) {
    std::clog << "assistant_sdk CreateCustomChannel(" << server
              << ", creds, arg)" << std::endl
              << std::endl;
  }
  ::grpc::ChannelArguments channel_args;
  return CreateCustomChannel(server, creds, channel_args);
}

void PrintUsage() {
  std::cerr << "Usage: ./run_assistant_file "
            << "--input <audio_file> "
            << "--output <audio_file> "
            << "--credentials <credentials_file> "
            << "[--api_endpoint <API endpoint>] "
            << "[--locale <locale>]" << std::endl;
}

bool GetCommandLineFlags(int argc, char** argv, std::string* audio_input,
                         std::string* audio_output,
                         std::string* credentials_file_path,
                         std::string* api_endpoint, std::string* locale) {
  const struct option long_options[] = {
      {"input", required_argument, nullptr, 'i'},
      {"output", required_argument, nullptr, 'o'},
      {"credentials", required_argument, nullptr, 'c'},
      {"api_endpoint", required_argument, nullptr, 'e'},
      {"locale", required_argument, nullptr, 'l'},
      {"verbose", no_argument, nullptr, 'v'},
      {nullptr, 0, nullptr, 0}};
  *api_endpoint = ASSISTANT_ENDPOINT;
  while (true) {
    int option_index;
    int option_char =
        getopt_long(argc, argv, "i:o:c:e:l:v", long_options, &option_index);
    if (option_char == -1) {
      break;
    }
    switch (option_char) {
      case 'i':
        *audio_input = optarg;
        break;
      case 'o':
        *audio_output = optarg;
        break;
      case 'c':
        *credentials_file_path = optarg;
        break;
      case 'e':
        *api_endpoint = optarg;
        break;
      case 'l':
        *locale = optarg;
        break;
      case 'v':
        verbose = true;
        break;
      default:
        PrintUsage();
        return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  std::string audio_input_source, audio_output_source, credentials_file_path,
      api_endpoint, locale;
  // Initialize gRPC and DNS resolvers
  // https://github.com/grpc/grpc/issues/11366#issuecomment-328595941
  grpc_init();
  if (!GetCommandLineFlags(argc, argv, &audio_input_source,
                           &audio_output_source, &credentials_file_path,
                           &api_endpoint, &locale)) {
    return -1;
  }

  // Create an AssistRequest
  AssistRequest request;
  auto* assist_config = request.mutable_config();

  if (locale.empty()) {
    locale = kLanguageCode;  // Default locale
  }
  if (verbose) {
    std::clog << "Using locale " << locale << std::endl;
  }
  // Set the DialogStateIn of the AssistRequest
  assist_config->mutable_dialog_state_in()->set_language_code(locale);

  // Set the DeviceConfig of the AssistRequest
  assist_config->mutable_device_config()->set_device_id(kDeviceInstanceId);
  assist_config->mutable_device_config()->set_device_model_id(kDeviceModelId);

  // Set parameters for audio output
  assist_config->mutable_audio_out_config()->set_encoding(
      AudioOutConfig::LINEAR16);
  assist_config->mutable_audio_out_config()->set_sample_rate_hertz(16000);

  if (audio_input_source.empty()) {
    std::cerr << "requires --input" << std::endl;
    return -2;
  }
  // Make sure the input file exists
  std::ifstream audio_input_file(audio_input_source);
  if (!audio_input_file) {
    std::cerr << "Audio input file \"" << audio_input_source
              << "\" does not exist." << std::endl;
    return -2;
  }
  if (audio_output_source.empty()) {
    std::clog << "requires --output" << std::endl;
    return -3;
  }

  std::unique_ptr<AudioInput> audio_input;
  // Set the AudioInConfig of the AssistRequest
  assist_config->mutable_audio_in_config()->set_encoding(
      AudioInConfig::LINEAR16);
  assist_config->mutable_audio_in_config()->set_sample_rate_hertz(16000);

  // Read credentials file.
  std::ifstream credentials_file(credentials_file_path);
  if (!credentials_file) {
    std::cerr << "Credentials file \"" << credentials_file_path
              << "\" does not exist." << std::endl;
    return -1;
  }
  std::stringstream credentials_buffer;
  credentials_buffer << credentials_file.rdbuf();
  std::string credentials = credentials_buffer.str();
  std::shared_ptr<CallCredentials> call_credentials;
  call_credentials = grpc::GoogleRefreshTokenCredentials(credentials);
  if (call_credentials.get() == nullptr) {
    std::cerr << "Credentials file \"" << credentials_file_path
              << "\" is invalid. Check step 5 in README for how to get valid "
              << "credentials." << std::endl;
    return -1;
  }

  // Begin a stream.
  auto channel = CreateChannel(api_endpoint);
  std::unique_ptr<EmbeddedAssistant::Stub> assistant(
      EmbeddedAssistant::NewStub(channel));

  grpc::ClientContext context;
  context.set_fail_fast(false);
  context.set_credentials(call_credentials);

  std::shared_ptr<ClientReaderWriter<AssistRequest, AssistResponse>> stream(
      std::move(assistant->Assist(&context)));
  // Write config in first stream.
  if (verbose) {
    std::clog << "assistant_sdk wrote first request: "
              << request.ShortDebugString() << std::endl;
  }
  stream->Write(request);

  audio_input.reset(new AudioInputFile(audio_input_source));

  audio_input->AddDataListener(
      [stream, &request](std::shared_ptr<std::vector<unsigned char>> data) {
        request.set_audio_in(&((*data)[0]), data->size());
        stream->Write(request);
      });
  audio_input->AddStopListener([stream]() { stream->WritesDone(); });
  audio_input->Start();

  // Read responses.
  if (verbose) {
    std::clog << "assistant_sdk waiting for response ... " << std::endl;
  }
  AssistResponse response;

  // Create an audio file to store the response
  std::ofstream audio_output_file;
  // Make sure to rewrite contents of file
  audio_output_file.open(audio_output_source,
                         std::ofstream::out | std::ofstream::trunc);
  // Check whether file was opened correctly
  if (audio_output_file.fail()) {
    std::cerr << "error opening file " << audio_output_source << std::endl;
    return -3;
  }
  if (verbose) {
    std::clog << "assistant_sdk writing audio response to "
              << audio_output_source << std::endl;
  }

  while (stream->Read(&response)) {  // Returns false when no more to read.
    if (response.has_audio_out() ||
        response.event_type() == AssistResponse_EventType_END_OF_UTTERANCE) {
      // Synchronously stops audio input if there is one.
      if (audio_input != nullptr && audio_input->IsRunning()) {
        audio_input->Stop();
      }
    }
    if (response.has_audio_out()) {
      audio_output_file << response.audio_out().audio_data();
    }
    // CUSTOMIZE: render spoken request on screen
    for (int i = 0; i < response.speech_results_size(); i++) {
      auto result = response.speech_results(i);
      if (verbose) {
        std::clog << "assistant_sdk request: \n"
                  << result.transcript() << " ("
                  << std::to_string(result.stability()) << ")" << std::endl;
      }
    }
    if (response.dialog_state_out().supplemental_display_text().size() > 0) {
      // CUSTOMIZE: render spoken response on screen
      std::clog << "assistant_sdk response:" << std::endl;
      std::cout << response.dialog_state_out().supplemental_display_text()
                << std::endl;
    }
  }
  audio_input_file.close();
  audio_output_file.close();

  grpc::Status status = stream->Finish();
  if (!status.ok()) {
    // Report the RPC failure.
    std::cerr << "assistant_sdk failed, error: " << status.error_message()
              << std::endl;
    return -1;
  }

  return 0;
}
