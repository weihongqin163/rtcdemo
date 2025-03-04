//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-04.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

//video
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//  {SDK::capture video}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                                                                                  sample send h264(this sample)                              sample receive h264

//This sample will show how to use the SDK to send the encoded_Video to the Agora_channel
//As a user,you should papera the encoded_Video and create the Agora_service,Agora_connection, Agora_EncodedImage_sender and Agora_local_video_track
//You should parse the encoded_Video and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendEncodedVideoImage())
//The class HelperH264FileParser is a common_helper class to parse the h264 video
//And all Agora necessary Classes builded in main() function
//Last, the sendVideoThread call Sender->sendEncodedVideoImage() to send the encoded videoFrame

//The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
//The callback functions can be found in **observer , you should register the observer first.

// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//             connection->connect()
//         factory = service->createMediaNodeFactory()
//             VideoSender = factory->createVideoEncodedImageSender();
//                 VideoTrack = service->createCustomVideoTrack()
//      connection->getLocalUser()->publishAudio(VideoTrack);
//                 Sender->sendEncodedVideoImage()

//audio
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//  {SDK::capture audio}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                        sample send pcm(this sample)                sample send opus                                                                                            sample receive pcm

//This sample will show how to use the SDK to send the raw AudioData(pcm) to the Agora_channel
//As a user,you should papera the audio and create the Agora_service,Agora_connection, Agora_audio_pcm_sender and Agora_local_audio_track
//You should parse the audio and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendAudioPcmData())
//And all Agora necessary Classes builded in main() function
//Last, the sendAudioThread call Sender->sendAudioPcmData() to send the audioFrame

//The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
//The callback functions can be found in **observer , you should register the observer first.

// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//         connection->connect()
//         factory = service->createMediaNodeFactory()
//             AudioSender = factory->createAudioPcmDataSender();
//                 AudioTrack = service->createCustomAudioTrack();
//      connection->getLocalUser()->publishAudio(audioTrack);
//                 Sender->sendAudioPcmData()

//The destruct order of all the class can be find in the main function end.

// Wish you have a great experience with Agora_SDK!

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

#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_SAMPLE_RATE (16000)
#define DEFAULT_NUM_OF_CHANNELS (1)
#define DEFAULT_FRAME_RATE (30)
#define DEFAULT_AUDIO_FILE "test_data/send_audio_16k_1ch.pcm"

struct SampleOptions {
	std::string appId;
	std::string channelId;
	std::string userId;
	std::string audioFile = DEFAULT_AUDIO_FILE;
	std::string localIP;
	bool cloudProxy = false;
	bool useTcpProxy = false;
	struct {
		int sampleRate = DEFAULT_SAMPLE_RATE;
		int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
	} audio;
};

static void sendOnePcmFrame(const SampleOptions &options,
							agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender)
{
	static FILE *file = nullptr;
	const char *fileName = options.audioFile.c_str();

	// Calculate byte size for 10ms audio samples
	int sampleSize = sizeof(int16_t) * options.audio.numOfChannels;
	int samplesPer10ms = options.audio.sampleRate / 100;
	int sendBytes = sampleSize * samplesPer10ms;

	if (!file) {
		if (!(file = fopen(fileName, "rb"))) {
			AG_LOG(ERROR, "Failed to open audio file %s", fileName);
			return;
		}
		AG_LOG(INFO, "Open audio file %s successfully", fileName);
	}

	uint8_t frameBuf[sendBytes];

	if (fread(frameBuf, 1, sizeof(frameBuf), file) != sizeof(frameBuf)) {
		if (feof(file)) {
			fclose(file);
			file = nullptr;
			AG_LOG(INFO, "End of audio file");
		} else {
			AG_LOG(ERROR, "Error reading audio data: %s", std::strerror(errno));
		}
		return;
	}

	if (audioFrameSender->sendAudioPcmData(
				frameBuf, 0, 0,samplesPer10ms, agora::rtc::TWO_BYTES_PER_SAMPLE,
				options.audio.numOfChannels, options.audio.sampleRate) < 0) {
		AG_LOG(ERROR, "Failed to send audio frame!");
	}
}

