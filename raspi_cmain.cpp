// Copyright 2016

#include <process.h>
#include <Windows.h>
#include <opencv/cv.h>
#include <opencv/cxcore.h>
#include "raspi_cmain.h"

enum {kImageWait = 100};

const bool kDebug = false;

// Address and port of the Raspberry Pi.
const char* kCameraAddr = "192.168.42.1";
const char* kCameraPort = "12345";

// Mouse callback function to show the color of the selected pixel.
void OnMouseEvent(int event, int x, int y, int flags, void* param) {
  if (kDebug)
    std::cerr << "OnMouseEvent(" << event << ", " << x << ", " << y
        << ", " << flags << ", " << param << ")\n";
  IplImage* image = static_cast<IplImage*>(param);
  double red = 0, green = 0, blue = 0;
  double luminance = 0, chroma_blue = 0, chroma_red = 0;
  CvScalar ColorData;
  switch (event) {
    case CV_EVENT_LBUTTONDOWN:
      ColorData = cvGet2D(image, y, x);
      blue = ColorData.val[0];
      green = ColorData.val[1];
      red = ColorData.val[2];
      luminance = ((0.299 * red) + (0.587 * green) + (0.114 * blue));
      chroma_blue = (0.5643 * (blue - luminance) + 128);
      chroma_red = (0.7132 * (red - luminance) + 128);
      printf("x = %3d, y = %3d\n\n", x, y);
      printf("B = %5f, G = %5f, R = %5f\n\n", blue, green, red);
      printf("Y = %5f, Cb = %5f, Cr = %5f\n\n",
             luminance, chroma_blue, chroma_red);
      return;
    default:
      return;
  }
}

// Assign a mask.
void MaskField(IplImage *mask, IplImage *roiImage) {
  CvPoint pt1, pt2;
  pt1.x = roiImage->width / 2;
  pt1.y = 0;
  pt2.x = 0;
  pt2.y = roiImage->height / 4;
  cvLine(mask, pt1, pt2, cvScalar(0), 2, 8);
  pt1.x = 0;
  pt1.y = 0;
  pt2.x = 0;
  pt2.y = roiImage->height / 4;
  cvLine(mask, pt1, pt2, cvScalar(0), 2, 8);
  pt1.x = roiImage->width;
  pt1.y = 0;
  pt2.x = roiImage->width;
  pt2.y = roiImage->height / 4;
  cvLine(mask, pt1, pt2, cvScalar(0), 2, 8);
  pt1.x = 0;
  pt1.y = 0;
  pt2.x = roiImage->width;
  pt2.y = 0;
  cvLine(mask, pt1, pt2, cvScalar(0), 2, 8);
  pt1.x = roiImage->width / 2;
  pt1.y = 0;
  pt2.x = roiImage->width;
  pt2.y = roiImage->height / 4;
  cvLine(mask, pt1, pt2, cvScalar(0), 2, 8);
  CvScalar lo_diff = cvScalarAll(10);
  CvScalar up_diff = cvScalarAll(10);
  CvConnectedComp comp;
  int floodFlags = 4 | CV_FLOODFILL_FIXED_RANGE;
  cvFloodFill(mask, cvPoint(5, 5), cvScalar(0),
              lo_diff, up_diff, &comp, floodFlags);
  cvFloodFill(mask, cvPoint(roiImage->width - 5, 5), cvScalar(0),
              lo_diff, up_diff, &comp, floodFlags);
}

