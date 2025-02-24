//  Agora RTC/MEDIA SDK
//
//  Created by Seven Du in 2024-12.
//  Receive Audio/Video and Echo Back
//  Modified from sample_receive_h264_pcm.cpp

#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>
#include <ctime>
#include <string>

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraVideoTrack.h"

#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_SAMPLE_RATE (16000)
#define DEFAULT_NUM_OF_CHANNELS (1)
#define DEFAULT_AUDIO_FILE "received_audio.pcm"
#define DEFAULT_VIDEO_FILE "received_video.h264"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)
#define STREAM_TYPE_HIGH "high"
#define STREAM_TYPE_LOW "low"


static std::condition_variable *cv = nullptr;

static std::string currentTime() {
  auto now = std::chrono::system_clock::now();
  auto now_as_time_t = std::chrono::system_clock::to_time_t(now);
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  int milliseconds = now_ms.count() % 1000;

  // Convert to tm struct
  std::tm* now_tm = std::localtime(&now_as_time_t);
  char formatted[100];
  strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", now_tm);
  char *p = formatted + strlen(formatted);
  snprintf(p, sizeof(formatted) - (p - formatted), ".%03d", milliseconds);
  return std::string(formatted);
}

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string remoteUserId;
  std::string streamType = STREAM_TYPE_HIGH;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  std::string videoFile = DEFAULT_VIDEO_FILE;

  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
};

class PcmFrameObserver : public agora::media::IAudioFrameObserverBase {
 public:
  PcmFrameObserver(const std::string& outputFilePath, agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender, bool isDirect)
      : outputFilePath_(outputFilePath),
      audioFrameSender_(audioFrameSender),
      isDirect_(isDirect),
        pcmFile_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  ~PcmFrameObserver() {
    if (cv) cv =  nullptr;
  }

  bool onPlaybackAudioFrame(const char* channelId,AudioFrame& audioFrame) override { return true; };

  bool onRecordAudioFrame(const char* channelId,AudioFrame& audioFrame) override { return true; };

  bool onMixedAudioFrame(const char* channelId,AudioFrame& audioFrame) override { return true; };

  bool onPlaybackAudioFrameBeforeMixing(const char* channelId, agora::media::base::user_id_t userId, AudioFrame& audioFrame) override;

  int getObservedAudioFramePosition() override {return 0;};

  bool onEarMonitoringAudioFrame(AudioFrame& audioFrame) override {return true;};

  AudioParams getEarMonitoringAudioParams()override {return  AudioParams();};

  AudioParams getPlaybackAudioParams() override {return  AudioParams();};

  AudioParams getRecordAudioParams()  override {return  AudioParams();};

  AudioParams getMixedAudioParams() override {return  AudioParams();};

  void registerGlobalCondRef() { cv = &cond; }

  void lock() { m.lock(); }
  void unlock() { m.unlock(); }

  void notify_one() { cond.notify_one(); }

  AudioFrame *pop(bool &exitFlag) { // block pop
    std::unique_lock<std::mutex> lock(m);
    cond.wait(lock, [this, &exitFlag]() { return !q.empty() || exitFlag; });

    if (exitFlag) {
      return nullptr;
    }

    AudioFrame *frame = q.front();
    q.pop();
    return frame;
  }

 private:
  std::string outputFilePath_;
  FILE* pcmFile_;
  int fileCount;
  int fileSize_;
  std::queue<agora::media::IAudioFrameObserverBase::AudioFrame *> q; // audio queue
  std::mutex m;
  std::condition_variable cond;
  // add by wei
  agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender_;
  bool isDirect_; // dirct loopback audio or not
};

class H264FrameReceiver : public agora::media::IVideoEncodedFrameObserver {
 public:
  H264FrameReceiver(const std::string& outputFilePath)
      : outputFilePath_(outputFilePath),
        h264File_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  bool onEncodedVideoFrameReceived(agora::rtc::uid_t uid, const uint8_t* imageBuffer, size_t length,
                                   const agora::rtc::EncodedVideoFrameInfo& videoEncodedFrameInfo)  override;

 private:
  std::string outputFilePath_;
  FILE* h264File_;
  int fileCount;
  int fileSize_;
};

