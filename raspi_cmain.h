// Copyright 2016

#ifndef RASPICAMERA_RASPI_CMAIN_H_
#define RASPICAMERA_RASPI_CMAIN_H_

// struct used to store information about pixels of a certain color.
struct ColorDetect {
  // number of pixels of the corresponding color
  int count;
  // the average x coordinate of the corresponding color
  int average_x;
  // the average y coordinate of the corresponding color
  int average_y;
};

#endif  // RASPICAMERA_RASPI_CMAIN_H_