// Processes the source_image and returns the result_image.
IplImage* ProcessImage(IplImage* source_image) {
  int R = 0, G = 0, B = 0;
  int Y = 0, Cb = 0, Cr = 0;
  int x = 0, y = 0, row = 0, col = 0;
  int roi_col = 0;
  double edge_darkness = 0, lane_darkness = 0, mask_darkness = 0;
  // The result of image processing. A 3-channel color image.
  IplImage* result_image = cvCreateImage(cvGetSize(source_image),
                                         IPL_DEPTH_8U, 3);
  cvCopy(source_image, result_image, 0);
  // Rectangular roi for traffic light detection.
  // cvRectangle(result_image, cvPoint(0, 0),
  //             cvPoint(source_image->width-1, (source_image->height / 2)-1),
  //                     CV_RGB(0, 255, 0), 2);
  // Rectangular roi for lane detection.
  // cvRectangle(result_image, cvPoint(0, source_image->height/2),
  //             cvPoint(source_image->width-1, source_image->height-1),
  //                     CV_RGB(0, 0, 255), 2);
  // Rectangular roi for lane detection.
  CvRect roi = cvRect(0, result_image->height / 2, result_image->width,
                      result_image->height / 2);
  cvSetImageROI(result_image, roi);
  // The image inside the roi. A 3-channel color image.
  IplImage* roi_image = cvCreateImage(cvSize(roi.width, roi.height),
                                      IPL_DEPTH_8U, 3);
  cvCopy(result_image, roi_image);
  cvResetImageROI(result_image);
  // Convert roi_image to 0 ~ 255 gray scale image.
  // Data type is IPL_DEPTH_8U. A 1-channel gray image.
  IplImage* gray_image = cvCreateImage(cvGetSize(roi_image), IPL_DEPTH_8U, 1);
  cvCvtColor(roi_image, gray_image, CV_BGR2GRAY);
  // Convert gray_image to 0. ~ 1. gray scale image.
  // Data type is IPL_DEPTH_32F
  IplImage *img_32f = cvCreateImage(cvGetSize(gray_image), IPL_DEPTH_32F, 1);
  // Divide the gray_image data by 255.
  cvConvertScale(gray_image, img_32f, 1.0 / 255.0, 0);
  cvSmooth(img_32f, img_32f, CV_GAUSSIAN, 5);
  // Sobel edge detection.
  // Differentiation result of img_32f by x.
  IplImage *diff_x = cvCreateImage(cvGetSize(img_32f), IPL_DEPTH_32F, 1);
  cvSobel(img_32f, diff_x, 1, 0, 3);
  // Differentiation result of img_32f by y.
  IplImage *diff_y = cvCreateImage(cvGetSize(img_32f), IPL_DEPTH_32F, 1);
  cvSobel(img_32f, diff_y, 0, 1, 3);
  // Convert to polar coordinates.
  IplImage *mag = cvCreateImage(cvGetSize(img_32f), IPL_DEPTH_32F, 1);
  IplImage *ori = cvCreateImage(cvGetSize(img_32f), IPL_DEPTH_32F, 1);
  cvCartToPolar(diff_x, diff_y, mag, ori, 1);
  // Canny edge detection.
  IplImage *edge_image = cvCreateImage(cvGetSize(roi_image), IPL_DEPTH_8U, 1);
  cvCanny(gray_image, edge_image, 50, 200, 3);
  // red, yellow, green structs for detecting colors of traffic lights.
  ColorDetect red, yellow, green;
  CvScalar color_data;
  red.count = 1;
  red.average_x = 0;
  red.average_y = 0;
  yellow.count = 1;
  yellow.average_x = 0;
  yellow.average_y = 0;
  green.count = 1;
  green.average_x = 0;
  green.average_y = 0;
  // Square the img_32f image data to emphasize brightness.
  cvPow(img_32f, img_32f, 2);
  IplImage *lane_gray_image = cvCreateImage(cvGetSize(roi_image),
                                            IPL_DEPTH_8U, 1);
  // Set lane_gray_image to be white.
  cvSet(lane_gray_image, cvScalar(255));
  IplImage* mask = cvCreateImage(cvGetSize(roi_image), IPL_DEPTH_8U, 1);
  // Set mask to be white.
  cvSet(mask, cvScalar(255));
  MaskField(mask, roi_image);
  // Traffic light color detection.
  // Detect color only from the upper half of the image.
  // First divide the image into 40x40 areas.
  for (y = 0; y < result_image->height / 2; y += 40) {
    for (x = 0; x < result_image->width; x += 40) {
      // Search for colors inside the 40x40 area.
      for (col = 0; col < 40; col++) {
        for (row = 0; row < 40; row++) {
          // Coordinate data of detected colors.
          color_data = cvGet2D(source_image, y + col, x + row);
          B = cvRound(color_data.val[0]);   // channel B
          G = cvRound(color_data.val[1]);   // channel G
          R = cvRound(color_data.val[2]);   // channel R
          // Convert BGR to YCbCr
          Y = cvRound((0.299 * R) + (0.587 * G) + (0.114 * B));
          Cb = cvRound(0.5643 * (B - Y) + 128);
          Cr = cvRound(0.7132 * (R - Y) + 128);
          if ((105 < Cb && Cb < 140) && (50 < Cr && Cr < 90)) {   // green
            green.count++;
            green.average_x += x + row;
            green.average_y += y + col;
          }
          if ((20 <Cb && Cb < 70) && (130 < Cr && Cr < 150)) {    // yellow
            yellow.count++;
            yellow.average_x += x + row;
            yellow.average_y += y + col;
          }
          if ((90 <Cb && Cb < 120) && (190 < Cr && Cr < 215)) {   // red
            red.count++;
            red.average_x += x + row;
            red.average_y += y + col;
          }
        }
      }
      // Calculate the average of the coordinates.
      red.average_x /= red.count;
      red.average_y /= red.count;
      yellow.average_x /= yellow.count;
      yellow.average_y /= yellow.count;
      green.average_x /= green.count;
      green.average_y /= green.count;
      // Draw a circle with the center at the average location of the pixels
      // with the corresponding color.
      if (red.count > 100) {
        cvCircle(result_image, cvPoint(red.average_x, red.average_y),
                 7, CV_RGB(255, 0, 0), 2);
        // cvRectangle(result_image, cvPoint(x, y),
        //             cvPoint(x+row, y+col), CV_RGB(255, 0, 0), 2);
      }
      if (yellow.count > 100) {
        cvCircle(result_image, cvPoint(yellow.average_x, yellow.average_y),
                 7, CV_RGB(255, 255, 0), 2);
        // cvRectangle(result_image, cvPoint(x, y),
        //             cvPoint(x+row, y+col), CV_RGB(255, 255, 0), 2);
      }
      if (green.count > 80) {
        cvCircle(result_image, cvPoint(green.average_x, green.average_y),
                 7, CV_RGB(0, 255, 0), 2);
        // cvRectangle(result_image, cvPoint(x, y),
        //             cvPoint(x+row, y+col), CV_RGB(0, 255, 0), 2);
      }
      red.count = 1;
      yellow.count = 1;
      green.count = 1;
    }
  }
  for (y = 0; y < (result_image->height); y++) {
    for (x = 0; x < (result_image->width); x++) {
      if (y < result_image->height / 2) {
        // Upper half of the image to detect traffic lights.
        // TODO(bits_and_scraps): Trafic light detection algorithm.
      } else {
        // Lower half of the image to detect lanes.
        roi_col = y - (result_image->height / 2);
        lane_darkness = cvGetReal2D(img_32f, roi_col, x);
        edge_darkness = cvGetReal2D(mag, roi_col, x);
        mask_darkness = cvGetReal2D(mask, roi_col, x);
        if (lane_darkness < 0.1 &&
            edge_darkness > 0.2 && mask_darkness > 200) {
          // cvSet2D(roi_image, roi_col, x, CV_RGB(255,0,255));
          cvSet2D(result_image, y, x, CV_RGB(255, 0, 255));
        }
      }
    }
  }
  // Turn edge_image into a straight line using cvHoughLines2.
  // CV_HOUGH_STANDARD MODE
  CvMemStorage* storage = cvCreateMemStorage(0);
  CvSeq* seq_lines;
  seq_lines = cvHoughLines2(edge_image, storage, CV_HOUGH_STANDARD,
                            1, CV_PI / 180, 100, 0, 0);
  for (int k = 0; k < MIN(seq_lines->total, 100); ++k) {
    float* line;
    float rho, theta;
    float c, s;
    float x0, y0;
    line = reinterpret_cast<float*>(cvGetSeqElem(seq_lines, k));
    rho = line[0];
    theta = line[1];
    // drawing line
    c = cos(theta);
    s = sin(theta);
    x0 = rho*c;
    y0 = rho*s;
    CvPoint pt1, pt2;
    pt1.x = cvRound(x0 + 1000 * (-s));
    pt1.y = cvRound(y0 + 1000 * (c));
    pt2.x = cvRound(x0 - 1000 * (-s));
    pt2.y = cvRound(y0 - 1000 * (c));
    cvLine(lane_gray_image, pt1, pt2, CV_RGB(255, 255, 255), 3, 8);
  }
  // CV_HOUGH_PROBABILISTIC MODE
  seq_lines = cvHoughLines2(edge_image, storage, CV_HOUGH_PROBABILISTIC,
                            1, CV_PI / 180, 80, 30, 3);
  for (int k = 0; k < seq_lines->total; ++k) {
    CvPoint* line = reinterpret_cast<CvPoint*>(cvGetSeqElem(seq_lines, k));
    cvLine(lane_gray_image, line[0], line[1], cvScalar(0), 3, 8);
  }
  cvReleaseMemStorage(&storage);
  cvReleaseImage(&gray_image);
  cvReleaseImage(&img_32f);
  cvReleaseImage(&diff_x);
  cvReleaseImage(&diff_y);
  cvReleaseImage(&mag);
  cvReleaseImage(&ori);
  cvReleaseImage(&roi_image);
  cvReleaseImage(&lane_gray_image);
  cvReleaseImage(&edge_image);
  cvReleaseImage(&mask);
  return result_image;
}

