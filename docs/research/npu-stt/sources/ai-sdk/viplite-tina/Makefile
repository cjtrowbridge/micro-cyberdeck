
ifdef AI_SDK_PLATFORM
include ../machinfo/$(AI_SDK_PLATFORM)/config.mk
endif

all:$(TARGET) install

install: $(TARGET)
	-@mkdir -p $(INSTALL_PREFIX)/usr/lib
	-@mkdir -p $(INSTALL_PREFIX)/usr/include
	@cp lib/$(C_LIB_TYPE)/$(NPU_SW_VERSION)/inc/*.h $(INSTALL_PREFIX)/usr/include
	@cp lib/$(C_LIB_TYPE)/$(NPU_SW_VERSION)/*.so $(INSTALL_PREFIX)/usr/lib

clean:
	rm -rf *.o *~
