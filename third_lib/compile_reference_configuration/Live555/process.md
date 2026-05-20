tar -zxvf live555-latest.tar.gz
cd live

# 1. 清理旧编译文件（必做，避免残留错误）
make clean
```Makefile
# IMX6ULL 专用交叉编译器（不变）
CROSS_COMPILE?=		arm-buildroot-linux-gnueabihf-
# 核心修改：
# 1. 删除 -I/usr/local/include（禁止引用PC头文件）
# 2. 保留所有必须的宏定义 + 端口复用
COMPILE_OPTS =		$(INCLUDES) -I. -O2 -DSOCKLEN_T=socklen_t -DNO_SSTREAM=1 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -DALLOW_RTSP_SERVER_PORT_REUSE=1
C =			c
C_COMPILER =		$(CROSS_COMPILE)gcc
C_FLAGS =		$(COMPILE_OPTS)
CPP =			cpp
CPLUSPLUS_COMPILER =	$(CROSS_COMPILE)g++
# 核心修改：C++20 → C++11（老工具链完美支持，live555无任何问题）
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -std=c++11 -Wall -DBSD=1
OBJ =			o
LINK =			$(CROSS_COMPILE)g++ -o
LINK_OPTS =		
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		$(CROSS_COMPILE)ar cr 
LIBRARY_LINK_OPTS =	$(LINK_OPTS)
LIB_SUFFIX =			a
LIBS_FOR_CONSOLE_APPLICATION = -lssl -lcrypto
LIBS_FOR_GUI_APPLICATION =
EXE =
```
# 2. 生成 Linux 平台 Makefile + 编译（和之前一致）
./genMakefiles armlinux
# cd Makefile
```makefile
##### Change the following for your environment:
# IMX6ULL 专用交叉编译器（不变）
CROSS_COMPILE?=		arm-buildroot-linux-gnueabihf-
# 核心修改：
# 1. 删除 -I/usr/local/include（禁止引用PC头文件）
# 2. 保留所有必须的宏定义 + 端口复用
COMPILE_OPTS =		$(INCLUDES) -I. -O2 -DSOCKLEN_T=socklen_t -DNO_SSTREAM=1 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -DALLOW_RTSP_SERVER_PORT_REUSE=1
C =			c
C_COMPILER =		$(CROSS_COMPILE)gcc
C_FLAGS =		$(COMPILE_OPTS)
CPP =			cpp
CPLUSPLUS_COMPILER =	$(CROSS_COMPILE)g++
# 核心修改：C++20 → C++11（老工具链完美支持，live555无任何问题）
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -std=c++11 -Wall -DBSD=1
OBJ =			o
LINK =			$(CROSS_COMPILE)g++ -o
LINK_OPTS =		
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		$(CROSS_COMPILE)ar cr 
LIBRARY_LINK_OPTS =	$(LINK_OPTS)
LIB_SUFFIX =			a
LIBS_FOR_CONSOLE_APPLICATION = -lssl -lcrypto
LIBS_FOR_GUI_APPLICATION =
EXE =
##### End of variables to change

LIVEMEDIA_DIR = liveMedia
GROUPSOCK_DIR = groupsock
USAGE_ENVIRONMENT_DIR = UsageEnvironment
BASIC_USAGE_ENVIRONMENT_DIR = BasicUsageEnvironment

# 以下模块全部无用，推流不需要，注释掉testProgs
# TESTPROGS_DIR = testProgs
# MEDIA_SERVER_DIR = mediaServer
# PROXY_SERVER_DIR = proxyServer
# HLS_PROXY_DIR = hlsProxy

# ===================== 核心修改：只编译4个基础库 =====================
all:
	cd $(LIVEMEDIA_DIR) ; $(MAKE)
	cd $(GROUPSOCK_DIR) ; $(MAKE)
	cd $(USAGE_ENVIRONMENT_DIR) ; $(MAKE)
	cd $(BASIC_USAGE_ENVIRONMENT_DIR) ; $(MAKE)
	@echo
	@echo "live555 核心库编译完成！适配 IMX6ULL RTSP 推流"

# ===================== 核心修改：只安装4个基础库 =====================
install:
	cd $(LIVEMEDIA_DIR) ; $(MAKE) install
	cd $(GROUPSOCK_DIR) ; $(MAKE) install
	cd $(USAGE_ENVIRONMENT_DIR) ; $(MAKE) install
	cd $(BASIC_USAGE_ENVIRONMENT_DIR) ; $(MAKE) install
	@echo "live555 核心库安装完成！"

clean:
	cd $(LIVEMEDIA_DIR) ; $(MAKE) clean
	cd $(GROUPSOCK_DIR) ; $(MAKE) clean
	cd $(USAGE_ENVIRONMENT_DIR) ; $(MAKE) clean
	cd $(BASIC_USAGE_ENVIRONMENT_DIR) ; $(MAKE) clean
	# 清理无用模块
	# cd $(TESTPROGS_DIR) ; $(MAKE) clean
	# cd $(MEDIA_SERVER_DIR) ; $(MAKE) clean
	# cd $(PROXY_SERVER_DIR) ; $(MAKE) clean
	# cd $(HLS_PROXY_DIR) ; $(MAKE) clean

distclean: clean
	-rm -f $(LIVEMEDIA_DIR)/Makefile $(GROUPSOCK_DIR)/Makefile \
	  $(USAGE_ENVIRONMENT_DIR)/Makefile $(BASIC_USAGE_ENVIRONMENT_DIR)/Makefile \
	  Makefile
```
make -j4
# 3. ✅ 核心：安装到 你的项目自定义目录（无权限问题）
DESTDIR=/home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/live555 make install