int _tmain(int argc, _TCHAR* argv[]) {
  RasPiCamera rpic = RasPiCamera::RasPiCamera(kCameraAddr, kCameraPort, kDebug);
  puts("Waiting for camera preview. It takes about 2 seconds.");
  while (TRUE) {
	//std::cerr << rpic.get_status() << " kOK=" << RasPiCamera::kOK << " kErr=" << RasPiCamera::kError << " kEnd=" << RasPiCamera::kEnd << "\n";
    if (rpic.get_status() != RasPiCamera::kOK)
      return EXIT_FAILURE;
    IplImage* source_image = rpic.GetImage();
    if (source_image == NULL) {
      Sleep(kImageWait);
      continue;
    }
    IplImage* result_image = ProcessImage(source_image);
    // Create windows and show the images.
    cvNamedWindow("source_image", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("result_image", CV_WINDOW_AUTOSIZE);
    cvShowImage("source_image", source_image);
    cvShowImage("result_image", result_image);
    // Mouse callback for color infomration of source_image.
    cvSetMouseCallback("source_image", OnMouseEvent,
                       static_cast<void*>(source_image));
    // Keyboard input must be received after images are activated.
    // OpenCv requires some time for showing the images.
    int c = cvWaitKey(10);
    cvReleaseImage(&source_image);
    cvReleaseImage(&result_image);
    if (c == VK_ESCAPE || c == 'q') {
      rpic.RequestSerial("q", 1);
      continue;
    }
    switch (c) {
      case (VK_UP << 16) :
        printf("Pressed UP.\n");
        rpic.RequestSerial("a", 1);
        break;
      case (VK_DOWN << 16) :
        printf("Pressed DOWN.\n");
        rpic.RequestSerial("b", 1);
        break;
      case (VK_RIGHT << 16) :
        printf("Pressed RIGHT.\n");
        rpic.RequestSerial("f", 1);
        break;
      case (VK_LEFT << 16) :
        printf("Pressed LEFT.\n");
        rpic.RequestSerial("d", 1);
        break;
/*
      case 'c':
        printf("Pressed STOP.\n");
        rpic.RequestSerial("c", 1);
        break;
      case 'e':
        printf("Pressed STRAIGHT.\n");
        rpic.RequestSerial("e", 1);
        break;
      case '+':
        printf("Pressed +.");
        rpic.RequestSerial("+", 1);
        break;
*/
      default:
        break;
    }
  }
  cvDestroyAllWindows();
  return EXIT_SUCCESS;
}
