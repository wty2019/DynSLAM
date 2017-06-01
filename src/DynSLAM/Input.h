
#ifndef DYNSLAM_INPUT_H
#define DYNSLAM_INPUT_H

#include <string>
#include <sys/stat.h>
#include <highgui.h>

#include "DepthEngine.h"
#include "Utils.h"
#include "../InfiniTAM/InfiniTAM/ITMLib/Utils/ITMLibDefines.h"
#include "../InfiniTAM/InfiniTAM/ITMLib/Objects/ITMRGBDCalib.h"
#include "../InfiniTAM/InfiniTAM/ITMLib/Utils/ITMCalibIO.h"
#include "../InfiniTAM/InfiniTAM/Utils/FileUtils.h"

namespace dynslam {

// TODO move to utility
inline bool file_exists(const std::string& name) {
  struct stat buffer;
  return stat(name.c_str(), &buffer) == 0;
}

// TODO do not depend on infinitam objects
ITMLib::Objects::ITMRGBDCalib ReadITMCalibration(const std::string& fpath);

// TODO(andrei): Better name and docs for this once the interface is fleshed out.
// TODO(andrei): Move code to cpp.
class Input {
 public:
  Input(const std::string &dataset_folder,
        DepthEngine *depth_engine,
        const ITMLib::Objects::ITMRGBDCalib &calibration)
      : depth_engine_(depth_engine),
        dataset_folder_(dataset_folder),
        frame_idx_(0),
        calibration_(calibration) {}

  bool HasMoreImages() {
    std::string next_fpath = GetRgbFrameName(
        dataset_folder_ + "/image_0/",
        "%06d.png",
        frame_idx_);
    std::cout << next_fpath << std::endl;
    return file_exists(next_fpath);
  }

  /// \brief Converts an OpenCV RGB Mat into an InfiniTAM image.
  void CvToItm(const cv::Mat &mat, ITMUChar4Image *out_rgb) {
    Vector2i newSize(mat.cols, mat.rows);
    out_rgb->ChangeDims(newSize);
    Vector4u *data_ptr = out_rgb->GetData(MEMORYDEVICE_CPU);

    for (int i = 0; i < mat.rows; ++i) {
      for (int j = 0; j < mat.cols; ++j) {
        int idx = i * mat.cols + j;
        // Convert from OpenCV's standard BGR format to RGB.
        data_ptr[idx].r = mat.data[idx * 3 + 2];
        data_ptr[idx].g = mat.data[idx * 3 + 1];
        data_ptr[idx].b = mat.data[idx * 3 + 0];
        data_ptr[idx].a = 255u;
      }
    }

    // This does not currently work because the input images lack the alpha channel.
//    memcpy(data_ptr, mat.data, mat.rows * mat.cols * 4 * sizeof(unsigned char));
  }

  /// \brief Converts an OpenCV depth Mat into an InfiniTAM depth image.
  void CvToItm(const cv::Mat1s &mat, ITMShortImage *out_depth) {
    short *data_ptr = out_depth->GetData(MEMORYDEVICE_CPU);
    out_depth->ChangeDims(Vector2i(mat.cols, mat.rows));
    memcpy(data_ptr, mat.data, mat.rows * mat.cols * sizeof(short));
  }

  // TODO get rid of this and use some other format
  /// \brief Reads from the input folders into the specified InfiniTAM buffers.
  /// \return True if the images could be loaded and processed appropriately.
  bool GetITMImages(ITMUChar4Image *rgb, ITMShortImage *raw_depth) {
    std::string left_folder = dataset_folder_ + "/image_2";
    std::string right_folder = dataset_folder_ + "/image_3";
    std::string rgb_frame_fname_format = "%06d.png";
    std::string left_frame_fpath = GetRgbFrameName(left_folder, rgb_frame_fname_format, frame_idx_);
    std::string right_frame_fpath = GetRgbFrameName(right_folder, rgb_frame_fname_format, frame_idx_);
    left_frame_buf_ = cv::imread(left_frame_fpath);
    right_frame_buf_ = cv::imread(right_frame_fpath);

    // Sanity checks to ensure the dimensions from the calibration file and the actual image
    // dimensions correspond.
    const auto &rgb_size = GetRgbSize();
    if (left_frame_buf_.rows != rgb_size.height || left_frame_buf_.cols != rgb_size.width) {
      std::cerr << "Unexpected left RGB frame size. Got " << left_frame_buf_.size() << ", but "
                << "the calibration file specified " << rgb_size << ".";
      return false;
    }

    if (right_frame_buf_.rows != rgb_size.height || right_frame_buf_.cols != rgb_size.width) {
      std::cerr << "Unexpected right RGB frame size. Got " << right_frame_buf_.size() << ", but "
                << "the calibration file specified " << rgb_size << ".";
      return false;
    }

    // The left frame is the RGB input to our system.
    CvToItm(left_frame_buf_, rgb);

    depth_engine_->DisparityMapFromStereo(left_frame_buf_, right_frame_buf_, depth_buf_);

    const auto &depth_size = GetDepthSize();
    if (depth_buf_.rows != depth_size.height || depth_buf_.cols != depth_size.width) {
      std::cerr << "Unexpected depth map size. Got [" << depth_buf_.size() << "], but the "
                << "calibration file specified [" << depth_size << "].";
      return false;
    }

    // TODO(andrei): Make sure you actually use this. ATM, libelas-tooling's kitti2klg does the
    // depth from disparity calculation!
//    StereoCalibration stereo_calibration(0, 0);
//    depth_engine_->DepthFromDisparityMap(disparity, stereo_calibration, depth);
    CvToItm(depth_buf_, raw_depth);

    frame_idx_++;
    return true;
  }

  ITMLib::Objects::ITMRGBDCalib GetITMCalibration() {
    std::cerr << "Warning: Using deprecated ITM calibration accessor!" << std::endl;
    return calibration_;
  };

  cv::Size2i GetRgbSize() {
    return cv::Size2i(static_cast<int>(calibration_.intrinsics_rgb.sizeX),
                     static_cast<int>(calibration_.intrinsics_rgb.sizeY));
  }

  /// \note The ordering of the coordinates, in accordance with the OpenCV conventions.
  // TODO remove this comment

  cv::Size2i GetDepthSize() {
    return cv::Size2i(static_cast<int>(calibration_.intrinsics_d.sizeX),
                      static_cast<int>(calibration_.intrinsics_d.sizeY));
  }

 private:
  DepthEngine *depth_engine_;

  std::string dataset_folder_;
  int frame_idx_;

  cv::Mat left_frame_buf_;
  cv::Mat right_frame_buf_;
  cv::Mat depth_buf_;
  cv::Mat disparity_buf_;

  // TODO get rid of this
  ITMLib::Objects::ITMRGBDCalib calibration_;

  // TODO dedicated subclass for reading stereo input
  std::string GetRgbFrameName(std::string folder, std::string fname_format, int frame_idx) const {
    return folder + "/" + utils::Format(fname_format, frame_idx);
  }

};

} // namespace dynslam

#endif //DYNSLAM_INPUT_H