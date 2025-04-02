
## 目录结构概述

**example**目录包括了Agora RTSA_Pro 的示例代码，编译脚本，编译工具等。你可以快速地编译得到若干个可执行文件，运行它们并体验agoraSDK的音视频码流传输功能。而其中的示例代码是你学习agoraSDK使用的绝佳范例，强烈建议你仔细阅读其中的相关sample，可以帮你快速上手如何正确调用agora的api接口实现相关的功能。具体如何编译、运行并阅读学习相关的内容，你可以阅读该目录下的ReadMe文档，指引你一步步继续。

**agora_sdk**目录包括了Agora RTSA_Pro的so库文件以及相关的头文件。相关的头文件中附有大量的注释，可以帮助你快速理解每个API的功能及所需参数。你可以结合 https://docs-preview.agoralab.co/en/RTSA/API%20Reference/rtsa_c/v2.0/index.html?transId=v2.0  上的API文档，学习掌握相关的API。

**example_java**目录包括了Agora RTSA_Pro Java 的示例代码，编译脚本，编译工具等。

**agora_sdk_java**目录包括了Agora RTSA_Pro Java的so库文件以及相关的Java类库文件。

**third_party**目录包括了一些有用的第三方库，可以认为这部分与SDK本身无关，你无需过分关注这一目录。

**上述** 文件夹中均有更加详细的ReadMe说明，强烈建议你仔细阅读每个ReadMe文档，可以帮助你更顺利的使用RTSA_Pro！

date: 2025-04-02
目录：examples/vad
用途：增加了vad.h, vad.cpp
用来实现在aivad v2 算法基础上的vad 判断
用法：参考main.cpp。
和rtc c++sdk的配合：
在c++ sdk的audioFrameObserver的onPlaybackBeforeMixing回调中，将frame数据传入到vad，这样就可以获得vad检测的结果
具体可以参考main.cpp。vadconfigure参数的含义可以咨询author。

