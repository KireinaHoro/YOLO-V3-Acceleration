/*
 * Copyright 1993-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO USER:
 *
 * This source code is subject to NVIDIA ownership rights under U.S. and
 * international Copyright laws.
 *
 * NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
 * CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR
 * IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
 * OR PERFORMANCE OF THIS SOURCE CODE.
 *
 * U.S. Government End Users.  This source code is a "commercial item" as
 * that term is defined at 48 C.F.R. 2.101 (OCT 1995), consisting  of
 * "commercial computer software" and "commercial computer software
 * documentation" as such terms are used in 48 C.F.R. 12.212 (SEPT 1995)
 * and is provided to the U.S. Government only as a commercial end item.
 * Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
 * 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the
 * source code with only those rights set forth herein.
 */

#include <cuda.h>
#include <cuda_runtime.h>
#include <fstream>
#include <iostream>

#include "helper_cuda.h"
#include "interpPlugin.h"
#include "nvUtils.h"
#include "preproc_yolov3.h"
#include "procInferOutput.h"
#include "tensorRTClassifier.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

int g_devID = 0;
int g_nBatchSize = 0;
int g_nIteration = 0;
float nmsThreshold = 0.0;
float confThreshold = 0.0;

char *g_imageList = nullptr;
char *g_deployFile = nullptr;
char *g_modelFile = nullptr;
char *g_meanFile = nullptr;
char *g_synsetFile = nullptr;
char *g_calibrationTable = nullptr;

std::vector<std::string> g_vSynsets;
simplelogger::Logger *logger =
    simplelogger::LoggerFactory::CreateConsoleLogger();
bool parseArg(int argc, char **argv);
double testInference(IClassifier *pClassifiler, image im,
                     const std::string imageIndex);

int main(int argc, char **argv) {
  bool ret = parseArg(argc, argv);
  if (!ret) {
    LOG_ERROR(logger, "Error in parse Args!");
    return 1;
  }

  // image list
  std::ifstream in(g_imageList);
  std::string imageName;

  // TensorRT inference
  std::string inputLayerName{"data"};
#if 1 // VOC_DEMO
  std::vector<std::string> outputLayerNames{"conv81", "conv93", "conv105"};
#endif

  PluginFactory pluginFactory;
  std::string table;
  if (nullptr == g_calibrationTable) {
    table = std::string();
  } else {
    table = std::string(g_calibrationTable);
  }
  TensorRTClassifier *pTensorRTClassifier = new TensorRTClassifier(
      g_deployFile, g_modelFile, g_meanFile, inputLayerName, outputLayerNames,
      g_nBatchSize, g_devID, &pluginFactory, table);
  assert(nullptr != pTensorRTClassifier);

  LOG_DEBUG(logger, "");
  LOG_DEBUG(logger, "Inference Result: ");
  double t_tensorRT = .0;
  long cnt = 0;
  if (in) {
    while (getline(in, imageName)) {
      size_t startLoc = imageName.find_last_of('/');
      size_t endLoc = imageName.find_first_of('.');
      std::string imageIndex =
          imageName.substr(startLoc + 1, endLoc - startLoc - 1);
      image img = load_image_color((char *)(imageName.c_str()), 0, 0);
      t_tensorRT += testInference(pTensorRTClassifier, img, imageIndex);
      free_image(img);
      ++cnt;
    }
  }
  in.close();

  delete pTensorRTClassifier;
  pluginFactory.destroyPlugin();

  LOG_DEBUG(logger, "");
  LOG_DEBUG(logger, "Performance:");
  LOG_DEBUG(logger, "TensorRT Inference Time: " << t_tensorRT * 1000.0
                                                << " ms, with " << cnt
                                                << " pictures processed.");

  // free_image(img);
  return 0;
}