bool PcmFrameObserver::onPlaybackAudioFrameBeforeMixing(const char* channelId, agora::media::base::user_id_t userId, AudioFrame& audioFrame) {
  if (isDirect_) {
    if (audioFrameSender_) {
      audioFrameSender_->sendAudioPcmData((void *)audioFrame.buffer, 0, 0, audioFrame.samplesPerChannel, agora::rtc::TWO_BYTES_PER_SAMPLE,
                                      (size_t)audioFrame.channels,
                                      (uint32_t)audioFrame.samplesPerSec);
    }

  }else {  // origi codes
    AudioFrame *frame = (AudioFrame *)malloc(sizeof(AudioFrame));
    memcpy(frame, &audioFrame, sizeof(AudioFrame));
    int len = frame->samplesPerChannel * frame->channels * frame->bytesPerSample;
    frame->buffer = malloc(len);
    memcpy(frame->buffer, audioFrame.buffer, len);
    m.lock();
    q.push(frame);
    cond.notify_one();
    m.unlock();
    AG_LOG(INFO, "%s pushed frame %d", currentTime().c_str(), frame->samplesPerChannel);

    // Create new file to save received PCM samples
    if (!pcmFile_) {
      std::string fileName = (++fileCount > 1)
                                ? (outputFilePath_ + to_string(fileCount))
                                : outputFilePath_;
      if (!(pcmFile_ = fopen(fileName.c_str(), "w"))) {
        AG_LOG(ERROR, "Failed to create received audio file %s",
              fileName.c_str());
        return false;
      }
      AG_LOG(INFO, "Created file %s to save received PCM samples",
            fileName.c_str());
    }

    // Write PCM samples
    size_t writeBytes =
        audioFrame.samplesPerChannel * audioFrame.channels * sizeof(int16_t);
    if (fwrite(audioFrame.buffer, 1, writeBytes, pcmFile_) != writeBytes) {
      AG_LOG(ERROR, "Error writing decoded audio data: %s", std::strerror(errno));
      return false;
    }
    fileSize_ += writeBytes;

    // Close the file if size limit is reached
    if (fileSize_ >= DEFAULT_FILE_LIMIT) {
      fclose(pcmFile_);
      pcmFile_ = nullptr;
      fileSize_ = 0;
    }
  }
  return true;
}

bool H264FrameReceiver::onEncodedVideoFrameReceived(agora::rtc::uid_t uid, const uint8_t* imageBuffer, size_t length,
                                   const agora::rtc::EncodedVideoFrameInfo& videoEncodedFrameInfo) {
  // Create new file to save received H264 frames
  if (!h264File_) {
    std::string fileName = (++fileCount > 1)
                               ? (outputFilePath_ + to_string(fileCount))
                               : outputFilePath_;
    if (!(h264File_ = fopen(fileName.c_str(), "w+"))) {
      AG_LOG(ERROR, "Failed to create received video file %s",
             fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s to save received H264 frames",
           fileName.c_str());
  }

  if (fwrite(imageBuffer, 1, length, h264File_) != length) {
    AG_LOG(ERROR, "Error writing h264 data: %s", std::strerror(errno));
    return false;
  }
  fileSize_ += length;

  // Close the file if size limit is reached
  if (fileSize_ >= DEFAULT_FILE_LIMIT) {
    fclose(h264File_);
    h264File_ = nullptr;
    fileSize_ = 0;
  }
  return true;
}

static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; if (cv) cv->notify_one(); }

static void SendAudioTask(
    const SampleOptions& options,
    std::shared_ptr<PcmFrameObserver> audioFrameObserver,
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender,
    bool& exitFlag) {
    AG_LOG(INFO, "%s SendAudioTask Started", currentTime().c_str());
    while(!exitFlag) {
      struct agora::media::IAudioFrameObserverBase::AudioFrame *frame = audioFrameObserver->pop(exitFlag);
      if (!frame) break;
      AG_LOG(INFO, "%s SendAudioTask frame samples: %d rate: %d", currentTime().c_str(), frame->samplesPerChannel, frame->samplesPerSec);

      if (audioFrameSender->sendAudioPcmData((void *)frame->buffer, 0, 0, frame->samplesPerChannel, agora::rtc::TWO_BYTES_PER_SAMPLE,
                                      (size_t)frame->channels,
                                      (uint32_t)frame->samplesPerSec) < 0) {
        AG_LOG(ERROR, "Failed to send audio frame!");
      }
      // free
      if (frame->buffer) free(frame->buffer);
      free(frame);
    }
    AG_LOG(INFO, "%s SendAudioTask Done", currentTime().c_str());
}