static void
SampleSendAudioTask(const SampleOptions &options,
					agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender,
					bool &exitFlag)
{
	// Currently only 10 ms PCM frame is supported. So PCM frames are sent at 10 ms interval
	PacerInfo pacer = { 0, 10, 0, std::chrono::steady_clock::now() };

	while (!exitFlag) {
		sendOnePcmFrame(options, audioFrameSender);
		waitBeforeNextSend(pacer); // sleep for a while before sending next frame
	}
}

static bool exitFlag = false;
static void SignalHandler(int sigNo)
{
	exitFlag = true;
}

int main(int argc, char *argv[])
{
	SampleOptions options;
	opt_parser optParser;

	optParser.add_long_opt("token", &options.appId, "The token for authentication / must");
	optParser.add_long_opt("channelId", &options.channelId, "Channel Id / must");
	optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
	optParser.add_long_opt("audioFile", &options.audioFile,
						   "The audio file in raw PCM format to be sent");
	optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
						   "Sample rate for the PCM file to be sent");
	optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
						   "Number of channels for the PCM file to be sent");
	optParser.add_long_opt("localIP", &options.localIP, "Local IP");
	optParser.add_long_opt("enableCloudProxy", &options.cloudProxy, "enableCloudProxy");
  optParser.add_long_opt("enableTcpProxy", &options.useTcpProxy, "enableCloudProxy");

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
	ccfg.autoSubscribeAudio = false;
	ccfg.autoSubscribeVideo = false;
	ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
	agora::agora_refptr<agora::rtc::IRtcConnection> connection = service->createRtcConnection(ccfg);
	if (!connection) {
		AG_LOG(ERROR, "Failed to creating Agora connection!");
		return -1;
	}

	if (!options.localIP.empty()) {
		if (setLocalIP(connection, options.localIP)) {
			AG_LOG(ERROR, "set local IP to %s error!", options.localIP.c_str());
			return -1;
		}
	}

	// Register connection observer to monitor connection event
	auto connObserver = std::make_shared<SampleConnectionObserver>();
	connection->registerObserver(connObserver.get());

	// Create local user observer to monitor intra frame request
	auto localUserObserver = std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

	//  open the CloudProxy
	if (options.cloudProxy == true) {
		agora::base::IAgoraParameter *agoraParameter = connection->getAgoraParameter();
		agoraParameter->setBool("rtc.enable_proxy", true);
		AG_LOG(INFO, "set the Cloud_Proxy Open!");
		if (options.useTcpProxy == true) {
			int rr = agoraParameter->setParameters("{\"rtc.proxy_server\":[13,\"\",0]}");
			AG_LOG(INFO, "set the Proxy Type TCP!");
		}
	}
	// Connect to Agora channel
	if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
							options.userId.c_str())) {
		AG_LOG(ERROR, "Failed to connect to Agora channel!");
		return -1;
	}

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

	// Publish audio & video track
	connection->getLocalUser()->publishAudio(customAudioTrack);

	// Wait until connected before sending media stream
	connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);

	if (!options.localIP.empty()) {
		std::string ip;
		getLocalIP(connection, ip);
		AG_LOG(INFO, "Local IP:%s", ip.c_str());
	}

	// Start sending media data
	AG_LOG(INFO, "Start sending audio & video data ...");
	std::thread sendAudioThread(SampleSendAudioTask, options, audioFrameSender, std::ref(exitFlag));

	sendAudioThread.join();

	// Unpublish audio & video track
	connection->getLocalUser()->unpublishAudio(customAudioTrack);
	// Unregister connection observer
	connection->unregisterObserver(connObserver.get());

	// Unregister network observer
	connection->unregisterNetworkObserver(connObserver.get());

	// Disconnect from Agora channel
	if (connection->disconnect()) {
		AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
		return -1;
	}
	AG_LOG(INFO, "Disconnected from Agora channel successfully");

	// Destroy Agora connection and related resources
	connObserver.reset();
	localUserObserver.reset();
	audioFrameSender = nullptr;
	customAudioTrack = nullptr;
	factory = nullptr;
	connection = nullptr;

	// Destroy Agora Service
	service->release();
	service = nullptr;

	return 0;
}