double testInference(IClassifier *pClassifiler, image img,
                     const std::string imageIndex) {
  // network width and height
  int nWidth = pClassifiler->getInferWidth();
  int nHeight = pClassifiler->getInferHeight();

  // use image letterbox from darknet.
  image imgResized = letterbox_image(img, nWidth, nHeight);

  // a batch of images (same image)
  float *pBGR = nullptr;
  pBGR = (float *)malloc(g_nBatchSize * 3 * nWidth * nHeight * sizeof(float));
  assert(nullptr != pBGR);
  for (int i = 0; i < g_nBatchSize; ++i) {
    float *ptr = pBGR + i * 3 * nWidth * nHeight;
    memcpy(ptr, imgResized.data, nWidth * nHeight * 3 * sizeof(float));
  }

  pClassifiler->setInputData(pBGR, nWidth, nHeight, g_nBatchSize);

  // Forward
  StopWatch timer;
  INFER_OUTPUT_PARAMS inferOutputParams;
  timer.Start();
  for (int iter = 0; iter < g_nIteration; ++iter) {
    pClassifiler->forward(&inferOutputParams);
  }
  double t = timer.Stop() / g_nIteration;

  std::string output;
  if (nullptr == g_calibrationTable) {
    output = std::string("predictions_fp32");
  } else {
    output = std::string("predictions_int8");
  }
  t += procInferOutput_yolov2(&inferOutputParams, img, g_vSynsets, output,
                              imageIndex, nmsThreshold, confThreshold);

  free(pBGR);
  free_image(imgResized);
  return t;
}

bool parseArg(int argc, char **argv) {
  bool ret;

  int nDevs = 0;
  ck(cudaGetDeviceCount(&nDevs));
  if (0 == nDevs) {
    LOG_ERROR(logger, "Warning: No CUDA capable device!");
    exit(-1);
  }

  g_devID = getCmdLineArgumentInt(argc, (const char **)argv, "devID");
  if (g_devID < 0 || g_devID >= nDevs) {
    LOG_ERROR(logger, "Warning: No such GPU device!");
    return false;
  }
  cudaDeviceProp deviceProp;
  cudaGetDeviceProperties(&deviceProp, g_devID);

  LOG_DEBUG(logger, "Device ID: " << g_devID);
  LOG_DEBUG(logger, "Device Name: " << deviceProp.name);

  g_nBatchSize = getCmdLineArgumentInt(argc, (const char **)argv, "batchSize");
  if (g_nBatchSize <= 0) {
    return false;
  }
  LOG_DEBUG(logger, "Batch size: " << g_nBatchSize);

  nmsThreshold =
      getCmdLineArgumentFloat(argc, (const char **)argv, "nmsThreshold");
  if (nmsThreshold <= 0) {
    return false;
  }
  LOG_DEBUG(logger, "nmsThreshold: " << nmsThreshold);

  confThreshold =
      getCmdLineArgumentFloat(argc, (const char **)argv, "confThreshold");
  if (confThreshold <= 0) {
    return false;
  }
  LOG_DEBUG(logger, "confThreshold: " << confThreshold);

  ret = getCmdLineArgumentString(argc, (const char **)argv, "imageFile",
                                 &g_imageList);
  if (!ret) {
    LOG_ERROR(logger, "No Image file!");
    return ret;
  }

  ret = getCmdLineArgumentString(argc, (const char **)argv, "deployFile",
                                 &g_deployFile);
  if (!ret) {
    LOG_ERROR(logger, "No deploy file!");
    return ret;
  }

  ret = getCmdLineArgumentString(argc, (const char **)argv, "modelFile",
                                 &g_modelFile);
  if (!ret) {
    LOG_ERROR(logger, "No Model file!");
    return ret;
  }

  ret = getCmdLineArgumentString(argc, (const char **)argv, "meanFile",
                                 &g_meanFile);
  if (!ret) {
    LOG_WARN(logger, "No Mean file.");
    // return ret;
  }

  ret = getCmdLineArgumentString(argc, (const char **)argv, "synsetFile",
                                 &g_synsetFile);
  if (!ret) {
    LOG_WARN(logger, "No synset files.");
    return false;
  }
  std::ifstream iLabel(g_synsetFile);
  if (iLabel.is_open()) {
    std::string line;
    while (std::getline(iLabel, line)) {
      g_vSynsets.push_back(line);
      line.clear();
    }
    iLabel.close();
  } else {
    LOG_ERROR(logger, "Failed to open synset file " << g_synsetFile);
    return false;
  }

  ret = getCmdLineArgumentString(argc, (const char **)argv, "cali",
                                 &g_calibrationTable);
  if (!ret) {
    LOG_WARN(logger, "No calibration table.");
  }

  g_nIteration = getCmdLineArgumentInt(argc, (const char **)argv, "nIters");
  if (g_nIteration <= 0) {
    return false;
  }
  LOG_DEBUG(logger, "Iterations: " << g_nIteration);

  return true;
}