// 
agora::base::IAgoraService* local_createAndInitAgoraService(bool enableAudioDevice,
                                                      bool enableAudioProcessor, bool enableVideo,
                                                      agora::CHANNEL_PROFILE_TYPE profile,
                                                      agora::rtc::AUDIO_SCENARIO_TYPE audioSenario,
                                                      bool enableuseStringUid=false,bool enablelowDelay = false,const char* appid=nullptr) {
  int32_t buildNum = 0;
  getAgoraSdkVersion(&buildNum);
#if defined(SDK_BUILD_NUM)
  if ( buildNum != SDK_BUILD_NUM ) {
    AG_LOG(ERROR, "SDK VERSION CHECK FAILED!\nSDK version: %d\nAPI Version: %d\n", buildNum, SDK_BUILD_NUM);
    //return nullptr;
  }
#endif
  AG_LOG(INFO, "SDK version: %d\n", buildNum);
  auto service = createAgoraService();
  agora::base::AgoraServiceConfiguration scfg;
  scfg.appId = appid;
  scfg.enableAudioProcessor = enableAudioProcessor;
  scfg.enableAudioDevice = enableAudioDevice;
  scfg.enableVideo = enableVideo;
  scfg.useStringUid = enableuseStringUid;
  scfg.audioScenario = audioSenario;
  scfg.channelProfile = profile;
  if(enablelowDelay){
    scfg.channelProfile = agora::CHANNEL_PROFILE_TYPE::CHANNEL_PROFILE_CLOUD_GAMING;
  }
  if (service->initialize(scfg) != agora::ERR_OK) {
    return nullptr;
  }

  #if defined(TARGET_OS_LINUX)
#define DEFAULT_LOG_PATH ("./io.agora.rtc_sdk/agorasdk.log")
#else
#define DEFAULT_LOG_PATH ("/data/local/tmp/agorasdk.log")
#endif

#define DEFAULT_LOG_SIZE (512 * 1024)  // default log size is 512 kb
  if (service->setLogFile(DEFAULT_LOG_PATH, DEFAULT_LOG_SIZE) != 0) {
    return nullptr;
  }
 
  return service;
}


int main(int argc, char* argv[]) {
  SampleOptions options;
  opt_parser optParser;

  optParser.add_long_opt("token", &options.appId,
                         "The token for authentication");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("remoteUserId", &options.remoteUserId,
                         "The remote user to receive stream from");
  optParser.add_long_opt("audioFile", &options.audioFile, "Output audio file");
  optParser.add_long_opt("videoFile", &options.videoFile, "Output video file");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for received audio");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for received audio");
  optParser.add_long_opt("streamtype", &options.streamType, "the stream  type");

  if ((argc <= 1) || !optParser.parse_opts(argc, argv)) {
    std::ostringstream strStream;
    optParser.print_usage(argv[0], strStream);
    std::cout << strStream.str() << std::endl;
    return -1;
  }

  if (options.appId.empty()) {
    AG_LOG(ERROR, "Must provide appId!");
    return -1;
  }

  if (options.channelId.empty()) {
    AG_LOG(ERROR, "Must provide channelId!");
    return -1;
  }

  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  bool isDirectLoopback = true;

  // Create Agora service
  auto service = local_createAndInitAgoraService(false, true, true, agora::CHANNEL_PROFILE_LIVE_BROADCASTING, agora::rtc::AUDIO_SCENARIO_TYPE::AUDIO_SCENARIO_CHORUS);
  if (!service) {
    AG_LOG(ERROR, "Failed to creating Agora service!");
  }

  // Create Agora connection

  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.enableAudioRecordingOrPlayout =
      false;  // Subscribe audio but without playback

