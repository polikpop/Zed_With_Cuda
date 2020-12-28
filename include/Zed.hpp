/*
 * @Author: your name
 * @Date: 2020-12-24 08:30:32
 * @LastEditTime: 2020-12-25 15:07:23
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /ZedDriver/ZED.hpp
 */
#include <opencv2/opencv.hpp>
#include <sl/Camera.hpp>
#include <opencv2/cvconfig.h>
#include <vector>

using namespace std;

#define RED 1
#define BLUE 2




int getOCVtype(sl::MAT_TYPE type) 
{
   int cv_type = -1;
   switch (type)
   {
      case sl::MAT_TYPE::F32_C1: cv_type = CV_32FC1; break;
      case sl::MAT_TYPE::F32_C2: cv_type = CV_32FC2; break;
      case sl::MAT_TYPE::F32_C3: cv_type = CV_32FC3; break;
      case sl::MAT_TYPE::F32_C4: cv_type = CV_32FC4; break;
      case sl::MAT_TYPE::U8_C1: cv_type = CV_8UC1; break;
      case sl::MAT_TYPE::U8_C2: cv_type = CV_8UC2; break;
      case sl::MAT_TYPE::U8_C3: cv_type = CV_8UC3; break;
      case sl::MAT_TYPE::U8_C4: cv_type = CV_8UC4; break;
      default: break;
   }
   return cv_type;
}

cv::Mat slMat2cvMat(sl::Mat& input) 
{
    // Since cv::Mat data requires a uchar* pointer, we get the uchar1 pointer from sl::Mat (getPtr<T>())
    // cv::Mat and sl::Mat will share a single memory structure
   return cv::Mat(input.getHeight(), input.getWidth(), getOCVtype(input.getDataType()), input.getPtr<sl::uchar1>(sl::MEM::CPU), input.getStepBytes(sl::MEM::CPU));
}

cv::cuda::GpuMat slMat2cvMatGPU(sl::Mat& input) {
    // Since cv::Mat data requires a uchar* pointer, we get the uchar1 pointer from sl::Mat (getPtr<T>())
    // cv::Mat and sl::Mat will share a single memory structure
    return cv::cuda::GpuMat(input.getHeight(), input.getWidth(), getOCVtype(input.getDataType()), input.getPtr<sl::uchar1>(sl::MEM::GPU), input.getStepBytes(sl::MEM::GPU));
}

class ZED
{
   private:
      sl::Camera zed;
      sl::Resolution image_size; 
      int new_width;
      int new_height;
      sl::Mat point_cloud;
      sl::RuntimeParameters runtime_params;
     
      
   public:

      void init()
      {
         //设置参数
         sl::InitParameters init_params;
         init_params.camera_resolution = sl::RESOLUTION::HD720;
         init_params.depth_mode = sl::DEPTH_MODE::QUALITY;
         init_params.coordinate_units = sl::UNIT::CENTIMETER;
         
         //打开相机
         sl::ERROR_CODE returned_state = zed.open(init_params);
         if (returned_state != sl::ERROR_CODE::SUCCESS) 
         {
            std::cout << "Error " << returned_state << ", exit program.\n";
            exit(1);
         }

         //设置runtime参数
         runtime_params.sensing_mode = sl::SENSING_MODE::STANDARD;
         runtime_params.confidence_threshold = 100;
         runtime_params.texture_confidence_threshold = 100;
         image_size = zed.getCameraInformation().camera_resolution;
         new_width = image_size.width;
         new_height = image_size.height;
      }   
      void setCamera(int a)
      {
         switch (a)
         {
         case 1:
            zed.setCameraSettings(sl::VIDEO_SETTINGS::SATURATION, 8);
            zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE, 2800);
            break;
         
         case 2:
            zed.setCameraSettings(sl::VIDEO_SETTINGS::SATURATION, 8);
            zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE, 5600);
            break;   
         
         default:
            break;
         }
      }
      vector<cv::Mat> getImg()
      {

      
         sl::Mat image_zed(new_width, new_height, sl::MAT_TYPE::U8_C4);
         cv::Mat image_ocv = slMat2cvMat(image_zed);
      
   
      
         cv::Mat depth_image_ocv_m;
         sl::Mat depth_image_zed(new_width, new_height, sl::MAT_TYPE::U8_C4,sl::MEM::GPU);
         cv::cuda::GpuMat depth_image_ocv = slMat2cvMatGPU(depth_image_zed);
         if (zed.grab(runtime_params) == sl::ERROR_CODE::SUCCESS)
         {
            zed.retrieveImage(depth_image_zed, sl::VIEW::DEPTH, sl::MEM::GPU, image_size);
            zed.retrieveImage(image_zed, sl::VIEW::LEFT, sl::MEM::CPU, image_size);
         }

         
         depth_image_ocv.download(depth_image_ocv_m);
         vector<cv::Mat> pictures;
         pictures.push_back(image_ocv);
         pictures.push_back(depth_image_ocv_m);
         return pictures;
      }
      float calcDistance(cv::Point center)
      {
         zed.retrieveMeasure(point_cloud, sl::MEASURE::DEPTH, sl::MEM::GPU, image_size);
         sl::float1 point3D;
         float sumDistance;
         int count = 0;
         for (int i = -5; i < 5; i++)
         {
            for(int j = -5; j < 5; j++)
            {
               point_cloud.getValue(center.x, center.y , &point3D);
               sumDistance = 0.0;
               //cout<<point3D<<endl;
               //sumDistance += sqrt(point3D.x*point3D.x + point3D.y*point3D.y + point3D.z*point3D.z);
               sumDistance += point3D;
               if(point3D==0) continue;
               count++;
            }
         }
         float distance = sumDistance/count;
         return distance;
      }

      cv::cuda::GpuMat depthMask(float dist, float range, cv::cuda::GpuMat temp)
      {
         sl::Mat point_cloud2;
         sl::float1 point;
         zed.retrieveMeasure(point_cloud2, sl::MEASURE::DEPTH, sl::MEM::GPU, image_size);
         cv::cuda::GpuMat depthMask;
         depthMask.upload(cv::Mat::zeros(temp.size(), CV_8UC1));
         for(int i = 0; i < temp.rows; i++)
         {
            for(int j = 0; j < temp.cols; j++)
            {
               point_cloud2.getValue(0.5*new_width - 150 + i, 0.5*new_height - 150 + j, &point);
               float distance;
               distance = point;
               if(abs(distance/100 - dist) < range)
               {
                  depthMask.ptr<uchar>(j)[i] = 1;
               }
            }
         }
         return depthMask;
      }   

      int getWidth()
      {
         return new_width;
      }
      int getHeight()
      {
         return new_height;
      }

};
