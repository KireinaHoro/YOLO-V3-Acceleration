#ifndef REGION_LAYER_H_
#define REGION_LAYER_H_

#include "nvUtils.h"

class regionParams {
public:
  int classes; // number of class
  int n;       // number of bbox per location
  int coords;  // number of coords (4)
  int w;       // w (darknet)
  int h;       // h (darknet), in total, we have w*h*n bbox
  int outputs; // outputs (darknet), output dimension of previous layer,

  bool softmax;   // 1 for softmax process
  int background; // background index
};

typedef struct {
  float x, y, w, h;
} box;

void regionLayer_gpu(const int batch, const int C, const int nCells,
                     const int num, const int coords, const int classes,
                     const float *input, float *output, cudaStream_t stream);

void reorgOutput_gpu(const int nBatch, const int nClasses,
                     const int nBboxesPerLoc, const int coords, const int l0_w,
                     const int l0_h, const int nCells,
                     float *dpData_unordered[], float *dpData, const long nData,
                     cudaStream_t stream);
#endif
