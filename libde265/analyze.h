/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * Authors: Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ANALYZE_H
#define ANALYZE_H

#include "libde265/nal-parser.h"
#include "libde265/decctx.h"
#include "libde265/encode.h"
#include "libde265/slice.h"
#include "libde265/scan.h"
#include "libde265/intrapred.h"
#include "libde265/transform.h"
#include "libde265/fallback-dct.h"
#include "libde265/quality.h"
#include "libde265/fallback.h"
#include "libde265/configparam.h"
#include "libde265/encode.h"


/*  Encoder search tree, bottom up:

    - Algo_CB_IntraPartMode - choose between NxN and 2Nx2N intra parts

    - Algo_CB_Split - whether CB is split or not

    - Algo_CTB_QScale - select QScale on CTB granularity
 */


// --- CB intra NxN vs. 2Nx2N decision ---

class Algo_CB_IntraPartMode
{
 public:
  virtual ~Algo_CB_IntraPartMode() { }

  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CbSize, int ctDepth, int qp) = 0;
};

/* Try both NxN, 2Nx2N and choose better one.
 */
class Algo_CB_IntraPartMode_BruteForce : public Algo_CB_IntraPartMode
{
 public:
  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CbSize, int ctDepth, int qp);
};

/* Always use choose selected part mode.
   If NxN is chosen but cannot be applied (CB tree not at maximum depth), 2Nx2N is used instead.
 */
class Algo_CB_IntraPartMode_Fixed : public Algo_CB_IntraPartMode
{
 public:
 Algo_CB_IntraPartMode_Fixed() { }

  struct params
  {
  params() : partMode(PART_2Nx2N) { }

    enum PartMode partMode;
  };

  void setParams(const params& p) { mParams=p; }

  virtual enc_cb* analyze(encoder_context* ectx,
                          context_model_table ctxModel,
                          const de265_image* input,
                          int x0,int y0, int log2CbSize, int ctDepth,
                          int qp);

 private:
  params mParams;
};


// --- CB split decision ---

class Algo_CB_Split
{
 public:
  virtual ~Algo_CB_Split() { }

  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CbSize, int ctDepth, int qp) = 0;

  // TODO: probably, this will later be a intra/inter decision which again
  // has two child algorithms, depending on the coding mode.
  void setChildAlgo(Algo_CB_IntraPartMode* algo) { mIntraPartModeAlgo = algo; }

 protected:
  Algo_CB_IntraPartMode* mIntraPartModeAlgo;
};

class Algo_CB_Split_BruteForce : public Algo_CB_Split
{
 public:
  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CtbSize, int ctDepth, int qp);
};



// --- choose a qscale at CTB level ---

class Algo_CTB_QScale
{
 public:
 Algo_CTB_QScale() : mChildAlgo(NULL) { }
  virtual ~Algo_CTB_QScale() { }

  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CtbSize, int ctDepth) = 0;

  void setChildAlgo(Algo_CB_Split* algo) { mChildAlgo = algo; }

 protected:
  Algo_CB_Split* mChildAlgo;
};

class Algo_CTB_QScale_Constant : public Algo_CTB_QScale
{
 public:
  struct params
  {
  params() : mQP(27) { }

    int mQP;
  };

  void setParams(const params& p) { mParams=p; }


  virtual enc_cb* analyze(encoder_context*,
                          context_model_table,
                          const de265_image* input,
                          int ctb_x,int ctb_y,
                          int log2CtbSize, int ctDepth);

  int getQP() const { return mParams.mQP; }

 private:
  params mParams;
};



// --- an encoding algorithm combines a set of algorithm modules ---

class EncodingAlgorithm
{
 public:
  virtual ~EncodingAlgorithm() { }

  virtual Algo_CTB_QScale* getAlgoCTBQScale() = 0;

  virtual int getPPS_QP() const = 0;
  virtual int getSlice_QPDelta() const { return 0; }
};


class EncodingAlgorithm_Custom : public EncodingAlgorithm
{
 public:

  void setParams(encoder_context& ectx);

  virtual Algo_CTB_QScale* getAlgoCTBQScale() { return &mAlgo_CTB_QScale_Constant; }

  virtual int getPPS_QP() const { return mAlgo_CTB_QScale_Constant.getQP(); }

 private:
  Algo_CTB_QScale_Constant         mAlgo_CTB_QScale_Constant;
  Algo_CB_Split_BruteForce         mAlgo_CB_Split_BruteForce;

  Algo_CB_IntraPartMode_BruteForce mAlgo_CB_IntraPartMode_BruteForce;
  Algo_CB_IntraPartMode_Fixed      mAlgo_CB_IntraPartMode_Fixed;
};



enum IntraPredMode find_best_intra_mode(de265_image& img,int x0,int y0, int log2BlkSize, int cIdx,
                                        const uint8_t* ref, int stride);

enc_cb* encode_cb_no_split(encoder_context*, context_model_table ctxModel, const de265_image* input,
                           int x0,int y0, int log2CbSize, int ctDepth, int qp);

enc_cb* encode_cb_split(encoder_context*, context_model_table ctxModel, const de265_image* input,
                        int x0,int y0, int Log2CbSize, int ctDepth, int qp);

enc_cb* encode_cb_may_split(encoder_context*, context_model_table ctxModel,
                            const de265_image* input,
                            int x0,int y0, int Log2CtbSize, int ctDepth, int qp);

double encode_image(encoder_context*, const de265_image* input, int qp);

void encode_sequence(encoder_context*);

#endif
