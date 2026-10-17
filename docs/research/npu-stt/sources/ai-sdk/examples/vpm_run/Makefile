TARGET = vpm_run

ifdef AI_SDK_PLATFORM
include ../../machinfo/$(AI_SDK_PLATFORM)/config.mk
endif

INCLUDE += -I$(INSTALL_PREFIX)/usr/include -I../libawnn_viplite -I../libawutils
LIBS    += -L $(INSTALL_PREFIX)/usr/lib -Wl,-rpath-link,$(INSTALL_PREFIX)/usr/lib

CFLAGS += -g -O0 -DDEBUG -D_DEBUG

MYCFLAGS += -DSAVE_OUTPUT_TXT_FILE
MYCFLAGS += -DSHOW_TOP5

ifeq ("$(NPU_SW_VERSION)", "v2.0")
  INCLUDE += -I../../viplite-tina/lib/aarch64-none-linux-gnu/v2.0/inc
  LIBS_VIP += -L../../viplite-tina/lib/aarch64-none-linux-gnu/v2.0/ -lNBGlinker -lVIPhal
  MYCFLAGS += -DNPU_SW_VERSION=2
else
  INCLUDE += -I../../viplite-tina/lib/aarch64-none-linux-gnu/v1.13/inc
  LIBS_VIP += -L../../viplite-tina/lib/aarch64-none-linux-gnu/v1.13/ -lVIPlite -lVIPuser
  MYCFLAGS += -DNPU_SW_VERSION=1
endif

$(TARGET): vpm_run.c
	$(CC) $(CFLAGS) $(MYCFLAGS) -g -o $@ $^ $(INCLUDE) $(LIBS) $(LIBS_VIP) -lm

all:$(TARGET) install

install: $(TARGET)
	-@mkdir -p $(INSTALL_PREFIX)/usr/bin
	@cp $(TARGET) $(INSTALL_PREFIX)/usr/bin
	-@mkdir -p $(INSTALL_PREFIX)/etc/npu/vpm_run
	@cp $(INSTALL_PREFIX)/usr/bin/$(TARGET) $(INSTALL_PREFIX)/etc/npu/vpm_run
	@cp operator/sample.txt $(INSTALL_PREFIX)/etc/npu/vpm_run
	@cp operator/input_0.dat $(INSTALL_PREFIX)/etc/npu/vpm_run
	@cp operator/$(NPU_VERSION)/network_binary.nb $(INSTALL_PREFIX)/etc/npu/vpm_run
	@cp readme.txt $(INSTALL_PREFIX)/etc/npu/vpm_run

clean:
	rm -rf $(TARGET) *.o *~

