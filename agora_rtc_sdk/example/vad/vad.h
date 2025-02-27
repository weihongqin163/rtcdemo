#pragma once
#include <deque>
#include <vector>
#include <cstdint>
#include <memory>
#include <cmath>

//#define MY_TEST
#ifdef MY_TEST
#include "AgoraMediaBase.h"
using namespace agora::rtc;
using namespace agora::media;

#else
#include "audioframetest.h"

#endif

// 类型定义和常量
using AudioBuffer = std::vector<int8_t>;
constexpr int VAD_STATE_NONSPEAKING = 0;
constexpr int VAD_STATE_STARTSPEAKING = 1;
constexpr int VAD_STATE_SPEAKING = 2;
constexpr int VAD_STATE_STOPSPEAKING = 3;



// 配置结构体
struct AudioVadConfigV2 {
    int preStartRecognizeCount;
    int startRecognizeCount;
    int stopRecognizeCount;
    double activePercent;
    double inactivePercent;
    int start_voiceprob;
    int stop_voiceprob;
    double start_rms;
    double stop_rms;
    //default constructor
    AudioVadConfigV2()
        : preStartRecognizeCount(16),
          startRecognizeCount(30),
          stopRecognizeCount(30),
          activePercent(0.7),
          inactivePercent(0.2),
          start_voiceprob(70),
          stop_voiceprob(70),
          start_rms(-30),
          stop_rms(-30) {}
};

// VAD数据包装
struct VadDataV2 {
    IAudioFrameObserver::AudioFrame audio_frame;
    bool is_activity;
    std::vector<int8_t> audio_buffer;
    
    VadDataV2(const IAudioFrameObserver::AudioFrame& frame, bool active)
        : is_activity(active) {
            audio_frame = frame;
            int bufsize = frame.bytesPerSample * frame.channels * frame.samplesPerChannel;
            audio_buffer.resize(bufsize);
            memcpy (audio_buffer.data(), frame.buffer, bufsize);
        }
    VadDataV2() {
        is_activity = false;
        memset (&audio_frame, 0, sizeof(audio_frame));
    }
    // 禁用拷贝构造和赋值操作符
    VadDataV2( VadDataV2&& other)
    {
        audio_frame = (other.audio_frame);
        audio_buffer = std::move(other.audio_buffer);
        is_activity = other.is_activity;
    }
       // 拷贝构造函数
    VadDataV2(const VadDataV2& other)
        : audio_frame(other.audio_frame),
          is_activity(other.is_activity),
          audio_buffer(other.audio_buffer) {}
    
    // 移动赋值运算符
    VadDataV2& operator=(VadDataV2&& other) noexcept {
        if (this != &other) {
            audio_frame = other.audio_frame;
            is_activity = other.is_activity;
            audio_buffer = std::move(other.audio_buffer);
        }
        return *this;
    }
    
    // 拷贝赋值运算符
    VadDataV2& operator=(const VadDataV2& other) {
        if (this != &other) {
            audio_frame = other.audio_frame;
            is_activity = other.is_activity;
            audio_buffer = other.audio_buffer;
        }
        return *this;
    }

};

//fixed size queue
template <typename T>
class FixedSizeQueue {
public:
    FixedSizeQueue(int size) : max_size_(size){
    }

    // 添加元素，若队列满则先移除最旧元素
    void push(const T& value) {
        if (dq_.size() >= max_size_) {
            dq_.pop_front();
        }
        dq_.push_back(value);
    }
     // 添加移动语义支持
    void push(T&& value) {
        if (dq_.size() >= max_size_) {
            dq_.pop_front();
        }
        dq_.push_back(std::move(value));
    }
    void resize(int size) {
        max_size_ = size;
    }

    // 移除最旧元素
    void pop() {
        if (!empty()) {
            dq_.pop_front();
        }
    }

    // 访问最旧元素
    T& front() { return dq_.front(); }
    const T& front() const { return dq_.front(); }

    // 访问最新元素
    T& back() { return dq_.back(); }
    const T& back() const { return dq_.back(); }

    // 队列当前大小
    size_t size() const { return dq_.size(); }

    // 队列是否为空
    bool empty() const { return dq_.empty(); }

    // 清空队列
    void clear() { dq_.clear(); }

    // 队列是否已满
    bool full() const { return dq_.size() >= max_size_; }

    // 迭代器类型
    using iterator = typename std::deque<T>::iterator;
    using const_iterator = typename std::deque<T>::const_iterator;

    // 获取起始迭代器
    iterator begin() { return dq_.begin(); }
    const_iterator begin() const { return dq_.begin(); }

    // 获取结束迭代器
    iterator end() { return dq_.end(); }
    const_iterator end() const { return dq_.end(); }

private:
    std::deque<T> dq_;
    size_t max_size_;
};
// 主VAD类
class AudioVadV2 {
public:
    AudioVadV2(const AudioVadConfigV2& config);
    virtual ~AudioVadV2();

    std::pair<int, AudioBuffer> process( IAudioFrameObserver::AudioFrame& frame);

    
protected:
    // 辅助方法
    inline bool isVadActive( IAudioFrameObserver::AudioFrame& frame)  ;

    std::pair<int, AudioBuffer> processStart(IAudioFrameObserver::AudioFrame& frame, bool is_active);
    std::pair<int, AudioBuffer> processSpeaking( IAudioFrameObserver::AudioFrame& frame, bool is_active) ;


    int getStartActiveCount();
    int getSpeakingActiveCount();
    AudioBuffer getAudioBuffer( FixedSizeQueue<VadDataV2>& queue, int startPos);
    AudioBuffer getAudioBuffer( IAudioFrameObserver::AudioFrame& frame);
    inline int getFrameSize(IAudioFrameObserver::AudioFrame& frame);
    inline void resetStopFlagList();
private:
    AudioVadConfigV2 config_;
    int current_state_ = VAD_STATE_NONSPEAKING;
    
    FixedSizeQueue<VadDataV2> start_queue_;
    FixedSizeQueue<int> stop_flag_list_; // only storing the activity flag no need to store whoe frame, so select a fixed-lenght array is enough

    int stop_flag_max_ = 0;
    int start_queue_max_;

        
};