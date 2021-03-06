DEBUG := 1
NVPROFILER := 0

#-DVOC  # model trained on VOC
#-Dcal_mAP  # calculate mAP
#-DPRINT_LOG  # print the prediction result
#-DVISULIZATION # draw boxes on the image && save
CUSTOM_MICRO := -DVOC -DPRINT_LOG #  -DVISULIZATION

GCC := g++
CCFLAGS := -std=c++11 -O3 $(CUSTOM_MICRO)
NVCC := /usr/local/cuda/bin/nvcc
# Choose your arch for fast compilation, 
# sm_60 and sm_61 are for pascal gpu,
# sm_30 and sm_35 are for Tesla K40 gpu
NVCC_FLAGS := -gencode arch=compute_72,code=compute_72
ifeq ($(DEBUG), 1)
	CCFLAGS += -g
	NVCC_FLAGS += -G 
endif
ifeq ($(NVPROFILER), 1)
	NVCC_FLAGS += -lineinfo
endif
NVCC_FLAGS += $(CCFLAGS)

TENSORRT_VERSION := 212GA
SRC_PATH := ./src
INC_PATH := ./include

TENSORRT_INC_PATH := /usr/include/aarch64-linux-gnu
TENSORRT_LIB_PATH := /usr/lib/aarch64-linux-gnu

INCLUDES := -I$(SRC_PATH) -I$(INC_PATH) -I$(TENSORRT_INC_PATH) -I/usr/local/cuda/include -I/usr/local/include

LDPATH :=  -L/usr/local/lib -L/usr/lib -L$(TENSORRT_LIB_PATH) -L/usr/local/cuda/lib64 -Wl,-rpath,$(TENSORRT_LIB_PATH) 
LDFLAGS := $(LDPATH) -ldl -lcudart -lcudnn -lnvinfer -lnvcaffe_parser $(shell pkg-config opencv --libs)

OBJ_PATH := ./bin/obj
BIN_PATH := ./bin
EXE_FILE := runYOLOv3

all: build

build: $(BIN_PATH)/$(EXE_FILE)

$(OBJ_PATH)/tensorRTClassifier.o: $(SRC_PATH)/tensorRTClassifier.cpp
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

$(OBJ_PATH)/main.o: $(SRC_PATH)/main.cpp
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

$(OBJ_PATH)/interpPlugin.o: $(SRC_PATH)/interpPlugin.cu
	$(NVCC) $(NVCC_FLAGS) $(INCLUDES) -o $@ -c $<
	
$(OBJ_PATH)/bboxParser.o: $(SRC_PATH)/bboxParser.cu
	$(NVCC) $(NVCC_FLAGS) $(INCLUDES) -o $@ -c $<

$(OBJ_PATH)/regionLayer.o: $(SRC_PATH)/regionLayer.cu
	$(NVCC) $(NVCC_FLAGS) $(INCLUDES) -o $@ -c $<
	
$(OBJ_PATH)/common.o: $(SRC_PATH)/common.cu
	$(NVCC) $(NVCC_FLAGS) $(INCLUDES) -o $@ -c $<

$(BIN_PATH)/$(EXE_FILE): $(OBJ_PATH)/tensorRTClassifier.o $(OBJ_PATH)/main.o $(OBJ_PATH)/interpPlugin.o $(OBJ_PATH)/bboxParser.o $(OBJ_PATH)/regionLayer.o $(OBJ_PATH)/common.o
	$(GCC) $+ $(CCFLAGS)  $(LDFLAGS) -o $@

clean:
	rm -rf $(OBJ_PATH)/* $(BIN_PATH)/$(EXE_FILE)