//move to here
   // Create media node factory
  agora::agora_refptr<agora::rtc::IMediaNodeFactory> factory = service->createMediaNodeFactory();
  if (!factory) {
    AG_LOG(ERROR, "Failed to create media node factory!");
  }

  // Create audio data sender
  agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender =
      factory->createAudioPcmDataSender();
  if (!audioFrameSender) {
    AG_LOG(ERROR, "Failed to create audio data sender!");
    return -1;
  }

  // Create audio track
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> customAudioTrack =
      service->createCustomAudioTrack(audioFrameSender);
  if (!customAudioTrack) {
    AG_LOG(ERROR, "Failed to create audio track!");
    return -1;
  }
  // by wei:set track to no-delay
  customAudioTrack->setAudioFrameSendDelayMs(10);

  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

// by wei
  connection->getLocalUser()->setAudioScenario(agora::rtc::AUDIO_SCENARIO_CHORUS);

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Subcribe streams from all remote users or specific remote user
  agora::rtc::VideoSubscriptionOptions subscriptionOptions;
  subscriptionOptions.encodedFrameOnly = true;
  if (options.streamType == STREAM_TYPE_HIGH) {
    subscriptionOptions.type = agora::rtc::VIDEO_STREAM_HIGH;
  } else if(options.streamType==STREAM_TYPE_LOW){
    subscriptionOptions.type = agora::rtc::VIDEO_STREAM_LOW;
  } else{
    AG_LOG(ERROR, "It is a error stream type");
    return -1;
  }
  if (options.remoteUserId.empty()) {
    AG_LOG(INFO, "Subscribe streams from all remote users");
    connection->getLocalUser()->subscribeAllAudio();
    connection->getLocalUser()->subscribeAllVideo(subscriptionOptions);
  } else {
    connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str());
    connection->getLocalUser()->subscribeVideo(options.remoteUserId.c_str(),
                                               subscriptionOptions);
  }



  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }

  // Create local user observer
  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Register audio frame observer to receive audio stream
  auto pcmFrameObserver = std::make_shared<PcmFrameObserver>(options.audioFile, audioFrameSender, isDirectLoopback);
  pcmFrameObserver->registerGlobalCondRef(); // make it global so we can notify it in signal handler
  if (connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(
          options.audio.numOfChannels, options.audio.sampleRate)) {
    AG_LOG(ERROR, "Failed to set audio frame parameters!");
    return -1;
  }
  localUserObserver->setAudioFrameObserver(pcmFrameObserver.get());

  // Register h264 frame receiver to receive video stream
  auto h264FrameReceiver =
      std::make_shared<H264FrameReceiver>(options.videoFile);
  localUserObserver->setVideoEncodedImageReceiver(h264FrameReceiver.get());



  // Publish audio & video track
  connection->getLocalUser()->publishAudio(customAudioTrack);
  // connection->getLocalUser()->publishVideo(customVideoTrack);

  // low latency settings
  /* // moved by wei 
  if (1) {
    customAudioTrack->setAudioFrameSendDelayMs(10);
    connection->getLocalUser()->setAudioScenario(agora::rtc::AUDIO_SCENARIO_CHORUS);
  }
  */

  // Wait until connected before sending media stream
  connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);

  // Start receiving incoming media data
  AG_LOG(INFO, "Start receiving audio & video data ...");

  std::thread aTask;
  if(!isDirectLoopback) {
    aTask = std::thread(SendAudioTask, options, pcmFrameObserver, audioFrameSender, std::ref(exitFlag));
   
  }

  // Periodically check exit flag
  while (!exitFlag) {
    usleep(10000);
  }

  // Unregister audio & video frame observers
  localUserObserver->unsetAudioFrameObserver();
  localUserObserver->unsetVideoFrameObserver();

  if(!isDirectLoopback) {
    aTask.join();
  }
 

  // Disconnect from Agora channel
  if (connection->disconnect()) {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");

  // Destroy Agora connection and related resources
  localUserObserver.reset();
  pcmFrameObserver.reset();
  h264FrameReceiver.reset();
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
