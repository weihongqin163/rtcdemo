//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-04.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/file_parser/helper_h264_parser.h"
#include "common/helper.h"
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
#define DEFAULT_FRAME_RATE (30)
#define HIGH_VIDEO_FILE "test_data/send_video.h264"
#define LOW_VIDEO_FILE "test_data/send_video.h264"

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;

  std::string HighVideoFile = HIGH_VIDEO_FILE;
  std::string LowVideoFile = LOW_VIDEO_FILE;
  struct {
    int frameRate = DEFAULT_FRAME_RATE;
    bool showBandwidthEstimation = false;
  } video;
};

static void sendOneH264Frame(
    int frameRate, std::unique_ptr<HelperH264Frame> h264Frame,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender>
        videoH264FrameSender,
    agora::rtc::VIDEO_STREAM_TYPE streamtype = agora::rtc::VIDEO_STREAM_HIGH) {
  agora::rtc::EncodedVideoFrameInfo videoEncodedFrameInfo;
  videoEncodedFrameInfo.rotation = agora::rtc::VIDEO_ORIENTATION_0;
  videoEncodedFrameInfo.codecType = agora::rtc::VIDEO_CODEC_H264;
  videoEncodedFrameInfo.framesPerSecond = frameRate;
  videoEncodedFrameInfo.streamType = streamtype;
  videoEncodedFrameInfo.frameType =
      (h264Frame.get()->isKeyFrame
           ? agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_KEY_FRAME
           : agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_DELTA_FRAME);

  /*   AG_LOG(DEBUG, "sendEncodedVideoImage, buffer %p, len %d, frameType %d",
           reinterpret_cast<uint8_t*>(h264Frame.get()->buffer.get()),
     h264Frame.get()->bufferLen, videoEncodedFrameInfo.frameType); */

  videoH264FrameSender->sendEncodedVideoImage(
      reinterpret_cast<uint8_t*>(h264Frame.get()->buffer.get()),
      h264Frame.get()->bufferLen, videoEncodedFrameInfo);
}

static void SampleSendVideoH264Task(
    const SampleOptions& options,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender>
        videoH264FrameSender,
    bool& exitFlag, agora::rtc::VIDEO_STREAM_TYPE streamtype) {
  char* filename;
  if (streamtype == agora::rtc::VIDEO_STREAM_HIGH)
    filename = (char*)options.HighVideoFile.c_str();
  else
    filename = (char*)options.LowVideoFile.c_str();
  std::unique_ptr<HelperH264FileParser> h264FileParser(
      new HelperH264FileParser(filename));
  h264FileParser->initialize();

  // Calculate send interval based on frame rate. H264 frames are sent at this
  // interval
  PacerInfo pacer = {0, 1000 / options.video.frameRate,0,
                     std::chrono::steady_clock::now()};

  while (!exitFlag) {
    if (auto h264Frame = h264FileParser->getH264Frame()) {
      sendOneH264Frame(options.video.frameRate, std::move(h264Frame),
                       videoH264FrameSender, streamtype);
      waitBeforeNextSend(pacer);  // sleep for a while before sending next frame
    }
  };
}

static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; }

int main(int argc, char* argv[]) {
  SampleOptions options;
  opt_parser optParser;

  optParser.add_long_opt("token", &options.appId,
                         "The token for authentication / must");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id / must");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("HighVideoFile", &options.HighVideoFile,
                         "The high stream video file in H264 format to be sent");
  optParser.add_long_opt("LowVideoFile", &options.LowVideoFile,
                         "The low stream video file in H264 format to be sent");
  optParser.add_long_opt("fps", &options.video.frameRate,
                         "Target frame rate for sending the video stream");
  optParser.add_long_opt("bwe", &options.video.showBandwidthEstimation,
                         "show or hide bandwidth estimation info");

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

  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service) {
    AG_LOG(ERROR, "Failed to creating Agora service!");
  }

  // Create Agora connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.autoSubscribeVideo = false;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Register network observer to monitor bandwidth estimation result
  if (options.video.showBandwidthEstimation) {
    connection->registerNetworkObserver(connObserver.get());
  }

  // Create local user observer to monitor intra frame request
  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }

  // Create media node factory
  agora::agora_refptr<agora::rtc::IMediaNodeFactory> factory =
      service->createMediaNodeFactory();
  if (!factory) {
    AG_LOG(ERROR, "Failed to create media node factory!");
  }

  // Create video frame sender
  agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoFrameSender =
      factory->createVideoEncodedImageSender();
  if (!videoFrameSender) {
    AG_LOG(ERROR, "Failed to create video frame sender!");
    return -1;
  }
//if you want to send_dual_h264,the ccMode must be enabled
  agora::rtc::SenderOptions option;
  option.ccMode = agora::rtc::TCcMode::CC_ENABLED;
  // Create video track
  agora::agora_refptr<agora::rtc::ILocalVideoTrack> customVideoTrack =
      service->createCustomVideoTrack(videoFrameSender, option);
  if (!customVideoTrack) {
    AG_LOG(ERROR, "Failed to create video track!");
    return -1;
  }


// Set the dual_model 
  agora::rtc::SimulcastConfigInternal Low_streamConfig;
  Low_streamConfig.simulcastlayerConfigs[agora::rtc::StreamLayerIndexInternal::STREAM_LOW].enable = true;
  customVideoTrack->setSimulcastStreamMode(agora::rtc::SIMULCAST_STREAM_MODE::ENABLE_SIMULCAST_STREAM, Low_streamConfig);

  // Publish  video track
  connection->getLocalUser()->publishVideo(customVideoTrack);

  // Wait until connected before sending media stream
  connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);

  // Start sending media data
  AG_LOG(INFO, "Start sending  video data ...");

  std::thread sendHighVideoThread(SampleSendVideoH264Task, options,
                              videoFrameSender, std::ref(exitFlag),
                              agora::rtc::VIDEO_STREAM_TYPE::VIDEO_STREAM_HIGH);
  std::thread sendLowVideoThread(SampleSendVideoH264Task, options,
                               videoFrameSender, std::ref(exitFlag),
                               agora::rtc::VIDEO_STREAM_TYPE::VIDEO_STREAM_LOW);
  sendHighVideoThread.join();
  sendLowVideoThread.join();
  // Unpublish  video track

  connection->getLocalUser()->unpublishVideo(customVideoTrack);

 // Unregister connection observer
  connection->unregisterObserver(connObserver.get());

  // Disconnect from Agora channel
  if (connection->disconnect()) {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");

  // Destroy Agora connection and related resources
  connObserver.reset();
  localUserObserver.reset();
  videoFrameSender = nullptr;
  customVideoTrack = nullptr;
  factory = nullptr;
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
