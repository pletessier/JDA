#include <omp.h>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <jsmn.hpp>
#include "jda/common.hpp"

using namespace cv;
using namespace std;
using namespace jsmn;

namespace jda {

int Feature::CalcFeatureValue(const Mat& o, const Mat& h, const Mat& q, \
                              const Mat_<double>& s) const {
  double ratio;
  int height, width;
  Mat img;
  switch (scale) {
  case ORIGIN:
    ratio = 1;
    height = o.rows;
    width = o.cols;
    img = o; // ref
    break;
  case HALF:
    ratio = double(h.rows) / double(o.rows);
    height = h.rows;
    width = h.cols;
    img = h; // ref
    break;
  case QUARTER:
    ratio = double(q.rows) / double(o.rows);
    height = q.rows;
    width = q.cols;
    img = q; // ref
    break;
  default:
    dieWithMsg("Unsupported SCALE");
    break;
  }

  double x1, y1, x2, y2;
  x1 = s(0, 2 * landmark_id1) + o.cols*offset1_x;
  y1 = s(0, 2 * landmark_id1 + 1) + o.rows*offset1_y;
  x2 = s(0, 2 * landmark_id2) + o.cols*offset2_x;
  y2 = s(0, 2 * landmark_id2 + 1) + o.rows*offset2_y;
  x1 *= ratio; y1 *= ratio;
  x2 *= ratio; y2 *= ratio;
  int x1_ = int(round(x1));
  int y1_ = int(round(y1));
  int x2_ = int(round(x2));
  int y2_ = int(round(y2));

  checkBoundaryOfImage(width, height, x1_, y1_);
  checkBoundaryOfImage(width, height, x2_, y2_);

  int val = img.at<uchar>(y1_, x1_) - img.at<uchar>(y2_, x2_);
  return val;
}

void LOG(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char msg[256];
  vsprintf(msg, fmt, args);
  va_end(args);

  char buff[256];
  time_t t = time(NULL);
  strftime(buff, sizeof(buff), "[%x - %X]", localtime(&t));
  printf("%s %s\n", buff, msg);
}

void dieWithMsg(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char msg[256];
  vsprintf(msg, fmt, args);
  va_end(args);

  LOG(msg);
  exit(-1);
}

double calcMeanError(const vector<Mat_<double> >& gt_shapes, \
                     const vector<Mat_<double> >& current_shapes) {
  const Config& c = Config::GetInstance();
  const int N = gt_shapes.size();
  const int landmark_n = c.landmark_n;
  double e = 0.;
  Mat_<double> delta_shape;
  for (int i = 0; i < N; i++) {
    delta_shape = gt_shapes[i] - current_shapes[i];
    for (int j = 0; j < landmark_n; j++) {
      e += std::sqrt(std::pow(delta_shape(0, 2 * j), 2) + \
                     std::pow(delta_shape(0, 2 * j + 1), 2));
    }
  }
  e /= landmark_n * N;
  e /= c.img_o_width;
  return e;
}

Mat drawShape(const Mat& img, const Mat_<double>& shape) {
  Mat img_ = img.clone();
  const int landmark_n = shape.cols / 2;
  for (int i = 0; i < landmark_n; i++) {
    circle(img_, Point(shape(0, 2 * i), shape(0, 2 * i + 1)), 2, Scalar(0, 255, 0), -1);
  }
  return img_;
}
Mat drawShape(const Mat& img, const Mat_<double>& shape, const Rect& bbox) {
  Mat img_ = img.clone();
  const int landmark_n = shape.cols / 2;
  rectangle(img_, bbox, Scalar(0, 0, 255), 2);
  for (int i = 0; i < landmark_n; i++) {
    circle(img_, Point(shape(0, 2 * i), shape(0, 2 * i + 1)), 2, Scalar(0, 255, 0), -1);
  }
  return img_;
}

void showImage(const Mat& img) {
  cv::imshow("img", img);
  cv::waitKey(0);
}

Config::Config() {
  jsmn::Object json_config = jsmn::parse("../config.json");

  // model meta data
  T = json_config["T"].unwrap<Number>();
  K = json_config["K"].unwrap<Number>();
  landmark_n = json_config["landmark_n"].unwrap<Number>();
  tree_depth = json_config["tree_depth"].unwrap<Number>();
  shift_size = json_config["random_shift"].unwrap<Number>();

  // image size
  jsmn::Object& image_size_config = json_config["image_size"].unwrap<Object>();
  multi_scale = image_size_config["multi_scale"].unwrap<Boolean>();
  img_o_width = image_size_config["origin_w"].unwrap<Number>();
  img_o_height = image_size_config["origin_h"].unwrap<Number>();
  img_h_width = image_size_config["half_w"].unwrap<Number>();
  img_h_height = image_size_config["half_h"].unwrap<Number>();
  img_q_width = image_size_config["quarter_w"].unwrap<Number>();
  img_q_height = image_size_config["quarter_h"].unwrap<Number>();

  // hard negative mining
  jsmn::Object& mining_config = json_config["hard_negative_mining"].unwrap<Object>();
  x_step = mining_config["x_step"].unwrap<Number>();
  y_step = mining_config["y_step"].unwrap<Number>();
  scale_factor = mining_config["scale"].unwrap<Number>();
  mining_pool_size = omp_get_max_threads();
  esp = 2.2e-16;

  // stage parameters
  jsmn::Object& stages = json_config["stages"].unwrap<Object>();
  this->feats.clear();
  this->radius.clear();
  this->probs.clear();
  for (int i = 0; i < T; i++) {
    this->feats.push_back(stages["feature_pool_size"][i].unwrap<Number>());
    this->nps.push_back(stages["neg_pos_ratio"][i].unwrap<Number>());
    this->radius.push_back(stages["random_sample_radius"][i].unwrap<Number>());
    this->probs.push_back(stages["classification_p"][i].unwrap<Number>());
    this->recall.push_back(stages["recall"][i].unwrap<Number>());
  }

  // data
  jsmn::Object& data = json_config["data"].unwrap<Object>();
  train_pos_txt = data["face"].unwrap<jsmn::String>();
  test_pos_txt = "../data/test.txt";
  train_neg_txt = data["background"].unwrap<jsmn::String>();
  test_neg_txt = "../data/test_nega.txt";
  detection_txt = "../data/detection.txt";

  // status
  if (json_config["phase"].unwrap<jsmn::String>() == "train") phase = 0;
  else phase = 1;

  current_stage_idx = json_config["current_stage_idx"].unwrap<Number>();
  current_cart_idx = json_config["current_cart_idx"].unwrap<Number>();
  tmp_model = json_config["tmp_model"].unwrap<jsmn::String>();
}

} // namespace jda
